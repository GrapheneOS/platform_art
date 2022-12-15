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
#include "base/enums.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_linker.h"
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

namespace art {

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

TraceClockSource Trace::default_clock_source_ = kDefaultTraceClockSource;

Trace* volatile Trace::the_trace_ = nullptr;
pthread_t Trace::sampling_pthread_ = 0U;
std::unique_ptr<std::vector<ArtMethod*>> Trace::temp_stack_trace_;

// The key identifying the tracer to update instrumentation.
static constexpr const char* kTracerInstrumentationKey = "Tracer";

static TraceAction DecodeTraceAction(uint32_t tmid) {
  return static_cast<TraceAction>(tmid & kTraceMethodActionMask);
}

ArtMethod* Trace::DecodeTraceMethod(uint32_t tmid) {
  MutexLock mu(Thread::Current(), *unique_methods_lock_);
  return unique_methods_[tmid >> TraceActionBits];
}

uint32_t Trace::EncodeTraceMethod(ArtMethod* method) {
  MutexLock mu(Thread::Current(), *unique_methods_lock_);
  uint32_t idx;
  auto it = art_method_id_map_.find(method);
  if (it != art_method_id_map_.end()) {
    idx = it->second;
  } else {
    unique_methods_.push_back(method);
    idx = unique_methods_.size() - 1;
    art_method_id_map_.emplace(method, idx);
  }
  DCHECK_LT(idx, unique_methods_.size());
  DCHECK_EQ(unique_methods_[idx], method);
  return idx;
}

uint32_t Trace::EncodeTraceMethodAndAction(ArtMethod* method, TraceAction action) {
  uint32_t tmid = (EncodeTraceMethod(method) << TraceActionBits) | action;
  DCHECK_EQ(method, DecodeTraceMethod(tmid));
  return tmid;
}

std::vector<ArtMethod*>* Trace::AllocStackTrace() {
  return (temp_stack_trace_.get() != nullptr)  ? temp_stack_trace_.release() :
      new std::vector<ArtMethod*>();
}

void Trace::FreeStackTrace(std::vector<ArtMethod*>* stack_trace) {
  stack_trace->clear();
  temp_stack_trace_.reset(stack_trace);
}

void Trace::SetDefaultClockSource(TraceClockSource clock_source) {
#if defined(__linux__)
  default_clock_source_ = clock_source;
#else
  if (clock_source != TraceClockSource::kWall) {
    LOG(WARNING) << "Ignoring tracing request to use CPU time.";
  }
#endif
}

static uint16_t GetTraceVersion(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kTraceVersionDualClock
                                                    : kTraceVersionSingleClock;
}

static uint16_t GetRecordSize(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kTraceRecordSizeDualClock
                                                    : kTraceRecordSizeSingleClock;
}

bool Trace::UseThreadCpuClock() {
  return (clock_source_ == TraceClockSource::kThreadCpu) ||
      (clock_source_ == TraceClockSource::kDual);
}

bool Trace::UseWallClock() {
  return (clock_source_ == TraceClockSource::kWall) ||
      (clock_source_ == TraceClockSource::kDual);
}

void Trace::MeasureClockOverhead() {
  if (UseThreadCpuClock()) {
    Thread::Current()->GetCpuMicroTime();
  }
  if (UseWallClock()) {
    MicroTime();
  }
}

// Compute an average time taken to measure clocks.
uint32_t Trace::GetClockOverheadNanoSeconds() {
  Thread* self = Thread::Current();
  uint64_t start = self->GetCpuMicroTime();

  for (int i = 4000; i > 0; i--) {
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
  }

  uint64_t elapsed_us = self->GetCpuMicroTime() - start;
  return static_cast<uint32_t>(elapsed_us / 32);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append2LE(uint8_t* buf, uint16_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append4LE(uint8_t* buf, uint32_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append8LE(uint8_t* buf, uint64_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
  *buf++ = static_cast<uint8_t>(val >> 32);
  *buf++ = static_cast<uint8_t>(val >> 40);
  *buf++ = static_cast<uint8_t>(val >> 48);
  *buf++ = static_cast<uint8_t>(val >> 56);
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

static void ClearThreadStackTraceAndClockBase(Thread* thread, void* arg ATTRIBUTE_UNUSED) {
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
  uint32_t thread_clock_diff = 0;
  uint32_t wall_clock_diff = 0;
  ReadClocks(thread, &thread_clock_diff, &wall_clock_diff);
  if (old_stack_trace == nullptr) {
    // If there's no previous stack trace sample for this thread, log an entry event for all
    // methods in the trace.
    for (auto rit = stack_trace->rbegin(); rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, instrumentation::Instrumentation::kMethodEntered,
                          thread_clock_diff, wall_clock_diff);
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
      LogMethodTraceEvent(thread, *old_it, instrumentation::Instrumentation::kMethodExited,
                          thread_clock_diff, wall_clock_diff);
    }
    // Iterate bottom-up over the new trace from the point where they differ, emitting entry events.
    for (; rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, instrumentation::Instrumentation::kMethodEntered,
                          thread_clock_diff, wall_clock_diff);
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
      if (the_trace == nullptr) {
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
      int result ATTRIBUTE_UNUSED = file->Close();
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
    if (the_trace_ != nullptr) {
      LOG(ERROR) << "Trace already in progress, ignoring this request";
    } else {
      enable_stats = (flags & kTraceCountAllocs) != 0;
      the_trace_ = new Trace(trace_file.release(), buffer_size, flags, output_mode, trace_mode);
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
        }
        runtime->GetInstrumentation()->AddListener(
            the_trace_,
            instrumentation::Instrumentation::kMethodEntered |
                instrumentation::Instrumentation::kMethodExited |
                instrumentation::Instrumentation::kMethodUnwind);
        // TODO: In full-PIC mode, we don't need to fully deopt.
        // TODO: We can only use trampoline entrypoints if we are java-debuggable since in that case
        // we know that inlining and other problematic optimizations are disabled. We might just
        // want to use the trampolines anyway since it is faster. It makes the story with disabling
        // jit-gc more complex though.
        runtime->GetInstrumentation()->EnableMethodTracing(kTracerInstrumentationKey,
                                                           /*needs_interpreter=*/false);
      }
    }
  }

  // Can't call this when holding the mutator lock.
  if (enable_stats) {
    runtime->SetStatsEnabled(true);
  }
}

void Trace::StopTracing(bool finish_tracing, bool flush_file) {
  bool stop_alloc_counting = false;
  Runtime* const runtime = Runtime::Current();
  Trace* the_trace = nullptr;
  Thread* const self = Thread::Current();
  pthread_t sampling_pthread = 0U;
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (the_trace_ == nullptr) {
      LOG(ERROR) << "Trace stop requested, but no trace currently running";
    } else {
      the_trace = the_trace_;
      the_trace_ = nullptr;
      sampling_pthread = sampling_pthread_;
    }
  }
  // Make sure that we join before we delete the trace since we don't want to have
  // the sampling thread access a stale pointer. This finishes since the sampling thread exits when
  // the_trace_ is null.
  if (sampling_pthread != 0U) {
    CHECK_PTHREAD_CALL(pthread_join, (sampling_pthread, nullptr), "sampling thread shutdown");
    sampling_pthread_ = 0U;
  }

  if (the_trace != nullptr) {
    stop_alloc_counting = (the_trace->flags_ & Trace::kTraceCountAllocs) != 0;
    // Stop the trace sources adding more entries to the trace buffer and synchronise stores.
    {
      gc::ScopedGCCriticalSection gcs(self,
                                      gc::kGcCauseInstrumentation,
                                      gc::kCollectorTypeInstrumentation);
      jit::ScopedJitSuspend suspend_jit;
      ScopedSuspendAll ssa(__FUNCTION__);

      if (the_trace->trace_mode_ == TraceMode::kSampling) {
        MutexLock mu(self, *Locks::thread_list_lock_);
        runtime->GetThreadList()->ForEach(ClearThreadStackTraceAndClockBase, nullptr);
      } else {
        runtime->GetInstrumentation()->RemoveListener(
            the_trace,
            instrumentation::Instrumentation::kMethodEntered |
                instrumentation::Instrumentation::kMethodExited |
                instrumentation::Instrumentation::kMethodUnwind);
        runtime->GetInstrumentation()->DisableMethodTracing(kTracerInstrumentationKey);
        if (!runtime->IsJavaDebuggableAtInit() && !runtime->IsShuttingDown(self)) {
          art::jit::Jit* jit = runtime->GetJit();
          if (jit != nullptr) {
            jit->GetCodeCache()->InvalidateAllCompiledCode();
            jit->GetJitCompiler()->SetDebuggableCompilerOption(false);
          }
          runtime->SetRuntimeDebugState(art::Runtime::RuntimeDebugState::kNonJavaDebuggable);
        }
      }
    }
    // At this point, code may read buf_ as it's writers are shutdown
    // and the ScopedSuspendAll above has ensured all stores to buf_
    // are now visible.
    if (finish_tracing) {
      the_trace->FinishTracing();
    }
    if (the_trace->trace_file_.get() != nullptr) {
      // Do not try to erase, so flush and close explicitly.
      if (flush_file) {
        if (the_trace->trace_file_->Flush() != 0) {
          PLOG(WARNING) << "Could not flush trace file.";
        }
      } else {
        the_trace->trace_file_->MarkUnchecked();  // Do not trigger guard.
      }
      if (the_trace->trace_file_->Close() != 0) {
        PLOG(ERROR) << "Could not close trace file.";
      }
    }
    delete the_trace;
  }
  if (stop_alloc_counting) {
    // Can be racy since SetStatsEnabled is not guarded by any locks.
    runtime->SetStatsEnabled(false);
  }
}

void Trace::FlushThreadBuffer(Thread* self) {
  MutexLock mu(self, *Locks::trace_lock_);
  the_trace_->FlushStreamingBuffer(self);
}

void Trace::Abort() {
  // Do not write anything anymore.
  StopTracing(false, false);
}

void Trace::Stop() {
  // Finish writing.
  StopTracing(true, true);
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

static constexpr size_t kMinBufSize = 18U;  // Trace header is up to 18B.
// Size of per-thread buffer size. The value is chosen arbitrarily. This value
// should be greater than kMinBufSize.
static constexpr size_t kPerThreadBufSize = 512 * 1024;
static_assert(kPerThreadBufSize > kMinBufSize);

Trace::Trace(File* trace_file,
             size_t buffer_size,
             int flags,
             TraceOutputMode output_mode,
             TraceMode trace_mode)
    : trace_file_(trace_file),
      buf_(new uint8_t[std::max(kMinBufSize, buffer_size)]()),
      flags_(flags),
      trace_output_mode_(output_mode),
      trace_mode_(trace_mode),
      clock_source_(default_clock_source_),
      buffer_size_(std::max(kMinBufSize, buffer_size)),
      start_time_(MicroTime()),
      clock_overhead_ns_(GetClockOverheadNanoSeconds()),
      overflow_(false),
      interval_us_(0),
      streaming_lock_(nullptr),
      unique_methods_lock_(new Mutex("unique methods lock", kTracingUniqueMethodsLock)) {
  CHECK_IMPLIES(trace_file == nullptr, output_mode == TraceOutputMode::kDDMS);

  uint16_t trace_version = GetTraceVersion(clock_source_);
  if (output_mode == TraceOutputMode::kStreaming) {
    trace_version |= 0xF0U;
  }
  // Set up the beginning of the trace.
  memset(buf_.get(), 0, kTraceHeaderLength);
  Append4LE(buf_.get(), kTraceMagicValue);
  Append2LE(buf_.get() + 4, trace_version);
  Append2LE(buf_.get() + 6, kTraceHeaderLength);
  Append8LE(buf_.get() + 8, start_time_);
  if (trace_version >= kTraceVersionDualClock) {
    uint16_t record_size = GetRecordSize(clock_source_);
    Append2LE(buf_.get() + 16, record_size);
  }
  static_assert(18 <= kMinBufSize, "Minimum buffer size not large enough for trace header");

  cur_offset_.store(kTraceHeaderLength, std::memory_order_relaxed);

  if (output_mode == TraceOutputMode::kStreaming) {
    streaming_lock_ = new Mutex("tracing lock", LockLevel::kTracingStreamingLock);
    // Flush the header information to the file. We use a per thread buffer, so
    // it is easier to just write the header information directly to file.
    if (!trace_file_->WriteFully(buf_.get(), kTraceHeaderLength)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
    cur_offset_.store(0, std::memory_order_relaxed);
  }
}

Trace::~Trace() {
  delete streaming_lock_;
  delete unique_methods_lock_;
}

static uint64_t ReadBytes(uint8_t* buf, size_t bytes) {
  uint64_t ret = 0;
  for (size_t i = 0; i < bytes; ++i) {
    ret |= static_cast<uint64_t>(buf[i]) << (i * 8);
  }
  return ret;
}

void Trace::DumpBuf(uint8_t* buf, size_t buf_size, TraceClockSource clock_source) {
  uint8_t* ptr = buf + kTraceHeaderLength;
  uint8_t* end = buf + buf_size;

  while (ptr < end) {
    uint32_t tmid = ReadBytes(ptr + 2, sizeof(tmid));
    ArtMethod* method = DecodeTraceMethod(tmid);
    TraceAction action = DecodeTraceAction(tmid);
    LOG(INFO) << ArtMethod::PrettyMethod(method) << " " << static_cast<int>(action);
    ptr += GetRecordSize(clock_source);
  }
}

void Trace::FinishTracing() {
  size_t final_offset = 0;
  std::set<ArtMethod*> visited_methods;
  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // Clean up.
    MutexLock mu(Thread::Current(), *streaming_lock_);
    STLDeleteValues(&seen_methods_);
  } else {
    final_offset = cur_offset_.load(std::memory_order_relaxed);
    GetVisitedMethods(final_offset, &visited_methods);
  }

  // Compute elapsed time.
  uint64_t elapsed = MicroTime() - start_time_;

  std::ostringstream os;

  os << StringPrintf("%cversion\n", kTraceTokenChar);
  os << StringPrintf("%d\n", GetTraceVersion(clock_source_));
  os << StringPrintf("data-file-overflow=%s\n", overflow_ ? "true" : "false");
  if (UseThreadCpuClock()) {
    if (UseWallClock()) {
      os << StringPrintf("clock=dual\n");
    } else {
      os << StringPrintf("clock=thread-cpu\n");
    }
  } else {
    os << StringPrintf("clock=wall\n");
  }
  os << StringPrintf("elapsed-time-usec=%" PRIu64 "\n", elapsed);
  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    size_t num_records = (final_offset - kTraceHeaderLength) / GetRecordSize(clock_source_);
    os << StringPrintf("num-method-calls=%zd\n", num_records);
  }
  os << StringPrintf("clock-call-overhead-nsec=%d\n", clock_overhead_ns_);
  os << StringPrintf("vm=art\n");
  os << StringPrintf("pid=%d\n", getpid());
  if ((flags_ & kTraceCountAllocs) != 0) {
    os << "alloc-count=" << Runtime::Current()->GetStat(KIND_ALLOCATED_OBJECTS) << "\n";
    os << "alloc-size=" << Runtime::Current()->GetStat(KIND_ALLOCATED_BYTES) << "\n";
    os << "gc-count=" <<  Runtime::Current()->GetStat(KIND_GC_INVOCATIONS) << "\n";
  }
  os << StringPrintf("%cthreads\n", kTraceTokenChar);
  DumpThreadList(os);
  os << StringPrintf("%cmethods\n", kTraceTokenChar);
  DumpMethodList(os, visited_methods);
  os << StringPrintf("%cend\n", kTraceTokenChar);
  std::string header(os.str());

  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // Flush thread specific buffer from all threads.
    {
      MutexLock tl_lock(Thread::Current(), *Locks::thread_list_lock_);
      for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
        if (thread->GetMethodTraceBuffer() != nullptr) {
          FlushStreamingBuffer(thread);
          thread->ResetMethodTraceBuffer();
        }
      }
    }
    // It is expected that this method is called when all other threads are suspended, so there
    // cannot be any writes to trace_file_ after finish tracing.
    // Write a special token to mark the end of trace records and the start of
    // trace summary.
    uint8_t buf[7];
    Append2LE(buf, 0);
    buf[2] = kOpTraceSummary;
    Append4LE(buf + 3, static_cast<uint32_t>(header.length()));
    // Write the trace summary. The summary is identical to the file header when
    // the output mode is not streaming (except for methods).
    if (!trace_file_->WriteFully(buf, sizeof(buf)) ||
        !trace_file_->WriteFully(header.c_str(), header.length())) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  } else {
    if (trace_file_.get() == nullptr) {
      std::vector<uint8_t> data;
      data.resize(header.length() + final_offset);
      memcpy(data.data(), header.c_str(), header.length());
      memcpy(data.data() + header.length(), buf_.get(), final_offset);
      Runtime::Current()->GetRuntimeCallbacks()->DdmPublishChunk(CHUNK_TYPE("MPSE"),
                                                                 ArrayRef<const uint8_t>(data));
      const bool kDumpTraceInfo = false;
      if (kDumpTraceInfo) {
        LOG(INFO) << "Trace sent:\n" << header;
        DumpBuf(buf_.get(), final_offset, clock_source_);
      }
    } else {
      if (!trace_file_->WriteFully(header.c_str(), header.length()) ||
          !trace_file_->WriteFully(buf_.get(), final_offset)) {
        std::string detail(StringPrintf("Trace data write failed: %s", strerror(errno)));
        PLOG(ERROR) << detail;
        ThrowRuntimeException("%s", detail.c_str());
      }
    }
  }
}

