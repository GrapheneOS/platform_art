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

import java.text.DateFormat;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Implement a thread sleeping for virtual thread on top of a single-threaded {@link Timer}.
 * Each sleeping virtual thread should take ~1 KB or less memory. It should be more scalable than
 * platform threads.
 */
public class Main {

    private static final boolean DEBUG = false;

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

        testNSleepingThreads(2);
        testNSleepingThreads(10);
        testNSleepingThreads(100);
        testNSleepingThreads(1000);
        testNSleepingThreads(10000);
    }

    /**
     * Start {@code numOfThreads} virtual threads sleeping for the given duration and wait until
     * all threads join or time out.
     *
     * For the heap size limit, one virtual thread in the parked state takes ~1kB heap. Too many
     * virtual threads can cause {@link OutOfMemoryError}.
     * @param numOfThreads number of concurrent virtual threads sleeping
     */
    private static void testNSleepingThreads(int numOfThreads) {
        long sleepDurationMs = Math.max(numOfThreads / 10, 100);
        try (SleepingVirtualThreadTestCase test =
                     new SleepingVirtualThreadTestCase(numOfThreads, sleepDurationMs)) {
            test.start();
            test.waitUntilAllThreadsWakeUp();
        }
    }

    private static void debugPrintln(String msg) {
        if (DEBUG) {
             System.out.println(msg);
        }
    }

    /**
     * Starts the given number of virtual threads which sleeps for the given duration concurrently.
     */
    private static class SleepingVirtualThreadTestCase implements AutoCloseable {
        private static final int CARRIER_THREADS_LIMIT = 128;
        private final DateFormat df = DateFormat.getTimeInstance();
        private final Timer mTimer = new Timer();
        private final int mNumOfThreads;
        private final long mSleepDurationMs;
        private final Set<Long> virtualThreadIds;
        private final AtomicInteger mJoinedCounter = new AtomicInteger(0);
        private final LinkedBlockingQueue<Object> mJoinedNotifier
                = new LinkedBlockingQueue<Object>();

        SleepingVirtualThreadTestCase(int numOfThreads, long sleepDurationMs) {
            mNumOfThreads = numOfThreads;
            mSleepDurationMs = sleepDurationMs;
            virtualThreadIds = new HashSet<>(mNumOfThreads);
        }

        void start() {
            long startTime = System.currentTimeMillis();
            debugPrintln("Started at " + df.format(new Date(startTime)));

            startAndParkAllThreads();
        }

        private void startAndParkAllThreads() {
            ConcurrentLinkedQueue<ParkedSleepingThreadHolder> parkingThreads =
                    new ConcurrentLinkedQueue<>();
            AtomicInteger runningSize = new AtomicInteger(0);
            AtomicInteger startedCounter = new AtomicInteger(0);
            AtomicInteger parkedCounter = new AtomicInteger(0);

            // Without the proper support from the ForkJoinPool and a new implementation of
            // Thread.sleep(ms) for Virtual Thread, we simulate a simple fixed sized
            // pool and Thread.sleep(ms) here. It starts the given number of virtual thread and
            // schedule all virtual threads to unpark after sleeping for the given durations, i.e.
            // parkedCounter == mNumOfThreads.
            while (parkedCounter.get() < mNumOfThreads) {
                while (startedCounter.get() < mNumOfThreads &&
                        runningSize.get() < CARRIER_THREADS_LIMIT) {
                    runningSize.incrementAndGet();
                    startSleepingThread(parkingThreads, mNumOfThreads, mSleepDurationMs,
                            mJoinedCounter, mJoinedNotifier);
                    startedCounter.incrementAndGet();
                }
                while (!parkingThreads.isEmpty()) {
                    ParkedSleepingThreadHolder head = parkingThreads.peek();
                    if (head == null) {
                        break;
                    }
                    VirtualThreadContext vt_context = head.thread.getVirtualThreadContext();
                    virtualThreadIds.add(vt_context.id);
                    if (vt_context.parkedStates == null) {
                        // Stop iterating
                        break;
                    }
                    long delay = head.startTime - System.currentTimeMillis() + head.millis;
                    if (delay > 0) {
                        debugPrintln("Thread delayed for " + delay  + "ms " + "started.");
                        mTimer.schedule(new TimerTask() {
                            @Override
                            public void run() {
                                Thread th = Thread.unparkVirtual(vt_context);
                                debugPrintln("Thread " + th.getName() + " delayed for " + delay  + "ms " + "started.");
                            }
                        }, delay);
                    } else {
                        Thread.unparkVirtual(vt_context);
                    }
                    parkingThreads.poll();
                    parkedCounter.incrementAndGet();
                    runningSize.decrementAndGet();
                }
            }
            debugPrintln("Started " + mNumOfThreads + " threads!");
            debugPrintln("Approx. " + (mNumOfThreads - mJoinedCounter.get()) + " threads are sleeping");
        }

        void waitUntilAllThreadsWakeUp() {
            // The constant multiplier needs to be significantly larger than 2 because the timer
            // is single-threaded, and is slower in the interpreter mode.
            long timeoutThresholdMs = mSleepDurationMs * 5;
            Object joined_signal;
            try {
                joined_signal = mJoinedNotifier.poll(timeoutThresholdMs, TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }

            long endTime = System.currentTimeMillis();
            debugPrintln("all " + mJoinedCounter.get() + " Threads joined at "
                    + df.format(new Date(endTime)));
            if (joined_signal == null) {
                throw new AssertionError("Expected " + mNumOfThreads + " threads to "
                        + "join, but only " + mJoinedCounter.get() + " threads joined within " +
                        timeoutThresholdMs + " ms.");
            }
            if (virtualThreadIds.size() != mNumOfThreads) {
                throw new AssertionError("Expected " + mNumOfThreads + " threads, but only " +
                        virtualThreadIds.size() + " unique virtual thread ids.");
            }
        }

        private static void startSleepingThread(
                ConcurrentLinkedQueue<ParkedSleepingThreadHolder> queue,
                long numOfThreads, long sleepDurationMs,
                AtomicInteger joinedCounter, LinkedBlockingQueue<Object> allJoinedNotifier) {
            Thread.startVirtual(() -> sleepingTask(queue, numOfThreads, sleepDurationMs,
                    joinedCounter, allJoinedNotifier));
        }

        private static void sleepingTask(ConcurrentLinkedQueue<ParkedSleepingThreadHolder> queue,
                long numOfThreads, long sleepDurationMs,
                AtomicInteger joinedCounter, LinkedBlockingQueue<Object> allJoinedNotifier) {
            int tid1 = Os.gettid();
            parkVirtual(queue,  sleepDurationMs);
            int tid2 = Os.gettid();
            if (tid1 == tid2) {
                // It may actually happen when tid is re-used for a separate Thread object.
                throw new RuntimeException("tid shouldn't normally be the same: "
                        + tid1 + " != " + tid2);
            }

            int c = joinedCounter.incrementAndGet();

            if (c >= numOfThreads) {
                allJoinedNotifier.add(new Object());
            }
        }

        private static void parkVirtual(ConcurrentLinkedQueue<ParkedSleepingThreadHolder> queue,
                long millis) {
            queue.add(new ParkedSleepingThreadHolder(Thread.currentThread(),
                    System.currentTimeMillis(), millis));
            Thread.parkVirtual();
        }

        @Override
        public void close() {
            // Timer thread isn't a daemon. Canceling the timer allows process termination.
            mTimer.cancel();
        }
    }

    private static class ParkedSleepingThreadHolder {
        private final Thread thread;
        private final long startTime;
        private final long millis;

        ParkedSleepingThreadHolder(Thread thread, long startTime, long millis) {
            this.thread = thread;
            this.startTime = startTime;
            this.millis = millis;
        }
    }
}
