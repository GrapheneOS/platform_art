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

        WeakReference<VirtualThreadContext> ref = startVirtualThreadAndGetParkedContext();
        long startTime = System.currentTimeMillis();
        while (!ref.refersTo(null)) {
            if (System.currentTimeMillis() - startTime > 10 * 1000) {
                throw new AssertionError("10s time out");
            }
            System.gc();
        }
    }

    private static WeakReference<VirtualThreadContext> startVirtualThreadAndGetParkedContext() {
        Thread carrier1 = Thread.startVirtual(Main::task);
        return new WeakReference<>(carrier1.getVirtualThreadContext());
    }

    private static void task() {
        long threadId1 = getCarrierThreadId();
        Thread.parkVirtual();
        long threadId2 = getCarrierThreadId();
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
