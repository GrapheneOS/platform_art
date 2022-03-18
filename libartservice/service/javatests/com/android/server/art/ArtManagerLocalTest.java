/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import android.content.pm.ApplicationInfo;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.DeleteOptions;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageDataSnapshot;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.art.wrapper.PackageState;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.mockito.quality.Strictness;

import java.util.List;

@SmallTest
@RunWith(Parameterized.class)
public class ArtManagerLocalTest {
    private static final String PKG_NAME = "com.example.foo";

    @Rule public MockitoRule mockitoRule = MockitoJUnit.rule().strictness(Strictness.STRICT_STUBS);

    @Mock private ArtManagerLocal.Injector mInjector;
    @Mock private PackageManagerLocal mPackageManagerLocal;
    @Mock private IArtd mArtd;
    private PackageState mPkgState;

    // True if the primary dex'es are in a readonly partition.
    @Parameter(0) public boolean mIsInReadonlyPartition;

    private ArtManagerLocal mArtManagerLocal;

    @Parameters(name = "isInReadonlyPartition={0}")
    public static Iterable<? extends Object> data() {
        return List.of(false, true);
    }

    @Before
    public void setUp() throws Exception {
        lenient().when(mInjector.getPackageManagerLocal()).thenReturn(mPackageManagerLocal);
        lenient().when(mInjector.getArtd()).thenReturn(mArtd);

        mPkgState = createPackageState();
        lenient()
                .when(mPackageManagerLocal.getPackageState(any(), anyInt(), eq(PKG_NAME)))
                .thenReturn(mPkgState);

        mArtManagerLocal = new ArtManagerLocal(mInjector);
    }

    @Test
    public void testDeleteOptimizedArtifacts() throws Exception {
        when(mArtd.deleteArtifacts(any())).thenReturn(1l);

        DeleteResult result = mArtManagerLocal.deleteOptimizedArtifacts(
                mock(PackageDataSnapshot.class), PKG_NAME, new DeleteOptions.Builder().build());
        assertThat(result.getFreedBytes()).isEqualTo(4);

        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("arm64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("arm")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("arm64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("arm")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verifyNoMoreInteractions(mArtd);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDeleteOptimizedArtifactsPackageNotFound() throws Exception {
        when(mPackageManagerLocal.getPackageState(any(), anyInt(), eq(PKG_NAME))).thenReturn(null);

        mArtManagerLocal.deleteOptimizedArtifacts(
                mock(PackageDataSnapshot.class), PKG_NAME, new DeleteOptions.Builder().build());
    }

    @Test(expected = IllegalStateException.class)
    public void testDeleteOptimizedArtifactsNoPackage() throws Exception {
        when(mPkgState.getAndroidPackage()).thenReturn(null);

        mArtManagerLocal.deleteOptimizedArtifacts(
                mock(PackageDataSnapshot.class), PKG_NAME, new DeleteOptions.Builder().build());
    }

    private AndroidPackageApi createPackage() {
        AndroidPackageApi pkg = mock(AndroidPackageApi.class);

        lenient().when(pkg.getBaseApkPath()).thenReturn("/data/app/foo/base.apk");
        lenient().when(pkg.isHasCode()).thenReturn(true);

        // split_0 has code while split_1 doesn't.
        lenient().when(pkg.getSplitNames()).thenReturn(new String[] {"split_0", "split_1"});
        lenient()
                .when(pkg.getSplitCodePaths())
                .thenReturn(
                        new String[] {"/data/app/foo/split_0.apk", "/data/app/foo/split_1.apk"});
        lenient()
                .when(pkg.getSplitFlags())
                .thenReturn(new int[] {ApplicationInfo.FLAG_HAS_CODE, 0});

        return pkg;
    }

    private PackageState createPackageState() {
        PackageState pkgState = mock(PackageState.class);

        lenient().when(pkgState.getPackageName()).thenReturn(PKG_NAME);
        lenient().when(pkgState.getPrimaryCpuAbi()).thenReturn("arm64-v8a");
        lenient().when(pkgState.getSecondaryCpuAbi()).thenReturn("armeabi-v7a");
        lenient().when(pkgState.isSystem()).thenReturn(mIsInReadonlyPartition);
        lenient().when(pkgState.isUpdatedSystemApp()).thenReturn(false);
        AndroidPackageApi pkg = createPackage();
        lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);

        return pkgState;
    }
}
