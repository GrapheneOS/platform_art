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

public class Main {
    public static void main(String[] args) {
        $noinline$testLeadingZerosInt();
        $noinline$testLeadingZerosLong();
        $noinline$testTrailingZerosInt();
        $noinline$testTrailingZerosLong();
    }

    /// CHECK-START: void Main.$noinline$testLeadingZerosInt() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerNumberOfLeadingZeros

    /// CHECK-START: void Main.$noinline$testLeadingZerosInt() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerNumberOfLeadingZeros
    private static void $noinline$testLeadingZerosInt() {
        $noinline$assertIntEquals(32, Integer.numberOfLeadingZeros(0));
        $noinline$assertIntEquals(31, Integer.numberOfLeadingZeros(1 << 0));
        $noinline$assertIntEquals(30, Integer.numberOfLeadingZeros(1 << 1));
        $noinline$assertIntEquals(29, Integer.numberOfLeadingZeros(1 << 2));
        $noinline$assertIntEquals(28, Integer.numberOfLeadingZeros(1 << 3));
        $noinline$assertIntEquals(27, Integer.numberOfLeadingZeros(1 << 4));
        $noinline$assertIntEquals(26, Integer.numberOfLeadingZeros(1 << 5));
        $noinline$assertIntEquals(25, Integer.numberOfLeadingZeros(1 << 6));
        $noinline$assertIntEquals(24, Integer.numberOfLeadingZeros(1 << 7));
        $noinline$assertIntEquals(23, Integer.numberOfLeadingZeros(1 << 8));
        $noinline$assertIntEquals(22, Integer.numberOfLeadingZeros(1 << 9));
        $noinline$assertIntEquals(21, Integer.numberOfLeadingZeros(1 << 10));
        $noinline$assertIntEquals(20, Integer.numberOfLeadingZeros(1 << 11));
        $noinline$assertIntEquals(19, Integer.numberOfLeadingZeros(1 << 12));
        $noinline$assertIntEquals(18, Integer.numberOfLeadingZeros(1 << 13));
        $noinline$assertIntEquals(17, Integer.numberOfLeadingZeros(1 << 14));
        $noinline$assertIntEquals(16, Integer.numberOfLeadingZeros(1 << 15));
        $noinline$assertIntEquals(15, Integer.numberOfLeadingZeros(1 << 16));
        $noinline$assertIntEquals(14, Integer.numberOfLeadingZeros(1 << 17));
        $noinline$assertIntEquals(13, Integer.numberOfLeadingZeros(1 << 18));
        $noinline$assertIntEquals(12, Integer.numberOfLeadingZeros(1 << 19));
        $noinline$assertIntEquals(11, Integer.numberOfLeadingZeros(1 << 20));
        $noinline$assertIntEquals(10, Integer.numberOfLeadingZeros(1 << 21));
        $noinline$assertIntEquals(9, Integer.numberOfLeadingZeros(1 << 22));
        $noinline$assertIntEquals(8, Integer.numberOfLeadingZeros(1 << 23));
        $noinline$assertIntEquals(7, Integer.numberOfLeadingZeros(1 << 24));
        $noinline$assertIntEquals(6, Integer.numberOfLeadingZeros(1 << 25));
        $noinline$assertIntEquals(5, Integer.numberOfLeadingZeros(1 << 26));
        $noinline$assertIntEquals(4, Integer.numberOfLeadingZeros(1 << 27));
        $noinline$assertIntEquals(3, Integer.numberOfLeadingZeros(1 << 28));
        $noinline$assertIntEquals(2, Integer.numberOfLeadingZeros(1 << 29));
        $noinline$assertIntEquals(1, Integer.numberOfLeadingZeros(1 << 30));
        $noinline$assertIntEquals(0, Integer.numberOfLeadingZeros(1 << 31));
    }

    private static void $noinline$testLeadingZerosLong() {
        // We need to do two smaller methods because otherwise we don't compile it due to our
        // heuristics.
        $noinline$testLeadingZerosLongFirst32();
        $noinline$testLeadingZerosLongLast32();
    }

    /// CHECK-START: void Main.$noinline$testLeadingZerosLongFirst32() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros

