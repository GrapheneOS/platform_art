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

package com.android.server.art;

import static com.google.common.truth.Truth.assertThat;

import android.util.SparseArray;

import androidx.test.filters.SmallTest;

import com.android.server.art.Utils;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.List;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class UtilsTest {
    @Test
    public void testCollectionIsEmptyTrue() {
        assertThat(Utils.isEmpty(List.of())).isTrue();
    }

    @Test
    public void testCollectionIsEmptyFalse() {
        assertThat(Utils.isEmpty(List.of(1))).isFalse();
    }

    @Test
    public void testSparseArrayIsEmptyTrue() {
        assertThat(Utils.isEmpty(new SparseArray<Integer>())).isTrue();
    }

    @Test
    public void testSparseArrayIsEmptyFalse() {
        SparseArray<Integer> array = new SparseArray<>();
        array.put(1, 1);
        assertThat(Utils.isEmpty(array)).isFalse();
    }

    @Test
    public void testArrayIsEmptyTrue() {
        assertThat(Utils.isEmpty(new int[0])).isTrue();
    }

    @Test
    public void testArrayIsEmptyFalse() {
        assertThat(Utils.isEmpty(new int[] {1})).isFalse();
    }

    @Test
    public void testImplies() {
        assertThat(Utils.implies(false, false)).isTrue();
        assertThat(Utils.implies(false, true)).isTrue();
        assertThat(Utils.implies(true, false)).isFalse();
        assertThat(Utils.implies(true, true)).isTrue();
    }

    @Test
    public void testCheck() {
        Utils.check(true);
    }

    @Test(expected = IllegalStateException.class)
    public void testCheckFailed() throws Exception {
        Utils.check(false);
    }
}
