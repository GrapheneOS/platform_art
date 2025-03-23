/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "trace.h"

#include <sys/uio.h>
#include <unistd.h>

#include "android-base/macros.h"
#include "android-base/stringprintf.h"
#include "art_method-inl.h"
#include "base/casts.h"
#include "base/leb128.h"
#include "base/os.h"
#include "base/pointer_size.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_linker.h"
#include "com_android_art_flags.h"
#include "common_throws.h"
#include "debugger.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "gc/scoped_gc_critical_section.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "nativehelper/scoped_local_ref.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread.h"
#include "thread_list.h"
#include "trace_common.h"
#include "trace_profile.h"

namespace art_flags = com::android::art::flags;

namespace art HIDDEN {

struct MethodTraceRecord {
  ArtMethod* method;
  TraceAction action;
  uint64_t wall_clock_time;
  uint64_t thread_cpu_time;
};

using android::base::StringPrintf;

static constexpr size_t TraceActionBits = MinimumBitsToStore(
    static_cast<size_t>(kTraceMethodActionMask));
static constexpr uint8_t kOpNewMethod = 1U;
static constexpr uint8_t kOpNewThread = 2U;
static constexpr uint8_t kOpTraceSummary = 3U;

static const char     kTraceTokenChar             = '*';
static const uint16_t kTraceHeaderLength          = 32;
static const uint32_t kTraceMagicValue            = 0x574f4c53;
static const uint16_t kTraceVersionSingleClock    = 2;
static const uint16_t kTraceVersionDualClock      = 3;
static const uint16_t kTraceRecordSizeSingleClock = 10;  // using v2
static const uint16_t kTraceRecordSizeDualClock   = 14;  // using v3 with two timestamps
static const size_t kNumTracePoolBuffers = 32;


static constexpr size_t kMinBufSize = 18U;  // Trace header is up to 18B.
// Size of per-thread buffer size. The value is chosen arbitrarily. This value
// should be greater than kMinBufSize.
static constexpr size_t kPerThreadBufSize = 512 * 1024;
static_assert(kPerThreadBufSize > kMinBufSize);
// On average we need 12 bytes for encoding an entry. We typically use two
// entries in per-thread buffer, the scaling factor is 6.
static constexpr size_t kScalingFactorEncodedEntries = 6;

// The key identifying the tracer to update instrumentation.
static constexpr const char* kTracerInstrumentationKey = "Tracer";

double TimestampCounter::tsc_to_nanosec_scaling_factor = -1;

Trace* Trace::the_trace_ = nullptr;
pthread_t Trace::sampling_pthread_ = 0U;
std::unique_ptr<std::vector<ArtMethod*>> Trace::temp_stack_trace_;


static TraceAction DecodeTraceAction(uint32_t tmid) {
  return static_cast<TraceAction>(tmid & kTraceMethodActionMask);
}

namespace {
TraceClockSource GetClockSourceFromFlags(int flags) {
  bool need_wall = flags & Trace::TraceFlag::kTraceClockSourceWallClock;
  bool need_thread_cpu = flags & Trace::TraceFlag::kTraceClockSourceThreadCpu;
  if (need_wall && need_thread_cpu) {
    return TraceClockSource::kDual;
  } else if (need_wall) {
    return TraceClockSource::kWall;
  } else if (need_thread_cpu) {
    return TraceClockSource::kThreadCpu;
  } else {
    return kDefaultTraceClockSource;
  }
}

int GetTraceFormatVersionFromFlags(int flags) {
  int version = (flags & Trace::kTraceFormatVersionFlagMask) >> Trace::kTraceFormatVersionShift;
  return version;
}

}  // namespace

// Temporary code for debugging b/342768977
int num_trace_starts_ GUARDED_BY(Locks::trace_lock_);
int num_trace_stops_initiated_ GUARDED_BY(Locks::trace_lock_);
std::atomic<int> num_trace_stops_finished_;
std::string Trace::GetDebugInformation() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  std::stringstream debug_info;
  debug_info << "start:" << num_trace_starts_ << "stop:" << num_trace_stops_initiated_ << "done:"
             << num_trace_stops_finished_ << "trace:" << the_trace_;
  return debug_info.str();
}

bool TraceWriter::HasMethodEncoding(ArtMethod* method) {
  return art_method_id_map_.find(method) != art_method_id_map_.end();
}

std::pair<uint32_t, bool> TraceWriter::GetMethodEncoding(ArtMethod* method) {
  auto it = art_method_id_map_.find(method);
  if (it != art_method_id_map_.end()) {
    return std::pair<uint32_t, bool>(it->second, false);
  } else {
    uint32_t idx = current_method_index_;
    art_method_id_map_.emplace(method, idx);
    current_method_index_++;
    return std::pair<uint32_t, bool>(idx, true);
  }
}

uint16_t TraceWriter::GetThreadEncoding(pid_t thread_id) {
  auto it = thread_id_map_.find(thread_id);
  if (it != thread_id_map_.end()) {
    return it->second;
  }

  uint16_t idx = current_thread_index_;
  thread_id_map_.emplace(thread_id, current_thread_index_);
  DCHECK_LT(current_thread_index_, (1 << 16) - 2);
  current_thread_index_++;
  return idx;
}

class TraceWriterTask : public SelfDeletingTask {
 public:
  TraceWriterTask(
      TraceWriter* trace_writer, int index, uintptr_t* buffer, size_t cur_offset, size_t thread_id)
      : trace_writer_(trace_writer),
        index_(index),
        buffer_(buffer),
        cur_offset_(cur_offset),
        thread_id_(thread_id) {}

  void Run(Thread* self ATTRIBUTE_UNUSED) override {
    ProcessBuffer(buffer_, cur_offset_, thread_id_);
    if (index_ == -1) {
      // This was a temporary buffer we allocated since there are no free buffers and it wasn't
      // safe to wait for one. This should only happen when we have fewer buffers than the number
      // of threads.
      delete[] buffer_;
    }
    trace_writer_->ReleaseBuffer(index_);
  }

  virtual void ProcessBuffer(uintptr_t* buffer, size_t cur_offset, size_t thread_id) = 0;

  TraceWriter* GetTraceWriter() { return trace_writer_; }

 private:
  TraceWriter* trace_writer_;
  int index_;
  uintptr_t* buffer_;
  size_t cur_offset_;
  size_t thread_id_;
};

class TraceEntriesWriterTask final : public TraceWriterTask {
 public:
  TraceEntriesWriterTask(
      TraceWriter* trace_writer, int index, uintptr_t* buffer, size_t cur_offset, size_t tid)
      : TraceWriterTask(trace_writer, index, buffer, cur_offset, tid) {}

  void ProcessBuffer(uintptr_t* buffer, size_t cur_offset, size_t thread_id) override {
    std::unordered_map<ArtMethod*, std::string> method_infos;
    TraceWriter* trace_writer = GetTraceWriter();
    if (trace_writer->GetTraceFormatVersion() == Trace::kFormatV1) {
      ScopedObjectAccess soa(Thread::Current());
      trace_writer->PreProcessTraceForMethodInfos(buffer, cur_offset, method_infos);
    }
    trace_writer->FlushBuffer(buffer, cur_offset, thread_id, method_infos);
  }
};

class MethodInfoWriterTask final : public TraceWriterTask {
 public:
  MethodInfoWriterTask(TraceWriter* trace_writer, int index, uintptr_t* buffer, size_t cur_offset)
      : TraceWriterTask(trace_writer, index, buffer, cur_offset, 0) {}

  void ProcessBuffer(uintptr_t* buffer,
                     size_t cur_offset,
                     [[maybe_unused]] size_t thread_id) override {
    GetTraceWriter()->WriteToFile(reinterpret_cast<uint8_t*>(buffer), cur_offset);
  }
};

std::vector<ArtMethod*>* Trace::AllocStackTrace() {
  return (temp_stack_trace_.get() != nullptr)  ? temp_stack_trace_.release() :
      new std::vector<ArtMethod*>();
}

void Trace::FreeStackTrace(std::vector<ArtMethod*>* stack_trace) {
  stack_trace->clear();
  temp_stack_trace_.reset(stack_trace);
}

static uint16_t GetTraceVersion(TraceClockSource clock_source, int version) {
  if (version == Trace::kFormatV1) {
    return (clock_source == TraceClockSource::kDual) ? kTraceVersionDualClock :
                                                       kTraceVersionSingleClock;
  } else {
    return (clock_source == TraceClockSource::kDual) ? kTraceVersionDualClockV2 :
                                                       kTraceVersionSingleClockV2;
  }
}

static uint16_t GetRecordSize(TraceClockSource clock_source, int version) {
  if (version == Trace::kFormatV1) {
    return (clock_source == TraceClockSource::kDual) ? kTraceRecordSizeDualClock :
                                                       kTraceRecordSizeSingleClock;
  } else {
    return (clock_source == TraceClockSource::kDual) ? kMaxTraceRecordSizeDualClockV2
                                                     : kMaxTraceRecordSizeSingleClockV2;
  }
}

static uint16_t GetNumEntries(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kNumEntriesForDualClock
                                                   : kNumEntriesForWallClock;
}

bool UseThreadCpuClock(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kThreadCpu) ||
         (clock_source == TraceClockSource::kDual);
}

bool UseWallClock(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kWall) || (clock_source == TraceClockSource::kDual);
}

