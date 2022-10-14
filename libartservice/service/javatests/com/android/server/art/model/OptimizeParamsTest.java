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
 * limitations under the License
 */

package com.android.server.art.model;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class OptimizeParamsTest {
    @Test
    public void testBuild() {
        new OptimizeParams.Builder("install").build();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testBuildEmptyReason() {
        new OptimizeParams.Builder("").build();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testBuildInvalidCompilerFilter() {
        new OptimizeParams.Builder("install").setCompilerFilter("invalid").build();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testBuildInvalidPriorityClass() {
        new OptimizeParams.Builder("install").setPriorityClass(101).build();
    }

    @Test
    public void testBuildCustomReason() {
        new OptimizeParams.Builder("custom")
                .setCompilerFilter("speed")
                .setPriorityClass(90)
                .build();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testBuildCustomReasonEmptyCompilerFilter() {
        new OptimizeParams.Builder("custom")
                .setPriorityClass(ArtFlags.PRIORITY_INTERACTIVE)
                .build();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testBuildCustomReasonEmptyPriorityClass() {
        new OptimizeParams.Builder("custom").setCompilerFilter("speed").build();
    }
}
