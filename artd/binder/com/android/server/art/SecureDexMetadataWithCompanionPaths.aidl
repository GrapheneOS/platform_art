/*
 * Copyright (C) 2025 The Android Open Source Project
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
 * Represents the paths to a secure dex metadata (SDM) file and its companion (SDC) file.
 *
 * @hide
 */
parcelable SecureDexMetadataWithCompanionPaths {
    /**
     * The absolute path starting with '/' to the dex file that the SDM file is next to.
     */
    @utf8InCpp String dexPath;
    /** The instruction set of the dexopt artifacts. */
    @utf8InCpp String isa;
    /**
     * Whether the SDC file in the dalvik-cache folder. This is true typically when the app is in
     * incremental-fs.
     *
     * Only applicable to the SDC file, because the SDM file is installed with the app and therefore
     * always to the dex file regardlessly.
     */
    boolean isInDalvikCache;
}
