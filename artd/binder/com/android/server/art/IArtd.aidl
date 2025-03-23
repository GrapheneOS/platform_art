/*
 * Copyright (C) 2021 The Android Open Source Project
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

/** @hide */
interface IArtd {
    // Test to see if the artd service is available.
    boolean isAlive();

    /**
     * Deletes dexopt artifacts and returns the released space, in bytes.
     *
     * Note that this method doesn't delete runtime artifacts. To delete them, call
     * `deleteRuntimeArtifacts`.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long deleteArtifacts(in com.android.server.art.ArtifactsPath artifactsPath);

    /**
     * Returns the dexopt status of a dex file.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.GetDexoptStatusResult getDexoptStatus(
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @nullable @utf8InCpp String classLoaderContext);

    /**
     * Returns true if the profile exists and contains entries for the given dex file.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean isProfileUsable(in com.android.server.art.ProfilePath profile,
            @utf8InCpp String dexFile);

    /**
     * Copies the profile and rewrites it for the given dex file. Returns `SUCCESS` and fills
     * `dst.profilePath.id` if the operation succeeds and `src` exists and contains entries that
     * match the given dex file.
     *
     * Throws fatal and non-fatal errors, except if the input is a bad profile.
     */
    com.android.server.art.CopyAndRewriteProfileResult copyAndRewriteProfile(
            in com.android.server.art.ProfilePath src,
            inout com.android.server.art.OutputProfile dst, @utf8InCpp String dexFile);

    /**
     * Similar to above. The difference is that the profile is not taken from a separate file but
     * taken from `dexFile` itself. Specifically, if `dexFile` is a zip file, the profile is taken
     * from `assets/art-profile/baseline.prof` in the zip. Returns `NO_PROFILE` if `dexFile` is not
     * a zip file or it doesn't contain a profile.
     */
    com.android.server.art.CopyAndRewriteProfileResult copyAndRewriteEmbeddedProfile(
            inout com.android.server.art.OutputProfile dst, @utf8InCpp String dexFile);

    /**
     * Moves the temporary profile to the permanent location.
     *
     * Throws fatal and non-fatal errors.
     */
    void commitTmpProfile(in com.android.server.art.ProfilePath.TmpProfilePath profile);

    /**
     * Deletes the profile. Does nothing of the profile doesn't exist.
     *
     * Operates on the whole DM file if given one.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    void deleteProfile(in com.android.server.art.ProfilePath profile);

    /**
     * Returns the visibility of the profile.
     *
     * Operates on the whole DM file if given one.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.FileVisibility getProfileVisibility(
            in com.android.server.art.ProfilePath profile);

    /**
     * Merges profiles. Both `profiles` and `referenceProfile` are inputs, while the difference is
     * that `referenceProfile` is also used as the reference to calculate the diff. `profiles` that
     * don't exist are skipped, while `referenceProfile`, if provided, must exist. Returns true,
     * writes the merge result to `outputProfile` and fills `outputProfile.profilePath.id` and
     * `outputProfile.profilePath.tmpPath` if a merge has been performed.
     *
     * When `options.forceMerge`, `options.dumpOnly`, or `options.dumpClassesAndMethods` is set,
     * `referenceProfile` must not be set. I.e., all inputs must be provided by `profiles`. This is
     * because the merge will always happen, and hence no reference profile is needed to calculate
     * the diff.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean mergeProfiles(in List<com.android.server.art.ProfilePath> profiles,
            in @nullable com.android.server.art.ProfilePath referenceProfile,
            inout com.android.server.art.OutputProfile outputProfile,
            in @utf8InCpp List<String> dexFiles,
            in com.android.server.art.MergeProfileOptions options);

    /**
     * Returns the visibility of the artifacts.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.FileVisibility getArtifactsVisibility(
            in com.android.server.art.ArtifactsPath artifactsPath);

    /**
     * Returns the visibility of the dex file.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.FileVisibility getDexFileVisibility(@utf8InCpp String dexFile);

    /**
     * Returns the visibility of the DM file.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.FileVisibility getDmFileVisibility(
            in com.android.server.art.DexMetadataPath dmFile);

    /**
     * Returns true if dexopt is needed. `dexoptTrigger` is a bit field that consists of values
     * defined in `com.android.server.art.DexoptTrigger`.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.GetDexoptNeededResult getDexoptNeeded(
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @nullable @utf8InCpp String classLoaderContext, @utf8InCpp String compilerFilter,
            int dexoptTrigger);

    /**
     * Creates a secure dex metadata companion (SDC) file for the secure dex metadata (SDM) file, if
     * the SDM file exists while the SDC file doesn't exist (meaning the SDM file is seen the first
     * time).
     *
     * Throws fatal and non-fatal errors.
     */
    void maybeCreateSdc(in com.android.server.art.OutputSecureDexMetadataCompanion outputSdc);

