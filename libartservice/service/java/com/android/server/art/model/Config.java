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

package com.android.server.art.model;

import static com.android.server.art.ArtManagerLocal.OptimizePackagesCallback;
import static com.android.server.art.ArtManagerLocal.ScheduleBackgroundDexoptJobCallback;

import android.annotation.NonNull;
import android.annotation.Nullable;

import com.android.internal.annotations.GuardedBy;
import com.android.server.art.ArtManagerLocal;

import com.google.auto.value.AutoValue;

import java.util.concurrent.Executor;

/**
 * A class that stores the configurations set by the consumer of ART Service at runtime. This class
 * is thread-safe.
 *
 * @hide
 */
public class Config {
    /** @see ArtManagerLocal#setOptimizePackagesCallback(Executor, OptimizePackagesCallback) */
    @GuardedBy("this")
    @Nullable
    private Callback<OptimizePackagesCallback> mOptimizePackagesCallback = null;

    /**
     * @see ArtManagerLocal#setScheduleBackgroundDexoptJobCallback(Executor,
     *         ScheduleBackgroundDexoptJobCallback)
     */
    @GuardedBy("this")
    @Nullable
    private Callback<ScheduleBackgroundDexoptJobCallback> mScheduleBackgroundDexoptJobCallback =
            null;

    public synchronized void setOptimizePackagesCallback(
            @NonNull Executor executor, @NonNull OptimizePackagesCallback callback) {
        mOptimizePackagesCallback = Callback.<OptimizePackagesCallback>create(callback, executor);
    }

    public synchronized void clearOptimizePackagesCallback() {
        mOptimizePackagesCallback = null;
    }

    @Nullable
    public synchronized Callback<OptimizePackagesCallback> getOptimizePackagesCallback() {
        return mOptimizePackagesCallback;
    }

    public synchronized void setScheduleBackgroundDexoptJobCallback(
            @NonNull Executor executor, @NonNull ScheduleBackgroundDexoptJobCallback callback) {
        mScheduleBackgroundDexoptJobCallback =
                Callback.<ScheduleBackgroundDexoptJobCallback>create(callback, executor);
    }

    public synchronized void clearScheduleBackgroundDexoptJobCallback() {
        mScheduleBackgroundDexoptJobCallback = null;
    }

    @Nullable
    public synchronized Callback<ScheduleBackgroundDexoptJobCallback>
    getScheduleBackgroundDexoptJobCallback() {
        return mScheduleBackgroundDexoptJobCallback;
    }

    @AutoValue
    public static abstract class Callback<T> {
        public abstract @NonNull T get();
        public abstract @NonNull Executor executor();
        static <T> @NonNull Callback<T> create(@NonNull T callback, @NonNull Executor executor) {
            return new AutoValue_Config_Callback<T>(callback, executor);
        }
    }
}
