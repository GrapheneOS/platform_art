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
        $noinline$testHighestOneBitInt();
        $noinline$testHighestOneBitLong();
        $noinline$testLowestOneBitInt();
        $noinline$testLowestOneBitLong();
        $noinline$testLeadingZerosInt();
        $noinline$testLeadingZerosLong();
        $noinline$testTrailingZerosInt();
        $noinline$testTrailingZerosLong();
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit
    private static void $noinline$testHighestOneBitInt() {
        $noinline$assertIntEquals(1 << 30, Integer.highestOneBit(Integer.MAX_VALUE));
        $noinline$assertIntEquals(1 << 31, Integer.highestOneBit(Integer.MIN_VALUE));
        $noinline$testHighestOneBitInt_powerOfTwo();
        $noinline$testHighestOneBitInt_powerOfTwoMinusOne();
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit
    private static void $noinline$testHighestOneBitInt_powerOfTwo() {
        $noinline$assertIntEquals(0, Integer.highestOneBit(0));
        $noinline$assertIntEquals(1 << 0, Integer.highestOneBit(1 << 0));
        $noinline$assertIntEquals(1 << 1, Integer.highestOneBit(1 << 1));
        $noinline$assertIntEquals(1 << 2, Integer.highestOneBit(1 << 2));
        $noinline$assertIntEquals(1 << 3, Integer.highestOneBit(1 << 3));
        $noinline$assertIntEquals(1 << 4, Integer.highestOneBit(1 << 4));
        $noinline$assertIntEquals(1 << 5, Integer.highestOneBit(1 << 5));
        $noinline$assertIntEquals(1 << 6, Integer.highestOneBit(1 << 6));
        $noinline$assertIntEquals(1 << 7, Integer.highestOneBit(1 << 7));
        $noinline$assertIntEquals(1 << 8, Integer.highestOneBit(1 << 8));
        $noinline$assertIntEquals(1 << 9, Integer.highestOneBit(1 << 9));
        $noinline$assertIntEquals(1 << 10, Integer.highestOneBit(1 << 10));
        $noinline$assertIntEquals(1 << 11, Integer.highestOneBit(1 << 11));
        $noinline$assertIntEquals(1 << 12, Integer.highestOneBit(1 << 12));
        $noinline$assertIntEquals(1 << 13, Integer.highestOneBit(1 << 13));
        $noinline$assertIntEquals(1 << 14, Integer.highestOneBit(1 << 14));
        $noinline$assertIntEquals(1 << 15, Integer.highestOneBit(1 << 15));
        $noinline$assertIntEquals(1 << 16, Integer.highestOneBit(1 << 16));
        $noinline$assertIntEquals(1 << 17, Integer.highestOneBit(1 << 17));
        $noinline$assertIntEquals(1 << 18, Integer.highestOneBit(1 << 18));
        $noinline$assertIntEquals(1 << 19, Integer.highestOneBit(1 << 19));
        $noinline$assertIntEquals(1 << 20, Integer.highestOneBit(1 << 20));
        $noinline$assertIntEquals(1 << 21, Integer.highestOneBit(1 << 21));
        $noinline$assertIntEquals(1 << 22, Integer.highestOneBit(1 << 22));
        $noinline$assertIntEquals(1 << 23, Integer.highestOneBit(1 << 23));
        $noinline$assertIntEquals(1 << 24, Integer.highestOneBit(1 << 24));
        $noinline$assertIntEquals(1 << 25, Integer.highestOneBit(1 << 25));
        $noinline$assertIntEquals(1 << 26, Integer.highestOneBit(1 << 26));
        $noinline$assertIntEquals(1 << 27, Integer.highestOneBit(1 << 27));
        $noinline$assertIntEquals(1 << 28, Integer.highestOneBit(1 << 28));
        $noinline$assertIntEquals(1 << 29, Integer.highestOneBit(1 << 29));
        $noinline$assertIntEquals(1 << 30, Integer.highestOneBit(1 << 30));
        $noinline$assertIntEquals(1 << 31, Integer.highestOneBit(1 << 31));
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitInt_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerHighestOneBit
    private static void $noinline$testHighestOneBitInt_powerOfTwoMinusOne() {
        // We start on `(1 << 2) - 1` (i.e. `3`) as the other values are already being tested.
        $noinline$assertIntEquals(1 << (2 - 1), Integer.highestOneBit((1 << 2) - 1));
        $noinline$assertIntEquals(1 << (3 - 1), Integer.highestOneBit((1 << 3) - 1));
        $noinline$assertIntEquals(1 << (4 - 1), Integer.highestOneBit((1 << 4) - 1));
        $noinline$assertIntEquals(1 << (5 - 1), Integer.highestOneBit((1 << 5) - 1));
        $noinline$assertIntEquals(1 << (6 - 1), Integer.highestOneBit((1 << 6) - 1));
        $noinline$assertIntEquals(1 << (7 - 1), Integer.highestOneBit((1 << 7) - 1));
        $noinline$assertIntEquals(1 << (8 - 1), Integer.highestOneBit((1 << 8) - 1));
        $noinline$assertIntEquals(1 << (9 - 1), Integer.highestOneBit((1 << 9) - 1));
        $noinline$assertIntEquals(1 << (10 - 1), Integer.highestOneBit((1 << 10) - 1));
        $noinline$assertIntEquals(1 << (11 - 1), Integer.highestOneBit((1 << 11) - 1));
        $noinline$assertIntEquals(1 << (12 - 1), Integer.highestOneBit((1 << 12) - 1));
        $noinline$assertIntEquals(1 << (13 - 1), Integer.highestOneBit((1 << 13) - 1));
        $noinline$assertIntEquals(1 << (14 - 1), Integer.highestOneBit((1 << 14) - 1));
        $noinline$assertIntEquals(1 << (15 - 1), Integer.highestOneBit((1 << 15) - 1));
        $noinline$assertIntEquals(1 << (16 - 1), Integer.highestOneBit((1 << 16) - 1));
        $noinline$assertIntEquals(1 << (17 - 1), Integer.highestOneBit((1 << 17) - 1));
        $noinline$assertIntEquals(1 << (18 - 1), Integer.highestOneBit((1 << 18) - 1));
        $noinline$assertIntEquals(1 << (19 - 1), Integer.highestOneBit((1 << 19) - 1));
        $noinline$assertIntEquals(1 << (20 - 1), Integer.highestOneBit((1 << 20) - 1));
        $noinline$assertIntEquals(1 << (21 - 1), Integer.highestOneBit((1 << 21) - 1));
        $noinline$assertIntEquals(1 << (22 - 1), Integer.highestOneBit((1 << 22) - 1));
        $noinline$assertIntEquals(1 << (23 - 1), Integer.highestOneBit((1 << 23) - 1));
        $noinline$assertIntEquals(1 << (24 - 1), Integer.highestOneBit((1 << 24) - 1));
        $noinline$assertIntEquals(1 << (25 - 1), Integer.highestOneBit((1 << 25) - 1));
        $noinline$assertIntEquals(1 << (26 - 1), Integer.highestOneBit((1 << 26) - 1));
        $noinline$assertIntEquals(1 << (27 - 1), Integer.highestOneBit((1 << 27) - 1));
        $noinline$assertIntEquals(1 << (28 - 1), Integer.highestOneBit((1 << 28) - 1));
        $noinline$assertIntEquals(1 << (29 - 1), Integer.highestOneBit((1 << 29) - 1));
        $noinline$assertIntEquals(1 << (30 - 1), Integer.highestOneBit((1 << 30) - 1));
        $noinline$assertIntEquals(1 << (31 - 1), Integer.highestOneBit((1 << 31) - 1));
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitLong() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitLong() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongHighestOneBit
    private static void $noinline$testHighestOneBitLong() {
        $noinline$assertLongEquals(1L << 62, Long.highestOneBit(Long.MAX_VALUE));
        $noinline$assertLongEquals(1L << 63, Long.highestOneBit(Long.MIN_VALUE));
        // We need to do two smaller methods because otherwise we don't compile it due to our
        // heuristics.
        $noinline$testHighestOneBitLongFirst32_powerOfTwo();
        $noinline$testHighestOneBitLongLast32_powerOfTwo();
        $noinline$testHighestOneBitLongFirst32_powerOfTwoMinusOne();
        $noinline$testHighestOneBitLongLast32_powerOfTwoMinusOne();
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongFirst32_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongFirst32_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongHighestOneBit
    private static void $noinline$testHighestOneBitLongFirst32_powerOfTwo() {
        $noinline$assertLongEquals(0, Long.highestOneBit(0L));
        $noinline$assertLongEquals(1L << 0L, Long.highestOneBit(1L << 0L));
        $noinline$assertLongEquals(1L << 1L, Long.highestOneBit(1L << 1L));
        $noinline$assertLongEquals(1L << 2L, Long.highestOneBit(1L << 2L));
        $noinline$assertLongEquals(1L << 3L, Long.highestOneBit(1L << 3L));
        $noinline$assertLongEquals(1L << 4L, Long.highestOneBit(1L << 4L));
        $noinline$assertLongEquals(1L << 5L, Long.highestOneBit(1L << 5L));
        $noinline$assertLongEquals(1L << 6L, Long.highestOneBit(1L << 6L));
        $noinline$assertLongEquals(1L << 7L, Long.highestOneBit(1L << 7L));
        $noinline$assertLongEquals(1L << 8L, Long.highestOneBit(1L << 8L));
        $noinline$assertLongEquals(1L << 9L, Long.highestOneBit(1L << 9L));
        $noinline$assertLongEquals(1L << 10L, Long.highestOneBit(1L << 10L));
        $noinline$assertLongEquals(1L << 11L, Long.highestOneBit(1L << 11L));
        $noinline$assertLongEquals(1L << 12L, Long.highestOneBit(1L << 12L));
        $noinline$assertLongEquals(1L << 13L, Long.highestOneBit(1L << 13L));
        $noinline$assertLongEquals(1L << 14L, Long.highestOneBit(1L << 14L));
        $noinline$assertLongEquals(1L << 15L, Long.highestOneBit(1L << 15L));
        $noinline$assertLongEquals(1L << 16L, Long.highestOneBit(1L << 16L));
        $noinline$assertLongEquals(1L << 17L, Long.highestOneBit(1L << 17L));
        $noinline$assertLongEquals(1L << 18L, Long.highestOneBit(1L << 18L));
        $noinline$assertLongEquals(1L << 19L, Long.highestOneBit(1L << 19L));
        $noinline$assertLongEquals(1L << 20L, Long.highestOneBit(1L << 20L));
        $noinline$assertLongEquals(1L << 21L, Long.highestOneBit(1L << 21L));
        $noinline$assertLongEquals(1L << 22L, Long.highestOneBit(1L << 22L));
        $noinline$assertLongEquals(1L << 23L, Long.highestOneBit(1L << 23L));
        $noinline$assertLongEquals(1L << 24L, Long.highestOneBit(1L << 24L));
        $noinline$assertLongEquals(1L << 25L, Long.highestOneBit(1L << 25L));
        $noinline$assertLongEquals(1L << 26L, Long.highestOneBit(1L << 26L));
        $noinline$assertLongEquals(1L << 27L, Long.highestOneBit(1L << 27L));
        $noinline$assertLongEquals(1L << 28L, Long.highestOneBit(1L << 28L));
        $noinline$assertLongEquals(1L << 29L, Long.highestOneBit(1L << 29L));
        $noinline$assertLongEquals(1L << 30L, Long.highestOneBit(1L << 30L));
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongLast32_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongLast32_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongHighestOneBit
    private static void $noinline$testHighestOneBitLongLast32_powerOfTwo() {
        $noinline$assertLongEquals(1L << 31L, Long.highestOneBit(1L << 31L));
        $noinline$assertLongEquals(1L << 32L, Long.highestOneBit(1L << 32L));
        $noinline$assertLongEquals(1L << 33L, Long.highestOneBit(1L << 33L));
        $noinline$assertLongEquals(1L << 34L, Long.highestOneBit(1L << 34L));
        $noinline$assertLongEquals(1L << 35L, Long.highestOneBit(1L << 35L));
        $noinline$assertLongEquals(1L << 36L, Long.highestOneBit(1L << 36L));
        $noinline$assertLongEquals(1L << 37L, Long.highestOneBit(1L << 37L));
        $noinline$assertLongEquals(1L << 38L, Long.highestOneBit(1L << 38L));
        $noinline$assertLongEquals(1L << 39L, Long.highestOneBit(1L << 39L));
        $noinline$assertLongEquals(1L << 40L, Long.highestOneBit(1L << 40L));
        $noinline$assertLongEquals(1L << 41L, Long.highestOneBit(1L << 41L));
        $noinline$assertLongEquals(1L << 42L, Long.highestOneBit(1L << 42L));
        $noinline$assertLongEquals(1L << 43L, Long.highestOneBit(1L << 43L));
        $noinline$assertLongEquals(1L << 44L, Long.highestOneBit(1L << 44L));
        $noinline$assertLongEquals(1L << 45L, Long.highestOneBit(1L << 45L));
        $noinline$assertLongEquals(1L << 46L, Long.highestOneBit(1L << 46L));
        $noinline$assertLongEquals(1L << 47L, Long.highestOneBit(1L << 47L));
        $noinline$assertLongEquals(1L << 48L, Long.highestOneBit(1L << 48L));
        $noinline$assertLongEquals(1L << 49L, Long.highestOneBit(1L << 49L));
        $noinline$assertLongEquals(1L << 50L, Long.highestOneBit(1L << 50L));
        $noinline$assertLongEquals(1L << 51L, Long.highestOneBit(1L << 51L));
        $noinline$assertLongEquals(1L << 52L, Long.highestOneBit(1L << 52L));
        $noinline$assertLongEquals(1L << 53L, Long.highestOneBit(1L << 53L));
        $noinline$assertLongEquals(1L << 54L, Long.highestOneBit(1L << 54L));
        $noinline$assertLongEquals(1L << 55L, Long.highestOneBit(1L << 55L));
        $noinline$assertLongEquals(1L << 56L, Long.highestOneBit(1L << 56L));
        $noinline$assertLongEquals(1L << 57L, Long.highestOneBit(1L << 57L));
        $noinline$assertLongEquals(1L << 58L, Long.highestOneBit(1L << 58L));
        $noinline$assertLongEquals(1L << 59L, Long.highestOneBit(1L << 59L));
        $noinline$assertLongEquals(1L << 60L, Long.highestOneBit(1L << 60L));
        $noinline$assertLongEquals(1L << 61L, Long.highestOneBit(1L << 61L));
        $noinline$assertLongEquals(1L << 62L, Long.highestOneBit(1L << 62L));
        $noinline$assertLongEquals(1L << 63L, Long.highestOneBit(1L << 63L));
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongFirst32_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongFirst32_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongHighestOneBit
    private static void $noinline$testHighestOneBitLongFirst32_powerOfTwoMinusOne() {
        // We start on `(1L << 2) - 1` (i.e. `3L`) as the other values are already being tested.
        $noinline$assertLongEquals(1L << (2 - 1), Long.highestOneBit((1L << 2) - 1L));
        $noinline$assertLongEquals(1L << (3 - 1), Long.highestOneBit((1L << 3) - 1L));
        $noinline$assertLongEquals(1L << (4 - 1), Long.highestOneBit((1L << 4) - 1L));
        $noinline$assertLongEquals(1L << (5 - 1), Long.highestOneBit((1L << 5) - 1L));
        $noinline$assertLongEquals(1L << (6 - 1), Long.highestOneBit((1L << 6) - 1L));
        $noinline$assertLongEquals(1L << (7 - 1), Long.highestOneBit((1L << 7) - 1L));
        $noinline$assertLongEquals(1L << (8 - 1), Long.highestOneBit((1L << 8) - 1L));
        $noinline$assertLongEquals(1L << (9 - 1), Long.highestOneBit((1L << 9) - 1L));
        $noinline$assertLongEquals(1L << (10 - 1), Long.highestOneBit((1L << 10) - 1L));
        $noinline$assertLongEquals(1L << (11 - 1), Long.highestOneBit((1L << 11) - 1L));
        $noinline$assertLongEquals(1L << (12 - 1), Long.highestOneBit((1L << 12) - 1L));
        $noinline$assertLongEquals(1L << (13 - 1), Long.highestOneBit((1L << 13) - 1L));
        $noinline$assertLongEquals(1L << (14 - 1), Long.highestOneBit((1L << 14) - 1L));
        $noinline$assertLongEquals(1L << (15 - 1), Long.highestOneBit((1L << 15) - 1L));
        $noinline$assertLongEquals(1L << (16 - 1), Long.highestOneBit((1L << 16) - 1L));
        $noinline$assertLongEquals(1L << (17 - 1), Long.highestOneBit((1L << 17) - 1L));
        $noinline$assertLongEquals(1L << (18 - 1), Long.highestOneBit((1L << 18) - 1L));
        $noinline$assertLongEquals(1L << (19 - 1), Long.highestOneBit((1L << 19) - 1L));
        $noinline$assertLongEquals(1L << (20 - 1), Long.highestOneBit((1L << 20) - 1L));
        $noinline$assertLongEquals(1L << (21 - 1), Long.highestOneBit((1L << 21) - 1L));
        $noinline$assertLongEquals(1L << (22 - 1), Long.highestOneBit((1L << 22) - 1L));
        $noinline$assertLongEquals(1L << (23 - 1), Long.highestOneBit((1L << 23) - 1L));
        $noinline$assertLongEquals(1L << (24 - 1), Long.highestOneBit((1L << 24) - 1L));
        $noinline$assertLongEquals(1L << (25 - 1), Long.highestOneBit((1L << 25) - 1L));
        $noinline$assertLongEquals(1L << (26 - 1), Long.highestOneBit((1L << 26) - 1L));
        $noinline$assertLongEquals(1L << (27 - 1), Long.highestOneBit((1L << 27) - 1L));
        $noinline$assertLongEquals(1L << (28 - 1), Long.highestOneBit((1L << 28) - 1L));
        $noinline$assertLongEquals(1L << (29 - 1), Long.highestOneBit((1L << 29) - 1L));
        $noinline$assertLongEquals(1L << (30 - 1), Long.highestOneBit((1L << 30) - 1L));
    }

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongLast32_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongHighestOneBit

    /// CHECK-START: void Main.$noinline$testHighestOneBitLongLast32_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongHighestOneBit
    private static void $noinline$testHighestOneBitLongLast32_powerOfTwoMinusOne() {
        $noinline$assertLongEquals(1L << (31 - 1), Long.highestOneBit((1L << 31) - 1L));
        $noinline$assertLongEquals(1L << (32 - 1), Long.highestOneBit((1L << 32) - 1L));
        $noinline$assertLongEquals(1L << (33 - 1), Long.highestOneBit((1L << 33) - 1L));
        $noinline$assertLongEquals(1L << (34 - 1), Long.highestOneBit((1L << 34) - 1L));
        $noinline$assertLongEquals(1L << (35 - 1), Long.highestOneBit((1L << 35) - 1L));
        $noinline$assertLongEquals(1L << (36 - 1), Long.highestOneBit((1L << 36) - 1L));
        $noinline$assertLongEquals(1L << (37 - 1), Long.highestOneBit((1L << 37) - 1L));
        $noinline$assertLongEquals(1L << (38 - 1), Long.highestOneBit((1L << 38) - 1L));
        $noinline$assertLongEquals(1L << (39 - 1), Long.highestOneBit((1L << 39) - 1L));
        $noinline$assertLongEquals(1L << (40 - 1), Long.highestOneBit((1L << 40) - 1L));
        $noinline$assertLongEquals(1L << (41 - 1), Long.highestOneBit((1L << 41) - 1L));
        $noinline$assertLongEquals(1L << (42 - 1), Long.highestOneBit((1L << 42) - 1L));
        $noinline$assertLongEquals(1L << (43 - 1), Long.highestOneBit((1L << 43) - 1L));
        $noinline$assertLongEquals(1L << (44 - 1), Long.highestOneBit((1L << 44) - 1L));
        $noinline$assertLongEquals(1L << (45 - 1), Long.highestOneBit((1L << 45) - 1L));
        $noinline$assertLongEquals(1L << (46 - 1), Long.highestOneBit((1L << 46) - 1L));
        $noinline$assertLongEquals(1L << (47 - 1), Long.highestOneBit((1L << 47) - 1L));
        $noinline$assertLongEquals(1L << (48 - 1), Long.highestOneBit((1L << 48) - 1L));
        $noinline$assertLongEquals(1L << (49 - 1), Long.highestOneBit((1L << 49) - 1L));
        $noinline$assertLongEquals(1L << (50 - 1), Long.highestOneBit((1L << 50) - 1L));
        $noinline$assertLongEquals(1L << (51 - 1), Long.highestOneBit((1L << 51) - 1L));
        $noinline$assertLongEquals(1L << (52 - 1), Long.highestOneBit((1L << 52) - 1L));
        $noinline$assertLongEquals(1L << (53 - 1), Long.highestOneBit((1L << 53) - 1L));
        $noinline$assertLongEquals(1L << (54 - 1), Long.highestOneBit((1L << 54) - 1L));
        $noinline$assertLongEquals(1L << (55 - 1), Long.highestOneBit((1L << 55) - 1L));
        $noinline$assertLongEquals(1L << (56 - 1), Long.highestOneBit((1L << 56) - 1L));
        $noinline$assertLongEquals(1L << (57 - 1), Long.highestOneBit((1L << 57) - 1L));
        $noinline$assertLongEquals(1L << (58 - 1), Long.highestOneBit((1L << 58) - 1L));
        $noinline$assertLongEquals(1L << (59 - 1), Long.highestOneBit((1L << 59) - 1L));
        $noinline$assertLongEquals(1L << (60 - 1), Long.highestOneBit((1L << 60) - 1L));
        $noinline$assertLongEquals(1L << (61 - 1), Long.highestOneBit((1L << 61) - 1L));
        $noinline$assertLongEquals(1L << (62 - 1), Long.highestOneBit((1L << 62) - 1L));
        $noinline$assertLongEquals(1L << (63 - 1), Long.highestOneBit((1L << 63) - 1L));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit
    private static void $noinline$testLowestOneBitInt() {
        $noinline$assertIntEquals(1, Integer.lowestOneBit(Integer.MAX_VALUE));
        $noinline$assertIntEquals(1 << 31, Integer.lowestOneBit(Integer.MIN_VALUE));
        $noinline$testHighestOneBitInt_powerOfTwo();
        $noinline$testHighestOneBitInt_powerOfTwoMinusOne();
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit
    private static void $noinline$testLowestOneBitInt_powerOfTwo() {
        $noinline$assertIntEquals(0, Integer.lowestOneBit(0));
        $noinline$assertIntEquals(1 << 0, Integer.lowestOneBit(1 << 0));
        $noinline$assertIntEquals(1 << 1, Integer.lowestOneBit(1 << 1));
        $noinline$assertIntEquals(1 << 2, Integer.lowestOneBit(1 << 2));
        $noinline$assertIntEquals(1 << 3, Integer.lowestOneBit(1 << 3));
        $noinline$assertIntEquals(1 << 4, Integer.lowestOneBit(1 << 4));
        $noinline$assertIntEquals(1 << 5, Integer.lowestOneBit(1 << 5));
        $noinline$assertIntEquals(1 << 6, Integer.lowestOneBit(1 << 6));
        $noinline$assertIntEquals(1 << 7, Integer.lowestOneBit(1 << 7));
        $noinline$assertIntEquals(1 << 8, Integer.lowestOneBit(1 << 8));
        $noinline$assertIntEquals(1 << 9, Integer.lowestOneBit(1 << 9));
        $noinline$assertIntEquals(1 << 10, Integer.lowestOneBit(1 << 10));
        $noinline$assertIntEquals(1 << 11, Integer.lowestOneBit(1 << 11));
        $noinline$assertIntEquals(1 << 12, Integer.lowestOneBit(1 << 12));
        $noinline$assertIntEquals(1 << 13, Integer.lowestOneBit(1 << 13));
        $noinline$assertIntEquals(1 << 14, Integer.lowestOneBit(1 << 14));
        $noinline$assertIntEquals(1 << 15, Integer.lowestOneBit(1 << 15));
        $noinline$assertIntEquals(1 << 16, Integer.lowestOneBit(1 << 16));
        $noinline$assertIntEquals(1 << 17, Integer.lowestOneBit(1 << 17));
        $noinline$assertIntEquals(1 << 18, Integer.lowestOneBit(1 << 18));
        $noinline$assertIntEquals(1 << 19, Integer.lowestOneBit(1 << 19));
        $noinline$assertIntEquals(1 << 20, Integer.lowestOneBit(1 << 20));
        $noinline$assertIntEquals(1 << 21, Integer.lowestOneBit(1 << 21));
        $noinline$assertIntEquals(1 << 22, Integer.lowestOneBit(1 << 22));
        $noinline$assertIntEquals(1 << 23, Integer.lowestOneBit(1 << 23));
        $noinline$assertIntEquals(1 << 24, Integer.lowestOneBit(1 << 24));
        $noinline$assertIntEquals(1 << 25, Integer.lowestOneBit(1 << 25));
        $noinline$assertIntEquals(1 << 26, Integer.lowestOneBit(1 << 26));
        $noinline$assertIntEquals(1 << 27, Integer.lowestOneBit(1 << 27));
        $noinline$assertIntEquals(1 << 28, Integer.lowestOneBit(1 << 28));
        $noinline$assertIntEquals(1 << 29, Integer.lowestOneBit(1 << 29));
        $noinline$assertIntEquals(1 << 30, Integer.lowestOneBit(1 << 30));
        $noinline$assertIntEquals(1 << 31, Integer.lowestOneBit(1 << 31));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitInt_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerLowestOneBit
    private static void $noinline$testLowestOneBitInt_powerOfTwoMinusOne() {
        // We start on `(1 << 2) - 1` (i.e. `3`) as the other values are already being tested.
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 2) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 3) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 4) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 5) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 6) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 7) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 8) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 9) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 10) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 11) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 12) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 13) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 14) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 15) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 16) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 17) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 18) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 19) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 20) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 21) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 22) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 23) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 24) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 25) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 26) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 27) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 28) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 29) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 30) - 1));
        $noinline$assertIntEquals(1, Integer.lowestOneBit((1 << 31) - 1));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitLong() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitLong() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongLowestOneBit
    private static void $noinline$testLowestOneBitLong() {
        $noinline$assertLongEquals(1L, Long.lowestOneBit(Long.MAX_VALUE));
        $noinline$assertLongEquals(1L << 63, Long.lowestOneBit(Long.MIN_VALUE));
        // We need to do two smaller methods because otherwise we don't compile it due to our
        // heuristics.
        $noinline$testLowestOneBitLongFirst32_powerOfTwo();
        $noinline$testLowestOneBitLongLast32_powerOfTwo();
        $noinline$testLowestOneBitLongFirst32_powerOfTwoMinusOne();
        $noinline$testLowestOneBitLongLast32_powerOfTwoMinusOne();
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongFirst32_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongFirst32_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongLowestOneBit
    private static void $noinline$testLowestOneBitLongFirst32_powerOfTwo() {
        $noinline$assertLongEquals(0L, Long.lowestOneBit(0L));
        $noinline$assertLongEquals(1L << 0L, Long.lowestOneBit(1L << 0L));
        $noinline$assertLongEquals(1L << 1L, Long.lowestOneBit(1L << 1L));
        $noinline$assertLongEquals(1L << 2L, Long.lowestOneBit(1L << 2L));
        $noinline$assertLongEquals(1L << 3L, Long.lowestOneBit(1L << 3L));
        $noinline$assertLongEquals(1L << 4L, Long.lowestOneBit(1L << 4L));
        $noinline$assertLongEquals(1L << 5L, Long.lowestOneBit(1L << 5L));
        $noinline$assertLongEquals(1L << 6L, Long.lowestOneBit(1L << 6L));
        $noinline$assertLongEquals(1L << 7L, Long.lowestOneBit(1L << 7L));
        $noinline$assertLongEquals(1L << 8L, Long.lowestOneBit(1L << 8L));
        $noinline$assertLongEquals(1L << 9L, Long.lowestOneBit(1L << 9L));
        $noinline$assertLongEquals(1L << 10L, Long.lowestOneBit(1L << 10L));
        $noinline$assertLongEquals(1L << 11L, Long.lowestOneBit(1L << 11L));
        $noinline$assertLongEquals(1L << 12L, Long.lowestOneBit(1L << 12L));
        $noinline$assertLongEquals(1L << 13L, Long.lowestOneBit(1L << 13L));
        $noinline$assertLongEquals(1L << 14L, Long.lowestOneBit(1L << 14L));
        $noinline$assertLongEquals(1L << 15L, Long.lowestOneBit(1L << 15L));
        $noinline$assertLongEquals(1L << 16L, Long.lowestOneBit(1L << 16L));
        $noinline$assertLongEquals(1L << 17L, Long.lowestOneBit(1L << 17L));
        $noinline$assertLongEquals(1L << 18L, Long.lowestOneBit(1L << 18L));
        $noinline$assertLongEquals(1L << 19L, Long.lowestOneBit(1L << 19L));
        $noinline$assertLongEquals(1L << 20L, Long.lowestOneBit(1L << 20L));
        $noinline$assertLongEquals(1L << 21L, Long.lowestOneBit(1L << 21L));
        $noinline$assertLongEquals(1L << 22L, Long.lowestOneBit(1L << 22L));
        $noinline$assertLongEquals(1L << 23L, Long.lowestOneBit(1L << 23L));
        $noinline$assertLongEquals(1L << 24L, Long.lowestOneBit(1L << 24L));
        $noinline$assertLongEquals(1L << 25L, Long.lowestOneBit(1L << 25L));
        $noinline$assertLongEquals(1L << 26L, Long.lowestOneBit(1L << 26L));
        $noinline$assertLongEquals(1L << 27L, Long.lowestOneBit(1L << 27L));
        $noinline$assertLongEquals(1L << 28L, Long.lowestOneBit(1L << 28L));
        $noinline$assertLongEquals(1L << 29L, Long.lowestOneBit(1L << 29L));
        $noinline$assertLongEquals(1L << 30L, Long.lowestOneBit(1L << 30L));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongLast32_powerOfTwo() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongLast32_powerOfTwo() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongLowestOneBit
    private static void $noinline$testLowestOneBitLongLast32_powerOfTwo() {
        $noinline$assertLongEquals(1L << 31L, Long.lowestOneBit(1L << 31L));
        $noinline$assertLongEquals(1L << 32L, Long.lowestOneBit(1L << 32L));
        $noinline$assertLongEquals(1L << 33L, Long.lowestOneBit(1L << 33L));
        $noinline$assertLongEquals(1L << 34L, Long.lowestOneBit(1L << 34L));
        $noinline$assertLongEquals(1L << 35L, Long.lowestOneBit(1L << 35L));
        $noinline$assertLongEquals(1L << 36L, Long.lowestOneBit(1L << 36L));
        $noinline$assertLongEquals(1L << 37L, Long.lowestOneBit(1L << 37L));
        $noinline$assertLongEquals(1L << 38L, Long.lowestOneBit(1L << 38L));
        $noinline$assertLongEquals(1L << 39L, Long.lowestOneBit(1L << 39L));
        $noinline$assertLongEquals(1L << 40L, Long.lowestOneBit(1L << 40L));
        $noinline$assertLongEquals(1L << 41L, Long.lowestOneBit(1L << 41L));
        $noinline$assertLongEquals(1L << 42L, Long.lowestOneBit(1L << 42L));
        $noinline$assertLongEquals(1L << 43L, Long.lowestOneBit(1L << 43L));
        $noinline$assertLongEquals(1L << 44L, Long.lowestOneBit(1L << 44L));
        $noinline$assertLongEquals(1L << 45L, Long.lowestOneBit(1L << 45L));
        $noinline$assertLongEquals(1L << 46L, Long.lowestOneBit(1L << 46L));
        $noinline$assertLongEquals(1L << 47L, Long.lowestOneBit(1L << 47L));
        $noinline$assertLongEquals(1L << 48L, Long.lowestOneBit(1L << 48L));
        $noinline$assertLongEquals(1L << 49L, Long.lowestOneBit(1L << 49L));
        $noinline$assertLongEquals(1L << 50L, Long.lowestOneBit(1L << 50L));
        $noinline$assertLongEquals(1L << 51L, Long.lowestOneBit(1L << 51L));
        $noinline$assertLongEquals(1L << 52L, Long.lowestOneBit(1L << 52L));
        $noinline$assertLongEquals(1L << 53L, Long.lowestOneBit(1L << 53L));
        $noinline$assertLongEquals(1L << 54L, Long.lowestOneBit(1L << 54L));
        $noinline$assertLongEquals(1L << 55L, Long.lowestOneBit(1L << 55L));
        $noinline$assertLongEquals(1L << 56L, Long.lowestOneBit(1L << 56L));
        $noinline$assertLongEquals(1L << 57L, Long.lowestOneBit(1L << 57L));
        $noinline$assertLongEquals(1L << 58L, Long.lowestOneBit(1L << 58L));
        $noinline$assertLongEquals(1L << 59L, Long.lowestOneBit(1L << 59L));
        $noinline$assertLongEquals(1L << 60L, Long.lowestOneBit(1L << 60L));
        $noinline$assertLongEquals(1L << 61L, Long.lowestOneBit(1L << 61L));
        $noinline$assertLongEquals(1L << 62L, Long.lowestOneBit(1L << 62L));
        $noinline$assertLongEquals(1L << 63L, Long.lowestOneBit(1L << 63L));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongFirst32_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongFirst32_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongLowestOneBit
    private static void $noinline$testLowestOneBitLongFirst32_powerOfTwoMinusOne() {
        // We start on `(1L << 2) - 1` (i.e. `3L`) as the other values are already being tested.
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 2) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 3) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 4) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 5) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 6) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 7) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 8) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 9) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 10) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 11) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 12) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 13) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 14) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 15) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 16) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 17) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 18) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 19) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 20) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 21) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 22) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 23) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 24) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 25) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 26) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 27) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 28) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 29) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 30) - 1L));
    }

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongLast32_powerOfTwoMinusOne() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongLowestOneBit

    /// CHECK-START: void Main.$noinline$testLowestOneBitLongLast32_powerOfTwoMinusOne() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongLowestOneBit
    private static void $noinline$testLowestOneBitLongLast32_powerOfTwoMinusOne() {
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 31) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 32) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 33) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 34) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 35) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 36) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 37) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 38) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 39) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 40) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 41) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 42) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 43) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 44) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 45) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 46) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 47) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 48) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 49) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 50) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 51) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 52) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 53) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 54) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 55) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 56) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 57) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 58) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 59) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 60) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 61) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 62) - 1L));
        $noinline$assertLongEquals(1L, Long.lowestOneBit((1L << 63) - 1L));
    }

    /// CHECK-START: void Main.$noinline$testLeadingZerosInt() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:IntegerNumberOfLeadingZeros

    /// CHECK-START: void Main.$noinline$testLeadingZerosInt() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:IntegerNumberOfLeadingZeros
    private static void $noinline$testLeadingZerosInt() {
        $noinline$assertIntEquals(1, Integer.numberOfLeadingZeros(Integer.MAX_VALUE));
        $noinline$assertIntEquals(0, Integer.numberOfLeadingZeros(Integer.MIN_VALUE));
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

    /// CHECK-START: void Main.$noinline$testLeadingZerosLong() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros

    /// CHECK-START: void Main.$noinline$testLeadingZerosLong() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfLeadingZeros
    private static void $noinline$testLeadingZerosLong() {
        $noinline$assertIntEquals(1, Long.numberOfLeadingZeros(Long.MAX_VALUE));
        $noinline$assertIntEquals(0, Long.numberOfLeadingZeros(Long.MIN_VALUE));
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
        $noinline$assertIntEquals(0, Integer.numberOfTrailingZeros(Integer.MAX_VALUE));
        $noinline$assertIntEquals(31, Integer.numberOfTrailingZeros(Integer.MIN_VALUE));
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

    /// CHECK-START: void Main.$noinline$testTrailingZerosLong() constant_folding (before)
    /// CHECK-DAG: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros

    /// CHECK-START: void Main.$noinline$testTrailingZerosLong() constant_folding (after)
    /// CHECK-NOT: InvokeStaticOrDirect intrinsic:LongNumberOfTrailingZeros
    private static void $noinline$testTrailingZerosLong() {
        $noinline$assertIntEquals(0, Long.numberOfTrailingZeros(Long.MAX_VALUE));
        $noinline$assertIntEquals(63, Long.numberOfTrailingZeros(Long.MIN_VALUE));
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

    public static void $noinline$assertLongEquals(long expected, long result) {
        if (expected != result) {
            throw new Error("Expected: " + expected + ", found: " + result);
        }
    }
}
