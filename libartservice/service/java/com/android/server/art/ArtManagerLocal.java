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
import static com.android.server.art.model.OptimizationStatus.DexFileOptimizationStatus;

import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.os.Binder;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.IArtd;
import com.android.server.art.model.DeleteOptions;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.GetStatusOptions;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.wrapper.AndroidPackageApi;
import com.android.server.art.wrapper.PackageDataSnapshot;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.art.wrapper.PackageState;

import java.io.FileDescriptor;
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

    public ArtManagerLocal() {
        this(new Injector());
    }

    /** @hide */
    @VisibleForTesting
    public ArtManagerLocal(Injector injector) {
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
    public int handleShellCommand(@NonNull Binder target, @NonNull FileDescriptor in,
            @NonNull FileDescriptor out, @NonNull FileDescriptor err, @NonNull String[] args) {
        return new ArtShellCommand(this, mInjector.getPackageManagerLocal())
                .exec(target, in, out, err, args);
    }

    /**
     * Deletes optimized artifacts of a package.
     *
     * @throws IllegalArgumentException if the package is not found or the options are illegal
     * @throws IllegalStateException if an internal error occurs
     *
     * @hide
     */
    public DeleteResult deleteOptimizedArtifacts(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @NonNull DeleteOptions options) {
        if (!options.isForPrimaryDex() && !options.isForSecondaryDex()) {
            throw new IllegalArgumentException("Nothing to delete");
        }

        PackageState pkgState = getPackageStateOrThrow(snapshot, packageName);
        AndroidPackageApi pkg = getPackageOrThrow(pkgState);

        try {
            long freedBytes = 0;

            if (options.isForPrimaryDex()) {
                boolean isInDalvikCache = Utils.isInDalvikCache(pkgState);
                for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                    if (!dexInfo.hasCode()) {
                        continue;
                    }
                    for (String isa : Utils.getAllIsas(pkgState)) {
                        freedBytes += mInjector.getArtd().deleteArtifacts(
                                Utils.buildArtifactsPath(dexInfo.dexPath(), isa, isInDalvikCache));
                    }
                }
            }

            if (options.isForSecondaryDex()) {
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
     * @throws IllegalArgumentException if the package is not found or the options are illegal
     * @throws IllegalStateException if an internal error occurs
     *
     * @hide
     */
    @NonNull
    public OptimizationStatus getOptimizationStatus(@NonNull PackageDataSnapshot snapshot,
            @NonNull String packageName, @NonNull GetStatusOptions options) {
        if (!options.isForPrimaryDex() && !options.isForSecondaryDex()) {
            throw new IllegalArgumentException("Nothing to check");
        }

        PackageState pkgState = getPackageStateOrThrow(snapshot, packageName);
        AndroidPackageApi pkg = getPackageOrThrow(pkgState);

        try {
            List<DexFileOptimizationStatus> statuses = new ArrayList<>();

            if (options.isForPrimaryDex()) {
                for (DetailedPrimaryDexInfo dexInfo :
                        PrimaryDexUtils.getDetailedDexInfo(pkgState, pkg)) {
                    if (!dexInfo.hasCode()) {
                        continue;
                    }
                    for (String isa : Utils.getAllIsas(pkgState)) {
                        GetOptimizationStatusResult result =
                                mInjector.getArtd().getOptimizationStatus(
                                        dexInfo.dexPath(), isa, dexInfo.classLoaderContext());
                        statuses.add(new DexFileOptimizationStatus(dexInfo.dexPath(), isa,
                                result.compilerFilter, result.compilationReason,
                                result.locationDebugString));
                    }
                }
            }

            if (options.isForSecondaryDex()) {
                // TODO(jiakaiz): Implement this.
                throw new UnsupportedOperationException(
                        "Getting optimization status of secondary dex'es is not implemented yet");
            }

            return new OptimizationStatus(statuses);
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
            throw new IllegalStateException("Unable to get package " + pkgState.getPackageName());
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
        private final PackageManagerLocal mPackageManagerLocal;

        Injector() {
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

        public PackageManagerLocal getPackageManagerLocal() {
            return mPackageManagerLocal;
        }

        public IArtd getArtd() {
            IArtd artd = IArtd.Stub.asInterface(ServiceManager.waitForService("artd"));
            if (artd == null) {
                throw new IllegalStateException("Unable to connect to artd");
            }
            return artd;
        }
    }
}