void Trace::DexPcMoved(Thread* thread ATTRIBUTE_UNUSED,
                       Handle<mirror::Object> this_object ATTRIBUTE_UNUSED,
                       ArtMethod* method,
                       uint32_t new_dex_pc) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected dex PC event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << new_dex_pc;
}

void Trace::FieldRead(Thread* thread ATTRIBUTE_UNUSED,
                      Handle<mirror::Object> this_object ATTRIBUTE_UNUSED,
                      ArtMethod* method,
                      uint32_t dex_pc,
                      ArtField* field ATTRIBUTE_UNUSED)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field read event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::FieldWritten(Thread* thread ATTRIBUTE_UNUSED,
                         Handle<mirror::Object> this_object ATTRIBUTE_UNUSED,
                         ArtMethod* method,
                         uint32_t dex_pc,
                         ArtField* field ATTRIBUTE_UNUSED,
                         const JValue& field_value ATTRIBUTE_UNUSED)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field write event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::MethodEntered(Thread* thread, ArtMethod* method) {
  uint32_t thread_clock_diff = 0;
  uint32_t wall_clock_diff = 0;
  ReadClocks(thread, &thread_clock_diff, &wall_clock_diff);
  LogMethodTraceEvent(thread, method, instrumentation::Instrumentation::kMethodEntered,
                      thread_clock_diff, wall_clock_diff);
}