bool UseFastTraceListeners(TraceClockSource clock_source) {
  // Thread cpu clocks needs a kernel call, so we don't directly support them in JITed code.
  bool is_fast_trace = !UseThreadCpuClock(clock_source);
#if defined(__arm__)
  // On ARM 32 bit, we don't always have access to the timestamp counters from
  // user space. See comment in TimestampCounter::GetTimestamp for more details.
  is_fast_trace = false;
#endif
  return is_fast_trace;
}

void Trace::MeasureClockOverhead() {
  if (UseThreadCpuClock(clock_source_)) {
    Thread::Current()->GetCpuNanoTime();
  }
  if (UseWallClock(clock_source_)) {
    TimestampCounter::GetTimestamp();
  }
}

// Compute an average time taken to measure clocks.
uint64_t Trace::GetClockOverheadNanoSeconds() {
  Thread* self = Thread::Current();
  uint64_t start = self->GetCpuNanoTime();

  const uint64_t numIter = 4000;
  for (int i = numIter; i > 0; i--) {
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
  }

  uint64_t elapsed_ns = self->GetCpuNanoTime() - start;
  return elapsed_ns / (numIter * 8);
}

static void GetSample(Thread* thread, void* arg) REQUIRES_SHARED(Locks::mutator_lock_) {
  std::vector<ArtMethod*>* const stack_trace = Trace::AllocStackTrace();
  StackVisitor::WalkStack(
      [&](const art::StackVisitor* stack_visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
        ArtMethod* m = stack_visitor->GetMethod();
        // Ignore runtime frames (in particular callee save).
        if (!m->IsRuntimeMethod()) {
          stack_trace->push_back(m);
        }
        return true;
      },
      thread,
      /* context= */ nullptr,
      art::StackVisitor::StackWalkKind::kIncludeInlinedFrames);
  Trace* the_trace = reinterpret_cast<Trace*>(arg);
  the_trace->CompareAndUpdateStackTrace(thread, stack_trace);
}

static void ClearThreadStackTraceAndClockBase(Thread* thread, [[maybe_unused]] void* arg) {
  thread->SetTraceClockBase(0);
  std::vector<ArtMethod*>* stack_trace = thread->GetStackTraceSample();
  thread->SetStackTraceSample(nullptr);
  delete stack_trace;
}

void Trace::CompareAndUpdateStackTrace(Thread* thread,
                                       std::vector<ArtMethod*>* stack_trace) {
  CHECK_EQ(pthread_self(), sampling_pthread_);
  std::vector<ArtMethod*>* old_stack_trace = thread->GetStackTraceSample();
  // Update the thread's stack trace sample.
  thread->SetStackTraceSample(stack_trace);
  // Read timer clocks to use for all events in this trace.
  uint64_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  if (old_stack_trace == nullptr) {
    // If there's no previous stack trace sample for this thread, log an entry event for all
    // methods in the trace.
    for (auto rit = stack_trace->rbegin(); rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
    }
  } else {
    // If there's a previous stack trace for this thread, diff the traces and emit entry and exit
    // events accordingly.
    auto old_rit = old_stack_trace->rbegin();
    auto rit = stack_trace->rbegin();
    // Iterate bottom-up over both traces until there's a difference between them.
    while (old_rit != old_stack_trace->rend() && rit != stack_trace->rend() && *old_rit == *rit) {
      old_rit++;
      rit++;
    }
    // Iterate top-down over the old trace until the point where they differ, emitting exit events.
    for (auto old_it = old_stack_trace->begin(); old_it != old_rit.base(); ++old_it) {
      LogMethodTraceEvent(thread, *old_it, kTraceMethodExit, thread_clock_diff, timestamp_counter);
    }
    // Iterate bottom-up over the new trace from the point where they differ, emitting entry events.
    for (; rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
    }
    FreeStackTrace(old_stack_trace);
  }
}

void* Trace::RunSamplingThread(void* arg) {
  Runtime* runtime = Runtime::Current();
  intptr_t interval_us = reinterpret_cast<intptr_t>(arg);
  CHECK_GE(interval_us, 0);
  CHECK(runtime->AttachCurrentThread("Sampling Profiler", true, runtime->GetSystemThreadGroup(),
                                     !runtime->IsAotCompiler()));

  while (true) {
    usleep(interval_us);
    ScopedTrace trace("Profile sampling");
    Thread* self = Thread::Current();
    Trace* the_trace;
    {
      MutexLock mu(self, *Locks::trace_lock_);
      the_trace = the_trace_;
      if (the_trace_->stop_tracing_) {
        break;
      }
    }
    {
      // Avoid a deadlock between a thread doing garbage collection
      // and the profile sampling thread, by blocking GC when sampling
      // thread stacks (see b/73624630).
      gc::ScopedGCCriticalSection gcs(self,
                                      art::gc::kGcCauseInstrumentation,
                                      art::gc::kCollectorTypeInstrumentation);
      ScopedSuspendAll ssa(__FUNCTION__);
      MutexLock mu(self, *Locks::thread_list_lock_);
      runtime->GetThreadList()->ForEach(GetSample, the_trace);
    }
  }

  runtime->DetachCurrentThread();
  return nullptr;
}

// Visitor used to record all methods currently loaded in the runtime. This is done at the start of
// method tracing.
class RecordMethodInfoClassVisitor : public ClassVisitor {
 public:
  explicit RecordMethodInfoClassVisitor(Trace* trace)
      : trace_(trace), offset_(0), buffer_(nullptr) {}

  bool operator()(ObjPtr<mirror::Class> klass) override REQUIRES(Locks::mutator_lock_) {
    // We use a buffer to aggregate method infos from different classes to avoid multiple small
    // writes to the file. The RecordMethodInfo handles the overflows by enqueueing a task to
    // flush the old buffer and allocates a new buffer.
    trace_->GetTraceWriter()->RecordMethodInfoV2(klass.Ptr(), &buffer_, &offset_);
    return true;  // Visit all classes.
  }

  void FlushBuffer() REQUIRES_SHARED(Locks::mutator_lock_) {
    // Flushes any data in the buffer to the file. Called at the end of visit to write any
    // remaining data to the file.
    trace_->GetTraceWriter()->AddMethodInfoWriteTask(
        buffer_, offset_, Thread::Current()->GetTid(), true);
  }

 private:
  Trace* const trace_;
  // Use a buffer to aggregate method infos of all classes to avoid multiple smaller writes to file.
  size_t offset_ = 0;
  uint8_t* buffer_ = nullptr;
};

void Trace::ClassPrepare([[maybe_unused]] Handle<mirror::Class> temp_klass,
                         Handle<mirror::Class> klass) {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  if (the_trace_ == nullptr) {
    return;
  }
  size_t offset = 0;
  size_t tid = Thread::Current()->GetTid();
  uint8_t* buffer = nullptr;
  // Write the method infos of the newly loaded class.
  the_trace_->GetTraceWriter()->RecordMethodInfoV2(klass.Get(), &buffer, &offset);
  the_trace_->GetTraceWriter()->AddMethodInfoWriteTask(buffer, offset, tid, true);
}

uint8_t* TraceWriter::AddMethodInfoWriteTask(uint8_t* buffer,
                                             size_t offset,
                                             size_t tid,
                                             bool release) {
  int old_index = GetMethodTraceIndex(reinterpret_cast<uintptr_t*>(buffer));
  uintptr_t* new_buf = nullptr;
  thread_pool_->AddTask(
      Thread::Current(),
      new MethodInfoWriterTask(this, old_index, reinterpret_cast<uintptr_t*>(buffer), offset));
  if (!release) {
    new_buf = AcquireTraceBuffer(tid);
  }
  return reinterpret_cast<uint8_t*>(new_buf);
}

void TraceWriter::WriteToFile(uint8_t* buffer, size_t offset) {
  MutexLock mu(Thread::Current(), trace_writer_lock_);
  if (!trace_file_->WriteFully(buffer, offset)) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
}

