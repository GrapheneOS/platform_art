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

import static com.android.server.art.DexUseManagerLocal.DexLoader;
import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;
import static com.android.server.art.model.DexoptStatus.DexContainerFileDexoptStatus;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.DexoptStatus;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class DumpHelperTest {
    private static final String PKG_NAME_FOO = "com.example.foo";
    private static final String PKG_NAME_BAR = "com.example.bar";

    @Mock private DumpHelper.Injector mInjector;
    @Mock private ArtManagerLocal mArtManagerLocal;
    @Mock private DexUseManagerLocal mDexUseManagerLocal;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;

    private DumpHelper mDumpHelper;

    @Before
    public void setUp() throws Exception {
        lenient().when(mInjector.getArtManagerLocal()).thenReturn(mArtManagerLocal);
        lenient().when(mInjector.getDexUseManager()).thenReturn(mDexUseManagerLocal);

        LinkedHashMap<String, PackageState> pkgStates = createPackageStates();
        lenient().when(mSnapshot.getPackageStates()).thenReturn(pkgStates);

        setUpForFoo();
        setUpForBar();

        mDumpHelper = new DumpHelper(mInjector);
    }

    @Test
    public void testDump() throws Exception {
        String expected = "[com.example.foo]\n"
                + "  path: /data/app/foo/base.apk\n"
                + "    arm64: [status=speed-profile] [reason=bg-dexopt]\n"
                + "    arm: [status=verify] [reason=install]\n"
                + "  path: /data/app/foo/split_0.apk\n"
                + "    arm64: [status=verify] [reason=vdex]\n"
                + "    arm: [status=verify] [reason=vdex]\n"
                + "    used by other apps: [com.example.bar]\n"
                + "  known secondary dex files:\n"
                + "    /data/user_de/0/foo/1.apk\n"
                + "      arm: [status=run-from-apk] [reason=unknown]\n"
                + "      class loader context: =VaryingClassLoaderContexts=\n"
                + "      used by other apps: [com.example.foo (isolated), com.example.baz]\n"
                + "    /data/user_de/0/foo/2.apk\n"
                + "      arm64: [status=speed-profile] [reason=bg-dexopt]\n"
                + "      arm: [status=verify] [reason=vdex]\n"
                + "      class loader context: PCL[]\n"
                + "[com.example.bar]\n"
                + "  path: /data/app/bar/base.apk\n"
                + "    arm: [status=verify] [reason=install]\n"
                + "    arm64: [status=verify] [reason=install]\n";

        var stringWriter = new StringWriter();
        mDumpHelper.dump(new PrintWriter(stringWriter), mSnapshot);
        assertThat(stringWriter.toString()).isEqualTo(expected);
    }

    private PackageState createPackageState(String packageName, int appId, boolean hasPackage) {
        var pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(packageName);
        lenient().when(pkgState.getAppId()).thenReturn(appId);
        lenient()
                .when(pkgState.getAndroidPackage())
                .thenReturn(hasPackage ? mock(AndroidPackage.class) : null);
        return pkgState;
    }

    private LinkedHashMap<String, PackageState> createPackageStates() {
        // Use LinkedHashMap to ensure the determinism of the output.
        var pkgStates = new LinkedHashMap<String, PackageState>();
        pkgStates.put(PKG_NAME_FOO,
                createPackageState(PKG_NAME_FOO, 10001 /* appId */, true /* hasPackage */));
        pkgStates.put(PKG_NAME_BAR,
                createPackageState(PKG_NAME_BAR, 10003 /* appId */, true /* hasPackage */));
        // This should not be included in the output because it has a negative app id.
        pkgStates.put("com.android.art",
                createPackageState("com.android.art", -1 /* appId */, true /* hasPackage */));
        // This should not be included in the output because it does't have AndroidPackage.
        pkgStates.put("com.example.null",
                createPackageState("com.example.null", 10010 /* appId */, false /* hasPackage */));
        return pkgStates;
    }

    private void setUpForFoo() {
        // The order of the dex path and the ABI should be kept in the output.
        var status = DexoptStatus.create(
                List.of(DexContainerFileDexoptStatus.create("/data/app/foo/base.apk",
                                true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                "speed-profile", "bg-dexopt", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/app/foo/base.apk",
                                true /* isPrimaryDex */, false /* isPrimaryAbi */, "armeabi-v7a",
                                "verify", "install", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/app/foo/split_0.apk",
                                true /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                "verify", "vdex", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/app/foo/split_0.apk",
                                true /* isPrimaryDex */, false /* isPrimaryAbi */, "armeabi-v7a",
                                "verify", "vdex", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/user_de/0/foo/1.apk",
                                false /* isPrimaryDex */, false /* isPrimaryAbi */, "armeabi-v7a",
                                "run-from-apk", "unknown", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/user_de/0/foo/2.apk",
                                false /* isPrimaryDex */, true /* isPrimaryAbi */, "arm64-v8a",
                                "speed-profile", "bg-dexopt", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/user_de/0/foo/2.apk",
                                false /* isPrimaryDex */, false /* isPrimaryAbi */, "armeabi-v7a",
                                "verify", "vdex", "location-ignored")));

        lenient()
                .when(mArtManagerLocal.getDexoptStatus(any(), eq(PKG_NAME_FOO)))
                .thenReturn(status);

        // The output should not show "used by other apps:".
        lenient()
                .when(mDexUseManagerLocal.getPrimaryDexLoaders(
                        PKG_NAME_FOO, "/data/app/foo/base.apk"))
                .thenReturn(Set.of());

        // The output should not show "foo" in "used by other apps:".
        lenient()
                .when(mDexUseManagerLocal.getPrimaryDexLoaders(
                        PKG_NAME_FOO, "/data/app/foo/split_0.apk"))
                .thenReturn(Set.of(DexLoader.create(PKG_NAME_FOO, false /* isolatedProcess */),
                        DexLoader.create(PKG_NAME_BAR, false /* isolatedProcess */)));

        var info1 = mock(SecondaryDexInfo.class);
        lenient().when(info1.dexPath()).thenReturn("/data/user_de/0/foo/1.apk");
        lenient()
                .when(info1.displayClassLoaderContext())
                .thenReturn(SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS);
        var loaders = new LinkedHashSet<DexLoader>();
        // The output should show "foo" with "(isolated)" in "used by other apps:".
        loaders.add(DexLoader.create(PKG_NAME_FOO, true /* isolatedProcess */));
        loaders.add(DexLoader.create("com.example.baz", false /* isolatedProcess */));
        lenient().when(info1.loaders()).thenReturn(loaders);

        var info2 = mock(SecondaryDexInfo.class);
        lenient().when(info2.dexPath()).thenReturn("/data/user_de/0/foo/2.apk");
        lenient().when(info2.displayClassLoaderContext()).thenReturn("PCL[]");
        // The output should not show "used by other apps:".
        lenient()
                .when(info2.loaders())
                .thenReturn(Set.of(DexLoader.create(PKG_NAME_FOO, false /* isolatedProcess */)));

        lenient()
                .doReturn(List.of(info1, info2))
                .when(mDexUseManagerLocal)
                .getSecondaryDexInfo(PKG_NAME_FOO);
    }

    private void setUpForBar() {
        // The order of the ABI should be kept in the output, despite that it's different from the
        // order for package "foo".
        // The output should not show "known secondary dex files:".
        var status = DexoptStatus.create(
                List.of(DexContainerFileDexoptStatus.create("/data/app/bar/base.apk",
                                true /* isPrimaryDex */, true /* isPrimaryAbi */, "armeabi-v7a",
                                "verify", "install", "location-ignored"),
                        DexContainerFileDexoptStatus.create("/data/app/bar/base.apk",
                                true /* isPrimaryDex */, false /* isPrimaryAbi */, "arm64-v8a",
                                "verify", "install", "location-ignored")));

        lenient()
                .when(mArtManagerLocal.getDexoptStatus(any(), eq(PKG_NAME_BAR)))
                .thenReturn(status);

        lenient()
                .when(mDexUseManagerLocal.getPrimaryDexLoaders(
                        PKG_NAME_BAR, "/data/app/bar/base.apk"))
                .thenReturn(Set.of());
    }
}