    /// CHECK-START: void Main.$noinline$testLeadingZerosLongFirst32() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros
    private static void $noinline$testLeadingZerosLongFirst32() {
        $noinline$assertIntEquals(64, Long.numberOfLeadingZeros(0L));
        $noinline$assertIntEquals(63, Long.numberOfLeadingZeros(1L << 0L));
        $noinline$assertIntEquals(62, Long.numberOfLeadingZeros(1L << 1L));
        $noinline$assertIntEquals(61, Long.numberOfLeadingZeros(1L << 2L));
        $noinline$assertIntEquals(60, Long.numberOfLeadingZeros(1L << 3L));
        $noinline$assertIntEquals(59, Long.numberOfLeadingZeros(1L << 4L));
        $noinline$assertIntEquals(58, Long.numberOfLeadingZeros(1L << 5L));
        $noinline$assertIntEquals(57, Long.numberOfLeadingZeros(1L << 6L));
        $noinline$assertIntEquals(56, Long.numberOfLeadingZeros(1L << 7L));
        $noinline$assertIntEquals(55, Long.numberOfLeadingZeros(1L << 8L));
        $noinline$assertIntEquals(54, Long.numberOfLeadingZeros(1L << 9L));
        $noinline$assertIntEquals(53, Long.numberOfLeadingZeros(1L << 10L));
        $noinline$assertIntEquals(52, Long.numberOfLeadingZeros(1L << 11L));
        $noinline$assertIntEquals(51, Long.numberOfLeadingZeros(1L << 12L));
        $noinline$assertIntEquals(50, Long.numberOfLeadingZeros(1L << 13L));
        $noinline$assertIntEquals(49, Long.numberOfLeadingZeros(1L << 14L));
        $noinline$assertIntEquals(48, Long.numberOfLeadingZeros(1L << 15L));
        $noinline$assertIntEquals(47, Long.numberOfLeadingZeros(1L << 16L));
        $noinline$assertIntEquals(46, Long.numberOfLeadingZeros(1L << 17L));
        $noinline$assertIntEquals(45, Long.numberOfLeadingZeros(1L << 18L));
        $noinline$assertIntEquals(44, Long.numberOfLeadingZeros(1L << 19L));
        $noinline$assertIntEquals(43, Long.numberOfLeadingZeros(1L << 20L));
        $noinline$assertIntEquals(42, Long.numberOfLeadingZeros(1L << 21L));
        $noinline$assertIntEquals(41, Long.numberOfLeadingZeros(1L << 22L));
        $noinline$assertIntEquals(40, Long.numberOfLeadingZeros(1L << 23L));
        $noinline$assertIntEquals(39, Long.numberOfLeadingZeros(1L << 24L));
        $noinline$assertIntEquals(38, Long.numberOfLeadingZeros(1L << 25L));
        $noinline$assertIntEquals(37, Long.numberOfLeadingZeros(1L << 26L));
        $noinline$assertIntEquals(36, Long.numberOfLeadingZeros(1L << 27L));
        $noinline$assertIntEquals(35, Long.numberOfLeadingZeros(1L << 28L));
        $noinline$assertIntEquals(34, Long.numberOfLeadingZeros(1L << 29L));
        $noinline$assertIntEquals(33, Long.numberOfLeadingZeros(1L << 30L));
    }

    /// CHECK-START: void Main.$noinline$testLeadingZerosLongLast32() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros

    /// CHECK-START: void Main.$noinline$testLeadingZerosLongLast32() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros
    private static void $noinline$testLeadingZerosLongLast32() {
        $noinline$assertIntEquals(32, Long.numberOfLeadingZeros(1L << 31L));
        $noinline$assertIntEquals(31, Long.numberOfLeadingZeros(1L << 32L));
        $noinline$assertIntEquals(30, Long.numberOfLeadingZeros(1L << 33L));
        $noinline$assertIntEquals(29, Long.numberOfLeadingZeros(1L << 34L));
        $noinline$assertIntEquals(28, Long.numberOfLeadingZeros(1L << 35L));
        $noinline$assertIntEquals(27, Long.numberOfLeadingZeros(1L << 36L));
        $noinline$assertIntEquals(26, Long.numberOfLeadingZeros(1L << 37L));
        $noinline$assertIntEquals(25, Long.numberOfLeadingZeros(1L << 38L));
        $noinline$assertIntEquals(24, Long.numberOfLeadingZeros(1L << 39L));
        $noinline$assertIntEquals(23, Long.numberOfLeadingZeros(1L << 40L));
        $noinline$assertIntEquals(22, Long.numberOfLeadingZeros(1L << 41L));
        $noinline$assertIntEquals(21, Long.numberOfLeadingZeros(1L << 42L));
        $noinline$assertIntEquals(20, Long.numberOfLeadingZeros(1L << 43L));
        $noinline$assertIntEquals(19, Long.numberOfLeadingZeros(1L << 44L));
        $noinline$assertIntEquals(18, Long.numberOfLeadingZeros(1L << 45L));
        $noinline$assertIntEquals(17, Long.numberOfLeadingZeros(1L << 46L));
        $noinline$assertIntEquals(16, Long.numberOfLeadingZeros(1L << 47L));
        $noinline$assertIntEquals(15, Long.numberOfLeadingZeros(1L << 48L));
        $noinline$assertIntEquals(14, Long.numberOfLeadingZeros(1L << 49L));
        $noinline$assertIntEquals(13, Long.numberOfLeadingZeros(1L << 50L));
        $noinline$assertIntEquals(12, Long.numberOfLeadingZeros(1L << 51L));
        $noinline$assertIntEquals(11, Long.numberOfLeadingZeros(1L << 52L));
        $noinline$assertIntEquals(10, Long.numberOfLeadingZeros(1L << 53L));
        $noinline$assertIntEquals(9, Long.numberOfLeadingZeros(1L << 54L));
        $noinline$assertIntEquals(8, Long.numberOfLeadingZeros(1L << 55L));
        $noinline$assertIntEquals(7, Long.numberOfLeadingZeros(1L << 56L));
        $noinline$assertIntEquals(6, Long.numberOfLeadingZeros(1L << 57L));
        $noinline$assertIntEquals(5, Long.numberOfLeadingZeros(1L << 58L));
        $noinline$assertIntEquals(4, Long.numberOfLeadingZeros(1L << 59L));
        $noinline$assertIntEquals(3, Long.numberOfLeadingZeros(1L << 60L));
        $noinline$assertIntEquals(2, Long.numberOfLeadingZeros(1L << 61L));
        $noinline$assertIntEquals(1, Long.numberOfLeadingZeros(1L << 62L));
        $noinline$assertIntEquals(0, Long.numberOfLeadingZeros(1L << 63L));
    }

    /// CHECK-START: void Main.$noinline$testTrailingZerosInt() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerNumberOfTrailingZeros

    /// CHECK-START: void Main.$noinline$testTrailingZerosInt() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerNumberOfTrailingZeros
    private static void $noinline$testTrailingZerosInt() {
        $noinline$assertIntEquals(32, Integer.numberOfTrailingZeros(0));
        $noinline$assertIntEquals(0, Integer.numberOfTrailingZeros(1 << 0));
        $noinline$assertIntEquals(1, Integer.numberOfTrailingZeros(1 << 1));
        $noinline$assertIntEquals(2, Integer.numberOfTrailingZeros(1 << 2));
        $noinline$assertIntEquals(3, Integer.numberOfTrailingZeros(1 << 3));
        $noinline$assertIntEquals(4, Integer.numberOfTrailingZeros(1 << 4));
        $noinline$assertIntEquals(5, Integer.numberOfTrailingZeros(1 << 5));
        $noinline$assertIntEquals(6, Integer.numberOfTrailingZeros(1 << 6));
        $noinline$assertIntEquals(7, Integer.numberOfTrailingZeros(1 << 7));
        $noinline$assertIntEquals(8, Integer.numberOfTrailingZeros(1 << 8));
        $noinline$assertIntEquals(9, Integer.numberOfTrailingZeros(1 << 9));
        $noinline$assertIntEquals(10, Integer.numberOfTrailingZeros(1 << 10));
        $noinline$assertIntEquals(11, Integer.numberOfTrailingZeros(1 << 11));
        $noinline$assertIntEquals(12, Integer.numberOfTrailingZeros(1 << 12));
        $noinline$assertIntEquals(13, Integer.numberOfTrailingZeros(1 << 13));
        $noinline$assertIntEquals(14, Integer.numberOfTrailingZeros(1 << 14));
        $noinline$assertIntEquals(15, Integer.numberOfTrailingZeros(1 << 15));
        $noinline$assertIntEquals(16, Integer.numberOfTrailingZeros(1 << 16));
        $noinline$assertIntEquals(17, Integer.numberOfTrailingZeros(1 << 17));
        $noinline$assertIntEquals(18, Integer.numberOfTrailingZeros(1 << 18));
        $noinline$assertIntEquals(19, Integer.numberOfTrailingZeros(1 << 19));
        $noinline$assertIntEquals(20, Integer.numberOfTrailingZeros(1 << 20));
        $noinline$assertIntEquals(21, Integer.numberOfTrailingZeros(1 << 21));
        $noinline$assertIntEquals(22, Integer.numberOfTrailingZeros(1 << 22));
        $noinline$assertIntEquals(23, Integer.numberOfTrailingZeros(1 << 23));
        $noinline$assertIntEquals(24, Integer.numberOfTrailingZeros(1 << 24));
        $noinline$assertIntEquals(25, Integer.numberOfTrailingZeros(1 << 25));
        $noinline$assertIntEquals(26, Integer.numberOfTrailingZeros(1 << 26));
        $noinline$assertIntEquals(27, Integer.numberOfTrailingZeros(1 << 27));
        $noinline$assertIntEquals(28, Integer.numberOfTrailingZeros(1 << 28));
        $noinline$assertIntEquals(29, Integer.numberOfTrailingZeros(1 << 29));
        $noinline$assertIntEquals(30, Integer.numberOfTrailingZeros(1 << 30));
        $noinline$assertIntEquals(31, Integer.numberOfTrailingZeros(1 << 31));
    }