void TraceWriter::RecordMethodInfoV2(mirror::Class* klass, uint8_t** buffer, size_t* offset) {
  // For the v1 format, we record methods when we first execute them.
  DCHECK_EQ(trace_format_version_, Trace::kFormatV2);

  auto methods = klass->GetMethods(kRuntimePointerSize);
  if (methods.empty()) {
    return;
  }

  size_t tid = Thread::Current()->GetTid();
  size_t buffer_size = kPerThreadBufSize * sizeof(uintptr_t);
  size_t index = *offset;
  uint8_t* buf = *buffer;
  if (buf == nullptr) {
    buf = reinterpret_cast<uint8_t*>(AcquireTraceBuffer(tid));
  }

  std::string class_name_current = klass->PrettyDescriptor();
  const char* source_file_current = klass->GetSourceFile();
  if (source_file_current == nullptr) {
    // Generated classes have no source file.
    source_file_current = "";
  }
  for (ArtMethod& method : klass->GetMethods(kRuntimePointerSize)) {
    if (!method.IsInvokable()) {
      continue;
    }

    std::string class_name;
    const char* source_file;
    if (method.IsCopied()) {
      // For copied methods use method's declaring class which may not be the current class.
      class_name = method.GetDeclaringClass()->PrettyDescriptor();
      source_file = method.GetDeclaringClass()->GetSourceFile();
    } else {
      DCHECK(klass == method.GetDeclaringClass());
      class_name = class_name_current;
      source_file = source_file_current;
    }
    int class_name_len = class_name.length();
    int source_file_len = strlen(source_file);

    uint64_t method_id = reinterpret_cast<uint64_t>(&method);
    // TODO(mythria): Change how we report method infos in V2 to reduce the
    // repetition of the information about class and the source file.
    const char* name = method.GetName();
    int name_len = strlen(name);
    std::string signature = method.GetSignature().ToString();
    int signature_len = signature.length();
    // We need 3 tabs in between and a \n at the end and hence 4 additional characters.
    int method_info_length = class_name_len + name_len + signature_len + source_file_len + 4;
    // 1 byte header + 8 bytes method id + 2 bytes method_info_length
    int header_length = 11;
    if (index + header_length + method_info_length >= buffer_size) {
      buf = AddMethodInfoWriteTask(buf, index, tid, false);
      index = 0;
    }
    // Write the header to the buffer
    buf[index] = kMethodInfoHeaderV2;
    Append8LE(buf + index + 1, method_id);
    Append2LE(buf + index + 9, method_info_length);
    index += header_length;

    // Copy method line into the buffer
    memcpy(buf + index, class_name.c_str(), class_name_len);
    buf[index + class_name_len] = '\t';
    index += class_name_len + 1;
    memcpy(buf + index, name, name_len);
    buf[index + name_len] = '\t';
    index += name_len + 1;
    memcpy(buf + index, signature.c_str(), signature_len);
    buf[index + signature_len] = '\t';
    index += signature_len + 1;
    memcpy(buf + index, source_file, source_file_len);
    buf[index + source_file_len] = '\n';
    index += source_file_len + 1;
  }
  *offset = index;
  *buffer = buf;
}

void Trace::Start(const char* trace_filename,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  std::unique_ptr<File> file(OS::CreateEmptyFileWriteOnly(trace_filename));
  if (file == nullptr) {
    std::string msg = android::base::StringPrintf("Unable to open trace file '%s'", trace_filename);
    PLOG(ERROR) << msg;
    ScopedObjectAccess soa(Thread::Current());
    Thread::Current()->ThrowNewException("Ljava/lang/RuntimeException;", msg.c_str());
    return;
  }
  Start(std::move(file), buffer_size, flags, output_mode, trace_mode, interval_us);
}

void Trace::Start(int trace_fd,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  if (trace_fd < 0) {
    std::string msg = android::base::StringPrintf("Unable to start tracing with invalid fd %d",
                                                  trace_fd);
    LOG(ERROR) << msg;
    ScopedObjectAccess soa(Thread::Current());
    Thread::Current()->ThrowNewException("Ljava/lang/RuntimeException;", msg.c_str());
    return;
  }
  std::unique_ptr<File> file(new File(trace_fd, /* path= */ "tracefile", /* check_usage= */ true));
  Start(std::move(file), buffer_size, flags, output_mode, trace_mode, interval_us);
}

void Trace::StartDDMS(size_t buffer_size,
                      int flags,
                      TraceMode trace_mode,
                      int interval_us) {
  Start(std::unique_ptr<File>(),
        buffer_size,
        flags,
        TraceOutputMode::kDDMS,
        trace_mode,
        interval_us);
}

void Trace::Start(std::unique_ptr<File>&& trace_file_in,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  // We own trace_file now and are responsible for closing it. To account for error situations, use
  // a specialized unique_ptr to ensure we close it on the way out (if it hasn't been passed to a
  // Trace instance).
  auto deleter = [](File* file) {
    if (file != nullptr) {
      file->MarkUnchecked();  // Don't deal with flushing requirements.
      [[maybe_unused]] int result = file->Close();
      delete file;
    }
  };
  std::unique_ptr<File, decltype(deleter)> trace_file(trace_file_in.release(), deleter);

  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (the_trace_ != nullptr) {
      LOG(ERROR) << "Trace already in progress, ignoring this request";
      return;
    }
  }

  // Check interval if sampling is enabled
  if (trace_mode == TraceMode::kSampling && interval_us <= 0) {
    LOG(ERROR) << "Invalid sampling interval: " << interval_us;
    ScopedObjectAccess soa(self);
    ThrowRuntimeException("Invalid sampling interval: %d", interval_us);
    return;
  }

  // Initialize the frequency of timestamp counter updates here. This is needed
  // to get wallclock time from timestamp counter values.
  TimestampCounter::InitializeTimestampCounters();

  Runtime* runtime = Runtime::Current();

  // Enable count of allocs if specified in the flags.
  bool enable_stats = false;

  // Create Trace object.
  {
    // Suspend JIT here since we are switching runtime to debuggable. Debuggable runtimes cannot use
    // JITed code from before so we need to invalidated all JITed code here. Enter suspend JIT scope
    // to prevent any races with ongoing JIT compilations.
    jit::ScopedJitSuspend suspend_jit;
    // Required since EnableMethodTracing calls ConfigureStubs which visits class linker classes.
    gc::ScopedGCCriticalSection gcs(self,
                                    gc::kGcCauseInstrumentation,
                                    gc::kCollectorTypeInstrumentation);
    ScopedSuspendAll ssa(__FUNCTION__);
    MutexLock mu(self, *Locks::trace_lock_);
    if (TraceProfiler::IsTraceProfileInProgress()) {
      LOG(ERROR) << "On-demand profile in progress, ignoring this request";
      return;
    }

    if (Trace::IsTracingEnabledLocked()) {
      LOG(ERROR) << "Trace already in progress, ignoring this request";
      return;
    }

    enable_stats = (flags & kTraceCountAllocs) != 0;
    bool is_trace_format_v2 = GetTraceFormatVersionFromFlags(flags) == Trace::kFormatV2;
    the_trace_ = new Trace(trace_file.release(), buffer_size, flags, output_mode, trace_mode);
    num_trace_starts_++;
    if (is_trace_format_v2) {
      // Record all the methods that are currently loaded. We log all methods when any new class
      // is loaded. This will allow us to process the trace entries without requiring a mutator
      // lock.
      RecordMethodInfoClassVisitor visitor(the_trace_);
      runtime->GetClassLinker()->VisitClasses(&visitor);
      visitor.FlushBuffer();
    }
    if (trace_mode == TraceMode::kSampling) {
      CHECK_PTHREAD_CALL(pthread_create, (&sampling_pthread_, nullptr, &RunSamplingThread,
                                          reinterpret_cast<void*>(interval_us)),
                         "Sampling profiler thread");
      the_trace_->interval_us_ = interval_us;
    } else {
      if (!runtime->IsJavaDebuggable()) {
        art::jit::Jit* jit = runtime->GetJit();
        if (jit != nullptr) {
          jit->GetCodeCache()->InvalidateAllCompiledCode();
          jit->GetCodeCache()->TransitionToDebuggable();
          jit->GetJitCompiler()->SetDebuggableCompilerOption(true);
        }
        runtime->SetRuntimeDebugState(art::Runtime::RuntimeDebugState::kJavaDebuggable);
        runtime->GetInstrumentation()->UpdateEntrypointsForDebuggable();
        runtime->DeoptimizeBootImage();
      }
      if (is_trace_format_v2) {
        // Add ClassLoadCallback to record methods on class load.
        runtime->GetRuntimeCallbacks()->AddClassLoadCallback(the_trace_);
      }
      runtime->GetInstrumentation()->AddListener(
          the_trace_,
          instrumentation::Instrumentation::kMethodEntered |
              instrumentation::Instrumentation::kMethodExited |
              instrumentation::Instrumentation::kMethodUnwind,
          UseFastTraceListeners(the_trace_->GetClockSource()));
      runtime->GetInstrumentation()->EnableMethodTracing(kTracerInstrumentationKey,
                                                         the_trace_,
                                                         /*needs_interpreter=*/false);
    }
  }

  // Can't call this when holding the mutator lock.
  if (enable_stats) {
    runtime->SetStatsEnabled(true);
  }
}

