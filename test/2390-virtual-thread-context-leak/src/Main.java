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

import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;
import java.text.DateFormat;
import java.util.Date;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicInteger;


/**
 *  Virtual Thread isn't GC root according to the spec. Verify that a virtual thread context and
 *  associated parked states aren't reachable if no application code references it.
 *  The spec may change in the future.
 */
public class Main {
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

        WeakReference<VirtualThreadContext> ref = $noinline$startVirtualThreadAndGetParkedContext();
        long startTime = System.nanoTime();
        while (!ref.refersTo(null)) {
            if (System.nanoTime() - startTime > 20 * NANOS_PER_SECOND) {
                throw new AssertionError("20s time out");
            }

            runGcAndFinalization();
        }
    }

    private static void runGcAndFinalization() {
        for (int i = 0; i < 3; ++i) {
            // Both GC and finalization are needed. Otherwise, the test could fail in the gcstress.
            Runtime.getRuntime().gc();
            System.runFinalization();
        }
    }

    private static WeakReference<VirtualThreadContext> $noinline$startVirtualThreadAndGetParkedContext() {
        Thread carrier1 = Thread.startVirtual(Main::task);
        return new WeakReference<>(carrier1.getVirtualThreadContext());
    }

    private static void task() {
        long threadId1 = $noinline$getCarrierThreadId();
        Thread.parkVirtual();
        long threadId2 = $noinline$getCarrierThreadId();
        if (threadId1 == threadId2) {
            throw new RuntimeException("tid shouldn't normally be the same: "
                    + threadId1 + " != " + threadId2);
        }
    }

    /**
     * This method is extracted to avoid holding a reference to the carrier thread.
     */
    private static long $noinline$getCarrierThreadId() {
        return Thread.currentThread().threadId();
    }
}
