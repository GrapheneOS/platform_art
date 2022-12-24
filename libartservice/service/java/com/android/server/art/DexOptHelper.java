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

import static com.android.server.art.ArtManagerLocal.OptimizePackageDoneCallback;
import static com.android.server.art.model.Config.Callback;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.WorkSource;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.Config;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.SharedLibrary;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Objects;
import java.util.Queue;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.function.Supplier;
import java.util.stream.Collectors;

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
     * Timeout of the wake lock. This is required by AndroidLint, but we set it to a very large
     * value so that it should normally never triggered.
     */
    private static final long WAKE_LOCK_TIMEOUT_MS = TimeUnit.DAYS.toMillis(1);

    @NonNull private final Injector mInjector;

    public DexOptHelper(@NonNull Context context, @NonNull Config config) {
        this(new Injector(context, config));
    }

    @VisibleForTesting
    public DexOptHelper(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * DO NOT use this method directly. Use {@link ArtManagerLocal#optimizePackage} or {@link
     * ArtManagerLocal#optimizePackages}.
     */
    @NonNull
    public OptimizeResult dexopt(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull List<String> packageNames, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal, @NonNull Executor dexoptExecutor) {
        return dexopt(snapshot, packageNames, params, cancellationSignal, dexoptExecutor,
                null /* progressCallbackExecutor */, null /* progressCallback */);
    }

    /**
     * DO NOT use this method directly. Use {@link ArtManagerLocal#optimizePackage} or {@link
     * ArtManagerLocal#optimizePackages}.
     */
    @NonNull
    public OptimizeResult dexopt(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull List<String> packageNames, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal, @NonNull Executor dexoptExecutor,
            @Nullable Executor progressCallbackExecutor,
            @Nullable Consumer<OperationProgress> progressCallback) {
        return dexoptPackages(
                getPackageStates(snapshot, packageNames,
                        (params.getFlags() & ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES) != 0),
                params, cancellationSignal, dexoptExecutor, progressCallbackExecutor,
                progressCallback);
    }

    /**
     * DO NOT use this method directly. Use {@link ArtManagerLocal#optimizePackage} or {@link
     * ArtManagerLocal#optimizePackages}.
     */
    @NonNull
    private OptimizeResult dexoptPackages(@NonNull List<PackageState> pkgStates,
            @NonNull OptimizeParams params, @NonNull CancellationSignal cancellationSignal,
            @NonNull Executor dexoptExecutor, @Nullable Executor progressCallbackExecutor,
            @Nullable Consumer<OperationProgress> progressCallback) {
        int callingUid = Binder.getCallingUid();
        long identityToken = Binder.clearCallingIdentity();
        PowerManager.WakeLock wakeLock = null;

        try {
            // Acquire a wake lock.
            PowerManager powerManager = mInjector.getPowerManager();
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, TAG);
            wakeLock.setWorkSource(new WorkSource(callingUid));
            wakeLock.acquire(WAKE_LOCK_TIMEOUT_MS);

            List<CompletableFuture<PackageOptimizeResult>> futures = new ArrayList<>();
            for (PackageState pkgState : pkgStates) {
                futures.add(CompletableFuture.supplyAsync(
                        () -> dexoptPackage(pkgState, params, cancellationSignal), dexoptExecutor));
            }

            if (progressCallback != null) {
                CompletableFuture.runAsync(() -> {
                    progressCallback.accept(
                            OperationProgress.create(0 /* current */, futures.size()));
                }, progressCallbackExecutor);
                AtomicInteger current = new AtomicInteger(0);
                for (CompletableFuture<PackageOptimizeResult> future : futures) {
                    future.thenRunAsync(() -> {
                        progressCallback.accept(OperationProgress.create(
                                current.incrementAndGet(), futures.size()));
                    }, progressCallbackExecutor);
                }
            }

            List<PackageOptimizeResult> results =
                    futures.stream().map(Utils::getFuture).collect(Collectors.toList());

            var result =
                    new OptimizeResult(params.getCompilerFilter(), params.getReason(), results);

            for (Callback<OptimizePackageDoneCallback, Boolean> doneCallback :
                    mInjector.getConfig().getOptimizePackageDoneCallbacks()) {
                boolean onlyIncludeUpdates = doneCallback.extra();
                if (onlyIncludeUpdates) {
                    List<PackageOptimizeResult> filteredResults =
                            results.stream()
                                    .filter(PackageOptimizeResult::hasUpdatedArtifacts)
                                    .collect(Collectors.toList());
                    if (!filteredResults.isEmpty()) {
                        var resultForCallback = new OptimizeResult(
                                params.getCompilerFilter(), params.getReason(), filteredResults);
                        CompletableFuture.runAsync(() -> {
                            doneCallback.get().onOptimizePackageDone(resultForCallback);
                        }, doneCallback.executor());
                    }
                } else {
                    CompletableFuture.runAsync(() -> {
                        doneCallback.get().onOptimizePackageDone(result);
                    }, doneCallback.executor());
                }
            }

            return result;
        } finally {
            if (wakeLock != null) {
                wakeLock.release();
            }
            Binder.restoreCallingIdentity(identityToken);
        }
    }

    /**
     * DO NOT use this method directly. Use {@link ArtManagerLocal#optimizePackage} or {@link
     * ArtManagerLocal#optimizePackages}.
     */
    @NonNull
    private PackageOptimizeResult dexoptPackage(@NonNull PackageState pkgState,
            @NonNull OptimizeParams params, @NonNull CancellationSignal cancellationSignal) {
        List<DexContainerFileOptimizeResult> results = new ArrayList<>();
        Supplier<PackageOptimizeResult> createResult = ()
                -> new PackageOptimizeResult(
                        pkgState.getPackageName(), results, cancellationSignal.isCanceled());

        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        if (!canOptimizePackage(pkgState)) {
            return createResult.get();
        }

        if ((params.getFlags() & ArtFlags.FLAG_FOR_SINGLE_SPLIT) != 0) {
            // Throws if the split is not found.
            PrimaryDexUtils.getDexInfoBySplitName(pkg, params.getSplitName());
        }

        try {
            if ((params.getFlags() & ArtFlags.FLAG_FOR_PRIMARY_DEX) != 0) {
                if (cancellationSignal.isCanceled()) {
                    return createResult.get();
                }

                results.addAll(
                        mInjector.getPrimaryDexOptimizer(pkgState, pkg, params, cancellationSignal)
                                .dexopt());
            }

            if ((params.getFlags() & ArtFlags.FLAG_FOR_SECONDARY_DEX) != 0) {
                if (cancellationSignal.isCanceled()) {
                    return createResult.get();
                }

                results.addAll(
                        mInjector
                                .getSecondaryDexOptimizer(pkgState, pkg, params, cancellationSignal)
                                .dexopt());
            }
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }

        return createResult.get();
    }

    private boolean canOptimizePackage(@NonNull PackageState pkgState) {
        return Utils.canOptimizePackage(pkgState, mInjector.getAppHibernationManager());
    }

    @NonNull
    private List<PackageState> getPackageStates(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull List<String> packageNames, boolean includeDependencies) {
        var pkgStates = new LinkedHashMap<String, PackageState>();
        Set<String> visitedLibraries = new HashSet<>();
        Queue<SharedLibrary> queue = new LinkedList<>();

        Consumer<SharedLibrary> maybeEnqueue = library -> {
            // The package name is not null if the library is an APK.
            // TODO(jiakaiz): Support JAR libraries.
            if (library.getPackageName() != null && !visitedLibraries.contains(library.getName())) {
                visitedLibraries.add(library.getName());
                queue.add(library);
            }
        };

        for (String packageName : packageNames) {
            PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
            Utils.getPackageOrThrow(pkgState);
            pkgStates.put(packageName, pkgState);
            if (includeDependencies && canOptimizePackage(pkgState)) {
                for (SharedLibrary library : pkgState.getUsesLibraries()) {
                    maybeEnqueue.accept(library);
                }
            }
        }

        SharedLibrary library;
        while ((library = queue.poll()) != null) {
            String packageName = library.getPackageName();
            PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
            if (canOptimizePackage(pkgState)) {
                pkgStates.put(packageName, pkgState);

                // Note that `library.getDependencies()` is different from
                // `pkgState.getUsesLibraries()`. Different libraries can belong to the same
                // package. `pkgState.getUsesLibraries()` returns a union of dependencies of
                // libraries that belong to the same package, which is not what we want here.
                // Therefore, this loop cannot be unified with the one above.
                for (SharedLibrary dep : library.getDependencies()) {
                    maybeEnqueue.accept(dep);
                }
            }
        }

        // `LinkedHashMap` guarantees deterministic order.
        return new ArrayList<>(pkgStates.values());
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;
        @NonNull private final Config mConfig;

        Injector(@NonNull Context context, @NonNull Config config) {
            mContext = context;
            mConfig = config;

            // Call the getters for various dependencies, to ensure correct initialization order.
            getAppHibernationManager();
            getPowerManager();
        }

        @NonNull
        PrimaryDexOptimizer getPrimaryDexOptimizer(@NonNull PackageState pkgState,
                @NonNull AndroidPackage pkg, @NonNull OptimizeParams params,
                @NonNull CancellationSignal cancellationSignal) {
            return new PrimaryDexOptimizer(mContext, pkgState, pkg, params, cancellationSignal);
        }

        @NonNull
        SecondaryDexOptimizer getSecondaryDexOptimizer(@NonNull PackageState pkgState,
                @NonNull AndroidPackage pkg, @NonNull OptimizeParams params,
                @NonNull CancellationSignal cancellationSignal) {
            return new SecondaryDexOptimizer(mContext, pkgState, pkg, params, cancellationSignal);
        }

        @NonNull
        public AppHibernationManager getAppHibernationManager() {
            return Objects.requireNonNull(mContext.getSystemService(AppHibernationManager.class));
        }

        @NonNull
        public PowerManager getPowerManager() {
            return Objects.requireNonNull(mContext.getSystemService(PowerManager.class));
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }
    }
}
