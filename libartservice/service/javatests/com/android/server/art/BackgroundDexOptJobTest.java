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
 * limitations under the License
 */

package com.android.server.art;

import static com.android.server.art.model.Config.Callback;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.same;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.os.CancellationSignal;
import android.os.SystemProperties;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.BackgroundDexOptJob.CompletedResult;
import com.android.server.art.BackgroundDexOptJob.FatalErrorResult;
import com.android.server.art.BackgroundDexOptJob.Result;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.Config;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.PackageManagerLocal;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;

import java.util.concurrent.Future;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class BackgroundDexOptJobTest {
    private static final long TIMEOUT_SEC = 1;

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, BackgroundDexOptJobService.class);

    @Mock private BackgroundDexOptJob.Injector mInjector;
    @Mock private ArtManagerLocal mArtManagerLocal;
    @Mock private PackageManagerLocal mPackageManagerLocal;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    @Mock private JobScheduler mJobScheduler;
    @Mock private OptimizeResult mOptimizeResult;
    @Mock private BackgroundDexOptJobService mJobService;
    @Mock private JobParameters mJobParameters;
    private Config mConfig;
    private BackgroundDexOptJob mBackgroundDexOptJob;
    private Semaphore mJobFinishedCalled = new Semaphore(0);

    @Before
    public void setUp() throws Exception {
        lenient()
                .when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(false);

        lenient().when(mPackageManagerLocal.withFilteredSnapshot()).thenReturn(mSnapshot);

        mConfig = new Config();

        lenient().when(mInjector.getArtManagerLocal()).thenReturn(mArtManagerLocal);
        lenient().when(mInjector.getPackageManagerLocal()).thenReturn(mPackageManagerLocal);
        lenient().when(mInjector.getConfig()).thenReturn(mConfig);
        lenient().when(mInjector.getJobScheduler()).thenReturn(mJobScheduler);

        mBackgroundDexOptJob = new BackgroundDexOptJob(mInjector);
        lenient().when(BackgroundDexOptJobService.getJob()).thenReturn(mBackgroundDexOptJob);

        lenient()
                .doAnswer(invocation -> {
                    mJobFinishedCalled.release();
                    return null;
                })
                .when(mJobService)
                .jobFinished(any(), anyBoolean());

        lenient()
                .when(mJobParameters.getStopReason())
                .thenReturn(JobParameters.STOP_REASON_UNDEFINED);
    }

    @Test
    public void testStart() {
        when(mArtManagerLocal.optimizePackages(
                     same(mSnapshot), eq(ReasonMapping.REASON_BG_DEXOPT), any(), any(), any()))
                .thenReturn(mOptimizeResult);

        Result result = Utils.getFuture(mBackgroundDexOptJob.start());
        assertThat(result).isInstanceOf(CompletedResult.class);
        assertThat(((CompletedResult) result).dexoptResult()).isSameInstanceAs(mOptimizeResult);
    }

    @Test
    public void testStartAlreadyRunning() {
        Semaphore optimizeDone = new Semaphore(0);
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenAnswer(invocation -> {
                    assertThat(optimizeDone.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
                    return mOptimizeResult;
                });

        Future<Result> future1 = mBackgroundDexOptJob.start();
        Future<Result> future2 = mBackgroundDexOptJob.start();
        assertThat(future1).isSameInstanceAs(future2);

        optimizeDone.release();
        Utils.getFuture(future1);

        verify(mArtManagerLocal, times(1)).optimizePackages(any(), any(), any(), any(), any());
    }

    @Test
    public void testStartAnother() {
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenReturn(mOptimizeResult);

        Future<Result> future1 = mBackgroundDexOptJob.start();
        Utils.getFuture(future1);
        Future<Result> future2 = mBackgroundDexOptJob.start();
        Utils.getFuture(future2);
        assertThat(future1).isNotSameInstanceAs(future2);
    }

    @Test
    public void testStartFatalError() {
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenThrow(IllegalStateException.class);

        Result result = Utils.getFuture(mBackgroundDexOptJob.start());
        assertThat(result).isInstanceOf(FatalErrorResult.class);
    }

    @Test
    public void testStartIgnoreDisabled() {
        lenient()
                .when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(true);

        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenReturn(mOptimizeResult);

        // The `start` method should ignore the system property. The system property is for
        // `schedule`.
        Utils.getFuture(mBackgroundDexOptJob.start());
    }

    @Test
    public void testCancel() {
        Semaphore optimizeCancelled = new Semaphore(0);
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenAnswer(invocation -> {
                    assertThat(optimizeCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS))
                            .isTrue();
                    var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
                    assertThat(cancellationSignal.isCanceled()).isTrue();
                    return mOptimizeResult;
                });

        Future<Result> future = mBackgroundDexOptJob.start();
        mBackgroundDexOptJob.cancel();
        optimizeCancelled.release();
        Utils.getFuture(future);
    }

    @Test
    public void testSchedule() {
        var captor = ArgumentCaptor.forClass(JobInfo.class);
        when(mJobScheduler.schedule(captor.capture())).thenReturn(JobScheduler.RESULT_SUCCESS);

        assertThat(mBackgroundDexOptJob.schedule()).isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        JobInfo jobInfo = captor.getValue();
        assertThat(jobInfo.getIntervalMillis()).isEqualTo(BackgroundDexOptJob.JOB_INTERVAL_MS);
        assertThat(jobInfo.isRequireDeviceIdle()).isTrue();
        assertThat(jobInfo.isRequireCharging()).isTrue();
        assertThat(jobInfo.isRequireBatteryNotLow()).isTrue();
        assertThat(jobInfo.isRequireStorageNotLow()).isTrue();
    }

    @Test
    public void testScheduleDisabled() {
        when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(true);

        assertThat(mBackgroundDexOptJob.schedule())
                .isEqualTo(ArtFlags.SCHEDULE_DISABLED_BY_SYSPROP);

        verify(mJobScheduler, never()).schedule(any());
    }

    @Test
    public void testScheduleOverride() {
        mConfig.setScheduleBackgroundDexoptJobCallback(Runnable::run, builder -> {
            builder.setRequiresBatteryNotLow(false);
            builder.setPriority(JobInfo.PRIORITY_LOW);
        });

        var captor = ArgumentCaptor.forClass(JobInfo.class);
        when(mJobScheduler.schedule(captor.capture())).thenReturn(JobScheduler.RESULT_SUCCESS);

        assertThat(mBackgroundDexOptJob.schedule()).isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        JobInfo jobInfo = captor.getValue();
        assertThat(jobInfo.getIntervalMillis()).isEqualTo(BackgroundDexOptJob.JOB_INTERVAL_MS);
        assertThat(jobInfo.isRequireDeviceIdle()).isTrue();
        assertThat(jobInfo.isRequireCharging()).isTrue();
        assertThat(jobInfo.isRequireBatteryNotLow()).isFalse();
        assertThat(jobInfo.isRequireStorageNotLow()).isTrue();
        assertThat(jobInfo.getPriority()).isEqualTo(JobInfo.PRIORITY_LOW);
    }

    @Test
    public void testScheduleOverrideCleared() {
        mConfig.setScheduleBackgroundDexoptJobCallback(
                Runnable::run, builder -> { builder.setRequiresBatteryNotLow(false); });
        mConfig.clearScheduleBackgroundDexoptJobCallback();

        var captor = ArgumentCaptor.forClass(JobInfo.class);
        when(mJobScheduler.schedule(captor.capture())).thenReturn(JobScheduler.RESULT_SUCCESS);

        assertThat(mBackgroundDexOptJob.schedule()).isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        JobInfo jobInfo = captor.getValue();
        assertThat(jobInfo.isRequireBatteryNotLow()).isTrue();
    }

    @Test
    public void testUnschedule() {
        mBackgroundDexOptJob.unschedule();
        verify(mJobScheduler).cancel(anyInt());
    }

    @Test
    public void testWantsRescheduleFalsePerformed() throws Exception {
        when(mOptimizeResult.getFinalStatus()).thenReturn(OptimizeResult.OPTIMIZE_PERFORMED);
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenReturn(mOptimizeResult);

        mBackgroundDexOptJob.onStartJob(mJobService, mJobParameters);
        assertThat(mJobFinishedCalled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        verify(mJobService).jobFinished(any(), eq(false) /* wantsReschedule */);
    }

    @Test
    public void testWantsRescheduleFalseFatalError() throws Exception {
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenThrow(RuntimeException.class);

        mBackgroundDexOptJob.onStartJob(mJobService, mJobParameters);
        assertThat(mJobFinishedCalled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        verify(mJobService).jobFinished(any(), eq(false) /* wantsReschedule */);
    }

    @Test
    public void testWantsRescheduleTrue() throws Exception {
        when(mOptimizeResult.getFinalStatus()).thenReturn(OptimizeResult.OPTIMIZE_CANCELLED);
        when(mArtManagerLocal.optimizePackages(any(), any(), any(), any(), any()))
                .thenReturn(mOptimizeResult);

        mBackgroundDexOptJob.onStartJob(mJobService, mJobParameters);
        assertThat(mJobFinishedCalled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        verify(mJobService).jobFinished(any(), eq(true) /* wantsReschedule */);
    }
}
