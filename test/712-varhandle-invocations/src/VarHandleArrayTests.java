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

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

// These tests cover DoVarHandleInvokeCommon in interpreter_common.cc.

public class VarHandleArrayTests {
    public static class ArrayStoreTest extends VarHandleUnitTest {
        private static final Integer ZERO = Integer.valueOf(0);
        private static final Integer ONE = Integer.valueOf(1);
        private static final Integer TWO = Integer.valueOf(2);

        private final Integer[] values = new Integer[10];

        private void testIntegerArrayVarHandle() {
            final VarHandle vh = MethodHandles.arrayElementVarHandle(Integer[].class);

            // AccessModeTemplate::kSet
            vh.set(values, 0, ZERO);
            assertEquals(0, values[0].intValue());
            vh.set((Object[]) values, 1, ONE);
            assertEquals(ONE, values[1]);
            assertThrowsAIOBE(() -> vh.set(values, values.length, null));
            assertThrowsCCE(() -> vh.set(values, 6, new Object()));
            assertThrowsCCE(() -> vh.set((Object[]) values, 6, new Object()));
            assertThrowsNPE(() -> vh.set((Integer[]) null, 6, ONE));
            assertThrowsWMTE(() -> vh.set(values, 'c'));
            assertThrowsWMTE(() -> vh.set((Object[]) values, 5, 'c'));

            // AccessModeTemplate::kGetAndUpdate
            assertEquals(ZERO, (Integer) vh.getAndSet(values, 0, ONE));
            assertEquals(ONE, values[0]);
            assertThrowsAIOBE(() -> vh.getAndSet(values, values.length, null));
            assertThrowsCCE(() -> vh.getAndSet(values, 6, new Object()));
            assertThrowsCCE(() -> vh.getAndSet((Object[]) values, 6, new Object()));
            assertThrowsNPE(() -> vh.getAndSet((Integer[]) null, 6, ONE));
            assertThrowsWMTE(() -> vh.getAndSet(values, 'c'));
            assertThrowsWMTE(() -> vh.getAndSet((Object[]) values, 5, 'c'));

            // AccessModeTemplate::kCompareAndExchange
            assertEquals(ONE, (Integer) vh.compareAndExchange(values, 0, ONE, TWO));
            assertEquals(TWO, values[0]);
            assertEquals(TWO, (Integer) vh.compareAndExchange(values, 0, ONE, ZERO));
            assertEquals(TWO, values[0]);
            assertThrowsAIOBE(() -> vh.compareAndExchange(values, values.length, null, null));
            assertThrowsCCE(() -> vh.compareAndExchange(values, 6, 6, new Object()));
            assertThrowsCCE(() -> vh.compareAndExchange((Object[]) values, 6, 6, new Object()));
            assertThrowsNPE(() -> vh.compareAndExchange((Integer[]) null, 6, ONE, ONE));
            assertThrowsWMTE(() -> vh.compareAndExchange(values, null, 'c'));
            assertThrowsWMTE(() -> vh.compareAndExchange((Object[]) values, 5, null, 'c'));

            // AccessModeTemplate::kCompareAndSet
            assertEquals(true, (boolean) vh.compareAndSet(values, 0, TWO, ONE));
            assertEquals(ONE, values[0]);
            assertEquals(false, (boolean) vh.compareAndSet(values, 0, ZERO, TWO));
            assertEquals(ONE, values[0]);
            assertThrowsAIOBE(() -> vh.compareAndSet(values, values.length, null, null));
            assertThrowsCCE(() -> vh.compareAndSet(values, 6, 6, new Object()));
            assertThrowsCCE(() -> vh.compareAndSet((Object[]) values, 6, 6, new Object()));
            assertThrowsNPE(() -> vh.compareAndSet((Integer[]) null, 6, ONE, ONE));
            assertThrowsWMTE(() -> vh.compareAndSet(values, null, 'c'));
            assertThrowsWMTE(() -> vh.compareAndSet((Object[]) values, 5, null, 'c'));
        }

        private void testObjectArrayVarHandle() {
            final VarHandle vho = MethodHandles.arrayElementVarHandle(Object[].class);

            // AccessModeTemplate::kSet
            vho.set(values, 0, ONE);
            assertEquals(ONE, values[0]);
            assertThrowsAIOBE(() -> vho.set(values, values.length, null));
            assertThrowsASE(() -> vho.set(values, 0, new Object()));
            assertThrowsASE(() -> vho.set(values, 0, "hello"));
            assertThrowsNPE(() -> vho.set(null, 0, ZERO));
            assertThrowsWMTE(() -> vho.set(0, ZERO));
            assertThrowsWMTE(() -> vho.set(values, ZERO));

            // AccessModeTemplate::kGetAndUpdate
            assertEquals(ONE, vho.getAndSetAcquire(values, 0, TWO));
            assertThrowsAIOBE(() -> vho.getAndSetRelease(values, values.length, null));
            assertThrowsASE(() -> vho.getAndSet(values, 0, new Object()));
            assertThrowsASE(() -> vho.getAndSet(values, 0, "hello"));
            assertThrowsNPE(() -> vho.getAndSet(null, 0, ZERO));
            assertThrowsWMTE(() -> vho.getAndSet(0, ZERO));
            assertThrowsWMTE(() -> vho.getAndSet(values, ZERO));

            // AccessModeTemplate::kCompareAndExchange
            assertEquals(TWO, vho.compareAndExchange(values, 0, TWO, ZERO));
            assertThrowsAIOBE(() -> vho.compareAndExchange(values, values.length, ONE, TWO));
            assertThrowsASE(() -> vho.compareAndExchange(values, 0, ONE, new Object()));
            assertThrowsASE(() -> vho.compareAndExchange(values, 0, ONE, "hello"));
            assertThrowsNPE(() -> vho.compareAndExchange(null, 0, ONE, ZERO));
            assertThrowsWMTE(() -> vho.compareAndExchange(0, ZERO, ONE));
            assertThrowsWMTE(() -> vho.compareAndExchange(values, ONE, ZERO));

            // AccessModeTemplate::kCompareAndSet
            assertEquals(true, (boolean) vho.compareAndSet(values, 0, ZERO, ONE));
            assertThrowsAIOBE(() -> vho.compareAndSet(values, values.length, ONE, TWO));
            assertThrowsASE(() -> vho.compareAndSet(values, 0, ONE, new Object()));
            assertThrowsASE(() -> vho.compareAndSet(values, 0, ONE, "hello"));
            assertThrowsNPE(() -> vho.compareAndSet(null, 0, ONE, ZERO));
            assertThrowsWMTE(() -> vho.compareAndSet(0, ZERO, ONE));
            assertThrowsWMTE(() -> vho.compareAndSet(values, ONE, ZERO));
        }

        @Override
        protected void doTest() throws Exception {
            testIntegerArrayVarHandle();
            testObjectArrayVarHandle();
        }

        public static void main(String[] args) {
            new ArrayStoreTest().run();
        }
    }

    public static void main(String[] args) {
        ArrayStoreTest.main(args);
    }
}
