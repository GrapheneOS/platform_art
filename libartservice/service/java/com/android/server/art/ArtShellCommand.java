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

import android.os.Binder;
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

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
public final class ArtShellCommand extends BasicShellCommandHandler {
    private static final String TAG = "ArtShellCommand";

    private final ArtManagerLocal mArtManagerLocal;
    private final PackageManagerLocal mPackageManagerLocal;

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
                OptimizeResult result = mArtManagerLocal.optimizePackage(
                        snapshot, getNextArgRequired(), paramsBuilder.build());
                switch (result.getFinalStatus()) {
                    case OptimizeResult.OPTIMIZE_SKIPPED:
                        pw.println("SKIPPED");
                        break;
                    case OptimizeResult.OPTIMIZE_PERFORMED:
                        pw.println("PERFORMED");
                        break;
                    case OptimizeResult.OPTIMIZE_FAILED:
                        pw.println("FAILED");
                        break;
                    case OptimizeResult.OPTIMIZE_CANCELLED:
                        pw.println("CANCELLED");
                        break;
                }
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
        pw.println("    Options:");
        pw.println("      -m Set the compiler filter.");
        pw.println("      -f Force compilation.");
    }

    private void enforceRoot() {
        final int uid = Binder.getCallingUid();
        if (uid != Process.ROOT_UID) {
            throw new SecurityException("ART service shell commands need root access");
        }
    }
}
