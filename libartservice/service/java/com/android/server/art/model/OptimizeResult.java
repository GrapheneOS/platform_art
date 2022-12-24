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
     * @see OptimizeParams.Builder#setCompilerFilter(String)
     * @see DexContainerFileOptimizeResult#getActualCompilerFilter()
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

    /**
     * Describes the result of a package.
     *
     * @hide
     */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    @Immutable
    public static class PackageOptimizeResult {
        private final @NonNull String mPackageName;
        private final
                @NonNull List<DexContainerFileOptimizeResult> mDexContainerFileOptimizeResults;
        private final boolean mIsCanceled;

        /** @hide */
        public PackageOptimizeResult(@NonNull String packageName,
                @NonNull List<DexContainerFileOptimizeResult> dexContainerFileOptimizeResults,
                boolean isCanceled) {
            mPackageName = packageName;
            mDexContainerFileOptimizeResults = dexContainerFileOptimizeResults;
            mIsCanceled = isCanceled;
        }

        /** The package name. */
        public @NonNull String getPackageName() {
            return mPackageName;
        }

        /**
         * The results of optimizing dex container files. Note that there can be multiple entries
         * for the same dex container file, but for different ABIs.
         */
        @NonNull
        public List<DexContainerFileOptimizeResult> getDexContainerFileOptimizeResults() {
            return mDexContainerFileOptimizeResults;
        }

        /** The overall status of the package. */
        public @OptimizeStatus int getStatus() {
            return mIsCanceled ? OPTIMIZE_CANCELLED
                               : mDexContainerFileOptimizeResults.stream()
                                         .mapToInt(result -> result.getStatus())
                                         .max()
                                         .orElse(OPTIMIZE_SKIPPED);
        }

        /** True if the package has any artifacts updated by this operation. */
        public boolean hasUpdatedArtifacts() {
            return mDexContainerFileOptimizeResults.stream().anyMatch(
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
    public static class DexContainerFileOptimizeResult {
        private final @NonNull String mDexContainerFile;
        private final boolean mIsPrimaryAbi;
        private final @NonNull String mAbi;
        private final @NonNull String mActualCompilerFilter;
        private final @OptimizeStatus int mStatus;
        private final long mDex2oatWallTimeMillis;
        private final long mDex2oatCpuTimeMillis;
        private final long mSizeBytes;
        private final long mSizeBeforeBytes;
        private final boolean mIsSkippedDueToStorageLow;

        /** @hide */
        public DexContainerFileOptimizeResult(@NonNull String dexContainerFile,
                boolean isPrimaryAbi, @NonNull String abi, @NonNull String compilerFilter,
                @OptimizeStatus int status, long dex2oatWallTimeMillis, long dex2oatCpuTimeMillis,
                long sizeBytes, long sizeBeforeBytes, boolean isSkippedDueToStorageLow) {
            mDexContainerFile = dexContainerFile;
            mIsPrimaryAbi = isPrimaryAbi;
            mAbi = abi;
            mActualCompilerFilter = compilerFilter;
            mStatus = status;
            mDex2oatWallTimeMillis = dex2oatWallTimeMillis;
            mDex2oatCpuTimeMillis = dex2oatCpuTimeMillis;
            mSizeBytes = sizeBytes;
            mSizeBeforeBytes = sizeBeforeBytes;
            mIsSkippedDueToStorageLow = isSkippedDueToStorageLow;
        }

        /** The absolute path to the dex container file. */
        public @NonNull String getDexContainerFile() {
            return mDexContainerFile;
        }

        /**
         * If true, the optimization is for the primary ABI of the package (the ABI that the
         * application is launched with). Otherwise, the optimization is for an ABI that other
         * applications might be launched with when using this application's code.
         */
        public boolean isPrimaryAbi() {
            return mIsPrimaryAbi;
        }

        /**
         * Returns the ABI that the optimization is for. Possible values are documented at
         * https://developer.android.com/ndk/guides/abis#sa.
         */
        public @NonNull String getAbi() {
            return mAbi;
        }

        /**
         * The actual compiler filter.
         *
         * @see OptimizeParams.Builder#setCompilerFilter(String)
         */
        public @NonNull String getActualCompilerFilter() {
            return mActualCompilerFilter;
        }

        /** The status of optimizing this dex container file. */
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

        /**
         * The total size, in bytes, of the optimized artifacts. Returns 0 if {@link #getStatus()}
         * is not {@link #OPTIMIZE_PERFORMED}.
         */
        public long getSizeBytes() {
            return mSizeBytes;
        }

        /**
         * The total size, in bytes, of the previous optimized artifacts that has been replaced.
         * Returns 0 if there were no previous optimized artifacts or {@link #getStatus()} is not
         * {@link #OPTIMIZE_PERFORMED}.
         */
        public long getSizeBeforeBytes() {
            return mSizeBeforeBytes;
        }

        /** @hide */
        public boolean isSkippedDueToStorageLow() {
            return mIsSkippedDueToStorageLow;
        }
    }
}
