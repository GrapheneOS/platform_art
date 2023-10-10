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

/**
 * The result of {@code IArtd.getDexoptNeeded}.
 *
 * @hide
 */
parcelable GetDexoptNeededResult {
    /** Whether dexopt is needed. */
    boolean isDexoptNeeded;
    /** Whether there is a usable VDEX file. Note that this can be true even if dexopt is needed. */
    boolean isVdexUsable;
    /** The location of the best usable artifacts. */
    ArtifactsLocation artifactsLocation = ArtifactsLocation.NONE_OR_ERROR;
    /**
     * True if the dex file has dex code. (The dex file is a .jar/.apk file that has .dex entries,
     * or is a .dex file.) False otherwise. (The dex file is a .jar/.apk file that has no .dex
     * entries.)
     */
    boolean hasDexCode;

    @Backing(type="int")
    enum ArtifactsLocation {
        /** No usable artifacts. */
        NONE_OR_ERROR = 0,
        /** In the global "dalvik-cache" folder. */
        DALVIK_CACHE = 1,
        /** In the "oat" folder next to the dex file. */
        NEXT_TO_DEX = 2,
        /** In the dex metadata file. This means the only usable artifact is the VDEX file. */
        DM = 3,
    }
}