void Trace::MethodExited(Thread* thread,
                         ArtMethod* method,
                         instrumentation::OptionalFrame frame ATTRIBUTE_UNUSED,
                         JValue& return_value ATTRIBUTE_UNUSED) {
  uint32_t thread_clock_diff = 0;
  uint32_t wall_clock_diff = 0;
  ReadClocks(thread, &thread_clock_diff, &wall_clock_diff);
  LogMethodTraceEvent(thread,
                      method,
                      instrumentation::Instrumentation::kMethodExited,
                      thread_clock_diff,
                      wall_clock_diff);
}

void Trace::MethodUnwind(Thread* thread,
                         ArtMethod* method,
                         uint32_t dex_pc ATTRIBUTE_UNUSED) {
  uint32_t thread_clock_diff = 0;
  uint32_t wall_clock_diff = 0;
  ReadClocks(thread, &thread_clock_diff, &wall_clock_diff);
  LogMethodTraceEvent(thread, method, instrumentation::Instrumentation::kMethodUnwind,
                      thread_clock_diff, wall_clock_diff);
}

void Trace::ExceptionThrown(Thread* thread ATTRIBUTE_UNUSED,
                            Handle<mirror::Throwable> exception_object ATTRIBUTE_UNUSED)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::ExceptionHandled(Thread* thread ATTRIBUTE_UNUSED,
                             Handle<mirror::Throwable> exception_object ATTRIBUTE_UNUSED)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::Branch(Thread* /*thread*/, ArtMethod* method,
                   uint32_t /*dex_pc*/, int32_t /*dex_pc_offset*/)
      REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected branch event in tracing" << ArtMethod::PrettyMethod(method);
}

