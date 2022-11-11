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

import static com.android.server.art.model.ArtFlags.OptimizeFlags;
import static com.android.server.art.model.OptimizationStatus.DexContainerFileOptimizationStatus;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.OptimizeStatus;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.Process;

import com.android.internal.annotations.GuardedBy;
import com.android.modules.utils.BasicShellCommandHandler;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.pm.PackageManagerLocal;

import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
public final class ArtShellCommand extends BasicShellCommandHandler {
    private static final String TAG = "ArtShellCommand";

    private final ArtManagerLocal mArtManagerLocal;
    private final PackageManagerLocal mPackageManagerLocal;
    private final DexUseManager mDexUseManager = DexUseManager.getInstance();

    @GuardedBy("sCancellationSignalMap")
    @NonNull
    private static final Map<String, CancellationSignal> sCancellationSignalMap = new HashMap<>();

    public ArtShellCommand(
            ArtManagerLocal artManagerLocal, PackageManagerLocal packageManagerLocal) {
        mArtManagerLocal = artManagerLocal;
        mPackageManagerLocal = packageManagerLocal;
    }

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
                    while ((opt = getNextOption()) != null) {
                        switch (opt) {
                            case "-m":
                                paramsBuilder.setCompilerFilter(getNextArgRequired());
                                break;
                            case "-f":
                                paramsBuilder.setFlags(ArtFlags.FLAG_FORCE, ArtFlags.FLAG_FORCE);
                                break;
                            case "--secondary-dex":
                                paramsBuilder.setFlags(ArtFlags.FLAG_FOR_SECONDARY_DEX,
                                        ArtFlags.FLAG_FOR_PRIMARY_DEX
                                                | ArtFlags.FLAG_FOR_SECONDARY_DEX);
                                break;
                            case "--include-dependencies":
                                paramsBuilder.setFlags(ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES,
                                        ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES);
                                break;
                            default:
                                pw.println("Error: Unknown option: " + opt);
                                return 1;
                        }
                    }

                    OptimizeResult result;
                    try (var signal = new WithCancellationSignal(pw)) {
                        result = mArtManagerLocal.optimizePackage(snapshot, getNextArgRequired(),
                                paramsBuilder.build(), signal.get());
                    }
                    printOptimizeResult(pw, result);
                    return 0;
                }
                case "optimize-packages": {
                    OptimizeResult result;
                    try (var signal = new WithCancellationSignal(pw)) {
                        result = mArtManagerLocal.optimizePackages(
                                snapshot, getNextArgRequired(), signal.get());
                    }
                    printOptimizeResult(pw, result);
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
                    mArtManagerLocal.notifyDexContainersLoaded(snapshot, getNextArgRequired(),
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
                    for (DexUseManager.SecondaryDexInfo info :
                            mDexUseManager.getSecondaryDexInfo(getNextArgRequired())) {
                        pw.println(info);
                    }
                    return 0;
                }
                case "dex-use-dump": {
                    pw.println(mDexUseManager.dump());
                    return 0;
                }
                case "dex-use-save": {
                    try {
                        mDexUseManager.save(getNextArgRequired());
                        return 0;
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                }
                case "dex-use-load": {
                    try {
                        mDexUseManager.load(getNextArgRequired());
                        return 0;
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
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
        // TODO(jiakaiz): Also do operations for secondary dex'es by default.
        pw.println("  delete-optimized-artifacts PACKAGE_NAME");
        pw.println("    Delete the optimized artifacts of a package.");
        pw.println("    By default, the command only deletes the optimized artifacts of primary "
                + "dex'es.");
        pw.println("  get-optimization-status PACKAGE_NAME");
        pw.println("    Print the optimization status of a package.");
        pw.println("    By default, the command only prints the optimization status of primary "
                + "dex'es.");
        pw.println("  optimize-package [-m COMPILER_FILTER] [-f] PACKAGE_NAME");
        pw.println("    Optimize a package.");
        pw.println("    By default, the command only optimizes primary dex'es.");
        pw.println("    The command prints a job ID, which can be used to cancel the job using the"
                + "'cancel' command.");
        pw.println("    Options:");
        pw.println("      -m Set the compiler filter.");
        pw.println("      -f Force compilation.");
        pw.println("      --secondary-dex Only compile secondary dex.");
        pw.println("      --include-dependencies Include dependencies.");
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
        pw.println("  dex-use-save PATH");
        pw.println("    Save dex use information to a file in binary proto format.");
        pw.println("  dex-use-load PATH");
        pw.println("    Load dex use information from a file in binary proto format.");
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
                                + "dex2oatWallTimeMillis = %d, dex2oatCpuTimeMillis = %d\n",
                        fileResult.getDexContainerFile(), fileResult.isPrimaryAbi(),
                        fileResult.getAbi(), fileResult.getActualCompilerFilter(),
                        optimizeStatusToString(fileResult.getStatus()),
                        fileResult.getDex2oatWallTimeMillis(),
                        fileResult.getDex2oatCpuTimeMillis());
            }
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
