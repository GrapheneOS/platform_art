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
import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.OutputArtifacts.PermissionSettings.SeContext;
import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.model.OptimizeResult.DexFileOptimizeResult;

import android.R;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.util.Log;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;

import dalvik.system.DexFile;

import java.util.ArrayList;
import java.util.List;

/** @hide */
public class PrimaryDexOptimizer {
    private static final String TAG = "PrimaryDexOptimizer";

    @NonNull private final Injector mInjector;

    public PrimaryDexOptimizer(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public PrimaryDexOptimizer(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * DO NOT use this method directly. Use {@link
     * ArtManagerLocal#optimizePackage(PackageDataSnapshot, String, OptimizeParams)}.
     */
    @NonNull
    public List<DexFileOptimizeResult> dexopt(@NonNull PackageState pkgState,
            @NonNull AndroidPackageApi pkg, @NonNull OptimizeParams params) throws RemoteException {
        List<DexFileOptimizeResult> results = new ArrayList<>();

        String targetCompilerFilter =
                adjustCompilerFilter(pkgState, pkg, params.getCompilerFilter(), params.getReason());
        if (targetCompilerFilter.equals(OptimizeParams.COMPILER_FILTER_NOOP)) {
            return results;
        }

        boolean isInDalvikCache = Utils.isInDalvikCache(pkgState);

        for (DetailedPrimaryDexInfo dexInfo : PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
            try {
                if (!dexInfo.hasCode()) {
                    continue;
                }

                // TODO(jiakaiz): Support optimizing a single split.

                String compilerFilter = targetCompilerFilter;

                if (DexFile.isProfileGuidedCompilerFilter(compilerFilter)) {
                    throw new UnsupportedOperationException(
                            "Profile-guided compilation is not implemented");
                }
                PermissionSettings permissionSettings =
                        getPermissionSettings(pkgState, pkg, true /* canBePublic */);

                DexoptOptions dexoptOptions = getDexoptOptions(pkgState, pkg, params);

                for (String isa : Utils.getAllIsas(pkgState)) {
                    @OptimizeResult.OptimizeStatus int status = OptimizeResult.OPTIMIZE_SKIPPED;
                    try {
                        GetDexoptNeededResult getDexoptNeededResult =
                                getDexoptNeeded(dexInfo, isa, compilerFilter,
                                        (params.getFlags() & ArtFlags.FLAG_SHOULD_DOWNGRADE) != 0,
                                        (params.getFlags() & ArtFlags.FLAG_FORCE) != 0);

                        if (!getDexoptNeededResult.isDexoptNeeded) {
                            continue;
                        }

                        ProfilePath inputProfile = null;

                        status = dexoptFile(dexInfo, isa, isInDalvikCache, compilerFilter,
                                inputProfile, getDexoptNeededResult, permissionSettings,
                                params.getPriorityClass(), dexoptOptions);
                    } catch (ServiceSpecificException e) {
                        // Log the error and continue.
                        Log.e(TAG,
                                String.format("Failed to dexopt [packageName = %s, dexPath = %s, "
                                                + "isa = %s, classLoaderContext = %s]",
                                        pkgState.getPackageName(), dexInfo.dexPath(), isa,
                                        dexInfo.classLoaderContext()),
                                e);
                        status = OptimizeResult.OPTIMIZE_FAILED;
                    } finally {
                        results.add(new DexFileOptimizeResult(
                                dexInfo.dexPath(), isa, compilerFilter, status));
                    }
                }
            } finally {
                // TODO(jiakaiz): Cleanup profile.
            }
        }

        return results;
    }

    @NonNull
    private String adjustCompilerFilter(@NonNull PackageState pkgState,
            @NonNull AndroidPackageApi pkg, @NonNull String targetCompilerFilter,
            @NonNull String reason) {
        if (mInjector.isSystemUiPackage(pkgState.getPackageName())) {
            String systemUiCompilerFilter = getSystemUiCompilerFilter();
            if (!systemUiCompilerFilter.isEmpty()) {
                return systemUiCompilerFilter;
            }
        }

        // We force vmSafeMode on debuggable apps as well:
        //  - the runtime ignores their compiled code
        //  - they generally have lots of methods that could make the compiler used run out of
        //    memory (b/130828957)
        // Note that forcing the compiler filter here applies to all compilations (even if they
        // are done via adb shell commands). This is okay because the runtime will ignore the
        // compiled code anyway.
        if (pkg.isVmSafeMode() || pkg.isDebuggable()) {
            return DexFile.getSafeModeCompilerFilter(targetCompilerFilter);
        }

        return targetCompilerFilter;
    }

    @NonNull
    private String getSystemUiCompilerFilter() {
        String compilerFilter = SystemProperties.get("dalvik.vm.systemuicompilerfilter");
        if (!compilerFilter.isEmpty() && !Utils.isValidArtServiceCompilerFilter(compilerFilter)) {
            throw new IllegalStateException(
                    "Got invalid compiler filter '" + compilerFilter + "' for System UI");
        }
        return compilerFilter;
    }

    @NonNull
    PermissionSettings getPermissionSettings(
            @NonNull PackageState pkgState, @NonNull AndroidPackageApi pkg, boolean canBePublic) {
        int uid = pkg.getUid();
        if (uid < 0) {
            throw new IllegalStateException(
                    "Package '" + pkgState.getPackageName() + "' has invalid app uid");
        }
        int sharedGid = UserHandle.getSharedAppGid(uid);
        if (sharedGid < 0) {
            throw new IllegalStateException(
                    String.format("Unable to get shared gid for package '%s' (uid: %d)",
                            pkgState.getPackageName(), uid));
        }

        // The files and directories should belong to the system so that Package Manager can manage
        // them (e.g., move them around).
        // We don't need the "read" bit for "others" on the directories because others only need to
        // access the files in the directories, but they don't need to "ls" the directories.
        FsPermission dirFsPermission = AidlUtils.buildFsPermission(Process.SYSTEM_UID,
                Process.SYSTEM_UID, false /* isOtherReadable */, true /* isOtherExecutable */);
        FsPermission fileFsPermission =
                AidlUtils.buildFsPermission(Process.SYSTEM_UID, sharedGid, canBePublic);
        // For primary dex, we can use the default SELinux context.
        SeContext seContext = null;
        return AidlUtils.buildPermissionSettings(dirFsPermission, fileFsPermission, seContext);
    }

    @NonNull
    private DexoptOptions getDexoptOptions(@NonNull PackageState pkgState,
            @NonNull AndroidPackageApi pkg, @NonNull OptimizeParams params) {
        DexoptOptions dexoptOptions = new DexoptOptions();
        dexoptOptions.compilationReason = params.getReason();
        dexoptOptions.targetSdkVersion = pkg.getTargetSdkVersion();
        dexoptOptions.debuggable = pkg.isDebuggable() || isAlwaysDebuggable();
        dexoptOptions.generateAppImage = false;
        dexoptOptions.hiddenApiPolicyEnabled = isHiddenApiPolicyEnabled(pkgState, pkg);
        return dexoptOptions;
    }

    private boolean isAlwaysDebuggable() {
        return SystemProperties.getBoolean("dalvik.vm.always_debuggable", false /* def */);
    }

    private boolean isAppImageEnabled() {
        return !SystemProperties.get("dalvik.vm.appimageformat").isEmpty();
    }

    private boolean isHiddenApiPolicyEnabled(
            @NonNull PackageState pkgState, @NonNull AndroidPackageApi pkg) {
        if (pkg.isSignedWithPlatformKey()) {
            return false;
        }
        if (pkgState.isSystem() || pkgState.isUpdatedSystemApp()) {
            // TODO(b/236389629): Check whether the app is in hidden api whitelist.
            return !pkg.isUsesNonSdkApi();
        }
        return true;
    }

    @NonNull
    GetDexoptNeededResult getDexoptNeeded(@NonNull DetailedPrimaryDexInfo dexInfo,
            @NonNull String isa, @NonNull String compilerFilter, boolean shouldDowngrade,
            boolean force) throws RemoteException {
        int dexoptTrigger = getDexoptTrigger(shouldDowngrade, force);

        // The result should come from artd even if all the bits of `dexoptTrigger` are set
        // because the result also contains information about the usable VDEX file.
        GetDexoptNeededResult result = mInjector.getArtd().getDexoptNeeded(dexInfo.dexPath(), isa,
                dexInfo.classLoaderContext(), compilerFilter, dexoptTrigger);

        return result;
    }

    int getDexoptTrigger(boolean shouldDowngrade, boolean force) {
        if (force) {
            return DexoptTrigger.COMPILER_FILTER_IS_BETTER | DexoptTrigger.COMPILER_FILTER_IS_SAME
                    | DexoptTrigger.COMPILER_FILTER_IS_WORSE
                    | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
        }

        if (shouldDowngrade) {
            return DexoptTrigger.COMPILER_FILTER_IS_WORSE;
        }

        return DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
    }

    private @OptimizeResult.OptimizeStatus int dexoptFile(@NonNull DetailedPrimaryDexInfo dexInfo,
            @NonNull String isa, boolean isInDalvikCache, @NonNull String compilerFilter,
            @Nullable ProfilePath profile, @NonNull GetDexoptNeededResult getDexoptNeededResult,
            @NonNull PermissionSettings permissionSettings, @PriorityClass int priorityClass,
            @NonNull DexoptOptions dexoptOptions) throws RemoteException {
        OutputArtifacts outputArtifacts = AidlUtils.buildOutputArtifacts(
                dexInfo.dexPath(), isa, isInDalvikCache, permissionSettings);

        VdexPath inputVdex = getInputVdex(getDexoptNeededResult, dexInfo.dexPath(), isa);

        if (!mInjector.getArtd().dexopt(outputArtifacts, dexInfo.dexPath(), isa,
                    dexInfo.classLoaderContext(), compilerFilter, profile, inputVdex, priorityClass,
                    dexoptOptions)) {
            return OptimizeResult.OPTIMIZE_CANCELLED;
        }

        return OptimizeResult.OPTIMIZE_PERFORMED;
    }

    @Nullable
    private VdexPath getInputVdex(@NonNull GetDexoptNeededResult getDexoptNeededResult,
            @NonNull String dexPath, @NonNull String isa) {
        if (!getDexoptNeededResult.isVdexUsable) {
            return null;
        }
        switch (getDexoptNeededResult.artifactsLocation) {
            case ArtifactsLocation.DALVIK_CACHE:
                return VdexPath.artifactsPath(
                        AidlUtils.buildArtifactsPath(dexPath, isa, true /* isInDalvikCache */));
            case ArtifactsLocation.NEXT_TO_DEX:
                return VdexPath.artifactsPath(
                        AidlUtils.buildArtifactsPath(dexPath, isa, false /* isInDalvikCache */));
            case ArtifactsLocation.DM:
                return VdexPath.dexMetadataPath(AidlUtils.buildDexMetadataPath(dexPath));
            default:
                // This should never happen as the value is got from artd.
                throw new IllegalStateException(
                        "Unknown artifacts location " + getDexoptNeededResult.artifactsLocation);
        }
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;
        }

        boolean isSystemUiPackage(@NonNull String packageName) {
            return packageName.equals(mContext.getString(R.string.config_systemUi));
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }
    }
}
