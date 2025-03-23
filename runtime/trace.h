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

#ifndef ART_RUNTIME_TRACE_H_
#define ART_RUNTIME_TRACE_H_

#include <bitset>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/atomic.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/os.h"
#include "base/safe_map.h"
#include "base/unix_file/fd_file.h"
#include "class_linker.h"
#include "instrumentation.h"
#include "runtime_globals.h"
#include "thread_pool.h"

namespace art HIDDEN {

class ArtField;
class ArtMethod;
class DexFile;
class ShadowFrame;
class Thread;

struct MethodTraceRecord;

using DexIndexBitSet = std::bitset<65536>;

enum TracingMode {
  kTracingInactive,
  kMethodTracingActive,  // Trace activity synchronous with method progress.
  kSampleProfilingActive,  // Trace activity captured by sampling thread.
};
std::ostream& operator<<(std::ostream& os, TracingMode rhs);

// File format:
//     header
//     record 0
//     record 1
//     ...
//
// Header format:
//     u4  magic ('SLOW')
//     u2  version
//     u2  offset to data
//     u8  start date/time in usec
//     u2  record size in bytes (version >= 2 only)
//     ... padding to 32 bytes
//
// Record format v1:
//     u1  thread ID
//     u4  method ID | method action
//     u4  time delta since start, in usec
//
// Record format v2:
//     u2  thread ID
//     u4  method ID | method action
//     u4  time delta since start, in usec
//
// Record format v3:
//     u2  thread ID
//     u4  method ID | method action
//     u4  time delta since start, in usec
//     u4  wall time since start, in usec (when clock == "dual" only)
//
// 32 bits of microseconds is 70 minutes.
//
// All values are stored in little-endian order.

enum TraceAction {
    kTraceMethodEnter = 0x00,       // method entry
    kTraceMethodExit = 0x01,        // method exit
    kTraceUnroll = 0x02,            // method exited by exception unrolling
    // 0x03 currently unused
    kTraceMethodActionMask = 0x03,  // two bits
};

enum class TraceOutputMode {
    kFile,
    kDDMS,
    kStreaming
};

// We need 3 entries to store 64-bit timestamp counter as two 32-bit values on 32-bit architectures.
static constexpr uint32_t kNumEntriesForWallClock =
    (kRuntimePointerSize == PointerSize::k64) ? 2 : 3;
// Timestamps are stored as two 32-bit balues on 32-bit architectures.
static constexpr uint32_t kNumEntriesForDualClock = (kRuntimePointerSize == PointerSize::k64)
                                                        ? kNumEntriesForWallClock + 1
                                                        : kNumEntriesForWallClock + 2;

// These define offsets in bytes for the individual fields of a trace entry. These are used by the
// JITed code when storing a trace entry.
static constexpr int32_t kMethodOffsetInBytes = 0;
static constexpr int32_t kTimestampOffsetInBytes = 1 * static_cast<uint32_t>(kRuntimePointerSize);
// On 32-bit architectures we store 64-bit timestamp as two 32-bit values.
// kHighTimestampOffsetInBytes is only relevant on 32-bit architectures.
static constexpr int32_t kHighTimestampOffsetInBytes =
    2 * static_cast<uint32_t>(kRuntimePointerSize);

static constexpr uintptr_t kMaskTraceAction = ~0b11;

// Packet type encoding for the new method tracing format.
static constexpr int kThreadInfoHeaderV2 = 0;
static constexpr int kMethodInfoHeaderV2 = 1;
static constexpr int kEntryHeaderV2 = 2;
static constexpr int kSummaryHeaderV2 = 3;

// Packet sizes for the new method tracing format.
static constexpr uint16_t kTraceHeaderLengthV2 = 32;
// We have 2 entries (method pointer and timestamp) which are uleb encoded. Each
// of them is a maximum of 64 bits which would need 10 bytes at the maximum.
static constexpr uint16_t kMaxTraceRecordSizeSingleClockV2 = 20;
// We will have one more timestamp of 64 bits if we use a dual clock source.
static constexpr uint16_t kMaxTraceRecordSizeDualClockV2 = kMaxTraceRecordSizeSingleClockV2 + 10;
static constexpr uint16_t kEntryHeaderSizeV2 = 12;

static constexpr uint16_t kTraceVersionSingleClockV2 = 4;
static constexpr uint16_t kTraceVersionDualClockV2 = 5;

// TODO(mythria): Consider adding checks to guard agaist OOB access for Append*LE methods.
// Currently the onus is on the callers to ensure there is sufficient space in the buffer.
// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static inline void Append2LE(uint8_t* buf, uint16_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static inline void Append3LE(uint8_t* buf, uint16_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static inline void Append4LE(uint8_t* buf, uint32_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static inline void Append8LE(uint8_t* buf, uint64_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
  *buf++ = static_cast<uint8_t>(val >> 32);
  *buf++ = static_cast<uint8_t>(val >> 40);
  *buf++ = static_cast<uint8_t>(val >> 48);
  *buf++ = static_cast<uint8_t>(val >> 56);
}

