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

import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyLong;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.same;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import android.apphibernation.AppHibernationManager;
import android.os.CancellationSignal;
import android.os.PowerManager;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.testing.OnSuccessRule;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;
import com.android.server.pm.snapshot.PackageDataSnapshot;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.List;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class DexOptHelperTest {
    private static final String PKG_NAME = "com.example.foo";

    @Mock private DexOptHelper.Injector mInjector;
    @Mock private PrimaryDexOptimizer mPrimaryDexOptimizer;
    @Mock private AppHibernationManager mAhm;
    @Mock private PowerManager mPowerManager;
    @Mock private PowerManager.WakeLock mWakeLock;
    private PackageState mPkgState;
    private AndroidPackageApi mPkg;
    private CancellationSignal mCancellationSignal;

    @Rule
    public OnSuccessRule onSuccessRule = new OnSuccessRule(() -> {
        // Don't do this on failure because it will make the failure hard to understand.
        verifyNoMoreInteractions(mPrimaryDexOptimizer);
    });

    private final OptimizeParams mParams =
            new OptimizeParams.Builder("install").setCompilerFilter("speed-profile").build();
    private final List<DexContainerFileOptimizeResult> mPrimaryResults = List.of(
            new DexContainerFileOptimizeResult("/data/app/foo/base.apk", true /* isPrimaryAbi */,
                    "arm64-v8a", "verify", OptimizeResult.OPTIMIZE_PERFORMED,
                    100 /* dex2oatWallTimeMillis */, 400 /* dex2oatCpuTimeMillis */),
            new DexContainerFileOptimizeResult("/data/app/foo/base.apk", false /* isPrimaryAbi */,
                    "armeabi-v7a", "verify", OptimizeResult.OPTIMIZE_FAILED,
                    100 /* dex2oatWallTimeMillis */, 400 /* dex2oatCpuTimeMillis */));

    private DexOptHelper mDexOptHelper;

    @Before
    public void setUp() throws Exception {
        lenient().when(mInjector.getPrimaryDexOptimizer()).thenReturn(mPrimaryDexOptimizer);
        lenient().when(mInjector.getAppHibernationManager()).thenReturn(mAhm);
        lenient().when(mInjector.getPowerManager()).thenReturn(mPowerManager);

        lenient()
                .when(mPowerManager.newWakeLock(eq(PowerManager.PARTIAL_WAKE_LOCK), any()))
                .thenReturn(mWakeLock);

        lenient().when(mAhm.isHibernatingGlobally(PKG_NAME)).thenReturn(false);
        lenient().when(mAhm.isOatArtifactDeletionEnabled()).thenReturn(true);

        mPkgState = createPackageState();
        mPkg = mPkgState.getAndroidPackage();
        mCancellationSignal = new CancellationSignal();

        mDexOptHelper = new DexOptHelper(mInjector);
    }

    @Test
    public void testDexopt() throws Exception {
        when(mPrimaryDexOptimizer.dexopt(
                     same(mPkgState), same(mPkg), same(mParams), same(mCancellationSignal)))
                .thenReturn(mPrimaryResults);

        OptimizeResult result = mDexOptHelper.dexopt(
                mock(PackageDataSnapshot.class), mPkgState, mPkg, mParams, mCancellationSignal);

        assertThat(result.getRequestedCompilerFilter()).isEqualTo("speed-profile");
        assertThat(result.getReason()).isEqualTo("install");
        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_FAILED);
        assertThat(result.getPackageOptimizeResults()).hasSize(1);

        PackageOptimizeResult packageResult = result.getPackageOptimizeResults().get(0);
        assertThat(packageResult.getPackageName()).isEqualTo(PKG_NAME);
        assertThat(packageResult.getDexContainerFileOptimizeResults())
                .containsExactlyElementsIn(mPrimaryResults);

        InOrder inOrder = inOrder(mPrimaryDexOptimizer, mWakeLock);
        inOrder.verify(mWakeLock).acquire(anyLong());
        inOrder.verify(mPrimaryDexOptimizer).dexopt(any(), any(), any(), any());
        inOrder.verify(mWakeLock).release();
    }

    @Test
    public void testDexoptNoCode() throws Exception {
        when(mPkg.isHasCode()).thenReturn(false);

        OptimizeResult result = mDexOptHelper.dexopt(
                mock(PackageDataSnapshot.class), mPkgState, mPkg, mParams, mCancellationSignal);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_SKIPPED);
        assertThat(result.getPackageOptimizeResults().get(0).getDexContainerFileOptimizeResults())
                .isEmpty();
    }

    @Test
    public void testDexoptIsHibernating() throws Exception {
        lenient().when(mAhm.isHibernatingGlobally(PKG_NAME)).thenReturn(true);

        OptimizeResult result = mDexOptHelper.dexopt(
                mock(PackageDataSnapshot.class), mPkgState, mPkg, mParams, mCancellationSignal);

        assertThat(result.getFinalStatus()).isEqualTo(OptimizeResult.OPTIMIZE_SKIPPED);
        assertThat(result.getPackageOptimizeResults().get(0).getDexContainerFileOptimizeResults())
                .isEmpty();
    }

    @Test
    public void testDexoptIsHibernatingButOatArtifactDeletionDisabled() throws Exception {
        lenient().when(mAhm.isHibernatingGlobally(PKG_NAME)).thenReturn(true);
        lenient().when(mAhm.isOatArtifactDeletionEnabled()).thenReturn(false);

        when(mPrimaryDexOptimizer.dexopt(
                     same(mPkgState), same(mPkg), same(mParams), same(mCancellationSignal)))
                .thenReturn(mPrimaryResults);

        OptimizeResult result = mDexOptHelper.dexopt(
                mock(PackageDataSnapshot.class), mPkgState, mPkg, mParams, mCancellationSignal);

        assertThat(result.getPackageOptimizeResults().get(0).getDexContainerFileOptimizeResults())
                .containsExactlyElementsIn(mPrimaryResults);
    }

    @Test
    public void testDexoptAlwaysReleasesWakeLock() throws Exception {
        when(mPrimaryDexOptimizer.dexopt(
                     same(mPkgState), same(mPkg), same(mParams), same(mCancellationSignal)))
                .thenThrow(IllegalStateException.class);

        try {
            OptimizeResult result = mDexOptHelper.dexopt(
                    mock(PackageDataSnapshot.class), mPkgState, mPkg, mParams, mCancellationSignal);
        } catch (Exception e) {
        }

        verify(mWakeLock).release();
    }

    private AndroidPackageApi createPackage() {
        AndroidPackageApi pkg = mock(AndroidPackageApi.class);
        lenient().when(pkg.getUid()).thenReturn(12345);
        lenient().when(pkg.isHasCode()).thenReturn(true);
        return pkg;
    }

    private PackageState createPackageState() {
        PackageState pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(PKG_NAME);
        AndroidPackageApi pkg = createPackage();
        lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);
        return pkgState;
    }
}
