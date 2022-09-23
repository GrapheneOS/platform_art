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

package com.android.server.art.testing;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class TestingUtilsTest {
    @Test
    public void testDeepEquals() {
        var a = new Foo();
        var b = new Foo();
        assertThat(TestingUtils.deepEquals(a, b)).isTrue();
    }

    @Test
    public void testDeepEqualsNull() {
        assertThat(TestingUtils.deepEquals(null, null)).isTrue();
    }

    @Test
    public void testDeepEqualsNullabilityMismatch() {
        var a = new Foo();
        assertThat(TestingUtils.deepEquals(a, null)).isFalse();
    }

    @Test
    public void testDeepEqualsTypeMismatch() {
        var a = new Foo();
        var b = new Bar();
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test
    public void testDeepEqualsPrimitiveFieldMismatch() {
        var a = new Foo();
        var b = new Foo();
        b.mA = 11111111;
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test
    public void testDeepEqualsStringFieldMismatch() {
        var a = new Foo();
        var b = new Foo();
        b.mB = "def";
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test
    public void deepEqualsNestedFieldMismatch() {
        var a = new Foo();
        var b = new Foo();
        b.mC.setB(11111111);
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test(expected = UnsupportedOperationException.class)
    public void testDeepEqualsArrayNotSupported() throws Exception {
        int[] a = new int[] {1};
        int[] b = new int[] {2};
        TestingUtils.deepEquals(a, b);
    }

    @Test
    public void testListDeepEquals() throws Exception {
        var a = new ArrayList<Integer>();
        a.add(1);
        a.add(2);
        a.add(3);
        a.add(4);
        a.add(5);
        var b = List.of(1, 2, 3, 4, 5);
        assertThat(TestingUtils.deepEquals(a, b)).isTrue();
    }

    @Test
    public void testListDeepEqualsSizeMismatch() throws Exception {
        var a = new ArrayList<Integer>();
        a.add(1);
        var b = new ArrayList<Integer>();
        b.add(1);
        b.add(2);
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test
    public void testListDeepEqualsElementMismatch() throws Exception {
        var a = new ArrayList<Integer>();
        a.add(1);
        var b = new ArrayList<Integer>();
        b.add(2);
        assertThat(TestingUtils.deepEquals(a, b)).isFalse();
    }

    @Test(expected = UnsupportedOperationException.class)
    public void testDeepEqualsOtherContainerNotSupported() throws Exception {
        var a = new HashSet<Integer>();
        a.add(1);
        var b = new HashSet<Integer>();
        b.add(2);
        TestingUtils.deepEquals(a, b);
    }
}

class Foo {
    public int mA = 1234567;
    public String mB = "abc";
    public Bar mC = new Bar();
}

class Bar {
    public static int sA = 10000000;
    private int mB = 7654321;
    public void setB(int b) {
        mB = b;
    }
}
