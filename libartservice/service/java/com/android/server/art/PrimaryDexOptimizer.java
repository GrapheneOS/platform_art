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

package com.android.server.art;

import static com.android.server.art.model.OptimizeResult.DexFileOptimizeResult;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.OptimizeOptions;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;

import java.util.List;

/** @hide */
public class PrimaryDexOptimizer {
    private static final String TAG = "PrimaryDexOptimizer";

    @NonNull private final Injector mInjector;

    public PrimaryDexOptimizer(@Nullable Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public PrimaryDexOptimizer(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * DO NOT use this method directly. Use {@link
     * ArtManagerLocal#optimizePackage(PackageDataSnapshot, String, OptimizeOptions)}.
     */
    @NonNull
    public List<DexFileOptimizeResult> dexopt(@NonNull PackageState pkgState,
            @NonNull AndroidPackageApi pkg, @NonNull OptimizeOptions options) {
        throw new UnsupportedOperationException();
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        // TODO(b/236954191): Make this @NonNull.
        @Nullable private final Context mContext;

        Injector(@Nullable Context context) {
            mContext = context;
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }
    }
}
