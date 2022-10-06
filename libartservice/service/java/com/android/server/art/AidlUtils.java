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
import static com.android.server.art.ProfilePath.CurProfilePath;
import static com.android.server.art.ProfilePath.PrebuiltProfilePath;
import static com.android.server.art.ProfilePath.RefProfilePath;
import static com.android.server.art.ProfilePath.TmpRefProfilePath;

import android.annotation.NonNull;
import android.annotation.Nullable;

/** @hide */
public final class AidlUtils {
    private AidlUtils() {}

    @NonNull
    public static ArtifactsPath buildArtifactsPath(
            @NonNull String dexPath, @NonNull String isa, boolean isInDalvikCache) {
        var artifactsPath = new ArtifactsPath();
        artifactsPath.dexPath = dexPath;
        artifactsPath.isa = isa;
        artifactsPath.isInDalvikCache = isInDalvikCache;
        return artifactsPath;
    }

    @NonNull
    public static FsPermission buildFsPermission(
            int uid, int gid, boolean isOtherReadable, boolean isOtherExecutable) {
        var fsPermission = new FsPermission();
        fsPermission.uid = uid;
        fsPermission.gid = gid;
        fsPermission.isOtherReadable = isOtherReadable;
        fsPermission.isOtherExecutable = isOtherExecutable;
        return fsPermission;
    }

    @NonNull
    public static FsPermission buildFsPermission(int uid, int gid, boolean isOtherReadable) {
        return buildFsPermission(uid, gid, isOtherReadable, false /* isOtherExecutable */);
    }

    @NonNull
    public static DexMetadataPath buildDexMetadataPath(@NonNull String dexPath) {
        var dexMetadataPath = new DexMetadataPath();
        dexMetadataPath.dexPath = dexPath;
        return dexMetadataPath;
    }

    @NonNull
    public static PermissionSettings buildPermissionSettings(@NonNull FsPermission dirFsPermission,
            @NonNull FsPermission fileFsPermission, @Nullable SeContext seContext) {
        var permissionSettings = new PermissionSettings();
        permissionSettings.dirFsPermission = dirFsPermission;
        permissionSettings.fileFsPermission = fileFsPermission;
        permissionSettings.seContext = seContext;
        return permissionSettings;
    }

    @NonNull
    public static OutputArtifacts buildOutputArtifacts(@NonNull String dexPath, @NonNull String isa,
            boolean isInDalvikCache, @NonNull PermissionSettings permissionSettings) {
        var outputArtifacts = new OutputArtifacts();
        outputArtifacts.artifactsPath = buildArtifactsPath(dexPath, isa, isInDalvikCache);
        outputArtifacts.permissionSettings = permissionSettings;
        return outputArtifacts;
    }

    @NonNull
    public static RefProfilePath buildRefProfilePath(
            @NonNull String packageName, @NonNull String profileName) {
        var refProfilePath = new RefProfilePath();
        refProfilePath.packageName = packageName;
        refProfilePath.profileName = profileName;
        return refProfilePath;
    }

    @NonNull
    public static ProfilePath buildProfilePathForRef(
            @NonNull String packageName, @NonNull String profileName) {
        return ProfilePath.refProfilePath(buildRefProfilePath(packageName, profileName));
    }

    @NonNull
    public static ProfilePath buildProfilePathForPrebuilt(@NonNull String dexPath) {
        var prebuiltProfilePath = new PrebuiltProfilePath();
        prebuiltProfilePath.dexPath = dexPath;
        return ProfilePath.prebuiltProfilePath(prebuiltProfilePath);
    }

    @NonNull
    public static ProfilePath buildProfilePathForDm(@NonNull String dexPath) {
        return ProfilePath.dexMetadataPath(buildDexMetadataPath(dexPath));
    }

    @NonNull
    public static ProfilePath buildProfilePathForCur(
            int userId, @NonNull String packageName, @NonNull String profileName) {
        var curProfilePath = new CurProfilePath();
        curProfilePath.userId = userId;
        curProfilePath.packageName = packageName;
        curProfilePath.profileName = profileName;
        return ProfilePath.curProfilePath(curProfilePath);
    }

    @NonNull
    public static OutputProfile buildOutputProfile(@NonNull String packageName,
            @NonNull String profileName, int uid, int gid, boolean isPublic) {
        var outputProfile = new OutputProfile();
        outputProfile.profilePath = new TmpRefProfilePath();
        outputProfile.profilePath.refProfilePath = buildRefProfilePath(packageName, profileName);
        outputProfile.profilePath.id = ""; // Will be filled by artd.
        outputProfile.fsPermission = buildFsPermission(uid, gid, isPublic);
        return outputProfile;
    }
}
