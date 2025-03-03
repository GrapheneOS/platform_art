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

#include "trace_profile.h"

#include "android-base/stringprintf.h"
#include "arch/context.h"
#include "art_method-inl.h"
#include "base/leb128.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "com_android_art_flags.h"
#include "dex/descriptors_names.h"
#include "gc/task_processor.h"
#include "oat/oat_quick_method_header.h"
#include "runtime.h"
#include "stack.h"
#include "thread-current-inl.h"
#include "thread.h"
#include "thread_list.h"
#include "trace.h"
#include "trace_common.h"

namespace art_flags = com::android::art::flags;

namespace art HIDDEN {

using android::base::StringPrintf;

// This specifies the maximum number of bits we need for encoding one entry. Each entry just
// consists of a SLEB encoded value of method and action encodig which is a maximum of
// sizeof(uintptr_t).
static constexpr size_t kMaxBytesPerTraceEntry = sizeof(uintptr_t);

static constexpr size_t kMaxEntriesAfterFlush = kAlwaysOnTraceBufSize / 2;

// We don't handle buffer overflows when processing the raw trace entries. We have a maximum of
// kAlwaysOnTraceBufSize raw entries and we need a maximum of kMaxBytesPerTraceEntry to encode
// each entry. To avoid overflow, we ensure that there are at least kMinBufSizeForEncodedData
// bytes free space in the buffer.
static constexpr size_t kMinBufSizeForEncodedData = kAlwaysOnTraceBufSize * kMaxBytesPerTraceEntry;

static constexpr size_t kProfileMagicValue = 0x4C4F4D54;

// TODO(mythria): 10 is a randomly chosen value. Tune it if required.
static constexpr size_t kBufSizeForEncodedData = kMinBufSizeForEncodedData * 10;

static constexpr size_t kAlwaysOnTraceHeaderSize = 12;
static constexpr size_t kAlwaysOnMethodInfoHeaderSize = 11;
static constexpr size_t kAlwaysOnThreadInfoHeaderSize = 7;

bool TraceProfiler::profile_in_progress_ = false;

TraceData* TraceProfiler::trace_data_ = nullptr;

void TraceData::AddTracedThread(Thread* thread) {
  MutexLock mu(Thread::Current(), trace_data_lock_);
  size_t thread_id = thread->GetTid();
  if (traced_threads_.find(thread_id) != traced_threads_.end()) {
    return;
  }

  std::string thread_name;
  thread->GetThreadName(thread_name);
  traced_threads_.emplace(thread_id, thread_name);
}

void TraceData::MaybeWaitForTraceDumpToFinish() {
  if (!trace_dump_in_progress_) {
    return;
  }
  trace_dump_condition_.Wait(Thread::Current());
}

void TraceData::SignalTraceDumpComplete() {
  trace_dump_in_progress_ = false;
  trace_dump_condition_.Broadcast(Thread::Current());
}

void TraceData::AppendToLongRunningMethods(const uint8_t* buffer, size_t size) {
  MutexLock mu(Thread::Current(), trace_data_lock_);
  if (curr_buffer_ == nullptr) {
    curr_buffer_.reset(new uint8_t[kBufSizeForEncodedData]);
    curr_index_ = 0;
  }
  if (curr_index_ + size <= kBufSizeForEncodedData) {
    memcpy(curr_buffer_.get() + curr_index_, buffer, size);
    curr_index_ += size;
  } else {
    size_t remaining_bytes = kBufSizeForEncodedData - curr_index_;
    if (remaining_bytes != 0) {
      memcpy(curr_buffer_.get() + curr_index_, buffer, remaining_bytes);
    }
    overflow_buffers_.push_back(std::move(curr_buffer_));
    curr_buffer_.reset(new uint8_t[kBufSizeForEncodedData]);
    memcpy(curr_buffer_.get(), buffer + remaining_bytes, size - remaining_bytes);
  }
}

void TraceProfiler::AllocateBuffer(Thread* thread) {
  if (!art_flags::always_enable_profile_code()) {
    return;
  }

  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::trace_lock_);
  if (!profile_in_progress_) {
    return;
  }

