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

#include <stdio.h>

#include <map>
#include <memory>

#include "base/leb128.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"

namespace art {

// These constants are defined in the ART sources in the following files:
//
// - art/runtime/trace.h
// - art/runtime/trace_profile.cc
static const int kThreadInfoHeaderV2 = 0;
static const int kMethodInfoHeaderV2 = 1;
static const int kEntryHeaderV2 = 2;
static const int kMethodEntry = 0;
static const int kMethodExit = 1;
static const int kAlwaysOnMethodInfoHeaderSize = 11;
static const int kAlwaysOnTraceHeaderSize = 12;

uint64_t ReadNumber(int num_bytes, uint8_t* header) {
  uint64_t number = 0;
  for (int i = 0; i < num_bytes; i++) {
    uint64_t c = header[i];
    number += c << (i * 8);
  }
  return number;
}

bool ProcessMethodInfo(std::unique_ptr<File>& file, std::map<uint64_t, std::string>& name_map) {
  // The first byte that specified the type of the packet is already read in
  // ParseLongRunningMethodTrace.
  uint8_t header[kAlwaysOnMethodInfoHeaderSize - 1];
  if (!file->ReadFully(&header, sizeof(header))) {
    printf("Couldn't read header\n");
    return false;
  }
  uint64_t id = ReadNumber(8, header);
  int length = ReadNumber(2, header + 8);

  std::unique_ptr<char[]> name(new char[length]);
  if (!file->ReadFully(name.get(), length)) {
    return false;
  }
  std::string str(name.get(), length);
  std::replace(str.begin(), str.end(), '\t', ' ');
  if (str[str.length() - 1] == '\n') {
    str.erase(str.length() - 1);
  }
  name_map.emplace(id, str);
  return true;
}

void PrintTraceEntry(const std::string& method_name,
                     int event_type,
                     int* current_depth,
                     size_t timestamp) {
  std::string entry;
  for (int i = 0; i < *current_depth; i++) {
    entry.push_back('.');
  }
  if (event_type == kMethodEntry) {
    *current_depth += 1;
    entry.append(".>> ");
  } else if (event_type == kMethodExit) {
    *current_depth -= 1;
    entry.append("<< ");
  } else {
    entry.append("?? ");
  }
  entry.append(" ");
  entry.append(method_name);
  entry.append(" ");
  entry.append(std::to_string(timestamp));
  entry.append("\n");
  printf("%s", entry.c_str());
}

bool SkipTraceEntries(std::unique_ptr<File>& file) {
  // The first byte that specified the type of the packet is already read in
  // ParseLongRunningMethodTrace.
  uint8_t header[kAlwaysOnTraceHeaderSize - 1];
  if (!file->ReadFully(header, sizeof(header))) {
    return false;
  }

  // Read thread id
  ReadNumber(4, header);
  int offset = 4;
  // Read number of records
  ReadNumber(3, header + offset);
  offset += 3;
  int total_size = ReadNumber(4, header + offset);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[total_size]);
  if (!file->ReadFully(buffer.get(), total_size)) {
    return false;
  }
  return true;
}

bool ProcessLongRunningMethodTraceEntries(std::unique_ptr<File>& file,
                         std::map<int64_t, int>& current_depth_map,
                         std::map<uint64_t, std::string>& method_map) {
  // The first byte that specified the type of the packet is already read in
  // ParseLongRunningMethodTrace.
  uint8_t header[kAlwaysOnTraceHeaderSize - 1];
  if (!file->ReadFully(header, sizeof(header))) {
    return false;
  }

  uint32_t thread_id = ReadNumber(4, header);
  int offset = 4;
  int num_records = ReadNumber(3, header + offset);
  offset += 3;
  int total_size = ReadNumber(4, header + offset);
  if (total_size == 0) {
    return true;
  }
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[total_size]);
  if (!file->ReadFully(buffer.get(), total_size)) {
    return false;
  }

  printf("Thread: %d\n", thread_id);
  int current_depth = 0;
  if (current_depth_map.find(thread_id) != current_depth_map.end()) {
    // Get the current call stack depth. If it is the first method we are seeing on this thread
    // then this map wouldn't have an entry, and we start with the depth of 0.
    current_depth = current_depth_map[thread_id];
  }

  const uint8_t* current_buffer_ptr = buffer.get();
  const uint8_t* end_ptr = buffer.get() + total_size;
  uint64_t prev_method_id = 0;
  int64_t prev_timestamp_and_action = 0;
  for (int i = 0; i < num_records; i++) {
    // Read timestamp and action
    int64_t ts_diff = 0;
    if (!DecodeSignedLeb128Checked(&current_buffer_ptr, end_ptr, &ts_diff)) {
      LOG(FATAL) << "Reading past the buffer when decoding timestamp";
    }
    int64_t timestamp_and_action = prev_timestamp_and_action + ts_diff;
    prev_timestamp_and_action = timestamp_and_action;
    bool is_method_exit = timestamp_and_action & 0x1;

    uint64_t method_id;
    std::string method_name;
    if (!is_method_exit) {
      int64_t method_diff = 0;
      if (!DecodeSignedLeb128Checked(&current_buffer_ptr, end_ptr, &method_diff)) {
        LOG(FATAL) << "Reading past the buffer when decoding method id";
      }
      method_id = prev_method_id + method_diff;
      prev_method_id = method_id;
      if (method_map.find(method_id) == method_map.end()) {
        LOG(FATAL) << "No entry for method " << std::hex << method_id;
      }
      method_name = method_map[method_id];
    }

    PrintTraceEntry(method_name,
                    is_method_exit? kMethodExit: kMethodEntry,
                    &current_depth,
                    timestamp_and_action & ~0x1);
  }
  current_depth_map[thread_id] = current_depth;
  return true;
}

void ParseLongRunningMethodTrace(char* file_name) {
  std::unique_ptr<File> file(OS::OpenFileForReading(file_name));
  if (file == nullptr) {
    printf("Couldn't open file\n");
    return;
  }

  // Map to maintain information about threads and methods
  std::map<uint64_t, std::string> method_map;

  // Map to Maintain the current depth of the method in the call stack. Used to
  // correctly indent when printing the trace events.
  std::map<int64_t, int> current_depth_map;

  // First parse metadata. To keep the implementation of dumping the data
  // simple, we don't ensure that the information about methods is dumped before the
  // methods. This is also good if the ANR report got truncated. We will then
  // have information about how long the methods took and we can infer some of
  // the method names from the stack trace.
  while (true) {
    uint8_t entry_header;
    if (!file->ReadFully(&entry_header, sizeof(entry_header))) {
      break;
    }
    if (entry_header == kEntryHeaderV2) {
      if (!SkipTraceEntries(file)) {
        break;
      }
    } else {
      DCHECK_EQ(entry_header, kMethodInfoHeaderV2);
      if (!ProcessMethodInfo(file, method_map)) {
        break;
      }
    }
  }

  // Reset file
  file->ResetOffset();

  while (true) {
    uint8_t entry_header;
    if (!file->ReadFully(&entry_header, sizeof(entry_header))) {
      break;
    }
    if (entry_header != kEntryHeaderV2) {
      break;
    }
    if (!ProcessLongRunningMethodTraceEntries(file, current_depth_map, method_map)) {
      break;
    }
  }
}

}  // namespace art

int main(int argc, char **argv) {
  if (argc < 1) {
    printf("Usage trace <filename>");
    return -1;
  }

  art::ParseLongRunningMethodTrace(argv[1]);
  return 0;
}
