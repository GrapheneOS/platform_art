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

import static com.android.server.art.ArtManagerLocal.OptimizePackageDoneCallback;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.OptimizeStatus;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyLong;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.same;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import android.apphibernation.AppHibernationManager;
import android.os.CancellationSignal;
import android.os.PowerManager;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.Config;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.SharedLibrary;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;
import java.util.stream.Collectors;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class DexOptHelperTest {
    private static final String PKG_NAME_FOO = "com.example.foo";
    private static final String PKG_NAME_BAR = "com.example.bar";
    private static final String PKG_NAME_LIB1 = "com.example.lib1";
    private static final String PKG_NAME_LIB2 = "com.example.lib2";
    private static final String PKG_NAME_LIB3 = "com.example.lib3";
    private static final String PKG_NAME_LIB4 = "com.example.lib4";
    private static final String PKG_NAME_LIBBAZ = "com.example.libbaz";

    @Mock private DexOptHelper.Injector mInjector;
    @Mock private PrimaryDexOptimizer mPrimaryDexOptimizer;
    @Mock private SecondaryDexOptimizer mSecondaryDexOptimizer;
    @Mock private AppHibernationManager mAhm;
    @Mock private PowerManager mPowerManager;
    @Mock private PowerManager.WakeLock mWakeLock;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    private PackageState mPkgStateFoo;
    private PackageState mPkgStateBar;
    private PackageState mPkgStateLib1;
    private PackageState mPkgStateLib2;
    private PackageState mPkgStateLib4;
    private PackageState mPkgStateLibbaz;
    private AndroidPackage mPkgFoo;
    private AndroidPackage mPkgBar;
    private AndroidPackage mPkgLib1;
    private AndroidPackage mPkgLib2;
    private AndroidPackage mPkgLib4;
    private AndroidPackage mPkgLibbaz;
    private CancellationSignal mCancellationSignal;
    private ExecutorService mExecutor;
    private List<DexContainerFileOptimizeResult> mPrimaryResults;
    private List<DexContainerFileOptimizeResult> mSecondaryResults;
    private Config mConfig;
    private OptimizeParams mParams;
    private List<String> mRequestedPackages;
    private DexOptHelper mDexOptHelper;

    @Before
    public void setUp() throws Exception {
        lenient()
                .when(mPowerManager.newWakeLock(eq(PowerManager.PARTIAL_WAKE_LOCK), any()))
                .thenReturn(mWakeLock);

        lenient().when(mAhm.isHibernatingGlobally(any())).thenReturn(false);
        lenient().when(mAhm.isOatArtifactDeletionEnabled()).thenReturn(true);

        mCancellationSignal = new CancellationSignal();
        mExecutor = Executors.newSingleThreadExecutor();
        mConfig = new Config();

        preparePackagesAndLibraries();

        mPrimaryResults = createResults("/data/app/foo/base.apk",
                OptimizeResult.OPTIMIZE_PERFORMED /* status1 */,
                OptimizeResult.OPTIMIZE_PERFORMED /* status2 */);
        mSecondaryResults = createResults("/data/user_de/0/foo/foo.apk",
                OptimizeResult.OPTIMIZE_PERFORMED /* status1 */,
                OptimizeResult.OPTIMIZE_PERFORMED /* status2 */);

        lenient()
                .when(mInjector.getPrimaryDexOptimizer(any(), any(), any(), any()))
                .thenReturn(mPrimaryDexOptimizer);
        lenient().when(mPrimaryDexOptimizer.dexopt()).thenReturn(mPrimaryResults);

        lenient()
                .when(mInjector.getSecondaryDexOptimizer(any(), any(), any(), any()))
                .thenReturn(mSecondaryDexOptimizer);
        lenient().when(mSecondaryDexOptimizer.dexopt()).thenReturn(mSecondaryResults);

        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        lenient().when(mInjector.getAppHibernationManager()).thenReturn(mAhm);
        lenient().when(mInjector.getPowerManager()).thenReturn(mPowerManager);
        lenient().when(mInjector.getConfig()).thenReturn(mConfig);

        mDexOptHelper = new DexOptHelper(mInjector);
    }

    @After
    public void tearDown() {
        mExecutor.shutdown();
    }

    @Test
    public void testDexopt() throws Exception {
        // Only package libbaz fails.
        var failingPrimaryDexOptimizer = mock(PrimaryDexOptimizer.class);
        List<DexContainerFileOptimizeResult> partialFailureResults = createResults(
                "/data/app/foo/base.apk", OptimizeResult.OPTIMIZE_PERFORMED /* status1 */,
                OptimizeResult.OPTIMIZE_FAILED /* status2 */);
        lenient().when(failingPrimaryDexOptimizer.dexopt()).thenReturn(partialFailureResults);
        when(mInjector.getPrimaryDexOptimizer(same(mPkgStateLibbaz), any(), any(), any()))
                .thenReturn(failingPrimaryDexOptimizer);

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getRequestedCompilerFilter()).isEqualTo("speed-profile");
        assertThat(result.getReason()).isEqualTo("install");
        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_FAILED);

        // The requested packages must come first.
        assertThat(result.getPackageOptimizeResults()).hasSize(6);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ, OptimizeResult.OPTIMIZE_FAILED,
                List.of(partialFailureResults, mSecondaryResults));
        checkPackageResult(result, 3 /* index */, PKG_NAME_LIB1, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 4 /* index */, PKG_NAME_LIB2, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 5 /* index */, PKG_NAME_LIB4, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));

        // The order matters. It should acquire the wake lock only once, at the beginning, and
        // release the wake lock at the end. When running in a single thread, it should dexopt
        // primary dex files and the secondary dex files together for each package, and it should
        // dexopt requested packages, in the given order, and then dexopt dependencies.
        InOrder inOrder = inOrder(mInjector, mWakeLock);
        inOrder.verify(mWakeLock).setWorkSource(any());
        inOrder.verify(mWakeLock).acquire(anyLong());
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateFoo), same(mPkgFoo), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateFoo), same(mPkgFoo), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateBar), same(mPkgBar), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateBar), same(mPkgBar), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateLibbaz), same(mPkgLibbaz), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateLibbaz), same(mPkgLibbaz), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateLib1), same(mPkgLib1), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateLib1), same(mPkgLib1), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateLib2), same(mPkgLib2), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateLib2), same(mPkgLib2), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateLib4), same(mPkgLib4), same(mParams), same(mCancellationSignal));
        inOrder.verify(mInjector).getSecondaryDexOptimizer(
                same(mPkgStateLib4), same(mPkgLib4), same(mParams), same(mCancellationSignal));
        inOrder.verify(mWakeLock).release();

        verifyNoMoreDexopt(6 /* expectedPrimaryTimes */, 6 /* expectedSecondaryTimes */);

        verifyNoMoreInteractions(mWakeLock);
    }

    @Test
    public void testDexoptNoDependencies() throws Exception {
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_FOR_SECONDARY_DEX,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getPackageOptimizeResults()).hasSize(3);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ,
                OptimizeResult.OPTIMIZE_PERFORMED, List.of(mPrimaryResults, mSecondaryResults));

        verifyNoMoreDexopt(3 /* expectedPrimaryTimes */, 3 /* expectedSecondaryTimes */);
    }

    @Test
    public void testDexoptPrimaryOnly() throws Exception {
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getPackageOptimizeResults()).hasSize(6);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ,
                OptimizeResult.OPTIMIZE_PERFORMED, List.of(mPrimaryResults));
        checkPackageResult(result, 3 /* index */, PKG_NAME_LIB1, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 4 /* index */, PKG_NAME_LIB2, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 5 /* index */, PKG_NAME_LIB4, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));

        verifyNoMoreDexopt(6 /* expectedPrimaryTimes */, 0 /* expectedSecondaryTimes */);
    }

    @Test
    public void testDexoptPrimaryOnlyNoDependencies() throws Exception {
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(0,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getPackageOptimizeResults()).hasSize(3);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults));
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ,
                OptimizeResult.OPTIMIZE_PERFORMED, List.of(mPrimaryResults));

        verifyNoMoreDexopt(3 /* expectedPrimaryTimes */, 0 /* expectedSecondaryTimes */);
    }

    @Test
    public void testDexoptCancelledBetweenDex2oatInvocations() throws Exception {
        when(mPrimaryDexOptimizer.dexopt()).thenAnswer(invocation -> {
            mCancellationSignal.cancel();
            return mPrimaryResults;
        });

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_CANCELLED);

        assertThat(result.getPackageOptimizeResults()).hasSize(6);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_CANCELLED,
                List.of(mPrimaryResults));
        checkPackageResult(
                result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_CANCELLED, List.of());
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ,
                OptimizeResult.OPTIMIZE_CANCELLED, List.of());
        checkPackageResult(
                result, 3 /* index */, PKG_NAME_LIB1, OptimizeResult.OPTIMIZE_CANCELLED, List.of());
        checkPackageResult(
                result, 4 /* index */, PKG_NAME_LIB2, OptimizeResult.OPTIMIZE_CANCELLED, List.of());
        checkPackageResult(
                result, 5 /* index */, PKG_NAME_LIB4, OptimizeResult.OPTIMIZE_CANCELLED, List.of());

        verify(mInjector).getPrimaryDexOptimizer(
                same(mPkgStateFoo), same(mPkgFoo), same(mParams), same(mCancellationSignal));

        verifyNoMoreDexopt(1 /* expectedPrimaryTimes */, 0 /* expectedSecondaryTimes */);
    }

    @Test
    public void testDexoptNoCode() throws Exception {
        when(mPkgFoo.getSplits().get(0).isHasCode()).thenReturn(false);

        mRequestedPackages = List.of(PKG_NAME_FOO);
        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_SKIPPED);
        assertThat(result.getPackageOptimizeResults()).hasSize(1);
        checkPackageResult(
                result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_SKIPPED, List.of());

        verifyNoDexopt();
    }

    @Test
    public void testDexoptLibraryNoCode() throws Exception {
        when(mPkgLib1.getSplits().get(0).isHasCode()).thenReturn(false);

        mRequestedPackages = List.of(PKG_NAME_FOO);
        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_PERFORMED);
        assertThat(result.getPackageOptimizeResults()).hasSize(1);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));

        verifyNoMoreDexopt(1 /* expectedPrimaryTimes */, 1 /* expectedSecondaryTimes */);
    }

    @Test
    public void testDexoptIsHibernating() throws Exception {
        lenient().when(mAhm.isHibernatingGlobally(PKG_NAME_FOO)).thenReturn(true);

        mRequestedPackages = List.of(PKG_NAME_FOO);
        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_SKIPPED);
        checkPackageResult(
                result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_SKIPPED, List.of());

        verifyNoDexopt();
    }

    @Test
    public void testDexoptIsHibernatingButOatArtifactDeletionDisabled() throws Exception {
        lenient().when(mAhm.isHibernatingGlobally(PKG_NAME_FOO)).thenReturn(true);
        lenient().when(mAhm.isOatArtifactDeletionEnabled()).thenReturn(false);

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(result.getPackageOptimizeResults()).hasSize(6);
        checkPackageResult(result, 0 /* index */, PKG_NAME_FOO, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 1 /* index */, PKG_NAME_BAR, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 2 /* index */, PKG_NAME_LIBBAZ,
                OptimizeResult.OPTIMIZE_PERFORMED, List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 3 /* index */, PKG_NAME_LIB1, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 4 /* index */, PKG_NAME_LIB2, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
        checkPackageResult(result, 5 /* index */, PKG_NAME_LIB4, OptimizeResult.OPTIMIZE_PERFORMED,
                List.of(mPrimaryResults, mSecondaryResults));
    }

    @Test
    public void testDexoptAlwaysReleasesWakeLock() throws Exception {
        when(mPrimaryDexOptimizer.dexopt()).thenThrow(IllegalStateException.class);

        try {
            mDexOptHelper.dexopt(
                    mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);
        } catch (Exception ignored) {
        }

        verify(mWakeLock).release();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDexoptPackageNotFound() throws Exception {
        when(mSnapshot.getPackageState(any())).thenReturn(null);

        mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        verifyNoDexopt();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDexoptNoPackage() throws Exception {
        lenient().when(mPkgStateFoo.getAndroidPackage()).thenReturn(null);

        mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        verifyNoDexopt();
    }

    @Test
    public void testDexoptSplit() throws Exception {
        mRequestedPackages = List.of(PKG_NAME_FOO);
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                          .setSplitName("split_0")
                          .build();

        mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);
    }

    @Test
    public void testDexoptSplitNotFound() throws Exception {
        mRequestedPackages = List.of(PKG_NAME_FOO);
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                          .setSplitName("split_bogus")
                          .build();

        assertThrows(IllegalArgumentException.class, () -> {
            mDexOptHelper.dexopt(
                    mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);
        });
    }

    @Test
    public void testCallbacks() throws Exception {
        List<OptimizeResult> list1 = new ArrayList<>();
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, result -> list1.add(result));

        List<OptimizeResult> list2 = new ArrayList<>();
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, result -> list2.add(result));

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(list1).containsExactly(result);
        assertThat(list2).containsExactly(result);
    }

    @Test
    public void testCallbackRemoved() throws Exception {
        List<OptimizeResult> list1 = new ArrayList<>();
        OptimizePackageDoneCallback callback1 = result -> list1.add(result);
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, callback1);

        List<OptimizeResult> list2 = new ArrayList<>();
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, result -> list2.add(result));

        mConfig.removeOptimizePackageDoneCallback(callback1);

        OptimizeResult result = mDexOptHelper.dexopt(
                mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor);

        assertThat(list1).isEmpty();
        assertThat(list2).containsExactly(result);
    }

    @Test(expected = IllegalStateException.class)
    public void testCallbackAlreadyAdded() throws Exception {
        List<OptimizeResult> list = new ArrayList<>();
        OptimizePackageDoneCallback callback = result -> list.add(result);
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, callback);
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, callback);
    }

    // Tests `addOptimizePackageDoneCallback` with `onlyIncludeUpdates` being true and false.
    @Test
    public void testCallbackWithFailureResults() throws Exception {
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(0,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        // This list should collect all results.
        List<OptimizeResult> listAll = new ArrayList<>();
        mConfig.addOptimizePackageDoneCallback(
                false /* onlyIncludeUpdates */, Runnable::run, result -> listAll.add(result));

        // This list should only collect results that have updates.
        List<OptimizeResult> listOnlyIncludeUpdates = new ArrayList<>();
        mConfig.addOptimizePackageDoneCallback(true /* onlyIncludeUpdates */, Runnable::run,
                result -> listOnlyIncludeUpdates.add(result));

        // Dexopt partially fails on package "foo".
        List<DexContainerFileOptimizeResult> partialFailureResults = createResults(
                "/data/app/foo/base.apk", OptimizeResult.OPTIMIZE_PERFORMED /* status1 */,
                OptimizeResult.OPTIMIZE_FAILED /* status2 */);
        var fooPrimaryDexOptimizer = mock(PrimaryDexOptimizer.class);
        when(mInjector.getPrimaryDexOptimizer(same(mPkgStateFoo), any(), any(), any()))
                .thenReturn(fooPrimaryDexOptimizer);
        when(fooPrimaryDexOptimizer.dexopt()).thenReturn(partialFailureResults);

        // Dexopt totally fails on package "bar".
        List<DexContainerFileOptimizeResult> totalFailureResults = createResults(
                "/data/app/bar/base.apk", OptimizeResult.OPTIMIZE_FAILED /* status1 */,
                OptimizeResult.OPTIMIZE_FAILED /* status2 */);
        var barPrimaryDexOptimizer = mock(PrimaryDexOptimizer.class);
        when(mInjector.getPrimaryDexOptimizer(same(mPkgStateBar), any(), any(), any()))
                .thenReturn(barPrimaryDexOptimizer);
        when(barPrimaryDexOptimizer.dexopt()).thenReturn(totalFailureResults);

        OptimizeResult resultWithSomeUpdates = mDexOptHelper.dexopt(mSnapshot,
                List.of(PKG_NAME_FOO, PKG_NAME_BAR), mParams, mCancellationSignal, mExecutor);
        OptimizeResult resultWithNoUpdates = mDexOptHelper.dexopt(
                mSnapshot, List.of(PKG_NAME_BAR), mParams, mCancellationSignal, mExecutor);

        assertThat(listAll).containsExactly(resultWithSomeUpdates, resultWithNoUpdates);

        assertThat(listOnlyIncludeUpdates).hasSize(1);
        assertThat(listOnlyIncludeUpdates.get(0)
                           .getPackageOptimizeResults()
                           .stream()
                           .map(PackageOptimizeResult::getPackageName)
                           .collect(Collectors.toList()))
                .containsExactly(PKG_NAME_FOO);
    }

    @Test
    public void testProgressCallback() throws Exception {
        mParams = new OptimizeParams.Builder("install")
                          .setCompilerFilter("speed-profile")
                          .setFlags(ArtFlags.FLAG_FOR_SECONDARY_DEX,
                                  ArtFlags.FLAG_FOR_SECONDARY_DEX
                                          | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                          .build();

        // Delay the executor to verify that the commands passed to the executor are not bound to
        // changing variables.
        var progressCallbackExecutor = new DelayedExecutor();
        Consumer<OperationProgress> progressCallback = mock(Consumer.class);

        mDexOptHelper.dexopt(mSnapshot, mRequestedPackages, mParams, mCancellationSignal, mExecutor,
                progressCallbackExecutor, progressCallback);

        progressCallbackExecutor.runAll();

        InOrder inOrder = inOrder(progressCallback);
        inOrder.verify(progressCallback)
                .accept(eq(OperationProgress.create(0 /* current */, 3 /* total */)));
        inOrder.verify(progressCallback)
                .accept(eq(OperationProgress.create(1 /* current */, 3 /* total */)));
        inOrder.verify(progressCallback)
                .accept(eq(OperationProgress.create(2 /* current */, 3 /* total */)));
        inOrder.verify(progressCallback)
                .accept(eq(OperationProgress.create(3 /* current */, 3 /* total */)));
    }

    private AndroidPackage createPackage(boolean multiSplit) {
        AndroidPackage pkg = mock(AndroidPackage.class);

        var baseSplit = mock(AndroidPackageSplit.class);
        lenient().when(baseSplit.isHasCode()).thenReturn(true);

        if (multiSplit) {
            var split0 = mock(AndroidPackageSplit.class);
            lenient().when(split0.getName()).thenReturn("split_0");
            lenient().when(split0.isHasCode()).thenReturn(true);

            lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit, split0));
        } else {
            lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit));
        }

        return pkg;
    }

    private PackageState createPackageState(
            String packageName, List<SharedLibrary> deps, boolean multiSplit) {
        PackageState pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(packageName);
        lenient().when(pkgState.getAppId()).thenReturn(12345);
        lenient().when(pkgState.getUsesLibraries()).thenReturn(deps);
        AndroidPackage pkg = createPackage(multiSplit);
        lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);
        return pkgState;
    }

    private SharedLibrary createLibrary(
            String libraryName, String packageName, List<SharedLibrary> deps) {
        SharedLibrary library = mock(SharedLibrary.class);
        lenient().when(library.getName()).thenReturn(libraryName);
        lenient().when(library.getPackageName()).thenReturn(packageName);
        lenient().when(library.getDependencies()).thenReturn(deps);
        return library;
    }

    private void preparePackagesAndLibraries() {
        // Dependency graph:
        //                foo                bar
        //                 |                  |
        //            lib1a (lib1)       lib1b (lib1)       lib1c (lib1)
        //               /   \             /   \                  |
        //              /     \           /     \                 |
        //  libbaz (libbaz)    lib2 (lib2)    lib4 (lib4)    lib3 (lib3)
        //
        // "lib1a", "lib1b", and "lib1c" belong to the same package "lib1".

        mRequestedPackages = List.of(PKG_NAME_FOO, PKG_NAME_BAR, PKG_NAME_LIBBAZ);

        SharedLibrary libbaz = createLibrary("libbaz", PKG_NAME_LIBBAZ, List.of());
        SharedLibrary lib4 = createLibrary("lib4", PKG_NAME_LIB4, List.of());
        SharedLibrary lib3 = createLibrary("lib3", PKG_NAME_LIB3, List.of());
        SharedLibrary lib2 = createLibrary("lib2", PKG_NAME_LIB2, List.of());
        SharedLibrary lib1a = createLibrary("lib1a", PKG_NAME_LIB1, List.of(libbaz, lib2));
        SharedLibrary lib1b = createLibrary("lib1b", PKG_NAME_LIB1, List.of(lib2, lib4));
        SharedLibrary lib1c = createLibrary("lib1c", PKG_NAME_LIB1, List.of(lib3));

        mPkgStateFoo = createPackageState(PKG_NAME_FOO, List.of(lib1a), true /* multiSplit */);
        mPkgFoo = mPkgStateFoo.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_FOO)).thenReturn(mPkgStateFoo);

        mPkgStateBar = createPackageState(PKG_NAME_BAR, List.of(lib1b), false /* multiSplit */);
        mPkgBar = mPkgStateBar.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_BAR)).thenReturn(mPkgStateBar);

        mPkgStateLib1 = createPackageState(
                PKG_NAME_LIB1, List.of(libbaz, lib2, lib3, lib4), false /* multiSplit */);
        mPkgLib1 = mPkgStateLib1.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_LIB1)).thenReturn(mPkgStateLib1);

        mPkgStateLib2 = createPackageState(PKG_NAME_LIB2, List.of(), false /* multiSplit */);
        mPkgLib2 = mPkgStateLib2.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_LIB2)).thenReturn(mPkgStateLib2);

        // This should not be considered as a transitive dependency of any requested package, even
        // though it is a dependency of package "lib1".
        PackageState pkgStateLib3 =
                createPackageState(PKG_NAME_LIB3, List.of(), false /* multiSplit */);
        lenient().when(mSnapshot.getPackageState(PKG_NAME_LIB3)).thenReturn(pkgStateLib3);

        mPkgStateLib4 = createPackageState(PKG_NAME_LIB4, List.of(), false /* multiSplit */);
        mPkgLib4 = mPkgStateLib4.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_LIB4)).thenReturn(mPkgStateLib4);

        mPkgStateLibbaz = createPackageState(PKG_NAME_LIBBAZ, List.of(), false /* multiSplit */);
        mPkgLibbaz = mPkgStateLibbaz.getAndroidPackage();
        lenient().when(mSnapshot.getPackageState(PKG_NAME_LIBBAZ)).thenReturn(mPkgStateLibbaz);
    }

    private void verifyNoDexopt() {
        verify(mInjector, never()).getPrimaryDexOptimizer(any(), any(), any(), any());
        verify(mInjector, never()).getSecondaryDexOptimizer(any(), any(), any(), any());
    }

    private void verifyNoMoreDexopt(int expectedPrimaryTimes, int expectedSecondaryTimes) {
        verify(mInjector, times(expectedPrimaryTimes))
                .getPrimaryDexOptimizer(any(), any(), any(), any());
        verify(mInjector, times(expectedSecondaryTimes))
                .getSecondaryDexOptimizer(any(), any(), any(), any());
    }

    private List<DexContainerFileOptimizeResult> createResults(
            String dexPath, @OptimizeStatus int status1, @OptimizeStatus int status2) {
        return List.of(new DexContainerFileOptimizeResult(dexPath, true /* isPrimaryAbi */,
                               "arm64-v8a", "verify", status1, 100 /* dex2oatWallTimeMillis */,
                               400 /* dex2oatCpuTimeMillis */, 0 /* sizeBytes */,
                               0 /* sizeBeforeBytes */, false /* isSkippedDueToStorageLow */),
                new DexContainerFileOptimizeResult(dexPath, false /* isPrimaryAbi */, "armeabi-v7a",
                        "verify", status2, 100 /* dex2oatWallTimeMillis */,
                        400 /* dex2oatCpuTimeMillis */, 0 /* sizeBytes */, 0 /* sizeBeforeBytes */,
                        false /* isSkippedDueToStorageLow */));
    }

    private void checkPackageResult(OptimizeResult result, int index, String packageName,
            @OptimizeResult.OptimizeStatus int status,
            List<List<DexContainerFileOptimizeResult>> dexContainerFileOptimizeResults) {
        PackageOptimizeResult packageResult = result.getPackageOptimizeResults().get(index);
        assertThat(packageResult.getPackageName()).isEqualTo(packageName);
        assertThat(packageResult.getStatus()).isEqualTo(status);
        assertThat(packageResult.getDexContainerFileOptimizeResults())
                .containsExactlyElementsIn(dexContainerFileOptimizeResults.stream()
                                                   .flatMap(r -> r.stream())
                                                   .collect(Collectors.toList()));
    }

    /** An executor that delays execution until `runAll` is called. */
    private static class DelayedExecutor implements Executor {
        private List<Runnable> mCommands = new ArrayList<>();

        public void execute(Runnable command) {
            mCommands.add(command);
        }

        public void runAll() {
            for (Runnable command : mCommands) {
                command.run();
            }
            mCommands.clear();
        }
    }
}
