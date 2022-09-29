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
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.art.wrapper.PackageState;

import com.google.auto.value.AutoValue;

import dalvik.system.DexFile;
import dalvik.system.VMRuntime;

import java.util.ArrayList;
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
    public static List<Abi> getAllAbis(@NonNull PackageState pkgState) {
        List<Abi> abis = new ArrayList<>();
        String primaryCpuAbi = pkgState.getPrimaryCpuAbi();
        if (primaryCpuAbi != null) {
            abis.add(Abi.create(primaryCpuAbi, VMRuntime.getInstructionSet(primaryCpuAbi),
                    true /* isPrimaryAbi */));
        }
        String secondaryCpuAbi = pkgState.getSecondaryCpuAbi();
        if (secondaryCpuAbi != null) {
            abis.add(Abi.create(secondaryCpuAbi, VMRuntime.getInstructionSet(secondaryCpuAbi),
                    false /* isPrimaryAbi */));
        }
        // Primary and secondary ABIs are guaranteed to have different ISAs.
        if (abis.size() == 2 && abis.get(0).isa().equals(abis.get(1).isa())) {
            throw new IllegalStateException(
                    String.format("Duplicate ISA: primary ABI '%s', secondary ABI '%s'",
                            primaryCpuAbi, secondaryCpuAbi));
        }
        return abis;
    }

    public static boolean isInDalvikCache(@NonNull PackageState pkg) {
        return pkg.isSystem() && !pkg.isUpdatedSystemApp();
    }

    /** Returns true if the given string is a valid compiler filter. */
    public static boolean isValidArtServiceCompilerFilter(@NonNull String compilerFilter) {
        if (compilerFilter.equals(OptimizeParams.COMPILER_FILTER_NOOP)) {
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

    public static boolean implies(boolean cond1, boolean cond2) {
        return cond1 ? cond2 : true;
    }

    @AutoValue
    public abstract static class Abi {
        static @NonNull Abi create(
                @NonNull String name, @NonNull String isa, boolean isPrimaryAbi) {
            return new AutoValue_Utils_Abi(name, isa, isPrimaryAbi);
        }

        // The ABI name. E.g., "arm64-v8a".
        abstract @NonNull String name();

        // The instruction set name. E.g., "arm64".
        abstract @NonNull String isa();

        abstract boolean isPrimaryAbi();
    }
}
