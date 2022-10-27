
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

import com.android.server.pm.pkg.AndroidPackage;

/** @hide */
public class PackageState {
    @NonNull private final com.android.server.pm.pkg.PackageState mPkgState;

    public PackageState(@NonNull com.android.server.pm.pkg.PackageState pkgState) {
        mPkgState = pkgState;
    }

    @Nullable
    public String getVolumeUuid() {
        try {
            return (String) mPkgState.getClass().getMethod("getVolumeUuid").invoke(mPkgState);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @Nullable
    public String getSeInfo() {
        try {
            Object pkgStateUnserialized =
                    mPkgState.getClass().getMethod("getTransientState").invoke(mPkgState);
            String seInfo = (String) pkgStateUnserialized.getClass()
                                    .getMethod("getOverrideSeInfo")
                                    .invoke(pkgStateUnserialized);
            if (!TextUtils.isEmpty(seInfo)) {
                return seInfo;
            }

            // Default to the information in `AndroidPackage`. The defaulting behavior will
            // eventually be done by `PackageState` internally.
            AndroidPackage pkg = mPkgState.getAndroidPackage();
            if (pkg == null) {
                // This should never happen because we check the existence of the package at the
                // beginning of each ART Services method.
                throw new IllegalStateException("Unable to get package "
                        + mPkgState.getPackageName() + ". This should never happen.");
            }

            return (String) pkg.getClass().getMethod("getSeInfo").invoke(pkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
