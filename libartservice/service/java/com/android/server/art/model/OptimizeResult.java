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

package com.android.server.art.model;

import android.annotation.DurationMillisLong;
import android.annotation.IntDef;
import android.annotation.NonNull;
import android.annotation.SystemApi;

import com.android.internal.annotations.Immutable;

import com.google.auto.value.AutoValue;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;

/** @hide */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@Immutable
@AutoValue
public abstract class OptimizeResult {
    // Possible values of {@link #OptimizeStatus}.
    // A larger number means a higher priority. If multiple dex container files are processed, the
    // final status will be the one with the highest priority.
    public static final int OPTIMIZE_SKIPPED = 10;
    public static final int OPTIMIZE_PERFORMED = 20;
    public static final int OPTIMIZE_FAILED = 30;
    public static final int OPTIMIZE_CANCELLED = 40;

    /** @hide */
    // clang-format off
    @IntDef(prefix = {"OPTIMIZE_"}, value = {
        OPTIMIZE_SKIPPED,
        OPTIMIZE_FAILED,
        OPTIMIZE_PERFORMED,
        OPTIMIZE_CANCELLED,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface OptimizeStatus {}

    /** @hide */
    protected OptimizeResult() {}

    /** @hide */
    public static @NonNull OptimizeResult create(@NonNull String requestedCompilerFilter,
            @NonNull String reason, @NonNull List<PackageOptimizeResult> packageOptimizeResult) {
        return new AutoValue_OptimizeResult(requestedCompilerFilter, reason, packageOptimizeResult);
    }

    /**
     * The requested compiler filter. Note that the compiler filter might be adjusted before the
     * execution based on factors like whether the profile is available or whether the app is
     * used by other apps.
     *
     * @see OptimizeParams.Builder#setCompilerFilter(String)
     * @see DexContainerFileOptimizeResult#getActualCompilerFilter()
     */
    public abstract @NonNull String getRequestedCompilerFilter();

    /** The compilation reason. */
    public abstract @NonNull String getReason();

    /**
     * The result of each individual package.
     *
     * If the request is to optimize a single package without optimizing dependencies, the only
     * element is the result of the requested package.
     *
     * If the request is to optimize a single package with {@link
     * ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES} set, the first element is the result of the
     * requested package, and the rest are the results of the dependency packages.
     *
     * If the request is to optimize multiple packages, the list contains the results of all the
     * requested packages. The results of their dependency packages are also included if {@link
     * ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES} is set.
     */
    public abstract @NonNull List<PackageOptimizeResult> getPackageOptimizeResults();

    /** The final status. */
    public @OptimizeStatus int getFinalStatus() {
        return getPackageOptimizeResults()
                .stream()
                .mapToInt(result -> result.getStatus())
                .max()
                .orElse(OPTIMIZE_SKIPPED);
    }

    /**
     * Describes the result of a package.
     *
     * @hide
     */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    @Immutable
    @AutoValue
    public static abstract class PackageOptimizeResult {
        /** @hide */
        protected PackageOptimizeResult() {}

        /** @hide */
        public static @NonNull PackageOptimizeResult create(@NonNull String packageName,
                @NonNull List<DexContainerFileOptimizeResult> dexContainerFileOptimizeResults,
                boolean isCanceled) {
            return new AutoValue_OptimizeResult_PackageOptimizeResult(
                    packageName, dexContainerFileOptimizeResults, isCanceled);
        }

        /** The package name. */
        public abstract @NonNull String getPackageName();

        /**
         * The results of optimizing dex container files. Note that there can be multiple entries
         * for the same dex container file, but for different ABIs.
         */
        public abstract @NonNull List<DexContainerFileOptimizeResult>
        getDexContainerFileOptimizeResults();

        /** @hide */
        public abstract boolean isCanceled();

        /** The overall status of the package. */
        public @OptimizeStatus int getStatus() {
            return isCanceled() ? OPTIMIZE_CANCELLED
                                : getDexContainerFileOptimizeResults()
                                          .stream()
                                          .mapToInt(result -> result.getStatus())
                                          .max()
                                          .orElse(OPTIMIZE_SKIPPED);
        }

        /** True if the package has any artifacts updated by this operation. */
        public boolean hasUpdatedArtifacts() {
            return getDexContainerFileOptimizeResults().stream().anyMatch(
                    result -> result.getStatus() == OPTIMIZE_PERFORMED);
        }
    }

    /**
     * Describes the result of optimizing a dex container file.
     *
     * @hide
     */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    @Immutable
    @AutoValue
    public static abstract class DexContainerFileOptimizeResult {
        /** @hide */
        protected DexContainerFileOptimizeResult() {}

        /** @hide */
        public static @NonNull DexContainerFileOptimizeResult create(
                @NonNull String dexContainerFile, boolean isPrimaryAbi, @NonNull String abi,
                @NonNull String compilerFilter, @OptimizeStatus int status,
                long dex2oatWallTimeMillis, long dex2oatCpuTimeMillis, long sizeBytes,
                long sizeBeforeBytes, boolean isSkippedDueToStorageLow) {
            return new AutoValue_OptimizeResult_DexContainerFileOptimizeResult(dexContainerFile,
                    isPrimaryAbi, abi, compilerFilter, status, dex2oatWallTimeMillis,
                    dex2oatCpuTimeMillis, sizeBytes, sizeBeforeBytes, isSkippedDueToStorageLow);
        }

        /** The absolute path to the dex container file. */
        public abstract @NonNull String getDexContainerFile();

        /**
         * If true, the optimization is for the primary ABI of the package (the ABI that the
         * application is launched with). Otherwise, the optimization is for an ABI that other
         * applications might be launched with when using this application's code.
         */
        public abstract boolean isPrimaryAbi();

        /**
         * Returns the ABI that the optimization is for. Possible values are documented at
         * https://developer.android.com/ndk/guides/abis#sa.
         */
        public abstract @NonNull String getAbi();

        /**
         * The actual compiler filter.
         *
         * @see OptimizeParams.Builder#setCompilerFilter(String)
         */
        public abstract @NonNull String getActualCompilerFilter();

        /** The status of optimizing this dex container file. */
        public abstract @OptimizeStatus int getStatus();

        /**
         * The wall time of the dex2oat invocation, in milliseconds, if dex2oat succeeded or was
         * cancelled. Returns 0 if dex2oat failed or was not run, or if failed to get the value.
         */
        public abstract @DurationMillisLong long getDex2oatWallTimeMillis();

        /**
         * The CPU time of the dex2oat invocation, in milliseconds, if dex2oat succeeded or was
         * cancelled. Returns 0 if dex2oat failed or was not run, or if failed to get the value.
         */
        public abstract @DurationMillisLong long getDex2oatCpuTimeMillis();

        /**
         * The total size, in bytes, of the optimized artifacts. Returns 0 if {@link #getStatus()}
         * is not {@link #OPTIMIZE_PERFORMED}.
         */
        public abstract long getSizeBytes();

        /**
         * The total size, in bytes, of the previous optimized artifacts that has been replaced.
         * Returns 0 if there were no previous optimized artifacts or {@link #getStatus()} is not
         * {@link #OPTIMIZE_PERFORMED}.
         */
        public abstract long getSizeBeforeBytes();

        /** @hide */
        public abstract boolean isSkippedDueToStorageLow();
    }
}
