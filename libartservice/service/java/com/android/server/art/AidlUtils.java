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
}
