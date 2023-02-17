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
import android.annotation.SuppressLint;
import android.apphibernation.AppHibernationManager;
import android.os.SystemProperties;
import android.os.Trace;
import android.os.UserManager;
import android.text.TextUtils;
import android.util.Log;
import android.util.SparseArray;

import com.android.modules.utils.pm.PackageStateModulesUtils;
import com.android.server.art.model.DexoptParams;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.DexFile;
import dalvik.system.VMRuntime;

import com.google.auto.value.AutoValue;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.Future;
import java.util.stream.Collectors;

/** @hide */
public final class Utils {
    public static final String TAG = "ArtServiceUtils";
    public static final String PLATFORM_PACKAGE_NAME = "android";

    /** A copy of {@link android.os.Trace.TRACE_TAG_DALVIK}. */
    private static final long TRACE_TAG_DALVIK = 1L << 14;

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

    /** Returns the ABI information for the package. */
    @NonNull
    public static List<Abi> getAllAbis(@NonNull PackageState pkgState) {
        List<Abi> abis = new ArrayList<>();
        abis.add(getPrimaryAbi(pkgState));
        String pkgPrimaryCpuAbi = pkgState.getPrimaryCpuAbi();
        String pkgSecondaryCpuAbi = pkgState.getSecondaryCpuAbi();
        if (pkgSecondaryCpuAbi != null) {
            Utils.check(pkgState.getPrimaryCpuAbi() != null);
            String isa = getTranslatedIsa(VMRuntime.getInstructionSet(pkgSecondaryCpuAbi));
            abis.add(Abi.create(nativeIsaToAbi(isa), isa, false /* isPrimaryAbi */));
        }
        // Primary and secondary ABIs should be guaranteed to have different ISAs.
        if (abis.size() == 2 && abis.get(0).isa().equals(abis.get(1).isa())) {
            throw new IllegalStateException(String.format(
                    "Duplicate ISA: primary ABI '%s' ('%s'), secondary ABI '%s' ('%s')",
                    pkgPrimaryCpuAbi, abis.get(0).name(), pkgSecondaryCpuAbi, abis.get(1).name()));
        }
        return abis;
    }

    /** Returns the ABI information for the ABIs with the given names. */
    @NonNull
    public static List<Abi> getAllAbisForNames(
            @NonNull Set<String> abiNames, @NonNull PackageState pkgState) {
        Abi pkgPrimaryAbi = getPrimaryAbi(pkgState);
        return abiNames.stream()
                .map(name
                        -> Abi.create(name, VMRuntime.getInstructionSet(name),
                                name.equals(pkgPrimaryAbi.name())))
                .collect(Collectors.toList());
    }

    @NonNull
    public static Abi getPrimaryAbi(@NonNull PackageState pkgState) {
        String primaryCpuAbi = pkgState.getPrimaryCpuAbi();
        if (primaryCpuAbi != null) {
            String isa = getTranslatedIsa(VMRuntime.getInstructionSet(primaryCpuAbi));
            return Abi.create(nativeIsaToAbi(isa), isa, true /* isPrimaryAbi */);
        }
        // This is the most common case. The package manager can't infer the ABIs, probably because
        // the package doesn't contain any native library. The app is launched with the device's
        // preferred ABI.
        String preferredAbi = Constants.getPreferredAbi();
        return Abi.create(
                preferredAbi, VMRuntime.getInstructionSet(preferredAbi), true /* isPrimaryAbi */);
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
        if (compilerFilter.equals(DexoptParams.COMPILER_FILTER_NOOP)) {
            return true;
        }
        return DexFile.isValidCompilerFilter(compilerFilter);
    }

    @NonNull
    public static IArtd getArtd() {
        IArtd artd = IArtd.Stub.asInterface(ArtModuleServiceInitializer.getArtModuleServiceManager()
                                                    .getArtdServiceRegisterer()
                                                    .waitForService());
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

    @NonNull
    public static PackageState getPackageStateOrThrow(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        PackageState pkgState = snapshot.getPackageState(packageName);
        if (pkgState == null) {
            throw new IllegalArgumentException("Package not found: " + packageName);
        }
        return pkgState;
    }

    @NonNull
    public static AndroidPackage getPackageOrThrow(@NonNull PackageState pkgState) {
        AndroidPackage pkg = pkgState.getAndroidPackage();
        if (pkg == null) {
            throw new IllegalArgumentException(
                    "Unable to get package " + pkgState.getPackageName());
        }
        return pkg;
    }

    @NonNull
    public static String assertNonEmpty(@Nullable String str) {
        if (TextUtils.isEmpty(str)) {
            throw new IllegalArgumentException();
        }
        return str;
    }

    public static void executeAndWait(@NonNull Executor executor, @NonNull Runnable runnable) {
        getFuture(CompletableFuture.runAsync(runnable, executor));
    }

    public static <T> T getFuture(Future<T> future) {
        try {
            return future.get();
        } catch (ExecutionException e) {
            Throwable cause = e.getCause();
            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }
            throw new RuntimeException(cause);
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Returns true if the given package is dexoptable.
     *
     * @param appHibernationManager the {@link AppHibernationManager} instance for checking
     *         hibernation status, or null to skip the check
     */
    @SuppressLint("NewApi")
    public static boolean canDexoptPackage(
            @NonNull PackageState pkgState, @Nullable AppHibernationManager appHibernationManager) {
        if (!PackageStateModulesUtils.isDexoptable(pkgState)) {
            return false;
        }

        // We do not dexopt unused packages.
        // If `appHibernationManager` is null, the caller's intention is to skip the check.
        if (appHibernationManager != null
                && shouldSkipDexoptDueToHibernation(pkgState, appHibernationManager)) {
            return false;
        }

        return true;
    }

    public static boolean shouldSkipDexoptDueToHibernation(
            @NonNull PackageState pkgState, @NonNull AppHibernationManager appHibernationManager) {
        return appHibernationManager.isHibernatingGlobally(pkgState.getPackageName())
                && appHibernationManager.isOatArtifactDeletionEnabled();
    }

    public static long getPackageLastActiveTime(@NonNull PackageState pkgState,
            @NonNull DexUseManagerLocal dexUseManager, @NonNull UserManager userManager) {
        long lastUsedAtMs = dexUseManager.getPackageLastUsedAtMs(pkgState.getPackageName());
        // The time where the last user installed the package the first time.
        long lastFirstInstallTimeMs =
                userManager.getUserHandles(true /* excludeDying */)
                        .stream()
                        .map(handle -> pkgState.getStateForUser(handle))
                        .map(userState -> userState.getFirstInstallTimeMillis())
                        .max(Long::compare)
                        .orElse(0l);
        return Math.max(lastUsedAtMs, lastFirstInstallTimeMs);
    }

    public static void deleteIfExistsSafe(@Nullable File file) {
        if (file != null) {
            deleteIfExistsSafe(file.toPath());
        }
    }

    public static void deleteIfExistsSafe(@NonNull Path path) {
        try {
            Files.deleteIfExists(path);
        } catch (IOException e) {
            Log.e(TAG, "Failed to delete file '" + path + "'", e);
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

    public static class Tracing implements AutoCloseable {
        public Tracing(@NonNull String methodName) {
            Trace.traceBegin(TRACE_TAG_DALVIK, methodName);
        }

        @Override
        public void close() {
            Trace.traceEnd(TRACE_TAG_DALVIK);
        }
    }
}
