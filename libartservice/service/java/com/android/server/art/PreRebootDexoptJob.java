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

import static com.android.server.art.IDexoptChrootSetup.CHROOT_DIR;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.SuppressLint;
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
import android.os.UpdateEngine;
import android.provider.DeviceConfig;

import androidx.annotation.ChecksSdkIntAtLeast;
import androidx.annotation.RequiresApi;

import com.android.art.rw.flags.Flags;
import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;
import com.android.modules.utils.build.SdkLevel;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.ArtFlags.ScheduleStatus;
import com.android.server.art.model.ArtServiceJobInterface;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.prereboot.PreRebootDriver;
import com.android.server.art.prereboot.PreRebootDriver.PreRebootResult;
import com.android.server.art.prereboot.PreRebootStatsReporter;
import com.android.server.art.proto.PreRebootStats.FailureReason;
import com.android.server.art.proto.PreRebootStats.Status;
import com.android.server.art.utils.ArtdRefCache;
import com.android.server.art.utils.AsLog;
import com.android.server.art.utils.AsyncExecutor;
import com.android.server.art.utils.Utils;
import com.android.server.art.utils.Utils.Clock;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

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

    private static final long UPDATE_ENGINE_TIMEOUT_MS = 30000;

    @NonNull private final Injector mInjector;

    // Job state variables.
    // The monitor of `this` is notified when `mRunningJob` or `mIsUpdateEngineReady` is changed.
    // Also, an optimization to make `triggerUpdateEnginePostinstallAndWait` return early, if
    // `mCancellationSignal` is fired **before `triggerUpdateEnginePostinstallAndWait` returns**, it
    // should be guaranteed that the monitor of `this` is notified when it happens.
    // `mRunningJob` and `mCancellationSignal` have the same nullness.
    @GuardedBy("this") @Nullable private CompletableFuture<Void> mRunningJob = null;
    @GuardedBy("this") @Nullable private CancellationSignal mCancellationSignal = null;
    /** Whether update_engine has mapped snapshot devices. Only applicable to an OTA update. */
    @GuardedBy("this") private boolean mIsUpdateEngineReady = false;
    /** The start time of the current job, or 0 if there's no job running. */
    @GuardedBy("this") private long mJobStartTimeMillis = 0;
    /** The time limit of the current job, or 0 if there's no time limit. */
    @GuardedBy("this") private long mTimeLimitMillis = 0;

    /** Whether `mRunningJob` is running from the job scheduler's perspective. */
    @GuardedBy("this") private boolean mIsRunningJobKnownByJobScheduler = false;

    /** The slot that contains the OTA update, "_a" or "_b", or null for a Mainline update. */
    @GuardedBy("this") @Nullable private String mOtaSlot = null;

    /**
     * Offloads `onStartJob` and `onStopJob` calls from the main thread while keeping the execution
     * order as the main thread does.
     * Also offloads `onUpdateReady` calls from the package manager thread. We reuse this executor
     * just for simplicity. The execution order does not matter.
     */
    @NonNull
    private final ThreadPoolExecutor mSerializedExecutor =
            new ThreadPoolExecutor(1 /* corePoolSize */, 1 /* maximumPoolSize */,
                    60 /* keepAliveTime */, TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>());

    /**
     * A separate thread for executing `mRunningJob`. We avoid using any known thread / thread pool
     * such as {@link java.util.concurrent.ForkJoinPool} and {@link
     * com.android.server.art.utils.AsyncExecutor} because we don't want to block other things that
     * use known threads / thread pools by this long running job.
     */
    @NonNull
    private final ThreadPoolExecutor mExecutor =
            new ThreadPoolExecutor(1 /* corePoolSize */, 1 /* maximumPoolSize */,
                    60 /* keepAliveTime */, TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>());

    // Mutations to the global state of Pre-reboot Dexopt, including mounts, staged files, and
    // stats, should only be done when there is no job running and the `this` lock is held, or by
    // the job itself.

    public PreRebootDexoptJob(@NonNull Context context, @NonNull ArtManagerLocal artManagerLocal) {
        this(new Injector(context, artManagerLocal));
    }

    @VisibleForTesting
    public PreRebootDexoptJob(@NonNull Injector injector) {
        mInjector = injector;
        // Recycle the thread if it's not used for `keepAliveTime`.
        mSerializedExecutor.allowsCoreThreadTimeOut();
        mExecutor.allowsCoreThreadTimeOut();
        maybeCleanUpChrootAsyncForStartup();
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
            // There can be four cases when we reach here:
            // 1. The job has completed: No need to reschedule.
            // 2. The job failed: It means something went wrong, so we don't reschedule the job
            //    because it will likely fail again.
            // 3. The job was killed by update_engine, probably because the OTA was revoked: We
            //    should definitely give up.
            // 4. The job was cancelled by the job scheduler: The job will be rescheduled regardless
            //    of the arguments we pass here because the return value of `onStopJob` will be
            //    respected, and this call will be ignored.
            // Therefore, we can always pass `false` to the `wantsReschedule` parameter.
            jobService.jobFinished(params, false /* wantsReschedule */);
        };
        startLocked(onJobFinishedLocked, getSnapshotMode(mOtaSlot, false /* isUpdateEngineReady */),
                ReasonMapping.REASON_PRE_REBOOT_DEXOPT)
                .exceptionally(t -> {
                    AsLog.wtf("Fatal error", t);
                    return null;
                });
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
     * @param otaSlot the slot that contains the OTA update, "_a" or "_b", or null for a Mainline
     *         update.
     * @param isUpdateEngineReady whether update_engine has mapped snapshot devices. Only applicable
     *         to an OTA update.
     */
    public CompletableFuture<OnUpdateReadyResponse> onUpdateReady(
            @Nullable String otaSlot, boolean isUpdateEngineReady, JobSynchronicity synchronicity) {
        // `onUpdateReadyImpl` can take time, especially on `resetLocked` when there are staged
        // files from a previous run to be cleaned up, so we put it on a separate thread.
        return CompletableFuture.supplyAsync(() -> {
            return onUpdateReadyImpl(otaSlot, isUpdateEngineReady, synchronicity);
        }, mSerializedExecutor);
    }

    /** For internal use only. */
    private synchronized OnUpdateReadyResponse onUpdateReadyImpl(
            @Nullable String otaSlot, boolean isUpdateEngineReady, JobSynchronicity synchronicity) {
        cancelAnyLocked();
        resetLocked();
        updateOtaSlotLocked(otaSlot);

        long hybridModeSyncTimeLimitMillis = getHybridModeSyncTimeLimitMillis();

        if (synchronicity == JobSynchronicity.AUTO) {
            if (isOtaUpdate()) {
                if (isAsyncForOta()) {
                    synchronicity =
                            Flags.hybridPreRebootDexopt() && hybridModeSyncTimeLimitMillis > 0
                            ? JobSynchronicity.HYBRID
                            : JobSynchronicity.ASYNC;
                } else {
                    synchronicity = JobSynchronicity.SYNC;
                }
            } else {
                synchronicity = JobSynchronicity.ASYNC;
            }
        }

        if (!isEnabled()) {
            mInjector.getStatsReporter().recordJobNotScheduled(
                    Status.STATUS_NOT_SCHEDULED_DISABLED, synchronicity, isOtaUpdate());
            return new OnUpdateReadyResponse(
                    null /* synchronousJob */, null /* asynchronousJobScheduling */);
        }

        if (synchronicity == JobSynchronicity.ASYNC) {
            var asynchronousJobScheduling =
                    CompletableFuture.completedFuture(scheduleLocked(synchronicity));
            return new OnUpdateReadyResponse(null /* synchronousJob */, asynchronousJobScheduling);
        }

        if (synchronicity == JobSynchronicity.HYBRID) {
            mTimeLimitMillis = hybridModeSyncTimeLimitMillis;
            mInjector.getStatsReporter().recordJobScheduled(JobSynchronicity.HYBRID, isOtaUpdate());
            var synchronousJobTimedOut = new AtomicBoolean(false);
            var asynchronousJobScheduling = new CompletableFuture<@ScheduleStatus Integer>();
            Runnable onJobFinishedLocked = () -> {
                if (!synchronousJobTimedOut.get()) {
                    // Finished, failed, or cancelled before the timeout. Don't schedule the
                    // asynchronous job.
                    asynchronousJobScheduling.complete(null);
                    return;
                }
                try {
                    asynchronousJobScheduling.complete(scheduleLocked(JobSynchronicity.HYBRID));
                } catch (Exception e) {
                    asynchronousJobScheduling.completeExceptionally(e);
                }
            };
            var synchronousJob =
                    startLocked(onJobFinishedLocked, getSnapshotMode(otaSlot, isUpdateEngineReady),
                            ReasonMapping.REASON_PRE_REBOOT_DEXOPT_SYNC);
            mInjector.getAsyncExecutor().executeDelayed(() -> {
                synchronousJobTimedOut.set(true);
                cancelGiven(synchronousJob, false /* expectInterrupt */);
            }, mTimeLimitMillis);
            return new OnUpdateReadyResponse(synchronousJob, asynchronousJobScheduling);
        }

        mInjector.getStatsReporter().recordJobScheduled(JobSynchronicity.SYNC, isOtaUpdate());
        var synchronousJob = startLocked(null /* onJobFinishedLocked */,
                getSnapshotMode(otaSlot, isUpdateEngineReady),
                ReasonMapping.REASON_PRE_REBOOT_DEXOPT);
        return new OnUpdateReadyResponse(synchronousJob, null /* asynchronousJobScheduling */);
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

    /**
     * Returns the progress of the running job, or null if there is no running job.
     */
    public synchronized Float getProgress() {
        if (mRunningJob == null) {
            return null;
        }
        OperationProgress progress = mInjector.getStatsReporter().getProgress();
        float fraction =
                progress.getTotal() != 0 ? (float) progress.getCurrent() / progress.getTotal() : 0F;
        if (mTimeLimitMillis > 0) {
            float timeFraction = Math.min(
                    (float) (mInjector.getClock().currentTimeMillis() - mJobStartTimeMillis)
                            / mTimeLimitMillis,
                    1F);
            return Math.max(fraction, timeFraction);
        }
        return fraction;
    }

    /** Cleans up chroot if it exists. Only expected to be called on system server startup. */
    private synchronized void maybeCleanUpChrootAsyncForStartup() {
        if (!Files.exists(Paths.get(CHROOT_DIR))) {
            return;
        }
        // We only get here when there was a system server restart (probably due to a crash). In
        // this case, it's possible that a previous Pre-reboot Dexopt job didn't end normally and
        // left over a chroot, so we need to clean it up.
        // We assign this operation to `mRunningJob` to block other operations on their calls to
        // `cancelAnyLocked`.
        // `mCancellationSignal` is a placeholder and the signal actually ignored. It's created just
        // for keeping the invariant that `mRunningJob` and `mCancellationSignal` have the same
        // nullness, to make other code simpler.
        mCancellationSignal = new CancellationSignal();
        mRunningJob = new CompletableFuture().runAsync(() -> {
            try {
                mInjector.getPreRebootDriver().maybeCleanUpChroot();
            } finally {
                synchronized (this) {
                    mRunningJob = null;
                    mCancellationSignal = null;
                    this.notifyAll();
                }
            }
        }, mExecutor);
        this.notifyAll();
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
    private @ScheduleStatus int scheduleLocked(JobSynchronicity synchronicity) {
        if (this != BackgroundDexoptJobService.getJob(JOB_ID)) {
            throw new IllegalStateException("This job cannot be scheduled");
        }

        if (synchronicity != JobSynchronicity.ASYNC && synchronicity != JobSynchronicity.HYBRID) {
            throw new IllegalArgumentException("Invalid synchronicity: " + synchronicity);
        }

        boolean continueFromPrevious = synchronicity == JobSynchronicity.HYBRID;

        if (!isEnabled()) {
            mInjector.getStatsReporter().recordJobNotScheduled(Status.STATUS_NOT_SCHEDULED_DISABLED,
                    synchronicity, isOtaUpdate(), continueFromPrevious);
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
            mInjector.getStatsReporter().recordJobScheduled(
                    synchronicity, isOtaUpdate(), continueFromPrevious);
            return ArtFlags.SCHEDULE_SUCCESS;
        } else {
            AsLog.i("Failed to schedule Pre-reboot Dexopt Job");
            mInjector.getStatsReporter().recordJobNotScheduled(
                    Status.STATUS_NOT_SCHEDULED_JOB_SCHEDULER, synchronicity, isOtaUpdate(),
                    continueFromPrevious);
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
    private CompletableFuture<Void> startLocked(
            @Nullable Runnable onJobFinishedLocked, SnapshotMode snapshotMode, String reason) {
        Utils.check(mRunningJob == null);

        String otaSlot = mOtaSlot;
        var cancellationSignal = mCancellationSignal = new CancellationSignal();
        mJobStartTimeMillis = mInjector.getClock().currentTimeMillis();
        mIsUpdateEngineReady = false;
        PreRebootStatsReporter statsReporter = mInjector.getStatsReporter();
        statsReporter.recordJobStarted();
        mRunningJob = new CompletableFuture().runAsync(() -> {
            try {
                if (snapshotMode == SnapshotMode.UPDATE_ENGINE) {
                    Utils.check(otaSlot != null);
                    triggerUpdateEnginePostinstallAndWait();
                    synchronized (this) {
                        // This check is not strictly necessary, but is an optimization to return
                        // early.
                        if (mCancellationSignal.isCanceled()) {
                            statsReporter.recordJobEnded(
                                    new PreRebootResult(Status.STATUS_FINISHED));
                            return;
                        }
                        Utils.check(mIsUpdateEngineReady);
                    }
                }
                PreRebootResult result = mInjector.getPreRebootDriver().run(
                        otaSlot, snapshotMode == SnapshotMode.SELF, cancellationSignal, reason);
                statsReporter.recordJobEnded(result);
            } catch (UpdateEngineException e) {
                AsLog.wtf("update_engine error", e);
                statsReporter.recordJobEnded(new PreRebootResult(
                        Status.STATUS_FAILED, FailureReason.FAILURE_UPDATE_ENGINE));
            } catch (RuntimeException e) {
                statsReporter.recordJobEnded(new PreRebootResult(Status.STATUS_FAILED));
                throw e;
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
                    mIsUpdateEngineReady = false;
                    mJobStartTimeMillis = 0;
                    mTimeLimitMillis = 0;
                    this.notifyAll();
                }
            }
        }, mExecutor);
        this.notifyAll();
        return mRunningJob;
    }

    private void triggerUpdateEnginePostinstallAndWait() throws UpdateEngineException {
        if (!mInjector.isAtLeastB()) {
            // Should never happen.
            throw new UnsupportedOperationException();
        }
        // When we need snapshot devices, we trigger update_engine postinstall. update_engine will
        // map the snapshot devices for us and run the postinstall script, which will call
        // `pm art on-ota-staged --start` to notify us that the snapshot device are ready.
        // See art/libartservice/service/README.internal.md for typical flows.
        AsLog.i("Waiting for update_engine to map snapshots...");
        try {
            mInjector.getUpdateEngine().triggerPostinstall("system" /* partition */);
        } catch (ServiceSpecificException e) {
            // From system/update_engine/common/error_code.h.
            final int POSTINTALL_RUNNER_ERROR = 5;
            final int UPDATE_PROCESSING = 65;
            // update_engine threw "Already processing an update, cancel it first."
            // It probably means a newer update is superseding the original update.
            boolean updateSuperseded = e.errorCode == UPDATE_PROCESSING;
            // update_engine threw "Postinstall action did not run. OTA update must first reach the
            // Postinstall phase(which verfies that all partitions can be mounted) before calling
            // TriggerPostinstall".
            // It probably means the original update has been revoked.
            boolean updateRevoked =
                    e.errorCode == POSTINTALL_RUNNER_ERROR
                            && e.getMessage().contains("TriggerPostinstall");
            if (updateSuperseded || updateRevoked) {
                AsLog.i("update_engine error ignored: " + e.getMessage());
                synchronized (this) {
                    mCancellationSignal.cancel();
                }
                return;
            }
            throw new UpdateEngineException("Failed to trigger postinstall: " + e.getMessage());
        }
        long startTime = System.currentTimeMillis();
        synchronized (this) {
            while (true) {
                if (mIsUpdateEngineReady || mCancellationSignal.isCanceled()) {
                    return;
                }
                long remainingTime =
                        UPDATE_ENGINE_TIMEOUT_MS - (System.currentTimeMillis() - startTime);
                if (remainingTime <= 0) {
                    throw new UpdateEngineException("Timed out while waiting for update_engine");
                }
                try {
                    this.wait(remainingTime);
                } catch (InterruptedException e) {
                    AsLog.wtf("Interrupted", e);
                }
            }
        }
    }

    @Nullable
    public CompletableFuture<Void> notifyUpdateEngineReady() {
        synchronized (this) {
            if (mRunningJob == null) {
                AsLog.e("No waiting job found");
                return null;
            }
            AsLog.i("update_engine finished mapping snapshots");
            mIsUpdateEngineReady = true;
            // Notify triggerUpdateEnginePostinstallAndWait to stop waiting.
            this.notifyAll();
            return mRunningJob;
        }
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
                // This is not strictly necessary, but is an optimization to make
                // `triggerUpdateEnginePostinstallAndWait` return early.
                this.notifyAll();
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
                // This is not strictly necessary, but is an optimization to make
                // `triggerUpdateEnginePostinstallAndWait` return early.
                this.notifyAll();
                AsLog.i("Job cancelled");
            }
            try {
                this.wait();
            } catch (InterruptedException e) {
                AsLog.wtf("Interrupted", e);
            }
        }
        // In Hybrid mode, an asynchronous job is scheduled after the synchronous job times out. If
        // `cancelAnyLocked` is called right after the timeout, the asynchronous job may be
        // scheduled, so we need to unschedule it here.
        unscheduleLocked();
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

    private boolean isAsyncForOta() {
        return false;
    }

    private int getHybridModeSyncTimeLimitMillis() {
        return SystemProperties.getInt(
                "dalvik.vm.pr_dexopt_sync_time_limit_millis", 180000 /* def */);
    }

    @GuardedBy("this")
    private void resetLocked() {
        PreRebootStagedFilesStatus status = null;
        try {
            status = mInjector.getArtd().checkPreRebootStagedFilesStatus();
            if (status != null) {
                mInjector.getArtd().cleanUpPreRebootStagedFiles();
            }
        } catch (ServiceSpecificException | RemoteException e) {
            AsLog.e("Failed to clean up obsolete Pre-reboot staged files", e);
        } finally {
            var statsAfterRebootSession = mInjector.getStatsReporter().new AfterRebootSession();
            statsAfterRebootSession.setExpectFound(false);
            statsAfterRebootSession.recordArtifactsEndStatus(
                    PreRebootStatsReporter.END_STATUS_SUPERSEDED,
                    status != null
                            ? mInjector.getClock().currentTimeMillis() - status.createdAtMillis
                            : 0);
            // Usually does nothing, unless there are pending stats to report.
            statsAfterRebootSession.report();
        }
    }

    /**
     * Returns the age of the staged files and whether they are expired.
     */
    public StagedFilesAge checkStagedFilesAge() {
        int retentionDays = SystemProperties.getInt("dalvik.vm.pr_dexopt_retention", 30 /* def */);
        Duration retentionPeriod = Duration.ofDays(Math.max(retentionDays, 0));
        try {
            PreRebootStagedFilesStatus status =
                    mInjector.getArtd().checkPreRebootStagedFilesStatus();
            if (status == null) {
                return null;
            }
            Duration age = Duration.ofMillis(
                    mInjector.getClock().currentTimeMillis() - status.createdAtMillis);
            return new StagedFilesAge(age, age.compareTo(retentionPeriod) >= 0);
        } catch (ServiceSpecificException | RemoteException e) {
            AsLog.e("Failed to check Pre-reboot staged files status", e);
            // This method is for deciding whether we should keep the staged files. If we get an
            // error here, we will probably get the same when committing the staged files, so
            // there's no point to keep them. We return null here to get them removed.
            return null;
        }
    }

    @GuardedBy("this")
    private boolean isOtaUpdate() {
        return mOtaSlot != null;
    }

    private SnapshotMode getSnapshotMode(@Nullable String otaSlot, boolean isUpdateEngineReady) {
        if (otaSlot == null || isUpdateEngineReady) {
            return SnapshotMode.NONE;
        }
        // The job is for OTA and snapshots are not mapped by update_engine yet. We hit here in two
        // cases:
        // 1. The job is running asynchronously. Either we are on B+, or we are on V and the OEM
        //    sets `dalvik.vm.pr_dexopt_async_for_ota`.
        // 2. The job is running synchronously, initiated by a `pm art pr-dexopt-job --run` command
        //    for local development purposes.
        return mInjector.isAtLeastB() ? SnapshotMode.UPDATE_ENGINE : SnapshotMode.SELF;
    }

    private static class UpdateEngineException extends Exception {
        public UpdateEngineException(@NonNull String message) {
            super(message);
        }
    }

    public record StagedFilesAge(Duration age, boolean isExpired) {}

    public enum JobSynchronicity {
        /** Automatically pick the best synchronicity. */
        AUTO,
        /** Runs the job synchronously. The call is blocked until the entire job is done. */
        SYNC,
        /**
           Runs the job asynchronously, when the device is idle and charging and the battery is not
           low. The call is non-blocking.
         */
        ASYNC,
        /**
          Runs the job synchronously for a period of time, and finishes the rest of the work
          asynchronously, when the device is idle and charging and the battery is not low. The call
          is blocked until the synchronous phase is done.
        */
        HYBRID,
    }

    /**
     * The response of {@link #onUpdateReady}.
     *
     * Either future may be {@code null} depending on the provided {@code synchronicity} and whether
     * Pre-reboot Dexopt is enabled. If both are present, the synchronous job is guaranteed to
     * complete (success or failure) before the scheduling of the asynchronous job takes place.
     *
     * @param synchronousJob the synchronous job
     * @param asynchronousJobScheduling the scheduling of the asynchronous job (not the job itself)
     */
    public record OnUpdateReadyResponse(@Nullable CompletableFuture<Void> synchronousJob,
            @Nullable CompletableFuture<@ScheduleStatus Integer> asynchronousJobScheduling) {}

    /** Whether to map/unmap snapshots for an OTA update and how. */
    private enum SnapshotMode {
        /**
           Snapshots are not needed (i.e., it's a Mainline update) or are already mapped by
           update_engine.
         */
        NONE,
        /** Map/unmap snapshots using update_engine. */
        UPDATE_ENGINE,
        /** Map/unmap snapshots ourselves. For legacy use only. */
        SELF,
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;
        @NonNull private final ArtManagerLocal mArtManagerLocal;

        Injector(@NonNull Context context, @NonNull ArtManagerLocal artManagerLocal) {
            mContext = context;
            mArtManagerLocal = artManagerLocal;
        }

        @NonNull
        public JobScheduler getJobScheduler() {
            return Objects.requireNonNull(mContext.getSystemService(JobScheduler.class));
        }

        @NonNull
        public PreRebootDriver getPreRebootDriver() {
            return new PreRebootDriver(mContext, mArtManagerLocal);
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

        @NonNull
        public UpdateEngine getUpdateEngine() {
            return new UpdateEngine();
        }

        public Clock getClock() {
            return Clock.DEFAULT;
        }

        @ChecksSdkIntAtLeast(api = 36)
        public boolean isAtLeastB() {
            return SdkLevel.isAtLeastB();
        }

        public AsyncExecutor getAsyncExecutor() {
            return AsyncExecutor.getInstance();
        }
    }
}
