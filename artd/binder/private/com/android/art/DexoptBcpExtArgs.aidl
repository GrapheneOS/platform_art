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

package com.android.art;

import com.android.art.Isa;

/**
 * Arguments for dexopt of BCP extension.
 *
 * <h1>Security Considerations:</h1>
 *
 * <p>On Android, both the data provider and consumer are currently assumed in the same trusting
 * domain * (e.g. since they are in the same process and the boundary is function call).
 *
 * <p>Compilation OS, on the contrary, plays role of data consumer and can't trust the data provided
 * by * the potentially malicious clients. The data must be validated before use.
 *
 * <p>When adding a new field, make sure the value space can be validated. See "SECURITY:" below for
 * examples.
 *
 * {@hide}
 */
parcelable DexoptBcpExtArgs {
    // SECURITY: File descriptor are currently integers. They are assumed trusted in Android right
    // now. For CompOS, they are verified transparently to the compiler, thus can also be assumed
    // trusted.
    int[] dexFds;
    // Note: It's more ideal to put bootClasspaths and bootClasspathFds in a Map<path, fd>, but Map
    // is only supported by the Java backend of AIDL. As a result, we need to maintain both arrays
    // manually.
    String[] bootClasspaths;
    int[] bootClasspathFds;
    int profileFd = -1;
    int dirtyImageObjectsFd = -1;
    // Output file descriptors
    int oatFd = -1;
    int vdexFd = -1;
    int imageFd = -1;

    // TODO(victorhsieh): Try to reconstruct behind the API, otherwise reasonable the security.
    String[] dexPaths;
    String oatLocation;

    // SECURITY: The server may accept the request to produce code for the specified architecture,
    // if supported.
    Isa isa = Isa.UNSUPPORTED;

    // SECURITY: Computational resource should not affect the compilation results.
    int[] cpuSet;
    int threads;
    int timeoutSecs = 0;
}