void Trace::WatchedFramePop(Thread* self ATTRIBUTE_UNUSED,
                            const ShadowFrame& frame ATTRIBUTE_UNUSED) {
  LOG(ERROR) << "Unexpected WatchedFramePop event in tracing";
}

void Trace::ReadClocks(Thread* thread, uint32_t* thread_clock_diff, uint32_t* wall_clock_diff) {
  if (UseThreadCpuClock()) {
    uint64_t clock_base = thread->GetTraceClockBase();
    if (UNLIKELY(clock_base == 0)) {
      // First event, record the base time in the map.
      uint64_t time = thread->GetCpuMicroTime();
      thread->SetTraceClockBase(time);
    } else {
      *thread_clock_diff = thread->GetCpuMicroTime() - clock_base;
    }
  }
  if (UseWallClock()) {
    *wall_clock_diff = MicroTime() - start_time_;
  }
}

bool Trace::RegisterMethod(ArtMethod* method) {
  const DexFile* dex_file = method->GetDexFile();
  if (seen_methods_.find(dex_file) == seen_methods_.end()) {
    seen_methods_.insert(std::make_pair(dex_file, new DexIndexBitSet()));
  }
  DexIndexBitSet* bit_set = seen_methods_.find(dex_file)->second;
  if (!(*bit_set)[method->GetDexMethodIndex()]) {
    bit_set->set(method->GetDexMethodIndex());
    return true;
  }
  return false;
}

