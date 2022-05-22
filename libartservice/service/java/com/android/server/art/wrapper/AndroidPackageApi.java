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
import android.util.SparseArray;

/** @hide */
public class AndroidPackageApi {
    private final Object mPkg;

    AndroidPackageApi(@NonNull Object pkg) {
        mPkg = pkg;
    }

    @NonNull
    public String getBaseApkPath() {
        try {
            return (String) mPkg.getClass().getMethod("getBaseApkPath").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    public boolean isHasCode() {
        try {
            return (boolean) mPkg.getClass().getMethod("isHasCode").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public String[] getSplitNames() {
        try {
            return (String[]) mPkg.getClass().getMethod("getSplitNames").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public String[] getSplitCodePaths() {
        try {
            return (String[]) mPkg.getClass().getMethod("getSplitCodePaths").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public int[] getSplitFlags() {
        try {
            Class<?> parsingPackageImplClass =
                    Class.forName("com.android.server.pm.pkg.parsing.ParsingPackageImpl");
            return (int[]) parsingPackageImplClass.getMethod("getSplitFlags").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @Nullable
    public String getClassLoaderName() {
        try {
            return (String) mPkg.getClass().getMethod("getClassLoaderName").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @NonNull
    public String[] getSplitClassLoaderNames() {
        try {
            return (String[]) mPkg.getClass().getMethod("getSplitClassLoaderNames").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    @Nullable
    public SparseArray<int[]> getSplitDependencies() {
        try {
            return (SparseArray<int[]>) mPkg.getClass()
                    .getMethod("getSplitDependencies")
                    .invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    public boolean isIsolatedSplitLoading() {
        try {
            return (boolean) mPkg.getClass().getMethod("isIsolatedSplitLoading").invoke(mPkg);
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
