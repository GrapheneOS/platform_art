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

import static com.android.server.art.AidlUtils.buildFsPermission;
import static com.android.server.art.AidlUtils.buildOutputArtifacts;
import static com.android.server.art.AidlUtils.buildPermissionSettings;
import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.model.DexoptResult.DexContainerFileDexoptResult;
import static com.android.server.art.testing.TestingUtils.deepEq;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.content.pm.ApplicationInfo;
import android.os.Process;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.testing.TestingUtils;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;
import org.mockito.ArgumentMatcher;

import java.nio.file.NoSuchFileException;
import java.util.ArrayList;
import java.util.List;

@SmallTest
@RunWith(Parameterized.class)
public class PrimaryDexopterParameterizedTest extends PrimaryDexopterTestBase {
    private DexoptParams mDexoptParams;

    private PrimaryDexopter mPrimaryDexopter;

    @Parameter(0) public Params mParams;

    @Parameters(name = "{0}")
    public static Iterable<Params> data() {
        List<Params> list = new ArrayList<>();
        Params params;

        // Baseline.
        params = new Params();
        list.add(params);

        // Sandbox SdkLib.
        params = new Params();
        params.mIsSanboxSdkLib = true;
        list.add(params);

        params = new Params();
        params.mRequestedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "speed";
        list.add(params);

        params = new Params();
        params.mIsInDalvikCache = true;
        list.add(params);

        params = new Params();
        params.mHiddenApiEnforcementPolicy = ApplicationInfo.HIDDEN_API_ENFORCEMENT_DISABLED;
        params.mExpectedIsHiddenApiPolicyEnabled = false;
        list.add(params);

        params = new Params();
        params.mIsDebuggable = true;
        params.mRequestedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "verify";
        params.mExpectedIsDebuggable = true;
        list.add(params);

        params = new Params();
        params.mIsVmSafeMode = true;
        params.mRequestedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        params = new Params();
        params.mIsUseEmbeddedDex = true;
        params.mRequestedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        params = new Params();
        params.mAlwaysDebuggable = true;
        params.mExpectedIsDebuggable = true;
        list.add(params);

        params = new Params();
        params.mIsSystemUi = true;
        params.mExpectedCompilerFilter = "speed";
        list.add(params);

        params = new Params();
        params.mIsLauncher = true;
        params.mExpectedCompilerFilter = "speed-profile";
        list.add(params);

        params = new Params();
        params.mForce = true;
        params.mShouldDowngrade = false;
        params.mExpectedDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.COMPILER_FILTER_IS_SAME | DexoptTrigger.COMPILER_FILTER_IS_WORSE
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
        list.add(params);

        params = new Params();
        params.mForce = true;
        params.mShouldDowngrade = true;
        params.mExpectedDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.COMPILER_FILTER_IS_SAME | DexoptTrigger.COMPILER_FILTER_IS_WORSE
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
        list.add(params);

        params = new Params();
        params.mShouldDowngrade = true;
        params.mExpectedDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_WORSE;
        list.add(params);

        params = new Params();
        // This should not change the result.
        params.mSkipIfStorageLow = true;
        list.add(params);

        params = new Params();
        params.mIgnoreProfile = true;
        params.mRequestedCompilerFilter = "speed-profile";
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        params = new Params();
        params.mIgnoreProfile = true;
        params.mRequestedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "speed";
        list.add(params);

        // It respects the adjustment made by the callback.
        params = new Params();
        params.mRequestedCompilerFilter = "speed-profile";
        params.mExpectedCallbackInputCompilerFilter = "speed-profile";
        params.mCallbackReturnedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "speed";
        list.add(params);

        // It upgrades the compiler filter before calling the callback.
        params = new Params();
        params.mRequestedCompilerFilter = "speed-profile";
        params.mIsSystemUi = true;
        params.mExpectedCallbackInputCompilerFilter = "speed";
        params.mCallbackReturnedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "speed";
        list.add(params);

        // It downgrades the compiler filter after calling the callback.
        params = new Params();
        params.mRequestedCompilerFilter = "speed-profile";
        params.mExpectedCallbackInputCompilerFilter = "speed-profile";
        params.mCallbackReturnedCompilerFilter = "speed-profile";
        params.mIsUseEmbeddedDex = true;
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        params = new Params();
        params.mIsPreReboot = true;
        params.mExpectedOutputIsPreReboot = true;
        params.mExpectedDeletesRuntimeArtifacts = false;
        params.mExpectedDeletesSdmSdcFiles = false;
        list.add(params);

        params = new Params();
        params.mIsSystemUi = true;
        params.mForceCompilerFilter = true;
        params.mRequestedCompilerFilter = "verify";
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        params = new Params();
        params.mForceCompilerFilter = true;
        params.mRequestedCompilerFilter = "verify";
        params.mExpectedCallbackInputCompilerFilter = "verify";
        params.mCallbackReturnedCompilerFilter = "speed";
        params.mExpectedCompilerFilter = "verify";
        list.add(params);

        return list;
    }

