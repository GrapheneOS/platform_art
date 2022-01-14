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

public class Caller {
  /// CHECK-START: void Caller.doCall() builder (after)
  /// CHECK:       LoadClass load_kind:BssEntry class_name:pkg1.SuperClass
  /// CHECK:       InvokeStaticOrDirect method_name:pkg1.SuperClass.$noinline$callMethod
  public static void doCall() {
    pkg1.SubClass.$noinline$callMethod();
  }
}