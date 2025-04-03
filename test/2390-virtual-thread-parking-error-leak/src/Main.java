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
import dalvik.system.VirtualThreadParkingError;

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
 * Verify that app code can't catch VirtualThreadParkingError which should only used
 * by ART internally.
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

        Thread.startVirtual(Main::task1);
        Thread.startVirtual(Main::task2);
    }

    private static void task1() {
        try {
            Thread.parkVirtual();
        } catch (VirtualThreadParkingError e) {
            e.printStackTrace(System.err);
            throw new AssertionError("parkVirtual() shouldn't throw.");
        }
    }

    private static void task2() {
        try {
            Thread.parkVirtual();
        } catch (Throwable e) {
            e.printStackTrace(System.err);
            throw new AssertionError("parkVirtual() shouldn't throw.");
        }
    }
}