std::string Trace::GetMethodLine(ArtMethod* method) {
  method = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);
  return StringPrintf("%#x\t%s\t%s\t%s\t%s\n", (EncodeTraceMethod(method) << TraceActionBits),
      PrettyDescriptor(method->GetDeclaringClassDescriptor()).c_str(), method->GetName(),
      method->GetSignature().ToString().c_str(), method->GetDeclaringClassSourceFile());
}

void Trace::RecordStreamingMethodEvent(Thread* thread,
                                       ArtMethod* method,
                                       TraceAction action,
                                       uint32_t thread_clock_diff,
                                       uint32_t wall_clock_diff) {
  uintptr_t* method_trace_buffer = thread->GetMethodTraceBuffer();
  size_t* current_offset = thread->GetMethodTraceIndexPtr();
  // Initialize the buffer lazily. It's just simpler to keep the creation at one place.
  if (method_trace_buffer == nullptr) {
    method_trace_buffer = new uintptr_t[std::max(kMinBufSize, kPerThreadBufSize)]();
    thread->SetMethodTraceBuffer(method_trace_buffer);
    *current_offset = 0;

    // This is the first event from this thread, so first record information about the thread.
    std::string thread_name;
    thread->GetThreadName(thread_name);
    static constexpr size_t kThreadNameHeaderSize = 7;
    uint8_t header[kThreadNameHeaderSize];
    Append2LE(header, 0);
    header[2] = kOpNewThread;
    // We use only 16 bits to encode thread id. On Android, we don't expect to use more than
    // 16-bits for a Tid. For 32-bit platforms it is always ensured we use less than 16 bits.
    // See  __check_max_thread_id in bionic for more details. Even on 64-bit the max threads
    // is currently less than 65536.
    // TODO(mythria): On host, we know thread ids can be greater than 16 bits. Consider adding
    // a map similar to method ids.
    DCHECK(!kIsTargetBuild || thread->GetTid() < (1 << 16));
    Append2LE(header + 3, static_cast<uint16_t>(thread->GetTid()));
    Append2LE(header + 5, static_cast<uint16_t>(thread_name.length()));

    {
      MutexLock mu(Thread::Current(), *streaming_lock_);
      if (!trace_file_->WriteFully(header, kThreadNameHeaderSize) ||
          !trace_file_->WriteFully(reinterpret_cast<const uint8_t*>(thread_name.c_str()),
                                   thread_name.length())) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
    }
  }

  size_t required_entries = (clock_source_ == TraceClockSource::kDual) ? 4 : 3;
  if (*current_offset + required_entries >= kPerThreadBufSize) {
    // We don't have space for further entries. Flush the contents of the buffer and reuse the
    // buffer to store contents. Reset the index to the start of the buffer.
    FlushStreamingBuffer(thread);
    *current_offset = 0;
  }

  // Record entry in per-thread trace buffer.
  method_trace_buffer[*current_offset] = reinterpret_cast<uintptr_t>(method);
  *current_offset += 1;
  // TODO(mythria): We only need two bits to record the action. Consider merging
  // it with the method entry to save space.
  method_trace_buffer[*current_offset] = action;
  *current_offset += 1;
  if (UseThreadCpuClock()) {
    method_trace_buffer[*current_offset] = thread_clock_diff;
    *current_offset += 1;
  }
  if (UseWallClock()) {
    method_trace_buffer[*current_offset] = wall_clock_diff;
    *current_offset += 1;
  }
}

