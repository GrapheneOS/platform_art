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

import static com.android.server.art.ArtManagerLocal.AdjustCompilerFilterCallback;
import static com.android.server.art.DexMetadataHelper.DexMetadataInfo;
import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.ProfilePath.TmpProfilePath;
import static com.android.server.art.Utils.Abi;
import static com.android.server.art.Utils.InitProfileResult;
import static com.android.server.art.model.ArtFlags.DexoptFlags;
import static com.android.server.art.model.Config.Callback;
import static com.android.server.art.model.DexoptResult.DexContainerFileDexoptResult;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UserManager;
import android.os.storage.StorageManager;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.Dex2OatStatsReporter.Dex2OatResult;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.Config;
import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.DexFile;

import com.google.auto.value.AutoValue;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** @hide */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public abstract class Dexopter<DexInfoType extends DetailedDexInfo> {
    private static final List<String> ART_PACKAGE_NAMES =
            List.of("com.google.android.art", "com.android.art", "com.google.android.go.art");

    @NonNull protected final Injector mInjector;
    @NonNull protected final PackageState mPkgState;
    /** This is always {@code mPkgState.getAndroidPackage()} and guaranteed to be non-null. */
    @NonNull protected final AndroidPackage mPkg;
    @NonNull protected final DexoptParams mParams;
    @NonNull protected final CancellationSignal mCancellationSignal;

    protected Dexopter(@NonNull Injector injector, @NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull DexoptParams params,
            @NonNull CancellationSignal cancellationSignal) {
        mInjector = injector;
        mPkgState = pkgState;
        mPkg = pkg;
        mParams = params;
        mCancellationSignal = cancellationSignal;
    }

    /**
     * DO NOT use this method directly. Use {@link
     * ArtManagerLocal#dexoptPackage(PackageManagerLocal.FilteredSnapshot, String,
     * DexoptParams)}.
     */
    @NonNull
    public final List<DexContainerFileDexoptResult> dexopt() throws RemoteException {
        if (SystemProperties.getBoolean("dalvik.vm.disable-art-service-dexopt", false /* def */)) {
            AsLog.i("Dexopt skipped because it's disabled by system property");
            return List.of();
        }

        List<DexContainerFileDexoptResult> results = new ArrayList<>();

        boolean isInDalvikCache = isInDalvikCache();

        for (DexInfoType dexInfo : getDexInfoList()) {
            ProfilePath profile = null;
            boolean succeeded = true;
            List<String> externalProfileErrors = List.of();
            try {
                if (!isDexoptable(dexInfo)) {
                    continue;
                }

                String compilerFilter = adjustCompilerFilter(mParams.getCompilerFilter(), dexInfo);
                if (compilerFilter.equals(DexoptParams.COMPILER_FILTER_NOOP)) {
                    continue;
                }

                if (mInjector.isPreReboot() && !isDexFileFound(dexInfo)) {
                    // In the pre-reboot case, it's possible that a dex file doesn't exist in the
                    // new system image. Although code below can gracefully handle failures, those
                    // failures can be red herrings in metrics and bug reports, so we skip
                    // non-existing dex files to avoid them.
                    continue;
                }

                DexMetadataInfo dmInfo =
                        mInjector.getDexMetadataHelper().getDexMetadataInfo(buildDmPath(dexInfo));

                boolean needsToBeShared = needsToBeShared(dexInfo);
                boolean isOtherReadable = true;
                // If true, implies that the profile has changed since the last compilation.
                boolean profileMerged = false;
                if (DexFile.isProfileGuidedCompilerFilter(compilerFilter)) {
                    if (!dmInfo.config().getEnableEmbeddedProfile()) {
                        String dmPath = DexMetadataHelper.getDmPath(
                                Objects.requireNonNull(dmInfo.dmPath()));
                        AsLog.i("Embedded profile disabled by config in the dm file " + dmPath);
                    }

                    if (needsToBeShared) {
                        InitProfileResult result = initReferenceProfile(
                                dexInfo, dmInfo.config().getEnableEmbeddedProfile());
                        profile = result.profile();
                        isOtherReadable = result.isOtherReadable();
                        externalProfileErrors = result.externalProfileErrors();
                    } else {
                        InitProfileResult result = getOrInitReferenceProfile(
                                dexInfo, dmInfo.config().getEnableEmbeddedProfile());
                        profile = result.profile();
                        isOtherReadable = result.isOtherReadable();
                        externalProfileErrors = result.externalProfileErrors();
                        ProfilePath mergedProfile = mergeProfiles(dexInfo, profile);
                        if (mergedProfile != null) {
                            if (profile != null && profile.getTag() == ProfilePath.tmpProfilePath) {
                                mInjector.getArtd().deleteProfile(profile);
                            }
                            profile = mergedProfile;
                            isOtherReadable = false;
                            profileMerged = true;
                        }
                    }
                    if (profile == null) {
                        // A profile guided dexopt with no profile is essentially 'verify',
                        // and dex2oat already makes this transformation. However, we need to
                        // explicitly make this transformation here to guide the later decisions
                        // such as whether the artifacts can be public and whether dexopt is needed.
                        compilerFilter = printAdjustCompilerFilterReason(compilerFilter,
                                needsToBeShared ? ReasonMapping.getCompilerFilterForShared()
                                                : "verify",
                                "there is no valid profile"
                                        + (needsToBeShared ? " and the package needs to be shared"
                                                           : ""));
                    }
                }
                boolean isProfileGuidedCompilerFilter =
                        DexFile.isProfileGuidedCompilerFilter(compilerFilter);
                Utils.check(isProfileGuidedCompilerFilter == (profile != null));

                boolean canBePublic = (!isProfileGuidedCompilerFilter || isOtherReadable)
                        && isDexFilePublic(dexInfo);
                Utils.check(Utils.implies(needsToBeShared, canBePublic));
                PermissionSettings permissionSettings = getPermissionSettings(dexInfo, canBePublic);

                DexoptOptions dexoptOptions =
                        getDexoptOptions(dexInfo, isProfileGuidedCompilerFilter);

                for (Abi abi : getAllAbis(dexInfo)) {
                    @DexoptResult.DexoptResultStatus int status = DexoptResult.DEXOPT_SKIPPED;
                    long wallTimeMs = 0;
                    long cpuTimeMs = 0;
                    long sizeBytes = 0;
                    long sizeBeforeBytes = 0;
                    Dex2OatResult dex2OatResult = Dex2OatResult.notRun();
                    @DexoptResult.DexoptResultExtendedStatusFlags int extendedStatusFlags = 0;
                    try {
                        var target = DexoptTarget.<DexInfoType>builder()
                                             .setDexInfo(dexInfo)
                                             .setIsa(abi.isa())
                                             .setIsInDalvikCache(isInDalvikCache)
                                             .setCompilerFilter(compilerFilter)
                                             .setDmPath(dmInfo.dmPath())
                                             .build();
                        var options = GetDexoptNeededOptions.builder()
                                              .setProfileMerged(profileMerged)
                                              .setFlags(mParams.getFlags())
                                              .setNeedsToBePublic(needsToBeShared)
                                              .build();

                        if (mInjector.isPreReboot()) {
                            ArtifactsPath existingArtifacts =
                                    AidlUtils.buildArtifactsPathAsInputPreReboot(
                                            target.dexInfo().dexPath(), target.isa(),
                                            target.isInDalvikCache());
                            if (mInjector.getArtd().getArtifactsVisibility(existingArtifacts)
                                    != FileVisibility.NOT_FOUND) {
                                // Because `getDexoptNeeded` doesn't check Pre-reboot artifacts, we
                                // do a simple check here to handle job resuming. If the Pre-reboot
                                // artifacts exist, we assume they are up-to-date because
                                // `PreRebootDexoptJob` would otherwise clean them up, so we skip
                                // this dex file. The profile and the dex file may have been changed
                                // since the last cancelled job run, but we don't handle such cases
                                // because we are supposed to dexopt every dex file only once for
                                // each ISA.
                                extendedStatusFlags |=
                                        DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST;
                                continue;
                            }
                        }

                        GetDexoptNeededResult getDexoptNeededResult =
                                getDexoptNeeded(target, options);

                        if (!getDexoptNeededResult.hasDexCode) {
                            extendedStatusFlags |= DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE;
                        }

                        if (!getDexoptNeededResult.isDexoptNeeded) {
                            continue;
                        }

                        try {
                            // `StorageManager.getAllocatableBytes` returns (free space + space used
                            // by clearable cache - low storage threshold). Since we only compare
                            // the result with 0, the clearable cache doesn't make a difference.
                            // When the free space is below the threshold, there should be no
                            // clearable cache left because system cleans up cache every minute.
                            if ((mParams.getFlags() & ArtFlags.FLAG_SKIP_IF_STORAGE_LOW) != 0
                                    && mInjector.getStorageManager().getAllocatableBytes(
                                               mPkg.getStorageUuid())
                                            <= 0) {
                                extendedStatusFlags |= DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW;
                                continue;
                            }
                        } catch (IOException e) {
                            AsLog.e("Failed to check storage. Assuming storage not low", e);
                        }

                        IArtdCancellationSignal artdCancellationSignal =
                                mInjector.getArtd().createCancellationSignal();
                        mCancellationSignal.setOnCancelListener(() -> {
                            try {
                                artdCancellationSignal.cancel();
                            } catch (RemoteException e) {
                                AsLog.e("An error occurred when sending a cancellation signal", e);
                            }
                        });

                        ArtdDexoptResult dexoptResult = dexoptFile(target, profile,
                                getDexoptNeededResult, permissionSettings,
                                mParams.getPriorityClass(), dexoptOptions, artdCancellationSignal);
                        status = dexoptResult.cancelled ? DexoptResult.DEXOPT_CANCELLED
                                                        : DexoptResult.DEXOPT_PERFORMED;
                        wallTimeMs = dexoptResult.wallTimeMs;
                        cpuTimeMs = dexoptResult.cpuTimeMs;
                        sizeBytes = dexoptResult.sizeBytes;
                        sizeBeforeBytes = dexoptResult.sizeBeforeBytes;
                        dex2OatResult = dexoptResult.cancelled ? Dex2OatResult.cancelled()
                                                               : Dex2OatResult.exited(0);

                        if (status == DexoptResult.DEXOPT_CANCELLED) {
                            return results;
                        }
                    } catch (ServiceSpecificException e) {
                        // Log the error and continue.
                        AsLog.e(String.format("Failed to dexopt [packageName = %s, dexPath = %s, "
                                                + "isa = %s, classLoaderContext = %s]",
                                        mPkgState.getPackageName(), dexInfo.dexPath(), abi.isa(),
                                        dexInfo.classLoaderContext()),
                                e);
                        status = DexoptResult.DEXOPT_FAILED;

                        // Parse status, exit code and signal from the dex2oat error message
                        Pattern pattern = Pattern.compile(
                                "\\[status=(-?\\d+),exit_code=(-?\\d+),signal=(-?\\d+)]");
                        Matcher matcher = pattern.matcher(Objects.requireNonNull(e.getMessage()));
                        if (matcher.matches()) {
                            dex2OatResult = new Dex2OatResult(Integer.parseInt(matcher.group(1)),
                                    Integer.parseInt(matcher.group(2)),
                                    Integer.parseInt(matcher.group(3)));
                        }
                    } finally {
                        if (!externalProfileErrors.isEmpty()) {
                            extendedStatusFlags |= DexoptResult.EXTENDED_BAD_EXTERNAL_PROFILE;
                        }
                        var result = DexContainerFileDexoptResult.create(dexInfo.dexPath(),
                                abi.isPrimaryAbi(), abi.name(), compilerFilter, status, wallTimeMs,
                                cpuTimeMs, sizeBytes, sizeBeforeBytes, extendedStatusFlags,
                                externalProfileErrors);
                        AsLog.i(String.format("Dexopt result: [packageName = %s] %s",
                                mPkgState.getPackageName(), result));
                        results.add(result);
                        if (status != DexoptResult.DEXOPT_SKIPPED
                                && status != DexoptResult.DEXOPT_PERFORMED) {
                            succeeded = false;
                        }
                        // Make sure artd does not leak even if the caller holds
                        // `mCancellationSignal` forever.
                        mCancellationSignal.setOnCancelListener(null);

                        // Variables used in lambda needs to be effectively final.
                        Dex2OatResult finalDex2OatResult = dex2OatResult;
                        mInjector.getReporterExecutor().execute(
                                ()
                                        -> Dex2OatStatsReporter.report(mPkgState.getAppId(),
                                                result.getActualCompilerFilter(),
                                                mParams.getReason(), dmInfo.type(), dexInfo,
                                                abi.isa(), finalDex2OatResult,
                                                result.getSizeBytes(),
                                                result.getDex2oatWallTimeMillis()));
                    }
                }

                if (profile != null && succeeded) {
                    if (profile.getTag() == ProfilePath.tmpProfilePath) {
                        // Commit the profile only if dexopt succeeds.
                        if (commitProfileChanges(profile.getTmpProfilePath())) {
                            profile = null;
                        }
                    }
                    // We keep the current profiles in the Pre-reboot Dexopt case, to leave it to
                    // background dexopt.
                    if (profileMerged && !mInjector.isPreReboot()) {
                        // Note that this is just an optimization, to reduce the amount of data that
                        // the runtime writes on every profile save. The profile merge result on the
                        // next run won't change regardless of whether the cleanup is done or not
                        // because profman only looks at the diff.
                        // A caveat is that it may delete more than what has been merged, if the
                        // runtime writes additional entries between the merge and the cleanup, but
                        // this is fine because the runtime writes all JITed classes and methods on
                        // every save and the additional entries will likely be written back on the
                        // next save.
                        cleanupCurProfiles(dexInfo);
                    }
                }
            } finally {
                if (profile != null && profile.getTag() == ProfilePath.tmpProfilePath) {
                    mInjector.getArtd().deleteProfile(profile);
                }
            }
        }

        return results;
    }

    // The javadoc on `AdjustCompilerFilterCallback.onAdjustCompilerFilter` may need updating when
    // this method is changed.
    @NonNull
    private String adjustCompilerFilter(
            @NonNull String targetCompilerFilter, @NonNull DexInfoType dexInfo) {
        if (mPkgState.isSystem()) {
            targetCompilerFilter = "speed";
        }

        if ((mParams.getFlags() & ArtFlags.FLAG_FORCE_COMPILER_FILTER) == 0) {
            Callback<AdjustCompilerFilterCallback, Void> callback =
                    mInjector.getConfig().getAdjustCompilerFilterCallback();
            if (callback != null) {
                // Local variables passed to the lambda must be final or effectively final.
                final String originalCompilerFilter = targetCompilerFilter;
                targetCompilerFilter = printAdjustCompilerFilterReason(
                        targetCompilerFilter, Utils.executeAndWait(callback.executor(), () -> {
                            return callback.get().onAdjustCompilerFilter(mPkgState.getPackageName(),
                                    originalCompilerFilter, mParams.getReason());
                        }), "of AdjustCompilerFilterCallback");
            }
        }

        // Code below should only downgrade the compiler filter. Don't upgrade the compiler filter
        // beyond this point!

        // We force vmSafeMode on debuggable apps as well:
        //  - the runtime ignores their compiled code
        //  - they generally have lots of methods that could make the compiler used run out of
        //    memory (b/130828957)
        // Note that forcing the compiler filter here applies to all compilations (even if they
        // are done via adb shell commands). This is okay because the runtime will ignore the
        // compiled code anyway.
        if (mPkg.isVmSafeMode() || mPkg.isDebuggable()) {
            targetCompilerFilter = printAdjustCompilerFilterReason(targetCompilerFilter,
                    DexFile.getSafeModeCompilerFilter(targetCompilerFilter),
                    mPkg.isVmSafeMode() ? "the package requests VM safe mode"
                                        : "the package is debuggable");
        }

        // We cannot do AOT compilation if we don't have a valid class loader context.
        if (dexInfo.classLoaderContext() == null
                && DexFile.isOptimizedCompilerFilter(targetCompilerFilter)) {
            targetCompilerFilter = printAdjustCompilerFilterReason(
                    targetCompilerFilter, "verify", "there is no valid class loader context");
        }

        // This application wants to use the embedded dex in the APK, rather than extracted or
        // locally compiled variants, so we only verify it.
        // "verify" does not prevent dex2oat from extracting the dex code, but in practice, dex2oat
        // won't extract the dex code because the APK is uncompressed, and the assumption is that
        // such applications always use uncompressed APKs.
        if (mPkg.isUseEmbeddedDex() && DexFile.isOptimizedCompilerFilter(targetCompilerFilter)) {
            targetCompilerFilter = printAdjustCompilerFilterReason(
                    targetCompilerFilter, "verify", "the package requests to use embedded dex");
        }

        if ((mParams.getFlags() & ArtFlags.FLAG_IGNORE_PROFILE) != 0
                && DexFile.isProfileGuidedCompilerFilter(targetCompilerFilter)) {
            targetCompilerFilter = printAdjustCompilerFilterReason(
                    targetCompilerFilter, "verify", "the user requests to ignore the profile");
        }

        // pre-reboot dexopt runs ART code from next OTA update, it doesn't have access to
        // system_server code
        if (!ReasonMapping.REASON_PRE_REBOOT_DEXOPT.equals(mParams.getReason())) {
            String override = mInjector.getPackageManagerLocal()
                    .maybeOverrideCompilerFilter(targetCompilerFilter, mPkg, mParams);

            if (override != null) {
                return override;
            }
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

    private @NonNull String printAdjustCompilerFilterReason(@NonNull String oldCompilerFilter,
            @NonNull String newCompilerFilter, @NonNull String reason) {
        if (!oldCompilerFilter.equals(newCompilerFilter)) {
            AsLog.i(String.format(
                    "Adjusting the compiler filter for '%s' from '%s' to '%s' because %s",
                    mPkgState.getPackageName(), oldCompilerFilter, newCompilerFilter, reason));
        }
        return newCompilerFilter;
    }

    /** @see Utils#getOrInitReferenceProfile */
    @Nullable
    private InitProfileResult getOrInitReferenceProfile(
            @NonNull DexInfoType dexInfo, boolean enableEmbeddedProfile) throws RemoteException {
        return Utils.getOrInitReferenceProfile(mInjector.getArtd(), dexInfo.dexPath(),
                buildRefProfilePathAsInput(dexInfo), getExternalProfiles(dexInfo),
                enableEmbeddedProfile, buildOutputProfile(dexInfo, true /* isPublic */));
    }

    @Nullable
    private InitProfileResult initReferenceProfile(
            @NonNull DexInfoType dexInfo, boolean enableEmbeddedProfile) throws RemoteException {
        return Utils.initReferenceProfile(mInjector.getArtd(), dexInfo.dexPath(),
                getExternalProfiles(dexInfo), enableEmbeddedProfile,
                buildOutputProfile(dexInfo, true /* isPublic */));
    }

    @NonNull
    private DexoptOptions getDexoptOptions(
            @NonNull DexInfoType dexInfo, boolean isProfileGuidedFilter) {
        DexoptOptions dexoptOptions = new DexoptOptions();
        dexoptOptions.compilationReason = mParams.getReason();
        dexoptOptions.targetSdkVersion = mPkg.getTargetSdkVersion();
        dexoptOptions.debuggable = mPkg.isDebuggable() || isAlwaysDebuggable();
        // Generating a meaningful app image needs a profile to determine what to include in the
        // image. Otherwise, the app image will be nearly empty.
        dexoptOptions.generateAppImage = isProfileGuidedFilter && isAppImageEnabled();
        dexoptOptions.hiddenApiPolicyEnabled = isHiddenApiPolicyEnabled();
        dexoptOptions.comments =
                String.format("app-name:%s,app-version-name:%s,app-version-code:%d,art-version:%d",
                        mPkgState.getPackageName(), mPkg.getVersionName(),
                        mPkg.getLongVersionCode(), mInjector.getArtVersion());
        return dexoptOptions;
    }

    private boolean isAlwaysDebuggable() {
        return SystemProperties.getBoolean("dalvik.vm.always_debuggable", false /* def */);
    }

    private boolean isAppImageEnabled() {
        return !SystemProperties.get("dalvik.vm.appimageformat").isEmpty();
    }

    private boolean isHiddenApiPolicyEnabled() {
        return mPkgState.getHiddenApiEnforcementPolicy()
                != ApplicationInfo.HIDDEN_API_ENFORCEMENT_DISABLED;
    }

    @NonNull
    GetDexoptNeededResult getDexoptNeeded(@NonNull DexoptTarget<DexInfoType> target,
            @NonNull GetDexoptNeededOptions options) throws RemoteException {
        int dexoptTrigger = getDexoptTrigger(target, options);

        // The result should come from artd even if all the bits of `dexoptTrigger` are set
        // because the result also contains information about the usable VDEX file.
        // Note that the class loader context can be null. In that case, we intentionally pass the
        // null value down to lower levels to indicate that the class loader context check should be
        // skipped because we are only going to verify the dex code (see `adjustCompilerFilter`).
        GetDexoptNeededResult result = mInjector.getArtd().getDexoptNeeded(
                target.dexInfo().dexPath(), target.isa(), target.dexInfo().classLoaderContext(),
                target.compilerFilter(), dexoptTrigger);

        return result;
    }

    int getDexoptTrigger(@NonNull DexoptTarget<DexInfoType> target,
            @NonNull GetDexoptNeededOptions options) throws RemoteException {
        if ((options.flags() & ArtFlags.FLAG_FORCE) != 0) {
            return DexoptTrigger.COMPILER_FILTER_IS_BETTER | DexoptTrigger.COMPILER_FILTER_IS_SAME
                    | DexoptTrigger.COMPILER_FILTER_IS_WORSE
                    | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE
                    | DexoptTrigger.NEED_EXTRACTION;
        }

        if ((options.flags() & ArtFlags.FLAG_SHOULD_DOWNGRADE) != 0) {
            return DexoptTrigger.COMPILER_FILTER_IS_WORSE;
        }

        int dexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
                | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
        if (options.profileMerged()) {
            dexoptTrigger |= DexoptTrigger.COMPILER_FILTER_IS_SAME;
        }

        ArtifactsPath existingArtifactsPath = AidlUtils.buildArtifactsPathAsInput(
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

    private ArtdDexoptResult dexoptFile(@NonNull DexoptTarget<DexInfoType> target,
            @Nullable ProfilePath profile, @NonNull GetDexoptNeededResult getDexoptNeededResult,
            @NonNull PermissionSettings permissionSettings, @PriorityClass int priorityClass,
            @NonNull DexoptOptions dexoptOptions, IArtdCancellationSignal artdCancellationSignal)
            throws RemoteException {
        OutputArtifacts outputArtifacts =
                AidlUtils.buildOutputArtifacts(target.dexInfo().dexPath(), target.isa(),
                        target.isInDalvikCache(), permissionSettings, mInjector.isPreReboot());

        VdexPath inputVdex =
                getInputVdex(getDexoptNeededResult, target.dexInfo().dexPath(), target.isa());

        if (target.dmPath() != null
                && ReasonMapping.REASONS_FOR_INSTALL.contains(dexoptOptions.compilationReason)) {
            // If the DM file is passed to dex2oat, then add the "-dm" suffix to the reason (e.g.,
            // "install-dm").
            // Note that this only applies to reasons for app install because the goal is to give
            // Play a signal that a DM file is downloaded at install time. We actually pass the DM
            // file regardless of the compilation reason, but we don't append a suffix when the
            // compilation reason is not a reason for app install.
            // Also note that the "-dm" suffix does NOT imply anything in the DM file being used by
            // dex2oat. dex2oat may ignore some contents of the DM file when appropriate. The
            // compilation reason can still be "install-dm" even if dex2oat left all contents of the
            // DM file unused or an empty DM file is passed to dex2oat.
            dexoptOptions.compilationReason = dexoptOptions.compilationReason + "-dm";
        }

        ArtdDexoptResult result = mInjector.getArtd().dexopt(outputArtifacts,
                target.dexInfo().dexPath(), target.isa(), target.dexInfo().classLoaderContext(),
                target.compilerFilter(), profile, inputVdex, target.dmPath(), priorityClass,
                dexoptOptions, artdCancellationSignal);

        // Delete the existing runtime images after the dexopt is performed, even if they are still
        // usable (e.g., the compiler filter is "verify"). This is to make sure the dexopt puts the
        // dex file into a certain dexopt state, to make it easier for debugging and testing. It's
        // also an optimization to release disk space as soon as possible. However, not doing the
        // deletion here does not affect correctness or waste disk space: if the existing runtime
        // images are still usable, technically, they can still be used to improve runtime
        // performance; if they are no longer usable, they will be deleted by the file GC during the
        // daily background dexopt job anyway.
        // We keep the runtime artifacts in the Pre-reboot Dexopt case because they are still needed
        // before the reboot.
        if (!result.cancelled && !mInjector.isPreReboot()) {
            mInjector.getArtd().deleteRuntimeArtifacts(AidlUtils.buildRuntimeArtifactsPath(
                    mPkgState.getPackageName(), target.dexInfo().dexPath(), target.isa()));
        }

        return result;
    }

    @Nullable
    private VdexPath getInputVdex(@NonNull GetDexoptNeededResult getDexoptNeededResult,
            @NonNull String dexPath, @NonNull String isa) {
        if (!getDexoptNeededResult.isVdexUsable) {
            return null;
        }
        switch (getDexoptNeededResult.artifactsLocation) {
            case ArtifactsLocation.DALVIK_CACHE:
                return VdexPath.artifactsPath(AidlUtils.buildArtifactsPathAsInput(
                        dexPath, isa, true /* isInDalvikCache */));
            case ArtifactsLocation.NEXT_TO_DEX:
                return VdexPath.artifactsPath(AidlUtils.buildArtifactsPathAsInput(
                        dexPath, isa, false /* isInDalvikCache */));
            case ArtifactsLocation.DM:
                // The DM file is passed to dex2oat as a separate flag whenever it exists.
                return null;
            default:
                // This should never happen as the value is got from artd.
                throw new IllegalStateException(
                        "Unknown artifacts location " + getDexoptNeededResult.artifactsLocation);
        }
    }

    private boolean commitProfileChanges(@NonNull TmpProfilePath profile) throws RemoteException {
        try {
            mInjector.getArtd().commitTmpProfile(profile);
            return true;
        } catch (ServiceSpecificException e) {
            AsLog.e("Failed to commit profile changes " + AidlUtils.toString(profile.finalPath), e);
            return false;
        }
    }

    @Nullable
    private ProfilePath mergeProfiles(@NonNull DexInfoType dexInfo,
            @Nullable ProfilePath referenceProfile) throws RemoteException {
        OutputProfile output = buildOutputProfile(dexInfo, false /* isPublic */);

        var options = new MergeProfileOptions();
        options.forceMerge = (mParams.getFlags() & ArtFlags.FLAG_FORCE_MERGE_PROFILE) != 0;

        try {
            if (mInjector.getArtd().mergeProfiles(getCurProfiles(dexInfo), referenceProfile, output,
                        List.of(dexInfo.dexPath()), options)) {
                return ProfilePath.tmpProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            AsLog.e("Failed to merge profiles " + AidlUtils.toString(output.profilePath.finalPath),
                    e);
        }

        return null;
    }

    private void cleanupCurProfiles(@NonNull DexInfoType dexInfo) throws RemoteException {
        for (ProfilePath profile : getCurProfiles(dexInfo)) {
            mInjector.getArtd().deleteProfile(profile);
        }
    }

    // Methods to be implemented by child classes.

    /** Returns true if the artifacts should be written to the global dalvik-cache directory. */
    protected abstract boolean isInDalvikCache() throws RemoteException;

    /** Returns information about all dex files. */
    @NonNull protected abstract List<DexInfoType> getDexInfoList();

    /** Returns true if the given dex file should be dexopted. */
    protected abstract boolean isDexoptable(@NonNull DexInfoType dexInfo);

    /**
     * Returns true if the artifacts should be shared with other apps. Note that this must imply
     * {@link #isDexFilePublic(DexInfoType)}.
     */
    protected abstract boolean needsToBeShared(@NonNull DexInfoType dexInfo);

    /**
     * Returns true if the filesystem permission of the dex file has the "read" bit for "others"
     * (S_IROTH).
     */
    protected abstract boolean isDexFilePublic(@NonNull DexInfoType dexInfo);

    /**
     * Returns true if the dex file is found.
     */
    protected abstract boolean isDexFileFound(@NonNull DexInfoType dexInfo);

    /**
     * Returns a list of external profiles (e.g., a DM profile) that the reference profile can be
     * initialized from, in the order of preference.
     */
    @NonNull protected abstract List<ProfilePath> getExternalProfiles(@NonNull DexInfoType dexInfo);

    /** Returns the permission settings to use for the artifacts of the given dex file. */
    @NonNull
    protected abstract PermissionSettings getPermissionSettings(
            @NonNull DexInfoType dexInfo, boolean canBePublic);

    /** Returns all ABIs that the given dex file should be compiled for. */
    @NonNull protected abstract List<Abi> getAllAbis(@NonNull DexInfoType dexInfo);

    /** Returns the path to the reference profile of the given dex file. */
    @NonNull
    protected abstract ProfilePath buildRefProfilePathAsInput(@NonNull DexInfoType dexInfo);

    /**
     * Returns the data structure that represents the temporary profile to use during processing.
     */
    @NonNull
    protected abstract OutputProfile buildOutputProfile(
            @NonNull DexInfoType dexInfo, boolean isPublic);

    /** Returns the paths to the current profiles of the given dex file. */
    @NonNull protected abstract List<ProfilePath> getCurProfiles(@NonNull DexInfoType dexInfo);

    /**
     * Returns the path to the DM file that should be passed to dex2oat, or null if no DM file
     * should be passed.
     */
    @Nullable protected abstract DexMetadataPath buildDmPath(@NonNull DexInfoType dexInfo);

    @AutoValue
    abstract static class DexoptTarget<DexInfoType extends DetailedDexInfo> {
        abstract @NonNull DexInfoType dexInfo();
        abstract @NonNull String isa();
        abstract boolean isInDalvikCache();
        abstract @NonNull String compilerFilter();
        abstract @Nullable DexMetadataPath dmPath();

        static <DexInfoType extends DetailedDexInfo> Builder<DexInfoType> builder() {
            return new AutoValue_Dexopter_DexoptTarget.Builder<DexInfoType>();
        }

        @AutoValue.Builder
        abstract static class Builder<DexInfoType extends DetailedDexInfo> {
            abstract Builder setDexInfo(@NonNull DexInfoType value);
            abstract Builder setIsa(@NonNull String value);
            abstract Builder setIsInDalvikCache(boolean value);
            abstract Builder setCompilerFilter(@NonNull String value);
            abstract Builder setDmPath(@Nullable DexMetadataPath value);
            abstract DexoptTarget<DexInfoType> build();
        }
    }

    @AutoValue
    abstract static class GetDexoptNeededOptions {
        abstract @DexoptFlags int flags();
        abstract boolean profileMerged();
        abstract boolean needsToBePublic();

        static Builder builder() {
            return new AutoValue_Dexopter_GetDexoptNeededOptions.Builder();
        }

        @AutoValue.Builder
        abstract static class Builder {
            abstract Builder setFlags(@DexoptFlags int value);
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
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PROTECTED)
    public static class Injector {
        @NonNull private final Context mContext;
        @NonNull private final Config mConfig;
        @NonNull private final Executor mReporterExecutor;

        public Injector(@NonNull Context context, @NonNull Config config,
                @NonNull Executor reporterExecutor) {
            mContext = context;
            mConfig = config;
            mReporterExecutor = reporterExecutor;

            // Call the getters for various dependencies, to ensure correct initialization order.
            getUserManager();
            getDexUseManager();
            getStorageManager();
            GlobalInjector.getInstance().checkArtModuleServiceManager();
        }

        public boolean isSystemUiPackage(@NonNull String packageName) {
            return Utils.isSystemUiPackage(mContext, packageName);
        }

        public boolean isLauncherPackage(@NonNull String packageName) {
            return Utils.isLauncherPackage(mContext, packageName);
        }

        @NonNull
        public UserManager getUserManager() {
            return Objects.requireNonNull(mContext.getSystemService(UserManager.class));
        }

        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return GlobalInjector.getInstance().getDexUseManager();
        }

        @NonNull
        public IArtd getArtd() {
            return ArtdRefCache.getInstance().getArtd();
        }

        @NonNull
        public StorageManager getStorageManager() {
            return Objects.requireNonNull(mContext.getSystemService(StorageManager.class));
        }

        @NonNull
        private PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(PackageManagerLocal.class));
        }

        public long getArtVersion() {
            try (var snapshot = getPackageManagerLocal().withUnfilteredSnapshot()) {
                Map<String, PackageState> packageStates = snapshot.getPackageStates();
                for (String artPackageName : ART_PACKAGE_NAMES) {
                    PackageState pkgState = packageStates.get(artPackageName);
                    if (pkgState != null) {
                        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
                        return pkg.getLongVersionCode();
                    }
                }
            }
            return -1;
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }

        @NonNull
        public Executor getReporterExecutor() {
            return mReporterExecutor;
        }

        @NonNull
        public DexMetadataHelper getDexMetadataHelper() {
            return new DexMetadataHelper();
        }

        public boolean isPreReboot() {
            return GlobalInjector.getInstance().isPreReboot();
        }
    }
}
