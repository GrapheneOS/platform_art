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

package com.android.server.art.wrapper;

import android.annotation.NonNull;
import android.annotation.Nullable;

import java.util.List;
import java.util.stream.Collectors;

/** @hide */
public class PackageState {
    private final Object mPkgState;

    PackageState(@NonNull Object pkgState) {
        mPkgState = pkgState;
    }

    @Nullable
    public AndroidPackageApi getAndroidPackage() {
        try {
            Object pkg = mPkgState.getClass().getMethod("getAndroidPackage").invoke(mPkgState);
            return pkg != null ? new AndroidPackageApi(pkg) : null;
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public String getPackageName() {
        try {
            return (String) mPkgState.getClass().getMethod("getPackageName").invoke(mPkgState);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public List<SharedLibraryInfo> getUsesLibraryInfos() {
        try {
            Object packageStateUnserialized =
                    mPkgState.getClass().getMethod("getTransientState").invoke(mPkgState);
            var list = (List<?>) packageStateUnserialized.getClass()
                               .getMethod("getUsesLibraryInfos")
                               .invoke(packageStateUnserialized);
            return list.stream()
                    .map(obj -> new SharedLibraryInfo(obj))
                    .collect(Collectors.toList());
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
