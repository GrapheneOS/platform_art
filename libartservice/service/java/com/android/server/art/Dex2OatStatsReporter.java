/*
 * Copyright (C) 2024 The Android Open Source Project
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

import static com.android.server.art.Utils.Abi;

import android.os.Build;

import androidx.annotation.NonNull;
import androidx.annotation.RequiresApi;

import com.android.server.art.model.DetailedDexInfo;
import com.android.server.art.model.DexMetadata;
import com.android.server.art.model.DexoptParams;

import java.util.List;

/**
 * A class to report dex2oat metrics to StatsD.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class Dex2OatStatsReporter {
    public static void report(int appId, @NonNull String compilerFilter,
            @NonNull String compilationReason, @DexMetadata.Type int dexMetadataType,
            @NonNull DetailedDexInfo dexInfo, @NonNull String isa, @NonNull Dex2OatResult result,
            long artifactsSize, long compilationTime) {
        ArtStatsLog.write(ArtStatsLog.ART_DEX2OAT_REPORTED, appId,
                translateCompilerFilter(compilerFilter),
                translateCompilationReason(compilationReason),
                translateDexMetadataType(dexMetadataType), getApkType(dexInfo), translateIsa(isa),
                result.status, result.exitCode, result.signal, (int) (artifactsSize / 1024),
                (int) compilationTime);
    }

    public static void reportSkipped(int appId, @NonNull String compilationReason,
            @DexMetadata.Type int dexMetadataType, @NonNull DetailedDexInfo dexInfo,
            @NonNull List<Abi> abis) {
        Dex2OatResult skipped = Dex2OatResult.notRun();

        for (Abi abi : abis) {
            ArtStatsLog.write(ArtStatsLog.ART_DEX2OAT_REPORTED, appId,
                    translateCompilerFilter(DexoptParams.COMPILER_FILTER_NOOP),
                    translateCompilationReason(compilationReason), dexMetadataType,
                    getApkType(dexInfo), translateIsa(abi.isa()), skipped.status, skipped.exitCode,
                    skipped.signal,
                    0, // artifacts size
                    0 // compilation time
            );
        }
    }

    private static int translateCompilerFilter(String compilerFilter) {
        return switch (compilerFilter) {
            case "assume-verified" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_ASSUMED_VERIFIED;
            case "verify" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_VERIFY;
            case "space-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPACE_PROFILE;
            case "space" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPACE;
            case "speed-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPEED_PROFILE;
            case "speed" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SPEED;
            case "everything-profile" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_EVERYTHING_PROFILE;
            case "everything" ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_EVERYTHING;
            case "skip" ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_SKIP;
            default ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__COMPILER_FILTER__ART_COMPILATION_FILTER_UNKNOWN;
        };
    }

    private static int translateCompilationReason(String compilationReason) {
        return switch (compilationReason) {
            case ReasonMapping.REASON_FIRST_BOOT ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_FIRST_BOOT;
            case ReasonMapping.REASON_BOOT_AFTER_OTA ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BOOT_AFTER_OTA;
            case ReasonMapping.REASON_BOOT_AFTER_MAINLINE_UPDATE ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BOOT_AFTER_MAINLINE_UPDATE;
            case ReasonMapping.REASON_INSTALL ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL;
            case ReasonMapping.REASON_BG_DEXOPT ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_BG_DEXOPT;
            case ReasonMapping.REASON_CMDLINE ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_CMDLINE;
            case ReasonMapping.REASON_INACTIVE ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INACTIVE;
            case ReasonMapping.REASON_PRE_REBOOT_DEXOPT ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_AB_OTA;
            case ReasonMapping.REASON_INSTALL_FAST ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_FAST;
            case ReasonMapping.REASON_INSTALL_BULK ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK;
            case ReasonMapping.REASON_INSTALL_BULK_SECONDARY ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_SECONDARY;
            case ReasonMapping.REASON_INSTALL_BULK_DOWNGRADED ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_DOWNGRADED;
            case ReasonMapping.REASON_INSTALL_BULK_SECONDARY_DOWNGRADED ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_INSTALL_BULK_SECONDARY_DOWNGRADED;
            default ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__COMPILATION_REASON__ART_COMPILATION_REASON_UNKNOWN;
        };
    }

    private static int translateIsa(String isa) {
        return switch (isa) {
            case "arm" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_ARM;
            case "arm64" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_ARM64;
            case "riscv64" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_RISCV64;
            case "x86" -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_X86;
            case "x86_64" -> ArtStatsLog.ART_DATUM_DELTA_REPORTED__ISA__ART_ISA_X86_64;
            default -> ArtStatsLog.ART_DEX2_OAT_REPORTED__ISA__ART_ISA_UNKNOWN;
        };
    }

    private static int translateDexMetadataType(@DexMetadata.Type int dexMetadataType) {
        return switch (dexMetadataType) {
            case DexMetadata.TYPE_PROFILE ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_PROFILE;
            case DexMetadata.TYPE_VDEX ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_VDEX;
            case DexMetadata.TYPE_PROFILE_AND_VDEX ->
                ArtStatsLog
                        .ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_PROFILE_AND_VDEX;
            case DexMetadata.TYPE_NONE ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_NONE;
            case DexMetadata.TYPE_ERROR ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_ERROR;
            case DexMetadata.TYPE_UNKNOWN ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_UNKNOWN;
            default ->
                ArtStatsLog.ART_DEX2_OAT_REPORTED__DEX_METADATA_TYPE__ART_DEX_METADATA_TYPE_UNKNOWN;
        };
    }

    private static int getApkType(DetailedDexInfo dexInfo) {
        if (dexInfo instanceof PrimaryDexUtils.PrimaryDexInfo primaryDexInfo) {
            return primaryDexInfo.splitName() == null
                    ? ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_BASE
                    : ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_SPLIT;
        } else if (dexInfo instanceof DexUseManagerLocal.CheckedSecondaryDexInfo) {
            return ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_SECONDARY;
        }
        return ArtStatsLog.ART_DEX2_OAT_REPORTED__APK_TYPE__ART_APK_TYPE_UNKNOWN;
    }

    public record Dex2OatResult(int status, int exitCode, int signal) {
        public static Dex2OatResult notRun() {
            return new Dex2OatResult(
                    ArtStatsLog.ART_DEX2_OAT_REPORTED__RESULT_STATUS__EXEC_RESULT_STATUS_NOT_RUN,
                    -1 /* exitCode */, 0 /* signal */);
        }

        public static Dex2OatResult exited(int exitCode) {
            return new Dex2OatResult(
                    ArtStatsLog.ART_DEX2_OAT_REPORTED__RESULT_STATUS__EXEC_RESULT_STATUS_EXITED,
                    exitCode, 0 /* signal */);
        }

        public static Dex2OatResult cancelled() {
            return new Dex2OatResult(
                    ArtStatsLog.ART_DEX2_OAT_REPORTED__RESULT_STATUS__EXEC_RESULT_STATUS_CANCELLED,
                    -1 /* exitCode */, 0 /* signal */);
        }
    }
}
