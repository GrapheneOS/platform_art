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
import android.os.SystemProperties;
import android.text.TextUtils;
import android.util.SparseArray;

import com.android.server.art.model.OptimizeParams;
import com.android.server.pm.pkg.PackageState;

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
            String isa = getTranslatedIsa(VMRuntime.getInstructionSet(primaryCpuAbi));
            abis.add(Abi.create(nativeIsaToAbi(isa), isa, true /* isPrimaryAbi */));
        }
        String secondaryCpuAbi = pkgState.getSecondaryCpuAbi();
        if (secondaryCpuAbi != null) {
            Utils.check(primaryCpuAbi != null);
            String isa = getTranslatedIsa(VMRuntime.getInstructionSet(secondaryCpuAbi));
            abis.add(Abi.create(nativeIsaToAbi(isa), isa, false /* isPrimaryAbi */));
        }
        if (abis.isEmpty()) {
            // This is the most common case. The package manager can't infer the ABIs, probably
            // because the package doesn't contain any native library. The app is launched with
            // the device's preferred ABI.
            String preferredAbi = Constants.getPreferredAbi();
            abis.add(Abi.create(preferredAbi, VMRuntime.getInstructionSet(preferredAbi),
                    true /* isPrimaryAbi */));
        }
        // Primary and secondary ABIs should be guaranteed to have different ISAs.
        if (abis.size() == 2 && abis.get(0).isa().equals(abis.get(1).isa())) {
            throw new IllegalStateException(String.format(
                    "Duplicate ISA: primary ABI '%s' ('%s'), secondary ABI '%s' ('%s')",
                    primaryCpuAbi, abis.get(0).name(), secondaryCpuAbi, abis.get(1).name()));
        }
        return abis;
    }

    /**
     * If the given ISA isn't native to the device, returns the ISA that the native bridge
     * translates it to. Otherwise, returns the ISA as is. This is the ISA that the app is actually
     * launched with and therefore the ISA that should be used to compile the app.
     */
    @NonNull
    private static String getTranslatedIsa(@NonNull String isa) {
        String abi64 = Constants.getNative64BitAbi();
        String abi32 = Constants.getNative32BitAbi();
        if ((abi64 != null && isa.equals(VMRuntime.getInstructionSet(abi64)))
                || (abi32 != null && isa.equals(VMRuntime.getInstructionSet(abi32)))) {
            return isa;
        }
        String translatedIsa = SystemProperties.get("ro.dalvik.vm.isa." + isa);
        if (TextUtils.isEmpty(translatedIsa)) {
            throw new IllegalStateException(String.format("Unsupported isa '%s'", isa));
        }
        return translatedIsa;
    }

    @NonNull
    private static String nativeIsaToAbi(@NonNull String isa) {
        String abi64 = Constants.getNative64BitAbi();
        if (abi64 != null && isa.equals(VMRuntime.getInstructionSet(abi64))) {
            return abi64;
        }
        String abi32 = Constants.getNative32BitAbi();
        if (abi32 != null && isa.equals(VMRuntime.getInstructionSet(abi32))) {
            return abi32;
        }
        throw new IllegalStateException(String.format("Non-native isa '%s'", isa));
    }

    @NonNull
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

    public static void check(boolean cond) {
        // This cannot be replaced with `assert` because `assert` is not enabled in Android.
        if (!cond) {
            throw new IllegalStateException("Check failed");
        }
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
