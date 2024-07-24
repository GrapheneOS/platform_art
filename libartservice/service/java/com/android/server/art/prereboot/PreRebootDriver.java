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

package com.android.server.art.prereboot;

import static com.android.server.art.IDexoptChrootSetup.CHROOT_DIR;
import static com.android.server.art.prereboot.PreRebootManagerInterface.SystemRequirementException;
import static com.android.server.art.proto.PreRebootStats.Status;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.content.Context;
import android.os.ArtModuleServiceManager;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import android.system.ErrnoException;
import android.system.Os;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;
import com.android.server.art.ArtJni;
import com.android.server.art.ArtManagerLocal;
import com.android.server.art.ArtModuleServiceInitializer;
import com.android.server.art.ArtdRefCache;
import com.android.server.art.AsLog;
import com.android.server.art.GlobalInjector;
import com.android.server.art.IArtd;
import com.android.server.art.IDexoptChrootSetup;
import com.android.server.art.PreRebootDexoptJob;
import com.android.server.art.Utils;

import dalvik.system.DelegateLastClassLoader;

import libcore.io.Streams;

import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Drives Pre-reboot Dexopt, through reflection.
 *
 * DO NOT use this class directly. Use {@link PreRebootDexoptJob}.
 *
 * During Pre-reboot Dexopt, the old version of this code is run.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
public class PreRebootDriver {
    @NonNull private final Injector mInjector;

    public PreRebootDriver(@NonNull Context context) {
        this(new Injector(context));
    }

    @VisibleForTesting
    public PreRebootDriver(@NonNull Injector injector) {
        mInjector = injector;
    }

    /**
     * Runs Pre-reboot Dexopt and returns whether it is successful. Returns false if Pre-reboot
     * dexopt failed, the system requirement check failed, or system requirements are not met.
     *
     * @param otaSlot The slot that contains the OTA update, "_a" or "_b", or null for a Mainline
     *         update.
     * @param mapSnapshotsForOta Whether to map/unmap snapshots. Only applicable to an OTA update.
     */
    public boolean run(@Nullable String otaSlot, boolean mapSnapshotsForOta,
            @NonNull CancellationSignal cancellationSignal) {
        var statsReporter = new PreRebootStatsReporter();
        boolean success = false;
        boolean systemRequirementCheckFailed = false;
        try {
            statsReporter.recordJobStarted();
            setUp(otaSlot, mapSnapshotsForOta);
            runFromChroot(cancellationSignal);
            success = true;
            return true;
        } catch (RemoteException e) {
            Utils.logArtdException(e);
        } catch (ServiceSpecificException e) {
            AsLog.e("Failed to set up chroot", e);
        } catch (SystemRequirementException e) {
            systemRequirementCheckFailed = true;
            AsLog.e("System requirement check failed", e);
        } catch (ReflectiveOperationException e) {
            Throwable cause = e.getCause();
            if (cause != null
                    && cause.getClass().getName().equals(
                            SystemRequirementException.class.getName())) {
                // For future use only. Can't happen for now.
                systemRequirementCheckFailed = true;
                AsLog.e("System requirement check failed in chroot", cause);
            } else {
                AsLog.wtf("Failed to run Pre-reboot Dexopt", e);
            }
        } catch (IOException | ErrnoException e) {
            AsLog.e("Failed to run Pre-reboot Dexopt", e);
        } finally {
            try {
                // No need to pass `mapSnapshotsForOta` because `setUp` stores this information in a
                // temp file.
                tearDown();
            } catch (RemoteException e) {
                Utils.logArtdException(e);
            } catch (ServiceSpecificException | IOException e) {
                AsLog.e("Failed to tear down chroot", e);
            } finally {
                statsReporter.recordJobEnded(success, systemRequirementCheckFailed);
            }
        }
        return false;
    }

    public void test() {
        boolean teardownAttempted = false;
        try {
            setUp(null /* otaSlot */, false /* mapSnapshotsForOta */);
            // Ideally, we should try dexopting some packages here. However, it's not trivial to
            // pass a package list into chroot. Besides, we need to generate boot images even if we
            // dexopt only one package, and that can easily make the test fail the CTS quality
            // requirement on test duration (<30s).
            teardownAttempted = true;
            tearDown();
        } catch (SystemRequirementException e) {
            throw new AssertionError("System requirement check failed", e);
        } catch (RemoteException | IOException e) {
            throw new AssertionError("Unexpected exception", e);
        } finally {
            if (!teardownAttempted) {
                try {
                    tearDown();
                } catch (RemoteException | IOException | RuntimeException e) {
                    // Do nothing.
                }
            }
        }
    }