  auto buffer = new uintptr_t[kAlwaysOnTraceBufSize];
  size_t index = kAlwaysOnTraceBufSize;
  if (trace_data_->GetTraceType() == LowOverheadTraceType::kAllMethods) {
    memset(buffer, 0, kAlwaysOnTraceBufSize * sizeof(uintptr_t));
  } else {
    DCHECK(trace_data_->GetTraceType() == LowOverheadTraceType::kLongRunningMethods);
    // For long running methods add a placeholder method exit entry. This avoids
    // additional checks on method exits to see if the previous entry is valid.
    index--;
    buffer[index] = 0x1;
  }
  thread->SetMethodTraceBuffer(buffer, index);
}

LowOverheadTraceType TraceProfiler::GetTraceType() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  // LowOverhead trace entry points are configured based on the trace type. When trace_data_ is null
  // then there is no low overhead tracing running, so we use nop entry points.
  if (trace_data_ == nullptr) {
    return LowOverheadTraceType::kNone;
  }

  return trace_data_->GetTraceType();
}

namespace {
void RecordMethodsOnThreadStack(Thread* thread, uintptr_t* method_trace_buffer)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  struct MethodEntryStackVisitor final : public StackVisitor {
    MethodEntryStackVisitor(Thread* thread_in, Context* context)
        : StackVisitor(thread_in, context, StackVisitor::StackWalkKind::kSkipInlinedFrames) {}

    bool VisitFrame() override REQUIRES_SHARED(Locks::mutator_lock_) {
      ArtMethod* m = GetMethod();
      if (m != nullptr && !m->IsRuntimeMethod()) {
        if (GetCurrentShadowFrame() != nullptr) {
          // TODO(mythria): Support low-overhead tracing for the switch interpreter.
        } else {
          const OatQuickMethodHeader* method_header = GetCurrentOatQuickMethodHeader();
          if (method_header == nullptr) {
            // TODO(mythria): Consider low-overhead tracing support for the GenericJni stubs.
          } else {
            // Ignore nterp methods. We don't support recording trace events in nterp.
            if (!method_header->IsNterpMethodHeader()) {
              stack_methods_.push_back(m);
            }
          }
        }
      }
      return true;
    }

    std::vector<ArtMethod*> stack_methods_;
  };

  std::unique_ptr<Context> context(Context::Create());
  MethodEntryStackVisitor visitor(thread, context.get());
  visitor.WalkStack(true);

  // Create method entry events for all methods currently on the thread's stack.
  uint64_t init_time = TimestampCounter::GetNanoTime(TimestampCounter::GetTimestamp());
  // Set the lsb to 0 to indicate method entry.
  init_time = init_time & ~1;
  size_t index = kAlwaysOnTraceBufSize - 1;
  for (auto smi = visitor.stack_methods_.rbegin(); smi != visitor.stack_methods_.rend(); smi++) {
    method_trace_buffer[index--] = reinterpret_cast<uintptr_t>(*smi);
    method_trace_buffer[index--] = init_time;

    if (index < kMaxEntriesAfterFlush) {
      // To keep the implementation simple, ignore methods deep down the stack. If the call stack
      // unwinds beyond this point then we will see method exits without corresponding method
      // entries.
      break;
    }
  }

  // Record a placeholder method exit event into the buffer so we record method exits for the
  // methods that are currently on stack.
  method_trace_buffer[index] = 0x1;
  thread->SetMethodTraceBuffer(method_trace_buffer, index);
}

