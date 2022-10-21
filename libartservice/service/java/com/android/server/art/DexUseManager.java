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

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Binder;
import android.os.UserHandle;
import android.util.Log;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.Immutable;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.proto.DexUseProto;
import com.android.server.art.proto.Int32Value;
import com.android.server.art.proto.PackageDexUseProto;
import com.android.server.art.proto.PrimaryDexUseProto;
import com.android.server.art.proto.PrimaryDexUseRecordProto;
import com.android.server.art.proto.SecondaryDexUseProto;
import com.android.server.art.proto.SecondaryDexUseRecordProto;
import com.android.server.art.wrapper.Environment;
import com.android.server.art.wrapper.Process;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import com.google.auto.value.AutoValue;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.function.BiFunction;
import java.util.stream.Collectors;

/**
 * A singleton class that maintains the information about dex uses. This class is thread-safe.
 *
 * This class collects data sent directly by apps, and hence the data should be trusted as little as
 * possible.
 *
 * @hide
 */
public class DexUseManager {
    private static final String TAG = "DexUseManager";

    @GuardedBy("DexUseManager.class") @Nullable private static DexUseManager sInstance = null;

    @GuardedBy("this") @NonNull private DexUse mDexUse = new DexUse();

    @NonNull
    public static synchronized DexUseManager getInstance() {
        if (sInstance == null) {
            sInstance = new DexUseManager();
        }
        return sInstance;
    }

    /** Returns all entities that load the given primary dex file owned by the given package. */
    @VisibleForTesting
    @NonNull
    public synchronized Set<DexLoader> getPrimaryDexLoaders(
            @NonNull String packageName, @NonNull String dexPath) {
        PackageDexUse packageDexUse = mDexUse.mPackageDexUseByOwningPackageName.get(packageName);
        if (packageDexUse == null) {
            return Set.of();
        }
        PrimaryDexUse primaryDexUse = packageDexUse.mPrimaryDexUseByDexFile.get(dexPath);
        if (primaryDexUse == null) {
            return Set.of();
        }
        return Set.copyOf(primaryDexUse.mLoaders);
    }

    /** Returns whether a primary dex file owned by the given package is used by other apps. */
    public boolean isPrimaryDexUsedByOtherApps(
            @NonNull String packageName, @NonNull String dexPath) {
        return isUsedByOtherApps(getPrimaryDexLoaders(packageName, dexPath), packageName);
    }

    /** Returns information about all secondary dex files owned by the given package. */
    public synchronized @NonNull List<SecondaryDexInfo> getSecondaryDexInfo(
            @NonNull String packageName) {
        PackageDexUse packageDexUse = mDexUse.mPackageDexUseByOwningPackageName.get(packageName);
        if (packageDexUse == null) {
            return List.of();
        }
        var results = new ArrayList<SecondaryDexInfo>();
        for (var entry : packageDexUse.mSecondaryDexUseByDexFile.entrySet()) {
            String dexPath = entry.getKey();
            SecondaryDexUse secondaryDexUse = entry.getValue();
            if (secondaryDexUse.mRecordByLoader.isEmpty()) {
                continue;
            }
            List<String> distinctClcList =
                    secondaryDexUse.mRecordByLoader.values()
                            .stream()
                            .map(record -> Utils.assertNonEmpty(record.mClassLoaderContext))
                            .filter(clc
                                    -> !clc.equals(
                                            SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT))
                            .distinct()
                            .collect(Collectors.toList());
            String clc;
            if (distinctClcList.size() == 0) {
                clc = SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT;
            } else if (distinctClcList.size() == 1) {
                clc = distinctClcList.get(0);
            } else {
                // If there are more than one class loader contexts, we can't optimize the dex file.
                clc = SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS;
            }
            // Although we filter out unsupported CLCs above, `distinctAbiNames` and `loaders` still
            // need to take apps with unsupported CLCs into account because the vdex file is still
            // usable to them.
            Set<String> distinctAbiNames =
                    secondaryDexUse.mRecordByLoader.values()
                            .stream()
                            .map(record -> Utils.assertNonEmpty(record.mAbiName))
                            .collect(Collectors.toSet());
            Set<DexLoader> loaders = Set.copyOf(secondaryDexUse.mRecordByLoader.keySet());
            results.add(SecondaryDexInfo.create(dexPath,
                    Objects.requireNonNull(secondaryDexUse.mUserHandle), clc, distinctAbiNames,
                    loaders, isUsedByOtherApps(loaders, packageName)));
        }
        return Collections.unmodifiableList(results);
    }

