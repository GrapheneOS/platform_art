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
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Binder;
import android.os.PowerManager;
import android.os.WorkSource;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.OptimizeOptions;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageState;
import com.android.server.pm.snapshot.PackageDataSnapshot;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Supplier;

/**
 * A helper class to handle dexopt.
 *
 * It talks to other components (e.g., PowerManager) and dispatches tasks to dex optimizers.
 *
 * @hide
 */
public class DexOptHelper {
    private static final String TAG = "DexoptHelper";

    /**
     * Timeout of the wake lock. This is required by AndroidLint, but we set it to a value larger
     * than artd's {@code kLongTimeoutSec} so that it should normally never triggered.
     */
    private static final int WAKE_LOCK_TIMEOUT_MS = 11 * 60 * 1000; // 11 minutes.

    @NonNull private final Injector mInjector;

    public DexOptHelper(@Nullable Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public DexOptHelper(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * DO NOT use this method directly. Use {@link
     * ArtManagerLocal#optimizePackage(PackageDataSnapshot, String, OptimizeOptions)}.
     */
    @NonNull
    public OptimizeResult dexopt(@NonNull PackageDataSnapshot snapshot,
            @NonNull PackageState pkgState, @NonNull AndroidPackageApi pkg,
            @NonNull OptimizeOptions options) {
        List<DexFileOptimizeResult> results = new ArrayList<>();
        Supplier<OptimizeResult> createResult = ()
                -> new OptimizeResult(pkgState.getPackageName(), options.getCompilerFilter(),
                        options.getReason(), results);

        if (!canOptimizePackage(pkgState, pkg)) {
            return createResult.get();
        }

        long identityToken = Binder.clearCallingIdentity();
        PowerManager.WakeLock wakeLock = null;

        try {
            // Acquire a wake lock.
            // The power manager service may not be ready if this method is called on boot. In this
            // case, we don't have to acquire a wake lock because there is nothing else we can do.
            PowerManager powerManager = mInjector.getPowerManager();
            if (powerManager != null) {
                wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, TAG);
                wakeLock.setWorkSource(new WorkSource(pkg.getUid()));
                wakeLock.acquire(WAKE_LOCK_TIMEOUT_MS);
            }

            if (options.isForPrimaryDex()) {
                results.addAll(mInjector.getPrimaryDexOptimizer().dexopt(pkgState, pkg, options));
            }

            if (options.isForSecondaryDex()) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Optimizing secondary dex'es is not implemented yet");
            }

            if (options.getIncludesDependencies()) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Optimizing dependencies is not implemented yet");
            }
        } finally {
            if (wakeLock != null) {
                wakeLock.release();
            }
            Binder.restoreCallingIdentity(identityToken);
        }

        return createResult.get();
    }

    private boolean canOptimizePackage(
            @NonNull PackageState pkgState, @NonNull AndroidPackageApi pkg) {
        if (!pkg.isHasCode()) {
            return false;
        }

        // We do not dexopt unused packages.
        // It's possible for this to be called before app hibernation service is ready, especially
        // on boot. In this case, we ignore the hibernation check here because there is nothing else
        // we can do.
        AppHibernationManager ahm = mInjector.getAppHibernationManager();
        if (ahm != null && ahm.isHibernatingGlobally(pkgState.getPackageName())
                && ahm.isOatArtifactDeletionEnabled()) {
            return false;
        }

        return true;
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
        PrimaryDexOptimizer getPrimaryDexOptimizer() {
            return new PrimaryDexOptimizer(mContext);
        }

        @Nullable
        public AppHibernationManager getAppHibernationManager() {
            return mContext != null ? mContext.getSystemService(AppHibernationManager.class) : null;
        }

        @Nullable
        public PowerManager getPowerManager() {
            return mContext != null ? mContext.getSystemService(PowerManager.class) : null;
        }
    }
}
