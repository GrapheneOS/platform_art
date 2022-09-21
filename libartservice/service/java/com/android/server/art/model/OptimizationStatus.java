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

import android.annotation.NonNull;
import android.annotation.SystemApi;

import com.android.internal.annotations.Immutable;

import java.util.ArrayList;
import java.util.List;

/**
 * Describes the optimization status of a package.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@Immutable
public class OptimizationStatus {
    private final
            @NonNull List<DexContainerFileOptimizationStatus> mDexContainerFileOptimizationStatuses;

    /** @hide */
    public OptimizationStatus(@NonNull List<DexContainerFileOptimizationStatus>
                    dexContainerFileOptimizationStatuses) {
        mDexContainerFileOptimizationStatuses = dexContainerFileOptimizationStatuses;
    }

    /**
     * The statuses of the dex container file optimizations. Note that there can be
     * multiple entries for the same dex container file, but for different ABIs.
     */
    @NonNull
    public List<DexContainerFileOptimizationStatus> getDexContainerFileOptimizationStatuses() {
        return mDexContainerFileOptimizationStatuses;
    }

    /** Describes the optimization status of a dex container file. */
    @Immutable
    public static class DexContainerFileOptimizationStatus {
        private final @NonNull String mDexContainerFile;
        private final @NonNull boolean mIsPrimaryAbi;
        private final @NonNull String mAbi;
        private final @NonNull String mCompilerFilter;
        private final @NonNull String mCompilationReason;
        private final @NonNull String mLocationDebugString;

        /** @hide */
        public DexContainerFileOptimizationStatus(@NonNull String dexContainerFile,
                boolean isPrimaryAbi, @NonNull String abi, @NonNull String compilerFilter,
                @NonNull String compilationReason, @NonNull String locationDebugString) {
            mDexContainerFile = dexContainerFile;
            mIsPrimaryAbi = isPrimaryAbi;
            mAbi = abi;
            mCompilerFilter = compilerFilter;
            mCompilationReason = compilationReason;
            mLocationDebugString = locationDebugString;
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
         * A human-readable string that describes the compiler filter.
         *
         * Possible values are:
         * <ul>
         *   <li>A valid value of the {@code --compiler-filer} option passed to {@code dex2oat}, if
         *     the optimized artifacts are valid. See
         *     https://source.android.com/docs/core/dalvik/configure#compilation_options.
         *   <li>{@code "run-from-apk"}, if the optimized artifacts do not exist.
         *   <li>{@code "run-from-apk-fallback"}, if the optimized artifacts exist but are invalid
         *     because the dex container file has changed.
         *   <li>{@code "error"}, if an unexpected error occurs.
         * </ul>
         */
        public @NonNull String getCompilerFilter() {
            return mCompilerFilter;
        }

        /**
         * A string that describes the compilation reason.
         *
         * Possible values are:
         * <ul>
         *   <li>The compilation reason, in text format, passed to {@code dex2oat}.
         *   <li>{@code "unknown"}: if the reason is empty or the optimized artifacts do not exist.
         *   <li>{@code "error"}: if an unexpected error occurs.
         * </ul>
         */
        public @NonNull String getCompilationReason() {
            return mCompilationReason;
        }

        /**
         * A human-readable string that describes the location of the optimized artifacts.
         *
         * Note that this string is for debugging purposes only. There is no stability guarantees
         * for the format of the string. DO NOT use it programmatically.
         */
        public @NonNull String getLocationDebugString() {
            return mLocationDebugString;
        }
    }
}
