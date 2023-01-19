/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.android.server.art.wrapper;

import android.annotation.NonNull;

import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.SharedLibrary;

import java.util.List;

/** @hide */
public class PackageStateWrapper {
    @SuppressWarnings("unchecked")
    @NonNull
    public static List<SharedLibrary> getSharedLibraryDependencies(
            @NonNull PackageState packageState) {
        try {
            return (List<SharedLibrary>) packageState.getClass()
                    .getMethod("getSharedLibraryDependencies")
                    .invoke(packageState);
        } catch (ReflectiveOperationException ignored) {
            try {
                return (List<SharedLibrary>) packageState.getClass()
                        .getMethod("getUsesLibraries")
                        .invoke(packageState);
            } catch (ReflectiveOperationException e) {
                throw new RuntimeException(e);
            }
        }
    }
}
