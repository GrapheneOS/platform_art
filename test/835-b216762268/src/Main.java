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

public class Main {
    static long count;

    static int $noinline$mainTest() {
        int i, i1 = 10, i15 = 49099, i17, i20, i21 = -2;
        for (i = 5; i < 138; i++)
            for (i15 = 11; i15 < 188; ++i15) {
                for (i17 = 2; i17 > 1; i17--) {
                    i1 += i17;
                    count = i1;
                }
                for (i20 = 1; i20 < 2; ++i20) {
                    i21 += i17;
                }
            }
        return i21;
    }

    public static void assertEquals(int expected, int actual) {
      if (expected != actual) {
        throw new Error("Expected " + expected + ", got " +  actual);
      }
    }

    public static void main(String[] strArr) {
      assertEquals(23539, $noinline$mainTest());
    }
}
