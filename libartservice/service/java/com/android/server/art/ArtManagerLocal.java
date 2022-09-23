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
import static com.android.server.art.Utils.Abi;
import static com.android.server.art.model.ArtFlags.DeleteFlags;
import static com.android.server.art.model.ArtFlags.GetStatusFlags;
import static com.android.server.art.model.OptimizationStatus.DexContainerFileOptimizationStatus;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.SystemApi;
import android.content.Context;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.util.Log;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.IArtd;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.art.wrapper.PackageState;
import com.android.server.pm.snapshot.PackageDataSnapshot;

import java.util.ArrayList;
import java.util.List;

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
            @NonNull PackageDataSnapshot snapshot, @NonNull String packageName) {
        return deleteOptimizedArtifacts(snapshot, packageName, ArtFlags.defaultDeleteFlags());
    }

    /**
     * Same as above, but allows to specify flags.
     *
     * @see #deleteOptimizedArtifacts(PackageDataSnapshot, String)
     */
    @NonNull
    public DeleteResult deleteOptimizedArtifacts(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @DeleteFlags int flags) {
        if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to delete");
        }

        PackageState pkgState = getPackageStateOrThrow(snapshot, packageName);
        AndroidPackageApi pkg = getPackageOrThrow(pkgState);

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
            @NonNull PackageDataSnapshot snapshot, @NonNull String packageName) {
        return getOptimizationStatus(snapshot, packageName, ArtFlags.defaultGetStatusFlags());
    }

    /**
     * Same as above, but allows to specify flags.
     *
     * @see #getOptimizationStatus(PackageDataSnapshot, String)
     */
    @NonNull
    public OptimizationStatus getOptimizationStatus(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @GetStatusFlags int flags) {
        if ((flags & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (flags & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to check");
        }

        PackageState pkgState = getPackageStateOrThrow(snapshot, packageName);
        AndroidPackageApi pkg = getPackageOrThrow(pkgState);

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
    public OptimizeResult optimizePackage(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @NonNull OptimizeParams params) {
        var cancellationSignal = new CancellationSignal();
        return optimizePackage(snapshot, packageName, params, cancellationSignal);
    }

    /**
     * Same as above, but supports cancellation.
     *
     * @see #optimizePackage(PackageDataSnapshot, String, OptimizeParams)
     */
    @NonNull
    public OptimizeResult optimizePackage(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @NonNull OptimizeParams params,
            @NonNull CancellationSignal cancellationSignal) {
        if ((params.getFlags() & ArtFlags.FLAG_FOR_PRIMARY_DEX) == 0
                && (params.getFlags() & ArtFlags.FLAG_FOR_SECONDARY_DEX) == 0) {
            throw new IllegalArgumentException("Nothing to optimize");
        }

        PackageState pkgState = getPackageStateOrThrow(snapshot, packageName);
        AndroidPackageApi pkg = getPackageOrThrow(pkgState);

        try {
            return mInjector.getDexOptHelper().dexopt(
                    snapshot, pkgState, pkg, params, cancellationSignal);
        } catch (RemoteException e) {
            throw new IllegalStateException("An error occurred when calling artd", e);
        }
    }

    private PackageState getPackageStateOrThrow(
            @NonNull PackageDataSnapshot snapshot, @NonNull String packageName) {
        PackageState pkgState = mInjector.getPackageManagerLocal().getPackageState(
                snapshot, Binder.getCallingUid(), packageName);
        if (pkgState == null) {
            throw new IllegalArgumentException("Package not found: " + packageName);
        }
        return pkgState;
    }

    private AndroidPackageApi getPackageOrThrow(@NonNull PackageState pkgState) {
        AndroidPackageApi pkg = pkgState.getAndroidPackage();
        if (pkg == null) {
            throw new IllegalArgumentException(
                    "Unable to get package " + pkgState.getPackageName());
        }
        return pkg;
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

        Injector(@Nullable Context context) {
            mContext = context;

            PackageManagerLocal packageManagerLocal = null;
            try {
                packageManagerLocal = PackageManagerLocal.getInstance();
            } catch (Exception e) {
                // This is not a serious error. The reflection-based approach can be broken in some
                // cases. This is fine because ART services is under development and no one depends
                // on it.
                // TODO(b/177273468): Make this a serious error when we switch to using the real
                // APIs.
                Log.w(TAG, "Unable to get fake PackageManagerLocal", e);
            }
            mPackageManagerLocal = packageManagerLocal;
        }

        @NonNull
        public Context getContext() {
            if (mContext == null) {
                throw new IllegalStateException("Context is null");
            }
            return mContext;
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            if (mPackageManagerLocal == null) {
                throw new IllegalStateException("PackageManagerLocal is null");
            }
            return mPackageManagerLocal;
        }

        @NonNull
        public IArtd getArtd() {
            return Utils.getArtd();
        }

        @NonNull
        public DexOptHelper getDexOptHelper() {
            return new DexOptHelper(getContext());
        }
    }
}
