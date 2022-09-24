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

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;

/** @hide */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@Immutable
public class OptimizeResult {
    // Possible values of {@link #OptimizeStatus}.
    // A larger number means a higher priority. If multiple dex files are processed, the final
    // status will be the one with the highest priority.
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

    private final @NonNull String mRequestedCompilerFilter;
    private final @NonNull String mReason;
    private final @NonNull List<PackageOptimizeResult> mPackageOptimizeResult;

    /** @hide */
    public OptimizeResult(@NonNull String requestedCompilerFilter, @NonNull String reason,
            @NonNull List<PackageOptimizeResult> packageOptimizeResult) {
        mRequestedCompilerFilter = requestedCompilerFilter;
        mReason = reason;
        mPackageOptimizeResult = packageOptimizeResult;
    }

    /**
     * The requested compiler filter. Note that the compiler filter might be adjusted before the
     * execution based on factors like whether the profile is available or whether the app is
     * used by other apps.
     *
     * @see DexFileOptimizeResult#getActualCompilerFilter.
     */
    public @NonNull String getRequestedCompilerFilter() {
        return mRequestedCompilerFilter;
    }

    /** The compilation reason. */
    public @NonNull String getReason() {
        return mReason;
    }

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
    public @NonNull List<PackageOptimizeResult> getPackageOptimizeResults() {
        return mPackageOptimizeResult;
    }

    /** The final status. */
    public @OptimizeStatus int getFinalStatus() {
        return mPackageOptimizeResult.stream()
                .mapToInt(result -> result.getStatus())
                .max()
                .orElse(OPTIMIZE_SKIPPED);
    }

    /** Describes the result of a package. */
    @Immutable
    public static class PackageOptimizeResult {
        private final @NonNull String mPackageName;
        private final @NonNull List<DexFileOptimizeResult> mDexFileOptimizeResults;

        /** @hide */
        public PackageOptimizeResult(@NonNull String packageName,
                @NonNull List<DexFileOptimizeResult> dexFileOptimizeResults) {
            mPackageName = packageName;
            mDexFileOptimizeResults = dexFileOptimizeResults;
        }

        /** The package name. */
        public @NonNull String getPackageName() {
            return mPackageName;
        }

        /** The result of each individual dex file. */
        @NonNull
        public List<DexFileOptimizeResult> getDexFileOptimizeResults() {
            return mDexFileOptimizeResults;
        }

        /** The overall status of the package. */
        public @OptimizeStatus int getStatus() {
            return mDexFileOptimizeResults.stream()
                    .mapToInt(result -> result.getStatus())
                    .max()
                    .orElse(OPTIMIZE_SKIPPED);
        }
    }

    /** Describes the result of a dex file. */
    @Immutable
    public static class DexFileOptimizeResult {
        private final @NonNull String mDexFile;
        private final @NonNull String mInstructionSet;
        private final @NonNull String mActualCompilerFilter;
        private final @OptimizeStatus int mStatus;
        private final long mDex2oatWallTimeMillis;
        private final long mDex2oatCpuTimeMillis;

        /** @hide */
        public DexFileOptimizeResult(@NonNull String dexFile, @NonNull String instructionSet,
                @NonNull String compilerFilter, @OptimizeStatus int status,
                long dex2oatWallTimeMillis, long dex2oatCpuTimeMillis) {
            mDexFile = dexFile;
            mInstructionSet = instructionSet;
            mActualCompilerFilter = compilerFilter;
            mStatus = status;
            mDex2oatWallTimeMillis = dex2oatWallTimeMillis;
            mDex2oatCpuTimeMillis = dex2oatCpuTimeMillis;
        }

        /** The absolute path to the dex file. */
        public @NonNull String getDexFile() {
            return mDexFile;
        }

        /** The instruction set. */
        public @NonNull String getInstructionSet() {
            return mInstructionSet;
        }

        /** The actual compiler filter. */
        public @NonNull String getActualCompilerFilter() {
            return mActualCompilerFilter;
        }

        /** The status of optimizing this dex file. */
        public @OptimizeStatus int getStatus() {
            return mStatus;
        }

        /**
         * The wall time of the dex2oat invocation, in milliseconds, if dex2oat succeeded or was
         * cancelled. Returns 0 if dex2oat failed or was not run, or if failed to get the value.
         */
        public @DurationMillisLong long getDex2oatWallTimeMillis() {
            return mDex2oatWallTimeMillis;
        }

        /**
         * The CPU time of the dex2oat invocation, in milliseconds, if dex2oat succeeded or was
         * cancelled. Returns 0 if dex2oat failed or was not run, or if failed to get the value.
         */
        public @DurationMillisLong long getDex2oatCpuTimeMillis() {
            return mDex2oatCpuTimeMillis;
        }
    }
}
