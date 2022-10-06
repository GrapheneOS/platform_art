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
 * Represents the path to a profile file.
 *
 * @hide
 */
union ProfilePath {
    RefProfilePath refProfilePath;
    TmpRefProfilePath tmpRefProfilePath;
    PrebuiltProfilePath prebuiltProfilePath;
    CurProfilePath curProfilePath;
    /** Represents a profile in the dex metadata file. */
    com.android.server.art.DexMetadataPath dexMetadataPath;

    /** Represents a reference profile. */
    parcelable RefProfilePath {
        /** The name of the package. */
        @utf8InCpp String packageName;
        /** The stem of the profile file */
        @utf8InCpp String profileName;
    }

    /** Represents a temporary reference profile. */
    parcelable TmpRefProfilePath {
        /** The reference profile that this temporary file is for. */
        RefProfilePath refProfilePath;
        /** A unique identifier to distinguish this temporary file from others. Filled by artd. */
        @utf8InCpp String id;
    }

    /**
     * Represents a profile next to a dex file. This is usually a prebuilt profile in the system
     * image, but it can also be a profile that package manager can potentially put along with the
     * APK during installation. The latter one is not officially supported by package manager, but
     * OEMs can customize package manager to support that.
     */
    parcelable PrebuiltProfilePath {
        /** The path to the dex file that the profile is next to. */
        @utf8InCpp String dexPath;
    }

    /** Represents a current profile. */
    parcelable CurProfilePath {
        /** The user ID of the user that owns the profile. */
        int userId;
        /** The name of the package. */
        @utf8InCpp String packageName;
        /** The stem of the profile file */
        @utf8InCpp String profileName;
    }
}
