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

package com.android.server.art.model;

/** @hide */
public class DeleteOptions {
    public static final class Builder {
        private DeleteOptions mDeleteOptions = new DeleteOptions();

        /** Whether to delete optimized artifacts for primary dex'es. Default: true. */
        public Builder setForPrimaryDex(boolean value) {
            mDeleteOptions.mIsForPrimaryDex = value;
            return this;
        }

        /** Whether to delete optimized artifacts for secondary dex'es. Default: false. */
        public Builder setForSecondaryDex(boolean value) {
            mDeleteOptions.mIsForSecondaryDex = value;
            return this;
        }

        /** Returns the built object. */
        public DeleteOptions build() {
            return mDeleteOptions;
        }
    }

    private boolean mIsForPrimaryDex = true;
    private boolean mIsForSecondaryDex = false;

    private DeleteOptions() {}

    /** Whether to delete optimized artifacts for primary dex'es. */
    public boolean isForPrimaryDex() {
        return mIsForPrimaryDex;
    }

    /** Whether to delete optimized artifacts for secondary dex'es. */
    public boolean isForSecondaryDex() {
        return mIsForSecondaryDex;
    }
}
