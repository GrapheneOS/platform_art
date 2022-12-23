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

import android.annotation.NonNull;
import android.annotation.Nullable;

import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;

/**
 * A class that executes commands with a minimum interval.
 *
 * @hide
 */
public class Debouncer {
    @NonNull private Supplier<ScheduledExecutorService> mScheduledExecutorFactory;
    private final long mIntervalMs;
    @Nullable private ScheduledFuture<?> mCurrentTask = null;

    public Debouncer(
            long intervalMs, @NonNull Supplier<ScheduledExecutorService> scheduledExecutorFactory) {
        mScheduledExecutorFactory = scheduledExecutorFactory;
        mIntervalMs = intervalMs;
    }

    /**
     * Runs the given command after the interval has passed. If another command comes in during
     * this interval, the previous one will never run.
     */
    synchronized public void maybeRunAsync(@NonNull Runnable command) {
        if (mCurrentTask != null) {
            mCurrentTask.cancel(false /* mayInterruptIfRunning */);
        }
        ScheduledExecutorService executor = mScheduledExecutorFactory.get();
        mCurrentTask = executor.schedule(command, mIntervalMs, TimeUnit.MILLISECONDS);
        executor.shutdown();
    }
}
