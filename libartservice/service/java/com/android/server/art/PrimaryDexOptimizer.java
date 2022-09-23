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
import static com.android.server.art.ProfilePath.RefProfilePath;
import static com.android.server.art.ProfilePath.TmpRefProfilePath;
import static com.android.server.art.Utils.Abi;
import static com.android.server.art.model.ArtFlags.OptimizeFlags;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;

import android.R;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.CancellationSignal;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.text.TextUtils;
import android.util.Log;
import android.util.Pair;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;
import com.android.server.art.wrapper.PackageUserState;

import com.google.auto.value.AutoValue;

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
    public List<DexContainerFileOptimizeResult> dexopt(@NonNull PackageState pkgState,
            @NonNull AndroidPackageApi pkg, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal) throws RemoteException {
        List<DexContainerFileOptimizeResult> results = new ArrayList<>();

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

        String targetCompilerFilter =
                adjustCompilerFilter(pkgState, pkg, params.getCompilerFilter(), params.getReason());
        if (targetCompilerFilter.equals(OptimizeParams.COMPILER_FILTER_NOOP)) {
            return results;
        }

        boolean isInDalvikCache = Utils.isInDalvikCache(pkgState);

        for (DetailedPrimaryDexInfo dexInfo : PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
            ProfilePath profile = null;
            boolean succeeded = true;
            try {
                if (!dexInfo.hasCode()) {
                    continue;
                }

                // TODO(jiakaiz): Support optimizing a single split.

                String compilerFilter = targetCompilerFilter;

                boolean needsToBeShared = isSharedLibrary(pkg)
                        || mInjector.isUsedByOtherApps(pkgState.getPackageName());
                boolean isOtherReadable = true;
                // If true, implies that the profile has changed since the last compilation.
                boolean profileMerged = false;
                if (DexFile.isProfileGuidedCompilerFilter(compilerFilter)) {
                    if (needsToBeShared) {
                        profile = initReferenceProfile(pkgState, dexInfo, uid, sharedGid);
                    } else {
                        Pair<ProfilePath, Boolean> pair =
                                getOrInitReferenceProfile(pkgState, dexInfo, uid, sharedGid);
                        if (pair != null) {
                            profile = pair.first;
                            isOtherReadable = pair.second;
                        }
                        ProfilePath mergedProfile =
                                mergeProfiles(pkgState, dexInfo, uid, sharedGid, profile);
                        if (mergedProfile != null) {
                            if (profile != null
                                    && profile.getTag() == ProfilePath.tmpRefProfilePath) {
                                mInjector.getArtd().deleteProfile(profile);
                            }
                            profile = mergedProfile;
                            isOtherReadable = false;
                            profileMerged = true;
                        }
                    }
                    if (profile == null) {
                        // A profile guided optimization with no profile is essentially 'verify',
                        // and dex2oat already makes this transformation. However, we need to
                        // explicitly make this transformation here to guide the later decisions
                        // such as whether the artifacts can be public and whether dexopt is needed.
                        compilerFilter = needsToBeShared
                                ? ReasonMapping.getCompilerFilterForShared()
                                : "verify";
                    }
                }
                boolean isProfileGuidedCompilerFilter =
                        DexFile.isProfileGuidedCompilerFilter(compilerFilter);
                Utils.check(isProfileGuidedCompilerFilter == (profile != null));

                boolean canBePublic = !isProfileGuidedCompilerFilter || isOtherReadable;
                Utils.check(Utils.implies(needsToBeShared, canBePublic));
                PermissionSettings permissionSettings =
                        getPermissionSettings(sharedGid, canBePublic);

                DexoptOptions dexoptOptions =
                        getDexoptOptions(pkgState, pkg, params, isProfileGuidedCompilerFilter);

                for (Abi abi : Utils.getAllAbis(pkgState)) {
                    @OptimizeResult.OptimizeStatus int status = OptimizeResult.OPTIMIZE_SKIPPED;
                    long wallTimeMs = 0;
                    long cpuTimeMs = 0;
                    try {
                        DexoptTarget target = DexoptTarget.builder()
                                                      .setDexInfo(dexInfo)
                                                      .setIsa(abi.isa())
                                                      .setIsInDalvikCache(isInDalvikCache)
                                                      .setCompilerFilter(compilerFilter)
                                                      .build();
                        GetDexoptNeededOptions options =
                                GetDexoptNeededOptions.builder()
                                        .setProfileMerged(profileMerged)
                                        .setFlags(params.getFlags())
                                        .setNeedsToBePublic(needsToBeShared)
                                        .build();

                        GetDexoptNeededResult getDexoptNeededResult =
                                getDexoptNeeded(target, options);

                        if (!getDexoptNeededResult.isDexoptNeeded) {
                            continue;
                        }

                        IArtdCancellationSignal artdCancellationSignal =
                                mInjector.getArtd().createCancellationSignal();
                        cancellationSignal.setOnCancelListener(() -> {
                            try {
                                artdCancellationSignal.cancel();
                            } catch (RemoteException e) {
                                Log.e(TAG, "An error occurred when sending a cancellation signal",
                                        e);
                            }
                        });

                        DexoptResult dexoptResult = dexoptFile(target, profile,
                                getDexoptNeededResult, permissionSettings,
                                params.getPriorityClass(), dexoptOptions, artdCancellationSignal);
                        status = dexoptResult.cancelled ? OptimizeResult.OPTIMIZE_CANCELLED
                                                        : OptimizeResult.OPTIMIZE_PERFORMED;
                        wallTimeMs = dexoptResult.wallTimeMs;
                        cpuTimeMs = dexoptResult.cpuTimeMs;

                        if (status == OptimizeResult.OPTIMIZE_CANCELLED) {
                            return results;
                        }
                    } catch (ServiceSpecificException e) {
                        // Log the error and continue.
                        Log.e(TAG,
                                String.format("Failed to dexopt [packageName = %s, dexPath = %s, "
                                                + "isa = %s, classLoaderContext = %s]",
                                        pkgState.getPackageName(), dexInfo.dexPath(), abi.isa(),
                                        dexInfo.classLoaderContext()),
                                e);
                        status = OptimizeResult.OPTIMIZE_FAILED;
                    } finally {
                        results.add(new DexContainerFileOptimizeResult(dexInfo.dexPath(),
                                abi.isPrimaryAbi(), abi.name(), compilerFilter, status, wallTimeMs,
                                cpuTimeMs));
                        if (status != OptimizeResult.OPTIMIZE_SKIPPED
                                && status != OptimizeResult.OPTIMIZE_PERFORMED) {
                            succeeded = false;
                        }
                        // Make sure artd does not leak even if the caller holds
                        // `cancellationSignal` forever.
                        cancellationSignal.setOnCancelListener(null);
                    }
                }

                if (profile != null && succeeded) {
                    if (profile.getTag() == ProfilePath.tmpRefProfilePath) {
                        // Commit the profile only if dexopt succeeds.
                        if (commitProfileChanges(profile.getTmpRefProfilePath())) {
                            profile = null;
                        }
                    }
                    if (profileMerged) {
                        // Note that this is just an optimization, to reduce the amount of data that
                        // the runtime writes on every profile save. The profile merge result on the
                        // next run won't change regardless of whether the cleanup is done or not
                        // because profman only looks at the diff.
                        // A caveat is that it may delete more than what has been merged, if the
                        // runtime writes additional entries between the merge and the cleanup, but
                        // this is fine because the runtime writes all JITed classes and methods on
                        // every save and the additional entries will likely be written back on the
                        // next save.
                        cleanupCurProfiles(pkgState, dexInfo);
                    }
                }
            } finally {
                if (profile != null && profile.getTag() == ProfilePath.tmpRefProfilePath) {
                    mInjector.getArtd().deleteProfile(profile);
                }
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

    boolean isSharedLibrary(@NonNull AndroidPackageApi pkg) {
        // TODO(b/242688548): Package manager should provide a better API for this.
        return !TextUtils.isEmpty(pkg.getSdkLibName())
                || !TextUtils.isEmpty(pkg.getStaticSharedLibName())
                || !pkg.getLibraryNames().isEmpty();
    }

    /**
     * Returns a reference profile initialized from a prebuilt profile or a DM profile if exists, or
     * null otherwise.
     */
    @Nullable
    private ProfilePath initReferenceProfile(@NonNull PackageState pkgState,
            @NonNull DetailedPrimaryDexInfo dexInfo, int uid, int gid) throws RemoteException {
        String profileName = getProfileName(dexInfo.splitName());
        OutputProfile output = AidlUtils.buildOutputProfile(
                pkgState.getPackageName(), profileName, uid, gid, true /* isPublic */);

        ProfilePath prebuiltProfile = AidlUtils.buildProfilePathForPrebuilt(dexInfo.dexPath());
        try {
            // If the APK is really a prebuilt one, rewriting the profile is unnecessary because the
            // dex location is known at build time and is correctly set in the profile header.
            // However, the APK can also be an installed one, in which case partners may place a
            // profile file next to the APK at install time. Rewriting the profile in the latter
            // case is necessary.
            if (mInjector.getArtd().copyAndRewriteProfile(
                        prebuiltProfile, output, dexInfo.dexPath())) {
                return ProfilePath.tmpRefProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    String.format(
                            "Failed to use prebuilt profile [packageName = %s, profileName = %s]",
                            pkgState.getPackageName(), profileName),
                    e);
        }

        ProfilePath dmProfile = AidlUtils.buildProfilePathForDm(dexInfo.dexPath());
        try {
            if (mInjector.getArtd().copyAndRewriteProfile(dmProfile, output, dexInfo.dexPath())) {
                return ProfilePath.tmpRefProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    String.format("Failed to use profile in dex metadata file "
                                    + "[packageName = %s, profileName = %s]",
                            pkgState.getPackageName(), profileName),
                    e);
        }

        return null;
    }

    /**
     * Gets the existing reference profile if exists, or initializes a reference profile from an
     * external profile.
     *
     * @return A pair where the first element is the found or initialized profile, and the second
     *         element is true if the profile is readable by others. Or null if there is no
     *         reference profile or external profile to use.
     */
    @Nullable
    private Pair<ProfilePath, Boolean> getOrInitReferenceProfile(@NonNull PackageState pkgState,
            @NonNull DetailedPrimaryDexInfo dexInfo, int uid, int gid) throws RemoteException {
        String profileName = getProfileName(dexInfo.splitName());
        ProfilePath refProfile =
                AidlUtils.buildProfilePathForRef(pkgState.getPackageName(), profileName);
        try {
            if (mInjector.getArtd().isProfileUsable(refProfile, dexInfo.dexPath())) {
                boolean isOtherReadable = mInjector.getArtd().getProfileVisibility(refProfile)
                        == FileVisibility.OTHER_READABLE;
                return Pair.create(refProfile, isOtherReadable);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    String.format("Failed to use the existing reference profile "
                                    + "[packageName = %s, profileName = %s]",
                            pkgState.getPackageName(), profileName),
                    e);
        }

        ProfilePath initializedProfile = initReferenceProfile(pkgState, dexInfo, uid, gid);
        return initializedProfile != null ? Pair.create(initializedProfile, true) : null;
    }

    @NonNull
    public String getProfileName(@Nullable String splitName) {
        return splitName == null ? "primary" : splitName + ".split";
    }

    @NonNull
    PermissionSettings getPermissionSettings(int sharedGid, boolean canBePublic) {
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
            @NonNull AndroidPackageApi pkg, @NonNull OptimizeParams params,
            boolean isProfileGuidedFilter) {
        DexoptOptions dexoptOptions = new DexoptOptions();
        dexoptOptions.compilationReason = params.getReason();
        dexoptOptions.targetSdkVersion = pkg.getTargetSdkVersion();
        dexoptOptions.debuggable = pkg.isDebuggable() || isAlwaysDebuggable();
        // Generating a meaningful app image needs a profile to determine what to include in the
        // image. Otherwise, the app image will be nearly empty.
        // Additionally, disable app images if the app requests for the splits to be loaded in
        // isolation because app images are unsupported for multiple class loaders (b/72696798).
        dexoptOptions.generateAppImage = isProfileGuidedFilter
                && !PrimaryDexUtils.isIsolatedSplitLoading(pkg) && isAppImageEnabled();
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
    GetDexoptNeededResult getDexoptNeeded(@NonNull DexoptTarget target,
            @NonNull GetDexoptNeededOptions options) throws RemoteException {
        int dexoptTrigger = getDexoptTrigger(target, options);

        // The result should come from artd even if all the bits of `dexoptTrigger` are set
        // because the result also contains information about the usable VDEX file.
        GetDexoptNeededResult result = mInjector.getArtd().getDexoptNeeded(
                target.dexInfo().dexPath(), target.isa(), target.dexInfo().classLoaderContext(),
                target.compilerFilter(), dexoptTrigger);

        return result;
    }

    int getDexoptTrigger(@NonNull DexoptTarget target, @NonNull GetDexoptNeededOptions options)
            throws RemoteException {
        if ((options.flags() & ArtFlags.FLAG_FORCE) != 0) {
            return DexoptTrigger.COMPILER_FILTER_IS_BETTER | DexoptTrigger.COMPILER_FILTER_IS_SAME
                    | DexoptTrigger.COMPILER_FILTER_IS_WORSE
                    | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
        }

        if ((options.flags() & ArtFlags.FLAG_SHOULD_DOWNGRADE) != 0) {
            return DexoptTrigger.COMPILER_FILTER_IS_WORSE;
        }

        int dexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
        if (options.profileMerged()) {
            dexoptTrigger |= DexoptTrigger.COMPILER_FILTER_IS_SAME;
        }

        ArtifactsPath existingArtifactsPath = AidlUtils.buildArtifactsPath(
                target.dexInfo().dexPath(), target.isa(), target.isInDalvikCache());

        if (options.needsToBePublic()
                && mInjector.getArtd().getArtifactsVisibility(existingArtifactsPath)
                        == FileVisibility.NOT_OTHER_READABLE) {
            // Typically, this happens after an app starts being used by other apps.
            // This case should be the same as force as we have no choice but to trigger a new
            // dexopt.
            dexoptTrigger |=
                    DexoptTrigger.COMPILER_FILTER_IS_SAME | DexoptTrigger.COMPILER_FILTER_IS_WORSE;
        }

        return dexoptTrigger;
    }

    private DexoptResult dexoptFile(@NonNull DexoptTarget target, @Nullable ProfilePath profile,
            @NonNull GetDexoptNeededResult getDexoptNeededResult,
            @NonNull PermissionSettings permissionSettings, @PriorityClass int priorityClass,
            @NonNull DexoptOptions dexoptOptions, IArtdCancellationSignal artdCancellationSignal)
            throws RemoteException {
        OutputArtifacts outputArtifacts = AidlUtils.buildOutputArtifacts(target.dexInfo().dexPath(),
                target.isa(), target.isInDalvikCache(), permissionSettings);

        VdexPath inputVdex =
                getInputVdex(getDexoptNeededResult, target.dexInfo().dexPath(), target.isa());

        return mInjector.getArtd().dexopt(outputArtifacts, target.dexInfo().dexPath(), target.isa(),
                target.dexInfo().classLoaderContext(), target.compilerFilter(), profile, inputVdex,
                priorityClass, dexoptOptions, artdCancellationSignal);
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

    private boolean commitProfileChanges(@NonNull TmpRefProfilePath profile)
            throws RemoteException {
        try {
            mInjector.getArtd().commitTmpProfile(profile);
            return true;
        } catch (ServiceSpecificException e) {
            RefProfilePath refProfilePath = profile.refProfilePath;
            Log.e(TAG,
                    String.format(
                            "Failed to commit profile changes [packageName = %s, profileName = %s]",
                            refProfilePath.packageName, refProfilePath.profileName),
                    e);
            return false;
        }
    }

    @Nullable
    private ProfilePath mergeProfiles(@NonNull PackageState pkgState,
            @NonNull DetailedPrimaryDexInfo dexInfo, int uid, int gid,
            @Nullable ProfilePath referenceProfile) throws RemoteException {
        String profileName = getProfileName(dexInfo.splitName());
        OutputProfile output = AidlUtils.buildOutputProfile(
                pkgState.getPackageName(), profileName, uid, gid, false /* isPublic */);

        try {
            if (mInjector.getArtd().mergeProfiles(getCurProfiles(pkgState, dexInfo),
                        referenceProfile, output, dexInfo.dexPath())) {
                return ProfilePath.tmpRefProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    String.format("Failed to merge profiles [packageName = %s, profileName = %s]",
                            pkgState.getPackageName(), getProfileName(dexInfo.splitName())),
                    e);
        }

        return null;
    }

    private void cleanupCurProfiles(@NonNull PackageState pkgState,
            @NonNull DetailedPrimaryDexInfo dexInfo) throws RemoteException {
        for (ProfilePath profile : getCurProfiles(pkgState, dexInfo)) {
            mInjector.getArtd().deleteProfile(profile);
        }
    }

    @NonNull
    private List<ProfilePath> getCurProfiles(
            @NonNull PackageState pkgState, @NonNull DetailedPrimaryDexInfo dexInfo) {
        List<ProfilePath> profiles = new ArrayList<>();
        for (UserHandle handle :
                mInjector.getUserManager().getUserHandles(true /* excludeDying */)) {
            int userId = handle.getIdentifier();
            PackageUserState userState = pkgState.getUserStateOrDefault(userId);
            if (userState.isInstalled()) {
                profiles.add(AidlUtils.buildProfilePathForCur(
                        userId, pkgState.getPackageName(), getProfileName(dexInfo.splitName())));
            }
        }
        return profiles;
    }

    @AutoValue
    abstract static class DexoptTarget {
        abstract @NonNull DetailedPrimaryDexInfo dexInfo();
        abstract @NonNull String isa();
        abstract boolean isInDalvikCache();
        abstract @NonNull String compilerFilter();

        static Builder builder() {
            return new AutoValue_PrimaryDexOptimizer_DexoptTarget.Builder();
        }

        @AutoValue.Builder
        abstract static class Builder {
            abstract Builder setDexInfo(@NonNull DetailedPrimaryDexInfo value);
            abstract Builder setIsa(@NonNull String value);
            abstract Builder setIsInDalvikCache(boolean value);
            abstract Builder setCompilerFilter(@NonNull String value);
            abstract DexoptTarget build();
        }
    }

    @AutoValue
    abstract static class GetDexoptNeededOptions {
        abstract @OptimizeFlags int flags();
        abstract boolean profileMerged();
        abstract boolean needsToBePublic();

        static Builder builder() {
            return new AutoValue_PrimaryDexOptimizer_GetDexoptNeededOptions.Builder();
        }

        @AutoValue.Builder
        abstract static class Builder {
            abstract Builder setFlags(@OptimizeFlags int value);
            abstract Builder setProfileMerged(boolean value);
            abstract Builder setNeedsToBePublic(boolean value);
            abstract GetDexoptNeededOptions build();
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

        boolean isUsedByOtherApps(@NonNull String packageName) {
            // TODO(jiakaiz): Get the real value.
            return false;
        }

        @NonNull
        UserManager getUserManager() {
            return mContext.getSystemService(UserManager.class);
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }
    }
}