// Records the thread and method info.
void DumpThreadMethodInfo(const std::unordered_map<size_t, std::string>& traced_threads,
                          const std::unordered_set<ArtMethod*>& traced_methods,
                          std::ostringstream& os) REQUIRES_SHARED(Locks::mutator_lock_) {
  // Dump data about thread information.
  for (const auto& it : traced_threads) {
    uint8_t thread_header[kAlwaysOnThreadInfoHeaderSize];
    thread_header[0] = kThreadInfoHeaderV2;
    Append4LE(thread_header + 1, it.first);
    Append2LE(thread_header + 5, it.second.length());
    os.write(reinterpret_cast<char*>(thread_header), kAlwaysOnThreadInfoHeaderSize);
    os.write(it.second.c_str(), it.second.length());
  }

  // Dump data about method information.
  for (ArtMethod* method : traced_methods) {
    std::string method_line = GetMethodInfoLine(method);
    uint16_t method_line_length = static_cast<uint16_t>(method_line.length());
    uint8_t method_header[kAlwaysOnMethodInfoHeaderSize];
    method_header[0] = kMethodInfoHeaderV2;
    Append8LE(method_header + 1, reinterpret_cast<uint64_t>(method));
    Append2LE(method_header + 9, method_line_length);
    os.write(reinterpret_cast<char*>(method_header), kAlwaysOnMethodInfoHeaderSize);
    os.write(method_line.c_str(), method_line_length);
  }
}
}  // namespace

class TraceStopTask : public gc::HeapTask {
 public:
  explicit TraceStopTask(uint64_t target_run_time) : gc::HeapTask(target_run_time) {}

  void Run([[maybe_unused]] Thread* self) override { TraceProfiler::TraceTimeElapsed(); }
};

static class LongRunningMethodsTraceStartCheckpoint final : public Closure {
 public:
  void Run(Thread* thread) override REQUIRES_SHARED(Locks::mutator_lock_) {
    auto buffer = new uintptr_t[kAlwaysOnTraceBufSize];
    // Record methods that are currently on stack.
    RecordMethodsOnThreadStack(thread, buffer);
    thread->UpdateTlsLowOverheadTraceEntrypoints(LowOverheadTraceType::kLongRunningMethods);
  }
} long_running_methods_checkpoint_;

static class AllMethodsTraceStartCheckpoint final : public Closure {
 public:
  void Run(Thread* thread) override {
    auto buffer = new uintptr_t[kAlwaysOnTraceBufSize];
    memset(buffer, 0, kAlwaysOnTraceBufSize * sizeof(uintptr_t));
    thread->UpdateTlsLowOverheadTraceEntrypoints(LowOverheadTraceType::kAllMethods);
    thread->SetMethodTraceBuffer(buffer, kAlwaysOnTraceBufSize);
  }
} all_methods_checkpoint_;

void TraceProfiler::Start(LowOverheadTraceType trace_type, uint64_t trace_duration_ns) {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  TimestampCounter::InitializeTimestampCounters();

  Runtime* runtime = Runtime::Current();
  Thread* self = Thread::Current();
  uint64_t new_end_time = 0;
  bool add_trace_end_task = false;
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (Trace::IsTracingEnabledLocked()) {
      LOG(ERROR) << "Cannot start a low-overehad trace when regular tracing is in progress";
      return;
    }

    if (profile_in_progress_) {
      // We allow overlapping starts only when collecting long running methods.
      // If a trace of different type is in progress we ignore the request.
      if (trace_type == LowOverheadTraceType::kAllMethods ||
          trace_data_->GetTraceType() != trace_type) {
        LOG(ERROR) << "Profile already in progress. Ignoring this request";
        return;
      }

      // For long running methods, just update the end time if there's a trace already in progress.
      new_end_time = NanoTime() + trace_duration_ns;
      if (trace_data_->GetTraceEndTime() < new_end_time) {
        trace_data_->SetTraceEndTime(new_end_time);
        add_trace_end_task = true;
      }
    } else {
      profile_in_progress_ = true;
      trace_data_ = new TraceData(trace_type);

      if (trace_type == LowOverheadTraceType::kAllMethods) {
        runtime->GetThreadList()->RunCheckpoint(&all_methods_checkpoint_);
      } else {
        runtime->GetThreadList()->RunCheckpoint(&long_running_methods_checkpoint_);
      }

      if (trace_type == LowOverheadTraceType::kLongRunningMethods) {
        new_end_time = NanoTime() + trace_duration_ns;
        add_trace_end_task = true;
        trace_data_->SetTraceEndTime(new_end_time);
      }
    }
  }

  if (add_trace_end_task) {
    // Add a Task that stops the tracing after trace_duration.
    runtime->GetHeap()->AddHeapTask(new TraceStopTask(new_end_time));
  }
}

