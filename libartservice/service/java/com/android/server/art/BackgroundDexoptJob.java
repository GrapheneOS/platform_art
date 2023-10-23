/*
 * Copyright (C) 2022 The Android Open Source Project
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

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.Environment;
import android.os.SystemClock;
import android.os.SystemProperties;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.ArtManagerLocal.ScheduleBackgroundDexoptJobCallback;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.ArtFlags.BatchDexoptPass;
import com.android.server.art.model.ArtFlags.ScheduleStatus;
import com.android.server.art.model.ArtServiceJobInterface;
import com.android.server.art.model.Config;
import com.android.server.art.model.Config.Callback;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.utils.AsLog;
import com.android.server.art.utils.Utils;
import com.android.server.pm.PackageManagerLocal;

import com.google.auto.value.AutoValue;

import java.io.File;
import java.time.Duration;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/** @hide */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class BackgroundDexoptJob implements ArtServiceJobInterface {
    /**
     * "android" is the package name for a <service> declared in
     * frameworks/base/core/res/AndroidManifest.xml
     */
    private static final String JOB_PKG_NAME = Utils.PLATFORM_PACKAGE_NAME;

    @VisibleForTesting public static final long JOB_INTERVAL_MS = TimeUnit.DAYS.toMillis(1);

    @NonNull private final Injector mInjector;

    @GuardedBy("this") @Nullable private CompletableFuture<Result> mRunningJob = null;
    @GuardedBy("this") @Nullable private CancellationSignal mCancellationSignal = null;
    @GuardedBy("this") @NonNull private Optional<Integer> mLastStopReason = Optional.empty();

    /**
     * The time the {@link JobType.BG_DEXOPT} job was scheduled.
     *
     * The time is measured in milliseconds, on a monotonic clock including time spent in sleep.
     */
    @GuardedBy("this") private long mJobScheduledAtMillis = 0;

    /**
     * The completion time of the last finished (either completed or failed, not cancelled) run of
     * the {@link JobType.BG_DEXOPT} job since boot.
     *
     * The time is measured in milliseconds, on a monotonic clock including time spent in sleep.
     */
    @GuardedBy("this") private long mJobLastFinishedAtMillis = 0;

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

    public BackgroundDexoptJob(@NonNull Context context, @NonNull ArtManagerLocal artManagerLocal,
            @NonNull Config config) {
        this(new Injector(context, artManagerLocal, config));
        // Recycle the thread if it's not used for `keepAliveTime`.
        mExecutor.allowsCoreThreadTimeOut();
    }

    @VisibleForTesting
    public BackgroundDexoptJob(@NonNull Injector injector) {
        mInjector = injector;
    }

    /** Handles {@link BackgroundDexoptJobService#onStartJob(JobParameters)}. */
    @SuppressWarnings("FutureReturnValueIgnored") // This future never throws.
    @Override
    public boolean onStartJob(
            @NonNull BackgroundDexoptJobService jobService, @NonNull JobParameters params) {
        JobType jobType = JobType.fromJobId(params.getJobId());
        long jobStartedAtMillis = SystemClock.elapsedRealtime();
        start(jobType).thenAcceptAsync(result -> {
            boolean wantsReschedule = false;

            if (jobType == JobType.BG_DEXOPT) {
                try {
                    writeStats(result, jobStartedAtMillis);
                } catch (RuntimeException e) {
                    // Not expected. Log wtf to surface it.
                    AsLog.wtf("Failed to write stats", e);
                }

                // This is a periodic job, where the interval is specified in the `JobInfo`. "true"
                // means to execute again in the same interval with the default retry policy, while
                // "false" means not to execute again in the same interval but to execute again in
                // the next interval.
                wantsReschedule = result instanceof CompletedResult
                        && ((CompletedResult) result).isCancelled();

                synchronized (this) {
                    if (!wantsReschedule) {
                        mJobLastFinishedAtMillis = SystemClock.elapsedRealtime();
                    }
                }
            }

            // This call will be ignored if `onStopJob` is called.
            jobService.jobFinished(params, wantsReschedule);
        });
        // "true" means the job will continue running until `jobFinished` is called.
        return true;
    }

    /** Handles {@link BackgroundDexoptJobService#onStopJob(JobParameters)}. */
    @Override
    public boolean onStopJob(@NonNull JobParameters params) {
        synchronized (this) {
            mLastStopReason = Optional.of(params.getStopReason());
        }
        cancel();
        // "true" means to execute again in the same interval with the default retry policy.
        return true;
    }

    /** Handles {@link ArtManagerLocal#scheduleBackgroundDexoptJob()}. */
    public @ScheduleStatus int schedule(@NonNull JobType jobType) {
        if (this != BackgroundDexoptJobService.getJob(jobType.getJobId())) {
            throw new IllegalStateException("This job cannot be scheduled");
        }

        start(jobType).thenAcceptAsync(result -> {
            Map<Integer, DexoptResult> dr = null;
            long durationMs = 0L;
            if (result instanceof CompletedResult r) {
                dr = r.dexoptResultByPass();
                for (Long v : r.durationMsByPass().values()) {
                    durationMs += v.longValue();
                }
            }
            mInjector.getPackageManagerLocal().onBgDexoptCompleted(dr, durationMs);
        });

        if (SystemProperties.getBoolean("pm.dexopt.disable_bg_dexopt", false /* def */)) {
            AsLog.i("Job is disabled by system property 'pm.dexopt.disable_bg_dexopt'");
            return ArtFlags.SCHEDULE_DISABLED_BY_SYSPROP;
        }

        // Don't set requires device idle for the post-UR job.
        JobInfo.Builder builder =
                new JobInfo
                        .Builder(jobType.getJobId(),
                                new ComponentName(
                                        JOB_PKG_NAME, BackgroundDexoptJobService.class.getName()))
                        .setRequiresCharging(true)
                        .setRequiresBatteryNotLow(true);

        if (jobType == JobType.BG_DEXOPT) {
            builder.setRequiresDeviceIdle(true).setPeriodic(JOB_INTERVAL_MS);

            Callback<ScheduleBackgroundDexoptJobCallback, Void> callback =
                    mInjector.getConfig().getScheduleBackgroundDexoptJobCallback();
            if (callback != null) {
                Utils.executeAndWait(
                        callback.executor(), () -> { callback.get().onOverrideJobInfo(builder); });
            }
        } else {
            Utils.check(jobType == JobType.POST_UNATTENDED_REBOOT);
            // There are many things going on right after reboot, so we wait for a while to avoid
            // resource contention.
            builder.setMinimumLatency(Duration.ofMinutes(10).toMillis());
        }

        JobInfo info = builder.build();
        if (info.isRequireStorageNotLow()) {
            // See the javadoc of
            // `ArtManagerLocal.ScheduleBackgroundDexoptJobCallback.onOverrideJobInfo` for details.
            throw new IllegalStateException("'setRequiresStorageNotLow' must not be set");
        }

        if (jobType == JobType.BG_DEXOPT) {
            synchronized (this) {
                mJobScheduledAtMillis = SystemClock.elapsedRealtime();
                mJobLastFinishedAtMillis = 0;
            }
        }

        return mInjector.getJobScheduler().schedule(info) == JobScheduler.RESULT_SUCCESS
                ? ArtFlags.SCHEDULE_SUCCESS
                : ArtFlags.SCHEDULE_JOB_SCHEDULER_FAILURE;
    }

    /** Handles {@link ArtManagerLocal#unscheduleBackgroundDexoptJob()}. */
    public void unschedule(@NonNull JobType jobType) {
        if (this != BackgroundDexoptJobService.getJob(jobType.getJobId())) {
            throw new IllegalStateException("This job cannot be unscheduled");
        }

        mInjector.getJobScheduler().cancel(jobType.getJobId());
    }

    @NonNull
    public synchronized CompletableFuture<Result> start(@NonNull JobType jobType) {
        if (mRunningJob != null) {
            AsLog.i("Job is already running");
            return mRunningJob;
        }

        mCancellationSignal = new CancellationSignal();
        mLastStopReason = Optional.empty();
        mRunningJob = new CompletableFuture().supplyAsync(() -> {
            try (var tracing = new Utils.TracingWithTimingLogging(AsLog.getTag(), "jobExecution")) {
                return run(jobType, mCancellationSignal);
            } catch (RuntimeException e) {
                AsLog.wtf("Fatal error", e);
                return new FatalErrorResult();
            } finally {
                synchronized (this) {
                    mRunningJob = null;
                    mCancellationSignal = null;
                }
            }
        }, mExecutor);
        return mRunningJob;
    }

    public synchronized void cancel() {
        if (mRunningJob == null) {
            AsLog.i("Job is not running");
            return;
        }

        mCancellationSignal.cancel();
        AsLog.i("Job cancelled");
    }

    @Nullable
    public synchronized CompletableFuture<Result> get() {
        return mRunningJob;
    }

    @NonNull
    private CompletedResult run(
            @NonNull JobType jobType, @NonNull CancellationSignal cancellationSignal) {
        // Create callbacks to time each pass.
        Map<Integer, Long> startTimeMsByPass = new HashMap<>();
        Map<Integer, Long> durationMsByPass = new HashMap<>();
        Map<Integer, Consumer<OperationProgress>> progressCallbacks = new HashMap<>();
        for (@BatchDexoptPass int pass : ArtFlags.BATCH_DEXOPT_PASSES) {
            progressCallbacks.put(pass, progress -> {
                if (progress.getTotal() == 0) {
                    durationMsByPass.put(pass, 0l);
                } else if (progress.getCurrent() == 0) {
                    startTimeMsByPass.put(pass, SystemClock.uptimeMillis());
                } else if (progress.getCurrent() == progress.getTotal()) {
                    durationMsByPass.put(
                            pass, SystemClock.uptimeMillis() - startTimeMsByPass.get(pass));
                }
            });
        }

        Map<Integer, DexoptResult> dexoptResultByPass;
        try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot()) {
            dexoptResultByPass = mInjector.getArtManagerLocal().dexoptPackages(snapshot,
                    jobType.getReason(), cancellationSignal, Runnable::run, progressCallbacks);
        }

        // For simplicity, we don't support cancelling the following operation in the middle.
        // This is fine because it typically takes only a few seconds.
        if (jobType == JobType.BG_DEXOPT && !cancellationSignal.isCanceled()) {
            // We do the cleanup after dexopt so that it doesn't affect the `getSizeBeforeBytes`
            // field in the result that we send to callbacks. Admittedly, this will cause us to
            // lose some chance to dexopt when the storage is very low, but it's fine because we
            // can still dexopt in the next run.
            //
            // Take a new snapshot since the one used for dexoptPackages above is old by now and
            // new packages may have been installed.
            try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot()) {
                long freedBytes = mInjector.getArtManagerLocal().cleanup(snapshot);
                AsLog.i(String.format("Freed %d bytes", freedBytes));
            }
            cleanupLegacyDexoptFiles();
        }
        return CompletedResult.create(dexoptResultByPass, durationMsByPass);
    }

    private void cleanupLegacyDexoptFiles() {
        new File(Environment.getDataDirectory(), "system/package-dex-usage.list").delete();
        if (Build.VERSION.SDK_INT_FULL >= Build.VERSION_CODES_FULL.BAKLAVA_1) {
            new File(Environment.getDataDirectory(), "system/package-cstats.list").delete();
        }
        // TODO(b/258223472): Also delete "package-dcl.list" and "package-usage.list".
    }

    /**
     * @param jobStartedAtMillis the time the run was started, measured in milliseconds, on a
     *         monotonic clock including time spent in sleep.
     */
    private void writeStats(@NonNull Result result, long jobStartedAtMillis) {
        Optional<Integer> stopReason;
        boolean isFirstRun;
        long jobLatencyMillis;
        synchronized (this) {
            stopReason = mLastStopReason;
            Utils.check(mJobScheduledAtMillis > 0);
            isFirstRun = mJobLastFinishedAtMillis == 0;
            jobLatencyMillis = isFirstRun ? jobStartedAtMillis - mJobScheduledAtMillis
                                          : jobStartedAtMillis - mJobLastFinishedAtMillis;
        }
        if (result instanceof CompletedResult completedResult) {
            BackgroundDexoptJobStatsReporter.reportSuccess(
                    completedResult, stopReason, isFirstRun, jobLatencyMillis);
        } else if (result instanceof FatalErrorResult) {
            BackgroundDexoptJobStatsReporter.reportFailure(isFirstRun, jobLatencyMillis);
        }
    }

    static abstract class Result {}
    static class FatalErrorResult extends Result {}

    @AutoValue
    @SuppressWarnings("AutoValueImmutableFields") // Can't use ImmutableMap because it's in Guava.
    static abstract class CompletedResult extends Result {
        abstract @NonNull Map<Integer, DexoptResult> dexoptResultByPass();
        abstract @NonNull Map<Integer, Long> durationMsByPass();

        @NonNull
        static CompletedResult create(@NonNull Map<Integer, DexoptResult> dexoptResultByPass,
                @NonNull Map<Integer, Long> durationMsByPass) {
            return new AutoValue_BackgroundDexoptJob_CompletedResult(
                    Collections.unmodifiableMap(dexoptResultByPass),
                    Collections.unmodifiableMap(durationMsByPass));
        }

        public boolean isCancelled() {
            return dexoptResultByPass().values().stream().anyMatch(
                    result -> result.getFinalStatus() == DexoptResult.DEXOPT_CANCELLED);
        }
    }

    public enum JobType {
        /** An arbitrary number. Must be unique among all jobs owned by the system uid. */
        BG_DEXOPT(27873780, ReasonMapping.REASON_BG_DEXOPT),
        /** An arbitrary number. Must be unique among all jobs owned by the system uid. */
        POST_UNATTENDED_REBOOT(27873782, ReasonMapping.REASON_POST_UNATTENDED_REBOOT);

        private final int jobId;
        private final @NonNull String reason;

        JobType(int jobId, @NonNull String reason) {
            this.jobId = jobId;
            this.reason = reason;
        }

        public int getJobId() {
            return jobId;
        }

        public String getReason() {
            return reason;
        }

        public static JobType fromJobId(int jobId) {
            return Arrays.stream(values())
                    .filter(t -> t.getJobId() == jobId)
                    .findFirst()
                    .orElseThrow(() -> new IllegalArgumentException("Unknown jobId: " + jobId));
        }
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
        @NonNull private final Config mConfig;

        Injector(@NonNull Context context, @NonNull ArtManagerLocal artManagerLocal,
                @NonNull Config config) {
            mContext = context;
            mArtManagerLocal = artManagerLocal;
            mConfig = config;

            // Call the getters for various dependencies, to ensure correct initialization order.
            getPackageManagerLocal();
            getJobScheduler();
        }

        @NonNull
        public ArtManagerLocal getArtManagerLocal() {
            return mArtManagerLocal;
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(PackageManagerLocal.class));
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }

        @NonNull
        public JobScheduler getJobScheduler() {
            return Objects.requireNonNull(mContext.getSystemService(JobScheduler.class));
        }
    }
}
