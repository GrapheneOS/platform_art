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
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;

import android.os.CancellationSignal;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;

import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.PackageUserState;

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

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, Constants.class);

    @Mock protected PrimaryDexOptimizer.Injector mInjector;
    @Mock protected IArtd mArtd;
    @Mock protected UserManager mUserManager;
    @Mock protected DexUseManager mDexUseManager;
    protected PackageState mPkgState;
    protected AndroidPackage mPkg;
    protected PackageUserState mPkgUserStateNotInstalled;
    protected PackageUserState mPkgUserStateInstalled;
    protected CancellationSignal mCancellationSignal;

    protected PrimaryDexOptimizer mPrimaryDexOptimizer;

    @Before
    public void setUp() throws Exception {
        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.isSystemUiPackage(any())).thenReturn(false);
        lenient().when(mInjector.getUserManager()).thenReturn(mUserManager);
        lenient().when(mInjector.getDexUseManager()).thenReturn(mDexUseManager);

        lenient()
                .when(SystemProperties.get("dalvik.vm.systemuicompilerfilter"))
                .thenReturn("speed");
        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.always_debuggable"), anyBoolean()))
                .thenReturn(false);
        lenient().when(SystemProperties.get("dalvik.vm.appimageformat")).thenReturn("lz4");
        lenient().when(SystemProperties.get("pm.dexopt.shared")).thenReturn("speed");

        // No ISA translation.
        lenient()
                .when(SystemProperties.get(argThat(arg -> arg.startsWith("ro.dalvik.vm.isa."))))
                .thenReturn("");

        lenient().when(Constants.getPreferredAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");

        lenient()
                .when(mUserManager.getUserHandles(anyBoolean()))
                .thenReturn(List.of(UserHandle.of(0), UserHandle.of(1), UserHandle.of(2)));

        lenient().when(mDexUseManager.isPrimaryDexUsedByOtherApps(any(), any())).thenReturn(false);

        mPkgUserStateNotInstalled = createPackageUserState(false /* installed */);
        mPkgUserStateInstalled = createPackageUserState(true /* installed */);
        mPkgState = createPackageState();
        mPkg = mPkgState.getAndroidPackage();
        mCancellationSignal = new CancellationSignal();

        mPrimaryDexOptimizer = new PrimaryDexOptimizer(mInjector);
    }

    private AndroidPackage createPackage() {
        // This package has the base APK and one split APK that has code.
        AndroidPackage pkg = mock(AndroidPackage.class);
        var baseSplit = mock(AndroidPackageSplit.class);
        lenient().when(baseSplit.getPath()).thenReturn("/data/app/foo/base.apk");
        lenient().when(baseSplit.isHasCode()).thenReturn(true);
        lenient().when(baseSplit.getClassLoaderName()).thenReturn(PathClassLoader.class.getName());

        var split0 = mock(AndroidPackageSplit.class);
        lenient().when(split0.getName()).thenReturn("split_0");
        lenient().when(split0.getPath()).thenReturn("/data/app/foo/split_0.apk");
        lenient().when(split0.isHasCode()).thenReturn(true);

        var split1 = mock(AndroidPackageSplit.class);
        lenient().when(split1.getName()).thenReturn("split_1");
        lenient().when(split1.getPath()).thenReturn("/data/app/foo/split_1.apk");
        lenient().when(split1.isHasCode()).thenReturn(false);

        var splits = List.of(baseSplit, split0, split1);
        lenient().when(pkg.getSplits()).thenReturn(splits);

        lenient().when(pkg.isVmSafeMode()).thenReturn(false);
        lenient().when(pkg.isDebuggable()).thenReturn(false);
        lenient().when(pkg.getTargetSdkVersion()).thenReturn(123);
        lenient().when(pkg.isSignedWithPlatformKey()).thenReturn(false);
        lenient().when(pkg.isUsesNonSdkApi()).thenReturn(false);
        lenient().when(pkg.getSdkLibraryName()).thenReturn(null);
        lenient().when(pkg.getStaticSharedLibraryName()).thenReturn(null);
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
        lenient().when(pkgState.getAppId()).thenReturn(UID);
        lenient().when(pkgState.getUsesLibraries()).thenReturn(new ArrayList<>());
        lenient()
                .when(pkgState.getStateForUser(any()))
                .thenReturn(mPkgUserStateNotInstalled);
        AndroidPackage pkg = createPackage();
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