void Trace::WriteToBuf(uint8_t* header,
                       size_t header_size,
                       std::string data,
                       size_t* current_index,
                       uint8_t* buffer,
                       size_t buffer_size) {
  EnsureSpace(buffer, current_index, buffer_size, header_size);
  memcpy(buffer + *current_index, header, header_size);
  *current_index += header_size;

  EnsureSpace(buffer, current_index, buffer_size, data.length());
  if (data.length() < buffer_size) {
    memcpy(buffer + *current_index, reinterpret_cast<const uint8_t*>(data.c_str()), data.length());
    *current_index += data.length();
  } else {
    // The data is larger than buffer, so write directly to the file. EnsureSpace should have
    // flushed any data in the buffer.
    DCHECK_EQ(*current_index, 0U);
    if (!trace_file_->WriteFully(reinterpret_cast<const uint8_t*>(data.c_str()), data.length())) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  }
}

void Trace::FlushStreamingBuffer(Thread* thread) {
  // Take a streaming_lock_ to serialize writes across threads. We also need to allocate a unique
  // method id for each method. We do that by maintaining a map from id to method for each newly
  // seen method (see RegisterMethod). streaming_lock_ also is required to serialize these.
  MutexLock mu(Thread::Current(), *streaming_lock_);
  uintptr_t* method_trace_buffer = thread->GetMethodTraceBuffer();
  // Create a temporary buffer to encode the trace events from the specified thread.
  size_t buffer_size = kPerThreadBufSize;
  size_t current_index = 0;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[std::max(kMinBufSize, buffer_size)]);

  size_t num_entries = *(thread->GetMethodTraceIndexPtr());
  for (size_t entry_index = 0; entry_index < num_entries;) {
    ArtMethod* method = reinterpret_cast<ArtMethod*>(method_trace_buffer[entry_index++]);
    TraceAction action = DecodeTraceAction(method_trace_buffer[entry_index++]);
    uint32_t thread_time = 0;
    uint32_t wall_time = 0;
    if (UseThreadCpuClock()) {
      thread_time = method_trace_buffer[entry_index++];
    }
    if (UseWallClock()) {
      wall_time = method_trace_buffer[entry_index++];
    }

    // If we haven't seen this method before record information about the method.
    if (RegisterMethod(method)) {
      // Write a special block with the name.
      std::string method_line(GetMethodLine(method));
      static constexpr size_t kMethodNameHeaderSize = 5;
      uint8_t method_header[kMethodNameHeaderSize];
      DCHECK_LT(kMethodNameHeaderSize, kPerThreadBufSize);
      Append2LE(method_header, 0);
      method_header[2] = kOpNewMethod;
      Append2LE(method_header + 3, static_cast<uint16_t>(method_line.length()));
      WriteToBuf(method_header,
                 kMethodNameHeaderSize,
                 method_line,
                 &current_index,
                 buffer.get(),
                 buffer_size);
    }

    const size_t record_size = GetRecordSize(clock_source_);
    DCHECK_LT(record_size, kPerThreadBufSize);
    EnsureSpace(buffer.get(), &current_index, buffer_size, record_size);
    EncodeEventEntry(buffer.get() + current_index, thread, method, action, thread_time, wall_time);
    current_index += record_size;
  }

  // Flush the contents of buffer to file.
  if (!trace_file_->WriteFully(buffer.get(), current_index)) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
}