    /**
     * Dexopts a dex file for the given instruction set.
     *
     * Throws fatal and non-fatal errors. When dexopt fails, the non-fatal status includes an error
     * message containing a substring formatted as:
     * [status=%STATUS%,exit_code=%EXIT_CODE%,signal=%SIGNAL%]
     * where %STATUS% is the integer value of the corresponding ExecResultStatus enumeration defined
     * in frameworks/proto_logging/stats/enums/art/common_enums.proto,
     * %EXIT_CODE% is the exit code for the dex2oat process and set only when %STATUS% is set to
     * EXEC_RESULT_STATUS_EXITED (-1 otherwsie),
     * and %SIGNAL% is the signal that interrupted the dex2oat
     * process and set only when %STATUS% is EXEC_RESULT_STATUS_SIGNALED (0 otherwise).
     *
     * The purpose of this format is to share information about the dex2oat run result with the
     * ArtService code in Java that orchestrates the dexopt process, so that it can be reported to
     * StatsD.
     */
    com.android.server.art.ArtdDexoptResult dexopt(
            in com.android.server.art.OutputArtifacts outputArtifacts,
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @nullable @utf8InCpp String classLoaderContext, @utf8InCpp String compilerFilter,
            in @nullable com.android.server.art.ProfilePath profile,
            in @nullable com.android.server.art.VdexPath inputVdex,
            in @nullable com.android.server.art.DexMetadataPath dmFile,
            com.android.server.art.PriorityClass priorityClass,
            in com.android.server.art.DexoptOptions dexoptOptions,
            in com.android.server.art.IArtdCancellationSignal cancellationSignal);

    /**
     * Returns a cancellation signal which can be used to cancel {@code dexopt} calls.
     */
    com.android.server.art.IArtdCancellationSignal createCancellationSignal();

