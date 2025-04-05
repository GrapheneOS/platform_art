/*
 * Copyright (C) 2025 The Android Open Source Project
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

import android.system.Os;

import dalvik.system.VirtualThreadContext;

import java.lang.ref.WeakReference;

/**
 * Check that the carrier of virtual thread isn't leaked.
 */
public class Main {

    private static WeakReference<Thread> WEAK_REF = null;

    private static final long NANOS_PER_SECOND = 1_000_000_000L;

    public static void main(String[] args) throws InterruptedException {
        if (!com.android.art.flags.Flags.virtualThreadImplV1()) {
            return;
        }
        // Exit if the thread throws any exception.
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            System.err.println("thread: " + t.getName());
            e.printStackTrace(System.err);
            System.exit(1);
        });

        // Verify that a carrier thread isn't reachable and leaked by the underlying
        // Virtual Thread implementation after parking the Virtual Thread.
        VirtualThreadContext context = startVirtualThreadAndGetParkedContext();
        long startTime = System.nanoTime();
        while (!WEAK_REF.refersTo(null)) {
            if (System.nanoTime() - startTime > 20 * NANOS_PER_SECOND) {
                throw new AssertionError("20s time out");
            }
            System.gc();
        }

        Thread carrier2 = Thread.unparkVirtual(context);
        carrier2.join();
    }

    private static VirtualThreadContext startVirtualThreadAndGetParkedContext() {
        Thread carrier1 = Thread.startVirtual(Main::task);
        WEAK_REF = new WeakReference<>(carrier1);

        VirtualThreadContext context = null;
        while (context == null || context.parkedStates == null) {
            context = carrier1.getVirtualThreadContext();
        }
        return context;
    }

    private static void task() {
        int tid1 = Os.gettid();
        long threadId1 = getCarrierThreadId();
        Thread.parkVirtual();
        int tid2 = Os.gettid();
        long threadId2 = getCarrierThreadId();
        // Verify that the 2 carrier threads are not identical.
        if (tid1 == tid2) {
            throw new RuntimeException("tid shouldn't normally be the same: "
                    + tid1 + " != " + tid2);
        }
        if (threadId1 == threadId2) {
            throw new RuntimeException("tid shouldn't normally be the same: "
                    + threadId1 + " != " + threadId2);
        }
    }

    /**
     * This method is extracted to avoid holding a reference to the carrier thread.
     */
    private static long getCarrierThreadId() {
        return Thread.currentThread().threadId();
    }
}
