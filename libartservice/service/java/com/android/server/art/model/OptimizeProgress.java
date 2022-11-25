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

import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.annotation.Nullable;

import com.android.internal.annotations.Immutable;

import com.google.auto.value.AutoValue;

/** @hide */
@Immutable
@AutoValue
public abstract class OptimizeProgress {
    /** @hide */
    protected OptimizeProgress() {}

    /** @hide */
    public static @NonNull OptimizeProgress create(int donePackageCount, int totalPackageCount) {
        return new AutoValue_OptimizeProgress(donePackageCount, totalPackageCount);
    }

    /**
     * The number of packages, for which optimization has been done, regardless of the results
     * (performed, failed, skipped, etc.). Can be 0, which means the optimization was just started.
     */
    public abstract int getDonePackageCount();

    /**
     * The total number of packages to optimize. Stays constant during the operation.
     */
    public abstract int getTotalPackageCount();
}