void TraceProfiler::Start() {
  TraceProfiler::Start(LowOverheadTraceType::kAllMethods, /* trace_duration_ns= */ 0);
}

void TraceProfiler::Stop() {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::trace_lock_);
  TraceProfiler::StopLocked();
}

void TraceProfiler::StopLocked() {
  if (!profile_in_progress_) {
    LOG(ERROR) << "No Profile in progress but a stop was requested";
    return;
  }

  // We should not delete trace_data_ when there is an ongoing trace dump. So
  // wait for any in progress trace dump to finish.
  trace_data_->MaybeWaitForTraceDumpToFinish();

  static FunctionClosure reset_buffer([](Thread* thread) {
    auto buffer = thread->GetMethodTraceBuffer();
    if (buffer != nullptr) {
      delete[] buffer;
      thread->SetMethodTraceBuffer(/* buffer= */ nullptr, /* offset= */ 0);
    }
    thread->UpdateTlsLowOverheadTraceEntrypoints(LowOverheadTraceType::kNone);
  });

  Runtime::Current()->GetThreadList()->RunCheckpoint(&reset_buffer);
  profile_in_progress_ = false;
  DCHECK_NE(trace_data_, nullptr);
  delete trace_data_;
  trace_data_ = nullptr;
}

size_t TraceProfiler::DumpBuffer(uint32_t thread_id,
                                 uintptr_t* method_trace_entries,
                                 uint8_t* buffer,
                                 std::unordered_set<ArtMethod*>& methods) {
  // Encode header at the end once we compute the number of records.
  uint8_t* curr_buffer_ptr = buffer + kAlwaysOnTraceHeaderSize;

  int num_records = 0;
  uintptr_t prev_method_action_encoding = 0;
  int prev_action = -1;
  for (size_t i = kAlwaysOnTraceBufSize - 1; i > 0; i-=1) {
    uintptr_t method_action_encoding = method_trace_entries[i];
    // 0 value indicates the rest of the entries are empty.
    if (method_action_encoding == 0) {
      break;
    }

    int action = method_action_encoding & ~kMaskTraceAction;
    int64_t diff;
    if (action == TraceAction::kTraceMethodEnter) {
      diff = method_action_encoding - prev_method_action_encoding;

      ArtMethod* method = reinterpret_cast<ArtMethod*>(method_action_encoding & kMaskTraceAction);
      methods.insert(method);
    } else {
      // On a method exit, we don't record the information about method. We just need a 1 in the
      // lsb and the method information can be derived from the last method that entered. To keep
      // the encoded value small just add the smallest value to make the lsb one.
      if (prev_action == TraceAction::kTraceMethodExit) {
        diff = 0;
      } else {
        diff = 1;
      }
    }
    curr_buffer_ptr = EncodeSignedLeb128(curr_buffer_ptr, diff);
    num_records++;
    prev_method_action_encoding = method_action_encoding;
    prev_action = action;
  }

  // Fill in header information:
  // 1 byte of header identifier
  // 4 bytes of thread_id
  // 3 bytes of number of records
  buffer[0] = kEntryHeaderV2;
  Append4LE(buffer + 1, thread_id);
  Append3LE(buffer + 5, num_records);
  return curr_buffer_ptr - buffer;
}

void TraceProfiler::Dump(int fd) {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  std::unique_ptr<File> trace_file(new File(fd, /*check_usage=*/true));
  std::ostringstream os;
  Dump(std::move(trace_file), os);
}

void TraceProfiler::Dump(const char* filename) {
  if (!art_flags::always_enable_profile_code()) {
    LOG(ERROR) << "Feature not supported. Please build with ART_ALWAYS_ENABLE_PROFILE_CODE.";
    return;
  }

  std::unique_ptr<File> trace_file(OS::CreateEmptyFileWriteOnly(filename));
  if (trace_file == nullptr) {
    PLOG(ERROR) << "Unable to open trace file " << filename;
    return;
  }

  std::ostringstream os;
  Dump(std::move(trace_file), os);
}

