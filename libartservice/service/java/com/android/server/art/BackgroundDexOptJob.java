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

import static com.android.server.art.ArtManagerLocal.ScheduleBackgroundDexoptJobCallback;
import static com.android.server.art.model.ArtFlags.ScheduleStatus;
import static com.android.server.art.model.Config.Callback;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.os.CancellationSignal;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.Log;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.Config;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;

import com.google.auto.value.AutoValue;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

/** @hide */
public class BackgroundDexOptJob {
    private static final String TAG = "BackgroundDexOptJob";

    /**
     * "android" is the package name for a <service> declared in
     * frameworks/base/core/res/AndroidManifest.xml
     */
    private static final String JOB_PKG_NAME = Utils.PLATFORM_PACKAGE_NAME;
    /** An arbitrary number. Must be unique among all jobs owned by the system uid. */
    private static final int JOB_ID = 27873780;

    @VisibleForTesting public static final long JOB_INTERVAL_MS = TimeUnit.DAYS.toMillis(1);

    @NonNull private final Injector mInjector;

    @GuardedBy("this") @Nullable private CompletableFuture<Result> mRunningJob = null;
    @GuardedBy("this") @Nullable private CancellationSignal mCancellationSignal = null;
    @GuardedBy("this") @NonNull private Optional<Integer> mLastStopReason = Optional.empty();

    public BackgroundDexOptJob(@NonNull Context context, @NonNull ArtManagerLocal artManagerLocal,
            @NonNull Config config) {
        this(new Injector(context, artManagerLocal, config));
    }

    @VisibleForTesting
    public BackgroundDexOptJob(@NonNull Injector injector) {
        mInjector = injector;
    }

    /** Handles {@link BackgroundDexOptJobService#onStartJob(JobParameters)}. */
    public boolean onStartJob(
            @NonNull BackgroundDexOptJobService jobService, @NonNull JobParameters params) {
        start().thenAcceptAsync(result -> {
            writeStats(result);
            // This is a periodic job, where the interval is specified in the `JobInfo`. "true"
            // means to execute again during a future idle maintenance window in the same
            // interval, while "false" means not to execute again during a future idle maintenance
            // window in the same interval but to execute again in the next interval.
            // This call will be ignored if `onStopJob` is called.
            boolean wantsReschedule = result instanceof CompletedResult
                    && ((CompletedResult) result).dexoptResult().getFinalStatus()
                            == OptimizeResult.OPTIMIZE_CANCELLED;
            jobService.jobFinished(params, wantsReschedule);
        });
        // "true" means the job will continue running until `jobFinished` is called.
        return true;
    }

    /** Handles {@link BackgroundDexOptJobService#onStopJob(JobParameters)}. */
    public boolean onStopJob(@NonNull JobParameters params) {
        synchronized (this) {
            mLastStopReason = Optional.of(params.getStopReason());
        }
        cancel();
        // "true" means to execute again during a future idle maintenance window in the same
        // interval.
        return true;
    }

    /** Handles {@link ArtManagerLocal#scheduleBackgroundDexoptJob()}. */
    public @ScheduleStatus int schedule() {
        if (this != BackgroundDexOptJobService.getJob()) {
            throw new IllegalStateException("This job cannot be scheduled");
        }

        if (SystemProperties.getBoolean("pm.dexopt.disable_bg_dexopt", false /* def */)) {
            Log.i(TAG, "Job is disabled by system property 'pm.dexopt.disable_bg_dexopt'");
            return ArtFlags.SCHEDULE_DISABLED_BY_SYSPROP;
        }

        JobInfo.Builder builder =
                new JobInfo
                        .Builder(JOB_ID,
                                new ComponentName(
                                        JOB_PKG_NAME, BackgroundDexOptJobService.class.getName()))
                        .setPeriodic(JOB_INTERVAL_MS)
                        .setRequiresDeviceIdle(true)
                        .setRequiresCharging(true)
                        .setRequiresBatteryNotLow(true);

        Callback<ScheduleBackgroundDexoptJobCallback, Void> callback =
                mInjector.getConfig().getScheduleBackgroundDexoptJobCallback();
        if (callback != null) {
            Utils.executeAndWait(
                    callback.executor(), () -> { callback.get().onOverrideJobInfo(builder); });
        }

        JobInfo info = builder.build();
        if (info.isRequireStorageNotLow()) {
            // See the javadoc of
            // `ArtManagerLocal.ScheduleBackgroundDexoptJobCallback.onOverrideJobInfo` for details.
            throw new IllegalStateException("'setRequiresStorageNotLow' must not be set");
        }

        return mInjector.getJobScheduler().schedule(info) == JobScheduler.RESULT_SUCCESS
                ? ArtFlags.SCHEDULE_SUCCESS
                : ArtFlags.SCHEDULE_JOB_SCHEDULER_FAILURE;
    }

