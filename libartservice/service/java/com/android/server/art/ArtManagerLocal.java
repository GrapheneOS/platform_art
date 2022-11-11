/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static com.android.server.art.PrimaryDexUtils.DetailedPrimaryDexInfo;
import static com.android.server.art.PrimaryDexUtils.PrimaryDexInfo;
import static com.android.server.art.ReasonMapping.BatchOptimizeReason;
import static com.android.server.art.Utils.Abi;
import static com.android.server.art.model.ArtFlags.DeleteFlags;
import static com.android.server.art.model.ArtFlags.GetStatusFlags;
import static com.android.server.art.model.Config.Callback;
import static com.android.server.art.model.OptimizationStatus.DexContainerFileOptimizationStatus;

import android.annotation.CallbackExecutor;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.SystemApi;
import android.annotation.SystemService;
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.ServiceSpecificException;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.BatchOptimizeParams;
import com.android.server.art.model.Config;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

/**
 * This class provides a system API for functionality provided by the ART module.
 *
 * Note: Although this class is the entry point of ART services, this class is not a {@link
 * SystemService}, and it does not publish a binder. Instead, it is a module loaded by the
 * system_server process, registered in {@link LocalManagerRegistry}. {@link LocalManagerRegistry}
 * specifies that in-process module interfaces should be named with the suffix {@code ManagerLocal}
 * for consistency.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
public final class ArtManagerLocal {
    private static final String TAG = "ArtService";

    @NonNull private final Injector mInjector;

    @Deprecated
    public ArtManagerLocal() {
        this(new Injector(null /* context */));
    }

    public ArtManagerLocal(@NonNull Context context) {
        this(new Injector(context));
    }

    /** @hide */
    @VisibleForTesting
    public ArtManagerLocal(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * Handles `cmd package art` sub-command.
     *
     * For debugging purposes only. Intentionally enforces root access to limit the usage.
     *
     * Note: This method is not an override of {@link Binder#handleShellCommand} because ART
     * services does not publish a binder. Instead, it handles the `art` sub-command forwarded by
     * the `package` service. The semantics of the parameters are the same as {@link
     * Binder#handleShellCommand}.
     *
     * @return zero on success, non-zero on internal error (e.g., I/O error)
     * @throws SecurityException if the caller is not root
     * @throws IllegalArgumentException if the arguments are illegal
     * @see ArtShellCommand#onHelp()
     */
    public int handleShellCommand(@NonNull Binder target, @NonNull ParcelFileDescriptor in,
            @NonNull ParcelFileDescriptor out, @NonNull ParcelFileDescriptor err,
            @NonNull String[] args) {
        return new ArtShellCommand(this, mInjector.getPackageManagerLocal())
                .exec(target, in.getFileDescriptor(), out.getFileDescriptor(),
                        err.getFileDescriptor(), args);
    }

    /**
     * Deletes optimized artifacts of a package.
     *
     * Uses the default flags ({@link ArtFlags#defaultDeleteFlags()}).
     *
     * @throws IllegalArgumentException if the package is not found or the flags are illegal
     * @throws IllegalStateException if an internal error occurs
     */
    @NonNull
    public DeleteResult deleteOptimizedArtifacts(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        return deleteOptimizedArtifacts(snapshot, packageName, ArtFlags.defaultDeleteFlags());
    }

    /**
     * Same as above, but allows to specify flags.
     *
     * @see #deleteOptimizedArtifacts(PackageManagerLocal.FilteredSnapshot, String)
     */
    @NonNull
    public DeleteResult deleteOptimizedArtifacts(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @DeleteFlags int flags) {
        if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to delete");
        }

        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        try {
            long freedBytes = 0;

            if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) != 0) {
                boolean isInDalvikCache = Utils.isInDalvikCache(pkgState);
                for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                    if (!dexInfo.hasCode()) {
                        continue;
                    }
                    for (Abi abi : Utils.getAllAbis(pkgState)) {
                        freedBytes +=
                                mInjector.getArtd().deleteArtifacts(AidlUtils.buildArtifactsPath(
                                        dexInfo.dexPath(), abi.isa(), isInDalvikCache));
                    }
                }
            }

            if ((flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) != 0) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Deleting artifacts of secondary dex'es is not implemented yet");
            }

            return new DeleteResult(freedBytes);
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /**
     * Returns the optimization status of a package.
     *
     * Uses the default flags ({@link ArtFlags#defaultGetStatusFlags()}).
     *
     * @throws IllegalArgumentException if the package is not found or the flags are illegal
     * @throws IllegalStateException if an internal error occurs
     */
    @NonNull
    public OptimizationStatus getOptimizationStatus(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName) {
        return getOptimizationStatus(snapshot, packageName, ArtFlags.defaultGetStatusFlags());
    }

    /**
     * Same as above, but allows to specify flags.
     *
     * @see #getOptimizationStatus(PackageManagerLocal.FilteredSnapshot, String)
     */
    @NonNull
    public OptimizationStatus getOptimizationStatus(
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @GetStatusFlags int flags) {
        if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to check");
        }

        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);

        try {
            List<DexContainerFileOptimizationStatus> statuses = new ArrayList<>();

            if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) != 0) {
                for (DetailedPrimaryDexInfo dexInfo :
                        PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
                    if (!dexInfo.hasCode()) {
                        continue;
                    }
                    for (Abi abi : Utils.getAllAbis(pkgState)) {
                        try {
                            GetOptimizationStatusResult result =
                                    mInjector.getArtd().getOptimizationStatus(dexInfo.dexPath(),
                                            abi.isa(), dexInfo.classLoaderContext());
                            statuses.add(
                                    DexContainerFileOptimizationStatus.create(dexInfo.dexPath(),
                                            abi.isPrimaryAbi(), abi.name(), result.compilerFilter,
                                            result.compilationReason, result.locationDebugString));
                        } catch (ServiceSpecificException e) {
                            statuses.add(DexContainerFileOptimizationStatus.create(
                                    dexInfo.dexPath(), abi.isPrimaryAbi(), abi.name(), "error",
                                    "error", e.getMessage()));
                        }
                    }
                }
            }

            if ((flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) != 0) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Getting optimization status of secondary dex'es is not implemented yet");
            }

            return OptimizationStatus.create(statuses);
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    /**
     * Optimizes a package. The time this operation takes ranges from a few milliseconds to several
     * minutes, depending on the params and the code size of the package.
     *
     * @throws IllegalArgumentException if the package is not found or the params are illegal
     * @throws IllegalStateException if an internal error occurs
     */
    @NonNull
    public OptimizeResult optimizePackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull OptimizeParams params) {
        var cancellationSignal = new CancellationSignal();
        return optimizePackage(snapshot, packageName, params, cancellationSignal);
    }

    /**
     * Same as above, but supports cancellation.
     *
     * @see #optimizePackage(PackageManagerLocal.FilteredSnapshot, String, OptimizeParams)
     */
    @NonNull
    public OptimizeResult optimizePackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal) {
        if ((params.getFlags() & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (params.getFlags() & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to optimize");
        }

        if ((params.getFlags() & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (params.getFlags() & ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES) != 0) {
            throw new IllegalArgumentException(
                    "FLAG_SHOULD_INCLUDE_DEPENDENCIES must not set if FLAG_FOR_PRIMARY_DEX is not "
                    + "set.");
        }

        return mInjector.getDexOptHelper().dexopt(
                snapshot, List.of(packageName), params, cancellationSignal, Runnable::run);
    }

    /**
     * Runs batch optimization for the given reason.
     *
     * This is called by ART Service automatically during boot / background dexopt.
     *
     * The list of packages and options are determined by {@code reason}, and can be overridden by
     * {@link #setOptimizePackagesCallback(Executor, OptimizePackagesCallback)}.
     *
     * The optimization is done in a thread pool. The number of packages being optimized
     * simultaneously can be configured by system property {@code pm.dexopt.<reason>.concurrency}
     * (e.g., {@code pm.dexopt.bg-dexopt.concurrency=4}), and the number of threads for each {@code
     * dex2oat} invocation can be configured by system property {@code dalvik.vm.*dex2oat-threads}
     * (e.g., {@code dalvik.vm.background-dex2oat-threads=4}). I.e., the maximum number of
     * concurrent threads is the product of the two system properties. Note that the physical core
     * usage is always bound by {@code dalvik.vm.*dex2oat-cpu-set} regardless of the number of
     * threads.
     *
     * @param snapshot the snapshot from {@link PackageManagerLocal} to operate on
     * @param reason determines the default list of packages and options
     * @param cancellationSignal provides the ability to cancel this operation
     * @throws IllegalStateException if an internal error occurs, or the callback set by {@link
     *         #setOptimizePackagesCallback(Executor, OptimizePackagesCallback)} provides invalid
     *         params.
     *
     * @hide
     */
    @NonNull
    public OptimizeResult optimizePackages(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull @BatchOptimizeReason String reason,
            @NonNull CancellationSignal cancellationSignal) {
        List<String> defaultPackages =
                Collections.unmodifiableList(getDefaultPackages(snapshot, reason));
        OptimizeParams defaultOptimizeParams = new OptimizeParams.Builder(reason).build();
        var builder = new BatchOptimizeParams.Builder(defaultPackages, defaultOptimizeParams);
        Callback<OptimizePackagesCallback> callback =
                mInjector.getConfig().getOptimizePackagesCallback();
        if (callback != null) {
            Utils.executeAndWait(callback.executor(), () -> {
                callback.get().onOverrideBatchOptimizeParams(
                        snapshot, reason, defaultPackages, builder);
            });
        }
        BatchOptimizeParams params = builder.build();
        Utils.check(params.getOptimizeParams().getReason().equals(reason));

        return mInjector.getDexOptHelper().dexopt(snapshot, params.getPackages(),
                params.getOptimizeParams(), cancellationSignal,
                Executors.newFixedThreadPool(ReasonMapping.getConcurrencyForReason(reason)));
    }

    /**
     * Overrides the default params for {@link
     * #optimizePackages(PackageManagerLocal.FilteredSnapshot, String). This method is thread-safe.
     *
     * This method gives users the opportunity to change the behavior of {@link
     * #optimizePackages(PackageManagerLocal.FilteredSnapshot, String)}, which is called by ART
     * Service automatically during boot / background dexopt.
     *
     * If this method is not called, the default list of packages and options determined by {@code
     * reason} will be used.
     */
    public void setOptimizePackagesCallback(@NonNull @CallbackExecutor Executor executor,
            @NonNull OptimizePackagesCallback callback) {
        mInjector.getConfig().setOptimizePackagesCallback(executor, callback);
    }

    /**
     * Clears the callback set by {@link #setOptimizePackagesCallback(Executor,
     * OptimizePackagesCallback)}. This method is thread-safe.
     */
    public void clearOptimizePackagesCallback() {
        mInjector.getConfig().clearOptimizePackagesCallback();
    }

    /**
     * Notifies ART Service that a list of dex container files have been loaded.
     *
     * ART Service uses this information to:
     * <ul>
     *   <li>Determine whether an app is used by another app
     *   <li>Record which secondary dex container files to optimize and how to optimize them
     * </ul>
     *
     * @param loadingPackageName the name of the package who performs the load. ART Service assumes
     *         that this argument has been validated that it exists in the snapshot and matches the
     *         calling UID
     * @param classLoaderContextByDexContainerFile a map from dex container files' absolute paths to
     *         the string representations of the class loader contexts used to load them
     * @throws IllegalArgumentException if {@code classLoaderContextByDexContainerFile} contains
     *         invalid entries
     */
    public void notifyDexContainersLoaded(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Map<String, String> classLoaderContextByDexContainerFile) {
        DexUseManager.getInstance().addDexUse(
                snapshot, loadingPackageName, classLoaderContextByDexContainerFile);
    }

    @NonNull
    private List<String> getDefaultPackages(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull @BatchOptimizeReason String reason) {
        var packages = new ArrayList<String>();
        snapshot.forAllPackageStates((pkgState) -> {
            if (Utils.canOptimizePackage(pkgState, mInjector.getAppHibernationManager())) {
                packages.add(pkgState.getPackageName());
            }
        });
        return packages;
    }

    public interface OptimizePackagesCallback {
        /**
         * Mutates {@code builder} to override the default params for {@link
         * #optimizePackages(PackageManagerLocal.FilteredSnapshot, String). It must ignore unknown
         * reasons because more reasons may be added in the future.
         *
         * If {@code builder.setPackages} is not called, {@code defaultPackages} will be used as the
         * list of packages to optimize.
         *
         * If {@code builder.setOptimizeParams} is not called, the default params built from {@code
         * new OptimizeParams.Builder(reason)} will to used as the params for optimizing each
         * package.
         *
         * Changing the reason is not allowed. Doing so will result in {@link IllegalStateException}
         * when {@link #optimizePackages(PackageManagerLocal.FilteredSnapshot, String,
         * CancellationSignal)} is called.
         */
        void onOverrideBatchOptimizeParams(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
                @NonNull @BatchOptimizeReason String reason, @NonNull List<String> defaultPackages,
                @NonNull BatchOptimizeParams.Builder builder);
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @Nullable private final Context mContext;
        @Nullable private final PackageManagerLocal mPackageManagerLocal;
        @Nullable private final Config mConfig;

        Injector(@Nullable Context context) {
            mContext = context;
            if (context != null) {
                // We only need them on Android U and above, where a context is passed.
                mPackageManagerLocal = LocalManagerRegistry.getManager(PackageManagerLocal.class);
                mConfig = new Config();
            } else {
                mPackageManagerLocal = null;
                mConfig = null;
            }
        }

        @NonNull
        public Context getContext() {
            return Objects.requireNonNull(mContext);
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            return Objects.requireNonNull(mPackageManagerLocal);
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }

        @NonNull
        public DexOptHelper getDexOptHelper() {
            return new DexOptHelper(getContext());
        }

        @NonNull
        public Config getConfig() {
            return mConfig;
        }

        @NonNull
        public AppHibernationManager getAppHibernationManager() {
            return Objects.requireNonNull(mContext.getSystemService(AppHibernationManager.class));
        }
    }
}