void Trace::RecordMethodEvent(Thread* thread,
                              ArtMethod* method,
                              TraceAction action,
                              uint32_t thread_clock_diff,
                              uint32_t wall_clock_diff) {
  // Advance cur_offset_ atomically.
  int32_t new_offset;
  int32_t old_offset = 0;

  // In the non-streaming case, we do a busy loop here trying to get
  // an offset to write our record and advance cur_offset_ for the
  // next use.
  // Although multiple threads can call this method concurrently,
  // the compare_exchange_weak here is still atomic (by definition).
  // A succeeding update is visible to other cores when they pass
  // through this point.
  old_offset = cur_offset_.load(std::memory_order_relaxed);  // Speculative read
  do {
    new_offset = old_offset + GetRecordSize(clock_source_);
    if (static_cast<size_t>(new_offset) > buffer_size_) {
      overflow_ = true;
      return;
    }
  } while (!cur_offset_.compare_exchange_weak(old_offset, new_offset, std::memory_order_relaxed));

  // Write data into the tracing buffer (if not streaming) or into a
  // small buffer on the stack (if streaming) which we'll put into the
  // tracing buffer below.
  //
  // These writes to the tracing buffer are synchronised with the
  // future reads that (only) occur under FinishTracing(). The callers
  // of FinishTracing() acquire locks and (implicitly) synchronise
  // the buffer memory.
  uint8_t* ptr;
  ptr = buf_.get() + old_offset;
  EncodeEventEntry(ptr, thread, method, action, thread_clock_diff, wall_clock_diff);
}

