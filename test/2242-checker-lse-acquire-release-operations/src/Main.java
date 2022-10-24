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

class TestClass {
  TestClass() {}
  int i;
  int j;
  volatile int vi;
}

public class Main {
  public static void main(String[] args) {
    // Volatile accesses.
    assertEquals($noinline$testVolatileAccessesMustBeKept(new TestClass()), 3);

    // Volatile loads - Different fields shouldn't alias.
    assertEquals($noinline$testVolatileLoadDifferentFields(new TestClass(), new TestClass()), 3);
    assertEquals(
            $noinline$testVolatileLoadDifferentFieldsBlocking(new TestClass(), new TestClass()),
            3);

    // Volatile loads - Redundant store.
    assertEquals($noinline$testVolatileLoadRedundantStore(new TestClass()), 2);
    assertEquals($noinline$testVolatileLoadRedundantStoreBlocking(new TestClass()), 2);
    assertEquals($noinline$testVolatileLoadRedundantStoreBlockingOnlyLoad(new TestClass()), 2);

    // Volatile loads - Set and merge values.
    assertEquals($noinline$testVolatileLoadSetAndMergeValues(new TestClass(), true), 1);
    assertEquals($noinline$testVolatileLoadSetAndMergeValues(new TestClass(), false), 2);
    assertEquals($noinline$testVolatileLoadSetAndMergeValuesBlocking(new TestClass(), true), 1);
    assertEquals($noinline$testVolatileLoadSetAndMergeValuesBlocking(new TestClass(), false), 2);

    // Volatile stores - Different fields shouldn't alias.
    assertEquals($noinline$testVolatileStoreDifferentFields(new TestClass(), new TestClass()), 3);
    assertEquals(
            $noinline$testVolatileStoreDifferentFieldsBlocking(new TestClass(), new TestClass()),
            3);

    // Volatile stores - Redundant store.
    assertEquals($noinline$testVolatileStoreRedundantStore(new TestClass()), 2);
    assertEquals($noinline$testVolatileStoreRedundantStoreBlocking(new TestClass()), 2);
    assertEquals($noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(new TestClass()), 2);

    // Volatile stores - Set and merge values.
    assertEquals($noinline$testVolatileStoreSetAndMergeValues(new TestClass(), true), 1);
    assertEquals($noinline$testVolatileStoreSetAndMergeValues(new TestClass(), false), 2);
    assertEquals($noinline$testVolatileStoreSetAndMergeValuesNotBlocking(new TestClass(), true), 1);
    assertEquals($noinline$testVolatileStoreSetAndMergeValuesNotBlocking(new TestClass(), false), 2);

    // Monitor Operations - Different fields shouldn't alias.
    assertEquals(
            $noinline$testMonitorOperationDifferentFields(new TestClass(), new TestClass()), 3);
    assertEquals($noinline$testMonitorOperationDifferentFieldsBlocking(
                          new TestClass(), new TestClass()),
            3);

    // Monitor Operations - Redundant store.
    assertEquals($noinline$testMonitorOperationRedundantStore(new TestClass()), 2);
    assertEquals($noinline$testMonitorOperationRedundantStoreBlocking(new TestClass()), 2);
    assertEquals(
            $noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(new TestClass()), 2);
    assertEquals($noinline$testMonitorOperationRedundantStoreBlockingExit(new TestClass()), 2);

    // Monitor Operations - Set and merge values.
    assertEquals($noinline$testMonitorOperationSetAndMergeValues(new TestClass(), true), 1);
    assertEquals($noinline$testMonitorOperationSetAndMergeValues(new TestClass(), false), 2);
    assertEquals(
            $noinline$testMonitorOperationSetAndMergeValuesBlocking(new TestClass(), true), 1);
    assertEquals(
            $noinline$testMonitorOperationSetAndMergeValuesBlocking(new TestClass(), false), 2);
  }

