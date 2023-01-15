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

import static com.android.server.art.DexUseManagerLocal.DexLoader;
import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;
import static com.android.server.art.model.DexoptStatus.DexContainerFileDexoptStatus;

import android.annotation.NonNull;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.VMRuntime;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;

/**
 * A helper class to handle dump.
 *
 * @hide
 */
public class DumpHelper {
    @NonNull private final Injector mInjector;

    public DumpHelper(@NonNull ArtManagerLocal artManagerLocal) {
        this(new Injector(artManagerLocal));
    }

    @VisibleForTesting
    public DumpHelper(@NonNull Injector injector) {
        mInjector = injector;
    }

    /** Handles {@link ArtManagerLocal#dump(PrintWriter, PackageManagerLocal.FilteredSnapshot)}. */
    public void dump(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        for (PackageState pkgState : snapshot.getPackageStates().values()) {
            dumpPackage(pw, snapshot, pkgState);
        }
    }

    /**
     * Handles {@link
     * ArtManagerLocal#dumpPackage(PrintWriter, PackageManagerLocal.FilteredSnapshot, String)}.
     */
    public void dumpPackage(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull PackageState pkgState) {
        // An APEX has a uid of -1.
        // TODO(b/256637152): Consider using `isApex` instead.
        if (pkgState.getAppId() <= 0 || pkgState.getAndroidPackage() == null) {
            return;
        }

        var ipw = new IndentingPrintWriter(pw);

        String packageName = pkgState.getPackageName();
        ipw.printf("[%s]\n", packageName);

        List<DexContainerFileDexoptStatus> statuses =
                mInjector.getArtManagerLocal()
                        .getDexoptStatus(snapshot, packageName)
                        .getDexContainerFileDexoptStatuses();
        Map<String, SecondaryDexInfo> secondaryDexInfoByDexPath =
                mInjector.getDexUseManager()
                        .getSecondaryDexInfo(packageName)
                        .stream()
                        .collect(Collectors.toMap(SecondaryDexInfo::dexPath, Function.identity()));

        // Use LinkedHashMap to keep the order.
        var primaryStatusesByDexPath =
                new LinkedHashMap<String, List<DexContainerFileDexoptStatus>>();
        var secondaryStatusesByDexPath =
                new LinkedHashMap<String, List<DexContainerFileDexoptStatus>>();
        for (DexContainerFileDexoptStatus fileStatus : statuses) {
            if (fileStatus.isPrimaryDex()) {
                primaryStatusesByDexPath
                        .computeIfAbsent(fileStatus.getDexContainerFile(), k -> new ArrayList<>())
                        .add(fileStatus);
            } else if (secondaryDexInfoByDexPath.containsKey(fileStatus.getDexContainerFile())) {
                // The condition above is false only if a change occurs between
                // `getDexoptStatus` and `getSecondaryDexInfo`, which is an edge case.
                secondaryStatusesByDexPath
                        .computeIfAbsent(fileStatus.getDexContainerFile(), k -> new ArrayList<>())
                        .add(fileStatus);
            }
        }

        ipw.increaseIndent();
        for (List<DexContainerFileDexoptStatus> fileStatuses : primaryStatusesByDexPath.values()) {
            dumpPrimaryDex(ipw, fileStatuses, packageName);
        }
        if (!secondaryStatusesByDexPath.isEmpty()) {
            ipw.println("known secondary dex files:");
            ipw.increaseIndent();
            for (Map.Entry<String, List<DexContainerFileDexoptStatus>> entry :
                    secondaryStatusesByDexPath.entrySet()) {
                dumpSecondaryDex(ipw, entry.getValue(), packageName,
                        secondaryDexInfoByDexPath.get(entry.getKey()));
            }
            ipw.decreaseIndent();
        }
        ipw.decreaseIndent();
    }

    private void dumpPrimaryDex(@NonNull IndentingPrintWriter ipw,
            List<DexContainerFileDexoptStatus> fileStatuses, @NonNull String packageName) {
        String dexPath = fileStatuses.get(0).getDexContainerFile();
        ipw.printf("path: %s\n", dexPath);
        ipw.increaseIndent();
        dumpFileStatuses(ipw, fileStatuses);
        dumpUsedByOtherApps(ipw,
                mInjector.getDexUseManager().getPrimaryDexLoaders(packageName, dexPath),
                packageName);
        ipw.decreaseIndent();
    }

    private void dumpSecondaryDex(@NonNull IndentingPrintWriter ipw,
            List<DexContainerFileDexoptStatus> fileStatuses, @NonNull String packageName,
            @NonNull SecondaryDexInfo info) {
        String dexPath = fileStatuses.get(0).getDexContainerFile();
        ipw.println(dexPath);
        ipw.increaseIndent();
        dumpFileStatuses(ipw, fileStatuses);
        ipw.printf("class loader context: %s\n", info.displayClassLoaderContext());
        dumpUsedByOtherApps(ipw, info.loaders(), packageName);
        ipw.decreaseIndent();
    }

    private void dumpFileStatuses(
            @NonNull IndentingPrintWriter ipw, List<DexContainerFileDexoptStatus> fileStatuses) {
        for (DexContainerFileDexoptStatus fileStatus : fileStatuses) {
            ipw.printf("%s: [status=%s] [reason=%s]\n",
                    VMRuntime.getInstructionSet(fileStatus.getAbi()),
                    fileStatus.getCompilerFilter(), fileStatus.getCompilationReason());
        }
    }

    private void dumpUsedByOtherApps(@NonNull IndentingPrintWriter ipw,
            @NonNull Set<DexLoader> dexLoaders, @NonNull String packageName) {
        List<DexLoader> otherApps =
                dexLoaders.stream()
                        .filter(loader -> DexUseManagerLocal.isLoaderOtherApp(loader, packageName))
                        .collect(Collectors.toList());
        if (!otherApps.isEmpty()) {
            ipw.printf("used by other apps: [%s]\n",
                    otherApps.stream()
                            .map(loader
                                    -> loader.loadingPackageName()
                                            + (loader.isolatedProcess() ? " (isolated)" : ""))
                            .collect(Collectors.joining(", ")));
        }
    }

    /** Injector pattern for testing purpose. */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final ArtManagerLocal mArtManagerLocal;

        Injector(@NonNull ArtManagerLocal artManagerLocal) {
            mArtManagerLocal = artManagerLocal;
        }

        @NonNull
        public ArtManagerLocal getArtManagerLocal() {
            return mArtManagerLocal;
        }

        @NonNull
        public DexUseManagerLocal getDexUseManager() {
            return Objects.requireNonNull(
                    LocalManagerRegistry.getManager(DexUseManagerLocal.class));
        }
    }
}
