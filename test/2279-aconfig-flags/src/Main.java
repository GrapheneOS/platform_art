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

import dalvik.system.VMRuntime;

public class Main {
    public static void main(String[] args) {
        // Test a flag in libcore/libcore.aconfig.
        if (!isVTrunkStableFlagEnabled()) {
            throw new AssertionError(
                    "The value of com.android.libcore.v_apis flag is expected to be true.");
        }

        // Test a flag in art/build/flags/art-flags.aconfig.
        if (!isArtTestFlagEnabled()) {
            throw new AssertionError(
                    "The value of com.android.art.flags.test flag is expected to be true.");
        }

        // TODO: Ramp the flag fully and assert the value.
        isArtTestRwFlagEnabled();
    }

    private static boolean isVTrunkStableFlagEnabled() {
        // The Flags class definition is expected to be in core-libart.jar.
        return com.android.libcore.Flags.vApis();
    }

    private static boolean isArtTestFlagEnabled() {
        // The Flags class definition is expected to be in core-libart.jar.
        return com.android.art.flags.Flags.test();
    }


    private static boolean isArtTestRwFlagEnabled() {
        return VMRuntime.isArtTestRwFlagEnabled();
    }
}