void Trace::LogMethodTraceEvent(Thread* thread,
                                ArtMethod* method,
                                instrumentation::Instrumentation::InstrumentationEvent event,
                                uint32_t thread_clock_diff,
                                uint32_t wall_clock_diff) {
  // This method is called in both tracing modes (method and sampling). In sampling mode, this
  // method is only called by the sampling thread. In method tracing mode, it can be called
  // concurrently.

  TraceAction action = kTraceMethodEnter;
  switch (event) {
    case instrumentation::Instrumentation::kMethodEntered:
      action = kTraceMethodEnter;
      break;
    case instrumentation::Instrumentation::kMethodExited:
      action = kTraceMethodExit;
      break;
    case instrumentation::Instrumentation::kMethodUnwind:
      action = kTraceUnroll;
      break;
    default:
      UNIMPLEMENTED(FATAL) << "Unexpected event: " << event;
  }

  // Ensure we always use the non-obsolete version of the method so that entry/exit events have the
  // same pointer value.
  method = method->GetNonObsoleteMethod();

  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    RecordStreamingMethodEvent(thread, method, action, thread_clock_diff, wall_clock_diff);
  } else {
    RecordMethodEvent(thread, method, action, thread_clock_diff, wall_clock_diff);
  }
}

void Trace::EncodeEventEntry(uint8_t* ptr,
                             Thread* thread,
                             ArtMethod* method,
                             TraceAction action,
                             uint32_t thread_clock_diff,
                             uint32_t wall_clock_diff) {
  static constexpr size_t kPacketSize = 14U;  // The maximum size of data in a packet.
  uint32_t method_value = EncodeTraceMethodAndAction(method, action);
  Append2LE(ptr, thread->GetTid());
  Append4LE(ptr + 2, method_value);
  ptr += 6;

  if (UseThreadCpuClock()) {
    Append4LE(ptr, thread_clock_diff);
    ptr += 4;
  }
  if (UseWallClock()) {
    Append4LE(ptr, wall_clock_diff);
  }
  static_assert(kPacketSize == 2 + 4 + 4 + 4, "Packet size incorrect.");
}

void Trace::EnsureSpace(uint8_t* buffer,
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

void Trace::GetVisitedMethods(size_t buf_size,
                              std::set<ArtMethod*>* visited_methods) {
  uint8_t* ptr = buf_.get() + kTraceHeaderLength;
  uint8_t* end = buf_.get() + buf_size;

  while (ptr < end) {
    uint32_t tmid = ReadBytes(ptr + 2, sizeof(tmid));
    ArtMethod* method = DecodeTraceMethod(tmid);
    visited_methods->insert(method);
    ptr += GetRecordSize(clock_source_);
  }
}

void Trace::DumpMethodList(std::ostream& os, const std::set<ArtMethod*>& visited_methods) {
  for (const auto& method : visited_methods) {
    os << GetMethodLine(method);
  }
}

static void DumpThread(Thread* t, void* arg) {
  std::ostream& os = *reinterpret_cast<std::ostream*>(arg);
  std::string name;
  t->GetThreadName(name);
  os << t->GetTid() << "\t" << name << "\n";
}

void Trace::DumpThreadList(std::ostream& os) {
  Thread* self = Thread::Current();
  for (const auto& it : exited_threads_) {
    os << it.first << "\t" << it.second << "\n";
  }
  Locks::thread_list_lock_->AssertNotHeld(self);
  MutexLock mu(self, *Locks::thread_list_lock_);
  Runtime::Current()->GetThreadList()->ForEach(DumpThread, &os);
}

void Trace::StoreExitingThreadInfo(Thread* thread) {
  MutexLock mu(thread, *Locks::trace_lock_);
  if (the_trace_ != nullptr) {
    std::string name;
    thread->GetThreadName(name);
    // The same thread/tid may be used multiple times. As SafeMap::Put does not allow to override
    // a previous mapping, use SafeMap::Overwrite.
    the_trace_->exited_threads_.Overwrite(thread->GetTid(), name);
  }
}

Trace::TraceOutputMode Trace::GetOutputMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace output mode requested, but no trace currently running";
  return the_trace_->trace_output_mode_;
}

Trace::TraceMode Trace::GetMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace mode requested, but no trace currently running";
  return the_trace_->trace_mode_;
}

size_t Trace::GetBufferSize() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace mode requested, but no trace currently running";
  return the_trace_->buffer_size_;
}

bool Trace::IsTracingEnabled() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  return the_trace_ != nullptr;
}

}  // namespace art