void TraceProfiler::Dump(std::unique_ptr<File>&& trace_file, std::ostringstream& os) {
  Thread* self = Thread::Current();
  Runtime* runtime = Runtime::Current();

  size_t threads_running_checkpoint = 0;
  std::unique_ptr<TraceDumpCheckpoint> checkpoint;
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (!profile_in_progress_ || trace_data_->IsTraceDumpInProgress()) {
      if (trace_file != nullptr && !trace_file->Close()) {
        PLOG(WARNING) << "Failed to close file.";
      }
      return;
    }

    trace_data_->SetTraceDumpInProgress();

    // Collect long running methods from all the threads;
    checkpoint.reset(new TraceDumpCheckpoint(trace_data_, trace_file));
    threads_running_checkpoint = runtime->GetThreadList()->RunCheckpoint(checkpoint.get());
  }

  // Wait for all threads to dump their data.
  if (threads_running_checkpoint != 0) {
    checkpoint->WaitForThreadsToRunThroughCheckpoint(threads_running_checkpoint);
  }
  checkpoint->FinishTraceDump(os);

  if (trace_file != nullptr) {
    std::string info = os.str();
    if (!trace_file->WriteFully(info.c_str(), info.length())) {
      PLOG(WARNING) << "Failed writing information to file";
    }

    if (!trace_file->Close()) {
      PLOG(WARNING) << "Failed to close file.";
    }
  }
}

void TraceProfiler::ReleaseThreadBuffer(Thread* self) {
  if (!IsTraceProfileInProgress()) {
    return;
  }
  // TODO(mythria): Maybe it's good to cache these and dump them when requested. For now just
  // relese the buffer when a thread is exiting.
  auto buffer = self->GetMethodTraceBuffer();
  delete[] buffer;
  self->SetMethodTraceBuffer(nullptr, 0);
}

bool TraceProfiler::IsTraceProfileInProgress() {
  return profile_in_progress_;
}

void TraceProfiler::StartTraceLongRunningMethods(uint64_t trace_duration_ns) {
  TraceProfiler::Start(LowOverheadTraceType::kLongRunningMethods, trace_duration_ns);
}

void TraceProfiler::TraceTimeElapsed() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  DCHECK_IMPLIES(!profile_in_progress_, trace_data_ != nullptr);
  if (!profile_in_progress_ || trace_data_->GetTraceEndTime() > NanoTime()) {
    // The end duration was extended by another start, so just ignore this task.
    return;
  }
  TraceProfiler::StopLocked();
}

size_t TraceProfiler::DumpLongRunningMethodBuffer(uint32_t thread_id,
                                                  uintptr_t* method_trace_entries,
                                                  uintptr_t* end_trace_entries,
                                                  uint8_t* buffer,
                                                  std::unordered_set<ArtMethod*>& methods) {
  // Encode header at the end once we compute the number of records.
  uint8_t* curr_buffer_ptr = buffer + kAlwaysOnTraceHeaderSize;

  int num_records = 0;
  uintptr_t prev_timestamp_action_encoding = 0;
  uintptr_t prev_method_ptr = 0;
  size_t end_index = end_trace_entries - method_trace_entries;
  for (size_t i = kAlwaysOnTraceBufSize - 1; i >= end_index;) {
    uintptr_t event = method_trace_entries[i--];
    if (event == 0x1) {
      // This is a placeholder event. Ignore this event.
      continue;
    }

    bool is_method_exit = event & 0x1;
    uint64_t timestamp_action_encoding;
    uintptr_t method_ptr;
    if (is_method_exit) {
      // Method exit. We only have timestamp here.
      timestamp_action_encoding = event;
    } else {
      // method entry
      method_ptr = event;
      timestamp_action_encoding = method_trace_entries[i--];
    }

    int64_t timestamp_action_diff = timestamp_action_encoding - prev_timestamp_action_encoding;
    curr_buffer_ptr = EncodeSignedLeb128(curr_buffer_ptr, timestamp_action_diff);
    prev_timestamp_action_encoding = timestamp_action_encoding;

    if (!is_method_exit) {
      int64_t method_diff = method_ptr - prev_method_ptr;
      ArtMethod* method = reinterpret_cast<ArtMethod*>(method_ptr);
      methods.insert(method);
      prev_method_ptr = method_ptr;
      curr_buffer_ptr = EncodeSignedLeb128(curr_buffer_ptr, method_diff);
    }
    num_records++;
  }

  // Fill in header information:
  // 1 byte of header identifier
  // 4 bytes of thread_id
  // 3 bytes of number of records
  // 4 bytes the size of the data
  buffer[0] = kEntryHeaderV2;
  Append4LE(buffer + 1, thread_id);
  Append3LE(buffer + 5, num_records);
  size_t size = curr_buffer_ptr - buffer;
  Append4LE(buffer + 8, size - kAlwaysOnTraceHeaderSize);
  return curr_buffer_ptr - buffer;
}

