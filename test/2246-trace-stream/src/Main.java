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

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Method;

public class Main {
    private static final String TEMP_FILE_NAME_PREFIX = "test";
    private static final String TEMP_FILE_NAME_SUFFIX = ".trace";
    private static File file;

    public static void main(String[] args) throws Exception {
        String name = System.getProperty("java.vm.name");
        if (!"Dalvik".equals(name)) {
            System.out.println("This test is not supported on " + name);
            return;
        }
        file = createTempFile();
        FileOutputStream out_file = new FileOutputStream(file);
        Main m = new Main();
        Thread t = new Thread(() -> {
            Main m1 = new Main();
            m1.doSomeWork();
        }, "TestThread2246");
        try {
            if (VMDebug.getMethodTracingMode() != 0) {
                VMDebug.stopMethodTracing();
            }

            VMDebug.startMethodTracing(file.getPath(), out_file.getFD(), 0, 0, false, 0, true);
            t.start();
            t.join();
            m.doSomeWork();
            m.doSomeWorkThrow();
            VMDebug.stopMethodTracing();
            out_file.close();
            m.CheckTraceFileFormat(file);
        } finally {
            if (out_file != null) {
                out_file.close();
            }
        }
    }

    private void CheckTraceFileFormat(File trace_file) throws Exception {
        StreamTraceParser parser = new StreamTraceParser(trace_file);
        parser.validateTraceHeader(StreamTraceParser.TRACE_VERSION_DUAL_CLOCK);
        boolean has_entries = true;
        boolean seen_stop_tracing_method = false;
        while (has_entries) {
            int header_type = parser.GetEntryHeader();
            switch (header_type) {
                case 1:
                    parser.ProcessMethodInfoEntry();
                    break;
                case 2:
                    parser.ProcessThreadInfoEntry();
                    break;
                case 3:
                    // TODO(mythria): Add test to also check format of trace summary.
                    has_entries = false;
                    break;
                default:
                    String event_string = parser.ProcessEventEntry(header_type);
                    // Ignore events after method tracing was stopped. The code that is executed
                    // later could be non-deterministic.
                    if (!seen_stop_tracing_method) {
                        System.out.println(event_string);
                    }
                    if (event_string.contains("Main$VMDebug stopMethodTracing")) {
                        seen_stop_tracing_method = true;
                    }
            }
        }
        parser.closeFile();
    }

    private static File createTempFile() throws Exception {
        try {
            return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
        } catch (IOException e) {
            System.setProperty("java.io.tmpdir", "/data/local/tmp");
            try {
                return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
            } catch (IOException e2) {
                System.setProperty("java.io.tmpdir", "/sdcard");
                return File.createTempFile(TEMP_FILE_NAME_PREFIX, TEMP_FILE_NAME_SUFFIX);
            }
        }
    }

    public void callOuterFunction() {
        callLeafFunction();
    }

    public void callLeafFunction() {}

    public void doSomeWork() {
        callOuterFunction();
        callLeafFunction();
    }

    public void callThrowFunction() throws Exception {
        throw new Exception("test");
    }

    public void doSomeWorkThrow() {
        try {
            callThrowFunction();
        } catch (Exception e) {
        }
    }

    private static class VMDebug {
        private static final Method startMethodTracingMethod;
        private static final Method stopMethodTracingMethod;
        private static final Method getMethodTracingModeMethod;
        static {
            try {
                Class<?> c = Class.forName("dalvik.system.VMDebug");
                startMethodTracingMethod = c.getDeclaredMethod("startMethodTracing", String.class,
                        FileDescriptor.class, Integer.TYPE, Integer.TYPE, Boolean.TYPE,
                        Integer.TYPE, Boolean.TYPE);
                stopMethodTracingMethod = c.getDeclaredMethod("stopMethodTracing");
                getMethodTracingModeMethod = c.getDeclaredMethod("getMethodTracingMode");
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }

        public static void startMethodTracing(String filename, FileDescriptor fd, int bufferSize,
                int flags, boolean samplingEnabled, int intervalUs, boolean streaming)
                throws Exception {
            startMethodTracingMethod.invoke(
                    null, filename, fd, bufferSize, flags, samplingEnabled, intervalUs, streaming);
        }
        public static void stopMethodTracing() throws Exception {
            stopMethodTracingMethod.invoke(null);
        }
        public static int getMethodTracingMode() throws Exception {
            return (int) getMethodTracingModeMethod.invoke(null);
        }
    }
}
