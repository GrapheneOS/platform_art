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
import android.os.UserHandle;

import com.android.server.pm.snapshot.PackageDataSnapshot;

/** @hide */
public class PackageManagerLocal {
    private final Object mPackageManagerInternal;

    /**
     * Returns an instance this class, which is a reflection-based implementation of {@link
     * com.android.server.pm.PackageManagerLocal}.
     * Note: This is NOT a real system API! Use {@link LocalManagerRegistry} for getting a real
     * instance.
     */
    @NonNull
    public static PackageManagerLocal getInstance() throws Exception {
        Class<?> localServicesClass = Class.forName("com.android.server.LocalServices");
        Class<?> packageManagerInternalClass =
                Class.forName("android.content.pm.PackageManagerInternal");
        Object packageManagerInternal = localServicesClass.getMethod("getService", Class.class)
                                                .invoke(null, packageManagerInternalClass);
        if (packageManagerInternal == null) {
            throw new Exception("Failed to get PackageManagerInternal");
        }
        return new PackageManagerLocal(packageManagerInternal);
    }

    private PackageManagerLocal(@NonNull Object packageManagerInternal) {
        mPackageManagerInternal = packageManagerInternal;
    }

    @NonNull
    public PackageDataSnapshot snapshot() {
        try {
            return (PackageDataSnapshot) mPackageManagerInternal.getClass()
                    .getMethod("snapshot")
                    .invoke(mPackageManagerInternal);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @Nullable
    public PackageState getPackageState(@NonNull PackageDataSnapshot snapshot,
            @NonNull int callingUid, @NonNull String packageName) {
        try {
            int userId = (int) UserHandle.class.getMethod("getUserId", int.class)
                                 .invoke(null, callingUid);
            Object packageState = snapshot.getClass()
                                          .getMethod("getPackageStateForInstalledAndFiltered",
                                                  String.class, int.class, int.class)
                                          .invoke(snapshot, packageName, callingUid, userId);
            return packageState != null ? new PackageState(packageState) : null;
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
