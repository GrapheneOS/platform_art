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

import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.OutputArtifacts.PermissionSettings.SeContext;
import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.Utils.Abi;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.CancellationSignal;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.os.UserHandle;
import android.text.TextUtils;
import android.util.Log;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.PackageUserState;

import dalvik.system.DexFile;

import com.google.auto.value.AutoValue;

import java.util.ArrayList;
import java.util.List;

/** @hide */
public class PrimaryDexOptimizer extends DexOptimizer<DetailedPrimaryDexInfo> {
    private static final String TAG = "PrimaryDexOptimizer";

    private final int mSharedGid;

    public PrimaryDexOptimizer(@NonNull Context context, @NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal) {
        this(new Injector(context), pkgState, pkg, params, cancellationSignal);
    }

    @VisibleForTesting
    public PrimaryDexOptimizer(@NonNull Injector injector, @NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal) {
        super(injector, pkgState, pkg, params, cancellationSignal);

        mSharedGid = UserHandle.getSharedAppGid(pkgState.getAppId());
        if (mSharedGid < 0) {
            throw new IllegalStateException(
                    String.format("Unable to get shared gid for package '%s' (app ID: %d)",
                            pkgState.getPackageName(), pkgState.getAppId()));
        }
    }

    @Override
    protected boolean isInDalvikCache() {
        return Utils.isInDalvikCache(mPkgState);
    }

    @Override
    @NonNull
    protected List<DetailedPrimaryDexInfo> getDexInfoList() {
        return PrimaryDexUtils.getDetailedDexInfo(mPkgState, mPkg);
    }

    @Override
    protected boolean isOptimizable(@NonNull DetailedPrimaryDexInfo dexInfo) {
        // TODO(jiakaiz): Support optimizing a single split.
        return dexInfo.hasCode();
    }

    @Override
    protected boolean needsToBeShared(@NonNull DetailedPrimaryDexInfo dexInfo) {
        return isSharedLibrary()
                || mInjector.getDexUseManager().isPrimaryDexUsedByOtherApps(
                        mPkgState.getPackageName(), dexInfo.dexPath());
    }

    @Override
    @Nullable
    protected ProfilePath initReferenceProfile(@NonNull DetailedPrimaryDexInfo dexInfo)
            throws RemoteException {
        OutputProfile output = buildOutputProfile(dexInfo, true /* isPublic */);

        ProfilePath prebuiltProfile = AidlUtils.buildProfilePathForPrebuilt(dexInfo.dexPath());
        try {
            // If the APK is really a prebuilt one, rewriting the profile is unnecessary because the
            // dex location is known at build time and is correctly set in the profile header.
            // However, the APK can also be an installed one, in which case partners may place a
            // profile file next to the APK at install time. Rewriting the profile in the latter
            // case is necessary.
            if (mInjector.getArtd().copyAndRewriteProfile(
                        prebuiltProfile, output, dexInfo.dexPath())) {
                return ProfilePath.tmpProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    "Failed to use prebuilt profile "
                            + AidlUtils.toString(output.profilePath.finalPath),
                    e);
        }

        ProfilePath dmProfile = AidlUtils.buildProfilePathForDm(dexInfo.dexPath());
        try {
            if (mInjector.getArtd().copyAndRewriteProfile(dmProfile, output, dexInfo.dexPath())) {
                return ProfilePath.tmpProfilePath(output.profilePath);
            }
        } catch (ServiceSpecificException e) {
            Log.e(TAG,
                    "Failed to use profile in dex metadata file "
                            + AidlUtils.toString(output.profilePath.finalPath),
                    e);
        }

        return null;
    }

    @Override
    @NonNull
    protected PermissionSettings getPermissionSettings(
            @NonNull DetailedPrimaryDexInfo dexInfo, boolean canBePublic) {
        // The files and directories should belong to the system so that Package Manager can manage
        // them (e.g., move them around).
        // We don't need the "read" bit for "others" on the directories because others only need to
        // access the files in the directories, but they don't need to "ls" the directories.
        FsPermission dirFsPermission = AidlUtils.buildFsPermission(Process.SYSTEM_UID,
                Process.SYSTEM_UID, false /* isOtherReadable */, true /* isOtherExecutable */);
        FsPermission fileFsPermission =
                AidlUtils.buildFsPermission(Process.SYSTEM_UID, mSharedGid, canBePublic);
        // For primary dex, we can use the default SELinux context.
        SeContext seContext = null;
        return AidlUtils.buildPermissionSettings(dirFsPermission, fileFsPermission, seContext);
    }

    @Override
    @NonNull
    protected List<Abi> getAllAbis(@NonNull DetailedPrimaryDexInfo dexInfo) {
        return Utils.getAllAbis(mPkgState);
    }

    @Override
    @NonNull
    protected ProfilePath buildRefProfilePath(@NonNull DetailedPrimaryDexInfo dexInfo) {
        String profileName = getProfileName(dexInfo.splitName());
        return AidlUtils.buildProfilePathForPrimaryRef(mPkgState.getPackageName(), profileName);
    }

    @Override
    protected boolean isAppImageAllowed() {
        // Disable app images if the app requests for the splits to be loaded in isolation because
        // app images are unsupported for multiple class loaders (b/72696798).
        return !PrimaryDexUtils.isIsolatedSplitLoading(mPkg);
    }

    @Override
    @NonNull
    protected OutputProfile buildOutputProfile(
            @NonNull DetailedPrimaryDexInfo dexInfo, boolean isPublic) {
        String profileName = getProfileName(dexInfo.splitName());
        return AidlUtils.buildOutputProfileForPrimary(mPkgState.getPackageName(), profileName,
                mPkgState.getAppId(), mSharedGid, isPublic);
    }

    @Override
    @NonNull
    protected List<ProfilePath> getCurProfiles(@NonNull DetailedPrimaryDexInfo dexInfo) {
        List<ProfilePath> profiles = new ArrayList<>();
        for (UserHandle handle :
                mInjector.getUserManager().getUserHandles(true /* excludeDying */)) {
            int userId = handle.getIdentifier();
            PackageUserState userState = mPkgState.getStateForUser(handle);
            if (userState.isInstalled()) {
                profiles.add(AidlUtils.buildProfilePathForPrimaryCur(
                        userId, mPkgState.getPackageName(), getProfileName(dexInfo.splitName())));
            }
        }
        return profiles;
    }

    private boolean isSharedLibrary() {
        // TODO(b/242688548): Package manager should provide a better API for this.
        return !TextUtils.isEmpty(mPkg.getSdkLibraryName())
                || !TextUtils.isEmpty(mPkg.getStaticSharedLibraryName())
                || !mPkg.getLibraryNames().isEmpty();
    }

    @NonNull
    private String getProfileName(@Nullable String splitName) {
        return splitName == null ? "primary" : splitName + ".split";
    }
}
