/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server.art;

import static com.android.server.art.model.ArtFlags.ScheduleStatus;
import static com.android.server.art.proto.PreRebootStats.Status;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.os.Binder;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.PersistableBundle;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.provider.DeviceConfig;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.ArtServiceJobInterface;
import com.android.server.art.prereboot.PreRebootDriver;
import com.android.server.art.prereboot.PreRebootStatsReporter;

import java.time.Duration;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/**
 * The Pre-reboot Dexopt job.
 *
 * During Pre-reboot Dexopt, the old version of this code is run.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootDexoptJob implements ArtServiceJobInterface {
    /**
     * "android" is the package name for a <service> declared in
     * frameworks/base/core/res/AndroidManifest.xml
     */
    private static final String JOB_PKG_NAME = Utils.PLATFORM_PACKAGE_NAME;
    /** An arbitrary number. Must be unique among all jobs owned by the system uid. */
    public static final int JOB_ID = 27873781;

    @NonNull private final Injector mInjector;

    // Job state variables. The monitor of `this` is notified when `mRunningJob` is changed.
    @GuardedBy("this") @Nullable private CompletableFuture<Void> mRunningJob = null;
    @GuardedBy("this") @Nullable private CancellationSignal mCancellationSignal = null;

    /** Whether `mRunningJob` is running from the job scheduler's perspective. */
    @GuardedBy("this") private boolean mIsRunningJobKnownByJobScheduler = false;

    /** The slot that contains the OTA update, "_a" or "_b", or null for a Mainline update. */
    @GuardedBy("this") @Nullable private String mOtaSlot = null;

    /** Whether to map/unmap snapshots. Only applicable to an OTA update. */
    @GuardedBy("this") private boolean mMapSnapshotsForOta = false;

    /**
     * Offloads `onStartJob` and `onStopJob` calls from the main thread while keeping the execution
     * order as the main thread does.
     */
    @NonNull
    private final ThreadPoolExecutor mSerializedExecutor =
            new ThreadPoolExecutor(1 /* corePoolSize */, 1 /* maximumPoolSize */,
                    60 /* keepAliveTime */, TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>());

    // Mutations to the global state of Pre-reboot Dexopt, including mounts, staged files, and
    // stats, should only be done when there is no job running and the `this` lock is held, or by
    // the job itself.

    public PreRebootDexoptJob(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public PreRebootDexoptJob(@NonNull Injector injector) {
        mInjector = injector;
        // Recycle the thread if it's not used for `keepAliveTime`.
        mSerializedExecutor.allowsCoreThreadTimeOut();
    }

    @Override
    public boolean onStartJob(
            @NonNull BackgroundDexoptJobService jobService, @NonNull JobParameters params) {
        mSerializedExecutor.execute(() -> onStartJobImpl(jobService, params));
        // "true" means the job will continue running until `jobFinished` is called.
        return true;
    }

    @VisibleForTesting
    public synchronized void onStartJobImpl(
            @NonNull BackgroundDexoptJobService jobService, @NonNull JobParameters params) {
        JobInfo pendingJob = mInjector.getJobScheduler().getPendingJob(JOB_ID);
        if (pendingJob == null
                || !params.getExtras().getString("ticket").equals(
                        pendingJob.getExtras().getString("ticket"))) {
            // Job expired. We can only get here due to a race, and this should be very rare.
            Utils.check(!mIsRunningJobKnownByJobScheduler);
            return;
        }

        mIsRunningJobKnownByJobScheduler = true;
        @SuppressWarnings("GuardedBy") // https://errorprone.info/bugpattern/GuardedBy#limitations
        Runnable onJobFinishedLocked = () -> {
            Utils.check(mIsRunningJobKnownByJobScheduler);
            mIsRunningJobKnownByJobScheduler = false;
            // If it failed, it means something went wrong, so we don't reschedule the job because
            // it will likely fail again. If it's cancelled, the job will be rescheduled because the
            // return value of `onStopJob` will be respected, and this call will be ignored.
            jobService.jobFinished(params, false /* wantsReschedule */);
        };
        // No need to handle exceptions thrown by the future because exceptions are handled inside
        // the future itself.
        startLocked(onJobFinishedLocked);
    }

    @Override
    public boolean onStopJob(@NonNull JobParameters params) {
        mSerializedExecutor.execute(() -> onStopJobImpl(params));
        // "true" means to execute again with the default retry policy.
        return true;
    }

    @VisibleForTesting
    public synchronized void onStopJobImpl(@NonNull JobParameters params) {
        if (mIsRunningJobKnownByJobScheduler) {
            cancelGivenLocked(mRunningJob, false /* expectInterrupt */);
        }
    }

    /**
     * Notifies this class that an update (OTA or Mainline) is ready.
     *
     * @param otaSlot The slot that contains the OTA update, "_a" or "_b", or null for a Mainline
     *         update.
     */
    public synchronized @ScheduleStatus int onUpdateReady(@Nullable String otaSlot) {
        cancelAnyLocked();
        resetLocked();
        updateOtaSlotLocked(otaSlot);
        mMapSnapshotsForOta = true;
        return scheduleLocked();
    }

    /**
     * Same as above, but starts the job immediately, instead of going through the job scheduler.
     *
     * @param mapSnapshotsForOta whether to map/unmap snapshots. Only applicable to an OTA update.
     * @return The future of the job, or null if Pre-reboot Dexopt is not enabled.
     */
    @Nullable
    public synchronized CompletableFuture<Void> onUpdateReadyStartNow(
            @Nullable String otaSlot, boolean mapSnapshotsForOta) {
        cancelAnyLocked();
        resetLocked();
        updateOtaSlotLocked(otaSlot);
        mMapSnapshotsForOta = mapSnapshotsForOta;
        if (!isEnabled()) {
            mInjector.getStatsReporter().recordJobNotScheduled(
                    Status.STATUS_NOT_SCHEDULED_DISABLED, isOtaUpdate());
            return null;
        }
        mInjector.getStatsReporter().recordJobScheduled(false /* isAsync */, isOtaUpdate());
        return startLocked(null /* onJobFinishedLocked */);
    }

    public synchronized void test() {
        cancelAnyLocked();
        mInjector.getPreRebootDriver().test();
    }

    /** @see #cancelGivenLocked */
    public synchronized void cancelGiven(
            @NonNull CompletableFuture<Void> job, boolean expectInterrupt) {
        cancelGivenLocked(job, expectInterrupt);
    }

    /** @see #cancelAnyLocked */
    public synchronized void cancelAny() {
        cancelAnyLocked();
    }

    @VisibleForTesting
    public synchronized void waitForRunningJob() {
        while (mRunningJob != null) {
            try {
                this.wait();
            } catch (InterruptedException e) {
                AsLog.wtf("Interrupted", e);
            }
        }
    }

    @VisibleForTesting
    public synchronized boolean hasRunningJob() {
        return mRunningJob != null;
    }

    @GuardedBy("this")
    private @ScheduleStatus int scheduleLocked() {
        if (this != BackgroundDexoptJobService.getJob(JOB_ID)) {
            throw new IllegalStateException("This job cannot be scheduled");
        }

        if (!isEnabled()) {
            mInjector.getStatsReporter().recordJobNotScheduled(
                    Status.STATUS_NOT_SCHEDULED_DISABLED, isOtaUpdate());
            return ArtFlags.SCHEDULE_DISABLED_BY_SYSPROP;
        }

        String ticket = UUID.randomUUID().toString();
        PersistableBundle extras = new PersistableBundle(1 /* capacity */);
        extras.putString("ticket", ticket);
        JobInfo info = new JobInfo
                               .Builder(JOB_ID,
                                       new ComponentName(JOB_PKG_NAME,
                                               BackgroundDexoptJobService.class.getName()))
                               .setExtras(extras)
                               .setRequiresDeviceIdle(true)
                               .setRequiresCharging(true)
                               .setRequiresBatteryNotLow(true)
                               // The latency is to wait for update_engine to finish.
                               .setMinimumLatency(Duration.ofMinutes(10).toMillis())
                               .build();

        /* @JobScheduler.Result */ int result;

        // This operation requires the uid to be "system" (1000).
        long identityToken = Binder.clearCallingIdentity();
        try {
            result = mInjector.getJobScheduler().schedule(info);
        } finally {
            Binder.restoreCallingIdentity(identityToken);
        }

        if (result == JobScheduler.RESULT_SUCCESS) {
            AsLog.i("Pre-reboot Dexopt Job scheduled");
            mInjector.getStatsReporter().recordJobScheduled(true /* isAsync */, isOtaUpdate());
            return ArtFlags.SCHEDULE_SUCCESS;
        } else {
            AsLog.i("Failed to schedule Pre-reboot Dexopt Job");
            mInjector.getStatsReporter().recordJobNotScheduled(
                    Status.STATUS_NOT_SCHEDULED_JOB_SCHEDULER, isOtaUpdate());
            return ArtFlags.SCHEDULE_JOB_SCHEDULER_FAILURE;
        }
    }

    @GuardedBy("this")
    private void unscheduleLocked() {
        if (this != BackgroundDexoptJobService.getJob(JOB_ID)) {
            throw new IllegalStateException("This job cannot be unscheduled");
        }

        // This operation requires the uid to be "system" (1000).
        long identityToken = Binder.clearCallingIdentity();
        try {
            mInjector.getJobScheduler().cancel(JOB_ID);
        } finally {
            Binder.restoreCallingIdentity(identityToken);
        }
    }

    /**
     * The future returns true if the job is cancelled by the job scheduler.
     *
     * Can only be called when there is no running job.
     */
    @GuardedBy("this")
    @NonNull
    private CompletableFuture<Void> startLocked(@Nullable Runnable onJobFinishedLocked) {
        Utils.check(mRunningJob == null);

        String otaSlot = mOtaSlot;
        boolean mapSnapshotsForOta = mMapSnapshotsForOta;
        var cancellationSignal = mCancellationSignal = new CancellationSignal();
        mRunningJob = new CompletableFuture().runAsync(() -> {
            markHasStarted(true);
            try {
                mInjector.getPreRebootDriver().run(otaSlot, mapSnapshotsForOta, cancellationSignal);
            } catch (RuntimeException e) {
                AsLog.e("Fatal error", e);
            } finally {
                synchronized (this) {
                    if (onJobFinishedLocked != null) {
                        try {
                            onJobFinishedLocked.run();
                        } catch (RuntimeException e) {
                            AsLog.wtf("Unexpected exception", e);
                        }
                    }
                    mRunningJob = null;
                    mCancellationSignal = null;
                    this.notifyAll();
                }
            }
        });
        this.notifyAll();
        return mRunningJob;
    }

    /**
     * Cancels the given job and waits for it to exit, if it's running. Temporarily releases the
     * lock when waiting for the job to exit.
     *
     * When this method exits, it's guaranteed that the given job is not running, but another job
     * might be running.
     *
     * @param expectInterrupt if true, this method returns immediately when the thread is
     *         interrupted, with no guarantee on the job state
     */
    @GuardedBy("this")
    private void cancelGivenLocked(@NonNull CompletableFuture<Void> job, boolean expectInterrupt) {
        while (mRunningJob == job) {
            if (!mCancellationSignal.isCanceled()) {
                mCancellationSignal.cancel();
                AsLog.i("Job cancelled");
            }
            try {
                this.wait();
            } catch (InterruptedException e) {
                if (expectInterrupt) {
                    return;
                }
                AsLog.wtf("Interrupted", e);
            }
        }
    }

    /**
     * Cancels any running job, prevents the pending job (if any) from being started by the job
     * scheduler, and waits for the running job to exit. Temporarily releases the lock when waiting
     * for the job to exit.
     *
     * When this method exits, it's guaranteed that no job is running.
     */
    @GuardedBy("this")
    private void cancelAnyLocked() {
        unscheduleLocked();
        while (mRunningJob != null) {
            if (!mCancellationSignal.isCanceled()) {
                mCancellationSignal.cancel();
                AsLog.i("Job cancelled");
            }
            try {
                this.wait();
            } catch (InterruptedException e) {
                AsLog.wtf("Interrupted", e);
            }
        }
    }

    @GuardedBy("this")
    private void updateOtaSlotLocked(@Nullable String value) {
        Utils.check(value == null || value.equals("_a") || value.equals("_b"));
        // It's not possible that this method is called with two different slots.
        Utils.check(mOtaSlot == null || value == null || Objects.equals(mOtaSlot, value));
        // An OTA update has a higher priority than a Mainline update. When there are both a pending
        // OTA update and a pending Mainline update, the system discards the Mainline update on the
        // reboot.
        if (mOtaSlot == null && value != null) {
            mOtaSlot = value;
        }
    }

    private boolean isEnabled() {
        boolean syspropEnable =
                SystemProperties.getBoolean("dalvik.vm.enable_pr_dexopt", false /* def */);
        boolean deviceConfigEnable = mInjector.getDeviceConfigBoolean(
                DeviceConfig.NAMESPACE_RUNTIME, "enable_pr_dexopt", false /* defaultValue */);
        boolean deviceConfigForceDisable =
                mInjector.getDeviceConfigBoolean(DeviceConfig.NAMESPACE_RUNTIME,
                        "force_disable_pr_dexopt", false /* defaultValue */);
        if ((!syspropEnable && !deviceConfigEnable) || deviceConfigForceDisable) {
            AsLog.i(String.format(
                    "Pre-reboot Dexopt Job is not enabled (sysprop:dalvik.vm.enable_pr_dexopt=%b, "
                            + "device_config:enable_pr_dexopt=%b, "
                            + "device_config:force_disable_pr_dexopt=%b)",
                    syspropEnable, deviceConfigEnable, deviceConfigForceDisable));
            return false;
        }
        // If `pm.dexopt.disable_bg_dexopt` is set, the user probably means to disable any dexopt
        // jobs in the background.
        if (SystemProperties.getBoolean("pm.dexopt.disable_bg_dexopt", false /* def */)) {
            AsLog.i("Pre-reboot Dexopt Job is disabled by system property "
                    + "'pm.dexopt.disable_bg_dexopt'");
            return false;
        }
        return true;
    }

    public boolean isAsyncForOta() {
        return SystemProperties.getBoolean("dalvik.vm.pr_dexopt_async_for_ota", false /* def */);
    }

    @GuardedBy("this")
    private void resetLocked() {
        mInjector.getStatsReporter().delete();
        if (hasStarted()) {
            try {
                mInjector.getArtd().cleanUpPreRebootStagedFiles();
            } catch (ServiceSpecificException | RemoteException e) {
                AsLog.e("Failed to clean up obsolete Pre-reboot staged files", e);
            }
            markHasStarted(false);
        }
    }

    /**
     * Whether the job has started at least once, meaning the device is expected to have staged
     * files, no matter it succeed, failed, or cancelled.
     *
     * This flag is survives across system server restarts, but not device reboots.
     */
    public boolean hasStarted() {
        return SystemProperties.getBoolean("dalvik.vm.pre-reboot.has-started", false /* def */);
    }

    private void markHasStarted(boolean value) {
        ArtJni.setProperty("dalvik.vm.pre-reboot.has-started", String.valueOf(value));
    }

    @GuardedBy("this")
    private boolean isOtaUpdate() {
        return mOtaSlot != null;
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;
        }

        @NonNull
        public JobScheduler getJobScheduler() {
            return Objects.requireNonNull(mContext.getSystemService(JobScheduler.class));
        }

        @NonNull
        public PreRebootDriver getPreRebootDriver() {
            return new PreRebootDriver(mContext);
        }

        @NonNull
        public PreRebootStatsReporter getStatsReporter() {
            return new PreRebootStatsReporter();
        }

        @NonNull
        public IArtd getArtd() {
            return ArtdRefCache.getInstance().getArtd();
        }

        // Wrap `DeviceConfig` to avoid mocking it directly in tests. `DeviceConfig` backs
        // read-write Trunk Stable flags used by the framework.
        @NonNull
        public boolean getDeviceConfigBoolean(
                @NonNull String namespace, @NonNull String name, boolean defaultValue) {
            return DeviceConfig.getBoolean(namespace, name, defaultValue);
        }
    }
}