    /** Handles {@link ArtManagerLocal#unscheduleBackgroundDexoptJob()}. */
    public void unschedule() {
        if (this != BackgroundDexOptJobService.getJob()) {
            throw new IllegalStateException("This job cannot be unscheduled");
        }

        mInjector.getJobScheduler().cancel(JOB_ID);
    }

    @NonNull
    public synchronized CompletableFuture<Result> start() {
        if (mRunningJob != null) {
            Log.i(TAG, "Job is already running");
            return mRunningJob;
        }

        mCancellationSignal = new CancellationSignal();
        mLastStopReason = Optional.empty();
        mRunningJob = new CompletableFuture().supplyAsync(() -> {
            Log.i(TAG, "Job started");
            try {
                return run(mCancellationSignal);
            } catch (RuntimeException e) {
                Log.e(TAG, "Fatal error", e);
                return new FatalErrorResult();
            } finally {
                Log.i(TAG, "Job finished");
                synchronized (this) {
                    mRunningJob = null;
                    mCancellationSignal = null;
                }
            }
        });
        return mRunningJob;
    }

    public synchronized void cancel() {
        if (mRunningJob == null) {
            Log.i(TAG, "Job is not running");
            return;
        }

        mCancellationSignal.cancel();
        Log.i(TAG, "Job cancelled");
    }

    @NonNull
    private CompletedResult run(@NonNull CancellationSignal cancellationSignal) {
        // TODO(b/254013427): Cleanup dex use info.
        // TODO(b/254013425): Cleanup unused secondary dex file artifacts.
        long startTimeMs = SystemClock.uptimeMillis();
        OptimizeResult dexoptResult;
        try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot()) {
            dexoptResult = mInjector.getArtManagerLocal().optimizePackages(snapshot,
                    ReasonMapping.REASON_BG_DEXOPT, cancellationSignal,
                    null /* processCallbackExecutor */, null /* processCallback */);
        }
        return CompletedResult.create(dexoptResult, SystemClock.uptimeMillis() - startTimeMs);
    }

    private void writeStats(@NonNull Result result) {
        Optional<Integer> stopReason;
        synchronized (this) {
            stopReason = mLastStopReason;
        }
        if (result instanceof CompletedResult) {
            var completedResult = (CompletedResult) result;
            ArtStatsLog.write(ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED,
                    getStatusForStats(completedResult, stopReason),
                    stopReason.orElse(JobParameters.STOP_REASON_UNDEFINED),
                    completedResult.durationMs(), 0 /* deprecated */);
        } else if (result instanceof FatalErrorResult) {
            ArtStatsLog.write(ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED,
                    ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_FATAL_ERROR,
                    JobParameters.STOP_REASON_UNDEFINED, 0 /* durationMs */, 0 /* deprecated */);
        }
    }

    private int getStatusForStats(@NonNull CompletedResult result, Optional<Integer> stopReason) {
        if (result.dexoptResult().getFinalStatus() == OptimizeResult.OPTIMIZE_CANCELLED) {
            if (stopReason.isPresent()) {
                return ArtStatsLog
                        .BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_BY_CANCELLATION;
            } else {
                return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_BY_API;
            }
        }

        boolean isSkippedDueToStorageLow =
                result.dexoptResult()
                        .getPackageOptimizeResults()
                        .stream()
                        .flatMap(packageResult
                                -> packageResult.getDexContainerFileOptimizeResults().stream())
                        .anyMatch(fileResult -> fileResult.isSkippedDueToStorageLow());
        if (isSkippedDueToStorageLow) {
            return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_NO_SPACE_LEFT;
        }

        return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_JOB_FINISHED;
    }

    static abstract class Result {}
    static class FatalErrorResult extends Result {}

    @AutoValue
    static abstract class CompletedResult extends Result {
        abstract @NonNull OptimizeResult dexoptResult();
        abstract long durationMs();

        @NonNull
        static CompletedResult create(@NonNull OptimizeResult dexoptResult, long durationMs) {
            return new AutoValue_BackgroundDexOptJob_CompletedResult(dexoptResult, durationMs);
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
            return LocalManagerRegistry.getManager(PackageManagerLocal.class);
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }

        @NonNull
        public JobScheduler getJobScheduler() {
            return mContext.getSystemService(JobScheduler.class);
        }
    }
}
