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

import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.PrimaryDexInfo;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.content.pm.ApplicationInfo;
import android.util.SparseArray;

import androidx.test.filters.SmallTest;

import com.android.server.art.PrimaryDexUtils;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;
import com.android.server.art.wrapper.SharedLibraryInfo;

import dalvik.system.DelegateLastClassLoader;
import dalvik.system.DexClassLoader;
import dalvik.system.PathClassLoader;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.List;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class PrimaryDexUtilsTest {
    @Before
    public void setUp() {}

    @Test
    public void testGetDexInfo() {
        List<PrimaryDexInfo> infos =
                PrimaryDexUtils.getDexInfo(createPackage(false /* isIsolatedSplitLoading */));
        checkBasicInfo(infos);
    }

    @Test
    public void testGetDetailedDexInfo() {
        List<DetailedPrimaryDexInfo> infos = PrimaryDexUtils.getDetailedDexInfo(
                createPackageState(), createPackage(false /* isIsolatedSplitLoading */));
        checkBasicInfo(infos);

        String sharedLibrariesContext = "{"
                + "PCL[library_2.jar]{PCL[library_1_dex_1.jar:library_1_dex_2.jar]}#"
                + "PCL[library_3.jar]#"
                + "PCL[library_4.jar]{PCL[library_1_dex_1.jar:library_1_dex_2.jar]}"
                + "}";

        assertThat(infos.get(0).classLoaderContext()).isEqualTo("PCL[]" + sharedLibrariesContext);
        assertThat(infos.get(1).classLoaderContext())
                .isEqualTo("PCL[base.apk]" + sharedLibrariesContext);
        assertThat(infos.get(2).classLoaderContext()).isEqualTo(null);
        assertThat(infos.get(3).classLoaderContext())
                .isEqualTo("PCL[base.apk:split_0.apk:split_1.apk]" + sharedLibrariesContext);
        assertThat(infos.get(4).classLoaderContext())
                .isEqualTo("PCL[base.apk:split_0.apk:split_1.apk:split_2.apk]"
                        + sharedLibrariesContext);
    }

    @Test
    public void testGetDetailedDexInfoIsolated() {
        List<DetailedPrimaryDexInfo> infos = PrimaryDexUtils.getDetailedDexInfo(
                createPackageState(), createPackage(true /* isIsolatedSplitLoading */));
        checkBasicInfo(infos);

        String sharedLibrariesContext = "{"
                + "PCL[library_2.jar]{PCL[library_1_dex_1.jar:library_1_dex_2.jar]}#"
                + "PCL[library_3.jar]#"
                + "PCL[library_4.jar]{PCL[library_1_dex_1.jar:library_1_dex_2.jar]}"
                + "}";

        assertThat(infos.get(0).classLoaderContext()).isEqualTo("PCL[]" + sharedLibrariesContext);
        assertThat(infos.get(1).classLoaderContext())
                .isEqualTo("PCL[];DLC[split_2.apk];PCL[base.apk]" + sharedLibrariesContext);
        assertThat(infos.get(2).classLoaderContext()).isEqualTo(null);
        assertThat(infos.get(3).classLoaderContext())
                .isEqualTo("DLC[];PCL[base.apk]" + sharedLibrariesContext);
        assertThat(infos.get(4).classLoaderContext()).isEqualTo("PCL[]");
        assertThat(infos.get(5).classLoaderContext()).isEqualTo("PCL[];PCL[split_3.apk]");
    }

    private <T extends PrimaryDexInfo> void checkBasicInfo(List<T> infos) {
        assertThat(infos.get(0).dexPath()).isEqualTo("/data/app/foo/base.apk");
        assertThat(infos.get(0).hasCode()).isTrue();
        assertThat(infos.get(0).isBaseApk()).isTrue();
        assertThat(infos.get(0).splitIndex()).isEqualTo(-1);
        assertThat(infos.get(0).splitName()).isNull();

        assertThat(infos.get(1).dexPath()).isEqualTo("/data/app/foo/split_0.apk");
        assertThat(infos.get(1).hasCode()).isTrue();
        assertThat(infos.get(1).isBaseApk()).isFalse();
        assertThat(infos.get(1).splitIndex()).isEqualTo(0);
        assertThat(infos.get(1).splitName()).isEqualTo("split_0");

        assertThat(infos.get(2).dexPath()).isEqualTo("/data/app/foo/split_1.apk");
        assertThat(infos.get(2).hasCode()).isFalse();
        assertThat(infos.get(2).isBaseApk()).isFalse();
        assertThat(infos.get(2).splitIndex()).isEqualTo(1);
        assertThat(infos.get(2).splitName()).isEqualTo("split_1");

        assertThat(infos.get(3).dexPath()).isEqualTo("/data/app/foo/split_2.apk");
        assertThat(infos.get(3).hasCode()).isTrue();
        assertThat(infos.get(3).isBaseApk()).isFalse();
        assertThat(infos.get(3).splitIndex()).isEqualTo(2);
        assertThat(infos.get(3).splitName()).isEqualTo("split_2");

        assertThat(infos.get(4).dexPath()).isEqualTo("/data/app/foo/split_3.apk");
        assertThat(infos.get(4).hasCode()).isTrue();
        assertThat(infos.get(4).isBaseApk()).isFalse();
        assertThat(infos.get(4).splitIndex()).isEqualTo(3);
        assertThat(infos.get(4).splitName()).isEqualTo("split_3");

        assertThat(infos.get(5).dexPath()).isEqualTo("/data/app/foo/split_4.apk");
        assertThat(infos.get(5).hasCode()).isTrue();
        assertThat(infos.get(5).isBaseApk()).isFalse();
        assertThat(infos.get(5).splitIndex()).isEqualTo(4);
        assertThat(infos.get(5).splitName()).isEqualTo("split_4");
    }

    private AndroidPackageApi createPackage(boolean isIsolatedSplitLoading) {
        AndroidPackageApi pkg = mock(AndroidPackageApi.class);

        when(pkg.getBaseApkPath()).thenReturn("/data/app/foo/base.apk");
        when(pkg.isHasCode()).thenReturn(true);
        when(pkg.getClassLoaderName()).thenReturn(PathClassLoader.class.getName());

        when(pkg.getSplitNames())
                .thenReturn(new String[] {"split_0", "split_1", "split_2", "split_3", "split_4"});
        when(pkg.getSplitCodePaths())
                .thenReturn(new String[] {
                        "/data/app/foo/split_0.apk",
                        "/data/app/foo/split_1.apk",
                        "/data/app/foo/split_2.apk",
                        "/data/app/foo/split_3.apk",
                        "/data/app/foo/split_4.apk",
                });
        when(pkg.getSplitFlags())
                .thenReturn(new int[] {
                        ApplicationInfo.FLAG_HAS_CODE,
                        0,
                        ApplicationInfo.FLAG_HAS_CODE,
                        ApplicationInfo.FLAG_HAS_CODE,
                        ApplicationInfo.FLAG_HAS_CODE,
                });

        if (isIsolatedSplitLoading) {
            // split_0: PCL(PathClassLoader), depends on split_2.
            // split_1: no code.
            // split_2: DLC(DelegateLastClassLoader), depends on base.
            // split_3: PCL(DexClassLoader), no dependency.
            // split_4: PCL(null), depends on split_3.
            when(pkg.isIsolatedSplitLoading()).thenReturn(true);
            when(pkg.getSplitClassLoaderNames())
                    .thenReturn(new String[] {
                            PathClassLoader.class.getName(),
                            null,
                            DelegateLastClassLoader.class.getName(),
                            DexClassLoader.class.getName(),
                            null,
                    });
            SparseArray<int[]> splitDependencies = new SparseArray<>();
            splitDependencies.set(1, new int[] {3});
            splitDependencies.set(3, new int[] {0});
            splitDependencies.set(5, new int[] {4});
            when(pkg.getSplitDependencies()).thenReturn(splitDependencies);
        } else {
            when(pkg.isIsolatedSplitLoading()).thenReturn(false);
        }

        return pkg;
    }

    private PackageState createPackageState() {
        PackageState pkgState = mock(PackageState.class);

        when(pkgState.getPackageName()).thenReturn("com.example.foo");

        // Base depends on library 2, 3, 4.
        // Library 2, 4 depends on library 1.
        List<SharedLibraryInfo> usesLibraryInfos = new ArrayList<>();

        SharedLibraryInfo library1 = mock(SharedLibraryInfo.class);
        when(library1.getAllCodePaths())
                .thenReturn(List.of("library_1_dex_1.jar", "library_1_dex_2.jar"));
        when(library1.getDependencies()).thenReturn(null);

        SharedLibraryInfo library2 = mock(SharedLibraryInfo.class);
        when(library2.getAllCodePaths()).thenReturn(List.of("library_2.jar"));
        when(library2.getDependencies()).thenReturn(List.of(library1));
        usesLibraryInfos.add(library2);

        SharedLibraryInfo library3 = mock(SharedLibraryInfo.class);
        when(library3.getAllCodePaths()).thenReturn(List.of("library_3.jar"));
        when(library3.getDependencies()).thenReturn(null);
        usesLibraryInfos.add(library3);

        SharedLibraryInfo library4 = mock(SharedLibraryInfo.class);
        when(library4.getAllCodePaths()).thenReturn(List.of("library_4.jar"));
        when(library4.getDependencies()).thenReturn(List.of(library1));
        usesLibraryInfos.add(library4);

        when(pkgState.getUsesLibraryInfos()).thenReturn(usesLibraryInfos);

        return pkgState;
    }
}
