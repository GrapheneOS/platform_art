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

import android.os.IBinder;
import android.util.Log;

/**
 * An implementation of artd that logs the artd calls for debugging purposes.
 *
 * @hide
 */
public class LoggingArtd implements IArtd {
    private static final String TAG = "LoggingArtd";

    @Override
    public IBinder asBinder() {
        return null;
    }

    @Override
    public boolean isAlive() {
        return true;
    }

    @Override
    public long deleteArtifacts(ArtifactsPath artifactsPath) {
        Log.i(TAG, "deleteArtifacts " + artifactsPathToString(artifactsPath));
        return 0;
    }

    @Override
    public GetOptimizationStatusResult getOptimizationStatus(
            String dexFile, String instructionSet, String classLoaderContext) {
        Log.i(TAG,
                "getOptimizationStatus " + dexFile + ", " + instructionSet + ", "
                        + classLoaderContext);
        return new GetOptimizationStatusResult();
    }

    private String artifactsPathToString(ArtifactsPath artifactsPath) {
        return String.format("ArtifactsPath{dexPath = \"%s\", isa = \"%s\", isInDalvikCache = %s}",
                artifactsPath.dexPath, artifactsPath.isa,
                String.valueOf(artifactsPath.isInDalvikCache));
    }
}
