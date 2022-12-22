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

import com.google.auto.value.AutoValue;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** @hide */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@Immutable
@AutoValue
public abstract class BatchOptimizeParams {
    /** @hide */
    @SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
    public static final class Builder {
        private @NonNull List<String> mPackageNames; // This is assumed immutable.
        private @NonNull OptimizeParams mOptimizeParams;

        /** @hide */
        public Builder(@NonNull List<String> defaultPackages,
                @NonNull OptimizeParams defaultOptimizeParams) {
            mPackageNames = defaultPackages; // The argument is assumed immutable.
            mOptimizeParams = defaultOptimizeParams;
        }

        /**
         * Sets the list of packages to optimize. The optimization will be scheduled in the given
         * order.
         *
         * If not called, the default list will be used.
         */
        @NonNull
        public Builder setPackages(@NonNull List<String> packageNames) {
            mPackageNames = Collections.unmodifiableList(new ArrayList<>(packageNames));
            return this;
        }

        /**
         * Sets the params for optimizing each package.
         *
         * If not called, the default params built from {@link OptimizeParams#Builder(String)} will
         * be used.
         */
        @NonNull
        public Builder setOptimizeParams(@NonNull OptimizeParams optimizeParams) {
            mOptimizeParams = optimizeParams;
            return this;
        }

        /** Returns the built object. */
        @NonNull
        public BatchOptimizeParams build() {
            return new AutoValue_BatchOptimizeParams(mPackageNames, mOptimizeParams);
        }
    }

    /** @hide */
    protected BatchOptimizeParams() {}

    /** The ordered list of packages to optimize. */
    public abstract @NonNull List<String> getPackages();

    /** The params for optimizing each package. */
    public abstract @NonNull OptimizeParams getOptimizeParams();
}
