/*
 * Copyright (C) 2024 The Android Open Source Project
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

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.art.flags.Flags;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Helper class for <i>ART-managed install files</i> (files installed by Package Manager
 * and managed by ART).
 *
 * @hide
 */
@FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public final class ArtManagedInstallFileHelper {
    private static final List<String> FILE_TYPES = List.of(ArtConstants.DEX_METADATA_FILE_EXT,
            ArtConstants.PROFILE_FILE_EXT, ArtConstants.SECURE_DEX_METADATA_FILE_EXT);
    private static final List<String> SDM_SUFFIXES =
            Utils.getNativeIsas()
                    .stream()
                    .map(isa -> "." + isa + ArtConstants.SECURE_DEX_METADATA_FILE_EXT)
                    .toList();

    private ArtManagedInstallFileHelper() {}

    /**
     * Returns whether the file at the given path is an <i>ART-managed install file</i>. This
     * is a pure string operation on the input and does not involve any I/O.
     */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static boolean isArtManaged(@NonNull String path) {
        return FILE_TYPES.stream().anyMatch(ext -> path.endsWith(ext));
    }

    /**
     * Returns the subset of the given paths that are paths to the <i>ART-managed install files</i>
     * corresponding to the given APK path. This is a pure string operation on the inputs and does
     * not involve any I/O.
     *
     * Note that the files in different directories than the APK are not considered corresponding to
     * the APK.
     */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static @NonNull List<String> filterPathsForApk(
            @NonNull List<String> paths, @NonNull String apkPath) {
        Set<String> candidates =
                FILE_TYPES.stream()
                        .flatMap(ext
                                -> ext.equals(ArtConstants.SECURE_DEX_METADATA_FILE_EXT)
                                        ? SDM_SUFFIXES.stream().map(suffix
                                                  -> Utils.replaceFileExtension(apkPath, suffix))
                                        : Stream.of(Utils.replaceFileExtension(apkPath, ext)))
                        .collect(Collectors.toSet());
        return paths.stream().filter(path -> candidates.contains(path)).toList();
    }

    /**
     * Rewrites the path to the <i>ART-managed install file</i> so that it corresponds to the given
     * APK path. This is a pure string operation on the inputs and does not involve any I/O.
     *
     * Note that the result path is always in the same directory as the APK, in order to correspond
     * to the APK.
     *
     * @throws IllegalArgumentException if {@code originalPath} does not represent an <i>ART-managed
     *         install file</i>
     */
    @FlaggedApi(Flags.FLAG_ART_SERVICE_V3)
    public static @NonNull String getTargetPathForApk(
            @NonNull String originalPath, @NonNull String apkPath) {
        for (String ext : FILE_TYPES) {
            if (!ext.equals(ArtConstants.SECURE_DEX_METADATA_FILE_EXT)
                    && originalPath.endsWith(ext)) {
                return Utils.replaceFileExtension(apkPath, ext);
            }
        }
        if (originalPath.endsWith(ArtConstants.SECURE_DEX_METADATA_FILE_EXT)) {
            for (String suffix : SDM_SUFFIXES) {
                if (originalPath.endsWith(suffix)) {
                    return Utils.replaceFileExtension(apkPath, suffix);
                }
            }
            AsLog.w("SDM filename '" + originalPath
                    + "' does not contain a valid instruction set name");
            Path dirname = Paths.get(apkPath).getParent();
            Path basename = Paths.get(originalPath).getFileName();
            return (dirname != null ? dirname.resolve(basename) : basename).toString();
        }
        throw new IllegalArgumentException(
                "Illegal ART managed install file path '" + originalPath + "'");
    }
}
