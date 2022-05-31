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
    private final @NonNull List<DexFileOptimizationStatus> mDexFileOptimizationStatuses;

    /** @hide */
    public OptimizationStatus(
            @NonNull List<DexFileOptimizationStatus> dexFileOptimizationStatuses) {
        mDexFileOptimizationStatuses = dexFileOptimizationStatuses;
    }

    /** The optimization status of each individual dex file. */
    @NonNull
    public List<DexFileOptimizationStatus> getDexFileOptimizationStatuses() {
        return mDexFileOptimizationStatuses;
    }

    /** Describes the optimization status of a dex file. */
    @Immutable
    public static class DexFileOptimizationStatus {
        private final @NonNull String mDexFile;
        private final @NonNull String mInstructionSet;
        private final @NonNull String mCompilerFilter;
        private final @NonNull String mCompilationReason;
        private final @NonNull String mLocationDebugString;

        /** @hide */
        public DexFileOptimizationStatus(@NonNull String dexFile, @NonNull String instructionSet,
                @NonNull String compilerFilter, @NonNull String compilationReason,
                @NonNull String locationDebugString) {
            mDexFile = dexFile;
            mInstructionSet = instructionSet;
            mCompilerFilter = compilerFilter;
            mCompilationReason = compilationReason;
            mLocationDebugString = locationDebugString;
        }

        /** The absolute path to the dex file. */
        public @NonNull String getDexFile() {
            return mDexFile;
        }

        /** The instruction set. */
        public @NonNull String getInstructionSet() {
            return mInstructionSet;
        }

        /**
         * A string that describes the compiler filter.
         *
         * Possible values are:
         * <ul>
         *   <li>A valid value of the {@code --compiler-filer} option passed to {@code dex2oat}, if
         *     the optimized artifacts are valid.
         *   <li>{@code "run-from-apk"}, if the optimized artifacts do not exist.
         *   <li>{@code "run-from-apk-fallback"}, if the optimized artifacts exist but are invalid
         *     because the dex file has changed.
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
