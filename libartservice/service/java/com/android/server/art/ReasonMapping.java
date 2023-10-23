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

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.StringDef;
import android.annotation.SystemApi;
import android.apphibernation.AppHibernationManager;
import android.content.Context;
import android.os.Build;
import android.os.SystemProperties;
import android.os.UserManager;
import android.text.TextUtils;

import androidx.annotation.RequiresApi;

import com.android.art.rw.flags.Flags;
import com.android.internal.annotations.VisibleForTesting;
import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.ArtFlags.PriorityClassApi;
import com.android.server.art.model.DexoptStatus;
import com.android.server.art.utils.Utils;
import com.android.server.art.utils.Utils.Clock;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.DexFile;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;

/**
 * Maps a compilation reason to a compiler filter and a priority class.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class ReasonMapping {
    private final Injector mInjector;

    /** @hide */
    public ReasonMapping(Context context) {
        mInjector = new Injector(context);
    }

    /** @hide */
    @VisibleForTesting
    public ReasonMapping(Injector injector) {
        mInjector = injector;
    }

    // Keep this in sync with `ArtShellCommand.printHelp` except for 'inactive'.

    /** Dexopting apps on the first boot after flashing or factory resetting the device. */
    public static final String REASON_FIRST_BOOT = "first-boot";
    /** Dexopting apps on the next boot after an OTA. */
    public static final String REASON_BOOT_AFTER_OTA = "boot-after-ota";
    /** Dexopting apps on the next boot after a mainline update. */
    public static final String REASON_BOOT_AFTER_MAINLINE_UPDATE = "boot-after-mainline-update";
    /** Installing an app after user presses the "install"/"update" button. */
    public static final String REASON_INSTALL = "install";
    /** Dexopting apps in the background. */
    public static final String REASON_BG_DEXOPT = "bg-dexopt";
    /** Invoked by cmdline. */
    public static final String REASON_CMDLINE = "cmdline";
    /** Downgrading the compiler filter when an app is not used for a long time. */
    public static final String REASON_INACTIVE = "inactive";
    /**
     * Dexopting apps before the reboot for an OTA or a mainline update <b>asynchronously</b>, known
     * as Asynchronous Pre-reboot Dexopt.
     */
    public static final String REASON_PRE_REBOOT_DEXOPT = "ab-ota";
    /**
     * Dexopting apps before the reboot for an OTA or a mainline update <b>synchronously</b>, known
     * as Synchronous Pre-reboot Dexopt.
     */
    @FlaggedApi(com.android.libcore.Flags.FLAG_OPENJDK_25_V1_APIS)
    public static final String REASON_PRE_REBOOT_DEXOPT_SYNC = "ab-ota-sync";
    /**
     * Dexopting apps after the reboot for an OTA or a mainline update, if the reboot is
     * unattended, known as Post-UR Dexopt.
     *
     * @hide
     */
    public static final String REASON_POST_UNATTENDED_REBOOT = "post-ur";

    // Reasons for Play Install Hints (go/install-hints).
    public static final String REASON_INSTALL_FAST = "install-fast";
    public static final String REASON_INSTALL_BULK = "install-bulk";
    public static final String REASON_INSTALL_BULK_SECONDARY = "install-bulk-secondary";
    public static final String REASON_INSTALL_BULK_DOWNGRADED = "install-bulk-downgraded";
    public static final String REASON_INSTALL_BULK_SECONDARY_DOWNGRADED =
            "install-bulk-secondary-downgraded";

    /** @hide */
    public static final Set<String> REASONS_FOR_INSTALL = Set.of(REASON_INSTALL,
            REASON_INSTALL_FAST, REASON_INSTALL_BULK, REASON_INSTALL_BULK_SECONDARY,
            REASON_INSTALL_BULK_DOWNGRADED, REASON_INSTALL_BULK_SECONDARY_DOWNGRADED);

    // Keep this in sync with `ArtShellCommand.printHelp`.
    /** @hide */
    public static final Set<String> BATCH_DEXOPT_REASONS =
            Set.of(REASON_FIRST_BOOT, REASON_BOOT_AFTER_OTA, REASON_BOOT_AFTER_MAINLINE_UPDATE,
                    REASON_BG_DEXOPT, REASON_PRE_REBOOT_DEXOPT, REASON_POST_UNATTENDED_REBOOT);

    /** @hide */
    public static final Set<String> BOOT_REASONS =
            Set.of(REASON_FIRST_BOOT, REASON_BOOT_AFTER_OTA, REASON_BOOT_AFTER_MAINLINE_UPDATE);

    /**
     * Reasons for {@link ArtManagerLocal#dexoptPackages}.
     *
     * @hide
     */
    // clang-format off
    @StringDef(prefix = "REASON_", value = {
        REASON_FIRST_BOOT,
        REASON_BOOT_AFTER_OTA,
        REASON_BOOT_AFTER_MAINLINE_UPDATE,
        REASON_BG_DEXOPT,
        REASON_PRE_REBOOT_DEXOPT,
        REASON_POST_UNATTENDED_REBOOT,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface BatchDexoptReason {}

    /**
     * Reasons for {@link ArtManagerLocal#onBoot(String, Executor, Consumer<OperationProgress>)}.
     *
     * @hide
     */
    // clang-format off
    @StringDef(prefix = "REASON_", value = {
        REASON_FIRST_BOOT,
        REASON_BOOT_AFTER_OTA,
        REASON_BOOT_AFTER_MAINLINE_UPDATE,
    })
    // clang-format on
    @Retention(RetentionPolicy.SOURCE)
    public @interface BootReason {}

    /**
     * Loads the compiler filter from the system property for the given reason and checks for
     * validity.
     *
     * @throws IllegalArgumentException if the reason is invalid
     * @throws IllegalStateException if the system property value is invalid
     *
     * @hide
     */
    @NonNull
    public static String getCompilerFilterForReason(@NonNull String reason) {
        String value = SystemProperties.get("pm.dexopt." + reason);
        if (TextUtils.isEmpty(value)) {
            if (reason.equals(REASON_POST_UNATTENDED_REBOOT)) {
                // The Post unattended reboot job is supposed to use the bg-dexopt compiler filter,
                // unless explicitly overridden.
                return getCompilerFilterForReason(REASON_BG_DEXOPT);
            }
            if (reason.equals(REASON_PRE_REBOOT_DEXOPT_SYNC)) {
                return getCompilerFilterForReason(REASON_PRE_REBOOT_DEXOPT);
            }
            throw new IllegalArgumentException("No compiler filter for reason '" + reason + "'");
        }
        if (!Utils.isValidArtServiceCompilerFilter(value)) {
            throw new IllegalStateException(
                    "Got invalid compiler filter '" + value + "' for reason '" + reason + "'");
        }
        return value;
    }

    /**
     * Returns the priority for the given reason.
     *
     * @throws IllegalArgumentException if the reason is invalid
     * @see PriorityClassApi
     *
     * @hide
     */
    public static @PriorityClassApi byte getPriorityClassForReason(@NonNull String reason) {
        switch (reason) {
            case REASON_FIRST_BOOT:
            case REASON_BOOT_AFTER_OTA:
            case REASON_BOOT_AFTER_MAINLINE_UPDATE:
                return ArtFlags.PRIORITY_BOOT;
            case REASON_INSTALL_FAST:
                return ArtFlags.PRIORITY_INTERACTIVE_FAST;
            case REASON_INSTALL:
            case REASON_CMDLINE:
                return ArtFlags.PRIORITY_INTERACTIVE;
            case REASON_BG_DEXOPT:
            case REASON_PRE_REBOOT_DEXOPT:
            case REASON_PRE_REBOOT_DEXOPT_SYNC:
            case REASON_POST_UNATTENDED_REBOOT:
            case REASON_INACTIVE:
            case REASON_INSTALL_BULK:
            case REASON_INSTALL_BULK_SECONDARY:
            case REASON_INSTALL_BULK_DOWNGRADED:
            case REASON_INSTALL_BULK_SECONDARY_DOWNGRADED:
                return ArtFlags.PRIORITY_BACKGROUND;
            default:
                throw new IllegalArgumentException("No priority class for reason '" + reason + "'");
        }
    }

    /**
     * Loads the concurrency from the system property, for batch dexopt ({@link
     * ArtManagerLocal#dexoptPackages}). The default is tuned to strike a good balance between
     * device load and dexopt coverage, depending on the situation.
     *
     * @hide
     */
    public static int getConcurrencyForReason(@NonNull @BatchDexoptReason String reason) {
        // TODO(jiakaiz): Revisit the concurrency for non-boot reasons.
        int defaultValue = 1;
        if (BOOT_REASONS.contains(reason)) {
            defaultValue = 4;
        } else if (reason.equals(REASON_POST_UNATTENDED_REBOOT)) {
            // The Post unattended reboot job is supposed to use the bg-dexopt concurrency, unless
            // explicitly overridden.
            defaultValue = getConcurrencyForReason(REASON_BG_DEXOPT);
        } else if (reason.equals(REASON_PRE_REBOOT_DEXOPT_SYNC)) {
            defaultValue = getConcurrencyForReason(REASON_PRE_REBOOT_DEXOPT);
        }

        return SystemProperties.getInt("pm.dexopt." + reason + ".concurrency", defaultValue);
    }

    /**
     * Returns the list of packages to process for the given reason.
     *
     * @hide
     */
    public List<String> getDefaultPackagesForReason(PackageManagerLocal.FilteredSnapshot snapshot,
            /* @BatchDexoptReason|REASON_INACTIVE */ String reason) {
        var appHibernationManager = mInjector.getAppHibernationManager();
        long now = mInjector.getClock().currentTimeMillis();

        ArtManagerLocal artManager = LocalManagerRegistry.getManager(ArtManagerLocal.class);
        Objects.requireNonNull(artManager);

        Stream<PackageInfo> packages =
                snapshot.getPackageStates()
                        .values()
                        .stream()
                        .filter(pkgState -> {
                            // Filter out hibernating packages even if the reason is REASON_INACTIVE.
                            // This is because artifacts for hibernating packages are already deleted.
                            if (!Utils.canDexoptPackage(pkgState, appHibernationManager)) {
                                return false;
                            }
                            if (reason.equals(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)) {
                                // pre-reboot dexopt runs ART code from next OTA update, it doesn't have access to
                                // system_server code that is called below
                                return true;
                            }

                            List<DexoptStatus.DexContainerFileDexoptStatus> statuses = artManager.getDexoptStatus(snapshot, pkgState.getPackageName())
                                .getDexContainerFileDexoptStatuses();

                            for (var s : statuses) {
                                if (!"speed".equals(s.getCompilerFilter())) {
                                    return true;
                                }
                            }
                            return false;
                        })
                        .map(pkgState
                                -> new PackageInfo(pkgState,
                                        Utils.getPackageLastActiveTime(pkgState,
                                                mInjector.getDexUseManager(),
                                                mInjector.getUserManager()),
                                        Flags.hybridPreRebootDexopt()
                                                ? mInjector.getDexUseManager()
                                                          .calculateDecayedPackageScore(
                                                                  pkgState.getPackageName(), now)
                                                : 0D));

        // "pm.dexopt.downgrade_after_inactive_days" is repurposed to also determine whether to
        // dexopt a package.
        long inactiveMs = TimeUnit.DAYS.toMillis(SystemProperties.getInt(
                "pm.dexopt.downgrade_after_inactive_days", Integer.MAX_VALUE /* def */));
        long currentTimeMs = mInjector.getClock().currentTimeMillis();
        long thresholdTimeMs = currentTimeMs - inactiveMs;

        packages = switch (reason) {
            case ReasonMapping.REASON_BOOT_AFTER_MAINLINE_UPDATE ->
                packages.filter(pkgInfo
                        -> mInjector.isSystemUiPackage(pkgInfo.pkgState().getPackageName())
                                || mInjector.isLauncherPackage(
                                        pkgInfo.pkgState().getPackageName()));
            case ReasonMapping.REASON_INACTIVE ->
                packages.filter(pkgInfo -> pkgInfo.lastActiveTime() <= thresholdTimeMs)
                        .sorted(Comparator.<PackageInfo>comparingDouble(pkgInfo -> pkgInfo.score())
                                        .thenComparingLong(pkgInfo -> pkgInfo.lastActiveTime()));
            // Don't filter the default package list and no need to sort as in some cases the system
            // time can advance during bootup after package installation and cause filtering to
            // exclude all packages when m.dexopt.downgrade_after_inactive_days is set. See
            // aosp/3237478 for more details.
            case ReasonMapping.REASON_FIRST_BOOT -> packages;
            default -> {
                Comparator<PackageInfo> comparator =
                        Comparator.<PackageInfo>comparingDouble(PackageInfo::score)
                                .thenComparingLong(PackageInfo::lastActiveTime)
                                .reversed();
                if (reason.equals(REASON_PRE_REBOOT_DEXOPT_SYNC)) {
                    Set<String> forcedPackages = new HashSet<>();
                    forcedPackages.addAll(Constants.getPreRebootDexoptSyncForcedPackages());
                    forcedPackages.add(Utils.getSystemUiPackageName(mInjector.getContext()));
                    forcedPackages.addAll(Utils.getLauncherPackageNames(mInjector.getContext()));
                    forcedPackages.addAll(Utils.getCameraPackageNames(mInjector.getContext()));

                    comparator = Comparator
                                         .comparing((PackageInfo pkgInfo) -> {
                                             return forcedPackages.contains(
                                                     pkgInfo.pkgState().getPackageName());
                                         })
                                         .reversed()
                                         .thenComparing(comparator);
                }
                yield packages.filter(pkgInfo -> pkgInfo.lastActiveTime() > thresholdTimeMs)
                        .sorted(comparator);
            }
        };

        return packages.map(pkgInfo -> pkgInfo.pkgState().getPackageName()).toList();
    }

    /**
     * Maps the compiler filter string to an integer representation for reporting stats defined in
     * the "framework" module (specifically, the {@code package_optimization_compilation_filter}
     * field of the {@code AppStartOccurred} and {@code AppStartFullyDrawn} protos defined in {@code
     * frameworks/proto_logging/stats/atoms.proto}). The integer is not supposed to be understood by
     * the caller but to be filled as-is into the fields mentioned above.
     *
     * <p>Note that this mapping is different from the one used in the "art" module and must not be
     * used for reporting ART stats (e.g., ART runtime metrics).
     *
     * @param compilerFilter The string obtained from {@link DexFile.OptimizationInfo#getStatus()}.
     */
    public static int getCompilerFilterValueForFrameworkStatsReporting(
            @NonNull String compilerFilter) {
        return switch (compilerFilter) {
            // Reserved -1, 0, 3, 5, 14-27.
            case "unknown" -> 1;
            case "assume-verified" -> 2;
            case "verify" -> 4;
            case "space-profile" -> 6;
            case "space" -> 7;
            case "speed-profile" -> 8;
            case "speed" -> 9;
            case "everything-profile" -> 10;
            case "everything" -> 11;
            case "run-from-apk" -> 12;
            case "run-from-apk-fallback" -> 13;
            default -> 28;
        };
    }

    /**
     * Maps the compilation reason string to an integer representation for reporting stats defined
     * in the "framework" module (specifically, the {@code package_optimization_compilation_reason}
     * field of the {@code AppStartOccurred} and {@code AppStartFullyDrawn} protos defined in {@code
     * frameworks/proto_logging/stats/atoms.proto}). The integer is not supposed to be understood by
     * the caller but to be filled as-is into the fields mentioned above.
     *
     * <p>Note that this mapping is different from the one used in the "art" module and must not be
     * used for reporting ART stats (e.g., ART runtime metrics).
     *
     * @param compilationReason The string obtained from {@link
     *     DexFile.OptimizationInfo#getReason()}.
     */
    public static int getCompilationReasonValueForFrameworkStatsReporting(
            @NonNull String compilationReason) {
        return switch (compilationReason) {
            // Reserved -1, 0, 3, 8, 21.
            case "unknown" -> 1;
            case "first-boot" -> 2;
            case "install" -> 4;
            case "bg-dexopt" -> 5;
            case "ab-ota" -> 6;
            case "inactive" -> 7;
            case "install-dm" -> 9;
            case "install-fast" -> 10;
            case "install-bulk" -> 11;
            case "install-bulk-secondary" -> 12;
            case "install-bulk-downgraded" -> 13;
            case "install-bulk-secondary-downgraded" -> 14;
            case "install-fast-dm" -> 15;
            case "install-bulk-dm" -> 16;
            case "install-bulk-secondary-dm" -> 17;
            case "install-bulk-downgraded-dm" -> 18;
            case "install-bulk-secondary-downgraded-dm" -> 19;
            case "boot-after-ota" -> 20;
            case "cmdline" -> 22;
            case "prebuilt" -> 23;
            case "vdex" -> 24;
            case "boot-after-mainline-update" -> 25;
            case "cloud" -> 26;
            case "vdex-dm" -> 27;
            case "post-ur" -> 29;
            default -> 28;
        };
    }

    private record PackageInfo(PackageState pkgState, long lastActiveTime, double score) {}

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        private final Context mContext;

        Injector(Context context) {
            mContext = context;

            // Call the getters for the dependencies that aren't optional, to ensure correct
            // initialization order.
            getUserManager();
            getDexUseManager();
        }

        public AppHibernationManager getAppHibernationManager() {
            return Objects.requireNonNull(mContext.getSystemService(AppHibernationManager.class));
        }

        public UserManager getUserManager() {
            return Objects.requireNonNull(mContext.getSystemService(UserManager.class));
        }

        public DexUseManagerLocal getDexUseManager() {
            return GlobalInjector.getInstance().getDexUseManager();
        }

        public boolean isSystemUiPackage(@NonNull String packageName) {
            return Utils.isSystemUiPackage(mContext, packageName);
        }

        public boolean isLauncherPackage(@NonNull String packageName) {
            return Utils.isLauncherPackage(mContext, packageName);
        }

        public Context getContext() {
            return mContext;
        }

        public Clock getClock() {
            return Clock.DEFAULT;
        }
    }
}
