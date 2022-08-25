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

package com.android.server.art;

/**
 * Indicates the priority of an operation. The value affects the resource usage and the process
 * priority. A higher value may result in faster execution but may consume more resources and
 * compete for resources with other processes.
 *
 * @hide
 */
enum PriorityClass {
    /** Indicates that the operation blocks boot. */
    BOOT = 100,
    /**
     * Indicates that a human is waiting on the result and the operation is more latency sensitive
     * than usual.
     */
    INTERACTIVE_FAST = 80,
    /** Indicates that a human is waiting on the result. */
    INTERACTIVE = 60,
    /** Indicates that the operation runs in background. */
    BACKGROUND = 40,
}