class TraceWriterThreadPool : public ThreadPool {
 public:
  static TraceWriterThreadPool* Create(const char* name) {
    TraceWriterThreadPool* pool = new TraceWriterThreadPool(name);
    pool->CreateThreads();
    return pool;
  }

  uintptr_t* FinishTaskAndClaimBuffer(size_t tid);

 private:
  explicit TraceWriterThreadPool(const char* name)
      : ThreadPool(name,
                   /* num_threads= */ 1,
                   /* create_peers= */ false,
                   /* worker_stack_size= */ ThreadPoolWorker::kDefaultStackSize) {}
};

class TraceWriter {
 public:
  TraceWriter(File* trace_file,
              TraceOutputMode output_mode,
              TraceClockSource clock_source,
              size_t buffer_size,
              int num_trace_buffers,
              int trace_format_version,
              uint64_t clock_overhead_ns);

  // This encodes all the events in the per-thread trace buffer and writes it to the trace file /
  // buffer. This acquires streaming lock to prevent any other threads writing concurrently. It is
  // required to serialize these since each method is encoded with a unique id which is assigned
  // when the method is seen for the first time in the recoreded events. So we need to serialize
  // these flushes across threads.
  void FlushBuffer(Thread* thread, bool is_sync, bool free_buffer)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!trace_writer_lock_);

  // This is called when the per-thread buffer is full and a new entry needs to be recorded. This
  // returns a pointer to the new buffer where the entries should be recorded.
  // In streaming mode, we just flush the per-thread buffer. The buffer is flushed asynchronously
  // on a thread pool worker. This creates a new buffer and updates the per-thread buffer pointer
  // and returns a pointer to the newly created buffer.
  // In non-streaming mode, buffers from all threads are flushed to see if there's enough room
  // in the centralized buffer before recording new entries. We just flush these buffers
  // synchronously and reuse the existing buffer. Since this mode is mostly deprecated we want to
  // keep the implementation simple here.
  uintptr_t* PrepareBufferForNewEntries(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!trace_writer_lock_);

  // Creates a summary packet which includes some meta information like number of events, clock
  // overhead, trace version in human readable form. This is used to dump the summary at the end
  // of tracing..
  std::string CreateSummary(int flags) REQUIRES(!trace_writer_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Flushes all per-thread buffer and also write a summary entry.
  void FinishTracing(int flags, bool flush_entries) REQUIRES(!trace_writer_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void PreProcessTraceForMethodInfos(uintptr_t* buffer,
                                     size_t num_entries,
                                     std::unordered_map<ArtMethod*, std::string>& method_infos)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!trace_writer_lock_);

  // Flush buffer to the file (for streaming) or to the common buffer (for non-streaming). In
  // non-streaming case it returns false if all the contents couldn't be flushed.
  void FlushBuffer(uintptr_t* buffer,
                   size_t num_entries,
                   size_t tid,
                   const std::unordered_map<ArtMethod*, std::string>& method_infos)
      REQUIRES(!trace_writer_lock_);

  // This is called when we see the first entry from the thread to record the information about the
  // thread.
  void RecordThreadInfo(Thread* thread) REQUIRES(!trace_writer_lock_);

  // Records information about all methods in the newly loaded class in the buffer. If the buffer
  // doesn't have enough space to record the entry, then it adds a task to flush the buffer
  // contents and uses a new buffer to record the information.
  // buffer is the pointer to buffer that is used to record method info and the offset is the
  // offset in the buffer to start recording method info. If *buffer is nullptr then a new one is
  // allocated and buffer is updated to point to the newly allocated one.
  void RecordMethodInfoV2(mirror::Class* klass, uint8_t** buffer, size_t* offset)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!trace_writer_lock_);

  bool HasOverflow() { return overflow_; }
  TraceOutputMode GetOutputMode() { return trace_output_mode_; }
  size_t GetBufferSize() { return buffer_size_; }

  // Performs the initialization for the buffer pool. It marks all buffers as free by storing 0
  // as the owner tid. This also allocates the buffer pool.
  void InitializeTraceBuffers();

  // Releases the trace buffer and signals any waiting threads about a free buffer.
  void ReleaseBuffer(int index);

  // Release the trace buffer of the thread. This is called to release the buffer without flushing
  // the entries. See a comment in ThreadList::Unregister for more detailed explanation.
  void ReleaseBufferForThread(Thread* self);

  // Tries to find a free buffer (which has owner of 0) from the pool. If there are no free buffers
  // then it just waits for a free buffer. To prevent any deadlocks, we only wait if the number of
  // pending tasks are greater than the number of waiting threads. Allocates a new buffer if it
  // isn't safe to wait.
  uintptr_t* AcquireTraceBuffer(size_t tid) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!trace_writer_lock_);

  // Returns the index corresponding to the start of the current_buffer. We allocate one large
  // buffer and assign parts of it for each thread.
  int GetMethodTraceIndex(uintptr_t* current_buffer);

  int GetTraceFormatVersion() { return trace_format_version_; }

  // Ensures that there are no threads suspended waiting for a free buffer. It signals threads
  // waiting for a free buffer and waits for all the threads to respond to the signal.
  void StopTracing();

  // Adds a task to write method info to the file. The buffer is already in the
  // right format and it just adds a new task which takes the ownership of the
  // buffer and returns a new buffer that can be used. If release is set to true
  // then it doesn't fetch a new buffer.
  uint8_t* AddMethodInfoWriteTask(uint8_t* buffer, size_t offset, size_t tid, bool release)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!trace_writer_lock_);

  // Writes buffer contents to the file.
  void WriteToFile(uint8_t* buffer, size_t offset);

 private:
  void ReadValuesFromRecord(uintptr_t* method_trace_entries,
                            size_t record_index,
                            MethodTraceRecord& record,
                            bool has_thread_cpu_clock,
                            bool has_wall_clock);

  size_t FlushEntriesFormatV2(uintptr_t* method_trace_entries, size_t tid, size_t num_records)
      REQUIRES(trace_writer_lock_);

  size_t FlushEntriesFormatV1(uintptr_t* method_trace_entries,
                              size_t tid,
                              const std::unordered_map<ArtMethod*, std::string>& method_infos,
                              size_t end_offset,
                              size_t num_records) REQUIRES(trace_writer_lock_);
  // Get a 32-bit id for the method and specify if the method hasn't been seen before. If this is
  // the first time we see this method record information (like method name, declaring class etc.,)
  // about the method.
  std::pair<uint32_t, bool> GetMethodEncoding(ArtMethod* method) REQUIRES(trace_writer_lock_);
  bool HasMethodEncoding(ArtMethod* method) REQUIRES(trace_writer_lock_);

  // Get a 16-bit id for the thread. We don't want to use thread ids directly since they can be
  // more than 16-bit.
  uint16_t GetThreadEncoding(pid_t thread_id) REQUIRES(trace_writer_lock_);

  // Get the information about the method.
  std::string GetMethodLine(const std::string& method_line, uint32_t method_id);

  // Helper function to record method information when processing the events. These are used by
  // streaming output mode. Non-streaming modes dump the methods and threads list at the end of
  // tracing.
  void RecordMethodInfoV1(const std::string& method_line, uint64_t method_id)
      REQUIRES(trace_writer_lock_);

  // Encodes the trace event. This assumes that there is enough space reserved to encode the entry.
  void EncodeEventEntry(uint8_t* ptr,
                        uint16_t thread_id,
                        uint32_t method_index,
                        TraceAction action,
                        uint64_t thread_clock_diff,
                        uint64_t wall_clock_diff) REQUIRES(trace_writer_lock_);

  // Encodes the header for the events block. This assumes that there is enough space reserved to
  // encode the entry.
  void EncodeEventBlockHeader(uint8_t* ptr, uint32_t thread_id, uint32_t num_records, uint32_t size)
      REQUIRES(trace_writer_lock_);

  // Ensures there is sufficient space in the buffer to record the requested_size. If there is not
  // enough sufficient space the current contents of the buffer are written to the file and
  // current_index is reset to 0. This doesn't check if buffer_size is big enough to hold the
  // requested size.
  void EnsureSpace(uint8_t* buffer,
                   size_t* current_index,
                   size_t buffer_size,
                   size_t required_size);

  // Flush tracing buffers from all the threads.
  void FlushAllThreadBuffers() REQUIRES(!Locks::thread_list_lock_) REQUIRES(!trace_writer_lock_);


  // Methods to output traced methods and threads.
  void DumpMethodList(std::ostream& os) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!trace_writer_lock_);
  void DumpThreadList(std::ostream& os) REQUIRES(!Locks::thread_list_lock_, !trace_writer_lock_);

  // File to write trace data out to, null if direct to ddms.
  std::unique_ptr<File> trace_file_;

  // The kind of output for this tracing.
  const TraceOutputMode trace_output_mode_;

  // The clock source for this tracing.
  const TraceClockSource clock_source_;

  // Map of thread ids and names. This is used only in non-streaming mode, since we have to dump
  // information about all threads in one block. In streaming mode, thread info is recorded directly
  // in the file when we see the first even from this thread.
  SafeMap<uint16_t, std::string> threads_list_;

  // Map from ArtMethod* to index.
  std::unordered_map<ArtMethod*, uint32_t> art_method_id_map_ GUARDED_BY(trace_writer_lock_);
  uint32_t current_method_index_ = 0;

  // Map from thread_id to a 16-bit identifier.
  std::unordered_map<pid_t, uint16_t> thread_id_map_ GUARDED_BY(trace_writer_lock_);
  uint16_t current_thread_index_;

  // Buffer used when generating trace data from the raw entries.
  // In streaming mode, the trace data is flushed to file when the per-thread buffer gets full.
  // In non-streaming mode, this data is flushed at the end of tracing. If the buffer gets full
  // we stop tracing and following trace events are ignored. The size of this buffer is
  // specified by the user in non-streaming mode.
  std::unique_ptr<uint8_t[]> buf_;

  // The cur_offset_ into the buf_. Accessed only in SuspendAll scope when flushing data from the
  // thread local buffers to buf_.
  size_t cur_offset_ GUARDED_BY(trace_writer_lock_);

  // Size of buf_.
  const size_t buffer_size_;

  // Version of trace output
  const int trace_format_version_;

  // Time trace was created.
  const uint64_t start_time_;

  // Did we overflow the buffer recording traces?
  bool overflow_;

  // Total number of records flushed to file.
  size_t num_records_;

  // Clock overhead.
  const uint64_t clock_overhead_ns_;

  std::vector<std::atomic<size_t>> owner_tids_;
  std::unique_ptr<uintptr_t[]> trace_buffer_;

  Mutex buffer_pool_lock_;
  ConditionVariable buffer_available_ GUARDED_BY(buffer_pool_lock_);
  ConditionVariable num_waiters_zero_cond_ GUARDED_BY(buffer_pool_lock_);
  std::atomic<size_t> num_waiters_for_buffer_;
  std::atomic<bool> finish_tracing_ = false;

  // Lock to protect common data structures accessed from multiple threads like
  // art_method_id_map_, thread_id_map_.
  Mutex trace_writer_lock_;

  // Thread pool to flush the trace entries to file.
  std::unique_ptr<TraceWriterThreadPool> thread_pool_;
};