    /**
     * Handles {@link
     * ArtManagerLocal#notifyDexContainersLoaded(PackageManagerLocal.FilteredSnapshot, String,
     * Map<String, String>)}.
     */
    public void addDexUse(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Map<String, String> classLoaderContextByDexContainerFile) {
        // "android" comes from `SystemServerDexLoadReporter`. ART Services doesn't need to handle
        // this case because it doesn't compile system server and system server isn't allowed to
        // load artifacts produced by ART Services.
        if (loadingPackageName.equals("android")) {
            return;
        }

        validateInputs(snapshot, loadingPackageName, classLoaderContextByDexContainerFile);

        // TODO(jiakaiz): Investigate whether it should also be considered as isolated process if
        // `Process.isSdkSandboxUid` returns true.
        boolean isolatedProcess = Process.isIsolated(Binder.getCallingUid());

        for (var entry : classLoaderContextByDexContainerFile.entrySet()) {
            String dexPath = Utils.assertNonEmpty(entry.getKey());
            String classLoaderContext = Utils.assertNonEmpty(entry.getValue());
            String owningPackageName = findOwningPackage(snapshot, loadingPackageName, dexPath,
                    DexUseManager::isOwningPackageForPrimaryDex);
            if (owningPackageName != null) {
                addPrimaryDexUse(owningPackageName, dexPath, loadingPackageName, isolatedProcess);
                continue;
            }
            owningPackageName = findOwningPackage(snapshot, loadingPackageName, dexPath,
                    DexUseManager::isOwningPackageForSecondaryDex);
            if (owningPackageName != null) {
                PackageState loadingPkgState =
                        Utils.getPackageStateOrThrow(snapshot, loadingPackageName);
                // An app is always launched with its primary ABI.
                Utils.Abi abi = Utils.getPrimaryAbi(loadingPkgState);
                addSecondaryDexUse(owningPackageName, dexPath, loadingPackageName, isolatedProcess,
                        classLoaderContext, abi.name());
                continue;
            }
            // It is expected that a dex file isn't owned by any package. For example, the dex file
            // could be a shared library jar.
        }
    }

    @Nullable
    private static String findOwningPackage(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName, @NonNull String dexPath,
            @NonNull BiFunction<PackageState, String, Boolean> predicate) {
        // Most likely, the package is loading its own dex file, so we check this first as an
        // optimization.
        PackageState loadingPkgState = Utils.getPackageStateOrThrow(snapshot, loadingPackageName);
        if (predicate.apply(loadingPkgState, dexPath)) {
            return loadingPkgState.getPackageName();
        }

        // TODO(b/246609797): The API can be improved.
        var packageStates = new ArrayList<PackageState>();
        snapshot.forAllPackageStates((pkgState) -> { packageStates.add(pkgState); });

        for (PackageState pkgState : packageStates) {
            if (predicate.apply(pkgState, dexPath)) {
                return pkgState.getPackageName();
            }
        }

        return null;
    }

    private static boolean isOwningPackageForPrimaryDex(
            @NonNull PackageState pkgState, @NonNull String dexPath) {
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
        return PrimaryDexUtils.getDexInfo(pkg).stream().anyMatch(
                dexInfo -> dexInfo.dexPath().equals(dexPath));
    }

    private static boolean isOwningPackageForSecondaryDex(
            @NonNull PackageState pkgState, @NonNull String dexPath) {
        String volumeUuid =
                new com.android.server.art.wrapper.PackageState(pkgState).getVolumeUuid();
        UserHandle handle = Binder.getCallingUserHandle();

        File ceDir = Environment.getDataUserCePackageDirectory(
                volumeUuid, handle.getIdentifier(), pkgState.getPackageName());
        if (Paths.get(dexPath).startsWith(ceDir.toPath())) {
            return true;
        }

        File deDir = Environment.getDataUserDePackageDirectory(
                volumeUuid, handle.getIdentifier(), pkgState.getPackageName());
        if (Paths.get(dexPath).startsWith(deDir.toPath())) {
            return true;
        }

        return false;
    }