    @Before
    public void setUp() throws Exception {
        super.setUp();

        lenient().when(mInjector.isSystemUiPackage(any())).thenReturn(mParams.mIsSystemUi);
        lenient().when(mInjector.isLauncherPackage(any())).thenReturn(mParams.mIsLauncher);
        lenient().when(mInjector.isPreReboot()).thenReturn(mParams.mIsPreReboot);

        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.always_debuggable"), anyBoolean()))
                .thenReturn(mParams.mAlwaysDebuggable);

        lenient().when(mPkg.isVmSafeMode()).thenReturn(mParams.mIsVmSafeMode);
        lenient().when(mPkg.isDebuggable()).thenReturn(mParams.mIsDebuggable);
        lenient().when(mPkg.getTargetSdkVersion()).thenReturn(123);
        lenient()
                .when(mPkgState.getHiddenApiEnforcementPolicy())
                .thenReturn(mParams.mHiddenApiEnforcementPolicy);
        lenient().when(mPkg.isUseEmbeddedDex()).thenReturn(mParams.mIsUseEmbeddedDex);

        // Make all profile-related operations succeed so that "speed-profile" doesn't fall back to
        // "verify".
        lenient().when(mArtd.isProfileUsable(any(), any())).thenReturn(true);
        lenient().when(mArtd.getProfileVisibility(any())).thenReturn(FileVisibility.OTHER_READABLE);
        lenient().when(mArtd.mergeProfiles(any(), any(), any(), any(), any())).thenReturn(false);

        lenient().when(mArtd.isInDalvikCache(any())).thenReturn(mParams.mIsInDalvikCache);

        // By default, no artifacts exist.
        lenient().when(mArtd.getArtifactsVisibility(any())).thenReturn(FileVisibility.NOT_FOUND);

        if (mParams.mCallbackReturnedCompilerFilter != null) {
            mConfig.setAdjustCompilerFilterCallback(
                    Runnable::run, (packageName, originalCompilerFilter, reason) -> {
                        assertThat(packageName).isEqualTo(PKG_NAME);
                        assertThat(originalCompilerFilter)
                                .isEqualTo(mParams.mExpectedCallbackInputCompilerFilter);
                        assertThat(reason).isEqualTo("install");
                        return mParams.mCallbackReturnedCompilerFilter;
                    });
        }

        if (mParams.mIsSanboxSdkLib) {
            lenient().when(mPkgState.getAppId()).thenReturn(-1);
        }

        mDexoptParams =
                new DexoptParams.Builder("install")
                        .setCompilerFilter(mParams.mRequestedCompilerFilter)
                        .setPriorityClass(ArtFlags.PRIORITY_INTERACTIVE)
                        .setFlags(mParams.mForce ? ArtFlags.FLAG_FORCE : 0, ArtFlags.FLAG_FORCE)
                        .setFlags(mParams.mShouldDowngrade ? ArtFlags.FLAG_SHOULD_DOWNGRADE : 0,
                                ArtFlags.FLAG_SHOULD_DOWNGRADE)
                        .setFlags(mParams.mSkipIfStorageLow ? ArtFlags.FLAG_SKIP_IF_STORAGE_LOW : 0,
                                ArtFlags.FLAG_SKIP_IF_STORAGE_LOW)
                        .setFlags(mParams.mIgnoreProfile ? ArtFlags.FLAG_IGNORE_PROFILE : 0,
                                ArtFlags.FLAG_IGNORE_PROFILE)
                        .setFlags(mParams.mForceCompilerFilter ? ArtFlags.FLAG_FORCE_COMPILER_FILTER
                                                               : 0,
                                ArtFlags.FLAG_FORCE_COMPILER_FILTER)
                        .build();

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);
    }

    @Test
    public void testDexopt() throws Exception {
        int sharedGid = mParams.mIsSanboxSdkLib ? Process.SYSTEM_UID : SHARED_GID;
        PermissionSettings permissionSettings = buildPermissionSettings(
                buildFsPermission(Process.SYSTEM_UID /* uid */, Process.SYSTEM_UID /* gid */,
                        false /* isOtherReadable */, true /* isOtherExecutable */),
                buildFsPermission(Process.SYSTEM_UID /* uid */, sharedGid /* gid */,
                        true /* isOtherReadable */),
                null /* seContext */);

        // No need to check `generateAppImage`. It is checked in `PrimaryDexopterTest`.
        ArgumentMatcher<DexoptOptions> dexoptOptionsMatcher = options
                -> options.compilationReason.equals("install") && options.targetSdkVersion == 123
                && options.debuggable == mParams.mExpectedIsDebuggable
                && options.hiddenApiPolicyEnabled == mParams.mExpectedIsHiddenApiPolicyEnabled
                && options.comments.equals(String.format(
                        "app-name:%s,app-version-name:%s,app-version-code:%d,art-version:%d",
                        PKG_NAME, APP_VERSION_NAME, APP_VERSION_CODE, ART_VERSION));

        when(mArtd.createCancellationSignal()).thenReturn(mock(IArtdCancellationSignal.class));
        lenient()
                .when(mDexMetadataHelperInjector.openZipFile(any()))
                .thenThrow(NoSuchFileException.class);

        if (mParams.mIsPreReboot) {
            doReturn(FileVisibility.OTHER_READABLE)
                    .when(mArtd)
                    .getDexFileVisibility("/somewhere/app/foo/base.apk");
            doReturn(FileVisibility.OTHER_READABLE)
                    .when(mArtd)
                    .getDexFileVisibility("/somewhere/app/foo/split_0.apk");
        }

        // The first one is normal.
        doReturn(dexoptIsNeeded())
                .when(mArtd)
                .getDexoptNeeded("/somewhere/app/foo/base.apk", "arm64", "PCL[]",
                        mParams.mExpectedCompilerFilter, mParams.mExpectedDexoptTrigger);
        doReturn(createArtdDexoptResult(false /* cancelled */, 100 /* wallTimeMs */,
                         400 /* cpuTimeMs */, 30000 /* sizeBytes */, 32000 /* sizeBeforeBytes */))
                .when(mArtd)
                .dexopt(deepEq(buildOutputArtifacts("/somewhere/app/foo/base.apk", "arm64",
                                mParams.mIsInDalvikCache, permissionSettings,
                                mParams.mExpectedOutputIsPreReboot)),
                        eq("/somewhere/app/foo/base.apk"), eq("arm64"), eq("PCL[]"),
                        eq(mParams.mExpectedCompilerFilter), any() /* profile */,
                        isNull() /* inputVdex */, isNull() /* dmFile */,
                        eq(PriorityClass.INTERACTIVE), argThat(dexoptOptionsMatcher), any());

        // The second one fails on `dexopt`.
        doReturn(dexoptIsNeeded())
                .when(mArtd)
                .getDexoptNeeded("/somewhere/app/foo/base.apk", "arm", "PCL[]",
                        mParams.mExpectedCompilerFilter, mParams.mExpectedDexoptTrigger);
        doThrow(new ServiceSpecificException(31, "This is an error message."))
                .when(mArtd)
                .dexopt(deepEq(buildOutputArtifacts("/somewhere/app/foo/base.apk", "arm",
                                mParams.mIsInDalvikCache, permissionSettings,
                                mParams.mExpectedOutputIsPreReboot)),
                        eq("/somewhere/app/foo/base.apk"), eq("arm"), eq("PCL[]"),
                        eq(mParams.mExpectedCompilerFilter), any() /* profile */,
                        isNull() /* inputVdex */, isNull() /* dmFile */,
                        eq(PriorityClass.INTERACTIVE), argThat(dexoptOptionsMatcher), any());

        // The third one doesn't need dexopt.
        doReturn(dexoptIsNotNeeded())
                .when(mArtd)
                .getDexoptNeeded("/somewhere/app/foo/split_0.apk", "arm64", "PCL[base.apk]",
                        mParams.mExpectedCompilerFilter, mParams.mExpectedDexoptTrigger);

        // The fourth one is normal.
        doReturn(dexoptIsNeeded())
                .when(mArtd)
                .getDexoptNeeded("/somewhere/app/foo/split_0.apk", "arm", "PCL[base.apk]",
                        mParams.mExpectedCompilerFilter, mParams.mExpectedDexoptTrigger);
        doReturn(createArtdDexoptResult(false /* cancelled */, 200 /* wallTimeMs */,
                         200 /* cpuTimeMs */, 10000 /* sizeBytes */, 0 /* sizeBeforeBytes */))
                .when(mArtd)
                .dexopt(deepEq(buildOutputArtifacts("/somewhere/app/foo/split_0.apk", "arm",
                                mParams.mIsInDalvikCache, permissionSettings,
                                mParams.mExpectedOutputIsPreReboot)),
                        eq("/somewhere/app/foo/split_0.apk"), eq("arm"), eq("PCL[base.apk]"),
                        eq(mParams.mExpectedCompilerFilter), any() /* profile */,
                        isNull() /* inputVdex */, isNull() /* dmFile */,
                        eq(PriorityClass.INTERACTIVE), argThat(dexoptOptionsMatcher), any());

        if (mParams.mExpectedDeletesRuntimeArtifacts) {
            // Only delete runtime artifacts for successful dexopt operations, namely the first one
            // and the fourth one.
            doReturn(1l).when(mArtd).deleteRuntimeArtifacts(
                    deepEq(AidlUtils.buildRuntimeArtifactsPath(
                            PKG_NAME, "/somewhere/app/foo/base.apk", "arm64")));
            doReturn(1l).when(mArtd).deleteRuntimeArtifacts(
                    deepEq(AidlUtils.buildRuntimeArtifactsPath(
                            PKG_NAME, "/somewhere/app/foo/split_0.apk", "arm")));
        }

        if (mParams.mExpectedDeletesSdmSdcFiles) {
            // Only delete SDM and SDC files for successful dexopt operations, namely the first one
            // and the fourth one.
            doReturn(1l).when(mArtd).deleteSdmSdcFiles(
                    deepEq(AidlUtils.buildSecureDexMetadataWithCompanionPaths(
                            "/somewhere/app/foo/base.apk", "arm64", mParams.mIsInDalvikCache)));
            doReturn(1l).when(mArtd).deleteSdmSdcFiles(
                    deepEq(AidlUtils.buildSecureDexMetadataWithCompanionPaths(
                            "/somewhere/app/foo/split_0.apk", "arm", mParams.mIsInDalvikCache)));
        }

        assertThat(mPrimaryDexopter.dexopt())
                .comparingElementsUsing(TestingUtils.<DexContainerFileDexoptResult>deepEquality())
                .containsExactly(
                        DexContainerFileDexoptResult.create("/somewhere/app/foo/base.apk",
                                true /* isPrimaryAbi */, "arm64-v8a",
                                mParams.mExpectedCompilerFilter, DexoptResult.DEXOPT_PERFORMED,
                                100 /* dex2oatWallTimeMillis */, 400 /* dex2oatCpuTimeMillis */,
                                30000 /* sizeBytes */, 32000 /* sizeBeforeBytes */,
                                0 /* extendedStatusFlags */, List.of() /* externalProfileErrors */),
                        DexContainerFileDexoptResult.create("/somewhere/app/foo/base.apk",
                                false /* isPrimaryAbi */, "armeabi-v7a",
                                mParams.mExpectedCompilerFilter, DexoptResult.DEXOPT_FAILED),
                        DexContainerFileDexoptResult.create("/somewhere/app/foo/split_0.apk",
                                true /* isPrimaryAbi */, "arm64-v8a",
                                mParams.mExpectedCompilerFilter, DexoptResult.DEXOPT_SKIPPED),
                        DexContainerFileDexoptResult.create("/somewhere/app/foo/split_0.apk",
                                false /* isPrimaryAbi */, "armeabi-v7a",
                                mParams.mExpectedCompilerFilter, DexoptResult.DEXOPT_PERFORMED,
                                200 /* dex2oatWallTimeMillis */, 200 /* dex2oatCpuTimeMillis */,
                                10000 /* sizeBytes */, 0 /* sizeBeforeBytes */,
                                0 /* extendedStatusFlags */,
                                List.of() /* externalProfileErrors */));

        // Verify that there are no more calls than the ones above.
        verify(mArtd, times(3))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        any());

        if (!mParams.mExpectedDeletesRuntimeArtifacts) {
            verify(mArtd, times(0)).deleteRuntimeArtifacts(any());
        }

        if (!mParams.mExpectedDeletesSdmSdcFiles) {
            verify(mArtd, times(0)).deleteSdmSdcFiles(any());
        }
    }

    private static class Params {
        // Package information.
        public boolean mIsInDalvikCache = false;
        public int mHiddenApiEnforcementPolicy = ApplicationInfo.HIDDEN_API_ENFORCEMENT_ENABLED;
        public boolean mIsVmSafeMode = false;
        public boolean mIsDebuggable = false;
        public boolean mIsSystemUi = false;
        public boolean mIsLauncher = false;
        public boolean mIsUseEmbeddedDex = false;
        public boolean mIsSanboxSdkLib = false;

        // Options.
        public String mRequestedCompilerFilter = "verify";
        public String mCallbackReturnedCompilerFilter = null;
        public boolean mForce = false;
        public boolean mShouldDowngrade = false;
        public boolean mSkipIfStorageLow = false;
        public boolean mIgnoreProfile = false;
        public boolean mIsPreReboot = false;
        public boolean mForceCompilerFilter = false;

        // System properties.
        public boolean mAlwaysDebuggable = false;

        // Expectations.
        public String mExpectedCallbackInputCompilerFilter = "verify";
        public String mExpectedCompilerFilter = "verify";
        public int mExpectedDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
        public boolean mExpectedIsDebuggable = false;
        public boolean mExpectedIsHiddenApiPolicyEnabled = true;
        public boolean mExpectedOutputIsPreReboot = false;
        public boolean mExpectedDeletesRuntimeArtifacts = true;
        public boolean mExpectedDeletesSdmSdcFiles = true;

        public String toString() {
            return String.format("isInDalvikCache=%b,"
                            + "hiddenApiEnforcementPolicy=%d,"
                            + "isVmSafeMode=%b,"
                            + "isDebuggable=%b,"
                            + "isSystemUi=%b,"
                            + "isLauncher=%b,"
                            + "isUseEmbeddedDex=%b,"
                            + "isSanboxSdkLib=%b,"
                            + "requestedCompilerFilter=%s,"
                            + "callbackReturnedCompilerFilter=%s,"
                            + "force=%b,"
                            + "shouldDowngrade=%b,"
                            + "skipIfStorageLow=%b,"
                            + "ignoreProfile=%b,"
                            + "isPreReboot=%b,"
                            + "forceCompilerFilter=%b,"
                            + "alwaysDebuggable=%b"
                            + " => "
                            + "expectedCallbackInputCompilerFilter=%s,"
                            + "expectedCompilerFilter=%s,"
                            + "expectedDexoptTrigger=%d,"
                            + "expectedIsDebuggable=%b,"
                            + "expectedIsHiddenApiPolicyEnabled=%b,"
                            + "expectedOutputIsPreReboot=%b,"
                            + "expectedDeletesRuntimeArtifacts=%b,"
                            + "expectedDeletesSdmSdcFiles=%b",
                    mIsInDalvikCache, mHiddenApiEnforcementPolicy, mIsVmSafeMode, mIsDebuggable,
                    mIsSystemUi, mIsLauncher, mIsUseEmbeddedDex, mIsSanboxSdkLib,
                    mRequestedCompilerFilter, mCallbackReturnedCompilerFilter, mForce,
                    mShouldDowngrade, mSkipIfStorageLow, mIgnoreProfile, mIsPreReboot,
                    mForceCompilerFilter, mAlwaysDebuggable, mExpectedCallbackInputCompilerFilter,
                    mExpectedCompilerFilter, mExpectedDexoptTrigger, mExpectedIsDebuggable,
                    mExpectedIsHiddenApiPolicyEnabled, mExpectedOutputIsPreReboot,
                    mExpectedDeletesRuntimeArtifacts, mExpectedDeletesSdmSdcFiles);
        }
    }
}
