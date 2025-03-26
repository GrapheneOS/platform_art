/*
 * Copyright (C) 2024 The Android Open Source Project
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

import dalvik.system.VMRuntime;

public class Main {
    public static void main(String[] args) throws Exception {
        for (int p = Thread.MIN_PRIORITY; p <= Thread.MAX_PRIORITY; ++p) {
            int niceness = Thread.nicenessForPriority(p);
            if (p > Thread.MIN_PRIORITY &&  Thread.nicenessForPriority(p - 1) <= niceness) {
                throw new Error("Niceness not monotonic at " + p);
            }
            int mapped_p = Thread.priorityForNiceness(Thread.nicenessForPriority(p));
            if (mapped_p != p) {
                throw new Error(p + " mapped to: " + Thread.nicenessForPriority(p) +
                        " which mapped back to " + mapped_p);
            }
        }
        Thread self = Thread.currentThread();
        VMRuntime vmrt = VMRuntime.getRuntime();
        for (int p = Thread.MIN_PRIORITY; p <= Thread.MAX_PRIORITY; ++p) {
            self.setPriority(p);
            int result = self.getPriority();
            if (result != p) {
                System.out.println("Set priority to " + p + " but got " + result);
            }
            int niceness_result = vmrt.getThreadNiceness(self);
            if (niceness_result != Thread.nicenessForPriority(p)) {
                System.out.println("Set priority to " + p
                    + " but got niceness " + niceness_result);
            }
        }
        for (int n = -20; n <= 19; ++n) {
            vmrt.setThreadNiceness(self, n);
            int result = vmrt.getThreadNiceness(self);
            if (result != n) {
                System.out.println("Set niceness to " + n + " but got " + result);
            }
        }
        System.out.println("Done");
    }

    private static native int getThreadPlatformPriority();
}
