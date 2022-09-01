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
     * Deletes artifacts and returns the released space, in bytes.
     *
     * Throws fatal errors. Logs and ignores non-fatal errors.
     */
    long deleteArtifacts(in com.android.server.art.ArtifactsPath artifactsPath);

    /**
     * Returns the optimization status of a dex file.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.GetOptimizationStatusResult getOptimizationStatus(
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @utf8InCpp String classLoaderContext);

    /**
     * Returns true if dexopt is needed. `dexoptTrigger` is a bit field that consists of values
     * defined in `com.android.server.art.DexoptTrigger`.
     *
     * Throws fatal and non-fatal errors.
     */
    com.android.server.art.GetDexoptNeededResult getDexoptNeeded(
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @utf8InCpp String classLoaderContext, @utf8InCpp String compilerFilter,
            int dexoptTrigger);

    /**
     * Dexopts a dex file for the given instruction set. Returns true on success, or false if
     * cancelled.
     *
     * Throws fatal and non-fatal errors.
     */
    boolean dexopt(in com.android.server.art.OutputArtifacts outputArtifacts,
            @utf8InCpp String dexFile, @utf8InCpp String instructionSet,
            @utf8InCpp String classLoaderContext, @utf8InCpp String compilerFilter,
            in @nullable com.android.server.art.ProfilePath profile,
            in @nullable com.android.server.art.VdexPath inputVdex,
            com.android.server.art.PriorityClass priorityClass,
            in com.android.server.art.DexoptOptions dexoptOptions);
}
