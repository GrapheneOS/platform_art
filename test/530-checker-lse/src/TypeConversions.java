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

public class TypeConversions {
  static byte static_byte;
  static char static_char;
  static int static_int;
  static int unrelated_static_int;

  public static void assertIntEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionInt8(int) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_byte
  /// CHECK-DAG:                StaticFieldSet field_name:TypeConversions.unrelated_static_int loop:B{{\d+}}
  /// CHECK-DAG: <<GetB:b\d+>>  StaticFieldGet field_name:TypeConversions.static_byte
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetB>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI:i\d+>>  StaticFieldGet field_name:TypeConversions.static_int field_type:Int32
  /// CHECK-DAG:                Return [<<GetI>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionInt8(int) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<Conv:b\d+>>  TypeConversion [<<Value>>]
  /// CHECK-DAG:                Return [<<Conv>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionInt8(int) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$loopPhiStoreLoadConversionInt8(int value) {
    static_byte = (byte) value;
    // Irrelevant code but needed to make LSE use loop Phi placeholders.
    for (int q = 1; q < 12; q++) {
      unrelated_static_int = 24;
    }
    static_int = static_byte;
    return static_int;
  }

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionUint8(int) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_byte
  /// CHECK-DAG:                StaticFieldSet field_name:TypeConversions.unrelated_static_int loop:B{{\d+}}
  // The `& 0xff` has already been merged with the load.
  /// CHECK-DAG: <<GetA:a\d+>>  StaticFieldGet field_name:TypeConversions.static_byte
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetA>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI:i\d+>>  StaticFieldGet field_name:TypeConversions.static_int field_type:Int32
  /// CHECK-DAG:                Return [<<GetI>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionUint8(int) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<Conv:a\d+>>  TypeConversion [<<Value>>]
  /// CHECK-DAG:                Return [<<Conv>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiStoreLoadConversionUint8(int) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$loopPhiStoreLoadConversionUint8(int value) {
    static_byte = (byte) value;
    // Irrelevant code but needed to make LSE use loop Phi placeholders.
    for (int q = 1; q < 12; q++) {
        unrelated_static_int = 24;
    }
    static_int = static_byte & 0xff;
    return static_int;
  }

  /// CHECK-START: int TypeConversions.$noinline$loopPhiTwoStoreLoadConversions(int) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_byte
  /// CHECK-DAG:                StaticFieldSet field_name:TypeConversions.unrelated_static_int loop:B{{\d+}}
  /// CHECK-DAG: <<GetB:b\d+>>  StaticFieldGet field_name:TypeConversions.static_byte
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetB>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI1:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetI1>>] field_name:TypeConversions.static_char
  /// CHECK-DAG: <<GetC:c\d+>>  StaticFieldGet field_name:TypeConversions.static_char field_type:Uint16
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetC>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI2:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32
  /// CHECK-DAG:                Return [<<GetI2>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiTwoStoreLoadConversions(int) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<ConvB:b\d+>> TypeConversion [<<Value>>]
  /// CHECK-DAG: <<ConvC:c\d+>> TypeConversion [<<ConvB>>]
  /// CHECK-DAG:                Return [<<ConvC>>]

  /// CHECK-START: int TypeConversions.$noinline$loopPhiTwoStoreLoadConversions(int) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$loopPhiTwoStoreLoadConversions(int value) {
      static_byte = (byte) value;
      // Irrelevant code but needed to make LSE use loop Phi placeholders.
      for (int q = 1; q < 12; q++) {
          unrelated_static_int = 24;
      }
      // Note: We need to go through `static_int` so that the instruction
      // simplifier eliminates the type conversion to `char`.
      // TODO: Improve the instruction simplifier to eliminate the conversion
      // for `static_char = (char) static_byte`.
      static_int = static_byte;
      static_char = (char) static_int;
      static_int = static_char;
      return static_int;
  }

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionInt8InLoop(int, boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI:i\d+>>  StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:<<Loop:B\d+>>
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG: <<GetB:b\d+>>  StaticFieldGet field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetB>>] field_name:TypeConversions.static_int loop:<<Loop>>
  /// CHECK-DAG: <<GetI2:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:none
  /// CHECK-DAG:                Return [<<GetI2>>]

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionInt8InLoop(int, boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<Phi1:i\d+>>  Phi [<<Value>>,<<Phi2:i\d+>>] loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Conv:b\d+>>  TypeConversion [<<Phi1>>] loop:<<Loop>>
  /// CHECK-DAG: <<Phi2>>       Phi [<<Phi1>>,<<Conv>>] loop:<<Loop>>
  /// CHECK-DAG:                Return [<<Phi1>>]

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionInt8InLoop(int, boolean) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$conditionalStoreLoadConversionInt8InLoop(int value, boolean cond) {
      static_int = value;
      for (int q = 1; q < 12; q++) {
          if (cond) {
              static_byte = (byte) static_int;
              static_int = static_byte;
          }
      }
      return static_int;
  }

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionUint8InLoop(int, boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI1:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:<<Loop:B\d+>>
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetI1>>] field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG: <<GetA:a\d+>>  StaticFieldGet field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetA>>] field_name:TypeConversions.static_int loop:<<Loop>>
  /// CHECK-DAG: <<GetI2:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:none
  /// CHECK-DAG:                Return [<<GetI2>>]

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionUint8InLoop(int, boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<Phi1:i\d+>>  Phi [<<Value>>,<<Phi2:i\d+>>] loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Conv:a\d+>>  TypeConversion [<<Phi1>>] loop:<<Loop>>
  /// CHECK-DAG: <<Phi2>>       Phi [<<Phi1>>,<<Conv>>] loop:<<Loop>>
  /// CHECK-DAG:                Return [<<Phi1>>]

  /// CHECK-START: int TypeConversions.$noinline$conditionalStoreLoadConversionUint8InLoop(int, boolean) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$conditionalStoreLoadConversionUint8InLoop(int value, boolean cond) {
      static_int = value;
      for (int q = 1; q < 12; q++) {
          if (cond) {
              static_byte = (byte) static_int;
              static_int = static_byte & 0xff;
          }
      }
      return static_int;
  }

  /// CHECK-START: int TypeConversions.$noinline$twoConditionalStoreLoadConversionsInLoop(int, boolean) load_store_elimination (before)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<Value>>] field_name:TypeConversions.static_int
  /// CHECK-DAG: <<GetI1:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:<<Loop:B\d+>>
  // The type conversion has already been eliminated.
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetI1>>] field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG: <<GetB:b\d+>>  StaticFieldGet field_name:TypeConversions.static_byte loop:<<Loop>>
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetB>>] field_name:TypeConversions.static_int loop:<<Loop>>
  /// CHECK-DAG: <<GetI2:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:<<Loop>>
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetI2>>] field_name:TypeConversions.static_char loop:<<Loop>>
  /// CHECK-DAG: <<GetC:c\d+>>  StaticFieldGet field_name:TypeConversions.static_char loop:<<Loop>>
  /// CHECK-DAG:                StaticFieldSet [{{l\d+}},<<GetC>>] field_name:TypeConversions.static_int loop:<<Loop>>
  /// CHECK-DAG: <<GetI3:i\d+>> StaticFieldGet field_name:TypeConversions.static_int field_type:Int32 loop:none
  /// CHECK-DAG:                Return [<<GetI3>>]

  /// CHECK-START: int TypeConversions.$noinline$twoConditionalStoreLoadConversionsInLoop(int, boolean) load_store_elimination (after)
  /// CHECK-DAG: <<Value:i\d+>> ParameterValue
  /// CHECK-DAG: <<Phi1:i\d+>>  Phi [<<Value>>,<<Phi2:i\d+>>] loop:<<Loop:B\d+>>
  /// CHECK-DAG: <<Conv1:b\d+>> TypeConversion [<<Phi1>>] loop:<<Loop>>
  /// CHECK-DAG: <<Conv2:c\d+>> TypeConversion [<<Conv1>>] loop:<<Loop>>
  /// CHECK-DAG: <<Phi2>>       Phi [<<Phi1>>,<<Conv2>>] loop:<<Loop>>
  /// CHECK-DAG:                Return [<<Phi1>>]

  /// CHECK-START: int TypeConversions.$noinline$twoConditionalStoreLoadConversionsInLoop(int, boolean) load_store_elimination (after)
  /// CHECK-NOT: StaticFieldGet
  public static int $noinline$twoConditionalStoreLoadConversionsInLoop(int value, boolean cond) {
      static_int = value;
      for (int q = 1; q < 12; q++) {
          if (cond) {
              static_byte = (byte) static_int;
              // Note: We need to go through `static_int` so that the instruction
              // simplifier eliminates the type conversion to `char`.
              // TODO: Improve the instruction simplifier to eliminate the conversion
              // for `static_char = (char) static_byte`.
              static_int = static_byte;
              static_char = (char) static_int;
              static_int = static_char;
          }
      }
      return static_int;
  }

  public static void main() {
    assertIntEquals(42, $noinline$loopPhiStoreLoadConversionInt8(42));
    assertIntEquals(-42, $noinline$loopPhiStoreLoadConversionInt8(-42));
    assertIntEquals(-128, $noinline$loopPhiStoreLoadConversionInt8(128));
    assertIntEquals(127, $noinline$loopPhiStoreLoadConversionInt8(-129));

    assertIntEquals(42, $noinline$loopPhiStoreLoadConversionUint8(42));
    assertIntEquals(214, $noinline$loopPhiStoreLoadConversionUint8(-42));
    assertIntEquals(128, $noinline$loopPhiStoreLoadConversionUint8(128));
    assertIntEquals(127, $noinline$loopPhiStoreLoadConversionUint8(-129));

    assertIntEquals(42, $noinline$loopPhiTwoStoreLoadConversions(42));
    assertIntEquals(65494, $noinline$loopPhiTwoStoreLoadConversions(-42));
    assertIntEquals(65408, $noinline$loopPhiTwoStoreLoadConversions(128));
    assertIntEquals(127, $noinline$loopPhiTwoStoreLoadConversions(-129));
    assertIntEquals(0, $noinline$loopPhiTwoStoreLoadConversions(256));
    assertIntEquals(65535, $noinline$loopPhiTwoStoreLoadConversions(-257));

    assertIntEquals(42, $noinline$conditionalStoreLoadConversionInt8InLoop(42, false));
    assertIntEquals(42, $noinline$conditionalStoreLoadConversionInt8InLoop(42, true));
    assertIntEquals(-42, $noinline$conditionalStoreLoadConversionInt8InLoop(-42, false));
    assertIntEquals(-42, $noinline$conditionalStoreLoadConversionInt8InLoop(-42, true));
    assertIntEquals(128, $noinline$conditionalStoreLoadConversionInt8InLoop(128, false));
    assertIntEquals(-128, $noinline$conditionalStoreLoadConversionInt8InLoop(128, true));
    assertIntEquals(-129, $noinline$conditionalStoreLoadConversionInt8InLoop(-129, false));
    assertIntEquals(127, $noinline$conditionalStoreLoadConversionInt8InLoop(-129, true));

    assertIntEquals(42, $noinline$conditionalStoreLoadConversionUint8InLoop(42, false));
    assertIntEquals(42, $noinline$conditionalStoreLoadConversionUint8InLoop(42, true));
    assertIntEquals(-42, $noinline$conditionalStoreLoadConversionUint8InLoop(-42, false));
    assertIntEquals(214, $noinline$conditionalStoreLoadConversionUint8InLoop(-42, true));
    assertIntEquals(128, $noinline$conditionalStoreLoadConversionUint8InLoop(128, false));
    assertIntEquals(128, $noinline$conditionalStoreLoadConversionUint8InLoop(128, true));
    assertIntEquals(-129, $noinline$conditionalStoreLoadConversionUint8InLoop(-129, false));
    assertIntEquals(127, $noinline$conditionalStoreLoadConversionUint8InLoop(-129, true));

    assertIntEquals(42, $noinline$twoConditionalStoreLoadConversionsInLoop(42, false));
    assertIntEquals(42, $noinline$twoConditionalStoreLoadConversionsInLoop(42, true));
    assertIntEquals(-42, $noinline$twoConditionalStoreLoadConversionsInLoop(-42, false));
    assertIntEquals(65494, $noinline$twoConditionalStoreLoadConversionsInLoop(-42, true));
    assertIntEquals(128, $noinline$twoConditionalStoreLoadConversionsInLoop(128, false));
    assertIntEquals(65408, $noinline$twoConditionalStoreLoadConversionsInLoop(128, true));
    assertIntEquals(-129, $noinline$twoConditionalStoreLoadConversionsInLoop(-129, false));
    assertIntEquals(127, $noinline$twoConditionalStoreLoadConversionsInLoop(-129, true));
    assertIntEquals(256, $noinline$twoConditionalStoreLoadConversionsInLoop(256, false));
    assertIntEquals(0, $noinline$twoConditionalStoreLoadConversionsInLoop(256, true));
    assertIntEquals(-257, $noinline$twoConditionalStoreLoadConversionsInLoop(-257, false));
    assertIntEquals(65535, $noinline$twoConditionalStoreLoadConversionsInLoop(-257, true));
  }
}