void TraceProfiler::FlushBufferAndRecordTraceEvent(ArtMethod* method,
                                                   Thread* thread,
                                                   bool is_entry) {
  uint64_t timestamp = TimestampCounter::GetTimestamp();
  std::unordered_set<ArtMethod*> traced_methods;
  uintptr_t* method_trace_entries = thread->GetMethodTraceBuffer();
  DCHECK(method_trace_entries != nullptr);
  uintptr_t** method_trace_curr_ptr = thread->GetTraceBufferCurrEntryPtr();

  // Find the last method exit event. We can flush all the entries before this event. We cannot
  // flush remaining events because we haven't determined if they are long running or not.
  uintptr_t* processed_events_ptr = nullptr;
  for (uintptr_t* ptr = *method_trace_curr_ptr;
       ptr < method_trace_entries + kAlwaysOnTraceBufSize;) {
    if (*ptr & 0x1) {
      // Method exit. We need to keep events until (including this method exit) here.
      processed_events_ptr = ptr + 1;
      break;
    }
    ptr += 2;
  }

  size_t num_occupied_entries = (processed_events_ptr - *method_trace_curr_ptr);
  size_t index = kAlwaysOnTraceBufSize;

  std::unique_ptr<uint8_t> buffer_ptr(new uint8_t[kBufSizeForEncodedData]);
  size_t num_bytes;
  if (num_occupied_entries > kMaxEntriesAfterFlush) {
    // If we don't have sufficient space just record a placeholder exit and flush all the existing
    // events. We have accurate timestamps to filter out these events in a post-processing step.
    // This would happen only when we have very deeply (~1024) nested code.
    num_bytes = DumpLongRunningMethodBuffer(thread->GetTid(),
                                            method_trace_entries,
                                            *method_trace_curr_ptr,
                                            buffer_ptr.get(),
                                            traced_methods);

    // Encode a placeholder exit event. This will be ignored when dumping the methods.
    method_trace_entries[--index] = 0x1;
  } else {
    // Flush all the entries till the method exit event.
    num_bytes = DumpLongRunningMethodBuffer(thread->GetTid(),
                                            method_trace_entries,
                                            processed_events_ptr,
                                            buffer_ptr.get(),
                                            traced_methods);

    // Move the remaining events to the start of the buffer.
    for (uintptr_t* ptr = processed_events_ptr - 1; ptr >= *method_trace_curr_ptr; ptr--) {
      method_trace_entries[--index] = *ptr;
    }
  }

  // Record new entry
  if (is_entry) {
    method_trace_entries[--index] = reinterpret_cast<uintptr_t>(method);
    method_trace_entries[--index] = timestamp & ~1;
  } else {
    if (method_trace_entries[index] & 0x1) {
      method_trace_entries[--index] = timestamp | 1;
    } else {
      size_t prev_timestamp = method_trace_entries[index];
      if (timestamp - prev_timestamp < kLongRunningMethodThreshold) {
        index += 2;
        DCHECK_LT(index, kAlwaysOnTraceBufSize);
      } else {
        method_trace_entries[--index] = timestamp | 1;
      }
    }
  }
  *method_trace_curr_ptr = method_trace_entries + index;

  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  trace_data_->AppendToLongRunningMethods(buffer_ptr.get(), num_bytes);
  trace_data_->AddTracedMethods(traced_methods);
  trace_data_->AddTracedThread(thread);
}

