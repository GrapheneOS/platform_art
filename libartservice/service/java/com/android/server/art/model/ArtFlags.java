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
import android.annotation.SystemApi;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** @hide */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
public class ArtFlags {
    /** Whether the operation is applied for primary dex'es. */
    public static final int FLAG_FOR_PRIMARY_DEX = 1 << 0;
    /** Whether the operation is applied for secondary dex'es. */
    public static final int FLAG_FOR_SECONDARY_DEX = 1 << 1;

    /**
     * Flags for {@link ArtManagerLocal#deleteOptimizedArtifacts(PackageDataSnapshot, String, int)}.
     *
     * @hide
     */
    // clang-format off
    @IntDef(flag = true, prefix = "FLAG_", value = {
        FLAG_FOR_PRIMARY_DEX,
        FLAG_FOR_SECONDARY_DEX,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface DeleteFlags {}

    /**
     * Default flags that are used when
     * {@link ArtManagerLocal#deleteOptimizedArtifacts(PackageDataSnapshot, String)} is called.
     * Value: {@link #FLAG_FOR_PRIMARY_DEX}.
     */
    public static @DeleteFlags int defaultDeleteFlags() {
        return FLAG_FOR_PRIMARY_DEX;
    }

    /**
     * Flags for {@link ArtManagerLocal#getOptimizationStatus(PackageDataSnapshot, String, int)}.
     *
     * @hide
     */
    // clang-format off
    @IntDef(flag = true, prefix = "FLAG_", value = {
        FLAG_FOR_PRIMARY_DEX,
        FLAG_FOR_SECONDARY_DEX,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface GetStatusFlags {}

    /**
     * Default flags that are used when
     * {@link ArtManagerLocal#getOptimizationStatus(PackageDataSnapshot, String)} is called.
     * Value: {@link #FLAG_FOR_PRIMARY_DEX}.
     */
    public static @GetStatusFlags int defaultGetStatusFlags() {
        return FLAG_FOR_PRIMARY_DEX;
    }

    private ArtFlags() {}
}
