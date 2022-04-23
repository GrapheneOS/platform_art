/*
 * Copyright (C) 2021 The Android Open Source Project
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
import android.annotation.SystemApi;
import android.os.Binder;

import java.io.FileDescriptor;

/**
 * This class provides a system API for functionality provided by the ART module.
 *
 * Note: Although this class is the entry point of ART services, this class is not a {@link
 * SystemService}, and it does not publish a binder. Instead, it is a module loaded by the
 * system_server process, registered in {@link LocalManagerRegistry}. {@link LocalManagerRegistry}
 * specifies that in-process module interfaces should be named with the suffix {@code ManagerLocal}
 * for consistency.
 *
 * @hide
 */
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
public final class ArtManagerLocal {
    private static final String TAG = "ArtService";

    public ArtManagerLocal() {}

    /**
     * Handles `cmd package art` sub-command.
     *
     * For debugging purposes only. Intentionally enforces root access to limit the usage.
     *
     * Note: This method is not an override of {@link Binder#handleShellCommand} because ART
     * services does not publish a binder. Instead, it handles the `art` sub-command forwarded by
     * the `package` service. The semantics of the parameters are the same as {@link
     * Binder#handleShellCommand}.
     *
     * @return zero on success, non-zero on internal error (e.g., I/O error)
     * @throws SecurityException if the caller is not root
     * @throws IllegalArgumentException if the arguments are illegal
     * @see ArtShellCommand#onHelp()
     */
    public int handleShellCommand(@NonNull Binder target, @NonNull FileDescriptor in,
            @NonNull FileDescriptor out, @NonNull FileDescriptor err, @NonNull String[] args) {
        return new ArtShellCommand(this).exec(target, in, out, err, args);
    }
}