std::string TraceProfiler::GetLongRunningMethodsString() {
  if (!art_flags::always_enable_profile_code()) {
    return std::string();
  }

  std::ostringstream os;
  Dump(std::unique_ptr<File>(), os);
  return os.str();
}

void TraceDumpCheckpoint::Run(Thread* thread) {
  auto method_trace_entries = thread->GetMethodTraceBuffer();
  if (method_trace_entries != nullptr) {
    std::unordered_set<ArtMethod*> traced_methods;
    if (trace_data_->GetTraceType() == LowOverheadTraceType::kLongRunningMethods) {
      uintptr_t* method_trace_curr_ptr = *(thread->GetTraceBufferCurrEntryPtr());
      std::unique_ptr<uint8_t> buffer_ptr(new uint8_t[kBufSizeForEncodedData]);
      size_t num_bytes = TraceProfiler::DumpLongRunningMethodBuffer(thread->GetTid(),
                                                                    method_trace_entries,
                                                                    method_trace_curr_ptr,
                                                                    buffer_ptr.get(),
                                                                    traced_methods);
      MutexLock mu(Thread::Current(), trace_file_lock_);
      if (trace_file_ != nullptr) {
        if (!trace_file_->WriteFully(buffer_ptr.get(), num_bytes)) {
          PLOG(WARNING) << "Failed streaming a tracing event.";
        }
      } else {
        trace_data_->AppendToLongRunningMethods(buffer_ptr.get(), num_bytes);
      }
    } else {
      std::unique_ptr<uint8_t> buffer_ptr(new uint8_t[kBufSizeForEncodedData]);
      size_t num_bytes = TraceProfiler::DumpBuffer(
          thread->GetTid(), method_trace_entries, buffer_ptr.get(), traced_methods);
      MutexLock mu(Thread::Current(), trace_file_lock_);
      if (!trace_file_->WriteFully(buffer_ptr.get(), num_bytes)) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
    }
    trace_data_->AddTracedThread(thread);
    trace_data_->AddTracedMethods(traced_methods);
  }
  barrier_.Pass(Thread::Current());
}

void TraceDumpCheckpoint::WaitForThreadsToRunThroughCheckpoint(size_t threads_running_checkpoint) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, ThreadState::kWaitingForCheckPointsToRun);
  barrier_.Increment(self, threads_running_checkpoint);
}

void TraceDumpCheckpoint::FinishTraceDump(std::ostringstream& os) {
  // Dump all the data.
  trace_data_->DumpData(os);

  // Any trace stop requests will be blocked while a dump is in progress. So
  // broadcast the completion condition for any waiting requests.
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  trace_data_->SignalTraceDumpComplete();
}

void TraceData::DumpData(std::ostringstream& os) {
  std::unordered_set<ArtMethod*> methods;
  std::unordered_map<size_t, std::string> threads;
  {
    // We cannot dump method information while holding trace_lock_, since we have to also
    // acquire a mutator lock. Take a snapshot of thread and method information.
    MutexLock mu(Thread::Current(), trace_data_lock_);
    if (curr_buffer_ != nullptr) {
      for (size_t i = 0; i < overflow_buffers_.size(); i++) {
        os.write(reinterpret_cast<char*>(overflow_buffers_[i].get()), kBufSizeForEncodedData);
      }

      os.write(reinterpret_cast<char*>(curr_buffer_.get()), curr_index_);
    }

    methods = traced_methods_;
    threads = traced_threads_;
  }

  // Dump the information about traced_methods and threads
  {
    ScopedObjectAccess soa(Thread::Current());
    DumpThreadMethodInfo(threads, methods, os);
  }
}

}  // namespace art