void Trace::StopTracing(bool flush_entries) {
  Runtime* const runtime = Runtime::Current();
  Thread* const self = Thread::Current();

  pthread_t sampling_pthread = 0U;
  {
    MutexLock mu(self, *Locks::trace_lock_);
    num_trace_stops_initiated_++;
    if (the_trace_ == nullptr || the_trace_->stop_tracing_) {
      LOG(ERROR) << "Trace stop requested, but no trace currently running or trace is being"
                 << " stopped concurrently on another thread";
      return;
    }
    // Tell sampling_pthread_ to stop tracing.
    the_trace_->stop_tracing_ = true;
    sampling_pthread = sampling_pthread_;
  }

  // Make sure that we join before we delete the trace since we don't want to have
  // the sampling thread access a stale pointer. This finishes since the sampling thread exits when
  // the_trace_ is null.
  if (sampling_pthread != 0U) {
    CHECK_PTHREAD_CALL(pthread_join, (sampling_pthread, nullptr), "sampling thread shutdown");
  }

  // Wakeup any threads waiting for a buffer and abort allocating a buffer.
  the_trace_->trace_writer_->StopTracing();

  // Make a copy of the_trace_, so it can be flushed later. We want to reset
  // the_trace_ to nullptr in suspend all scope to prevent any races
  Trace* the_trace = the_trace_;
  bool stop_alloc_counting = (the_trace->flags_ & Trace::kTraceCountAllocs) != 0;
  // Stop the trace sources adding more entries to the trace buffer and synchronise stores.
  {
    gc::ScopedGCCriticalSection gcs(
        self, gc::kGcCauseInstrumentation, gc::kCollectorTypeInstrumentation);
    jit::ScopedJitSuspend suspend_jit;
    ScopedSuspendAll ssa(__FUNCTION__);

    if (the_trace->trace_mode_ == TraceMode::kSampling) {
      MutexLock mu(self, *Locks::thread_list_lock_);
      runtime->GetThreadList()->ForEach(ClearThreadStackTraceAndClockBase, nullptr);
    } else {
        runtime->GetRuntimeCallbacks()->RemoveClassLoadCallback(the_trace_);
        runtime->GetInstrumentation()->RemoveListener(
            the_trace,
            instrumentation::Instrumentation::kMethodEntered |
                instrumentation::Instrumentation::kMethodExited |
                instrumentation::Instrumentation::kMethodUnwind,
            UseFastTraceListeners(the_trace_->GetClockSource()));
        runtime->GetInstrumentation()->DisableMethodTracing(kTracerInstrumentationKey);
    }

    // Flush thread specific buffer from all threads before resetting the_trace_ to nullptr.
    // We also flush the buffer when destroying a thread which expects the_trace_ to be valid so
    // make sure that the per-thread buffer is reset before resetting the_trace_.
    {
      MutexLock mu(self, *Locks::trace_lock_);
      MutexLock tl_lock(Thread::Current(), *Locks::thread_list_lock_);
      // Flush the per-thread buffers and reset the trace inside the trace_lock_ to avoid any
      // race if the thread is detaching and trying to flush the buffer too. Since we hold the
      // trace_lock_ both here and when flushing on a thread detach only one of them will succeed
      // in actually flushing the buffer.
      for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
        if (thread->GetMethodTraceBuffer() != nullptr) {
          // We may have pending requests to flush the data. So just enqueue a
          // request to flush the current buffer so all the requests are
          // processed in order.
          the_trace->trace_writer_->FlushBuffer(
              thread, /* is_sync= */ false, /* free_buffer= */ true);
        }
      }
      the_trace_ = nullptr;
      sampling_pthread_ = 0U;
    }
  }

  // At this point, code may read buf_ as its writers are shutdown
  // and the ScopedSuspendAll above has ensured all stores to buf_
  // are now visible.
  the_trace->trace_writer_->FinishTracing(the_trace->flags_, flush_entries);
  delete the_trace;
  num_trace_stops_finished_++;

  if (stop_alloc_counting) {
    // Can be racy since SetStatsEnabled is not guarded by any locks.
    runtime->SetStatsEnabled(false);
  }
}

void Trace::RemoveListeners() {
  Thread* self = Thread::Current();
  // This is expected to be called in SuspendAll scope.
  DCHECK(Locks::mutator_lock_->IsExclusiveHeld(self));
  MutexLock mu(self, *Locks::trace_lock_);
  Runtime* runtime = Runtime::Current();
  runtime->GetRuntimeCallbacks()->RemoveClassLoadCallback(the_trace_);
  runtime->GetInstrumentation()->RemoveListener(
      the_trace_,
      instrumentation::Instrumentation::kMethodEntered |
      instrumentation::Instrumentation::kMethodExited |
      instrumentation::Instrumentation::kMethodUnwind,
      UseFastTraceListeners(the_trace_->GetClockSource()));
}

void Trace::FlushThreadBuffer(Thread* self) {
  MutexLock mu(self, *Locks::trace_lock_);
  // Check if we still need to flush inside the trace_lock_. If we are stopping tracing it is
  // possible we already deleted the trace and flushed the buffer too.
  if (the_trace_ == nullptr) {
    if (art_flags::always_enable_profile_code()) {
      TraceProfiler::ReleaseThreadBuffer(self);
    }
    DCHECK_EQ(self->GetMethodTraceBuffer(), nullptr);
    return;
  }
  the_trace_->trace_writer_->FlushBuffer(self, /* is_sync= */ false, /* free_buffer= */ true);
}

void Trace::ReleaseThreadBuffer(Thread* self) {
  MutexLock mu(self, *Locks::trace_lock_);
  // Check if we still need to flush inside the trace_lock_. If we are stopping tracing it is
  // possible we already deleted the trace and flushed the buffer too.
  if (the_trace_ == nullptr) {
    if (art_flags::always_enable_profile_code()) {
      TraceProfiler::ReleaseThreadBuffer(self);
    }
    DCHECK_EQ(self->GetMethodTraceBuffer(), nullptr);
    return;
  }
  the_trace_->trace_writer_->ReleaseBufferForThread(self);
  self->SetMethodTraceBuffer(nullptr, 0);
}

void Trace::Abort() {
  // Do not write anything anymore.
  StopTracing(/* flush_entries= */ false);
}

void Trace::Stop() {
  // Finish writing.
  StopTracing(/* flush_entries= */ true);
}

void Trace::Shutdown() {
  if (GetMethodTracingMode() != kTracingInactive) {
    Stop();
  }
}

TracingMode Trace::GetMethodTracingMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  if (the_trace_ == nullptr) {
    return kTracingInactive;
  } else {
    switch (the_trace_->trace_mode_) {
      case TraceMode::kSampling:
        return kSampleProfilingActive;
      case TraceMode::kMethodTracing:
        return kMethodTracingActive;
    }
    LOG(FATAL) << "Unreachable";
    UNREACHABLE();
  }
}

TraceWriter::TraceWriter(File* trace_file,
                         TraceOutputMode output_mode,
                         TraceClockSource clock_source,
                         size_t buffer_size,
                         int num_trace_buffers,
                         int trace_format_version,
                         uint64_t clock_overhead_ns)
    : trace_file_(trace_file),
      trace_output_mode_(output_mode),
      clock_source_(clock_source),
      buf_(new uint8_t[std::max(kMinBufSize, buffer_size)]()),
      buffer_size_(std::max(kMinBufSize, buffer_size)),
      trace_format_version_(trace_format_version),
      start_time_(TimestampCounter::GetNanoTime(TimestampCounter::GetTimestamp())),
      overflow_(false),
      num_records_(0),
      clock_overhead_ns_(clock_overhead_ns),
      owner_tids_(num_trace_buffers),
      buffer_pool_lock_("tracing buffer pool lock", kDefaultMutexLevel),
      buffer_available_("buffer available condition", buffer_pool_lock_),
      num_waiters_zero_cond_("Num waiters zero", buffer_pool_lock_),
      num_waiters_for_buffer_(0),
      trace_writer_lock_("trace writer lock", LockLevel::kTracingStreamingLock) {
  // We initialize the start_time_ from the timestamp counter. This may not match
  // with the monotonic timer but we only use this time to calculate the elapsed
  // time from this point which should be the same for both cases.
  // We record monotonic time at the start of the trace, because Android Studio
  // fetches the monotonic timer from other places and matches these times to
  // construct a cpu profile. See b/318052824 for more context.
  uint64_t start_time_monotonic =
      start_time_ + (NanoTime() - TimestampCounter::GetNanoTime(TimestampCounter::GetTimestamp()));
  uint16_t trace_version = GetTraceVersion(clock_source_, trace_format_version_);
  if (output_mode == TraceOutputMode::kStreaming) {
    trace_version |= 0xF0U;
  }

  // Set up the beginning of the trace.
  if (trace_format_version_ == Trace::kFormatV1) {
    memset(buf_.get(), 0, kTraceHeaderLength);
    Append4LE(buf_.get(), kTraceMagicValue);
    Append2LE(buf_.get() + 4, trace_version);
    Append2LE(buf_.get() + 6, kTraceHeaderLength);
    // Use microsecond precision for V1 format.
    Append8LE(buf_.get() + 8, (start_time_monotonic / 1000));
    if (trace_version >= kTraceVersionDualClock) {
      uint16_t record_size = GetRecordSize(clock_source_, trace_format_version_);
      Append2LE(buf_.get() + 16, record_size);
    }
    static_assert(18 <= kMinBufSize, "Minimum buffer size not large enough for trace header");

    cur_offset_ = kTraceHeaderLength;
  } else {
    memset(buf_.get(), 0, kTraceHeaderLengthV2);
    Append4LE(buf_.get(), kTraceMagicValue);
    Append2LE(buf_.get() + 4, trace_version);
    Append8LE(buf_.get() + 6, start_time_monotonic);
    cur_offset_ = kTraceHeaderLengthV2;
  }

  if (output_mode == TraceOutputMode::kStreaming || trace_format_version_ == Trace::kFormatV2) {
    // Flush the header information to the file. We use a per thread buffer, so
    // it is easier to just write the header information directly to file.
    if (!trace_file_->WriteFully(buf_.get(), kTraceHeaderLength)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
    cur_offset_ = 0;
  }
  // Thread index of 0 is a special identifier used to distinguish between trace
  // event entries and thread / method info entries.
  current_thread_index_ = 1;

  // Don't create threadpool for a zygote. This would cause slowdown when forking because we need
  // to stop and start this thread pool. Method tracing on zygote isn't a frequent use case and
  // it is okay to flush on the main thread in such cases.
  if (!Runtime::Current()->IsZygote()) {
    thread_pool_.reset(TraceWriterThreadPool::Create("Trace writer pool"));
    thread_pool_->StartWorkers(Thread::Current());
  }

  // Initialize the pool of per-thread buffers.
  InitializeTraceBuffers();
}

Trace::Trace(File* trace_file,
             size_t buffer_size,
             int flags,
             TraceOutputMode output_mode,
             TraceMode trace_mode)
    : flags_(flags),
      trace_mode_(trace_mode),
      clock_source_(GetClockSourceFromFlags(flags)),
      interval_us_(0),
      stop_tracing_(false) {
  CHECK_IMPLIES(trace_file == nullptr, output_mode == TraceOutputMode::kDDMS);

  int trace_format_version = GetTraceFormatVersionFromFlags(flags_);
  // In streaming mode, we only need a buffer big enough to store data per each
  // thread buffer. In non-streaming mode this is specified by the user and we
  // stop tracing when the buffer is full.
  size_t buf_size = (output_mode == TraceOutputMode::kStreaming) ?
                        kPerThreadBufSize * kScalingFactorEncodedEntries :
                        buffer_size;
  trace_writer_.reset(new TraceWriter(trace_file,
                                      output_mode,
                                      clock_source_,
                                      buf_size,
                                      kNumTracePoolBuffers,
                                      trace_format_version,
                                      GetClockOverheadNanoSeconds()));
}

std::string TraceWriter::CreateSummary(int flags) {
  std::ostringstream os;
  // Compute elapsed time.
  uint64_t elapsed = TimestampCounter::GetNanoTime(TimestampCounter::GetTimestamp()) - start_time_;
  os << StringPrintf("%cversion\n", kTraceTokenChar);
  os << StringPrintf("%d\n", GetTraceVersion(clock_source_, trace_format_version_));
  os << StringPrintf("data-file-overflow=%s\n", overflow_ ? "true" : "false");
  if (UseThreadCpuClock(clock_source_)) {
    if (UseWallClock(clock_source_)) {
      os << StringPrintf("clock=dual\n");
    } else {
      os << StringPrintf("clock=thread-cpu\n");
    }
  } else {
    os << StringPrintf("clock=wall\n");
  }
  if (trace_format_version_ == Trace::kFormatV1) {
    os << StringPrintf("elapsed-time-usec=%" PRIu64 "\n", elapsed / 1000);
  } else {
    os << StringPrintf("elapsed-time-nsec=%" PRIu64 "\n", elapsed);
  }
  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    os << StringPrintf("num-method-calls=%zd\n", num_records_);
  }
  os << StringPrintf("clock-call-overhead-nsec=%" PRIu64 "\n", clock_overhead_ns_);
  os << StringPrintf("vm=art\n");
  os << StringPrintf("pid=%d\n", getpid());
  if ((flags & Trace::kTraceCountAllocs) != 0) {
    os << "alloc-count=" << Runtime::Current()->GetStat(KIND_ALLOCATED_OBJECTS) << "\n";
    os << "alloc-size=" << Runtime::Current()->GetStat(KIND_ALLOCATED_BYTES) << "\n";
    os << "gc-count=" << Runtime::Current()->GetStat(KIND_GC_INVOCATIONS) << "\n";
  }

  if (trace_format_version_ == Trace::kFormatV1) {
    os << StringPrintf("%cthreads\n", kTraceTokenChar);
    DumpThreadList(os);
    os << StringPrintf("%cmethods\n", kTraceTokenChar);
    DumpMethodList(os);
  }
  os << StringPrintf("%cend\n", kTraceTokenChar);
  return os.str();
}

