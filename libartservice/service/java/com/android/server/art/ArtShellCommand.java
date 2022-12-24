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

import static android.os.ParcelFileDescriptor.AutoCloseInputStream;

import static com.android.server.art.ArtManagerLocal.SnapshotProfileException;
import static com.android.server.art.PrimaryDexUtils.PrimaryDexInfo;
import static com.android.server.art.model.ArtFlags.OptimizeFlags;
import static com.android.server.art.model.OptimizationStatus.DexContainerFileOptimizationStatus;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.OptimizeStatus;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.os.Binder;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.system.StructStat;
import android.util.Log;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.modules.utils.BasicShellCommandHandler;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import libcore.io.Streams;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Collectors;

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
public final class ArtShellCommand extends BasicShellCommandHandler {
    private static final String TAG = "ArtShellCommand";

    /** The default location for profile dumps. */
    private final static String PROFILE_DEBUG_LOCATION = "/data/misc/profman";

    private final ArtManagerLocal mArtManagerLocal;
    private final PackageManagerLocal mPackageManagerLocal;
    private final DexUseManagerLocal mDexUseManager;

    @GuardedBy("sCancellationSignalMap")
    @NonNull
    private static final Map<String, CancellationSignal> sCancellationSignalMap = new HashMap<>();

    public ArtShellCommand(@NonNull ArtManagerLocal artManagerLocal,
            @NonNull PackageManagerLocal packageManagerLocal,
            @NonNull DexUseManagerLocal dexUseManager) {
        mArtManagerLocal = artManagerLocal;
        mPackageManagerLocal = packageManagerLocal;
        mDexUseManager = dexUseManager;
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    @Override
    public int onCommand(String cmd) {
        enforceRoot();
        PrintWriter pw = getOutPrintWriter();
        try (var snapshot = mPackageManagerLocal.withFilteredSnapshot()) {
            switch (cmd) {
                case "delete-optimized-artifacts": {
                    DeleteResult result = mArtManagerLocal.deleteOptimizedArtifacts(
                            snapshot, getNextArgRequired(), ArtFlags.defaultDeleteFlags());
                    pw.printf("Freed %d bytes\n", result.getFreedBytes());
                    return 0;
                }
                case "get-optimization-status": {
                    OptimizationStatus optimizationStatus = mArtManagerLocal.getOptimizationStatus(
                            snapshot, getNextArgRequired(), ArtFlags.defaultGetStatusFlags());
                    pw.println(optimizationStatus);
                    return 0;
                }
                case "optimize-package": {
                    var paramsBuilder = new OptimizeParams.Builder("cmdline");
                    String opt;
                    @OptimizeFlags int scopeFlags = 0;
                    boolean forSingleSplit = false;
                    boolean reset = false;
                    while ((opt = getNextOption()) != null) {
                        switch (opt) {
                            case "-m":
                                paramsBuilder.setCompilerFilter(getNextArgRequired());
                                break;
                            case "-f":
                                paramsBuilder.setFlags(ArtFlags.FLAG_FORCE, ArtFlags.FLAG_FORCE);
                                break;
                            case "--primary-dex":
                                scopeFlags |= ArtFlags.FLAG_FOR_PRIMARY_DEX;
                                break;
                            case "--secondary-dex":
                                scopeFlags |= ArtFlags.FLAG_FOR_SECONDARY_DEX;
                                break;
                            case "--include-dependencies":
                                scopeFlags |= ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES;
                                break;
                            case "--split":
                                String splitName = getNextArgRequired();
                                forSingleSplit = true;
                                paramsBuilder
                                        .setFlags(ArtFlags.FLAG_FOR_SINGLE_SPLIT,
                                                ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                                        .setSplitName(!splitName.isEmpty() ? splitName : null);
                                break;
                            case "--reset":
                                reset = true;
                                break;
                            default:
                                pw.println("Error: Unknown option: " + opt);
                                return 1;
                        }
                    }
                    if (forSingleSplit) {
                        if (scopeFlags != 0) {
                            pw.println("'--primary-dex', '--secondary-dex', and "
                                    + "'--include-dependencies' must not be set when '--split' is "
                                    + "set.");
                            return 1;
                        }
                        scopeFlags = ArtFlags.FLAG_FOR_PRIMARY_DEX;
                    }
                    if (scopeFlags != 0) {
                        paramsBuilder.setFlags(scopeFlags,
                                ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SECONDARY_DEX
                                        | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES);
                    }

                    OptimizeResult result;
                    try (var signal = new WithCancellationSignal(pw)) {
                        if (reset) {
                            result = mArtManagerLocal.resetOptimizationStatus(
                                    snapshot, getNextArgRequired(), signal.get());
                        } else {
                            result = mArtManagerLocal.optimizePackage(snapshot,
                                    getNextArgRequired(), paramsBuilder.build(), signal.get());
                        }
                    }
                    printOptimizeResult(pw, result);
                    return 0;
                }
                case "optimize-packages": {
                    OptimizeResult result;
                    ExecutorService executor = Executors.newSingleThreadExecutor();
                    try (var signal = new WithCancellationSignal(pw)) {
                        result = mArtManagerLocal.optimizePackages(snapshot, getNextArgRequired(),
                                signal.get(), executor, progress -> {
                                    pw.println(String.format(
                                            "Optimizing apps: %d%%", progress.getPercentage()));
                                    pw.flush();
                                });
                        Utils.executeAndWait(executor, () -> printOptimizeResult(pw, result));
                    } finally {
                        executor.shutdown();
                    }
                    return 0;
                }
                case "cancel": {
                    String jobId = getNextArgRequired();
                    CancellationSignal signal;
                    synchronized (sCancellationSignalMap) {
                        signal = sCancellationSignalMap.getOrDefault(jobId, null);
                    }
                    if (signal == null) {
                        pw.println("Job not found");
                        return 1;
                    }
                    signal.cancel();
                    pw.println("Job cancelled");
                    return 0;
                }
                case "dex-use-notify": {
                    mDexUseManager.notifyDexContainersLoaded(snapshot, getNextArgRequired(),
                            Map.of(getNextArgRequired(), getNextArgRequired()));
                    return 0;
                }
                case "dex-use-get-primary": {
                    String packageName = getNextArgRequired();
                    String dexPath = getNextArgRequired();
                    pw.println("Loaders: "
                            + mDexUseManager.getPrimaryDexLoaders(packageName, dexPath)
                                      .stream()
                                      .map(Object::toString)
                                      .collect(Collectors.joining(", ")));
                    pw.println("Is used by other apps: "
                            + mDexUseManager.isPrimaryDexUsedByOtherApps(packageName, dexPath));
                    return 0;
                }
                case "dex-use-get-secondary": {
                    for (DexUseManagerLocal.SecondaryDexInfo info :
                            mDexUseManager.getSecondaryDexInfo(getNextArgRequired())) {
                        pw.println(info);
                    }
                    return 0;
                }
                case "dex-use-dump": {
                    pw.println(mDexUseManager.dump());
                    return 0;
                }
                case "bg-dexopt-job": {
                    String opt = getNextOption();
                    if (opt == null) {
                        mArtManagerLocal.startBackgroundDexoptJob();
                        return 0;
                    }
                    switch (opt) {
                        case "--cancel": {
                            mArtManagerLocal.cancelBackgroundDexoptJob();
                            return 0;
                        }
                        case "--enable": {
                            // This operation requires the uid to be "system" (1000).
                            long identityToken = Binder.clearCallingIdentity();
                            try {
                                mArtManagerLocal.scheduleBackgroundDexoptJob();
                            } finally {
                                Binder.restoreCallingIdentity(identityToken);
                            }
                            return 0;
                        }
                        case "--disable": {
                            // This operation requires the uid to be "system" (1000).
                            long identityToken = Binder.clearCallingIdentity();
                            try {
                                mArtManagerLocal.unscheduleBackgroundDexoptJob();
                            } finally {
                                Binder.restoreCallingIdentity(identityToken);
                            }
                            return 0;
                        }
                        default:
                            pw.println("Error: Unknown option: " + opt);
                            return 1;
                    }
                }
                case "snapshot-app-profile": {
                    String packageName = getNextArgRequired();
                    String splitName = getNextArg();
                    String outputRelativePath = String.format("%s%s.prof", packageName,
                            splitName != null ? String.format("-split_%s.apk", splitName) : "");
                    ParcelFileDescriptor fd;
                    try {
                        fd = mArtManagerLocal.snapshotAppProfile(snapshot, packageName, splitName);
                    } catch (SnapshotProfileException e) {
                        throw new RuntimeException(e);
                    }
                    writeProfileFdContentsToFile(fd, outputRelativePath);
                    return 0;
                }
                case "snapshot-boot-image-profile": {
                    String outputRelativePath = "android.prof";
                    ParcelFileDescriptor fd;
                    try {
                        fd = mArtManagerLocal.snapshotBootImageProfile(snapshot);
                    } catch (SnapshotProfileException e) {
                        throw new RuntimeException(e);
                    }
                    writeProfileFdContentsToFile(fd, outputRelativePath);
                    return 0;
                }
                case "dump-profiles": {
                    boolean dumpClassesAndMethods = false;
                    String opt;
                    while ((opt = getNextOption()) != null) {
                        switch (opt) {
                            case "--dump-classes-and-methods": {
                                dumpClassesAndMethods = true;
                                break;
                            }
                            default:
                                pw.println("Error: Unknown option: " + opt);
                                return 1;
                        }
                    }
                    String packageName = getNextArgRequired();
                    PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
                    AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
                    for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                        if (!dexInfo.hasCode()) {
                            continue;
                        }
                        String profileName = PrimaryDexUtils.getProfileName(dexInfo.splitName());
                        // The path is intentionally inconsistent with the one for
                        // "snapshot-app-profile". The is to match the behavior of the legacy PM
                        // shell command.
                        String outputRelativePath =
                                String.format("%s-%s.prof.txt", packageName, profileName);
                        ParcelFileDescriptor fd;
                        try {
                            fd = mArtManagerLocal.dumpAppProfile(snapshot, packageName,
                                    dexInfo.splitName(), dumpClassesAndMethods);
                        } catch (SnapshotProfileException e) {
                            throw new RuntimeException(e);
                        }
                        writeProfileFdContentsToFile(fd, outputRelativePath);
                    }
                    return 0;
                }
                default:
                    // Handles empty, help, and invalid commands.
                    return handleDefaultCommands(cmd);
            }
        }
    }

    @Override
    public void onHelp() {
        final PrintWriter pw = getOutPrintWriter();
        pw.println("ART service commands.");
        pw.println("Note: The commands are used for internal debugging purposes only. There are no "
                + "stability guarantees for them.");
        pw.println("");
        pw.println("Usage: cmd package art [ARGS]...");
        pw.println("");
        pw.println("Supported commands:");
        pw.println("  help or -h");
        pw.println("    Print this help text.");
        pw.println("  delete-optimized-artifacts PACKAGE_NAME");
        pw.println("    Delete the optimized artifacts of both primary dex files and secondary");
        pw.println("    dex files of a package.");
        pw.println("  get-optimization-status PACKAGE_NAME");
        pw.println("    Print the optimization status of both primary dex files and secondary dex");
        pw.println("    files of a package.");
        pw.println("  optimize-package [-m COMPILER_FILTER] [-f] [--primary-dex]");
        pw.println("      [--secondary-dex] [--include-dependencies] [--split SPLIT_NAME]");
        pw.println("      PACKAGE_NAME");
        pw.println("    Optimize a package.");
        pw.println("    If none of '--primary-dex', '--secondary-dex', and");
        pw.println("    '--include-dependencies' is set, the command optimizes all of them.");
        pw.println("    The command prints a job ID, which can be used to cancel the job using");
        pw.println("    the 'cancel' command.");
        pw.println("    Options:");
        pw.println("      -m Set the compiler filter.");
        pw.println("      -f Force compilation.");
        pw.println("      --primary-dex Optimize primary dex files.");
        pw.println("      --secondary-dex Optimize secondary dex files.");
        pw.println("      --include-dependencies Include dependencies.");
        pw.println("      --split SPLIT_NAME Only optimize the given split. If SPLIT_NAME is an");
        pw.println("        empty string, only optimize the base APK. When this option is set,");
        pw.println("        '--primary-dex', '--secondary-dex', and '--include-dependencies' must");
        pw.println("        not be set.");
        pw.println("      --reset Reset the optimization state of the package as if the package");
        pw.println("        is newly installed.");
        pw.println("        More specifically, it clears reference profiles, current profiles,");
        pw.println("        and any code compiled from those local profiles. If there is an");
        pw.println("        external profile (e.g., a cloud profile), the code compiled from that");
        pw.println("        profile will be kept.");
        pw.println("        For secondary dex files, it also clears all optimized artifacts.");
        pw.println("        When this flag is set, all the other flags are ignored.");
        pw.println("  optimize-packages REASON");
        pw.println("    Run batch optimization for the given reason.");
        pw.println("    The command prints a job ID, which can be used to cancel the job using the"
                + "'cancel' command.");
        pw.println("  cancel JOB_ID");
        pw.println("    Cancel a job.");
        pw.println("  dex-use-notify PACKAGE_NAME DEX_PATH CLASS_LOADER_CONTEXT");
        pw.println("    Notify that a dex file is loaded with the given class loader context by");
        pw.println("    the given package.");
        pw.println("  dex-use-get-primary PACKAGE_NAME DEX_PATH");
        pw.println("    Print the dex use information about a primary dex file owned by the given");
        pw.println("    package.");
        pw.println("  dex-use-get-secondary PACKAGE_NAME");
        pw.println("    Print the dex use information about all secondary dex files owned by the");
        pw.println("    given package.");
        pw.println("  dex-use-dump");
        pw.println("    Print all dex use information in textproto format.");
        pw.println("  bg-dexopt-job [--cancel | --disable | --enable]");
        pw.println("    Control the background dexopt job.");
        pw.println("    Without flags, it starts a background dexopt job immediately. It does");
        pw.println("      nothing if a job is already started either automatically by the system");
        pw.println("      or through this command. This command is not blocking.");
        pw.println("    Options:");
        pw.println("      --cancel Cancel any currently running background dexopt job");
        pw.println("        immediately. This cancels jobs started either automatically by the");
        pw.println("        system or through this command. This command is not blocking.");
        pw.println("      --disable: Disable the background dexopt job from being started by the");
        pw.println("        job scheduler. If a job is already started by the job scheduler and");
        pw.println("        is running, it will be cancelled immediately. Does not affect");
        pw.println("        jobs started through this command or by the system in other ways.");
        pw.println("        This state will be lost when the system_server process exits.");
        pw.println("      --enable: Enable the background dexopt job to be started by the job");
        pw.println("        scheduler again, if previously disabled by --disable.");
        pw.println("  snapshot-app-profile PACKAGE_NAME [SPLIT_NAME]");
        pw.println("    Snapshot the profile of the given app and save it to");
        pw.println("    '" + PROFILE_DEBUG_LOCATION + "'.");
        pw.println("    If SPLIT_NAME is empty, the command is for the base APK, and the output");
        pw.println("    filename is 'PACKAGE_NAME.prof'. Otherwise, the command is for the given");
        pw.println("    split, and the output filename is");
        pw.println("    'PACKAGE_NAME-split_SPLIT_NAME.apk.prof'.");
        pw.println("  snapshot-boot-image-profile");
        pw.println("    Snapshot the boot image profile and save it to");
        pw.println("    '" + PROFILE_DEBUG_LOCATION + "/android.prof'.");
        pw.println("  dump-profiles [--dump-classes-and-methods] PACKAGE_NAME");
        pw.println("    Dump the profiles of the given app in text format and save the outputs to");
        pw.println("    '" + PROFILE_DEBUG_LOCATION + "'.");
        pw.println("    The profile of the base APK is dumped to 'PACKAGE_NAME-primary.prof.txt'");
        pw.println("    The profile of a split APK is dumped to");
        pw.println("    'PACKAGE_NAME-SPLIT_NAME.split.prof.txt'");
    }

    private void enforceRoot() {
        final int uid = Binder.getCallingUid();
        if (uid != Process.ROOT_UID) {
            throw new SecurityException("ART service shell commands need root access");
        }
    }

    @NonNull
    private String optimizeStatusToString(@OptimizeStatus int status) {
        switch (status) {
            case OptimizeResult.OPTIMIZE_SKIPPED:
                return "SKIPPED";
            case OptimizeResult.OPTIMIZE_PERFORMED:
                return "PERFORMED";
            case OptimizeResult.OPTIMIZE_FAILED:
                return "FAILED";
            case OptimizeResult.OPTIMIZE_CANCELLED:
                return "CANCELLED";
        }
        throw new IllegalArgumentException("Unknown optimize status " + status);
    }

    private void printOptimizeResult(@NonNull PrintWriter pw, @NonNull OptimizeResult result) {
        pw.println(optimizeStatusToString(result.getFinalStatus()));
        for (PackageOptimizeResult packageResult : result.getPackageOptimizeResults()) {
            pw.printf("[%s]\n", packageResult.getPackageName());
            for (DexContainerFileOptimizeResult fileResult :
                    packageResult.getDexContainerFileOptimizeResults()) {
                pw.printf("dexContainerFile = %s, isPrimaryAbi = %b, abi = %s, "
                                + "compilerFilter = %s, status = %s, "
                                + "dex2oatWallTimeMillis = %d, dex2oatCpuTimeMillis = %d, "
                                + "sizeBytes = %d, sizeBeforeBytes = %d\n",
                        fileResult.getDexContainerFile(), fileResult.isPrimaryAbi(),
                        fileResult.getAbi(), fileResult.getActualCompilerFilter(),
                        optimizeStatusToString(fileResult.getStatus()),
                        fileResult.getDex2oatWallTimeMillis(), fileResult.getDex2oatCpuTimeMillis(),
                        fileResult.getSizeBytes(), fileResult.getSizeBeforeBytes());
            }
        }
    }

    private void writeProfileFdContentsToFile(
            @NonNull ParcelFileDescriptor fd, @NonNull String outputRelativePath) {
        try {
            StructStat st = Os.stat(PROFILE_DEBUG_LOCATION);
            if (st.st_uid != Process.SYSTEM_UID || st.st_gid != Process.SHELL_UID
                    || (st.st_mode & 0007) != 0) {
                throw new RuntimeException(
                        String.format("%s has wrong permissions: uid=%d, gid=%d, mode=%o",
                                PROFILE_DEBUG_LOCATION, st.st_uid, st.st_gid, st.st_mode));
            }
        } catch (ErrnoException e) {
            throw new RuntimeException("Unable to stat " + PROFILE_DEBUG_LOCATION, e);
        }
        Path outputPath = Paths.get(PROFILE_DEBUG_LOCATION, outputRelativePath);
        try (InputStream inputStream = new AutoCloseInputStream(fd);
                FileOutputStream outputStream = new FileOutputStream(outputPath.toFile())) {
            // The system server doesn't have the permission to chown the file to "shell", so we
            // make it readable by everyone and put it in a directory that is only accessible by
            // "shell", which is created by system/core/rootdir/init.rc. The permissions are
            // verified by the code above.
            Os.fchmod(outputStream.getFD(), 0644);
            Streams.copy(inputStream, outputStream);
        } catch (IOException | ErrnoException e) {
            Utils.deleteIfExistsSafe(outputPath);
            throw new RuntimeException(e);
        }
    }

    private static class WithCancellationSignal implements AutoCloseable {
        @NonNull private final CancellationSignal mSignal = new CancellationSignal();
        @NonNull private final String mJobId;

        public WithCancellationSignal(@NonNull PrintWriter pw) {
            mJobId = UUID.randomUUID().toString();
            pw.printf("Job ID: %s\n", mJobId);
            pw.flush();

            synchronized (sCancellationSignalMap) {
                sCancellationSignalMap.put(mJobId, mSignal);
            }
        }

        @NonNull
        public CancellationSignal get() {
            return mSignal;
        }

        public void close() {
            synchronized (sCancellationSignalMap) {
                sCancellationSignalMap.remove(mJobId);
            }
        }
    }
}