  public static void assertEquals(int expected, int result) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result);
    }
  }

  /// CHECK-START: int Main.$noinline$testVolatileAccessesMustBeKept(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileAccessesMustBeKept(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  static int $noinline$testVolatileAccessesMustBeKept(TestClass obj1) {
    int result;
    obj1.vi = 3;
    // Redundant load that has to be kept.
    result = obj1.vi;
    result = obj1.vi;
    // Redundant store that has to be kept.
    obj1.vi = 3;
    result = obj1.vi;
    return result;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFields(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldGet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet field_name:TestClass.i
  /// CHECK: InstanceFieldGet field_name:TestClass.j
  /// CHECK: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldGet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.i

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.j

  // Unrelated volatile loads shouldn't block LSE.
  static int $noinline$testVolatileLoadDifferentFields(TestClass obj1, TestClass obj2) {
    int unused = obj1.vi;
    obj1.i = 1;
    obj2.j = 2;
    int result = obj1.i + obj2.j;
    unused = obj1.vi;
    return result;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileLoadDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  // A volatile load blocks load elimination.
  static int $noinline$testVolatileLoadDifferentFieldsBlocking(TestClass obj1, TestClass obj2) {
    obj1.i = 1;
    obj2.j = 2;
    int unused = obj1.vi;
    return obj1.i + obj2.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStore(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldGet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldGet field_name:TestClass.j

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet field_name:TestClass.j
  /// CHECK-NOT: InstanceFieldSet field_name:TestClass.j

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.j
  static int $noinline$testVolatileLoadRedundantStore(TestClass obj) {
    int unused = obj.vi;
    obj.j = 1;
    obj.j = 2;
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlocking(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.j
  static int $noinline$testVolatileLoadRedundantStoreBlocking(TestClass obj) {
    // This store must be kept due to the volatile load.
    obj.j = 1;
    int unused = obj.vi;
    obj.j = 2;
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileLoadRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet
  static int $noinline$testVolatileLoadRedundantStoreBlockingOnlyLoad(TestClass obj) {
    // This store can be safely removed.
    obj.j = 1;
    obj.j = 2;
    int unused = obj.vi;
    // This load remains due to the volatile load in the middle.
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValues(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.i
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK: Phi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.i

  static int $noinline$testVolatileLoadSetAndMergeValues(TestClass obj, boolean b) {
    if (b) {
      int unused = obj.vi;
      obj.i = 1;
    } else {
      int unused = obj.vi;
      obj.i = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.i
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: Phi

  /// CHECK-START: int Main.$noinline$testVolatileLoadSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldGet

  static int $noinline$testVolatileLoadSetAndMergeValuesBlocking(TestClass obj, boolean b) {
    if (b) {
      obj.i = 1;
    } else {
      obj.i = 2;
    }
    int unused = obj.vi;
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFields(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet field_name:TestClass.i
  /// CHECK: InstanceFieldGet field_name:TestClass.j
  /// CHECK: InstanceFieldSet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.i

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet field_name:TestClass.j

  // Unrelated volatile stores shouldn't block LSE.
  static int $noinline$testVolatileStoreDifferentFields(TestClass obj1, TestClass obj2) {
    obj1.vi = 123;
    obj1.i = 1;
    obj2.j = 2;
    int result = obj1.i + obj2.j;
    obj1.vi = 123;
    return result;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileStoreDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  // A volatile store doesn't block load elimination, as it doesn't clobber existing values.
  static int $noinline$testVolatileStoreDifferentFieldsBlocking(TestClass obj1, TestClass obj2) {
    obj1.i = 1;
    obj2.j = 2;
    obj1.vi = 123;
    return obj1.i + obj2.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStore(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:TestClass.vi
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:TestClass.vi
  /// CHECK:     InstanceFieldSet field_name:TestClass.j
  /// CHECK-NOT: InstanceFieldSet field_name:TestClass.j

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  static int $noinline$testVolatileStoreRedundantStore(TestClass obj) {
    obj.vi = 123;
    obj.j = 1;
    obj.j = 2;
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlocking(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  static int $noinline$testVolatileStoreRedundantStoreBlocking(TestClass obj) {
    // This store must be kept due to the volatile store.
    obj.j = 1;
    obj.vi = 123;
    obj.j = 2;
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldSet field_name:TestClass.j
  /// CHECK: InstanceFieldSet field_name:TestClass.vi
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet field_name:TestClass.j
  /// CHECK-NOT: InstanceFieldSet field_name:TestClass.j

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet field_name:TestClass.vi

  /// CHECK-START: int Main.$noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  static int $noinline$testVolatileStoreRedundantStoreBlockingOnlyLoad(TestClass obj) {
    // This store can be safely removed.
    obj.j = 1;
    obj.j = 2;
    obj.vi = 123;
    // This load can also be safely eliminated as the volatile store doesn't clobber values.
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK: Phi

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  static int $noinline$testVolatileStoreSetAndMergeValues(TestClass obj, boolean b) {
    if (b) {
      obj.vi = 123;
      obj.i = 1;
    } else {
      obj.vi = 123;
      obj.i = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValuesNotBlocking(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValuesNotBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK: Phi

  /// CHECK-START: int Main.$noinline$testVolatileStoreSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet
  static int $noinline$testVolatileStoreSetAndMergeValuesNotBlocking(TestClass obj, boolean b) {
    if (b) {
      obj.i = 1;
    } else {
      obj.i = 2;
    }
    // This volatile store doesn't block the load elimination
    obj.vi = 123;
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFields(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFields(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFields(TestClass, TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  // Unrelated monitor operations shouldn't block LSE.
  static int $noinline$testMonitorOperationDifferentFields(TestClass obj1, TestClass obj2) {
    Main m = new Main();
    synchronized (m) {}

    obj1.i = 1;
    obj2.j = 2;
    int result = obj1.i + obj2.j;

    synchronized (m) {}

    return result;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationDifferentFieldsBlocking(TestClass, TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet
  /// CHECK: InstanceFieldGet

  // A synchronized operation blocks loads.
  static int $noinline$testMonitorOperationDifferentFieldsBlocking(TestClass obj1, TestClass obj2) {
    Main m = new Main();

    obj1.i = 1;
    obj2.j = 2;
    synchronized (m) {
      return obj1.i + obj2.j;
    }
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStore(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStore(TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStore(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  static int $noinline$testMonitorOperationRedundantStore(TestClass obj) {
    Main m = new Main();
    synchronized (m) {
      obj.j = 1;
      obj.j = 2;
    }

    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlocking(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlocking(TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlocking(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  static int $noinline$testMonitorOperationRedundantStoreBlocking(TestClass obj) {
    Main m = new Main();

    // This store must be kept due to the monitor operation.
    obj.j = 1;
    synchronized (m) {}
    obj.j = 2;

    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(TestClass) load_store_elimination (after)
  /// CHECK: InstanceFieldGet

  static int $noinline$testMonitorOperationRedundantStoreBlockingOnlyLoad(TestClass obj) {
    Main m = new Main();

    // This store can be safely removed.
    obj.j = 1;
    obj.j = 2;
    synchronized (m) {}

    // This load remains due to the monitor operation.
    return obj.j;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingExit(TestClass) load_store_elimination (before)
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldSet
  /// CHECK: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingExit(TestClass) load_store_elimination (before)
  /// CHECK: MonitorOperation kind:enter
  /// CHECK: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingExit(TestClass) load_store_elimination (after)
  /// CHECK:     InstanceFieldSet
  /// CHECK:     InstanceFieldSet
  /// CHECK-NOT: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationRedundantStoreBlockingExit(TestClass) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  static int $noinline$testMonitorOperationRedundantStoreBlockingExit(TestClass obj) {
    Main m = new Main();

    synchronized (m) {
      // This store can be removed.
      obj.j = 0;
      // This store must be kept due to the monitor exit operation.
      obj.j = 1;
    }
    obj.j = 2;

    return obj.j;
  }


  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValues(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValues(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: MonitorOperation kind:enter
  /// CHECK-DAG: MonitorOperation kind:exit
  /// CHECK-DAG: MonitorOperation kind:enter
  /// CHECK-DAG: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK: Phi

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValues(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: InstanceFieldGet

  static int $noinline$testMonitorOperationSetAndMergeValues(TestClass obj, boolean b) {
    Main m = new Main();

    if (b) {
      synchronized (m) {}
      obj.i = 1;
    } else {
      synchronized (m) {}
      obj.i = 2;
    }
    return obj.i;
  }

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldGet

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK-DAG: InstanceFieldSet
  /// CHECK-DAG: InstanceFieldSet

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (before)
  /// CHECK-DAG: MonitorOperation kind:enter
  /// CHECK-DAG: MonitorOperation kind:exit

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK-NOT: Phi

  /// CHECK-START: int Main.$noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass, boolean) load_store_elimination (after)
  /// CHECK: InstanceFieldGet

  static int $noinline$testMonitorOperationSetAndMergeValuesBlocking(TestClass obj, boolean b) {
    Main m = new Main();

    if (b) {
      obj.i = 1;
    } else {
      obj.i = 2;
    }
    synchronized (m) {}
    return obj.i;
  }
}
