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

package com.android.server.art;

/** @hide */
interface IDexoptChrootSetup {
    const @utf8InCpp String PRE_REBOOT_DEXOPT_DIR = "/mnt/pre_reboot_dexopt";
    const @utf8InCpp String CHROOT_DIR = PRE_REBOOT_DEXOPT_DIR + "/chroot";

    /**
     * Sets up the chroot environment. All files in chroot, except apexes and the linker config, are
     * accessible after this method is called.
     *
     * @param otaSlot The slot that contains the OTA update, "_a" or "_b", or null for a Mainline
     *         update.
     * @param mapSnapshotsForOta Whether to map/unmap snapshots. Only applicable to an OTA update.
     */
    void setUp(@nullable @utf8InCpp String otaSlot, boolean mapSnapshotsForOta);

    /**
     * Initializes the chroot environment. Can only be called after {@link #setUp}. Apexes and
     * the linker config in chroot are accessible, and binaries are executable in chroot, after
     * this method is called.
     */
    void init();

    /**
     * Tears down the chroot environment.
     *
     * @param allowConcurrent If true, allows this method to be called concurrently when another
     * call to the service is still being processed. Note that the service does not process this
     * call concurrently but waits until the other call is done.
     */
    void tearDown(boolean allowConcurrent);
}
