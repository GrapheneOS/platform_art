/*
 * Copyright (C) 2023 The Android Open Source Project
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

import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.PrimaryDexInfo;
import static com.android.server.art.Utils.Abi;

import android.annotation.NonNull;
import android.content.Context;
import android.os.Binder;
import android.os.Build;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.UserHandle;
import android.os.UserManager;
import android.util.Pair;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.Immutable;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.DetailedDexInfo;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.DexFile;

import com.google.auto.value.AutoValue;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * A helper class to list files that ART Service consumes or produces.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class ArtFileManager {
    @NonNull private final Injector mInjector;

    public ArtFileManager(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public ArtFileManager(@NonNull Injector injector) {
        mInjector = injector;
    }

    @NonNull
    public List<Pair<DetailedDexInfo, Abi>> getDexAndAbis(
            @NonNull PackageState pkgState, @NonNull AndroidPackage pkg, @NonNull Options options) {
        List<Pair<DetailedDexInfo, Abi>> dexAndAbis = new ArrayList<>();
        if (options.forPrimaryDex()) {
            for (DetailedPrimaryDexInfo dexInfo :
                    PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
                for (Abi abi : Utils.getAllAbis(pkgState)) {
                    dexAndAbis.add(Pair.create(dexInfo, abi));
                }
            }
        }
        if (options.forSecondaryDex()) {
            List<? extends SecondaryDexInfo> dexInfos = getSecondaryDexInfo(pkgState, options);
            for (SecondaryDexInfo dexInfo : dexInfos) {
                if (!mInjector.isSystemOrRootOrShell()
                        && !mInjector.getCallingUserHandle().equals(dexInfo.userHandle())) {
                    continue;
                }
                for (Abi abi : Utils.getAllAbisForNames(dexInfo.abiNames(), pkgState)) {
                    dexAndAbis.add(Pair.create(dexInfo, abi));
                }
            }
        }
        return dexAndAbis;
    }

    /**
     * Returns the writable paths of artifacts, regardless of whether the artifacts exist or
     * whether they are usable.
     */
    @NonNull
    public WritableArtifactLists getWritableArtifacts(@NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull Options options) throws RemoteException {
        List<ArtifactsPath> artifacts = new ArrayList<>();
        List<SecureDexMetadataWithCompanionPaths> sdmFiles = new ArrayList<>();
        List<RuntimeArtifactsPath> runtimeArtifacts = new ArrayList<>();

        if (options.forPrimaryDex()) {
            boolean isInDalvikCache = Utils.isInDalvikCache(pkgState, mInjector.getArtd());
            for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                for (Abi abi : Utils.getAllAbis(pkgState)) {
                    artifacts.add(AidlUtils.buildArtifactsPathAsInput(
                            dexInfo.dexPath(), abi.isa(), isInDalvikCache));
                    // SDM files are only for primary dex files.
                    sdmFiles.add(AidlUtils.buildSecureDexMetadataWithCompanionPaths(
                            dexInfo.dexPath(), abi.isa(), isInDalvikCache));
                    // Runtime images are only generated for primary dex files.
                    runtimeArtifacts.add(AidlUtils.buildRuntimeArtifactsPath(
                            pkgState.getPackageName(), dexInfo.dexPath(), abi.isa()));
                }
            }
        }

        if (options.forSecondaryDex()) {
            for (SecondaryDexInfo dexInfo : getSecondaryDexInfo(pkgState, options)) {
                for (Abi abi : Utils.getAllAbisForNames(dexInfo.abiNames(), pkgState)) {
                    artifacts.add(AidlUtils.buildArtifactsPathAsInput(
                            dexInfo.dexPath(), abi.isa(), false /* isInDalvikCache */));
                }
            }
        }

        return new WritableArtifactLists(artifacts, sdmFiles, runtimeArtifacts);
    }

    /** Returns artifacts that are usable, regardless of whether they are writable. */
    @NonNull
    public UsableArtifactLists getUsableArtifacts(
            @NonNull PackageState pkgState, @NonNull AndroidPackage pkg) throws RemoteException {
        List<ArtifactsPath> artifacts = new ArrayList<>();
        List<VdexPath> vdexFiles = new ArrayList<>();
        List<SecureDexMetadataWithCompanionPaths> sdmFiles = new ArrayList<>();
        List<RuntimeArtifactsPath> runtimeArtifacts = new ArrayList<>();

        var options = ArtFileManager.Options.builder()
                              .setForPrimaryDex(true)
                              .setForSecondaryDex(true)
                              .setExcludeForObsoleteDexesAndLoaders(true)
                              .build();
        for (Pair<DetailedDexInfo, Abi> pair : getDexAndAbis(pkgState, pkg, options)) {
            DetailedDexInfo dexInfo = pair.first;
            Abi abi = pair.second;
            try {
                GetDexoptStatusResult result = mInjector.getArtd().getDexoptStatus(
                        dexInfo.dexPath(), abi.isa(), dexInfo.classLoaderContext());
                if (result.artifactsLocation == ArtifactsLocation.DALVIK_CACHE
                        || result.artifactsLocation == ArtifactsLocation.NEXT_TO_DEX) {
                    ArtifactsPath thisArtifacts =
                            AidlUtils.buildArtifactsPathAsInput(dexInfo.dexPath(), abi.isa(),
                                    result.artifactsLocation == ArtifactsLocation.DALVIK_CACHE);
                    if (result.compilationReason.equals(ArtConstants.REASON_VDEX)) {
                        // Only the VDEX file is usable.
                        vdexFiles.add(VdexPath.artifactsPath(thisArtifacts));
                    } else {
                        artifacts.add(thisArtifacts);
                    }
                } else if (result.artifactsLocation == ArtifactsLocation.SDM_DALVIK_CACHE
                        || result.artifactsLocation == ArtifactsLocation.SDM_NEXT_TO_DEX) {
                    sdmFiles.add(AidlUtils.buildSecureDexMetadataWithCompanionPaths(
                            dexInfo.dexPath(), abi.isa(),
                            result.artifactsLocation == ArtifactsLocation.SDM_DALVIK_CACHE));
                }

                if (result.artifactsLocation != ArtifactsLocation.NONE_OR_ERROR) {
                    // Runtime images are only generated for primary dex files.
                    if (dexInfo instanceof DetailedPrimaryDexInfo
                            && !DexFile.isOptimizedCompilerFilter(result.compilerFilter)) {
                        // Those not added to the list are definitely unusable, but those added to
                        // the list are not necessarily usable. For example, runtime artifacts can
                        // be outdated when the corresponding dex file is updated, but they may
                        // still show up in this list.
                        //
                        // However, this is not a severe problem. For `ArtManagerLocal.cleanup`, the
                        // worst result is only that we are keeping more runtime artifacts than
                        // needed. For `ArtManagerLocal.getArtManagedFileStats`, this is an edge
                        // case because the API call is transitively initiated by the app itself,
                        // and the runtime refreshes unusable runtime artifacts as soon as the app
                        // starts.
                        //
                        // TODO(jiakaiz): Improve this.
                        runtimeArtifacts.add(AidlUtils.buildRuntimeArtifactsPath(
                                pkgState.getPackageName(), dexInfo.dexPath(), abi.isa()));
                    }
                }
            } catch (ServiceSpecificException e) {
                AsLog.e(String.format(
                                "Failed to get dexopt status [packageName = %s, dexPath = %s, "
                                        + "isa = %s, classLoaderContext = %s]",
                                pkgState.getPackageName(), dexInfo.dexPath(), abi.isa(),
                                dexInfo.classLoaderContext()),
                        e);
            }
        }

        return new UsableArtifactLists(artifacts, vdexFiles, sdmFiles, runtimeArtifacts);
    }

    @NonNull
    public ProfileLists getProfiles(
            @NonNull PackageState pkgState, @NonNull AndroidPackage pkg, @NonNull Options options) {
        List<ProfilePath> refProfiles = new ArrayList<>();
        List<ProfilePath> curProfiles = new ArrayList<>();

        if (options.forPrimaryDex()) {
            for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                refProfiles.add(PrimaryDexUtils.buildRefProfilePathAsInput(pkgState, dexInfo));
                curProfiles.addAll(mInjector.isSystemOrRootOrShell()
                                ? PrimaryDexUtils.getCurProfiles(
                                          mInjector.getUserManager(), pkgState, dexInfo)
                                : PrimaryDexUtils.getCurProfiles(
                                          List.of(mInjector.getCallingUserHandle()), pkgState,
                                          dexInfo));
            }
        }
        if (options.forSecondaryDex()) {
            List<? extends SecondaryDexInfo> dexInfos = getSecondaryDexInfo(pkgState, options);
            for (SecondaryDexInfo dexInfo : dexInfos) {
                if (!mInjector.isSystemOrRootOrShell()
                        && !mInjector.getCallingUserHandle().equals(dexInfo.userHandle())) {
                    continue;
                }
                refProfiles.add(
                        AidlUtils.buildProfilePathForSecondaryRefAsInput(dexInfo.dexPath()));
                curProfiles.add(AidlUtils.buildProfilePathForSecondaryCur(dexInfo.dexPath()));
            }
        }

        return new ProfileLists(refProfiles, curProfiles);
    }

    @NonNull
    private List<? extends SecondaryDexInfo> getSecondaryDexInfo(
            @NonNull PackageState pkgState, @NonNull Options options) {
        return options.excludeForObsoleteDexesAndLoaders()
                ? mInjector.getDexUseManager().getCheckedSecondaryDexInfo(
                          pkgState.getPackageName(), true /* excludeObsoleteDexesAndLoaders */)
                : mInjector.getDexUseManager().getSecondaryDexInfo(pkgState.getPackageName());
    }

    public record WritableArtifactLists(@NonNull List<ArtifactsPath> artifacts,
            @NonNull List<SecureDexMetadataWithCompanionPaths> sdmFiles,
            @NonNull List<RuntimeArtifactsPath> runtimeArtifacts) {}

    public record UsableArtifactLists(@NonNull List<ArtifactsPath> artifacts,
            @NonNull List<VdexPath> vdexFiles,
            @NonNull List<SecureDexMetadataWithCompanionPaths> sdmFiles,
            @NonNull List<RuntimeArtifactsPath> runtimeArtifacts) {}

    public record ProfileLists(
            @NonNull List<ProfilePath> refProfiles, @NonNull List<ProfilePath> curProfiles) {
        public @NonNull List<ProfilePath> allProfiles() {
            List<ProfilePath> profiles = new ArrayList<>();
            profiles.addAll(refProfiles());
            profiles.addAll(curProfiles());
            return profiles;
        }
    }

    @Immutable
    @AutoValue
    public abstract static class Options {
        // Whether to return files for primary dex files.
        public abstract boolean forPrimaryDex();
        // Whether to return files for secondary dex files.
        public abstract boolean forSecondaryDex();
        // If true, excludes files for secondary dex files and loaders based on file visibility. See
        // details in {@link DexUseManagerLocal#getCheckedSecondaryDexInfo}.
        public abstract boolean excludeForObsoleteDexesAndLoaders();

        public static @NonNull Builder builder() {
            return new AutoValue_ArtFileManager_Options.Builder()
                    .setForPrimaryDex(false)
                    .setForSecondaryDex(false)
                    .setExcludeForObsoleteDexesAndLoaders(false);
        }

        @AutoValue.Builder
        public abstract static class Builder {
            public abstract @NonNull Builder setForPrimaryDex(boolean value);
            public abstract @NonNull Builder setForSecondaryDex(boolean value);
            public abstract @NonNull Builder setExcludeForObsoleteDexesAndLoaders(boolean value);
            public abstract @NonNull Options build();
        }
    }

    /**Injector pattern for testing purpose. */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;

            // Call the getters for the dependencies that aren't optional, to ensure correct
            // initialization order.
            GlobalInjector.getInstance().checkArtModuleServiceManager();
            getUserManager();
            getDexUseManager();
        }

        @NonNull
        public IArtd getArtd() {
            return ArtdRefCache.getInstance().getArtd();
        }

        @NonNull
        public UserManager getUserManager() {
            return Objects.requireNonNull(mContext.getSystemService(UserManager.class));
        }

        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(DexUseManagerLocal.class));
        }

        public boolean isSystemOrRootOrShell() {
            // At the time of writing, this is only typically true unless called by an app through
            // {@link ArtManagerLocal#getArtManagedFileStats}.
            return Utils.isSystemOrRootOrShell();
        }

        @NonNull
        public UserHandle getCallingUserHandle() {
            return Binder.getCallingUserHandle();
        }
    }
}
