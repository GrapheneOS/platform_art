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

import java.util.zip.CRC32;

public class Main {

  public static void main(String[] args) {
      byte byteArray[] = { 1, 2 };
      CRC32 crc = new CRC32();
      // This is an intrinsic that only reads, and our load-store elimination pass forgot to take
      // that into account.
      crc.update(byteArray);
      assertEquals(3066839698L, crc.getValue());
  }

  public static void assertEquals(long expected, long value) {
    if (expected != value) {
      throw new Error("Expected: " + expected + ", got: " + value);
    }
  }
}
