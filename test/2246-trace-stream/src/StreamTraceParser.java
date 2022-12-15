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

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;

public class StreamTraceParser {
    public static final int MAGIC_NUMBER = 0x574f4c53;
    public static final int DUAL_CLOCK_VERSION = 3;
    public static final int TRACE_VERSION_DUAL_CLOCK = 0xF3;

    public StreamTraceParser(File file) throws IOException {
        dataStream = new DataInputStream(new FileInputStream(file));
        method_id_map = new HashMap<Integer, String>();
        thread_id_map = new HashMap<Integer, String>();
    }

    public void closeFile() throws IOException {
        dataStream.close();
    }

    public String readString(int num_bytes) throws IOException {
        byte[] buffer = new byte[num_bytes];
        dataStream.readFully(buffer);
        return new String(buffer, StandardCharsets.UTF_8);
    }

    public int readNumber(int num_bytes) throws IOException {
        int number = 0;
        for (int i = 0; i < num_bytes; i++) {
            number += dataStream.readUnsignedByte() << (i * 8);
        }
        return number;
    }

    public void validateTraceHeader(int expected_version) throws Exception {
        // Read 4-byte magic_number
        int magic_number = readNumber(4);
        if (magic_number != MAGIC_NUMBER) {
            throw new Exception("Magic number doesn't match. Expected "
                    + Integer.toHexString(MAGIC_NUMBER) + " Got "
                    + Integer.toHexString(magic_number));
        }
        // Read 2-byte version
        int version = readNumber(2);
        if (version != expected_version) {
            throw new Exception(
                    "Unexpected version. Expected " + expected_version + " Got " + version);
        }
        trace_format_version = version & 0xF;
        // Read 2-byte header_length length
        int header_length = readNumber(2);
        // Read 8-byte starting time - Ignore timestamps since they are not deterministic
        dataStream.skipBytes(8);
        // 4 byte magic_number + 2 byte version + 2 byte offset + 8 byte timestamp
        int num_bytes_read = 16;
        if (version >= DUAL_CLOCK_VERSION) {
            // Read 2-byte record size.
            // TODO(mythria): Check why this is needed. We can derive record_size from version. Not
            // sure why this is needed.
            record_size = readNumber(2);
            num_bytes_read += 2;
        }
        // Skip any padding
        if (header_length > num_bytes_read) {
            dataStream.skipBytes(header_length - num_bytes_read);
        }
    }

    public int GetEntryHeader() throws IOException {
        // Read 2-byte thread-id. On host thread-ids can be greater than 16-bit.
        int thread_id = readNumber(2);
        if (thread_id != 0) {
            return thread_id;
        }
        // Read 1-byte header type
        return readNumber(1);
    }

    public void ProcessMethodInfoEntry() throws IOException {
        // Read 2-byte method info size
        int header_length = readNumber(2);
        // Read header_size data.
        String method_info = readString(header_length);
        String[] tokens = method_info.split("\t", 2);
        // Get method_id and record method_id -> method_name map.
        int method_id = Integer.decode(tokens[0]);
        String method_line = tokens[1].replace('\t', ' ');
        method_line = method_line.substring(0, method_line.length() - 1);
        method_id_map.put(method_id, method_line);
    }

    public void ProcessThreadInfoEntry() throws IOException {
        // Read 2-byte thread id
        int thread_id = readNumber(2);
        // Read 2-byte thread info size
        int header_length = readNumber(2);
        // Read header_size data.
        String thread_info = readString(header_length);
        thread_id_map.put(thread_id, thread_info);
    }

    public String eventTypeToString(int event_type) {
        String str = "";
        for (int i = 0; i < nesting_level; i++) {
            str += ".";
        }
        switch (event_type) {
            case 0:
                nesting_level++;
                str += ".>>";
                break;
            case 1:
                nesting_level--;
                str += "<<";
                break;
            case 2:
                nesting_level--;
                str += "<<E";
                break;
            default:
                str += "??";
        }
        return str;
    }

    public String ProcessEventEntry(int thread_id) throws IOException {
        // Read 4-byte method value
        int method_and_event = readNumber(4);
        int method_id = method_and_event & ~0x3;
        int event_type = method_and_event & 0x3;

        String str = eventTypeToString(event_type) + " " + thread_id_map.get(thread_id) + " "
                + method_id_map.get(method_id);
        // Depending on the version skip either one or two timestamps.
        // TODO(mythria): Probably add a check that time stamps are always greater than initial
        // timestamp.
        int num_bytes_timestamp = (trace_format_version == 2) ? 4 : 8;
        dataStream.skipBytes(num_bytes_timestamp);
        return str;
    }

    DataInputStream dataStream;
    HashMap<Integer, String> method_id_map;
    HashMap<Integer, String> thread_id_map;
    int record_size = 0;
    int trace_format_version = 0;
    int nesting_level = 0;
}
