/*
 * Copyright (C) 2023 The Android Open Source Project
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

import java.lang.reflect.Field;

import sun.misc.Unsafe;

public class Main {
    private static final Unsafe unsafe = getUnsafe();
    public int i = 0;
    public long l = 0;

    public static void main(String[] args) {
        $noinline$testGetAndAdd();
    }

    private static void $noinline$testGetAndAdd() {
        final Main m = new Main();
        final long intOffset, longOffset;
        try {
            Field intField = Main.class.getDeclaredField("i");
            Field longField = Main.class.getDeclaredField("l");

            intOffset = unsafe.objectFieldOffset(intField);
            longOffset = unsafe.objectFieldOffset(longField);

        } catch (NoSuchFieldException e) {
            throw new Error("No offset: " + e);
        }

        $noinline$add(m, intOffset, 11);
        assertEquals(11, m.i);
        $noinline$add(m, intOffset, 13);
        assertEquals(24, m.i);

        $noinline$add(m, longOffset, 11L);
        assertEquals(11L, m.l);
        $noinline$add(m, longOffset, 13L);
        assertEquals(24L, m.l);
    }

    // UnsafeGetAndAddInt/Long are part of core-oj and we will not inline on host.

    /// CHECK-START-{ARM,ARM64}: int Main.$noinline$add(java.lang.Object, long, int) inliner (before)
    /// CHECK:     InvokeVirtual intrinsic:UnsafeGetAndAddInt

    /// CHECK-START-{ARM,ARM64}: int Main.$noinline$add(java.lang.Object, long, int) inliner (after)
    /// CHECK-NOT: InvokeVirtual intrinsic:UnsafeGetAndAddInt
    private static int $noinline$add(Object o, long offset, int delta) {
        return unsafe.getAndAddInt(o, offset, delta);
    }

    /// CHECK-START-{ARM,ARM64}: long Main.$noinline$add(java.lang.Object, long, long) inliner (before)
    /// CHECK:     InvokeVirtual intrinsic:UnsafeGetAndAddLong

    /// CHECK-START-{ARM,ARM64}: long Main.$noinline$add(java.lang.Object, long, long) inliner (after)
    /// CHECK-NOT: InvokeVirtual intrinsic:UnsafeGetAndAddLong
    private static long $noinline$add(Object o, long offset, long delta) {
        return unsafe.getAndAddLong(o, offset, delta);
    }

    private static Unsafe getUnsafe() {
        try {
            Class<?> unsafeClass = Unsafe.class;
            Field f = unsafeClass.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            return (Unsafe) f.get(null);
        } catch (Exception e) {
            throw new Error("Cannot get Unsafe instance");
        }
    }

    private static void assertEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }

    private static void assertEquals(long expected, long result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
