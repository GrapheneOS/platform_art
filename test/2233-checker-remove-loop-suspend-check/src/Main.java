/*
 * Copyright (C) 2021 The Android Open Source Project
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

  static final int ITERATIONS = 16;

  // Test 1: This test checks whether the SuspendCheck is removed from the
  // header.

  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheck(int[]) loop_optimization (before)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheck(int[]) loop_optimization (after)
  /// CHECK-NOT:   SuspendCheck         loop:{{B\d+}}
  public static void $noinline$testRemoveSuspendCheck(int[] a) {
    for (int i = 0; i < ITERATIONS; i++) {
      a[i++] = i;
    }
  }

  // Test 2: This test checks that the SuspendCheck is not removed from the
  // header because it contains a call to another function.

  /// CHECK-START: void Main.testRemoveSuspendCheckWithCall(int[]) loop_optimization (before)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}
  /// CHECK-START: void Main.testRemoveSuspendCheckWithCall(int[]) loop_optimization (after)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}

  public static void testRemoveSuspendCheckWithCall(int[] a) {
    for (int i = 0; i < ITERATIONS; i++) {
      a[i++] = i;
      $noinline$testRemoveSuspendCheck(a);
    }
  }

  // Test 3:  This test checks that the SuspendCheck is not removed from the
  // header because INSTR_COUNT * TRIP_COUNT exceeds the defined heuristic.

  /// CHECK-START: void Main.testRemoveSuspendCheckAboveHeuristic(int[]) loop_optimization (before)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}
  /// CHECK-START: void Main.testRemoveSuspendCheckAboveHeuristic(int[]) loop_optimization (after)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}

  public static void testRemoveSuspendCheckAboveHeuristic(int[] a) {
    for (int i = 0; i < ITERATIONS * 6; i++) {
      a[i++] = i;
    }
  }

  // Test 4:  This test checks that the SuspendCheck is not removed from the
  // header because the trip count is not known at compile time.

  /// CHECK-START: void Main.testRemoveSuspendCheckUnknownCount(int[], int) loop_optimization (before)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}
  /// CHECK-START: void Main.testRemoveSuspendCheckUnknownCount(int[], int) loop_optimization (after)
  /// CHECK:       SuspendCheck         loop:{{B\d+}}

  public static void testRemoveSuspendCheckUnknownCount(int[] a, int n) {
    for (int i = 0; i < n; i++) {
      a[i++] = i;
    }
  }

  public static void main(String[] args) {
    int[] a = new int[100];
    $noinline$testRemoveSuspendCheck(a);
    testRemoveSuspendCheckWithCall(a);
    testRemoveSuspendCheckAboveHeuristic(a);
    testRemoveSuspendCheckUnknownCount(a, 4);
  }
}