    private synchronized void addPrimaryDexUse(@NonNull String owningPackageName,
            @NonNull String dexPath, @NonNull String loadingPackageName, boolean isolatedProcess) {
        mDexUse.mPackageDexUseByOwningPackageName
                .computeIfAbsent(owningPackageName, k -> new PackageDexUse())
                .mPrimaryDexUseByDexFile.computeIfAbsent(dexPath, k -> new PrimaryDexUse())
                .mLoaders.add(DexLoader.create(loadingPackageName, isolatedProcess));
    }

    private synchronized void addSecondaryDexUse(@NonNull String owningPackageName,
            @NonNull String dexPath, @NonNull String loadingPackageName, boolean isolatedProcess,
            @NonNull String classLoaderContext, @NonNull String abiName) {
        SecondaryDexUse secondaryDexUse =
                mDexUse.mPackageDexUseByOwningPackageName
                        .computeIfAbsent(owningPackageName, k -> new PackageDexUse())
                        .mSecondaryDexUseByDexFile.computeIfAbsent(
                                dexPath, k -> new SecondaryDexUse());
        secondaryDexUse.mUserHandle = Binder.getCallingUserHandle();
        SecondaryDexUseRecord record = secondaryDexUse.mRecordByLoader.computeIfAbsent(
                DexLoader.create(loadingPackageName, isolatedProcess),
                k -> new SecondaryDexUseRecord());
        record.mClassLoaderContext = classLoaderContext;
        record.mAbiName = abiName;
    }

    @VisibleForTesting
    public synchronized void clear() {
        mDexUse = new DexUse();
    }

    public @NonNull String dump() {
        var builder = DexUseProto.newBuilder();
        synchronized (this) {
            mDexUse.toProto(builder);
        }
        return builder.build().toString();
    }

    public void save(@NonNull String filename) throws IOException {
        try (OutputStream out = new FileOutputStream(filename)) {
            var builder = DexUseProto.newBuilder();
            synchronized (this) {
                mDexUse.toProto(builder);
            }
            builder.build().writeTo(out);
        }
    }

    public void load(@NonNull String filename) throws IOException {
        try (InputStream in = new FileInputStream(filename)) {
            var proto = DexUseProto.parseFrom(in);
            var dexUse = new DexUse();
            dexUse.fromProto(proto);
            synchronized (this) {
                mDexUse = dexUse;
            }
        }
    }

    private static boolean isUsedByOtherApps(
            @NonNull Set<DexLoader> loaders, @NonNull String owningPackageName) {
        // If the dex file is loaded by an isolated process of the same app, it can also be
        // considered as "used by other apps" because isolated processes are sandboxed and can only
        // read world readable files, so they need the optimized artifacts to be world readable. An
        // example of such a package is webview.
        return loaders.stream().anyMatch(loader
                -> !loader.loadingPackageName().equals(owningPackageName)
                        || loader.isolatedProcess());
    }

    private static void validateInputs(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String loadingPackageName,
            @NonNull Map<String, String> classLoaderContextByDexContainerFile) {
        if (classLoaderContextByDexContainerFile.isEmpty()) {
            throw new IllegalArgumentException("Nothing to record");
        }

        for (var entry : classLoaderContextByDexContainerFile.entrySet()) {
            Utils.assertNonEmpty(entry.getKey());
            if (!Paths.get(entry.getKey()).isAbsolute()) {
                throw new IllegalArgumentException(String.format(
                        "Dex container file path must be absolute, got '%s'", entry.getKey()));
            }
            Utils.assertNonEmpty(entry.getValue());
        }

        // TODO(b/253570365): Make the validation more strict.
    }

    /**
     * Detailed information about a secondary dex file (an APK or JAR file that an app adds to its
     * own data directory and loads dynamically).
     */
    @Immutable
    @AutoValue
    public abstract static class SecondaryDexInfo implements DetailedDexInfo {
        // Special encoding used to denote a foreign ClassLoader was found when trying to encode
        // class loader contexts for each classpath element in a ClassLoader.
        // Must be in sync with `kUnsupportedClassLoaderContextEncoding` in
        // `art/runtime/class_loader_context.h`.
        public static final String UNSUPPORTED_CLASS_LOADER_CONTEXT =
                "=UnsupportedClassLoaderContext=";

