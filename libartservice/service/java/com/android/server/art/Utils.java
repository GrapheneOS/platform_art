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

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.ServiceManager;
import android.util.SparseArray;

import com.android.server.art.ArtifactsPath;
import com.android.server.art.model.OptimizeOptions;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.art.wrapper.PackageState;

import dalvik.system.DexFile;
import dalvik.system.VMRuntime;

import java.util.Collection;
import java.util.List;

/** @hide */
public final class Utils {
    private Utils() {}

    /**
     * Checks if given array is null or has zero elements.
     */
    public static <T> boolean isEmpty(@Nullable Collection<T> array) {
        return array == null || array.isEmpty();
    }

    /**
     * Checks if given array is null or has zero elements.
     */
    public static <T> boolean isEmpty(@Nullable SparseArray<T> array) {
        return array == null || array.size() == 0;
    }

    /**
     * Checks if given array is null or has zero elements.
     */
    public static boolean isEmpty(@Nullable int[] array) {
        return array == null || array.length == 0;
    }

    @NonNull
    public static List<String> getAllIsas(@NonNull PackageState pkgState) {
        String primaryCpuAbi = pkgState.getPrimaryCpuAbi();
        String secondaryCpuAbi = pkgState.getSecondaryCpuAbi();
        if (primaryCpuAbi != null) {
            if (secondaryCpuAbi != null) {
                return List.of(VMRuntime.getInstructionSet(primaryCpuAbi),
                        VMRuntime.getInstructionSet(secondaryCpuAbi));
            }
            return List.of(VMRuntime.getInstructionSet(primaryCpuAbi));
        }
        return List.of();
    }

    @NonNull
    public static ArtifactsPath buildArtifactsPath(
            @NonNull String dexPath, @NonNull String isa, boolean isInDalvikCache) {
        ArtifactsPath artifactsPath = new ArtifactsPath();
        artifactsPath.dexPath = dexPath;
        artifactsPath.isa = isa;
        artifactsPath.isInDalvikCache = isInDalvikCache;
        return artifactsPath;
    }

    public static boolean isInDalvikCache(@NonNull PackageState pkg) {
        return pkg.isSystem() && !pkg.isUpdatedSystemApp();
    }

    /** Returns true if the given string is a valid compiler filter. */
    public static boolean isValidArtServiceCompilerFilter(@NonNull String compilerFilter) {
        if (compilerFilter.equals(OptimizeOptions.COMPILER_FILTER_NOOP)) {
            return true;
        }
        return DexFile.isValidCompilerFilter(compilerFilter);
    }

    @NonNull
    public static IArtd getArtd() {
        IArtd artd = IArtd.Stub.asInterface(ServiceManager.waitForService("artd"));
        if (artd == null) {
            throw new IllegalStateException("Unable to connect to artd");
        }
        return artd;
    }
}
