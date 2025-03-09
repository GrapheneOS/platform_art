/*
 * Copyright (C) 2016 The Android Open Source Project
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

package com.android.ahat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Site;

import org.junit.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class ObjectHandlerTest {
  @Test
  public void noCrashClassInstance() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatInstance object = dump.getDumpedAhatInstance("aPhantomReference");
    assertNotNull(object);

    AhatHandler handler = new ObjectHandler(dump.getAhatSnapshot());
    TestHandler.testNoCrash(handler, "http://localhost:7100/object?id=" + object.getId());
  }

  @Test
  public void noCrashClassObj() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatSnapshot snapshot = dump.getAhatSnapshot();
    AhatHandler handler = new ObjectHandler(snapshot);

    AhatInstance object = dump.findClass("Main");
    assertNotNull(object);

    TestHandler.testNoCrash(handler, "http://localhost:7100/object?id=" + object.getId());
  }

  @Test
  public void noCrashSystemClassObj() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatSnapshot snapshot = dump.getAhatSnapshot();
    AhatHandler handler = new ObjectHandler(snapshot);

    AhatInstance object = dump.findClass("java.lang.String");
    assertNotNull(object);

    TestHandler.testNoCrash(handler, "http://localhost:7100/object?id=" + object.getId());
  }

  @Test
  public void noCrashArrayInstance() throws IOException {
    TestDump dump = TestDump.getTestDump();

    AhatInstance object = dump.getDumpedAhatInstance("gcPathArray");
    assertNotNull(object);

    AhatHandler handler = new ObjectHandler(dump.getAhatSnapshot());
    TestHandler.testNoCrash(handler, "http://localhost:7100/object?id=" + object.getId());
  }

  @Test
  public void noCrashUnreachable() throws IOException {
    // Exercise the case where the object is unreachable.
    // We had bugs in the past where trying to print the sample path for any
    // unreachable instance would lead to an infinite loop or null pointer
    // exception.

    // Our unreachable object should be the only reference to the
    // unreachableAnchor instance, aside from dumpedStuff.
    TestDump dump = TestDump.getTestDump();

    AhatInstance anchor = dump.getDumpedAhatInstance("unreachableAnchor");
    assertNotNull(anchor);

    List<AhatInstance> reverse = anchor.getReverseReferences();
    assertEquals(2, reverse.size());

    AhatInstance unreachable = reverse.get(0);
    if (!unreachable.isUnreachable()) {
      unreachable = reverse.get(1);
    }
    assertTrue(unreachable.isUnreachable());

    AhatHandler handler = new ObjectHandler(dump.getAhatSnapshot());
    TestHandler.testNoCrash(handler, "http://localhost:7100/object?id=" + unreachable.getId());
  }
}