        // Special encoding used to denote that a dex file is loaded by different packages with
        // different ClassLoader's. Only for display purpose (e.g., in dumpsys). This value is not
        // written to the file, and so far only used here.
        @VisibleForTesting
        public static final String VARYING_CLASS_LOADER_CONTEXTS = "=VaryingClassLoaderContexts=";

        static SecondaryDexInfo create(@NonNull String dexPath, @NonNull UserHandle userHandle,
                @Nullable String classLoaderContext, @NonNull Set<String> abiNames,
                @NonNull Set<DexLoader> loaders, boolean isUsedByOtherApps) {
            return new AutoValue_DexUseManager_SecondaryDexInfo(dexPath, userHandle,
                    classLoaderContext, Collections.unmodifiableSet(abiNames),
                    Collections.unmodifiableSet(loaders), isUsedByOtherApps);
        }

        /** The absolute path to the dex file within the user's app data directory. */
        public abstract @NonNull String dexPath();

        /**
         * The {@link UserHandle} that represents the human user who owns and loads the dex file. A
         * secondary dex file belongs to a specific human user, and only that user can load it.
         */
        public abstract @NonNull UserHandle userHandle();

        /**
         * A string describing the structure of the class loader that the dex file is loaded with,
         * or {@link #UNSUPPORTED_CLASS_LOADER_CONTEXT} or {@link #VARYING_CLASS_LOADER_CONTEXTS}.
         */
        public abstract @NonNull String classLoaderContext();

        /** The set of ABIs of the dex file is loaded with. */
        public abstract @NonNull Set<String> abiNames();

        /** The set of entities that load the dex file. */
        public abstract @NonNull Set<DexLoader> loaders();

        /** Returns whether the dex file is used by apps other than the app that owns it. */
        public abstract boolean isUsedByOtherApps();

        /**
         * Returns true if the class loader context is suitable for compilation.
         */
        public boolean isClassLoaderContextValid() {
            return !classLoaderContext().equals(UNSUPPORTED_CLASS_LOADER_CONTEXT)
                    && !classLoaderContext().equals(VARYING_CLASS_LOADER_CONTEXTS);
        }
    }

    private static class DexUse {
        @NonNull Map<String, PackageDexUse> mPackageDexUseByOwningPackageName = new HashMap<>();

        void toProto(@NonNull DexUseProto.Builder builder) {
            for (var entry : mPackageDexUseByOwningPackageName.entrySet()) {
                var packageBuilder =
                        PackageDexUseProto.newBuilder().setOwningPackageName(entry.getKey());
                entry.getValue().toProto(packageBuilder);
                builder.addPackageDexUse(packageBuilder);
            }
        }

        void fromProto(@NonNull DexUseProto proto) {
            for (PackageDexUseProto packageProto : proto.getPackageDexUseList()) {
                var packageDexUse = new PackageDexUse();
                packageDexUse.fromProto(packageProto);
                mPackageDexUseByOwningPackageName.put(
                        Utils.assertNonEmpty(packageProto.getOwningPackageName()), packageDexUse);
            }
        }
    }

    private static class PackageDexUse {
        /**
         * The keys are absolute paths to primary dex files of the owning package (the base APK and
         * split APKs).
         */
        @NonNull Map<String, PrimaryDexUse> mPrimaryDexUseByDexFile = new HashMap<>();

        /**
         * The keys are absolute paths to secondary dex files of the owning package (the APKs and
         * JARs in CE and DE directories).
         */
        @NonNull Map<String, SecondaryDexUse> mSecondaryDexUseByDexFile = new HashMap<>();

        void toProto(@NonNull PackageDexUseProto.Builder builder) {
            for (var entry : mPrimaryDexUseByDexFile.entrySet()) {
                var primaryBuilder = PrimaryDexUseProto.newBuilder().setDexFile(entry.getKey());
                entry.getValue().toProto(primaryBuilder);
                builder.addPrimaryDexUse(primaryBuilder);
            }
            for (var entry : mSecondaryDexUseByDexFile.entrySet()) {
                var secondaryBuilder = SecondaryDexUseProto.newBuilder().setDexFile(entry.getKey());
                entry.getValue().toProto(secondaryBuilder);
                builder.addSecondaryDexUse(secondaryBuilder);
            }
        }