void TraceWriter::FinishTracing(int flags, bool flush_entries) {
  Thread* self = Thread::Current();

  if (!flush_entries) {
    // This is only called from the child process post fork to abort the trace.
    // We shouldn't have any workers in the thread pool here.
    DCHECK_EQ(thread_pool_, nullptr);
    trace_file_->MarkUnchecked();  // Do not trigger guard.
    if (trace_file_->Close() != 0) {
      PLOG(ERROR) << "Could not close trace file.";
    }
    return;
  }

  if (thread_pool_ != nullptr) {
    // Wait for any workers to be created. If we are stopping tracing as a part of runtime
    // shutdown, any unstarted workers can create problems if they try attaching while shutting
    // down.
    thread_pool_->WaitForWorkersToBeCreated();
    // Wait for any outstanding writer tasks to finish. Let the thread pool worker finish the
    // tasks to avoid any re-ordering when processing tasks.
    thread_pool_->Wait(self, /* do_work= */ false, /* may_hold_locks= */ true);
    DCHECK_EQ(thread_pool_->GetTaskCount(self), 0u);
    thread_pool_->StopWorkers(self);
  }

  size_t final_offset = 0;
  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    MutexLock mu(Thread::Current(), trace_writer_lock_);
    final_offset = cur_offset_;
  }

  std::string summary = CreateSummary(flags);
  if (trace_format_version_ == Trace::kFormatV1) {
    if (trace_output_mode_ == TraceOutputMode::kStreaming) {
      DCHECK_NE(trace_file_.get(), nullptr);
      // It is expected that this method is called when all other threads are suspended, so there
      // cannot be any writes to trace_file_ after finish tracing.
      // Write a special token to mark the end of trace records and the start of
      // trace summary.
      uint8_t buf[7];
      Append2LE(buf, 0);
      buf[2] = kOpTraceSummary;
      Append4LE(buf + 3, static_cast<uint32_t>(summary.length()));
      // Write the trace summary. The summary is identical to the file header when
      // the output mode is not streaming (except for methods).
      if (!trace_file_->WriteFully(buf, sizeof(buf)) ||
          !trace_file_->WriteFully(summary.c_str(), summary.length())) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
    } else if (trace_output_mode_ == TraceOutputMode::kFile) {
      DCHECK_NE(trace_file_.get(), nullptr);
      if (!trace_file_->WriteFully(summary.c_str(), summary.length()) ||
          !trace_file_->WriteFully(buf_.get(), final_offset)) {
        std::string detail(StringPrintf("Trace data write failed: %s", strerror(errno)));
        PLOG(ERROR) << detail;
        ThrowRuntimeException("%s", detail.c_str());
      }
    } else {
      DCHECK_EQ(trace_file_.get(), nullptr);
      DCHECK(trace_output_mode_ == TraceOutputMode::kDDMS);
      std::vector<uint8_t> data;
      data.resize(summary.length() + final_offset);
      memcpy(data.data(), summary.c_str(), summary.length());
      memcpy(data.data() + summary.length(), buf_.get(), final_offset);
      Runtime::Current()->GetRuntimeCallbacks()->DdmPublishChunk(CHUNK_TYPE("MPSE"),
                                                                 ArrayRef<const uint8_t>(data));
    }
  } else {
    DCHECK(trace_format_version_ == Trace::kFormatV2);
    DCHECK(trace_output_mode_ != TraceOutputMode::kDDMS);

    if (trace_output_mode_ == TraceOutputMode::kFile) {
      if (!trace_file_->WriteFully(buf_.get(), final_offset)) {
        PLOG(WARNING) << "Failed to write trace output";
      }
    }

    // Write the summary packet
    uint8_t buf[3];
    buf[0] = kSummaryHeaderV2;
    Append2LE(buf + 1, static_cast<uint32_t>(summary.length()));
    // Write the trace summary. Reports information about tracing mode, number of records and
    // clock overhead in plain text format.
    if (!trace_file_->WriteFully(buf, sizeof(buf)) ||
        !trace_file_->WriteFully(summary.c_str(), summary.length())) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  }

  if (trace_file_.get() != nullptr) {
    // Do not try to erase, so flush and close explicitly.
    if (trace_file_->Flush() != 0) {
      PLOG(WARNING) << "Could not flush trace file.";
    }
    if (trace_file_->Close() != 0) {
      PLOG(ERROR) << "Could not close trace file.";
    }
  }
}

void Trace::DexPcMoved([[maybe_unused]] Thread* thread,
                       [[maybe_unused]] Handle<mirror::Object> this_object,
                       ArtMethod* method,
                       uint32_t new_dex_pc) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected dex PC event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << new_dex_pc;
}

void Trace::FieldRead([[maybe_unused]] Thread* thread,
                      [[maybe_unused]] Handle<mirror::Object> this_object,
                      ArtMethod* method,
                      uint32_t dex_pc,
                      [[maybe_unused]] ArtField* field) REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field read event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::FieldWritten([[maybe_unused]] Thread* thread,
                         [[maybe_unused]] Handle<mirror::Object> this_object,
                         ArtMethod* method,
                         uint32_t dex_pc,
                         [[maybe_unused]] ArtField* field,
                         [[maybe_unused]] const JValue& field_value)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field write event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::MethodEntered(Thread* thread, ArtMethod* method) {
  uint64_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
}

void Trace::MethodExited(Thread* thread,
                         ArtMethod* method,
                         [[maybe_unused]] instrumentation::OptionalFrame frame,
                         [[maybe_unused]] JValue& return_value) {
  uint64_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceMethodExit, thread_clock_diff, timestamp_counter);
}

void Trace::MethodUnwind(Thread* thread, ArtMethod* method, [[maybe_unused]] uint32_t dex_pc) {
  uint64_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceUnroll, thread_clock_diff, timestamp_counter);
}

