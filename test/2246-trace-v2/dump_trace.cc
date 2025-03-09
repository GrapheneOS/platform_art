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

#include <stdio.h>

#include <map>
#include <memory>

#include "base/leb128.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "jni.h"

namespace art {
namespace {

static const int kMagicValue = 0x574f4c53;
static const int kVersionDualClockStreaming = 0xf5;
static const int kVersionDualClock = 0x05;
static const int kThreadInfo = 0;
static const int kMethodInfo = 1;
static const int kTraceEntries = 2;
static const int kTraceActionBits = 2;
static const int kSummary = 3;
static const int kMethodEntry = 0;
static const int kMethodExitNormal = 1;
static const int kMethodExitError = 2;

// List of methods that could be triggered by a GC. It isn't possible to control
// when GCs happen especially in gcstress configs. So we ignore certain methods
// that could be executed based on when GC occurs.
static const std::string_view ignored_methods_list[] = {
    "java.lang.ref.ReferenceQueue add (Ljava/lang/ref/Reference;)V ReferenceQueue.java"
};

uint64_t ReadNumber(int num_bytes, uint8_t* header) {
  uint64_t number = 0;
  for (int i = 0; i < num_bytes; i++) {
    uint64_t c = header[i];
    number += c << (i * 8);
  }
  return number;
}

bool ProcessThreadOrMethodInfo(std::unique_ptr<File>& file,
                               std::map<uint64_t, std::string>& name_map,
                               bool is_method) {
  uint8_t header[10];
  uint8_t header_length = is_method ? 10 : 6;
  if (!file->ReadFully(&header, header_length)) {
    printf("Couldn't read header\n");
    return false;
  }
  uint8_t num_bytes_for_id = is_method ? 8 : 4;
  uint64_t id = ReadNumber(num_bytes_for_id, header);
  int length = ReadNumber(2, header + num_bytes_for_id);

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

bool MethodInIgnoreList(const std::string& method_name) {
  for (const std::string_view& ignored_method : ignored_methods_list) {
    if (method_name.compare(ignored_method) == 0) {
      return true;
    }
  }
  return false;
}

void PrintTraceEntry(const std::string& thread_name,
                     const std::string& method_name,
                     int event_type,
                     int* current_depth,
                     std::string& ignored_method,
                     int* ignored_method_depth) {
  bool ignore_trace_entry = false;
  if (ignored_method.empty()) {
    // Check if we need to ignore the method.
    if (MethodInIgnoreList(method_name)) {
      CHECK_EQ(event_type, kMethodEntry);
      ignored_method = method_name;
      *ignored_method_depth = *current_depth;
      ignore_trace_entry = true;
    }
  } else {
    // Check if the ignored method is exiting.
    if (MethodInIgnoreList(method_name) && *current_depth == *ignored_method_depth + 1) {
      CHECK_NE(event_type, kMethodEntry);
      ignored_method.clear();
    }
    ignore_trace_entry = true;
  }
  std::string entry;
  for (int i = 0; i < *current_depth; i++) {
    entry.push_back('.');
  }
  if (event_type == kMethodEntry) {
    *current_depth += 1;
    entry.append(".>> ");
  } else if (event_type == kMethodExitNormal) {
    *current_depth -= 1;
    entry.append("<< ");
  } else if (event_type == kMethodExitError) {
    *current_depth -= 1;
    entry.append("<<E ");
  } else {
    entry.append("?? ");
  }
  entry.append(thread_name);
  entry.append(" ");
  entry.append(method_name);
  entry.append("\n");
  if (!ignore_trace_entry) {
    printf("%s", entry.c_str());
  }
}

bool ProcessTraceEntries(std::unique_ptr<File>& file,
                         std::map<int64_t, int>& current_depth_map,
                         std::map<uint64_t, std::string>& thread_map,
                         std::map<uint64_t, std::string>& method_map,
                         bool is_dual_clock,
                         const char* thread_name_filter,
                         std::map<uint64_t, std::string>& ignored_method_map,
                         std::map<int64_t, int>& ignored_method_depth_map) {
  uint8_t header[11];
  if (!file->ReadFully(header, sizeof(header))) {
    return false;
  }

  uint32_t thread_id = ReadNumber(4, header);
  int offset = 4;
  int num_records = ReadNumber(3, header + offset);
  offset += 3;
  int total_size = ReadNumber(4, header + offset);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[total_size]);
  if (!file->ReadFully(buffer.get(), total_size)) {
    return false;
  }

  int current_depth = 0;
  if (current_depth_map.find(thread_id) != current_depth_map.end()) {
    // Get the current call stack depth. If it is the first method we are seeing on this thread
    // then this map wouldn't haven an entry we start with the depth of 0.
    current_depth = current_depth_map[thread_id];
  }

  int ignored_method_depth = 0;
  std::string ignored_method;
  if (ignored_method_map.find(thread_id) != ignored_method_map.end()) {
    ignored_method = ignored_method_map[thread_id];
    ignored_method_depth = ignored_method_depth_map[thread_id];
  }

  std::string thread_name = thread_map[thread_id];
  bool print_thread_events = (thread_name.compare(thread_name_filter) == 0);

  const uint8_t* current_buffer_ptr = buffer.get();
  int64_t prev_method_value = 0;
  for (int i = 0; i < num_records; i++) {
    int64_t diff = 0;
    if (!DecodeSignedLeb128Checked(&current_buffer_ptr, buffer.get() + total_size - 1, &diff)) {
      LOG(FATAL) << "Reading past the buffer???";
    }
    int64_t curr_method_value = prev_method_value + diff;
    prev_method_value = curr_method_value;
    uint8_t event_type = curr_method_value & 0x3;
    uint64_t method_id = (curr_method_value >> kTraceActionBits) << kTraceActionBits;
    if (method_map.find(method_id) == method_map.end()) {
      LOG(FATAL) << "No entry for method " << std::hex << method_id;
    }
    if (print_thread_events) {
      PrintTraceEntry(thread_name,
                      method_map[method_id],
                      event_type,
                      &current_depth,
                      ignored_method,
                      &ignored_method_depth);
    }
    // Read timestamps
    DecodeUnsignedLeb128(&current_buffer_ptr);
    if (is_dual_clock) {
      DecodeUnsignedLeb128(&current_buffer_ptr);
    }
  }
  current_depth_map[thread_id] = current_depth;
  if (!ignored_method.empty()) {
    ignored_method_map[thread_id] = ignored_method;
    ignored_method_depth_map[thread_id] = ignored_method_depth;
  }
  return true;
}

extern "C" JNIEXPORT void JNICALL Java_Main_dumpTrace(JNIEnv* env,
                                                      jclass,
                                                      jstring fileName,
                                                      jstring threadName) {
  const char* file_name = env->GetStringUTFChars(fileName, nullptr);
  const char* thread_name = env->GetStringUTFChars(threadName, nullptr);
  std::map<uint64_t, std::string> thread_map;
  std::map<uint64_t, std::string> method_map;
  std::map<uint64_t, std::string> ignored_method_map;
  std::map<int64_t, int> current_depth_map;
  std::map<int64_t, int> ignored_method_depth_map;

  std::unique_ptr<File> file(OS::OpenFileForReading(file_name));
  if (file == nullptr) {
    printf("Couldn't open file\n");
    return;
  }

  uint8_t header[32];
  if (!file->ReadFully(&header, sizeof(header))) {
    printf("Couldn't read header\n");
    return;
  }
  int magic_value = ReadNumber(4, header);
  if (magic_value != kMagicValue) {
    printf("Incorrect magic value got:%0x expected:%0x\n", magic_value, kMagicValue);
    return;
  }
  int version = ReadNumber(2, header + 4);
  printf("version=%0x\n", version);

  bool is_dual_clock = (version == kVersionDualClock) || (version == kVersionDualClockStreaming);
  bool has_entries = true;
  while (has_entries) {
    uint8_t entry_header;
    if (!file->ReadFully(&entry_header, sizeof(entry_header))) {
      break;
    }
    switch (entry_header) {
      case kThreadInfo:
        if (!ProcessThreadOrMethodInfo(file, thread_map, /*is_method=*/false)) {
          has_entries = false;
        }
        break;
      case kMethodInfo:
        if (!ProcessThreadOrMethodInfo(file, method_map, /*is_method=*/true)) {
          has_entries = false;
        }
        break;
      case kTraceEntries:
        ProcessTraceEntries(file,
                            current_depth_map,
                            thread_map,
                            method_map,
                            is_dual_clock,
                            thread_name,
                            ignored_method_map,
                            ignored_method_depth_map);
        break;
      case kSummary:
        has_entries = false;
        break;
      default:
        printf("Invalid Header %d\n", entry_header);
        has_entries = false;
        break;
    }
  }

  env->ReleaseStringUTFChars(fileName, file_name);
  env->ReleaseStringUTFChars(threadName, thread_name);
}

}  // namespace
}  // namespace art