    private static void $noinline$testTrailingZerosLong() {
        // We need to do two smaller methods because otherwise we don't compile it due to our
        // heuristics.
        $noinline$testTrailingZerosLongFirst32();
        $noinline$testTrailingZerosLongLast32();
    }

    /// CHECK-START: void Main.$noinline$testTrailingZerosLongFirst32() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros

    /// CHECK-START: void Main.$noinline$testTrailingZerosLongFirst32() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros
    private static void $noinline$testTrailingZerosLongFirst32() {
        $noinline$assertIntEquals(64, Long.numberOfTrailingZeros(0L));
        $noinline$assertIntEquals(0, Long.numberOfTrailingZeros(1L << 0L));
        $noinline$assertIntEquals(1, Long.numberOfTrailingZeros(1L << 1L));
        $noinline$assertIntEquals(2, Long.numberOfTrailingZeros(1L << 2L));
        $noinline$assertIntEquals(3, Long.numberOfTrailingZeros(1L << 3L));
        $noinline$assertIntEquals(4, Long.numberOfTrailingZeros(1L << 4L));
        $noinline$assertIntEquals(5, Long.numberOfTrailingZeros(1L << 5L));
        $noinline$assertIntEquals(6, Long.numberOfTrailingZeros(1L << 6L));
        $noinline$assertIntEquals(7, Long.numberOfTrailingZeros(1L << 7L));
        $noinline$assertIntEquals(8, Long.numberOfTrailingZeros(1L << 8L));
        $noinline$assertIntEquals(9, Long.numberOfTrailingZeros(1L << 9L));
        $noinline$assertIntEquals(10, Long.numberOfTrailingZeros(1L << 10L));
        $noinline$assertIntEquals(11, Long.numberOfTrailingZeros(1L << 11L));
        $noinline$assertIntEquals(12, Long.numberOfTrailingZeros(1L << 12L));
        $noinline$assertIntEquals(13, Long.numberOfTrailingZeros(1L << 13L));
        $noinline$assertIntEquals(14, Long.numberOfTrailingZeros(1L << 14L));
        $noinline$assertIntEquals(15, Long.numberOfTrailingZeros(1L << 15L));
        $noinline$assertIntEquals(16, Long.numberOfTrailingZeros(1L << 16L));
        $noinline$assertIntEquals(17, Long.numberOfTrailingZeros(1L << 17L));
        $noinline$assertIntEquals(18, Long.numberOfTrailingZeros(1L << 18L));
        $noinline$assertIntEquals(19, Long.numberOfTrailingZeros(1L << 19L));
        $noinline$assertIntEquals(20, Long.numberOfTrailingZeros(1L << 20L));
        $noinline$assertIntEquals(21, Long.numberOfTrailingZeros(1L << 21L));
        $noinline$assertIntEquals(22, Long.numberOfTrailingZeros(1L << 22L));
        $noinline$assertIntEquals(23, Long.numberOfTrailingZeros(1L << 23L));
        $noinline$assertIntEquals(24, Long.numberOfTrailingZeros(1L << 24L));
        $noinline$assertIntEquals(25, Long.numberOfTrailingZeros(1L << 25L));
        $noinline$assertIntEquals(26, Long.numberOfTrailingZeros(1L << 26L));
        $noinline$assertIntEquals(27, Long.numberOfTrailingZeros(1L << 27L));
        $noinline$assertIntEquals(28, Long.numberOfTrailingZeros(1L << 28L));
        $noinline$assertIntEquals(29, Long.numberOfTrailingZeros(1L << 29L));
        $noinline$assertIntEquals(30, Long.numberOfTrailingZeros(1L << 30L));
        $noinline$assertIntEquals(31, Long.numberOfTrailingZeros(1L << 31L));
    }