void Trace::ExceptionThrown([[maybe_unused]] Thread* thread,
                            [[maybe_unused]] Handle<mirror::Throwable> exception_object)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::ExceptionHandled([[maybe_unused]] Thread* thread,
                             [[maybe_unused]] Handle<mirror::Throwable> exception_object)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::Branch(Thread* /*thread*/, ArtMethod* method,
                   uint32_t /*dex_pc*/, int32_t /*dex_pc_offset*/)
      REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected branch event in tracing" << ArtMethod::PrettyMethod(method);
}

void Trace::WatchedFramePop([[maybe_unused]] Thread* self,
                            [[maybe_unused]] const ShadowFrame& frame) {
  LOG(ERROR) << "Unexpected WatchedFramePop event in tracing";
}

void Trace::ReadClocks(Thread* thread, uint64_t* thread_clock_diff, uint64_t* timestamp_counter) {
  if (UseThreadCpuClock(clock_source_)) {
    uint64_t clock_base = thread->GetTraceClockBase();
    if (UNLIKELY(clock_base == 0)) {
      // First event, record the base time in the map.
      uint64_t time = thread->GetCpuNanoTime();
      thread->SetTraceClockBase(time);
    } else {
      *thread_clock_diff = thread->GetCpuNanoTime() - clock_base;
    }
  }
  if (UseWallClock(clock_source_)) {
    *timestamp_counter = TimestampCounter::GetTimestamp();
  }
}

std::string TraceWriter::GetMethodLine(const std::string& method_line, uint32_t method_index) {
  return StringPrintf("%#x\t%s", (method_index << TraceActionBits), method_line.c_str());
}

void TraceWriter::RecordThreadInfo(Thread* thread) {
  // This is the first event from this thread, so first record information about the thread.
  std::string thread_name;
  thread->GetThreadName(thread_name);

  // In tests, we destroy VM after already detaching the current thread. We re-attach the current
  // thread again as a "Shutdown thread" during the process of shutting down. So don't record
  // information about shutdown threads since it overwrites the actual thread_name.
  if (thread_name.compare("Shutdown thread") == 0) {
    return;
  }

  MutexLock mu(Thread::Current(), trace_writer_lock_);
  if (trace_format_version_ == Trace::kFormatV1 &&
      trace_output_mode_ != TraceOutputMode::kStreaming) {
    threads_list_.Overwrite(GetThreadEncoding(thread->GetTid()), thread_name);
    return;
  }

  static constexpr size_t kThreadNameHeaderSize = 7;
  uint8_t header[kThreadNameHeaderSize];
  if (trace_format_version_ == Trace::kFormatV1) {
    Append2LE(header, 0);
    header[2] = kOpNewThread;
    Append2LE(header + 3, GetThreadEncoding(thread->GetTid()));
  } else {
    header[0] = kThreadInfoHeaderV2;
    Append4LE(header + 1, thread->GetTid());
  }
  DCHECK(thread_name.length() < (1 << 16));
  Append2LE(header + 5, static_cast<uint16_t>(thread_name.length()));

  if (!trace_file_->WriteFully(header, kThreadNameHeaderSize) ||
      !trace_file_->WriteFully(reinterpret_cast<const uint8_t*>(thread_name.c_str()),
                               thread_name.length())) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
}

void TraceWriter::PreProcessTraceForMethodInfos(
    uintptr_t* method_trace_entries,
    size_t current_offset,
    std::unordered_map<ArtMethod*, std::string>& method_infos) {
  // Compute the method infos before we process the entries. We don't want to assign an encoding
  // for the method here. The expectation is that once we assign a method id we write it to the
  // file before any other thread can see the method id. So we should assign method encoding while
  // holding the trace_writer_lock_ and not release it till we flush the method info to the file. We
  // don't want to flush entries to file while holding the mutator lock. We need the mutator lock to
  // get method info. So we just precompute method infos without assigning a method encoding here.
  // There may be a race and multiple threads computing the method info but only one of them would
  // actually put into the method_id_map_.
  MutexLock mu(Thread::Current(), trace_writer_lock_);
  size_t num_entries = GetNumEntries(clock_source_);
  DCHECK_EQ((kPerThreadBufSize - current_offset) % num_entries, 0u);
  for (size_t entry_index = kPerThreadBufSize; entry_index != current_offset;) {
    entry_index -= num_entries;
    uintptr_t method_and_action = method_trace_entries[entry_index];
    ArtMethod* method = reinterpret_cast<ArtMethod*>(method_and_action & kMaskTraceAction);
    if (!HasMethodEncoding(method) && method_infos.find(method) == method_infos.end()) {
      method_infos.emplace(method, GetMethodInfoLine(method));
    }
  }
}

void TraceWriter::RecordMethodInfoV1(const std::string& method_info_line, uint64_t method_id) {
  // Write a special block with the name.
  std::string method_line;
  size_t header_size;
  static constexpr size_t kMethodNameHeaderSize = 5;
  DCHECK_LT(kMethodNameHeaderSize, kPerThreadBufSize);
  uint8_t method_header[kMethodNameHeaderSize];
  uint16_t method_line_length = static_cast<uint16_t>(method_line.length());
  DCHECK(method_line.length() < (1 << 16));
  // Write a special block with the name.
  Append2LE(method_header, 0);
  method_header[2] = kOpNewMethod;
  method_line = GetMethodLine(method_info_line, method_id);
  method_line_length = static_cast<uint16_t>(method_line.length());
  Append2LE(method_header + 3, method_line_length);
  header_size = kMethodNameHeaderSize;

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(method_line.c_str());
  if (!trace_file_->WriteFully(method_header, header_size) ||
      !trace_file_->WriteFully(ptr, method_line_length)) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
}

void TraceWriter::FlushAllThreadBuffers() {
  ScopedThreadStateChange stsc(Thread::Current(), ThreadState::kSuspended);
  ScopedSuspendAll ssa(__FUNCTION__);
  {
    MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
    for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
      if (thread->GetMethodTraceBuffer() != nullptr) {
        FlushBuffer(thread, /* is_sync= */ true, /* free_buffer= */ false);
        // We cannot flush anynore data, so just break.
        if (overflow_) {
          break;
        }
      }
    }
  }
  Trace::RemoveListeners();
  return;
}

uintptr_t* TraceWriter::PrepareBufferForNewEntries(Thread* thread) {
  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // In streaming mode, just flush the per-thread buffer and reuse the
    // existing buffer for new entries.
    FlushBuffer(thread, /* is_sync= */ false, /* free_buffer= */ false);
    DCHECK_EQ(overflow_, false);
  } else {
    // For non-streaming mode, flush all the threads to check if we have space in the common
    // buffer to record any future events.
    FlushAllThreadBuffers();
  }
  if (overflow_) {
    return nullptr;
  }
  return thread->GetMethodTraceBuffer();
}

void TraceWriter::InitializeTraceBuffers() {
  for (size_t i = 0; i < owner_tids_.size(); i++) {
    owner_tids_[i].store(0);
  }

  trace_buffer_.reset(new uintptr_t[kPerThreadBufSize * owner_tids_.size()]);
  CHECK(trace_buffer_.get() != nullptr);
}

uintptr_t* TraceWriter::AcquireTraceBuffer(size_t tid) {
  Thread* self = Thread::Current();

  // Fast path, check if there is a free buffer in the pool
  for (size_t index = 0; index < owner_tids_.size(); index++) {
    size_t owner = 0;
    if (owner_tids_[index].compare_exchange_strong(owner, tid)) {
      return trace_buffer_.get() + index * kPerThreadBufSize;
    }
  }

  // Increment a counter so we know how many threads are potentially suspended in the tracing code.
  // We need this when stopping tracing. We need to wait for all these threads to finish executing
  // this code so we can safely delete the trace related data.
  num_waiters_for_buffer_.fetch_add(1);

  uintptr_t* buffer = nullptr;
  // If finish_tracing_ is set to true we shouldn't suspend ourselves. So check for finish_tracing_
  // before the thread suspension. As an example, consider the following:
  // T2 is looking for a free buffer in the loop above
  // T1 calls stop tracing -> Sets finish_tracing_ to true -> Checks that there are no waiters ->
  // Waiting to suspend all threads.
  // T2 doesn't find a buffer.
  // If T2 suspends before checking for finish_tracing_ there is a possibility T1 succeeds entering
  // SuspendAllScope while thread T2 is still in the TraceWriter code.
  // To avoid this, we increment the num_waiters_for_buffer and then check for finish_tracing
  // before suspending the thread. StopTracing sets finish_tracing_ to true first and then checks
  // for num_waiters_for_buffer. Both these are atomic variables and we use sequential consistency
  // (acquire for load and release for stores), so all threads see the updates for these variables
  // in the same order. That ensures we don't suspend in the tracing logic after Trace::StopTracing
  // has returned. This is required so that we can safely delete tracing data.
  if (self->IsThreadSuspensionAllowable() && !finish_tracing_.load()) {
    ScopedThreadSuspension sts(self, ThreadState::kSuspended);
    while (1) {
      MutexLock mu(self, buffer_pool_lock_);
      // Tracing is being stopped, so don't wait for a free buffer. Just return early.
      if (finish_tracing_.load()) {
        break;
      }

      // Check if there's a free buffer in the pool
      for (size_t index = 0; index < owner_tids_.size(); index++) {
        size_t owner = 0;
        if (owner_tids_[index].compare_exchange_strong(owner, tid)) {
          buffer = trace_buffer_.get() + index * kPerThreadBufSize;
          break;
        }
      }

      // Found a buffer
      if (buffer != nullptr) {
        break;
      }

      if (thread_pool_ == nullptr ||
          (thread_pool_->GetTaskCount(self) < num_waiters_for_buffer_.load())) {
        // We have fewer buffers than active threads, just allocate a new one.
        break;
      }

      buffer_available_.WaitHoldingLocks(self);
    }
  }

  // The thread is no longer in the suspend scope, so decrement the counter.
  num_waiters_for_buffer_.fetch_sub(1);
  if (num_waiters_for_buffer_.load() == 0 && finish_tracing_.load()) {
    MutexLock mu(self, buffer_pool_lock_);
    num_waiters_zero_cond_.Broadcast(self);
  }

  if (buffer == nullptr) {
    // Allocate a new buffer. We either don't want to wait or have too few buffers.
    buffer = new uintptr_t[kPerThreadBufSize];
    CHECK(buffer != nullptr);
  }
  return buffer;
}

