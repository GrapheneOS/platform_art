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
package art;

import android.system.Os;

import dalvik.system.VirtualThreadContext;

import java.lang.ref.WeakReference;

/**
 * Verify that a virtual thread is pinned on a carrier thread when

 */
public class Test2392 {

    private static WeakReference<Thread> WEAK_REF = null;
    private static volatile WeakReference<Thread> WEAK_REF2 = null;

    public static void main(String[] args) throws InterruptedException {
        // Exit if the thread throws any exception.
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            System.err.println("thread: " + t.getName());
            e.printStackTrace(System.err);
            System.exit(1);
        });

        VirtualThreadContext context = startVirtualThreadAndVerifyPinning();
        while (context.parkedStates == null) {}

        long startTime = System.currentTimeMillis();
        while (!WEAK_REF.refersTo(null)) {
            if (System.currentTimeMillis() - startTime > 10 * 1000) {
                throw new AssertionError("10s time out. The carrier thread should be GC-ed");
            }
            System.gc();
        }

        Thread carrier2 = Thread.unparkVirtual(context);
        carrier2.join();
    }

    private static VirtualThreadContext startVirtualThreadAndVerifyPinning() {
        VirtualThreadContext context = startVirtualThreadAndGetParkedContext();
        Thread carrier = context.pinnedCarrierThread;
        if (carrier == null) {
            throw new AssertionError("carrier shouldn't be null");
        }
        System.gc();
        if (!WEAK_REF.refersTo(carrier)) {
            throw new AssertionError("The carrier thread should be the same");
        }
        if (!WEAK_REF2.refersTo(carrier)) {
            throw new AssertionError("The carrier thread should be the same");
        }

        Thread carrier2 = Thread.unparkVirtual(context);
        if (carrier != carrier2) {
            throw new AssertionError("The carrier thread should be the same");
        }
        return context;
    }

    private static VirtualThreadContext startVirtualThreadAndGetParkedContext() {
        Thread carrier1 = Thread.startVirtual(Test2392::task);
        WEAK_REF = new WeakReference<>(carrier1);

        VirtualThreadContext context = null;
        while (context == null || context.pinnedCarrierThread == null || WEAK_REF2 == null) {
            context = carrier1.getVirtualThreadContext();
        }
        return context;
    }

    private static void task() {
        int tid1 = Os.gettid();
        long threadId1 = getCarrierThreadId();
        WEAK_REF2 = new WeakReference<>(Thread.currentThread());
        // synchronized (MONITOR) {
        //     Thread.parkVirtual();
        // }
        parkNative();

        int tid2 = Os.gettid();
        long threadId2 = getCarrierThreadId();
        // Verify that the 2 carrier threads are identical.
        if (tid1 != tid2) {
            throw new AssertionError("tid should be the same: "
                    + tid1 + " != " + tid2);
        }
        if (threadId1 != threadId2) {
            throw new AssertionError("tid should be the same: "
                    + threadId1 + " != " + threadId2);
        }

        parkVirtual();

        // Verify that the 2 carrier threads are not identical.
        tid2 = Os.gettid();
        threadId2 = getCarrierThreadId();
        if (tid1 == tid2) {
            throw new AssertionError("tid shouldn't be the same: "
                    + tid1 + " != " + tid2);
        }
        if (threadId1 == threadId2) {
            throw new AssertionError("tid shouldn't be the same: "
                    + threadId1 + " != " + threadId2);
        }
    }

    /**
     * This method is extracted to avoid holding a reference to the carrier thread.
     */
    private static long getCarrierThreadId() {
        return Thread.currentThread().threadId();
    }

    private static native void parkNative();

    /**
     * Called by the JNI code.
     */
    public static void parkVirtual() {
        Thread.parkVirtual();
    }
}
