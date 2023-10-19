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

import static com.android.server.art.DexUseManagerLocal.DetailedSecondaryDexInfo;
import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.OutputArtifacts.PermissionSettings.SeContext;
import static com.android.server.art.Utils.Abi;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.Build;
import android.os.CancellationSignal;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.DexoptParams;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import java.util.List;

/** @hide */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class SecondaryDexopter extends Dexopter<DetailedSecondaryDexInfo> {
    private static final String TAG = ArtManagerLocal.TAG;

    public SecondaryDexopter(@NonNull Context context, @NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull DexoptParams params,
            @NonNull CancellationSignal cancellationSignal) {
        this(new Injector(context), pkgState, pkg, params, cancellationSignal);
    }

    @VisibleForTesting
    public SecondaryDexopter(@NonNull Injector injector, @NonNull PackageState pkgState,
            @NonNull AndroidPackage pkg, @NonNull DexoptParams params,
            @NonNull CancellationSignal cancellationSignal) {
        super(injector, pkgState, pkg, params, cancellationSignal);
    }

    @Override
    protected boolean isInDalvikCache() {
        // A secondary dex file is added by the app, so it's always in a writable location and hence
        // never uses dalvik-cache.
        return false;
    }

    @Override
    @NonNull
    protected List<DetailedSecondaryDexInfo> getDexInfoList() {
        return mInjector.getDexUseManager().getFilteredDetailedSecondaryDexInfo(
                mPkgState.getPackageName());
    }

    @Override
    protected boolean isDexoptable(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return true;
    }

    @Override
    protected boolean needsToBeShared(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return dexInfo.isUsedByOtherApps();
    }

    @Override
    protected boolean isDexFilePublic(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return dexInfo.isDexFilePublic();
    }

    @Override
    @NonNull
    protected List<ProfilePath> getExternalProfiles(@NonNull DetailedSecondaryDexInfo dexInfo) {
        // A secondary dex file doesn't have any external profile to use.
        return List.of();
    }

    @Override
    @NonNull
    protected PermissionSettings getPermissionSettings(
            @NonNull DetailedSecondaryDexInfo dexInfo, boolean canBePublic) {
        int uid = getUid(dexInfo);
        // We need the "execute" bit for "others" even though `canBePublic` is false because the
        // directory can contain other artifacts that needs to be public.
        // We don't need the "read" bit for "others" on the directories because others only need to
        // access the files in the directories, but they don't need to "ls" the directories.
        FsPermission dirFsPermission = AidlUtils.buildFsPermission(uid /* uid */, uid /* gid */,
                false /* isOtherReadable */, true /* isOtherExecutable */);
        FsPermission fileFsPermission =
                AidlUtils.buildFsPermission(uid /* uid */, uid /* gid */, canBePublic);
        SeContext seContext = AidlUtils.buildSeContext(mPkgState.getSeInfo(), uid);
        return AidlUtils.buildPermissionSettings(dirFsPermission, fileFsPermission, seContext);
    }

    @Override
    @NonNull
    protected List<Abi> getAllAbis(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return Utils.getAllAbisForNames(dexInfo.abiNames(), mPkgState);
    }

    @Override
    @NonNull
    protected ProfilePath buildRefProfilePath(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return AidlUtils.buildProfilePathForSecondaryRef(dexInfo.dexPath());
    }

    @Override
    @NonNull
    protected OutputProfile buildOutputProfile(
            @NonNull DetailedSecondaryDexInfo dexInfo, boolean isPublic) {
        int uid = getUid(dexInfo);
        return AidlUtils.buildOutputProfileForSecondary(dexInfo.dexPath(), uid, uid, isPublic);
    }

    @Override
    @NonNull
    protected List<ProfilePath> getCurProfiles(@NonNull DetailedSecondaryDexInfo dexInfo) {
        // A secondary dex file can only be loaded by one user, so there is only one profile.
        return List.of(AidlUtils.buildProfilePathForSecondaryCur(dexInfo.dexPath()));
    }

    @Override
    @Nullable
    protected DexMetadataPath buildDmPath(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return null;
    }

    private int getUid(@NonNull DetailedSecondaryDexInfo dexInfo) {
        return dexInfo.userHandle().getUid(mPkgState.getAppId());
    }
}