    /**
     * Deletes all files that are managed by artd, except those specified in the arguments. Returns
     * the size of the freed space, in bytes.
     *
     * For each entry in `artifactsToKeep`, all three kinds of artifacts (ODEX, VDEX, ART) are
     * kept. For each entry in `vdexFilesToKeep`, only the VDEX file will be kept. Note that VDEX
     * files included in `artifactsToKeep` don't have to be listed in `vdexFilesToKeep`.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long cleanup(in List<com.android.server.art.ProfilePath> profilesToKeep,
            in List<com.android.server.art.ArtifactsPath> artifactsToKeep,
            in List<com.android.server.art.VdexPath> vdexFilesToKeep,
            in List<com.android.server.art.SecureDexMetadataWithCompanionPaths> SdmSdcFilesToKeep,
            in List<com.android.server.art.RuntimeArtifactsPath> runtimeArtifactsToKeep,
            boolean keepPreRebootStagedFiles);

    /**
     * Deletes all Pre-reboot staged files.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    void cleanUpPreRebootStagedFiles();

    /**
     * Returns whether the artifacts of the primary dex files should be in the global dalvik-cache
     * directory.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean isInDalvikCache(@utf8InCpp String dexFile);

    /**
     * Deletes the SDM and SDC files and returns the released space, in bytes.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long deleteSdmSdcFiles(
            in com.android.server.art.SecureDexMetadataWithCompanionPaths sdmSdcPaths);

    /**
     * Deletes runtime artifacts and returns the released space, in bytes.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long deleteRuntimeArtifacts(
            in com.android.server.art.RuntimeArtifactsPath runtimeArtifactsPath);

    /**
     * Returns the size of the dexopt artifacts, in bytes, or 0 if they don't exist or a non-fatal
     * error occurred.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long getArtifactsSize(in com.android.server.art.ArtifactsPath artifactsPath);

    /**
     * Returns the size of the vdex file, in bytes, or 0 if it doesn't exist or a non-fatal error
     * occurred.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long getVdexFileSize(in com.android.server.art.VdexPath vdexPath);

    /**
     * Returns the size of the SDM file, in bytes, or 0 if it doesn't exist or a non-fatal error
     * occurred.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long getSdmFileSize(in com.android.server.art.SecureDexMetadataWithCompanionPaths sdmPath);

    /**
     * Returns the size of the runtime artifacts, in bytes, or 0 if they don't exist or a non-fatal
     * error occurred.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long getRuntimeArtifactsSize(
            in com.android.server.art.RuntimeArtifactsPath runtimeArtifactsPath);

    /**
     * Returns the size of the profile, in bytes, or 0 if it doesn't exist or a non-fatal error
     * occurred.
     *
     * Operates on the whole DM file if given one.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long getProfileSize(in com.android.server.art.ProfilePath profile);

    /**
     * Returns a notification handle to wait for a process to finish profile save
     * ({@code ProfileCompilationInfo::Save}).
     *
     * A forced profile save can be triggered by sending {@code SIGUSR1} to the process.
     *
     * Throws fatal and non-fatal errors.
     *
     * Not supported in Pre-reboot Dexopt mode.
     */
    @PropagateAllowBlocking
    com.android.server.art.IArtdNotification initProfileSaveNotification(
            in com.android.server.art.ProfilePath.PrimaryCurProfilePath profilePath, int pid);

    /**
     * Moves the staged files of the given artifacts and profiles to the permanent locations,
     * replacing old files if they exist. Removes the staged files and restores the old files at
     * best effort if any error occurs.
     *
     * This is intended to be called for a superset of the packages that we actually expect to have
     * staged files, so missing files are expected.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal and non-fatal errors.
     *
     * @return true if any file has been committed.
     */
    boolean commitPreRebootStagedFiles(
            in List<com.android.server.art.ArtifactsPath> artifacts,
            in List<com.android.server.art.ProfilePath.WritableProfilePath> profiles);

    /**
     * Returns whether the old system and the new system meet the requirements to run Pre-reboot
     * Dexopt. This method can only be called with a chroot dir set up by
     * {@link IDexoptChrootSetup#setUp}.
     *
     * Not supported in Pre-reboot Dexopt mode.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean checkPreRebootSystemRequirements(@utf8InCpp String chrootDir);

    // The methods below are only for Pre-reboot Dexopt and only supported in Pre-reboot Dexopt
    // mode.

    /**
     * Initializes the environment for Pre-reboot Dexopt. This operation includes initializing
     * environment variables and boot images. Returns true on success, or false on cancellation.
     * Throws on failure.
     *
     * Note that this method results in a non-persistent state change, so it must be called every
     * time a new instance of artd is started for Pre-reboot Dexopt.
     *
     * On the first call to this method, a cancellation signal must be passed through the {@code
     * cancellationSignal} parameter. The cancellation signal can then be used for cancelling the
     * first call. On subsequent calls to this method, the {@code cancellationSignal} parameter is
     * ignored.
     *
     * After cancellation or failure, the environment will not be usable for Pre-reboot Dexopt, and
     * this operation cannot be retried.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean preRebootInit(
            in @nullable com.android.server.art.IArtdCancellationSignal cancellationSignal);

    /** For Pre-reboot Dexopt use. See {@link ArtJni#validateDexPath}. */
    @nullable @utf8InCpp String validateDexPath(@utf8InCpp String dexFile);

    /** For Pre-reboot Dexopt use. See {@link ArtJni#validateClassLoaderContext}. */
    @nullable @utf8InCpp String validateClassLoaderContext(
            @utf8InCpp String dexFile, @utf8InCpp String classLoaderContext);
}
