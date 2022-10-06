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
import android.text.TextUtils;

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

    @Nullable
    public String getPrimaryCpuAbi() {
        try {
            String abi =
                    (String) mPkgState.getClass().getMethod("getPrimaryCpuAbi").invoke(mPkgState);
            if (!TextUtils.isEmpty(abi)) {
                return abi;
            }

            // Default to the information in `AndroidPackageApi`. The defaulting behavior will
            // eventually be done by `PackageState` internally.
            AndroidPackageApi pkg = getAndroidPackage();
            if (pkg == null) {
                // This should never happen because we check the existence of the package at the
                // beginning of each ART Services method.
                throw new IllegalStateException("Unable to get package " + getPackageName()
                        + ". This should never happen.");
            }

            return (String) pkg.getRealInstance()
                    .getClass()
                    .getMethod("getPrimaryCpuAbi")
                    .invoke(pkg.getRealInstance());
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @Nullable
    public String getSecondaryCpuAbi() {
        try {
            String abi =
                    (String) mPkgState.getClass().getMethod("getSecondaryCpuAbi").invoke(mPkgState);
            if (!TextUtils.isEmpty(abi)) {
                return abi;
            }

            // Default to the information in `AndroidPackageApi`. The defaulting behavior will
            // eventually be done by `PackageState` internally.
            AndroidPackageApi pkg = getAndroidPackage();
            if (pkg == null) {
                // This should never happen because we check the existence of the package at the
                // beginning of each ART Services method.
                throw new IllegalStateException("Unable to get package " + getPackageName()
                        + ". This should never happen.");
            }

            return (String) pkg.getRealInstance()
                    .getClass()
                    .getMethod("getSecondaryCpuAbi")
                    .invoke(pkg.getRealInstance());
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    public boolean isSystem() {
        try {
            return (boolean) mPkgState.getClass().getMethod("isSystem").invoke(mPkgState);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    public boolean isUpdatedSystemApp() {
        try {
            Object packageStateUnserialized =
                    mPkgState.getClass().getMethod("getTransientState").invoke(mPkgState);
            return (boolean) packageStateUnserialized.getClass()
                    .getMethod("isUpdatedSystemApp")
                    .invoke(packageStateUnserialized);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public PackageUserState getUserStateOrDefault(int userId) {
        try {
            Object userState = mPkgState.getClass()
                                       .getMethod("getUserStateOrDefault", int.class)
                                       .invoke(mPkgState, userId);
            return userState != null ? new PackageUserState(userState) : null;
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