    /// CHECK-START: void Main.$noinline$testTrailingZerosLongLast32() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros

    /// CHECK-START: void Main.$noinline$testTrailingZerosLongLast32() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros
    private static void $noinline$testTrailingZerosLongLast32() {
        $noinline$assertIntEquals(32, Long.numberOfTrailingZeros(1L << 32L));
        $noinline$assertIntEquals(33, Long.numberOfTrailingZeros(1L << 33L));
        $noinline$assertIntEquals(34, Long.numberOfTrailingZeros(1L << 34L));
        $noinline$assertIntEquals(35, Long.numberOfTrailingZeros(1L << 35L));
        $noinline$assertIntEquals(36, Long.numberOfTrailingZeros(1L << 36L));
        $noinline$assertIntEquals(37, Long.numberOfTrailingZeros(1L << 37L));
        $noinline$assertIntEquals(38, Long.numberOfTrailingZeros(1L << 38L));
        $noinline$assertIntEquals(39, Long.numberOfTrailingZeros(1L << 39L));
        $noinline$assertIntEquals(40, Long.numberOfTrailingZeros(1L << 40L));
        $noinline$assertIntEquals(41, Long.numberOfTrailingZeros(1L << 41L));
        $noinline$assertIntEquals(42, Long.numberOfTrailingZeros(1L << 42L));
        $noinline$assertIntEquals(43, Long.numberOfTrailingZeros(1L << 43L));
        $noinline$assertIntEquals(44, Long.numberOfTrailingZeros(1L << 44L));
        $noinline$assertIntEquals(45, Long.numberOfTrailingZeros(1L << 45L));
        $noinline$assertIntEquals(46, Long.numberOfTrailingZeros(1L << 46L));
        $noinline$assertIntEquals(47, Long.numberOfTrailingZeros(1L << 47L));
        $noinline$assertIntEquals(48, Long.numberOfTrailingZeros(1L << 48L));
        $noinline$assertIntEquals(49, Long.numberOfTrailingZeros(1L << 49L));
        $noinline$assertIntEquals(50, Long.numberOfTrailingZeros(1L << 50L));
        $noinline$assertIntEquals(51, Long.numberOfTrailingZeros(1L << 51L));
        $noinline$assertIntEquals(52, Long.numberOfTrailingZeros(1L << 52L));
        $noinline$assertIntEquals(53, Long.numberOfTrailingZeros(1L << 53L));
        $noinline$assertIntEquals(54, Long.numberOfTrailingZeros(1L << 54L));
        $noinline$assertIntEquals(55, Long.numberOfTrailingZeros(1L << 55L));
        $noinline$assertIntEquals(56, Long.numberOfTrailingZeros(1L << 56L));
        $noinline$assertIntEquals(57, Long.numberOfTrailingZeros(1L << 57L));
        $noinline$assertIntEquals(58, Long.numberOfTrailingZeros(1L << 58L));
        $noinline$assertIntEquals(59, Long.numberOfTrailingZeros(1L << 59L));
        $noinline$assertIntEquals(60, Long.numberOfTrailingZeros(1L << 60L));
        $noinline$assertIntEquals(61, Long.numberOfTrailingZeros(1L << 61L));
        $noinline$assertIntEquals(62, Long.numberOfTrailingZeros(1L << 62L));
        $noinline$assertIntEquals(63, Long.numberOfTrailingZeros(1L << 63L));
    }

    public static void $noinline$assertIntEquals(int expected, int result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