void TraceWriter::StopTracing() {
  Thread* self = Thread::Current();
  MutexLock mu(self, buffer_pool_lock_);
  finish_tracing_.store(true);
  while (num_waiters_for_buffer_.load() != 0) {
    buffer_available_.Broadcast(self);
    num_waiters_zero_cond_.WaitHoldingLocks(self);
  }
}

void TraceWriter::ReleaseBuffer(int index) {
  // Only the trace_writer_ thread can release the buffer.
  MutexLock mu(Thread::Current(), buffer_pool_lock_);
  if (index != -1) {
    owner_tids_[index].store(0);
  }
  buffer_available_.Signal(Thread::Current());
}

void TraceWriter::ReleaseBufferForThread(Thread* self) {
  uintptr_t* buffer = self->GetMethodTraceBuffer();
  int index = GetMethodTraceIndex(buffer);
  if (index == -1) {
    delete[] buffer;
  } else {
    ReleaseBuffer(index);
  }
}

int TraceWriter::GetMethodTraceIndex(uintptr_t* current_buffer) {
  if (current_buffer < trace_buffer_.get() ||
      current_buffer > trace_buffer_.get() + (owner_tids_.size() - 1) * kPerThreadBufSize) {
    // This was the temporary buffer we allocated.
    return -1;
  }
  return (current_buffer - trace_buffer_.get()) / kPerThreadBufSize;
}

void TraceWriter::FlushBuffer(Thread* thread, bool is_sync, bool release) {
  uintptr_t* method_trace_entries = thread->GetMethodTraceBuffer();
  uintptr_t** current_entry_ptr = thread->GetTraceBufferCurrEntryPtr();
  size_t current_offset = *current_entry_ptr - method_trace_entries;
  size_t tid = thread->GetTid();
  DCHECK(method_trace_entries != nullptr);

  if (is_sync || thread_pool_ == nullptr) {
    std::unordered_map<ArtMethod*, std::string> method_infos;
    if (trace_format_version_ == Trace::kFormatV1) {
      PreProcessTraceForMethodInfos(method_trace_entries, current_offset, method_infos);
    }
    FlushBuffer(method_trace_entries, current_offset, tid, method_infos);

    // This is a synchronous flush, so no need to allocate a new buffer. This is used either
    // when the tracing has finished or in non-streaming mode.
    // Just reset the buffer pointer to the initial value, so we can reuse the same buffer.
    if (release) {
      thread->SetMethodTraceBuffer(nullptr, 0);
    } else {
      thread->SetMethodTraceBufferCurrentEntry(kPerThreadBufSize);
    }
  } else {
    int old_index = GetMethodTraceIndex(method_trace_entries);
    // The TraceWriterTask takes the ownership of the buffer and releases the buffer once the
    // entries are flushed.
    thread_pool_->AddTask(
        Thread::Current(),
        new TraceEntriesWriterTask(this, old_index, method_trace_entries, current_offset, tid));
    if (release) {
      thread->SetMethodTraceBuffer(nullptr, 0);
    } else {
      thread->SetMethodTraceBuffer(AcquireTraceBuffer(tid), kPerThreadBufSize);
    }
  }

  return;
}

void TraceWriter::ReadValuesFromRecord(uintptr_t* method_trace_entries,
                                       size_t record_index,
                                       MethodTraceRecord& record,
                                       bool has_thread_cpu_clock,
                                       bool has_wall_clock) {
  uintptr_t method_and_action = method_trace_entries[record_index++];
  record.method = reinterpret_cast<ArtMethod*>(method_and_action & kMaskTraceAction);
  CHECK(record.method != nullptr);
  record.action = DecodeTraceAction(method_and_action);

  record.thread_cpu_time = 0;
  record.wall_clock_time = 0;
  if (has_thread_cpu_clock) {
    record.thread_cpu_time = method_trace_entries[record_index++];
    if (art::kRuntimePointerSize == PointerSize::k32) {
      // On 32-bit architectures threadcputime is stored as two 32-bit values.
      uint64_t high_bits = method_trace_entries[record_index++];
      record.thread_cpu_time = (high_bits << 32 | record.thread_cpu_time);
    }
  }
  if (has_wall_clock) {
    uint64_t timestamp = method_trace_entries[record_index++];
    if (art::kRuntimePointerSize == PointerSize::k32) {
      // On 32-bit architectures timestamp is stored as two 32-bit values.
      uint64_t high_timestamp = method_trace_entries[record_index++];
      timestamp = (high_timestamp << 32 | timestamp);
    }
    record.wall_clock_time = TimestampCounter::GetNanoTime(timestamp) - start_time_;
  }
}

size_t TraceWriter::FlushEntriesFormatV1(
    uintptr_t* method_trace_entries,
    size_t tid,
    const std::unordered_map<ArtMethod*, std::string>& method_infos,
    size_t end_offset,
    size_t num_records) {
  size_t buffer_index = 0;
  uint8_t* buffer_ptr = buf_.get();

  const size_t record_size = GetRecordSize(clock_source_, trace_format_version_);
  DCHECK_LT(record_size, kPerThreadBufSize);
  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    // In non-streaming mode we only flush to file at the end, so retain the earlier data. If the
    // buffer is full we don't process any more entries.
    buffer_index = cur_offset_;

    // Check if there is sufficient space in the buffer for non-streaming case. If not return early.
    // In FormatV1, the encoding of events is fixed size, so we can determine the amount of buffer
    // space required.
    if (cur_offset_ + record_size * num_records >= buffer_size_) {
      overflow_ = true;
      return 0;
    }
  }

  uint16_t thread_id = GetThreadEncoding(tid);
  bool has_thread_cpu_clock = UseThreadCpuClock(clock_source_);
  bool has_wall_clock = UseWallClock(clock_source_);
  size_t num_entries = GetNumEntries(clock_source_);

  for (size_t entry_index = kPerThreadBufSize; entry_index != end_offset;) {
    entry_index -= num_entries;

    MethodTraceRecord record;
    ReadValuesFromRecord(
        method_trace_entries, entry_index, record, has_thread_cpu_clock, has_wall_clock);

    auto [method_id, is_new_method] = GetMethodEncoding(record.method);
    if (is_new_method && trace_output_mode_ == TraceOutputMode::kStreaming) {
      RecordMethodInfoV1(method_infos.find(record.method)->second, method_id);
    }

    DCHECK_LT(buffer_index + record_size, buffer_size_);
    EncodeEventEntry(buffer_ptr + buffer_index,
                     thread_id,
                     method_id,
                     record.action,
                     record.thread_cpu_time,
                     record.wall_clock_time);
    buffer_index += record_size;
  }

  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // Flush the contents of buffer to file.
    if (!trace_file_->WriteFully(buffer_ptr, buffer_index)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  } else {
    // In non-streaming mode, we keep the data in the buffer and write to the
    // file when tracing has stopped. Just update the offset of the buffer.
    cur_offset_ = buffer_index;
  }
  return num_records;
}

