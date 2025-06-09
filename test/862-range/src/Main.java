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

public class Main {

  public static void main(String[] args) {
    invoke(1, 2, 3, 4, 5, 6, 7, 8);
    assertEquals(8, field);
  }

  public static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  public static void invoke(int a, int b, int c, int d, int e, int f, int g, int h) {
    forward(a, b, c, d, e, f, g, h);
  }

  public static void forward(int a, int b, int c, int d, int e, int f, int g, int h) {
    field = h;
  }

  static int field;
}
