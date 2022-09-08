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

    private final @NonNull String mPackageName;
    private final @NonNull String mRequestedCompilerFilter;
    private final @NonNull String mReason;
    private final @NonNull List<DexFileOptimizeResult> mDexFileOptimizeResults;

    public OptimizeResult(@NonNull String packageName, @NonNull String requestedCompilerFilter,
            @NonNull String reason, @NonNull List<DexFileOptimizeResult> dexFileOptimizeResults) {
        mPackageName = packageName;
        mRequestedCompilerFilter = requestedCompilerFilter;
        mReason = reason;
        mDexFileOptimizeResults = dexFileOptimizeResults;
    }

    /** The package name. */
    public @NonNull String getPackageName() {
        return mPackageName;
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

    /** The final status. */
    public @OptimizeStatus int getFinalStatus() {
        return mDexFileOptimizeResults.stream()
                .mapToInt(result -> result.getStatus())
                .max()
                .orElse(OPTIMIZE_SKIPPED);
    }

    /** The result of each individual dex file. */
    @NonNull
    public List<DexFileOptimizeResult> getDexFileOptimizeResults() {
        return mDexFileOptimizeResults;
    }

    /** Describes the result of a dex file. */
    @Immutable
    public static class DexFileOptimizeResult {
        private final @NonNull String mDexFile;
        private final @NonNull String mInstructionSet;
        private final @NonNull String mActualCompilerFilter;
        private final @OptimizeStatus int mStatus;

        /** @hide */
        public DexFileOptimizeResult(@NonNull String dexFile, @NonNull String instructionSet,
                @NonNull String compilerFilter, @OptimizeStatus int status) {
            mDexFile = dexFile;
            mInstructionSet = instructionSet;
            mActualCompilerFilter = compilerFilter;
            mStatus = status;
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
    }
}