size_t TraceWriter::FlushEntriesFormatV2(uintptr_t* method_trace_entries,
                                         size_t tid,
                                         size_t num_records) {
  uint8_t* init_buffer_ptr = buf_.get();
  uint8_t* end_buffer_ptr = buf_.get() + buffer_size_;

  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    // In non-streaming mode we only flush to file at the end, so retain the earlier data. If the
    // buffer is full we don't process any more entries.
    init_buffer_ptr = buf_.get() + cur_offset_;
  }

  uint8_t* current_buffer_ptr = init_buffer_ptr;
  bool has_thread_cpu_clock = UseThreadCpuClock(clock_source_);
  bool has_wall_clock = UseWallClock(clock_source_);
  size_t num_entries = GetNumEntries(clock_source_);
  uint64_t prev_wall_timestamp = 0;
  uint64_t prev_thread_timestamp = 0;
  uint64_t prev_method_action_encoding = 0;
  size_t entry_index = kPerThreadBufSize;
  size_t curr_record_index = 0;
  const int max_record_size = GetRecordSize(clock_source_, trace_format_version_);

  while (curr_record_index < num_records) {
    current_buffer_ptr = init_buffer_ptr + kEntryHeaderSizeV2;
    for (; curr_record_index < num_records; curr_record_index++) {
      // Don't process more entries if the buffer doesn't have sufficient space.
      if (end_buffer_ptr - current_buffer_ptr < max_record_size) {
        break;
      }

      entry_index -= num_entries;
      MethodTraceRecord record;
      ReadValuesFromRecord(
          method_trace_entries, entry_index, record, has_thread_cpu_clock, has_wall_clock);

      uint64_t method_id = reinterpret_cast<uintptr_t>(record.method);
      uint64_t method_action_encoding = method_id | record.action;

      int64_t method_diff = method_action_encoding - prev_method_action_encoding;
      current_buffer_ptr = EncodeSignedLeb128(current_buffer_ptr, method_diff);
      prev_method_action_encoding = method_action_encoding;

      if (has_wall_clock) {
        current_buffer_ptr = EncodeUnsignedLeb128(current_buffer_ptr,
                                                  (record.wall_clock_time - prev_wall_timestamp));
        prev_wall_timestamp = record.wall_clock_time;
      }

      if (has_thread_cpu_clock) {
        current_buffer_ptr = EncodeUnsignedLeb128(current_buffer_ptr,
                                                  (record.thread_cpu_time - prev_thread_timestamp));
        prev_thread_timestamp = record.thread_cpu_time;
      }
    }

    uint32_t size = current_buffer_ptr - (init_buffer_ptr + kEntryHeaderSizeV2);
    EncodeEventBlockHeader(init_buffer_ptr, tid, curr_record_index, size);

    if (trace_output_mode_ != TraceOutputMode::kStreaming) {
      if (curr_record_index < num_records) {
        overflow_ = true;
      }
      // In non-streaming mode, we keep the data in the buffer and write to the
      // file when tracing has stopped. Just update the offset of the buffer.
      cur_offset_ += (current_buffer_ptr - init_buffer_ptr);
      return curr_record_index;
    } else {
      // Flush the contents of the buffer to the file.
      if (!trace_file_->WriteFully(init_buffer_ptr, current_buffer_ptr - init_buffer_ptr)) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
    }
  }

  return num_records;
}

void TraceWriter::FlushBuffer(uintptr_t* method_trace_entries,
                              size_t current_offset,
                              size_t tid,
                              const std::unordered_map<ArtMethod*, std::string>& method_infos) {
  // Take a trace_writer_lock_ to serialize writes across threads. We also need to allocate a unique
  // method id for each method. We do that by maintaining a map from id to method for each newly
  // seen method. trace_writer_lock_ is required to serialize these.
  MutexLock mu(Thread::Current(), trace_writer_lock_);
  size_t current_index = 0;
  uint8_t* buffer_ptr = buf_.get();
  size_t buffer_size = buffer_size_;

  size_t num_entries = GetNumEntries(clock_source_);
  size_t num_records = (kPerThreadBufSize - current_offset) / num_entries;
  DCHECK_EQ((kPerThreadBufSize - current_offset) % num_entries, 0u);

  int num_records_written = 0;
  if (trace_format_version_ == Trace::kFormatV1) {
    num_records_written =
        FlushEntriesFormatV1(method_trace_entries, tid, method_infos, current_offset, num_records);
  } else {
    num_records_written = FlushEntriesFormatV2(method_trace_entries, tid, num_records);
  }
  num_records_ += num_records_written;
  return;
}

void Trace::LogMethodTraceEvent(Thread* thread,
                                ArtMethod* method,
                                TraceAction action,
                                uint64_t thread_clock_diff,
                                uint64_t timestamp_counter) {
  // This method is called in both tracing modes (method and sampling). In sampling mode, this
  // method is only called by the sampling thread. In method tracing mode, it can be called
  // concurrently.

  uintptr_t* method_trace_buffer = thread->GetMethodTraceBuffer();
  uintptr_t** current_entry_ptr = thread->GetTraceBufferCurrEntryPtr();
  // Initialize the buffer lazily. It's just simpler to keep the creation at one place.
  if (method_trace_buffer == nullptr) {
    method_trace_buffer = trace_writer_->AcquireTraceBuffer(thread->GetTid());
    DCHECK(method_trace_buffer != nullptr);
    thread->SetMethodTraceBuffer(method_trace_buffer, kPerThreadBufSize);
    trace_writer_->RecordThreadInfo(thread);
  }

  if (trace_writer_->HasOverflow()) {
    // In non-streaming modes, we stop recoding events once the buffer is full. Just reset the
    // index, so we don't go to runtime for each method.
    thread->SetMethodTraceBufferCurrentEntry(kPerThreadBufSize);
    return;
  }

  size_t required_entries = GetNumEntries(clock_source_);
  if (*current_entry_ptr - required_entries < method_trace_buffer) {
    // This returns nullptr in non-streaming mode if there's an overflow and we cannot record any
    // more entries. In streaming mode, it returns nullptr if it fails to allocate a new buffer.
    method_trace_buffer = trace_writer_->PrepareBufferForNewEntries(thread);
    if (method_trace_buffer == nullptr) {
      thread->SetMethodTraceBufferCurrentEntry(kPerThreadBufSize);
      return;
    }
  }
  *current_entry_ptr = *current_entry_ptr - required_entries;

  // Record entry in per-thread trace buffer.
  int entry_index = 0;
  uintptr_t* current_entry = *current_entry_ptr;
  // Ensure we always use the non-obsolete version of the method so that entry/exit events have the
  // same pointer value.
  method = method->GetNonObsoleteMethod();
  current_entry[entry_index++] = reinterpret_cast<uintptr_t>(method) | action;
  if (UseThreadCpuClock(clock_source_)) {
    if (art::kRuntimePointerSize == PointerSize::k32) {
      // On 32-bit architectures store threadcputimer as two 32-bit values.
      current_entry[entry_index++] = static_cast<uint32_t>(thread_clock_diff);
      current_entry[entry_index++] = thread_clock_diff >> 32;
    } else {
      current_entry[entry_index++] = thread_clock_diff;
    }
  }
  if (UseWallClock(clock_source_)) {
    if (art::kRuntimePointerSize == PointerSize::k32) {
      // On 32-bit architectures store timestamp counter as two 32-bit values.
      current_entry[entry_index++] = static_cast<uint32_t>(timestamp_counter);
      current_entry[entry_index++] = timestamp_counter >> 32;
    } else {
      current_entry[entry_index++] = timestamp_counter;
    }
  }
}

void TraceWriter::EncodeEventEntry(uint8_t* ptr,
                                   uint16_t thread_id,
                                   uint32_t method_index,
                                   TraceAction action,
                                   uint64_t thread_clock_diff,
                                   uint64_t wall_clock_diff) {
  static constexpr size_t kPacketSize = 14U;  // The maximum size of data in a packet.
  DCHECK(method_index < (1 << (32 - TraceActionBits)));
  uint32_t method_value = (method_index << TraceActionBits) | action;
  Append2LE(ptr, thread_id);
  Append4LE(ptr + 2, method_value);
  ptr += 6;

  static constexpr uint64_t ns_to_us = 1000;
  uint32_t thread_clock_diff_us = thread_clock_diff / ns_to_us;
  uint32_t wall_clock_diff_us = wall_clock_diff / ns_to_us;
  if (UseThreadCpuClock(clock_source_)) {
    Append4LE(ptr, thread_clock_diff_us);
    ptr += 4;
  }
  if (UseWallClock(clock_source_)) {
    Append4LE(ptr, wall_clock_diff_us);
  }
  static_assert(kPacketSize == 2 + 4 + 4 + 4, "Packet size incorrect.");
}

void TraceWriter::EncodeEventBlockHeader(uint8_t* ptr,
                                         uint32_t thread_id,
                                         uint32_t num_records,
                                         uint32_t size) {
  ptr[0] = kEntryHeaderV2;
  Append4LE(ptr + 1, thread_id);
  // This specifies the total number of records encoded in the block using lebs.
  DCHECK_LT(num_records, 1u << 24);
  Append3LE(ptr + 5, num_records);
  Append4LE(ptr + 8, size);
}

void TraceWriter::EnsureSpace(uint8_t* buffer,
                              size_t* current_index,
                              size_t buffer_size,
                              size_t required_size) {
  if (*current_index + required_size < buffer_size) {
    return;
  }

  if (!trace_file_->WriteFully(buffer, *current_index)) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
  *current_index = 0;
}

void TraceWriter::DumpMethodList(std::ostream& os) {
  MutexLock mu(Thread::Current(), trace_writer_lock_);
  for (auto const& entry : art_method_id_map_) {
    os << GetMethodLine(GetMethodInfoLine(entry.first), entry.second);
  }
}

void TraceWriter::DumpThreadList(std::ostream& os) {
  MutexLock mu(Thread::Current(), trace_writer_lock_);
  for (const auto& it : threads_list_) {
    os << it.first << "\t" << it.second << "\n";
  }
}

TraceOutputMode Trace::GetOutputMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace output mode requested, but no trace currently running";
  return the_trace_->trace_writer_->GetOutputMode();
}

Trace::TraceMode Trace::GetMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace mode requested, but no trace currently running";
  return the_trace_->trace_mode_;
}

int Trace::GetFlags() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace flags requested, but no trace currently running";
  return the_trace_->flags_;
}

int Trace::GetIntervalInMillis() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace interval requested, but no trace currently running";
  return the_trace_->interval_us_;
}

size_t Trace::GetBufferSize() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace buffer size requested, but no trace currently running";
  return the_trace_->trace_writer_->GetBufferSize();
}

bool Trace::IsTracingEnabled() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  return the_trace_ != nullptr;
}

bool Trace::IsTracingEnabledLocked() {
  return the_trace_ != nullptr;
}

}  // namespace art
