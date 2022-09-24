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
import static com.android.server.art.model.OptimizationStatus.DexFileOptimizationStatus;
import static com.android.server.art.model.OptimizeResult.DexFileOptimizeResult;
import static com.android.server.art.model.OptimizeResult.OptimizeStatus;
import static com.android.server.art.model.OptimizeResult.PackageOptimizeResult;

import android.annotation.NonNull;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.Process;

import com.android.modules.utils.BasicShellCommandHandler;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.wrapper.PackageManagerLocal;
import com.android.server.pm.snapshot.PackageDataSnapshot;

import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
public final class ArtShellCommand extends BasicShellCommandHandler {
    private static final String TAG = "ArtShellCommand";

    private final ArtManagerLocal mArtManagerLocal;
    private final PackageManagerLocal mPackageManagerLocal;

    private static Map<String, CancellationSignal> sCancellationSignalMap = new HashMap<>();

    public ArtShellCommand(
            ArtManagerLocal artManagerLocal, PackageManagerLocal packageManagerLocal) {
        mArtManagerLocal = artManagerLocal;
        mPackageManagerLocal = packageManagerLocal;
    }

    @Override
    public int onCommand(String cmd) {
        enforceRoot();
        PrintWriter pw = getOutPrintWriter();
        PackageDataSnapshot snapshot = mPackageManagerLocal.snapshot();
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
                for (DexFileOptimizationStatus status :
                        optimizationStatus.getDexFileOptimizationStatuses()) {
                    pw.printf("dexFile = %s, instructionSet = %s, compilerFilter = %s, "
                                    + "compilationReason = %s, locationDebugString = %s\n",
                            status.getDexFile(), status.getInstructionSet(),
                            status.getCompilerFilter(), status.getCompilationReason(),
                            status.getLocationDebugString());
                }
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
                        default:
                            pw.println("Error: Unknown option: " + opt);
                            return 1;
                    }
                }

                String jobId = UUID.randomUUID().toString();
                var signal = new CancellationSignal();
                pw.printf("Job ID: %s\n", jobId);
                pw.flush();

                synchronized (sCancellationSignalMap) {
                    sCancellationSignalMap.put(jobId, signal);
                }

                OptimizeResult result;
                try {
                    result = mArtManagerLocal.optimizePackage(
                            snapshot, getNextArgRequired(), paramsBuilder.build(), signal);
                } finally {
                    synchronized (sCancellationSignalMap) {
                        sCancellationSignalMap.remove(jobId);
                    }
                }

                pw.println(optimizeStatusToString(result.getFinalStatus()));
                for (PackageOptimizeResult packageResult : result.getPackageOptimizeResults()) {
                    pw.printf("[%s]\n", packageResult.getPackageName());
                    for (DexFileOptimizeResult dexFileResult :
                            packageResult.getDexFileOptimizeResults()) {
                        pw.printf("dexFile = %s, instructionSet = %s, compilerFilter = %s, "
                                        + "status = %s, dex2oatWallTimeMillis = %d, "
                                        + "dex2oatCpuTimeMillis = %d\n",
                                dexFileResult.getDexFile(), dexFileResult.getInstructionSet(),
                                dexFileResult.getActualCompilerFilter(),
                                optimizeStatusToString(dexFileResult.getStatus()),
                                dexFileResult.getDex2oatWallTimeMillis(),
                                dexFileResult.getDex2oatCpuTimeMillis());
                    }
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
            default:
                // Handles empty, help, and invalid commands.
                return handleDefaultCommands(cmd);
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
        pw.println("  cancel JOB_ID");
        pw.println("    Cancel a job.");
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
}
