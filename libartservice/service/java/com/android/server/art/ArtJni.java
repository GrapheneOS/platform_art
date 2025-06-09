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

package com.android.server.art;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Build;
import android.os.RemoteException;

import androidx.annotation.RequiresApi;

import dalvik.system.VMRuntime;

import java.io.IOException;

/**
 * JNI methods for ART Service with wrappers.
 *
 * The wrappers are added for two reasons:
 * - They make the methods mockable, since Mockito cannot mock JNI methods.
 * - They delegate calls to artd if the code is running for Pre-reboot Dexopt,
 *  to avoid loading libartservice.so.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class ArtJni {

    private static volatile boolean sLoaded = false;

    private ArtJni() {}

    /**
     * Loads the library lazily.
     *
     * This method is synchronized to avoid loading the library multiple times.
     * This is mainly to be able to mock the JNI methods in tests without
     *  actually loading the libartservice.so library.
     */
    private static void loadLibrary() {
        if (sLoaded) {
            return;
        }
        Utils.check(!GlobalInjector.getInstance().isPreReboot());
        synchronized (ArtJni.class) {
            if (sLoaded) {
                return;
            }

            // During Pre-reboot Dexopt, the code is loaded by a separate class loader from the chroot
            // dir, where the new ART apex is mounted. In this case, loading libartservice.so is tricky.
            // The library depends on libc++.so, libbase.so, etc. Although the classloader allows
            // specifying a library search path, it doesn’t allow specifying how to search for
            // dependencies. Because the classloading takes place in system server, the old linkerconfig
            // takes effect rather than the new one, and the old linkerconfig doesn’t specify how to
            // search for dependencies for the new libartservice.so. This leads to an undesired
            // behavior: the dependencies are resolved to those on the old platform.
            //
            // Also, we can't statically link libartservice.so against all dependencies because it not
            // only bloats libartservice.so by a lot, but also prevents us from accessing the global
            // runtime instance when the code is running in the normal situation.
            //
            // Therefore, for Pre-reboot Dexopt, we just avoid loading libartservice.so, and delegate
            // calls to artd instead.
            if (VMRuntime.getRuntime().vmLibrary().equals("libartd.so")) {
                System.loadLibrary("artserviced");
            } else {
                System.loadLibrary("artservice");
            }
            sLoaded = true;
        }
    }

    /**
     * Returns an error message if the given dex path is invalid, or null if the validation passes.
     */
    @Nullable
    public static String validateDexPath(@NonNull String dexPath) {
        if (GlobalInjector.getInstance().isPreReboot()) {
            try {
                return ArtdRefCache.getInstance().getArtd().validateDexPath(dexPath);
            } catch (RemoteException e) {
                Utils.logArtdException(e);
                return null;
            }
        }
        loadLibrary();
        return validateDexPathNative(dexPath);
    }

    /**
     * Returns an error message if the given class loader context is invalid, or null if the
     * validation passes.
     */
    @Nullable
    public static String validateClassLoaderContext(
            @NonNull String dexPath, @NonNull String classLoaderContext) {
        if (GlobalInjector.getInstance().isPreReboot()) {
            try {
                return ArtdRefCache.getInstance().getArtd().validateClassLoaderContext(
                        dexPath, classLoaderContext);
            } catch (RemoteException e) {
                Utils.logArtdException(e);
                return null;
            }
        }
        loadLibrary();
        return validateClassLoaderContextNative(dexPath, classLoaderContext);
    }

    /**
     * Returns the name of the Garbage Collector currently in use in the Android Runtime.
     */
    @NonNull
    public static String getGarbageCollector() {
        if (GlobalInjector.getInstance().isPreReboot()) {
            // We don't need this for Pre-reboot Dexopt and we can't have this in artd because it
            // needs access to the global runtime instance.
            throw new UnsupportedOperationException();
        }
        loadLibrary();
        return getGarbageCollectorNative();
    }

    /**
     * Sets the system property {@code key} to {@code value}.
     *
     * @throws IllegalStateException if the operation fails. This caller should not expect this,
     *         unless the inputs are invalid (e.g., value too long).
     */
    public static Void setProperty(@NonNull String key, @NonNull String value) {
        if (GlobalInjector.getInstance().isPreReboot()) {
            // We don't need this for Pre-reboot Dexopt.
            throw new UnsupportedOperationException();
        }
        loadLibrary();
        setPropertyNative(key, value);
        // Return a placeholder value to make this method easier to mock. There is no good way to
        // mock a method that is both void and static, due to the poor design of Mockito API.
        return null;
    }

    /**
     * Waits for processes whose executable is in the given directory to exit, and kills them if
     * they don't exit within the timeout.
     *
     * Note that this method only checks processes' executable paths, not their open files. If the
     * executable of a process is outside of the given directory but the process opens a file in
     * that directory, this method doesn't handle it.
     *
     * After killing, the method waits another round with the given timeout. Theoretically, this
     * method can take at most {@code 2 * timeoutMs}. However, the second round should be pretty
     * fast in practice.
     *
     * This method assumes that no new process is started from an executable in the given directory
     * while the method is running. It is the callers responsibility to make sure that this
     * assumption holds.
     *
     * @throws IllegalArgumentException if {@code timeoutMs} is negative
     * @throws IOException if the operation fails
     */
    public static Void ensureNoProcessInDir(@NonNull String dir, int timeoutMs) throws IOException {
        if (GlobalInjector.getInstance().isPreReboot()) {
            // We don't need this for Pre-reboot Dexopt.
            throw new UnsupportedOperationException();
        }
        loadLibrary();
        ensureNoProcessInDirNative(dir, timeoutMs);
        // Return a placeholder value to make this method easier to mock. There is no good way to
        // mock a method that is both void and static, due to the poor design of Mockito API.
        return null;
    }

    @Nullable private static native String validateDexPathNative(@NonNull String dexPath);
    @Nullable
    private static native String validateClassLoaderContextNative(
            @NonNull String dexPath, @NonNull String classLoaderContext);
    @NonNull private static native String getGarbageCollectorNative();
    private static native void setPropertyNative(@NonNull String key, @NonNull String value);
    private static native void ensureNoProcessInDirNative(@NonNull String dir, int timeoutMs)
            throws IOException;
}
