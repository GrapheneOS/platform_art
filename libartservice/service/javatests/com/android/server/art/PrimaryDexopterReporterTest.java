/*
 * Copyright (C) 2025 The Android Open Source Project
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

import static com.android.dx.mockito.inline.extended.ExtendedMockito.verify;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.times;

import android.os.ServiceSpecificException;

import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.model.DexMetadata;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.testing.TestingUtils;

import org.junit.Before;
import org.junit.Test;

import java.nio.file.NoSuchFileException;

public final class PrimaryDexopterReporterTest extends PrimaryDexopterTestBase {
    private final String COMPILER_FILTER_VERIFY = "verify";
    private final String COMPILER_REASON_INSTALL = "install";
    private final long DEX2OAT_ARTIFACTS_SIZE = 567L;
    private final long DEX2OAT_COMPILATION_TIME = 234L;
    private final DexoptParams DEXOPT_PARAMS_SKIP =
            new DexoptParams.Builder(COMPILER_REASON_INSTALL)
                    .setCompilerFilter(DexoptParams.COMPILER_FILTER_NOOP)
                    .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX)
                    .build();
    private final DexoptParams DEXOPT_PARAMS_VERIFY =
            new DexoptParams.Builder(COMPILER_REASON_INSTALL)
                    .setCompilerFilter(COMPILER_FILTER_VERIFY)
                    .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX)
                    .build();

    private PrimaryDexopter mPrimaryDexopter;

    @Before
    public void setUp() throws Exception {
        super.setUp();

        lenient().when(mInjector.getReporterExecutor()).thenReturn(Runnable::run);

        // By default, none of the profiles are usable.
        lenient().when(mArtd.isProfileUsable(any(), anyString())).thenReturn(false);
        lenient()
                .when(mArtd.copyAndRewriteProfile(any(), any(), anyString()))
                .thenReturn(TestingUtils.createCopyAndRewriteProfileNoProfile());
        lenient()
                .when(mArtd.copyAndRewriteEmbeddedProfile(any(), anyString()))
                .thenReturn(TestingUtils.createCopyAndRewriteProfileNoProfile());

        // By default, no DM file exists.
        lenient()
                .when(mDexMetadataHelperInjector.openZipFile(anyString()))
                .thenThrow(NoSuchFileException.class);

        // Dexopt is by default needed and successful.
        lenient()
                .when(mArtd.getDexoptNeeded(
                        anyString(), anyString(), anyString(), anyString(), anyInt()))
                .thenReturn(dexoptIsNeeded());
        mockArtdDexoptResultSuccess(createArtdDexoptResult(false /* cancelled */,
                DEX2OAT_COMPILATION_TIME, DEX2OAT_COMPILATION_TIME + 10, DEX2OAT_ARTIFACTS_SIZE,
                DEX2OAT_ARTIFACTS_SIZE - 10));

        mockPrimaryDexopter(DEXOPT_PARAMS_VERIFY);
    }

    @Test
    public void testDex2OatResult_Success() throws Exception {
        mPrimaryDexopter.dexopt();

        Dex2OatStatsReporter.Dex2OatResult expectedResult =
                Dex2OatStatsReporter.Dex2OatResult.exited(0);
        verify(()
                        -> Dex2OatStatsReporter.report(eq(UID), eq(COMPILER_FILTER_VERIFY),
                                eq(COMPILER_REASON_INSTALL), eq(DexMetadata.TYPE_NONE),
                                any(DetailedDexInfo.class), eq("arm64"), eq(expectedResult),
                                eq(DEX2OAT_ARTIFACTS_SIZE), eq(DEX2OAT_COMPILATION_TIME)),
                times(2));
        verify(()
                        -> Dex2OatStatsReporter.report(eq(UID), eq(COMPILER_FILTER_VERIFY),
                                eq(COMPILER_REASON_INSTALL), eq(DexMetadata.TYPE_NONE),
                                any(DetailedDexInfo.class), eq("arm"), eq(expectedResult),
                                eq(DEX2OAT_ARTIFACTS_SIZE), eq(DEX2OAT_COMPILATION_TIME)),
                times(2));
    }

    @Test
    public void testDex2OatResult_ExitedWithNonZeroCode() throws Exception {
        int status = 1, exitCode = 2, signal = 0;
        mockArtdDexoptResultFailure("dex2oat exited with non-zero code", status, exitCode, signal);

        mPrimaryDexopter.dexopt();

        Dex2OatStatsReporter.Dex2OatResult expectedResult =
                new Dex2OatStatsReporter.Dex2OatResult(status, exitCode, signal);
        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm64"),
                                eq(expectedResult), eq(0L), eq(0L)),
                times(2));

        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm"),
                                eq(expectedResult), eq(0L), eq(0L)),
                times(2));
    }

    @Test
    public void testDex2OatResult_Signaled() throws Exception {
        int status = 2, exitCode = -1, signal = 4;
        mockArtdDexoptResultFailure("dex2oat signaled", status, exitCode, signal);

        mPrimaryDexopter.dexopt();

        Dex2OatStatsReporter.Dex2OatResult expectedResult =
                new Dex2OatStatsReporter.Dex2OatResult(status, exitCode, signal);
        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm64"),
                                eq(expectedResult), eq(0L), eq(0L)),
                times(2));

        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm"),
                                eq(expectedResult), eq(0L), eq(0L)),
                times(2));
    }

    @Test
    public void testDex2OatResult_StartFailed() throws Exception {
        int status = 4, exitCode = -1, signal = 0;
        lenient()
                .when(mArtd.dexopt(any(), anyString(), anyString(), anyString(), anyString(), any(),
                        any(), any(), anyInt(), any(), any()))
                .thenThrow(
                        new ServiceSpecificException(-1, "Not your typical dex2oat error message"));

        mPrimaryDexopter.dexopt();

        Dex2OatStatsReporter.Dex2OatResult failedToStart =
                new Dex2OatStatsReporter.Dex2OatResult(status, exitCode, signal);
        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm64"),
                                eq(failedToStart), eq(0L), eq(0L)),
                times(2));

        verify(()
                        -> Dex2OatStatsReporter.report(eq(mPkgState.getAppId()),
                                eq(COMPILER_FILTER_VERIFY), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), eq("arm"),
                                eq(failedToStart), eq(0L), eq(0L)),
                times(2));
    }

    @Test
    public void testDex2OatResult_NotRun() throws Exception {
        mockPrimaryDexopter(DEXOPT_PARAMS_SKIP);

        mPrimaryDexopter.dexopt();

        verify(()
                        -> Dex2OatStatsReporter.reportSkipped(eq(UID), eq(COMPILER_REASON_INSTALL),
                                eq(DexMetadata.TYPE_NONE), any(DetailedDexInfo.class), anyList()),
                times(2));
    }

    @Test
    public void testDex2OatResult_Cancelled() throws Exception {
        mockArtdDexoptResultSuccess(createArtdDexoptResult(true /* cancelled */));

        mPrimaryDexopter.dexopt();

        verify(()
                        -> Dex2OatStatsReporter.report(eq(UID), eq(COMPILER_FILTER_VERIFY),
                                eq(COMPILER_REASON_INSTALL), eq(DexMetadata.TYPE_NONE),
                                any(DetailedDexInfo.class), eq("arm64"),
                                eq(Dex2OatStatsReporter.Dex2OatResult.cancelled()), eq(0L),
                                eq(0L)));
    }

    private void mockPrimaryDexopter(DexoptParams params) {
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, params, mCancellationSignal);
    }

    private void mockArtdDexoptResultSuccess(ArtdDexoptResult result) throws Exception {
        lenient()
                .when(mArtd.dexopt(any(), anyString(), anyString(), anyString(), anyString(), any(),
                        any(), any(), anyInt(), any(), any()))
                .thenReturn(result);
    }

    private void mockArtdDexoptResultFailure(String message, int status, int exitCode, int signal)
            throws Exception {
        lenient()
                .when(mArtd.dexopt(any(), anyString(), anyString(), anyString(), anyString(), any(),
                        any(), any(), anyInt(), any(), any()))
                .thenThrow(new ServiceSpecificException(-1,
                        String.format(
                                "Failed to run dex2oat: %s [status=%d,exit_code=%d,signal=%d]",
                                message, status, exitCode, signal)));
    }
}
