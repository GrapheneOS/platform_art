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
import android.text.TextUtils;

import com.android.server.art.PriorityClass;
import com.android.server.art.ReasonMapping;
import com.android.server.art.Utils;

/** @hide */
public class OptimizeOptions {
    public static final class Builder {
        private OptimizeOptions mOptions = new OptimizeOptions();

        /**
         * Creates a builder.
         *
         * @param reason See {@link #setReason(String)}.
         */
        public Builder(@NonNull String reason) {
            setReason(reason);
        }

        /** Whether to generate optimized artifacts for primary dex'es. Default: true. */
        public Builder setForPrimaryDex(boolean value) {
            mOptions.mIsForPrimaryDex = value;
            return this;
        }

        /** Whether to generate optimized artifacts for secondary dex'es. Default: false. */
        public Builder setForSecondaryDex(boolean value) {
            mOptions.mIsForSecondaryDex = value;
            return this;
        }

        /** Whether to optimize dependency packages as well. Default: false. */
        public Builder setIncludesDependencies(boolean value) {
            mOptions.mIncludesDependencies = value;
            return this;
        }

        /**
         * The target compiler filter. Note that the compiler filter might be adjusted before the
         * execution based on factors like whether the profile is available or whether the app is
         * used by other apps. If not set, the default compiler filter for the given reason will be
         * used.
         */
        public Builder setCompilerFilter(@NonNull String value) {
            mOptions.mCompilerFilter = value;
            return this;
        }

        /**
         * The priority of the operation. If not set, the default priority class for the given
         * reason will be used.
         *
         * @see PriorityClass
         */
        public Builder setPriorityClass(@PriorityClass byte value) {
            mOptions.mPriorityClass = value;
            return this;
        }

        /**
         * Compilation reason. Can be a string defined in {@link ReasonMapping} or a custom string.
         *
         * If the value is a string defined in {@link ReasonMapping}, it determines the compiler
         * filter and/or the priority class, if those values are not explicitly set.
         *
         * If the value is a custom string, the priority class and the compiler filter must be
         * explicitly set.
         */
        public Builder setReason(@NonNull String value) {
            mOptions.mReason = value;
            return this;
        }

        /**
         * Whether the intention is to downgrade the compiler filter. If true, the compilation will
         * be skipped if the target compiler filter is better than or equal to the compiler filter
         * of the existing optimized artifacts, or optimized artifacts do not exist.
         */
        public Builder setShouldDowngrade(boolean value) {
            mOptions.mShouldDowngrade = value;
            return this;
        }

        /**
         * Whether to force compilation. If true, the compilation will be performed regardless of
         * any existing optimized artifacts.
         */
        public Builder setForce(boolean value) {
            mOptions.mForce = value;
            return this;
        }

        /**
         * Returns the built object.
         *
         * @throws IllegalArgumentException if the built options would be invalid
         */
        public OptimizeOptions build() {
            if (mOptions.mReason.isEmpty()) {
                throw new IllegalArgumentException("Reason must not be empty");
            }

            if (mOptions.mCompilerFilter.isEmpty()) {
                mOptions.mCompilerFilter =
                        ReasonMapping.getCompilerFilterForReason(mOptions.mReason);
            } else if (!Utils.isValidArtServiceCompilerFilter(mOptions.mCompilerFilter)) {
                throw new IllegalArgumentException(
                        "Invalid compiler filter '" + mOptions.mCompilerFilter + "'");
            }

            if (mOptions.mPriorityClass == -1) {
                mOptions.mPriorityClass = ReasonMapping.getPriorityClassForReason(mOptions.mReason);
            } else if (mOptions.mPriorityClass < 0 || mOptions.mPriorityClass > 100) {
                throw new IllegalArgumentException("Invalid priority class "
                        + mOptions.mPriorityClass + ". Must be between 0 and 100");
            }

            return mOptions;
        }
    }

    /**
     * A value indicating that dexopt shouldn't be run. This value is consumed by ART Services and
     * is not propagated to dex2oat.
     */
    public static final String COMPILER_FILTER_NOOP = "skip";

    private boolean mIsForPrimaryDex = true;
    private boolean mIsForSecondaryDex = false;
    private boolean mIncludesDependencies = false;
    private @NonNull String mCompilerFilter = "";
    private @PriorityClass byte mPriorityClass = -1;
    private @NonNull String mReason = "";
    private boolean mShouldDowngrade = false;
    private boolean mForce = false;

    private OptimizeOptions() {}

    /** Whether to generate optimized artifacts for primary dex'es. */
    public boolean isForPrimaryDex() {
        return mIsForPrimaryDex;
    }

    /** Whether to generate optimized artifacts for secondary dex'es. */
    public boolean isForSecondaryDex() {
        return mIsForSecondaryDex;
    }

    /** Whether to optimize dependency packages as well. */
    public boolean getIncludesDependencies() {
        return mIncludesDependencies;
    }

    /** The target compiler filter. */
    public @NonNull String getCompilerFilter() {
        return mCompilerFilter;
    }

    /** The priority class. */
    public @PriorityClass byte getPriorityClass() {
        return mPriorityClass;
    }

    /**
     * The compilation reason.
     *
     * DO NOT directly use the string value to determine the resource usage and the process
     * priority. Use {@link #getPriorityClass}.
     */
    public @NonNull String getReason() {
        return mReason;
    }

    /** Whether the intention is to downgrade the compiler filter. */
    public boolean getShouldDowngrade() {
        return mShouldDowngrade;
    }

    /** Whether to force compilation. */
    public boolean getForce() {
        return mForce;
    }
}
