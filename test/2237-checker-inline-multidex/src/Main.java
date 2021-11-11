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
  public static void main(String[] args) {
    System.out.println(testNeedsEnvironment());
    System.out.println(testNeedsBssEntryString());
    System.out.println(testNeedsBssEntryInvoke());
    System.out.println(testClass());
  }

  /// CHECK-START: java.lang.String Main.testNeedsEnvironment() inliner (before)
  /// CHECK:       InvokeStaticOrDirect method_name:Multi.$inline$NeedsEnvironmentMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsEnvironment() inliner (after)
  /// CHECK-NOT:   InvokeStaticOrDirect method_name:Multi.$inline$NeedsEnvironmentMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsEnvironment() inliner (after)
  /// CHECK:       StringBuilderAppend
  public static String testNeedsEnvironment() {
    return Multi.$inline$NeedsEnvironmentMultiDex("abc");
  }

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryString() inliner (before)
  /// CHECK:       InvokeStaticOrDirect method_name:Multi.$inline$NeedsBssEntryStringMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryString() inliner (after)
  /// CHECK-NOT:   InvokeStaticOrDirect method_name:Multi.$inline$NeedsBssEntryStringMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryString() inliner (after)
  /// CHECK:       LoadString load_kind:BssEntry
  public static String testNeedsBssEntryString() {
    return Multi.$inline$NeedsBssEntryStringMultiDex();
  }

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryInvoke() inliner (before)
  /// CHECK:       InvokeStaticOrDirect method_name:Multi.$inline$NeedsBssEntryInvokeMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryInvoke() inliner (after)
  /// CHECK-NOT:   InvokeStaticOrDirect method_name:Multi.$inline$NeedsBssEntryInvokeMultiDex

  /// CHECK-START: java.lang.String Main.testNeedsBssEntryInvoke() inliner (after)
  /// CHECK:       InvokeStaticOrDirect method_name:Multi.$noinline$InnerInvokeMultiDex method_load_kind:BssEntry
  public static String testNeedsBssEntryInvoke() {
    return Multi.$inline$NeedsBssEntryInvokeMultiDex();
  }

  /// CHECK-START: java.lang.Class Main.testClass() inliner (before)
  /// CHECK:       InvokeStaticOrDirect method_name:Multi.NeedsBssEntryClassMultiDex

  /// CHECK-START: java.lang.Class Main.testClass() inliner (after)
  /// CHECK-NOT:   InvokeStaticOrDirect method_name:Multi.NeedsBssEntryClassMultiDex

  /// CHECK-START: java.lang.Class Main.testClass() inliner (after)
  /// CHECK:       LoadClass load_kind:BssEntry class_name:Multi$Multi2
  public static Class<?> testClass() {
    return Multi.NeedsBssEntryClassMultiDex();
  }
}
