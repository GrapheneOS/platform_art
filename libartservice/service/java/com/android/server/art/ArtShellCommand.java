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
import static com.android.server.art.ReasonMapping.BatchDexoptReason;
import static com.android.server.art.model.ArtFlags.BatchDexoptPass;
import static com.android.server.art.model.ArtFlags.DexoptFlags;
import static com.android.server.art.model.ArtFlags.PriorityClassApi;
import static com.android.server.art.model.DexoptResult.DexContainerFileDexoptResult;
import static com.android.server.art.model.DexoptResult.DexoptResultStatus;
import static com.android.server.art.model.DexoptResult.PackageDexoptResult;
import static com.android.server.art.model.DexoptStatus.DexContainerFileDexoptStatus;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Binder;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.system.StructStat;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;
import com.android.modules.utils.BasicShellCommandHandler;
import com.android.modules.utils.build.SdkLevel;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.model.DexoptStatus;
import com.android.server.art.model.OperationProgress;
import com.android.server.art.prereboot.PreRebootDriver;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.PackageState;

import libcore.io.Streams;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.channels.ClosedByInterruptException;
import java.nio.channels.FileChannel;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;
import java.util.function.Function;

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public final class ArtShellCommand extends BasicShellCommandHandler {
    /** The default location for profile dumps. */
    private final static String PROFILE_DEBUG_LOCATION = "/data/misc/profman";

    @NonNull private final Injector mInjector;

    @GuardedBy("sCancellationSignalMap")
    @NonNull
    private static final Map<String, CancellationSignal> sCancellationSignalMap = new HashMap<>();

    public ArtShellCommand(@NonNull ArtManagerLocal artManagerLocal,
            @NonNull PackageManagerLocal packageManagerLocal) {
        this(new Injector(artManagerLocal, packageManagerLocal));
    }

    @VisibleForTesting
    public ArtShellCommand(@NonNull Injector injector) {
        mInjector = injector;
    }

    @Override
    public int onCommand(String cmd) {
        // Apps shouldn't call ART Service shell commands, not even for dexopting themselves.
        enforceRootOrShell();
        PrintWriter pw = getOutPrintWriter();
        try (var snapshot = mInjector.getPackageManagerLocal().withFilteredSnapshot()) {
            switch (cmd) {
                case "compile":
                    return handleCompile(pw, snapshot);
                case "reconcile-secondary-dex-files":
                    pw.println("Warning: 'pm reconcile-secondary-dex-files' is deprecated. It is "
                            + "now doing nothing");
                    return 0;
                case "force-dex-opt":
                    return handleForceDexopt(pw, snapshot);
                case "bg-dexopt-job":
                    return handleBgDexoptJob(pw, snapshot);
                case "cancel-bg-dexopt-job":
                    pw.println("Warning: 'pm cancel-bg-dexopt-job' is deprecated. It is now an "
                            + "alias of 'pm bg-dexopt-job --cancel'");
                    return handleCancelBgDexoptJob(pw);
                case "delete-dexopt":
                    return handleDeleteDexopt(pw, snapshot);
                case "dump-profiles":
                    return handleDumpProfile(pw, snapshot);
                case "snapshot-profile":
                    return handleSnapshotProfile(pw, snapshot);
                case "art":
                    return handleArtCommand(pw, snapshot);
                default:
                    // Can't happen. Only supported commands are forwarded to ART Service.
                    throw new IllegalArgumentException(
                            String.format("Unexpected command '%s' forwarded to ART Service", cmd));
            }
        } catch (IllegalArgumentException | SnapshotProfileException e) {
            pw.println("Error: " + e.getMessage());
            return 1;
        }
    }

    private int handleArtCommand(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        String subcmd = getNextArgRequired();
        switch (subcmd) {
            case "dexopt-packages": {
                return handleBatchDexopt(pw, snapshot);
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
            case "dump": {
                boolean verifySdmSignatures = false;

                String opt;
                while ((opt = getNextOption()) != null) {
                    switch (opt) {
                        case "--verify-sdm-signatures":
                            verifySdmSignatures = true;
                            break;
                    }
                }

                String packageName = getNextArg();
                if (packageName != null) {
                    mInjector.getArtManagerLocal().dumpPackage(
                            pw, snapshot, packageName, verifySdmSignatures);
                } else {
                    mInjector.getArtManagerLocal().dump(pw, snapshot, verifySdmSignatures);
                }
                return 0;
            }
            case "cleanup": {
                return handleCleanup(pw, snapshot);
            }
            case "clear-app-profiles": {
                mInjector.getArtManagerLocal().clearAppProfiles(snapshot, getNextArgRequired());
                pw.println("Profiles cleared");
                return 0;
            }
            case "on-ota-staged": {
                return handleOnOtaStaged(pw);
            }
            case "pr-dexopt-job": {
                return handlePrDexoptJob(pw);
            }
            case "configure-batch-dexopt": {
                return handleConfigureBatchDexopt(pw);
            }
            default:
                pw.printf("Error: Unknown 'art' sub-command '%s'\n", subcmd);
                pw.println("See 'pm help' for help");
                return 1;
        }
    }

    private int handleCompile(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        @DexoptFlags int scopeFlags = 0;
        String reason = null;
        String compilerFilter = null;
        @PriorityClassApi int priorityClass = ArtFlags.PRIORITY_NONE;
        String splitArg = null;
        boolean force = false;
        boolean reset = false;
        boolean forAllPackages = false;
        boolean legacyClearProfile = false;
        boolean verbose = false;
        boolean forceMergeProfile = false;
        boolean forceCompilerFilter = false;

        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "-a":
                    forAllPackages = true;
                    break;
                case "-r":
                    reason = getNextArgRequired();
                    break;
                case "-m":
                    compilerFilter = getNextArgRequired();
                    forceCompilerFilter = true;
                    break;
                case "-p":
                    priorityClass = parsePriorityClass(getNextArgRequired());
                    break;
                case "-f":
                    force = true;
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
                case "--full":
                    scopeFlags |= ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SECONDARY_DEX
                            | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES;
                    break;
                case "--split":
                    splitArg = getNextArgRequired();
                    break;
                case "--reset":
                    reset = true;
                    break;
                case "-c":
                    pw.println("Warning: Flag '-c' is deprecated and usually produces undesired "
                            + "results. Please use one of the following commands instead.");
                    pw.println("- To clear the local profiles only, use "
                            + "'pm art clear-app-profiles PACKAGE_NAME'. (The existing dexopt "
                            + "artifacts will be kept, even if they are derived from the "
                            + "profiles.)");
                    pw.println("- To clear the local profiles and also clear the dexopt artifacts "
                            + "that are derived from them, use 'pm compile --reset PACKAGE_NAME'. "
                            + "(The package will be reset to the initial state as if it's newly "
                            + "installed, which means the package will be re-dexopted if "
                            + "necessary, and cloud profiles will be used if exist.)");
                    pw.println("- To re-dexopt the package with no profile, use "
                            + "'pm compile -m verify -f PACKAGE_NAME'. (The local profiles "
                            + "will be kept but not used during the dexopt. The dexopt artifacts "
                            + "are guaranteed to have no compiled code.)");
                    legacyClearProfile = true;
                    break;
                case "--check-prof":
                    getNextArgRequired();
                    pw.println("Warning: Ignoring obsolete flag '--check-prof'. It is "
                            + "unconditionally enabled now");
                    break;
                case "-v":
                    verbose = true;
                    break;
                case "--force-merge-profile":
                    forceMergeProfile = true;
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }

        List<String> packageNames = forAllPackages
                ? List.copyOf(snapshot.getPackageStates().keySet())
                : List.of(getNextArgRequired());

        var paramsBuilder = new DexoptParams.Builder(ReasonMapping.REASON_CMDLINE);
        if (reason != null) {
            if (reason.equals(ReasonMapping.REASON_INACTIVE)) {
                pw.println("Warning: '-r inactive' produces undesired results.");
            }
            if (compilerFilter == null) {
                paramsBuilder.setCompilerFilter(ReasonMapping.getCompilerFilterForReason(reason));
            }
            if (priorityClass == ArtFlags.PRIORITY_NONE) {
                paramsBuilder.setPriorityClass(ReasonMapping.getPriorityClassForReason(reason));
            }
        }
        if (compilerFilter != null) {
            paramsBuilder.setCompilerFilter(compilerFilter);
        }
        if (priorityClass != ArtFlags.PRIORITY_NONE) {
            paramsBuilder.setPriorityClass(priorityClass);
        }
        if (force) {
            paramsBuilder.setFlags(ArtFlags.FLAG_FORCE, ArtFlags.FLAG_FORCE);
        }
        if (forceMergeProfile) {
            paramsBuilder.setFlags(
                    ArtFlags.FLAG_FORCE_MERGE_PROFILE, ArtFlags.FLAG_FORCE_MERGE_PROFILE);
        }
        if (forceCompilerFilter) {
            paramsBuilder.setFlags(
                    ArtFlags.FLAG_FORCE_COMPILER_FILTER, ArtFlags.FLAG_FORCE_COMPILER_FILTER);
        }
        if (splitArg != null) {
            if (scopeFlags != 0) {
                pw.println("Error: '--primary-dex', '--secondary-dex', "
                        + "'--include-dependencies', or '--full' must not be set when '--split' "
                        + "is set.");
                return 1;
            }
            if (forAllPackages) {
                pw.println("Error:  '-a' cannot be specified together with '--split'");
                return 1;
            }
            scopeFlags = ArtFlags.FLAG_FOR_PRIMARY_DEX;
            paramsBuilder.setFlags(ArtFlags.FLAG_FOR_SINGLE_SPLIT, ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                    .setSplitName(getSplitName(pw, snapshot, packageNames.get(0), splitArg));
        }
        if (scopeFlags != 0) {
            paramsBuilder.setFlags(scopeFlags,
                    ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SECONDARY_DEX
                            | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES);
        } else {
            paramsBuilder.setFlags(
                    ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES,
                    ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SECONDARY_DEX
                            | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES);
        }
        if (forAllPackages) {
            // We'll iterate over all packages anyway.
            paramsBuilder.setFlags(0, ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES);
        }

        if (reset) {
            return resetPackages(pw, snapshot, packageNames, verbose);
        } else {
            if (legacyClearProfile) {
                // For compat only. Combining this with dexopt usually produces in undesired
                // results.
                for (String packageName : packageNames) {
                    mInjector.getArtManagerLocal().clearAppProfiles(snapshot, packageName);
                }
            }
            return dexoptPackages(pw, snapshot, packageNames, paramsBuilder.build(), verbose);
        }
    }

    private int handleForceDexopt(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        pw.println("Warning: 'pm force-dex-opt' is deprecated. Please use 'pm compile "
                + "-f PACKAGE_NAME' instead");
        return dexoptPackages(pw, snapshot, List.of(getNextArgRequired()),
                new DexoptParams.Builder(ReasonMapping.REASON_CMDLINE)
                        .setFlags(ArtFlags.FLAG_FORCE, ArtFlags.FLAG_FORCE)
                        .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX
                                        | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES,
                                ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SECONDARY_DEX
                                        | ArtFlags.FLAG_SHOULD_INCLUDE_DEPENDENCIES)
                        .build(),
                false /* verbose */);
    }

    private int handleBgDexoptJob(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        String opt = getNextOption();
        if (opt == null) {
            List<String> packageNames = new ArrayList<>();
            String arg;
            while ((arg = getNextArg()) != null) {
                packageNames.add(arg);
            }
            if (!packageNames.isEmpty()) {
                pw.println("Warning: Running 'pm bg-dexopt-job' with package names is deprecated. "
                        + "Please use 'pm compile -r bg-dexopt PACKAGE_NAME' instead");
                return dexoptPackages(pw, snapshot, packageNames,
                        new DexoptParams.Builder(ReasonMapping.REASON_BG_DEXOPT).build(),
                        false /* verbose */);
            }

            CompletableFuture<BackgroundDexoptJob.Result> runningJob =
                    mInjector.getArtManagerLocal().getRunningBackgroundDexoptJob();
            if (runningJob != null) {
                pw.println("Another job already running. Waiting for it to finish... To cancel it, "
                        + "run 'pm bg-dexopt-job --cancel'. in a separate shell.");
                pw.flush();
                Utils.getFuture(runningJob);
            }
            CompletableFuture<BackgroundDexoptJob.Result> future =
                    mInjector.getArtManagerLocal().startBackgroundDexoptJobAndReturnFuture();
            pw.println("Job running...  To cancel it, run 'pm bg-dexopt-job --cancel'. in a "
                    + "separate shell.");
            pw.flush();
            BackgroundDexoptJob.Result result = Utils.getFuture(future);
            if (result instanceof BackgroundDexoptJob.CompletedResult) {
                var completedResult = (BackgroundDexoptJob.CompletedResult) result;
                if (completedResult.isCancelled()) {
                    pw.println("Job cancelled. See logs for details");
                } else {
                    pw.println("Job finished. See logs for details");
                }
            } else if (result instanceof BackgroundDexoptJob.FatalErrorResult) {
                // Never expected.
                pw.println("Job encountered a fatal error");
            }
            return 0;
        }
        switch (opt) {
            case "--cancel": {
                return handleCancelBgDexoptJob(pw);
            }
            case "--enable": {
                // This operation requires the uid to be "system" (1000).
                long identityToken = Binder.clearCallingIdentity();
                try {
                    mInjector.getArtManagerLocal().scheduleBackgroundDexoptJob();
                } finally {
                    Binder.restoreCallingIdentity(identityToken);
                }
                pw.println("Background dexopt job enabled");
                return 0;
            }
            case "--disable": {
                // This operation requires the uid to be "system" (1000).
                long identityToken = Binder.clearCallingIdentity();
                try {
                    mInjector.getArtManagerLocal().unscheduleBackgroundDexoptJob();
                } finally {
                    Binder.restoreCallingIdentity(identityToken);
                }
                pw.println("Background dexopt job disabled");
                return 0;
            }
            default:
                pw.println("Error: Unknown option: " + opt);
                return 1;
        }
    }

    private int handleCancelBgDexoptJob(@NonNull PrintWriter pw) {
        mInjector.getArtManagerLocal().cancelBackgroundDexoptJob();
        pw.println("Background dexopt job cancelled");
        return 0;
    }

    private int handleCleanup(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        long freedBytes = mInjector.getArtManagerLocal().cleanup(snapshot);
        pw.printf("Freed %d bytes\n", freedBytes);
        return 0;
    }

    private int handleDeleteDexopt(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        DeleteResult result = mInjector.getArtManagerLocal().deleteDexoptArtifacts(
                snapshot, getNextArgRequired());
        pw.printf("Freed %d bytes\n", result.getFreedBytes());
        return 0;
    }

    private int handleSnapshotProfile(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot)
            throws SnapshotProfileException {
        String splitName = null;
        String codePath = null;

        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "--split":
                    splitName = getNextArgRequired();
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }

        String packageName = getNextArgRequired();

        if ("--code-path".equals(getNextOption())) {
            pw.println("Warning: Specifying a split using '--code-path' is deprecated. Please use "
                    + "'--split SPLIT_NAME' instead");
            pw.println("Tip: '--split SPLIT_NAME' must be passed before the package name");
            codePath = getNextArgRequired();
        }

        if (splitName != null && codePath != null) {
            pw.println("Error: '--split' and '--code-path' cannot be both specified");
            return 1;
        }

        if (packageName.equals(Utils.PLATFORM_PACKAGE_NAME)) {
            if (splitName != null) {
                pw.println("Error: '--split' must not be specified for boot image profile");
                return 1;
            }
            if (codePath != null) {
                pw.println("Error: '--code-path' must not be specified for boot image profile");
                return 1;
            }
            return handleSnapshotBootProfile(pw, snapshot);
        }

        if (splitName != null && splitName.isEmpty()) {
            splitName = null;
        }
        if (codePath != null) {
            splitName = getSplitNameByFullPath(snapshot, packageName, codePath);
        }

        return handleSnapshotAppProfile(pw, snapshot, packageName, splitName);
    }

    private int handleSnapshotBootProfile(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot)
            throws SnapshotProfileException {
        String outputRelativePath = "android.prof";
        ParcelFileDescriptor fd = mInjector.getArtManagerLocal().snapshotBootImageProfile(snapshot);
        writeProfileFdContentsToFile(pw, fd, outputRelativePath);
        return 0;
    }

    private int handleSnapshotAppProfile(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @Nullable String splitName) throws SnapshotProfileException {
        String outputRelativePath = String.format("%s%s.prof", packageName,
                splitName != null ? String.format("-split_%s.apk", splitName) : "");
        ParcelFileDescriptor fd =
                mInjector.getArtManagerLocal().snapshotAppProfile(snapshot, packageName, splitName);
        writeProfileFdContentsToFile(pw, fd, outputRelativePath);
        return 0;
    }

    private int handleDumpProfile(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot)
            throws SnapshotProfileException {
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
        try (var tracing = new Utils.Tracing("dump profiles")) {
            // `flushProfiles` may take time and may have unexpected side-effects (e.g., when the
            // app has its own thread waiting for SIGUSR1). Therefore, We call it in the shell
            // command handler instead of in `dumpAppProfile` to prevent existing Java API users
            // from being impacted by this behavior.
            pw.println("Waiting for app processes to flush profiles...");
            pw.flush();
            long startTimeMs = System.currentTimeMillis();
            if (mInjector.getArtManagerLocal().flushProfiles(snapshot, packageName)) {
                pw.printf("App processes flushed profiles in %dms\n",
                        System.currentTimeMillis() - startTimeMs);
            } else {
                pw.println("Timed out while waiting for app processes to flush profiles");
            }

            for (PrimaryDexInfo dexInfo : PrimaryDexUtils.getDexInfo(pkg)) {
                String profileName = PrimaryDexUtils.getProfileName(dexInfo.splitName());
                // The path is intentionally inconsistent with the one for "snapshot-profile". This
                // is to match the behavior of the legacy PM shell command.
                String outputRelativePath =
                        String.format("%s-%s.prof.txt", packageName, profileName);
                ParcelFileDescriptor fd = mInjector.getArtManagerLocal().dumpAppProfile(
                        snapshot, packageName, dexInfo.splitName(), dumpClassesAndMethods);
                writeProfileFdContentsToFile(pw, fd, outputRelativePath);
            }
        }
        return 0;
    }

    private int handleBatchDexopt(
            @NonNull PrintWriter pw, @NonNull PackageManagerLocal.FilteredSnapshot snapshot) {
        String reason = null;
        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "-r":
                    reason = getNextArgRequired();
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }
        if (reason == null) {
            pw.println("Error: '-r REASON' is required");
            return 1;
        }
        if (!ReasonMapping.BATCH_DEXOPT_REASONS.contains(reason)) {
            pw.printf("Error: Invalid batch dexopt reason '%s'. Valid values are: %s\n", reason,
                    ReasonMapping.BATCH_DEXOPT_REASONS);
            return 1;
        }

        final String finalReason = reason;

        // Create callbacks to print the progress.
        Map<Integer, Consumer<OperationProgress>> progressCallbacks = new HashMap<>();
        for (@BatchDexoptPass int pass : ArtFlags.BATCH_DEXOPT_PASSES) {
            progressCallbacks.put(pass, progress -> {
                pw.println(String.format(Locale.US, "%s: %d%%",
                        getProgressMessageForBatchDexoptPass(pass, finalReason),
                        progress.getPercentage()));
                pw.flush();
            });
        }

        ExecutorService progressCallbackExecutor = Executors.newSingleThreadExecutor();
        try (var signal = new WithCancellationSignal(pw, true /* verbose */)) {
            Map<Integer, DexoptResult> results =
                    mInjector.getArtManagerLocal().dexoptPackages(snapshot, finalReason,
                            signal.get(), progressCallbackExecutor, progressCallbacks);

            Utils.executeAndWait(progressCallbackExecutor, () -> {
                for (@BatchDexoptPass int pass : ArtFlags.BATCH_DEXOPT_PASSES) {
                    if (results.containsKey(pass)) {
                        pw.println("Result of "
                                + getProgressMessageForBatchDexoptPass(pass, finalReason)
                                          .toLowerCase(Locale.US)
                                + ":");
                        printDexoptResult(
                                pw, results.get(pass), true /* verbose */, true /* multiPackage */);
                    }
                }
            });
        } finally {
            progressCallbackExecutor.shutdown();
        }

        return 0;
    }

    private int handleOnOtaStaged(@NonNull PrintWriter pw) {
        if (!SdkLevel.isAtLeastV()) {
            pw.println("Error: Unsupported command 'on-ota-staged'");
            return 1;
        }

        int uid = mInjector.getCallingUid();
        if (uid != Process.ROOT_UID) {
            throw new SecurityException("Only root can call 'on-ota-staged'");
        }

        String mode = null;
        String otaSlot = null;

        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "--slot":
                    otaSlot = getNextArgRequired();
                    break;
                case "--start":
                    mode = opt;
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }

        if ("--start".equals(mode)) {
            if (otaSlot != null) {
                pw.println("Error: '--slot' cannot be specified together with '--start'");
                return 1;
            }
            return handleOnOtaStagedStart(pw);
        } else {
            if (otaSlot == null) {
                pw.println("Error: '--slot' must be specified");
                return 1;
            }

            if (mInjector.getArtManagerLocal().getPreRebootDexoptJob().isAsyncForOta()) {
                return handleSchedulePrDexoptJob(pw, otaSlot);
            } else {
                // In the synchronous case, `update_engine` has already mapped snapshots for us.
                return handleRunPrDexoptJob(pw, otaSlot, true /* isUpdateEngineReady */);
            }
        }
    }

    private int handlePrDexoptJob(@NonNull PrintWriter pw) {
        if (!SdkLevel.isAtLeastV()) {
            pw.println("Error: Unsupported command 'pr-dexopt-job'");
            return 1;
        }

        String mode = null;
        String otaSlot = null;

        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "--slot":
                    otaSlot = getNextArgRequired();
                    break;
                case "--version":
                case "--test":
                case "--run":
                case "--schedule":
                case "--cancel":
                    if (mode != null) {
                        pw.println("Error: Only one mode can be specified");
                        return 1;
                    }
                    mode = opt;
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }

        if (mode == null) {
            pw.println("Error: No mode specified");
            return 1;
        }

        if (otaSlot != null && mInjector.getCallingUid() != Process.ROOT_UID) {
            throw new SecurityException("Only root can specify '--slot'");
        }

        switch (mode) {
            case "--version":
                pw.println(3);
                return 0;
            case "--test":
                return handleTestPrDexoptJob(pw);
            case "--run":
                // Passing isUpdateEngineReady=false will make the job call update_engine's
                // triggerPostinstall to map the snapshot devices if the API is available.
                // It's always safe to do so because triggerPostinstall can be called at any time
                // any number of times to map the snapshots if any are available.
                return handleRunPrDexoptJob(pw, otaSlot, false /* isUpdateEngineReady */);
            case "--schedule":
                return handleSchedulePrDexoptJob(pw, otaSlot);
            case "--cancel":
                return handleCancelPrDexoptJob(pw);
            default:
                // Can't happen.
                throw new IllegalStateException("Unknown mode: " + mode);
        }
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handleTestPrDexoptJob(@NonNull PrintWriter pw) {
        try {
            mInjector.getArtManagerLocal().getPreRebootDexoptJob().test();
            pw.println("Success");
            return 0;
        } catch (Exception e) {
            pw.println("Failure");
            e.printStackTrace(pw);
            return 2; // "1" is for general errors. Use "2" for the test failure.
        }
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handleRunPrDexoptJob(
            @NonNull PrintWriter pw, @Nullable String otaSlot, boolean isUpdateEngineReady) {
        PreRebootDexoptJob job = mInjector.getArtManagerLocal().getPreRebootDexoptJob();

        CompletableFuture<Void> future = job.onUpdateReadyStartNow(otaSlot, isUpdateEngineReady);
        if (future == null) {
            pw.println("Job disabled by system property");
            return 1;
        }

        return handlePrDexoptJobRunning(pw, future);
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handleOnOtaStagedStart(@NonNull PrintWriter pw) {
        PreRebootDexoptJob job = mInjector.getArtManagerLocal().getPreRebootDexoptJob();

        // We assume we're being invoked from within `UpdateEngine.triggerPostinstall` in
        // `PreRebootDexoptJob.triggerUpdateEnginePostinstallAndWait`, so a Pre-reboot Dexopt job is
        // waiting.
        CompletableFuture<Void> future = job.notifyUpdateEngineReady();
        if (future == null) {
            pw.println("No waiting job found");
            return 1;
        }

        return handlePrDexoptJobRunning(pw, future);
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handlePrDexoptJobRunning(
            @NonNull PrintWriter pw, @NonNull CompletableFuture<Void> future) {
        PreRebootDexoptJob job = mInjector.getArtManagerLocal().getPreRebootDexoptJob();

        // Read stdin and cancel on broken pipe, to detect if the caller (e.g. update_engine) has
        // killed the postinstall script.
        // Put the read in a separate thread because there isn't an easy way in Java to wait for
        // both the `Future` and the read.
        var readThread = new Thread(() -> {
            try (var in = new FileInputStream(getInFileDescriptor())) {
                ByteBuffer buffer = ByteBuffer.allocate(128 /* capacity */);
                FileChannel channel = in.getChannel();
                while (channel.read(buffer) >= 0) {
                    buffer.clear();
                }
                // Broken pipe.
                job.cancelGiven(future, true /* expectInterrupt */);
            } catch (ClosedByInterruptException e) {
                // Job finished normally.
            } catch (IOException e) {
                AsLog.e("Unexpected exception", e);
                job.cancelGiven(future, true /* expectInterrupt */);
            } catch (RuntimeException e) {
                AsLog.wtf("Unexpected exception", e);
                job.cancelGiven(future, true /* expectInterrupt */);
            }
        });
        readThread.start();
        pw.println("Job running...  To cancel it, press Ctrl+C or run "
                + "'pm art pr-dexopt-job --cancel' in a separate shell.");
        pw.flush();

        try {
            Utils.getFuture(future);
            pw.println("Job finished. See logs for details");
        } catch (RuntimeException e) {
            pw.println("Job encountered a fatal error");
            e.printStackTrace(pw);
        } finally {
            readThread.interrupt();
            try {
                readThread.join();
            } catch (InterruptedException e) {
                AsLog.wtf("Interrupted", e);
            }
        }

        return 0;
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handleSchedulePrDexoptJob(@NonNull PrintWriter pw, @Nullable String otaSlot) {
        int code =
                mInjector.getArtManagerLocal().getPreRebootDexoptJob().onUpdateReadyImpl(otaSlot);
        switch (code) {
            case ArtFlags.SCHEDULE_SUCCESS:
                pw.println("Pre-reboot Dexopt job scheduled");
                return 0;
            case ArtFlags.SCHEDULE_DISABLED_BY_SYSPROP:
                pw.println("Pre-reboot Dexopt job disabled by system property");
                return 1;
            case ArtFlags.SCHEDULE_JOB_SCHEDULER_FAILURE:
                pw.println("Failed to schedule Pre-reboot Dexopt job");
                return 1;
            default:
                // Can't happen.
                throw new IllegalStateException("Unknown result code: " + code);
        }
    }

    @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
    private int handleCancelPrDexoptJob(@NonNull PrintWriter pw) {
        mInjector.getArtManagerLocal().getPreRebootDexoptJob().cancelAny();
        pw.println("Pre-reboot Dexopt job cancelled");
        return 0;
    }

    private int handleConfigureBatchDexopt(@NonNull PrintWriter pw) {
        String inputReason = null;
        List<String> packages = new ArrayList<>();

        String opt;
        while ((opt = getNextOption()) != null) {
            switch (opt) {
                case "-r":
                    inputReason = getNextArgRequired();
                    break;
                case "--package":
                    packages.add(getNextArgRequired());
                    break;
                default:
                    pw.println("Error: Unknown option: " + opt);
                    return 1;
            }
        }

        // Variables used in lambda needs to be effectively final.
        String finalInputReason = inputReason;
        mInjector.getArtManagerLocal().setBatchDexoptStartCallback(
                Runnable::run, (snapshot, reason, defaultPackages, builder, cancellationSignal) -> {
                    if (reason.equals(finalInputReason)) {
                        if (!packages.isEmpty()) {
                            builder.setPackages(packages);
                        }
                    }
                });

        return 0;
    }

    @Override
    public void onHelp() {
        // No one should call this. The help text should be printed by the `onHelp` handler of `cmd
        // package`.
        throw new UnsupportedOperationException("Unexpected call to 'onHelp'");
    }

    public static void printHelp(@NonNull PrintWriter pw) {
        pw.println("compile [-r COMPILATION_REASON] [-m COMPILER_FILTER] [-p PRIORITY] [-f]");
        pw.println("    [--primary-dex] [--secondary-dex] [--include-dependencies] [--full]");
        pw.println("    [--split SPLIT_NAME] [--reset] [-a | PACKAGE_NAME]");
        pw.println("  Dexopt a package or all packages.");
        pw.println("  Options:");
        pw.println("    -a Dexopt all packages");
        pw.println("    -r Set the compiler filter and the priority based on the given");
        pw.println("       compilation reason.");
        pw.println("       Available options: 'first-boot', 'boot-after-ota',");
        pw.println("       'boot-after-mainline-update', 'install', 'bg-dexopt', 'cmdline'.");
        pw.println("    -m Set the target compiler filter. The filter actually used may be");
        pw.println("       different, e.g. 'speed-profile' without profiles present may result in");
        pw.println("       'verify' being used instead. If not specified, this defaults to the");
        pw.println("       value given by -r, or the system property 'pm.dexopt.cmdline'.");
        pw.println("       Available options (in descending order): 'speed', 'speed-profile',");
        pw.println("       'verify'.");
        pw.println("    -p Set the priority of the operation, which determines the resource usage");
        pw.println("       and the process priority. If not specified, this defaults to");
        pw.println("       the value given by -r, or 'PRIORITY_INTERACTIVE'.");
        pw.println("       Available options (in descending order): 'PRIORITY_BOOT',");
        pw.println("       'PRIORITY_INTERACTIVE_FAST', 'PRIORITY_INTERACTIVE',");
        pw.println("       'PRIORITY_BACKGROUND'.");
        pw.println("    -f Force dexopt, also when the compiler filter being applied is not");
        pw.println("       better than that of the current dexopt artifacts for a package.");
        pw.println("    --reset Reset the dexopt state of the package as if the package is newly");
        pw.println("       installed.");
        pw.println("       More specifically, it clears current profiles, reference profiles");
        pw.println("       from local profiles, and any code compiled from those local profiles.");
        pw.println("       If there is an external profile (e.g., a cloud profile), the reference");
        pw.println("       profile from that profile and the code compiled from that profile will");
        pw.println("       be kept.");
        pw.println("       For secondary dex files, it also clears all dexopt artifacts.");
        pw.println("       When this flag is set, all the other flags are ignored.");
        pw.println("    -v Verbose mode. This mode prints detailed results.");
        pw.println("    --force-merge-profile Force merge profiles even if the difference between");
        pw.println("       before and after the merge is not significant.");
        pw.println("  Scope options:");
        pw.println("    --primary-dex Dexopt primary dex files only (all APKs that are installed");
        pw.println("      as part of the package, including the base APK and all other split");
        pw.println("      APKs).");
        pw.println("    --secondary-dex Dexopt secondary dex files only (APKs/JARs that the app");
        pw.println("      puts in its own data directory at runtime and loads with custom");
        pw.println("      classloaders).");
        pw.println("    --include-dependencies Include dependency packages (dependencies that are");
        pw.println("      declared by the app with <uses-library> tags and transitive");
        pw.println("      dependencies). This option can only be used together with");
        pw.println("      '--primary-dex' or '--secondary-dex'.");
        pw.println("    --full Dexopt all above. (Recommended)");
        pw.println("    --split SPLIT_NAME Only dexopt the given split. If SPLIT_NAME is an empty");
        pw.println("      string, only dexopt the base APK.");
        pw.println("      Tip: To pass an empty string, use a pair of quotes (\"\").");
        pw.println("      When this option is set, '--primary-dex', '--secondary-dex',");
        pw.println("      '--include-dependencies', '--full', and '-a' must not be set.");
        pw.println("    Note: If none of the scope options above are set, the scope defaults to");
        pw.println("    '--primary-dex --include-dependencies'.");
        pw.println();
        pw.println("delete-dexopt PACKAGE_NAME");
        pw.println("  Delete the dexopt artifacts of both primary dex files and secondary dex");
        pw.println("  files of a package.");
        pw.println();
        pw.println("bg-dexopt-job [--cancel | --disable | --enable]");
        pw.println("  Control the background dexopt job.");
        pw.println("  Without flags, it starts a background dexopt job immediately and waits for");
        pw.println("    it to finish. If a job is already started either automatically by the");
        pw.println("    system or through this command, it will wait for the running job to");
        pw.println("    finish and then start a new one.");
        pw.println("  Different from 'pm compile -r bg-dexopt -a', the behavior of this command");
        pw.println("  is the same as a real background dexopt job. Specifically,");
        pw.println("    - It only dexopts a subset of apps determined by either the system's");
        pw.println("      default logic based on app usage data or the custom logic specified by");
        pw.println("      the 'ArtManagerLocal.setBatchDexoptStartCallback' Java API.");
        pw.println("    - It runs dexopt in parallel, where the concurrency setting is specified");
        pw.println("      by the system property 'pm.dexopt.bg-dexopt.concurrency'.");
        pw.println("    - If the storage is low, it also downgrades unused apps.");
        pw.println("    - It also cleans up obsolete files.");
        pw.println("  Options:");
        pw.println("    --cancel Cancel any currently running background dexopt job immediately.");
        pw.println("      This cancels jobs started either automatically by the system or through");
        pw.println("      this command. This command is not blocking.");
        pw.println("    --disable: Disable the background dexopt job from being started by the");
        pw.println("      job scheduler. If a job is already started by the job scheduler and is");
        pw.println("      running, it will be cancelled immediately. Does not affect jobs started");
        pw.println("      through this command or by the system in other ways.");
        pw.println("      This state will be lost when the system_server process exits.");
        pw.println("    --enable: Enable the background dexopt job to be started by the job");
        pw.println("      scheduler again, if previously disabled by --disable.");
        pw.println("  When a list of package names is passed, this command does NOT start a real");
        pw.println("  background dexopt job. Instead, it dexopts the given packages sequentially.");
        pw.println("  This usage is deprecated. Please use 'pm compile -r bg-dexopt PACKAGE_NAME'");
        pw.println("  instead.");
        pw.println();
        pw.println("snapshot-profile [android | [--split SPLIT_NAME] PACKAGE_NAME]");
        pw.println("  Snapshot the boot image profile or the app profile and save it to");
        pw.println("  '" + PROFILE_DEBUG_LOCATION + "'.");
        pw.println("  If 'android' is passed, the command snapshots the boot image profile, and");
        pw.println("  the output filename is 'android.prof'.");
        pw.println("  If a package name is passed, the command snapshots the app profile.");
        pw.println("  Options:");
        pw.println("    --split SPLIT_NAME If specified, the command snapshots the profile of the");
        pw.println("      given split, and the output filename is");
        pw.println("      'PACKAGE_NAME-split_SPLIT_NAME.apk.prof'.");
        pw.println("      If not specified, the command snapshots the profile of the base APK,");
        pw.println("      and the output filename is 'PACKAGE_NAME.prof'");
        pw.println();
        pw.println("dump-profiles [--dump-classes-and-methods] PACKAGE_NAME");
        pw.println("  Dump the profiles of the given app in text format and save the outputs to");
        pw.println("  '" + PROFILE_DEBUG_LOCATION + "'.");
        pw.println("  The profile of the base APK is dumped to 'PACKAGE_NAME-primary.prof.txt'");
        pw.println("  The profile of a split APK is dumped to");
        pw.println("  'PACKAGE_NAME-SPLIT_NAME.split.prof.txt'");
        pw.println();
        pw.println("art SUB_COMMAND [ARGS]...");
        pw.println("  Run ART Service commands");
        pw.println();
        pw.println("  Supported sub-commands:");
        pw.println();
        pw.println("  cancel JOB_ID");
        pw.println("    Cancel a job started by a shell command. This doesn't apply to background");
        pw.println("    jobs.");
        pw.println();
        pw.println("  clear-app-profiles PACKAGE_NAME");
        pw.println("    Clear the profiles that are collected locally for the given package,");
        pw.println("    including the profiles for primary and secondary dex files. More");
        pw.println("    specifically, this command clears reference profiles and current");
        pw.println("    profiles. External profiles (e.g., cloud profiles) will be kept.");
        pw.println();
        pw.println("  cleanup");
        pw.println("    Cleanup obsolete files, such as dexopt artifacts that are outdated or");
        pw.println("    correspond to dex container files that no longer exist.");
        pw.println();
        pw.println("  dump [--verify-sdm-signatures] [PACKAGE_NAME]");
        pw.println("    Dump the dexopt state in text format to stdout.");
        pw.println("    If PACKAGE_NAME is empty, the command is for all packages. Otherwise, it");
        pw.println("    is for the given package.");
        pw.println("    Options:");
        pw.println("      --verify-sdm-signatures Also verify SDM file signatures and include");
        pw.println("        their statuses.");
        pw.println();
        pw.println("  dexopt-packages -r REASON");
        pw.println("    Run batch dexopt for the given reason.");
        pw.println("    Valid values for REASON: 'first-boot', 'boot-after-ota',");
        pw.println("    'boot-after-mainline-update', 'bg-dexopt'");
        pw.println("    This command is different from 'pm compile -r REASON -a'. For example, it");
        pw.println("    only dexopts a subset of apps, and it runs dexopt in parallel. See the");
        pw.println("    API documentation for 'ArtManagerLocal.dexoptPackages' for details.");
        pw.println();
        pw.println("  on-ota-staged [--slot SLOT | --start]");
        pw.println("    Notify ART Service that an OTA update is staged. ART Service decides what");
        pw.println("    to do with this notification:");
        pw.println("    - If Pre-reboot Dexopt is disabled or unsupported, the command returns");
        pw.println("      non-zero.");
        pw.println("    - If Pre-reboot Dexopt is enabled in synchronous mode, the command blocks");
        pw.println("      until Pre-reboot Dexopt finishes, and returns zero no matter it");
        pw.println("      succeeds or not.");
        pw.println("    - If Pre-reboot Dexopt is enabled in asynchronous mode, the command");
        pw.println("      schedules an asynchronous job and returns 0 immediately. The job will");
        pw.println("      then run by the job scheduler when the device is idle and charging.");
        pw.println("    Options:");
        pw.println("      --slot SLOT The slot that contains the OTA update, '_a' or '_b'.");
        pw.println("      --start Notify the asynchronous job that the snapshot devices are");
        pw.println("        ready. The command blocks until the job finishes, and returns zero no");
        pw.println("        matter it succeeds or not.");
        pw.println("    Note: This command is only supposed to be used by the system. To manually");
        pw.println("    control the Pre-reboot Dexopt job, use 'pr-dexopt-job' instead.");
        pw.println();
        pw.println("  pr-dexopt-job [--version | --run | --schedule | --cancel | --test]");
        pw.println("      [--slot SLOT]");
        pw.println("    Control the Pre-reboot Dexopt job. One of the mode options must be");
        pw.println("    specified.");
        pw.println("    Mode Options:");
        pw.println("      --version Show the version of the Pre-reboot Dexopt job.");
        pw.println("      --run Start a Pre-reboot Dexopt job immediately and waits for it to");
        pw.println("        finish. This command preempts any pending or running job, previously");
        pw.println("        scheduled or started automatically by the system or through any");
        pw.println("        'pr-dexopt-job' command.");
        pw.println("      --schedule Schedule a Pre-reboot Dexopt job and return immediately. The");
        pw.println("        job will then be automatically started by the job scheduler when the");
        pw.println("        device is idle and charging. This command immediately preempts any");
        pw.println("        pending or running job, previously scheduled or started automatically");
        pw.println("        by the system or through any 'pr-dexopt-job' command.");
        pw.println("      --cancel Cancel any pending or running job, previously scheduled or");
        pw.println("        started automatically by the system or through any 'pr-dexopt-job'");
        pw.println("        command.");
        pw.println("      --test The behavior is undefined. Do not use it.");
        pw.println("    Options:");
        pw.println("      --slot SLOT The slot that contains the OTA update, '_a' or '_b'. If not");
        pw.println("        specified, the job is for a Mainline update");
        pw.println();
        pw.println("  configure-batch-dexopt -r REASON [--package PACKAGE_NAME]...");
        pw.println("    Configure batch dexopt parameters to be applied when the given reason is");
        pw.println("    used.");
        pw.println("    Once called, this command overwrites any configuration done through");
        pw.println("    'ArtManagerLocal.setBatchDexoptStartCallback' or through this command for");
        pw.println("    all reasons. In other words, configurations for other reasons are reset");
        pw.println("    to the default.");
        pw.println("    Valid values for REASON: 'first-boot', 'boot-after-ota',");
        pw.println("    'boot-after-mainline-update', 'bg-dexopt', 'ab-ota'");
        pw.println("    Options:");
        pw.println("      --package PACKAGE_NAME The package name to dexopt. This flag can be");
        pw.println("        passed multiple times, to specify multiple packages. If not");
        pw.println("        specified, the default package list will be used.");
    }

    private void enforceRootOrShell() {
        final int uid = mInjector.getCallingUid();
        if (uid != Process.ROOT_UID && uid != Process.SHELL_UID) {
            throw new SecurityException("ART service shell commands need root or shell access");
        }
    }

    @PriorityClassApi
    int parsePriorityClass(@NonNull String priorityClass) {
        switch (priorityClass) {
            case "PRIORITY_BOOT":
                return ArtFlags.PRIORITY_BOOT;
            case "PRIORITY_INTERACTIVE_FAST":
                return ArtFlags.PRIORITY_INTERACTIVE_FAST;
            case "PRIORITY_INTERACTIVE":
                return ArtFlags.PRIORITY_INTERACTIVE;
            case "PRIORITY_BACKGROUND":
                return ArtFlags.PRIORITY_BACKGROUND;
            default:
                throw new IllegalArgumentException("Unknown priority " + priorityClass);
        }
    }

    @Nullable
    private String getSplitName(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot, @NonNull String packageName,
            @NonNull String splitArg) {
        if (splitArg.isEmpty()) {
            return null; // Base APK.
        }

        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
        PrimaryDexInfo dexInfo =
                PrimaryDexUtils.findDexInfo(pkg, info -> splitArg.equals(info.splitName()));
        if (dexInfo != null) {
            return splitArg;
        }
        dexInfo = PrimaryDexUtils.findDexInfo(
                pkg, info -> splitArg.equals(new File(info.dexPath()).getName()));
        if (dexInfo != null) {
            pw.println("Warning: Specifying a split using a filename is deprecated. Please "
                    + "use a split name (or an empty string for the base APK) instead");
            return dexInfo.splitName();
        }

        throw new IllegalArgumentException(String.format("Split '%s' not found", splitArg));
    }

    @Nullable
    private String getSplitNameByFullPath(@NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull String packageName, @NonNull String fullPath) {
        PackageState pkgState = Utils.getPackageStateOrThrow(snapshot, packageName);
        AndroidPackage pkg = Utils.getPackageOrThrow(pkgState);
        PrimaryDexInfo dexInfo =
                PrimaryDexUtils.findDexInfo(pkg, info -> fullPath.equals(info.dexPath()));
        if (dexInfo != null) {
            return dexInfo.splitName();
        }
        throw new IllegalArgumentException(String.format("Code path '%s' not found", fullPath));
    }

    private int resetPackages(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull List<String> packageNames, boolean verbose) {
        try (var signal = new WithCancellationSignal(pw, verbose)) {
            for (String packageName : packageNames) {
                DexoptResult result = mInjector.getArtManagerLocal().resetDexoptStatus(
                        snapshot, packageName, signal.get());
                printDexoptResult(pw, result, verbose, packageNames.size() > 1);
            }
        }
        return 0;
    }

    private int dexoptPackages(@NonNull PrintWriter pw,
            @NonNull PackageManagerLocal.FilteredSnapshot snapshot,
            @NonNull List<String> packageNames, @NonNull DexoptParams params, boolean verbose) {
        try (var signal = new WithCancellationSignal(pw, verbose)) {
            for (String packageName : packageNames) {
                DexoptResult result = mInjector.getArtManagerLocal().dexoptPackage(
                        snapshot, packageName, params, signal.get());
                printDexoptResult(pw, result, verbose, packageNames.size() > 1);
            }
        }
        return 0;
    }

    @NonNull
    private String dexoptResultStatusToSimpleString(@DexoptResultStatus int status) {
        return (status == DexoptResult.DEXOPT_SKIPPED || status == DexoptResult.DEXOPT_PERFORMED)
                ? "Success"
                : "Failure";
    }

    private void printDexoptResult(@NonNull PrintWriter pw, @NonNull DexoptResult result,
            boolean verbose, boolean multiPackage) {
        for (PackageDexoptResult packageResult : result.getPackageDexoptResults()) {
            if (verbose) {
                pw.printf("[%s]\n", packageResult.getPackageName());
                for (DexContainerFileDexoptResult fileResult :
                        packageResult.getDexContainerFileDexoptResults()) {
                    pw.println(fileResult);
                }
            } else if (multiPackage) {
                pw.printf("[%s] %s\n", packageResult.getPackageName(),
                        dexoptResultStatusToSimpleString(packageResult.getStatus()));
            }
        }

        if (verbose) {
            pw.println("Final Status: "
                    + DexoptResult.dexoptResultStatusToString(result.getFinalStatus()));
        } else if (!multiPackage) {
            // Multi-package result is printed by the loop above.
            pw.println(dexoptResultStatusToSimpleString(result.getFinalStatus()));
        }

        pw.flush();
    }

    private void writeProfileFdContentsToFile(@NonNull PrintWriter pw,
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
            pw.printf("Profile saved to '%s'\n", outputPath);
        } catch (IOException | ErrnoException e) {
            Utils.deleteIfExistsSafe(outputPath);
            throw new RuntimeException(e);
        }
    }

    @NonNull
    private String getProgressMessageForBatchDexoptPass(
            @BatchDexoptPass int pass, @NonNull @BatchDexoptReason String reason) {
        switch (pass) {
            case ArtFlags.PASS_DOWNGRADE:
                return "Downgrading apps";
            case ArtFlags.PASS_MAIN:
                return reason.equals(ReasonMapping.REASON_BG_DEXOPT) ? "Dexopting apps (main pass)"
                                                                     : "Dexopting apps";
            case ArtFlags.PASS_SUPPLEMENTARY:
                return "Dexopting apps (supplementary pass)";
        }
        throw new IllegalArgumentException("Unknown batch dexopt pass " + pass);
    }

    private static class WithCancellationSignal implements AutoCloseable {
        @NonNull private final CancellationSignal mSignal = new CancellationSignal();
        @NonNull private final String mJobId;

        public WithCancellationSignal(@NonNull PrintWriter pw, boolean verbose) {
            mJobId = UUID.randomUUID().toString();
            if (verbose) {
                pw.printf(
                        "Job running. To cancel it, run 'pm art cancel %s' in a separate shell.\n",
                        mJobId);
                pw.flush();
            }

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

    /** Injector pattern for testing purpose. */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final ArtManagerLocal mArtManagerLocal;
        @NonNull private final PackageManagerLocal mPackageManagerLocal;

        public Injector(@NonNull ArtManagerLocal artManagerLocal,
                @NonNull PackageManagerLocal packageManagerLocal) {
            mArtManagerLocal = artManagerLocal;
            mPackageManagerLocal = packageManagerLocal;
        }

        @NonNull
        public ArtManagerLocal getArtManagerLocal() {
            return mArtManagerLocal;
        }

        @NonNull
        public PackageManagerLocal getPackageManagerLocal() {
            return mPackageManagerLocal;
        }

        public int getCallingUid() {
            return Binder.getCallingUid();
        }
    }
}