// Class for recording event traces. Trace data is either collected
// synchronously during execution (TracingMode::kMethodTracingActive),
// or by a separate sampling thread (TracingMode::kSampleProfilingActive).
class Trace final : public instrumentation::InstrumentationListener, public ClassLoadCallback {
 public:
  enum TraceFlag {
    kTraceCountAllocs = 0x001,
    kTraceClockSourceWallClock = 0x010,
    kTraceClockSourceThreadCpu = 0x100,
  };

  static const int kFormatV1 = 0;
  static const int kFormatV2 = 1;
  static const int kTraceFormatVersionFlagMask = 0b110;
  static const int kTraceFormatVersionShift = 1;

  enum class TraceMode {
    kMethodTracing,
    kSampling
  };

  // Temporary code for debugging b/342768977
  static std::string GetDebugInformation();

  static void SetDefaultClockSource(TraceClockSource clock_source);

  static void Start(const char* trace_filename,
                    size_t buffer_size,
                    int flags,
                    TraceOutputMode output_mode,
                    TraceMode trace_mode,
                    int interval_us)
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_,
               !Locks::trace_lock_);
  static void Start(int trace_fd,
                    size_t buffer_size,
                    int flags,
                    TraceOutputMode output_mode,
                    TraceMode trace_mode,
                    int interval_us)
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_,
               !Locks::trace_lock_);
  static void Start(std::unique_ptr<unix_file::FdFile>&& file,
                    size_t buffer_size,
                    int flags,
                    TraceOutputMode output_mode,
                    TraceMode trace_mode,
                    int interval_us)
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_,
               !Locks::trace_lock_);
  static void StartDDMS(size_t buffer_size,
                        int flags,
                        TraceMode trace_mode,
                        int interval_us)
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::thread_suspend_count_lock_,
               !Locks::trace_lock_);

  // Stop tracing. This will finish the trace and write it to file/send it via DDMS.
  static void Stop()
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::trace_lock_);
  // Abort tracing. This will just stop tracing and *not* write/send the collected data.
  static void Abort()
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::trace_lock_);
  static void Shutdown()
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::trace_lock_);

  static TracingMode GetMethodTracingMode() REQUIRES(!Locks::trace_lock_);

  // Flush the per-thread buffer. This is called when the thread is about to detach.
  static void FlushThreadBuffer(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::trace_lock_) NO_THREAD_SAFETY_ANALYSIS;

  // Release per-thread buffer without flushing any entries. This is used when a new trace buffer is
  // allocated while the thread is terminating. See ThreadList::Unregister for more details.
  static void ReleaseThreadBuffer(Thread* thread)
      REQUIRES(!Locks::trace_lock_) NO_THREAD_SAFETY_ANALYSIS;

  // Removes any listeners installed for method tracing. This is used in non-streaming case
  // when we no longer record any events once the buffer is full. In other cases listeners are
  // removed only when tracing stops. This is expected to be called in SuspendAll scope.
  static void RemoveListeners() REQUIRES(Locks::mutator_lock_);

  void MeasureClockOverhead();
  uint64_t GetClockOverheadNanoSeconds();

  void CompareAndUpdateStackTrace(Thread* thread, std::vector<ArtMethod*>* stack_trace)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // InstrumentationListener implementation.
  void MethodEntered(Thread* thread, ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_) override;
  void MethodExited(Thread* thread,
                    ArtMethod* method,
                    instrumentation::OptionalFrame frame,
                    JValue& return_value) REQUIRES_SHARED(Locks::mutator_lock_) override;
  void MethodUnwind(Thread* thread, ArtMethod* method, uint32_t dex_pc)
      REQUIRES_SHARED(Locks::mutator_lock_) override;
  void DexPcMoved(Thread* thread,
                  Handle<mirror::Object> this_object,
                  ArtMethod* method,
                  uint32_t new_dex_pc) REQUIRES_SHARED(Locks::mutator_lock_) override;
  void FieldRead(Thread* thread,
                 Handle<mirror::Object> this_object,
                 ArtMethod* method,
                 uint32_t dex_pc,
                 ArtField* field) REQUIRES_SHARED(Locks::mutator_lock_) override;
  void FieldWritten(Thread* thread,
                    Handle<mirror::Object> this_object,
                    ArtMethod* method,
                    uint32_t dex_pc,
                    ArtField* field,
                    const JValue& field_value) REQUIRES_SHARED(Locks::mutator_lock_) override;
  void ExceptionThrown(Thread* thread, Handle<mirror::Throwable> exception_object)
      REQUIRES_SHARED(Locks::mutator_lock_) override;
  void ExceptionHandled(Thread* thread, Handle<mirror::Throwable> exception_object)
      REQUIRES_SHARED(Locks::mutator_lock_) override;
  void Branch(Thread* thread, ArtMethod* method, uint32_t dex_pc, int32_t dex_pc_offset)
      REQUIRES_SHARED(Locks::mutator_lock_) override;
  void WatchedFramePop(Thread* thread, const ShadowFrame& frame)
      REQUIRES_SHARED(Locks::mutator_lock_) override;

  // ClassLoadCallback implementation
  void ClassLoad([[maybe_unused]] Handle<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) override {}
  void ClassPrepare(Handle<mirror::Class> temp_klass, Handle<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_) override;

  TraceClockSource GetClockSource() { return clock_source_; }

  // Reuse an old stack trace if it exists, otherwise allocate a new one.
  static std::vector<ArtMethod*>* AllocStackTrace();
  // Clear and store an old stack trace for later use.
  static void FreeStackTrace(std::vector<ArtMethod*>* stack_trace);

  static TraceOutputMode GetOutputMode() REQUIRES(!Locks::trace_lock_);
  static TraceMode GetMode() REQUIRES(!Locks::trace_lock_);
  static size_t GetBufferSize() REQUIRES(!Locks::trace_lock_);
  static int GetFlags() REQUIRES(!Locks::trace_lock_);
  static int GetIntervalInMillis() REQUIRES(!Locks::trace_lock_);

  // Used by class linker to prevent class unloading.
  static bool IsTracingEnabled() REQUIRES(!Locks::trace_lock_);

  // Used by the profiler to see if there is any ongoing tracing.
  static bool IsTracingEnabledLocked() REQUIRES(Locks::trace_lock_);

  // Callback for each class prepare event to record information about the newly created methods.
  static void ClassPrepare(Handle<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_);

  TraceWriter* GetTraceWriter() { return trace_writer_.get(); }

 private:
  Trace(File* trace_file,
        size_t buffer_size,
        int flags,
        TraceOutputMode output_mode,
        TraceMode trace_mode);

  // The sampling interval in microseconds is passed as an argument.
  static void* RunSamplingThread(void* arg) REQUIRES(!Locks::trace_lock_);

  static void StopTracing(bool flush_entries)
      REQUIRES(!Locks::mutator_lock_, !Locks::thread_list_lock_, !Locks::trace_lock_)
      // There is an annoying issue with static functions that create a new object and call into
      // that object that causes them to not be able to tell that we don't currently hold the lock.
      // This causes the negative annotations to incorrectly have a false positive. TODO: Figure out
      // how to annotate this.
      NO_THREAD_SAFETY_ANALYSIS;

  void ReadClocks(Thread* thread, uint64_t* thread_clock_diff, uint64_t* timestamp_counter);

  void LogMethodTraceEvent(Thread* thread,
                           ArtMethod* method,
                           TraceAction action,
                           uint64_t thread_clock_diff,
                           uint64_t timestamp_counter) REQUIRES_SHARED(Locks::mutator_lock_);

  // Singleton instance of the Trace or null when no method tracing is active.
  static Trace* the_trace_ GUARDED_BY(Locks::trace_lock_);

  // The default profiler clock source.
  static TraceClockSource default_clock_source_;

  // Sampling thread, non-zero when sampling.
  static pthread_t sampling_pthread_;

  // Used to remember an unused stack trace to avoid re-allocation during sampling.
  static std::unique_ptr<std::vector<ArtMethod*>> temp_stack_trace_;

  // Flags enabling extra tracing of things such as alloc counts.
  const int flags_;

  // The tracing method.
  const TraceMode trace_mode_;

  const TraceClockSource clock_source_;

  // Sampling profiler sampling interval.
  int interval_us_;

  // A flag to indicate to the sampling thread whether to stop tracing
  bool stop_tracing_;

  std::unique_ptr<TraceWriter> trace_writer_;

  DISALLOW_COPY_AND_ASSIGN(Trace);
};

}  // namespace art

#endif  // ART_RUNTIME_TRACE_H_
