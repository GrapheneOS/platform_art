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

import static com.android.server.art.GetDexoptNeededResult.ArtifactsLocation;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;

import android.content.pm.ApplicationInfo;
import android.os.CancellationSignal;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;

import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;
import com.android.server.art.wrapper.PackageUserState;

import dalvik.system.PathClassLoader;

import org.junit.Before;
import org.junit.Rule;
import org.mockito.Mock;

import java.util.ArrayList;
import java.util.List;

public class PrimaryDexOptimizerTestBase {
    protected static final String PKG_NAME = "com.example.foo";
    protected static final int UID = 12345;
    protected static final int SHARED_GID = UserHandle.getSharedAppGid(UID);

    @Rule public StaticMockitoRule mockitoRule = new StaticMockitoRule(SystemProperties.class);

    @Mock protected PrimaryDexOptimizer.Injector mInjector;
    @Mock protected IArtd mArtd;
    @Mock protected UserManager mUserManager;
    protected PackageState mPkgState;
    protected AndroidPackageApi mPkg;
    protected PackageUserState mPkgUserStateNotInstalled;
    protected PackageUserState mPkgUserStateInstalled;
    protected CancellationSignal mCancellationSignal;

    protected PrimaryDexOptimizer mPrimaryDexOptimizer;

    @Before
    public void setUp() throws Exception {
        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.isSystemUiPackage(any())).thenReturn(false);
        lenient().when(mInjector.isUsedByOtherApps(any())).thenReturn(false);
        lenient().when(mInjector.getUserManager()).thenReturn(mUserManager);

        lenient()
                .when(SystemProperties.get("dalvik.vm.systemuicompilerfilter"))
                .thenReturn("speed");
        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.always_debuggable"), anyBoolean()))
                .thenReturn(false);
        lenient().when(SystemProperties.get("dalvik.vm.appimageformat")).thenReturn("lz4");
        lenient().when(SystemProperties.get("pm.dexopt.shared")).thenReturn("speed");

        lenient()
                .when(mUserManager.getUserHandles(anyBoolean()))
                .thenReturn(List.of(UserHandle.of(0), UserHandle.of(1), UserHandle.of(2)));

        mPkgUserStateNotInstalled = createPackageUserState(false /* installed */);
        mPkgUserStateInstalled = createPackageUserState(true /* installed */);
        mPkgState = createPackageState();
        mPkg = mPkgState.getAndroidPackage();
        mCancellationSignal = new CancellationSignal();

        mPrimaryDexOptimizer = new PrimaryDexOptimizer(mInjector);
    }

    private AndroidPackageApi createPackage() {
        // This package has the base APK and one split APK that has code.
        AndroidPackageApi pkg = mock(AndroidPackageApi.class);
        lenient().when(pkg.getBaseApkPath()).thenReturn("/data/app/foo/base.apk");
        lenient().when(pkg.isHasCode()).thenReturn(true);
        lenient().when(pkg.getClassLoaderName()).thenReturn(PathClassLoader.class.getName());
        lenient().when(pkg.getSplitNames()).thenReturn(new String[] {"split_0", "split_1"});
        lenient()
                .when(pkg.getSplitCodePaths())
                .thenReturn(
                        new String[] {"/data/app/foo/split_0.apk", "/data/app/foo/split_1.apk"});
        lenient()
                .when(pkg.getSplitFlags())
                .thenReturn(new int[] {ApplicationInfo.FLAG_HAS_CODE, 0});
        lenient().when(pkg.getUid()).thenReturn(UID);
        lenient().when(pkg.isVmSafeMode()).thenReturn(false);
        lenient().when(pkg.isDebuggable()).thenReturn(false);
        lenient().when(pkg.getTargetSdkVersion()).thenReturn(123);
        lenient().when(pkg.isSignedWithPlatformKey()).thenReturn(false);
        lenient().when(pkg.isUsesNonSdkApi()).thenReturn(false);
        lenient().when(pkg.getSdkLibName()).thenReturn(null);
        lenient().when(pkg.getStaticSharedLibName()).thenReturn(null);
        lenient().when(pkg.getLibraryNames()).thenReturn(new ArrayList<>());
        return pkg;
    }

    private PackageState createPackageState() {
        PackageState pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(PKG_NAME);
        lenient().when(pkgState.getPrimaryCpuAbi()).thenReturn("arm64-v8a");
        lenient().when(pkgState.getSecondaryCpuAbi()).thenReturn("armeabi-v7a");
        lenient().when(pkgState.isSystem()).thenReturn(false);
        lenient().when(pkgState.isUpdatedSystemApp()).thenReturn(false);
        lenient().when(pkgState.getUsesLibraryInfos()).thenReturn(new ArrayList<>());
        lenient()
                .when(pkgState.getUserStateOrDefault(anyInt()))
                .thenReturn(mPkgUserStateNotInstalled);
        AndroidPackageApi pkg = createPackage();
        lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);
        return pkgState;
    }

    private PackageUserState createPackageUserState(boolean isInstalled) {
        PackageUserState pkgUserState = mock(PackageUserState.class);
        lenient().when(pkgUserState.isInstalled()).thenReturn(isInstalled);
        return pkgUserState;
    }

    protected GetDexoptNeededResult dexoptIsNotNeeded() {
        var result = new GetDexoptNeededResult();
        result.isDexoptNeeded = false;
        return result;
    }

    protected GetDexoptNeededResult dexoptIsNeeded() {
        return dexoptIsNeeded(ArtifactsLocation.NONE_OR_ERROR);
    }

    protected GetDexoptNeededResult dexoptIsNeeded(@ArtifactsLocation byte location) {
        var result = new GetDexoptNeededResult();
        result.isDexoptNeeded = true;
        result.artifactsLocation = location;
        if (location != ArtifactsLocation.NONE_OR_ERROR) {
            result.isVdexUsable = true;
        }
        return result;
    }

    protected DexoptResult createDexoptResult(boolean cancelled, long wallTimeMs, long cpuTimeMs) {
        var result = new DexoptResult();
        result.cancelled = cancelled;
        result.wallTimeMs = wallTimeMs;
        result.cpuTimeMs = cpuTimeMs;
        return result;
    }
}