        void fromProto(@NonNull PackageDexUseProto proto) {
            for (PrimaryDexUseProto primaryProto : proto.getPrimaryDexUseList()) {
                var primaryDexUse = new PrimaryDexUse();
                primaryDexUse.fromProto(primaryProto);
                mPrimaryDexUseByDexFile.put(
                        Utils.assertNonEmpty(primaryProto.getDexFile()), primaryDexUse);
            }
            for (SecondaryDexUseProto secondaryProto : proto.getSecondaryDexUseList()) {
                var secondaryDexUse = new SecondaryDexUse();
                secondaryDexUse.fromProto(secondaryProto);
                mSecondaryDexUseByDexFile.put(
                        Utils.assertNonEmpty(secondaryProto.getDexFile()), secondaryDexUse);
            }
        }
    }

    private static class PrimaryDexUse {
        @NonNull Set<DexLoader> mLoaders = new HashSet<>();

        void toProto(@NonNull PrimaryDexUseProto.Builder builder) {
            for (DexLoader loader : mLoaders) {
                builder.addRecord(PrimaryDexUseRecordProto.newBuilder()
                                          .setLoadingPackageName(loader.loadingPackageName())
                                          .setIsolatedProcess(loader.isolatedProcess()));
            }
        }

        void fromProto(@NonNull PrimaryDexUseProto proto) {
            for (PrimaryDexUseRecordProto recordProto : proto.getRecordList()) {
                mLoaders.add(
                        DexLoader.create(Utils.assertNonEmpty(recordProto.getLoadingPackageName()),
                                recordProto.getIsolatedProcess()));
            }
        }
    }

    private static class SecondaryDexUse {
        @Nullable UserHandle mUserHandle = null;
        @NonNull Map<DexLoader, SecondaryDexUseRecord> mRecordByLoader = new HashMap<>();

        void toProto(@NonNull SecondaryDexUseProto.Builder builder) {
            builder.setUserId(Int32Value.newBuilder().setValue(mUserHandle.getIdentifier()));
            for (var entry : mRecordByLoader.entrySet()) {
                var recordBuilder =
                        SecondaryDexUseRecordProto.newBuilder()
                                .setLoadingPackageName(entry.getKey().loadingPackageName())
                                .setIsolatedProcess(entry.getKey().isolatedProcess());
                entry.getValue().toProto(recordBuilder);
                builder.addRecord(recordBuilder);
            }
        }

        void fromProto(@NonNull SecondaryDexUseProto proto) {
            Utils.check(proto.hasUserId());
            mUserHandle = UserHandle.of(proto.getUserId().getValue());
            for (SecondaryDexUseRecordProto recordProto : proto.getRecordList()) {
                var record = new SecondaryDexUseRecord();
                record.fromProto(recordProto);
                mRecordByLoader.put(
                        DexLoader.create(Utils.assertNonEmpty(recordProto.getLoadingPackageName()),
                                recordProto.getIsolatedProcess()),
                        record);
            }
        }
    }

    /** Represents an entity that loads a dex file. */
    @Immutable
    @AutoValue
    public abstract static class DexLoader {
        static DexLoader create(@NonNull String loadingPackageName, boolean isolatedProcess) {
            return new AutoValue_DexUseManager_DexLoader(loadingPackageName, isolatedProcess);
        }

        abstract @NonNull String loadingPackageName();

        /** @see Process#isIsolated(int) */
        abstract boolean isolatedProcess();
    }

    private static class SecondaryDexUseRecord {
        // An app constructs their own class loader to load a secondary dex file, so only itself
        // knows the class loader context. Therefore, we need to record the class loader context
        // reported by the app.
        @Nullable String mClassLoaderContext = null;
        @Nullable String mAbiName = null;

        void toProto(@NonNull SecondaryDexUseRecordProto.Builder builder) {
            builder.setClassLoaderContext(mClassLoaderContext).setAbiName(mAbiName);
        }

        void fromProto(@NonNull SecondaryDexUseRecordProto proto) {
            mClassLoaderContext = Utils.assertNonEmpty(proto.getClassLoaderContext());
            mAbiName = Utils.assertNonEmpty(proto.getAbiName());
        }
    }
}
