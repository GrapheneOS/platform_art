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

import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.WorkSource;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

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

    public DexOptHelper(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public DexOptHelper(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * DO NOT use this method directly. Use {@link
     * ArtManagerLocal#optimizePackage(PackageManagerLocal.FilteredSnapshot, String,
     * OptimizeParams)}.
     */
    @NonNull
    public OptimizeResult dexopt(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull PackageState pkgState, @NonNull AndroidPackage pkg,
            @NonNull OptimizeParams params, @NonNull CancellationSignal cancellationSignal)
            throws RemoteException {
        List<DexContainerFileOptimizeResult> results = new ArrayList<>();
        Supplier<OptimizeResult> createResult = ()
                -> new OptimizeResult(params.getCompilerFilter(), params.getReason(),
                        List.of(new PackageOptimizeResult(pkgState.getPackageName(), results)));
        Supplier<Boolean> hasCancelledResult = ()
                -> results.stream().anyMatch(
                        result -> result.getStatus() == OptimizeResult.OPTIMIZE_CANCELLED);

        if (!canOptimizePackage(pkgState, pkg)) {
            return createResult.get();
        }

        long identityToken = Binder.clearCallingIdentity();
        PowerManager.WakeLock wakeLock = null;

        try {
            // Acquire a wake lock.
            PowerManager powerManager = mInjector.getPowerManager();
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, TAG);
            wakeLock.setWorkSource(new WorkSource(pkgState.getAppId()));
            wakeLock.acquire(WAKE_LOCK_TIMEOUT_MS);

            if ((params.getFlags() & ArtFlags.FLAG_FOR_PRIMARY_DEX) != 0) {
                results.addAll(mInjector.getPrimaryDexOptimizer().dexopt(
                        pkgState, pkg, params, cancellationSignal));
                if (hasCancelledResult.get()) {
                    return createResult.get();
                }
            }

            if ((params.getFlags() & ArtFlags.FLAG_FOR_SECONDARY_DEX) != 0) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Optimizing secondary dex'es is not implemented yet");
            }

            if ((params.getFlags() & ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES) != 0) {
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
            @NonNull PackageState pkgState, @NonNull AndroidPackage pkg) {
        if (!pkg.getSplits().get(0).isHasCode()) {
            return false;
        }

        // We do not dexopt unused packages.
        AppHibernationManager ahm = mInjector.getAppHibernationManager();
        if (ahm.isHibernatingGlobally(pkgState.getPackageName())
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
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;
        }

        @NonNull
        PrimaryDexOptimizer getPrimaryDexOptimizer() {
            return new PrimaryDexOptimizer(mContext);
        }

        @NonNull
        public AppHibernationManager getAppHibernationManager() {
            return mContext.getSystemService(AppHibernationManager.class);
        }

        @NonNull
        public PowerManager getPowerManager() {
            return mContext.getSystemService(PowerManager.class);
        }
    }
}
