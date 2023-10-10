package com.android.server.art;

import android.annotation.NonNull;
import android.app.job.JobParameters;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.server.art.model.DexoptResult;

import dalvik.system.DexFile;

import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

/**
 * This is an helper class to report the background DexOpt job metrics to StatsD.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class BackgroundDexoptJobStatsReporter {
    public static void reportFailure() {
        ArtStatsLog.write(ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED,
                ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_FATAL_ERROR,
                JobParameters.STOP_REASON_UNDEFINED, 0 /* durationMs */, 0 /* deprecated */,
                0 /* optimizedPackagesCount */, 0 /* packagesDependingOnBootClasspathCount */,
                0 /* totalPackagesCount */);
    }

    public static void reportSuccess(@NonNull BackgroundDexoptJob.CompletedResult completedResult,
            Optional<Integer> stopReason) {
        List<DexoptResult.PackageDexoptResult> packageDexoptResults =
                getFilteredPackageResults(completedResult);
        ArtStatsLog.write(ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED,
                getStatusForStats(completedResult, stopReason),
                stopReason.orElse(JobParameters.STOP_REASON_UNDEFINED),
                completedResult.durationMs(), 0 /* deprecated */,
                getDexoptedPackagesCount(packageDexoptResults),
                getPackagesDependingOnBootClasspathCount(packageDexoptResults),
                packageDexoptResults.size());
    }

    @NonNull
    private static List<DexoptResult.PackageDexoptResult> getFilteredPackageResults(
            @NonNull BackgroundDexoptJob.CompletedResult completedResult) {
        return completedResult.dexoptResult()
                .getPackageDexoptResults()
                .stream()
                .filter(packageResult
                        -> packageResult.getDexContainerFileDexoptResults().stream().anyMatch(
                                fileResult
                                -> (fileResult.getExtendedStatusFlags()
                                           & DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE)
                                        == 0))
                .collect(Collectors.toList());
    }

    private static int getStatusForStats(
            @NonNull BackgroundDexoptJob.CompletedResult result, Optional<Integer> stopReason) {
        if (result.dexoptResult().getFinalStatus() == DexoptResult.DEXOPT_CANCELLED) {
            if (stopReason.isPresent()) {
                return ArtStatsLog
                        .BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_BY_CANCELLATION;
            } else {
                return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_BY_API;
            }
        }

        boolean isSkippedDueToStorageLow =
                result.dexoptResult()
                        .getPackageDexoptResults()
                        .stream()
                        .flatMap(packageResult
                                -> packageResult.getDexContainerFileDexoptResults().stream())
                        .anyMatch(fileResult
                                -> (fileResult.getExtendedStatusFlags()
                                           & DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW)
                                        != 0);
        if (isSkippedDueToStorageLow) {
            return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_ABORT_NO_SPACE_LEFT;
        }

        return ArtStatsLog.BACKGROUND_DEXOPT_JOB_ENDED__STATUS__STATUS_JOB_FINISHED;
    }

    private static int getDexoptedPackagesCount(
            @NonNull List<DexoptResult.PackageDexoptResult> packageResults) {
        return (int) packageResults.stream()
                .filter(result -> result.getStatus() == DexoptResult.DEXOPT_PERFORMED)
                .count();
    }

    private static int getPackagesDependingOnBootClasspathCount(
            @NonNull List<DexoptResult.PackageDexoptResult> packageResults) {
        return (int) packageResults.stream()
                .map(DexoptResult.PackageDexoptResult::getDexContainerFileDexoptResults)
                .filter(BackgroundDexoptJobStatsReporter::isDependentOnBootClasspath)
                .count();
    }

    private static boolean isDependentOnBootClasspath(
            @NonNull List<DexoptResult.DexContainerFileDexoptResult> filesResults) {
        return filesResults.stream()
                .map(DexoptResult.DexContainerFileDexoptResult::getActualCompilerFilter)
                .anyMatch(DexFile::isOptimizedCompilerFilter);
    }
}