    public void maybeCleanUpChroot() {
        if (!Files.exists(Paths.get(CHROOT_DIR))) {
            return;
        }
        try {
            ArtJni.ensureNoProcessInDir(CHROOT_DIR, 5000 /* timeoutMs */);
            mInjector.getDexoptChrootSetup().tearDown(true /* allowConcurrent */);
        } catch (RemoteException e) {
            Utils.logArtdException(e);
        } catch (ServiceSpecificException | IOException e) {
            AsLog.e("Failed to clean up leftover chroot", e);
        }
    }

    private void setUp(@Nullable String otaSlot, boolean mapSnapshotsForOta)
            throws RemoteException, SystemRequirementException {
        mInjector.getDexoptChrootSetup().setUp(otaSlot, mapSnapshotsForOta);
        if (!mInjector.getArtd().checkPreRebootSystemRequirements(CHROOT_DIR)) {
            throw new SystemRequirementException("See logs for details");
        }
        mInjector.getDexoptChrootSetup().init();
    }

    private void tearDown() throws RemoteException, IOException {
        // In general, the teardown unmounts apexes and partitions, and open files can keep the
        // mounts busy so that they cannot be unmounted. Therefore, a running Pre-reboot artd
        // process can prevent the teardown from succeeding. It's managed by the service manager,
        // and there isn't a reliable API to kill it. We deal with it in two steps:
        // 1. Trigger GC and finalization. The service manager should gracefully shut it down, since
        //    there is no reference to it as this point.
        // 2. Call `ensureNoProcessInDir` to wait for it to exit. If it doesn't exit in 5 seconds,
        //    `ensureNoProcessInDir` will then kill it.
        Runtime.getRuntime().gc();
        Runtime.getRuntime().runFinalization();
        // At this point, no process other than `artd` is expected to be running. `runFromChroot`
        // blocks on `artd` calls, even upon cancellation, and `artd` in turn waits for child
        // processes to exit, even if they are killed due to the cancellation.
        ArtJni.ensureNoProcessInDir(CHROOT_DIR, 5000 /* timeoutMs */);
        mInjector.getDexoptChrootSetup().tearDown(false /* allowConcurrent */);
    }

    private void runFromChroot(@NonNull CancellationSignal cancellationSignal)
            throws ReflectiveOperationException, IOException, ErrnoException {
        String chrootArtDir = CHROOT_DIR + "/apex/com.android.art";
        String dexPath = chrootArtDir + "/javalib/service-art.jar";

        // We load the dex file into the memory and close it. In this way, the classloader won't
        // prevent unmounting even if it fails to unload.
        ClassLoader classLoader;
        FileDescriptor memfd = Os.memfd_create("in memory from " + dexPath, 0 /* flags */);
        try (FileOutputStream out = new FileOutputStream(memfd);
                InputStream in = new FileInputStream(dexPath)) {
            Streams.copy(in, out);
            classLoader = new DelegateLastClassLoader("/proc/self/fd/" + memfd.getInt$(),
                    this.getClass().getClassLoader() /* parent */);
        }

        Class<?> preRebootManagerClass =
                classLoader.loadClass("com.android.server.art.prereboot.PreRebootManager");
        // Check if the dex file is loaded successfully. Note that the constructor of
        // `DelegateLastClassLoader` does not throw when the load fails.
        if (preRebootManagerClass == PreRebootManager.class) {
            throw new IllegalStateException(String.format("Failed to load %s", dexPath));
        }
        Object preRebootManager = preRebootManagerClass.getConstructor().newInstance();
        preRebootManagerClass
                .getMethod("run", ArtModuleServiceManager.class, Context.class,
                        CancellationSignal.class)
                .invoke(preRebootManager, ArtModuleServiceInitializer.getArtModuleServiceManager(),
                        mInjector.getContext(), cancellationSignal);
    }

    /**
     * Injector pattern for testing purpose.
     *
     * @hide
     */
    @VisibleForTesting
    public static class Injector {
        @NonNull private final Context mContext;

        Injector(@NonNull Context context) {
            mContext = context;
        }

        @NonNull
        public Context getContext() {
            return mContext;
        }

        @NonNull
        public IDexoptChrootSetup getDexoptChrootSetup() {
            return GlobalInjector.getInstance().getDexoptChrootSetup();
        }

        @NonNull
        public IArtd getArtd() {
            return ArtdRefCache.getInstance().getArtd();
        }
    }
}
