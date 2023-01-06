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

#include "thread_list.h"

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <sstream>
#include <tuple>
#include <vector>

#include "android-base/stringprintf.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_utf_chars.h"
#include "unwindstack/AndroidUnwinder.h"

#include "art_field-inl.h"
#include "base/aborting.h"
#include "base/histogram-inl.h"
#include "base/mutex-inl.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/timing_logger.h"
#include "debugger.h"
#include "gc/collector/concurrent_copying.h"
#include "gc/gc_pause_listener.h"
#include "gc/heap.h"
#include "gc/reference_processor.h"
#include "gc_root.h"
#include "jni/jni_internal.h"
#include "lock_word.h"
#include "mirror/string.h"
#include "monitor.h"
#include "native_stack_dump.h"
#include "obj_ptr-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "trace.h"
#include "well_known_classes.h"

#if ART_USE_FUTEXES
#include "linux/futex.h"
#include "sys/syscall.h"
#ifndef SYS_futex
#define SYS_futex __NR_futex
#endif
#endif  // ART_USE_FUTEXES

namespace art {

using android::base::StringPrintf;

static constexpr uint64_t kLongThreadSuspendThreshold = MsToNs(5);
static constexpr useconds_t kThreadSuspendSleepUs = 100;
// The number of times we may retry when we find ourselves in a suspend-unfriendly state.
static constexpr int kMaxSuspendRetries = kIsDebugBuild ? 500 : 5000;

// Whether we should try to dump the native stack of unattached threads. See commit ed8b723 for
// some history.
static constexpr bool kDumpUnattachedThreadNativeStackForSigQuit = true;

ThreadList::ThreadList(uint64_t thread_suspend_timeout_ns)
    : suspend_all_count_(0),
      unregistering_count_(0),
      suspend_all_histogram_("suspend all histogram", 16, 64),
      long_suspend_(false),
      shut_down_(false),
      thread_suspend_timeout_ns_(thread_suspend_timeout_ns),
      empty_checkpoint_barrier_(new Barrier(0)) {
  CHECK(Monitor::IsValidLockWord(LockWord::FromThinLockId(kMaxThreadId, 1, 0U)));
}

ThreadList::~ThreadList() {
  CHECK(shut_down_);
}

void ThreadList::ShutDown() {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  // Detach the current thread if necessary. If we failed to start, there might not be any threads.
  // We need to detach the current thread here in case there's another thread waiting to join with
  // us.
  bool contains = false;
  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    contains = Contains(self);
  }
  if (contains) {
    Runtime::Current()->DetachCurrentThread();
  }
  WaitForOtherNonDaemonThreadsToExit();
  // The only caller of this function, ~Runtime, has already disabled GC and
  // ensured that the last GC is finished.
  gc::Heap* const heap = Runtime::Current()->GetHeap();
  CHECK(heap->IsGCDisabledForShutdown());

  // TODO: there's an unaddressed race here where a thread may attach during shutdown, see
  //       Thread::Init.
  SuspendAllDaemonThreadsForShutdown();

  shut_down_ = true;
}

bool ThreadList::Contains(Thread* thread) {
  return find(list_.begin(), list_.end(), thread) != list_.end();
}

pid_t ThreadList::GetLockOwner() {
  return Locks::thread_list_lock_->GetExclusiveOwnerTid();
}

void ThreadList::DumpNativeStacks(std::ostream& os) {
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  unwindstack::AndroidLocalUnwinder unwinder;
  for (const auto& thread : list_) {
    os << "DUMPING THREAD " << thread->GetTid() << "\n";
    DumpNativeStack(os, unwinder, thread->GetTid(), "\t");
    os << "\n";
  }
}

void ThreadList::DumpForSigQuit(std::ostream& os) {
  {
    ScopedObjectAccess soa(Thread::Current());
    // Only print if we have samples.
    if (suspend_all_histogram_.SampleSize() > 0) {
      Histogram<uint64_t>::CumulativeData data;
      suspend_all_histogram_.CreateHistogram(&data);
      suspend_all_histogram_.PrintConfidenceIntervals(os, 0.99, data);  // Dump time to suspend.
    }
  }
  bool dump_native_stack = Runtime::Current()->GetDumpNativeStackOnSigQuit();
  Dump(os, dump_native_stack);
  DumpUnattachedThreads(os, dump_native_stack && kDumpUnattachedThreadNativeStackForSigQuit);
}

static void DumpUnattachedThread(std::ostream& os, pid_t tid, bool dump_native_stack)
    NO_THREAD_SAFETY_ANALYSIS {
  // TODO: No thread safety analysis as DumpState with a null thread won't access fields, should
  // refactor DumpState to avoid skipping analysis.
  Thread::DumpState(os, nullptr, tid);
  if (dump_native_stack) {
    DumpNativeStack(os, tid, "  native: ");
  }
  os << std::endl;
}

void ThreadList::DumpUnattachedThreads(std::ostream& os, bool dump_native_stack) {
  DIR* d = opendir("/proc/self/task");
  if (!d) {
    return;
  }

  Thread* self = Thread::Current();
  dirent* e;
  while ((e = readdir(d)) != nullptr) {
    char* end;
    pid_t tid = strtol(e->d_name, &end, 10);
    if (!*end) {
      Thread* thread;
      {
        MutexLock mu(self, *Locks::thread_list_lock_);
        thread = FindThreadByTid(tid);
      }
      if (thread == nullptr) {
        DumpUnattachedThread(os, tid, dump_native_stack);
      }
    }
  }
  closedir(d);
}

// Dump checkpoint timeout in milliseconds. Larger amount on the target, since the device could be
// overloaded with ANR dumps.
static constexpr uint32_t kDumpWaitTimeout = kIsTargetBuild ? 100000 : 20000;

// A closure used by Thread::Dump.
class DumpCheckpoint final : public Closure {
 public:
  DumpCheckpoint(bool dump_native_stack)
      : lock_("Dump checkpoint lock", kGenericBottomLock),
        os_(),
        // Avoid verifying count in case a thread doesn't end up passing through the barrier.
        // This avoids a SIGABRT that would otherwise happen in the destructor.
        barrier_(0, /*verify_count_on_shutdown=*/false),
        unwinder_(std::vector<std::string>{}, std::vector<std::string> {"oat", "odex"}),
        dump_native_stack_(dump_native_stack) {
  }

  void Run(Thread* thread) override {
    // Note thread and self may not be equal if thread was already suspended at the point of the
    // request.
    Thread* self = Thread::Current();
    CHECK(self != nullptr);
    std::ostringstream local_os;
    Thread::DumpOrder dump_order;
    {
      ScopedObjectAccess soa(self);
      dump_order = thread->Dump(local_os, unwinder_, dump_native_stack_);
    }
    {
      MutexLock mu(self, lock_);
      // Sort, so that the most interesting threads for ANR are printed first (ANRs can be trimmed).
      std::pair<Thread::DumpOrder, uint32_t> sort_key(dump_order, thread->GetThreadId());
      os_.emplace(sort_key, std::move(local_os));
    }
    barrier_.Pass(self);
  }

  // Called at the end to print all the dumps in sequential prioritized order.
  void Dump(Thread* self, std::ostream& os) {
    MutexLock mu(self, lock_);
    for (const auto& it : os_) {
      os << it.second.str() << std::endl;
    }
  }

  void WaitForThreadsToRunThroughCheckpoint(size_t threads_running_checkpoint) {
    Thread* self = Thread::Current();
    ScopedThreadStateChange tsc(self, ThreadState::kWaitingForCheckPointsToRun);
    bool timed_out = barrier_.Increment(self, threads_running_checkpoint, kDumpWaitTimeout);
    if (timed_out) {
      // Avoid a recursive abort.
      LOG((kIsDebugBuild && (gAborting == 0)) ? ::android::base::FATAL : ::android::base::ERROR)
          << "Unexpected time out during dump checkpoint.";
    }
  }

 private:
  // Storage for the per-thread dumps (guarded by lock since they are generated in parallel).
  // Map is used to obtain sorted order. The key is unique, but use multimap just in case.
  Mutex lock_;
  std::multimap<std::pair<Thread::DumpOrder, uint32_t>, std::ostringstream> os_ GUARDED_BY(lock_);
  // The barrier to be passed through and for the requestor to wait upon.
  Barrier barrier_;
  // A backtrace map, so that all threads use a shared info and don't reacquire/parse separately.
  unwindstack::AndroidLocalUnwinder unwinder_;
  // Whether we should dump the native stack.
  const bool dump_native_stack_;
};

void ThreadList::Dump(std::ostream& os, bool dump_native_stack) {
  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    os << "DALVIK THREADS (" << list_.size() << "):\n";
  }
  if (self != nullptr) {
    DumpCheckpoint checkpoint(dump_native_stack);
    size_t threads_running_checkpoint;
    {
      // Use SOA to prevent deadlocks if multiple threads are calling Dump() at the same time.
      ScopedObjectAccess soa(self);
      threads_running_checkpoint = RunCheckpoint(&checkpoint);
    }
    if (threads_running_checkpoint != 0) {
      checkpoint.WaitForThreadsToRunThroughCheckpoint(threads_running_checkpoint);
    }
    checkpoint.Dump(self, os);
  } else {
    DumpUnattachedThreads(os, dump_native_stack);
  }
}

void ThreadList::AssertOtherThreadsAreSuspended(Thread* self) {
  MutexLock mu(self, *Locks::thread_list_lock_);
  MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
  for (const auto& thread : list_) {
    if (thread != self) {
      CHECK(thread->IsSuspended())
            << "\nUnsuspended thread: <<" << *thread << "\n"
            << "self: <<" << *Thread::Current();
    }
  }
}

#if HAVE_TIMED_RWLOCK
// Attempt to rectify locks so that we dump thread list with required locks before exiting.
NO_RETURN static void UnsafeLogFatalForThreadSuspendAllTimeout() {
  // Increment gAborting before doing the thread list dump since we don't want any failures from
  // AssertThreadSuspensionIsAllowable in cases where thread suspension is not allowed.
  // See b/69044468.
  ++gAborting;
  Runtime* runtime = Runtime::Current();
  std::ostringstream ss;
  ss << "Thread suspend timeout\n";
  Locks::mutator_lock_->Dump(ss);
  ss << "\n";
  runtime->GetThreadList()->Dump(ss);
  --gAborting;
  LOG(FATAL) << ss.str();
  exit(0);
}
#endif

size_t ThreadList::RunCheckpoint(Closure* checkpoint_function,
                                 Closure* callback,
                                 bool allowLockChecking) {
  Thread* self = Thread::Current();
  Locks::mutator_lock_->AssertNotExclusiveHeld(self);
  Locks::thread_list_lock_->AssertNotHeld(self);
  Locks::thread_suspend_count_lock_->AssertNotHeld(self);

  std::vector<Thread*> suspended_count_modified_threads;
  size_t count = 0;
  {
    // Call a checkpoint function for each thread. We directly invoke the function on behalf of
    // suspended threads.
    MutexLock mu(self, *Locks::thread_list_lock_);
    if (kIsDebugBuild && allowLockChecking) {
      self->DisallowPreMonitorMutexes();
    }
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    count = list_.size();
    for (const auto& thread : list_) {
      if (thread != self) {
        bool requested_suspend = false;
        while (true) {
          if (thread->RequestCheckpoint(checkpoint_function)) {
            // This thread will run its checkpoint some time in the near future.
            if (requested_suspend) {
              // The suspend request is now unnecessary.
              thread->DecrementSuspendCount(self);
              requested_suspend = false;
            }
            break;
          } else {
            // The thread is probably suspended.
            if (!requested_suspend) {
              // Since we don't suspend other threads to run checkpoint_function, we claim this is
              // safe even with flip_function set.
              // This does not risk suspension cycles: We may have a pending suspension request,
              // but it cannot block us: Checkpoint Run() functions may not suspend, thus we cannot
              // be blocked from decrementing the count again.
              thread->IncrementSuspendCount(self);
              requested_suspend = true;
            }
            if (thread->IsSuspended()) {
              // We saw it suspended after incrementing suspend count, so it will stay that way.
              break;
            }
          }
        }
        if (requested_suspend) {
          suspended_count_modified_threads.push_back(thread);
        }
      }
      // Thread either has honored or will honor the checkpoint, or it has been added to
      // suspended_count_modified_threads.
    }
    // Run the callback to be called inside this critical section.
    if (callback != nullptr) {
      callback->Run(self);
    }
  }

  // Run the checkpoint on ourself while we wait for threads to suspend.
  checkpoint_function->Run(self);

  // Run the checkpoint on the suspended threads.
  for (const auto& thread : suspended_count_modified_threads) {
    // We know for sure that the thread is suspended at this point.
    DCHECK(thread->IsSuspended());
    checkpoint_function->Run(thread);
    {
      MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
      thread->DecrementSuspendCount(self);
    }
  }

  {
    // Imitate ResumeAll, threads may be waiting on Thread::resume_cond_ since we raised their
    // suspend count. Now the suspend_count_ is lowered so we must do the broadcast.
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    Thread::resume_cond_->Broadcast(self);
  }

  if (kIsDebugBuild && allowLockChecking) {
    self->AllowPreMonitorMutexes();
  }
  return count;
}

void ThreadList::RunEmptyCheckpoint() {
  Thread* self = Thread::Current();
  Locks::mutator_lock_->AssertNotExclusiveHeld(self);
  Locks::thread_list_lock_->AssertNotHeld(self);
  Locks::thread_suspend_count_lock_->AssertNotHeld(self);
  std::vector<uint32_t> runnable_thread_ids;
  size_t count = 0;
  Barrier* barrier = empty_checkpoint_barrier_.get();
  barrier->Init(self, 0);
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    for (Thread* thread : list_) {
      if (thread != self) {
        while (true) {
          if (thread->RequestEmptyCheckpoint()) {
            // This thread will run an empty checkpoint (decrement the empty checkpoint barrier)
            // some time in the near future.
            ++count;
            if (kIsDebugBuild) {
              runnable_thread_ids.push_back(thread->GetThreadId());
            }
            break;
          }
          if (thread->GetState() != ThreadState::kRunnable) {
            // It's seen suspended, we are done because it must not be in the middle of a mutator
            // heap access.
            break;
          }
        }
      }
    }
  }

  // Wake up the threads blocking for weak ref access so that they will respond to the empty
  // checkpoint request. Otherwise we will hang as they are blocking in the kRunnable state.
  Runtime::Current()->GetHeap()->GetReferenceProcessor()->BroadcastForSlowPath(self);
  Runtime::Current()->BroadcastForNewSystemWeaks(/*broadcast_for_checkpoint=*/true);
  {
    ScopedThreadStateChange tsc(self, ThreadState::kWaitingForCheckPointsToRun);
    uint64_t total_wait_time = 0;
    bool first_iter = true;
    while (true) {
      // Wake up the runnable threads blocked on the mutexes that another thread, which is blocked
      // on a weak ref access, holds (indirectly blocking for weak ref access through another thread
      // and a mutex.) This needs to be done periodically because the thread may be preempted
      // between the CheckEmptyCheckpointFromMutex call and the subsequent futex wait in
      // Mutex::ExclusiveLock, etc. when the wakeup via WakeupToRespondToEmptyCheckpoint
      // arrives. This could cause a *very rare* deadlock, if not repeated. Most of the cases are
      // handled in the first iteration.
      for (BaseMutex* mutex : Locks::expected_mutexes_on_weak_ref_access_) {
        mutex->WakeupToRespondToEmptyCheckpoint();
      }
      static constexpr uint64_t kEmptyCheckpointPeriodicTimeoutMs = 100;  // 100ms
      static constexpr uint64_t kEmptyCheckpointTotalTimeoutMs = 600 * 1000;  // 10 minutes.
      size_t barrier_count = first_iter ? count : 0;
      first_iter = false;  // Don't add to the barrier count from the second iteration on.
      bool timed_out = barrier->Increment(self, barrier_count, kEmptyCheckpointPeriodicTimeoutMs);
      if (!timed_out) {
        break;  // Success
      }
      // This is a very rare case.
      total_wait_time += kEmptyCheckpointPeriodicTimeoutMs;
      if (kIsDebugBuild && total_wait_time > kEmptyCheckpointTotalTimeoutMs) {
        std::ostringstream ss;
        ss << "Empty checkpoint timeout\n";
        ss << "Barrier count " << barrier->GetCount(self) << "\n";
        ss << "Runnable thread IDs";
        for (uint32_t tid : runnable_thread_ids) {
          ss << " " << tid;
        }
        ss << "\n";
        Locks::mutator_lock_->Dump(ss);
        ss << "\n";
        LOG(FATAL_WITHOUT_ABORT) << ss.str();
        // Some threads in 'runnable_thread_ids' are probably stuck. Try to dump their stacks.
        // Avoid using ThreadList::Dump() initially because it is likely to get stuck as well.
        {
          ScopedObjectAccess soa(self);
          MutexLock mu1(self, *Locks::thread_list_lock_);
          for (Thread* thread : GetList()) {
            uint32_t tid = thread->GetThreadId();
            bool is_in_runnable_thread_ids =
                std::find(runnable_thread_ids.begin(), runnable_thread_ids.end(), tid) !=
                runnable_thread_ids.end();
            if (is_in_runnable_thread_ids &&
                thread->ReadFlag(ThreadFlag::kEmptyCheckpointRequest)) {
              // Found a runnable thread that hasn't responded to the empty checkpoint request.
              // Assume it's stuck and safe to dump its stack.
              thread->Dump(LOG_STREAM(FATAL_WITHOUT_ABORT),
                           /*dump_native_stack=*/ true,
                           /*force_dump_stack=*/ true);
            }
          }
        }
        LOG(FATAL_WITHOUT_ABORT)
            << "Dumped runnable threads that haven't responded to empty checkpoint.";
        // Now use ThreadList::Dump() to dump more threads, noting it may get stuck.
        Dump(LOG_STREAM(FATAL_WITHOUT_ABORT));
        LOG(FATAL) << "Dumped all threads.";
      }
    }
  }
}

// A checkpoint/suspend-all hybrid to switch thread roots from
// from-space to to-space refs. Used to synchronize threads at a point
// to mark the initiation of marking while maintaining the to-space
// invariant.
size_t ThreadList::FlipThreadRoots(Closure* thread_flip_visitor,
                                   Closure* flip_callback,
                                   gc::collector::GarbageCollector* collector,
                                   gc::GcPauseListener* pause_listener) {
  TimingLogger::ScopedTiming split("ThreadListFlip", collector->GetTimings());
  Thread* self = Thread::Current();
  Locks::mutator_lock_->AssertNotHeld(self);
  Locks::thread_list_lock_->AssertNotHeld(self);
  Locks::thread_suspend_count_lock_->AssertNotHeld(self);
  CHECK_NE(self->GetState(), ThreadState::kRunnable);

  collector->GetHeap()->ThreadFlipBegin(self);  // Sync with JNI critical calls.

  // ThreadFlipBegin happens before we suspend all the threads, so it does not count towards the
  // pause.
  const uint64_t suspend_start_time = NanoTime();
  SuspendAllInternal(self);
  if (pause_listener != nullptr) {
    pause_listener->StartPause();
  }

  // Run the flip callback for the collector.
  Locks::mutator_lock_->ExclusiveLock(self);
  suspend_all_histogram_.AdjustAndAddValue(NanoTime() - suspend_start_time);
  flip_callback->Run(self);
  Locks::mutator_lock_->ExclusiveUnlock(self);
  collector->RegisterPause(NanoTime() - suspend_start_time);
  if (pause_listener != nullptr) {
    pause_listener->EndPause();
  }

  // Resume runnable threads.
  size_t runnable_thread_count = 0;
  std::vector<Thread*> other_threads;
  {
    TimingLogger::ScopedTiming split2("ResumeRunnableThreads", collector->GetTimings());
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    --suspend_all_count_;
    for (Thread* thread : list_) {
      // Set the flip function for all threads because once we start resuming any threads,
      // they may need to run the flip function on behalf of other threads, even this one.
      thread->SetFlipFunction(thread_flip_visitor);
      if (thread == self) {
        continue;
      }
      // Resume early the threads that were runnable but are suspended just for this thread flip or
      // about to transition from non-runnable (eg. kNative at the SOA entry in a JNI function) to
      // runnable (both cases waiting inside Thread::TransitionFromSuspendedToRunnable), or waiting
      // for the thread flip to end at the JNI critical section entry (kWaitingForGcThreadFlip),
      ThreadState state = thread->GetState();
      if ((state == ThreadState::kWaitingForGcThreadFlip || thread->IsTransitioningToRunnable()) &&
          thread->GetSuspendCount() == 1) {
        // The thread will resume right after the broadcast.
        thread->DecrementSuspendCount(self);
        ++runnable_thread_count;
      } else {
        other_threads.push_back(thread);
      }
    }
    Thread::resume_cond_->Broadcast(self);
  }

  collector->GetHeap()->ThreadFlipEnd(self);

  // Try to run the closure on the other threads.
  {
    TimingLogger::ScopedTiming split3("FlipOtherThreads", collector->GetTimings());
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    for (Thread* thread : other_threads) {
      thread->EnsureFlipFunctionStarted(self);
      DCHECK(!thread->ReadFlag(ThreadFlag::kPendingFlipFunction));
    }
    // Try to run the flip function for self.
    self->EnsureFlipFunctionStarted(self);
    DCHECK(!self->ReadFlag(ThreadFlag::kPendingFlipFunction));
  }

  // Resume other threads.
  {
    TimingLogger::ScopedTiming split4("ResumeOtherThreads", collector->GetTimings());
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    for (const auto& thread : other_threads) {
      thread->DecrementSuspendCount(self);
    }
    Thread::resume_cond_->Broadcast(self);
  }

  return runnable_thread_count + other_threads.size() + 1;  // +1 for self.
}

bool ThreadList::WaitForSuspendBarrier(AtomicInteger* barrier) {
#if ART_USE_FUTEXES
  timespec wait_timeout;
  InitTimeSpec(false, CLOCK_MONOTONIC, NsToMs(thread_suspend_timeout_ns_), 0, &wait_timeout);
#endif
  while (true) {
    int32_t cur_val = barrier->load(std::memory_order_acquire);
    if (cur_val <= 0) {
      CHECK_EQ(cur_val, 0);
      return true;
    }
#if ART_USE_FUTEXES
    if (futex(barrier->Address(), FUTEX_WAIT_PRIVATE, cur_val, &wait_timeout, nullptr, 0) != 0) {
      if (errno == ETIMEDOUT) {
        return false;
      } else if (errno != EAGAIN && errno != EINTR) {
        PLOG(FATAL) << "futex wait for suspend barrier failed";
      }
    }
#endif
    // Else spin wait. This is likely to be slow, but ART_USE_FUTEXES is set on Linux,
    // including all targets.
  }
}

void ThreadList::SuspendAll(const char* cause, bool long_suspend) {
  Thread* self = Thread::Current();

  if (self != nullptr) {
    VLOG(threads) << *self << " SuspendAll for " << cause << " starting...";
  } else {
    VLOG(threads) << "Thread[null] SuspendAll for " << cause << " starting...";
  }
  {
    ScopedTrace trace("Suspending mutator threads");
    const uint64_t start_time = NanoTime();

    SuspendAllInternal(self);
    // All threads are known to have suspended (but a thread may still own the mutator lock)
    // Make sure this thread grabs exclusive access to the mutator lock and its protected data.
#if HAVE_TIMED_RWLOCK
    while (true) {
      if (Locks::mutator_lock_->ExclusiveLockWithTimeout(self,
                                                         NsToMs(thread_suspend_timeout_ns_),
                                                         0)) {
        break;
      } else if (!long_suspend_) {
        // Reading long_suspend without the mutator lock is slightly racy, in some rare cases, this
        // could result in a thread suspend timeout.
        // Timeout if we wait more than thread_suspend_timeout_ns_ nanoseconds.
        UnsafeLogFatalForThreadSuspendAllTimeout();
      }
    }
#else
    Locks::mutator_lock_->ExclusiveLock(self);
#endif

    long_suspend_ = long_suspend;

    const uint64_t end_time = NanoTime();
    const uint64_t suspend_time = end_time - start_time;
    suspend_all_histogram_.AdjustAndAddValue(suspend_time);
    if (suspend_time > kLongThreadSuspendThreshold) {
      LOG(WARNING) << "Suspending all threads took: " << PrettyDuration(suspend_time);
    }

    if (kDebugLocking) {
      // Debug check that all threads are suspended.
      AssertOtherThreadsAreSuspended(self);
    }
  }
  ATraceBegin((std::string("Mutator threads suspended for ") + cause).c_str());

  if (self != nullptr) {
    VLOG(threads) << *self << " SuspendAll complete";
  } else {
    VLOG(threads) << "Thread[null] SuspendAll complete";
  }
}

// Only used for kIsDebugBuild statistics.
struct DelayInfo {
  DelayInfo() : in_flip_(0), suspend_all_(0), self_susp_(0) {}
  int in_flip_;
  int suspend_all_;
  int self_susp_;
};

static inline std::ostream& operator<<(std::ostream& os, DelayInfo di) {
  return os << " in_flip =" << di.in_flip_ << " suspend_all = " << di.suspend_all_
            << " self_suspend = " << di.self_susp_;
}

static inline bool TrackIfFalse(bool cond, int* field) {
  if (kIsDebugBuild && !cond) {
    ++*field;
  }
  return cond;
}

// Ensures all threads running Java suspend and that those not running Java don't start.
void ThreadList::SuspendAllInternal(Thread* self, SuspendReason reason) {
  const uint64_t start_time = NanoTime();
  Locks::mutator_lock_->AssertNotExclusiveHeld(self);
  Locks::thread_list_lock_->AssertNotHeld(self);
  Locks::thread_suspend_count_lock_->AssertNotHeld(self);
  if (kDebugLocking && self != nullptr) {
    CHECK_NE(self->GetState(), ThreadState::kRunnable);
  }

  // First request that all threads suspend, then wait for them to suspend before
  // returning. This suspension scheme also relies on other behaviour:
  // 1. Threads cannot be deleted while they are suspended or have a suspend-
  //    request flag set - (see Unregister() below).
  // 2. When threads are created, they are created in a suspended state (actually
  //    kNative) and will never begin executing Java code without first checking
  //    the suspend-request flag.

  // The atomic counter for number of threads that need to pass the barrier.
  AtomicInteger pending_threads;

  DelayInfo di;
  for (int iter_count = 1;; ++iter_count) {
    {
      MutexLock mu(self, *Locks::thread_list_lock_);
      MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
      bool in_flip = false;
      for (const auto& thread : list_) {
        if (thread->GetFlipFunction() != nullptr) {
          in_flip = true;
          break;
        }
      }
      if (LIKELY(TrackIfFalse(suspend_all_count_ == 0, &di.suspend_all_) &&
                 TrackIfFalse(self == nullptr || self->GetSuspendCount() == 0, &di.self_susp_) &&
                 TrackIfFalse(!in_flip, &di.in_flip_))) {
        // The above condition remains valid while we hold thread_suspend_count_lock_ .
        bool found_myself = false;
        // Update global suspend all state for attaching threads.
        ++suspend_all_count_;
        pending_threads.store(list_.size() - (self == nullptr ? 0 : 1), std::memory_order_relaxed);
        // Increment everybody else's suspend count.
        for (const auto& thread : list_) {
          if (thread == self) {
            found_myself = true;
          } else {
            VLOG(threads) << "requesting thread suspend: " << *thread;
            DCHECK_EQ(suspend_all_count_, 1);
            thread->IncrementSuspendCount(self, &pending_threads, nullptr, reason);

            // Must install the pending_threads counter first, then check thread->IsSuspended()
            // and clear the counter. Otherwise there's a race with
            // Thread::TransitionFromRunnableToSuspended() that can lead a thread to miss a call
            // to PassActiveSuspendBarriers().
            if (thread->IsSuspended()) {
              // Effectively pass the barrier on behalf of the already suspended thread.
              DCHECK_EQ(thread->tlsPtr_.active_suspendall_barrier, &pending_threads);
              pending_threads.fetch_sub(1, std::memory_order_seq_cst);
              thread->tlsPtr_.active_suspendall_barrier = nullptr;
              if (!thread->HasActiveSuspendBarrier()) {
                thread->AtomicClearFlag(ThreadFlag::kActiveSuspendBarrier);
              }
            }
          }
        }
        DCHECK(self == nullptr || found_myself);
        break;
      }
    }
    // The if (LIKELY ...) condition above didn't hold.  This is a bad time to initiate a suspend.
    // Either the suspendall_barrier is already in use, or proceeding at this time risks deadlock.
    // See b/31683379 for an explanation of the thread flip condition. Note that in the event of a
    // competing Suspend or SuspendAll, we are about to be suspended anyway. We hold no locks,
    // so it's safe to sleep and retry.
    CHECK(iter_count <= kMaxSuspendRetries) << di;
    {
      MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
      DCHECK(suspend_all_count_ <= 1);
      if (suspend_all_count_ != 0) {
        // This may take a while, and we're not runnable, and thus would o.w. not block.
        Thread::resume_cond_->WaitHoldingLocks(self);
        continue;
      }
    }
    usleep(kThreadSuspendSleepUs);
    // We're already not runnable, so an attempt to suspend us should succeed.
  }

  if (!WaitForSuspendBarrier(&pending_threads)) {
    const uint64_t wait_time = NanoTime() - start_time;
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    std::ostringstream oss;
    for (const auto& thread : list_) {
      if (thread != self && !thread->IsSuspended()) {
        oss << std::endl << "Thread not suspended: " << *thread;
      }
    }
    LOG(::android::base::FATAL) << "Timed out waiting for threads to suspend, waited for "
                                << PrettyDuration(wait_time) << oss.str();
  }
}

void ThreadList::ResumeAll() {
  Thread* self = Thread::Current();

  if (self != nullptr) {
    VLOG(threads) << *self << " ResumeAll starting";
  } else {
    VLOG(threads) << "Thread[null] ResumeAll starting";
  }

  ATraceEnd();

  ScopedTrace trace("Resuming mutator threads");

  if (kDebugLocking) {
    // Debug check that all threads are suspended.
    AssertOtherThreadsAreSuspended(self);
  }

  long_suspend_ = false;

  Locks::mutator_lock_->ExclusiveUnlock(self);
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    // Update global suspend all state for attaching threads.
    --suspend_all_count_;
    // Decrement the suspend counts for all threads.
    for (const auto& thread : list_) {
      if (thread != self) {
        thread->DecrementSuspendCount(self);
      }
    }

    // Broadcast a notification to all suspended threads, some or all of
    // which may choose to wake up.  No need to wait for them.
    if (self != nullptr) {
      VLOG(threads) << *self << " ResumeAll waking others";
    } else {
      VLOG(threads) << "Thread[null] ResumeAll waking others";
    }
    Thread::resume_cond_->Broadcast(self);
  }

  if (self != nullptr) {
    VLOG(threads) << *self << " ResumeAll complete";
  } else {
    VLOG(threads) << "Thread[null] ResumeAll complete";
  }
}

bool ThreadList::Resume(Thread* thread, SuspendReason reason) {
  // This assumes there was an ATraceBegin when we suspended the thread.
  ATraceEnd();

  Thread* self = Thread::Current();
  DCHECK_NE(thread, self);
  VLOG(threads) << "Resume(" << reinterpret_cast<void*>(thread) << ") starting..." << reason;

  {
    // To check Contains.
    MutexLock mu(self, *Locks::thread_list_lock_);
    // To check IsSuspended.
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    if (UNLIKELY(!thread->IsSuspended())) {
      LOG(ERROR) << "Resume(" << reinterpret_cast<void*>(thread)
          << ") thread not suspended";
      return false;
    }
    if (!Contains(thread)) {
      // We only expect threads within the thread-list to have been suspended otherwise we can't
      // stop such threads from delete-ing themselves.
      LOG(ERROR) << "Resume(" << reinterpret_cast<void*>(thread)
          << ") thread not within thread list";
      return false;
    }
    thread->DecrementSuspendCount(self, reason);
  }

  {
    VLOG(threads) << "Resume(" << reinterpret_cast<void*>(thread) << ") waking others";
    MutexLock mu(self, *Locks::thread_suspend_count_lock_);
    Thread::resume_cond_->Broadcast(self);
  }

  VLOG(threads) << "Resume(" << reinterpret_cast<void*>(thread) << ") complete";
  return true;
}

static void ThreadSuspendByPeerWarning(ScopedObjectAccess& soa,
                                       LogSeverity severity,
                                       const char* message,
                                       jobject peer) REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Object> name =
      WellKnownClasses::java_lang_Thread_name->GetObject(soa.Decode<mirror::Object>(peer));
  if (name == nullptr) {
    LOG(severity) << message << ": " << peer;
  } else {
    LOG(severity) << message << ": " << peer << ":" << name->AsString()->ToModifiedUtf8();
  }
}

Thread* ThreadList::SuspendThreadByPeer(jobject peer, SuspendReason reason) {
  bool is_suspended = false;
  Thread* const self = Thread::Current();
  VLOG(threads) << "SuspendThreadByPeer starting";
  Thread* thread;
  WrappedSuspend1Barrier wrapped_barrier{};
  for (int iter_count = 1;; ++iter_count) {
    {
      // Note: this will transition to runnable and potentially suspend.
      ScopedObjectAccess soa(self);
      MutexLock thread_list_mu(self, *Locks::thread_list_lock_);
      thread = Thread::FromManagedThread(soa, peer);
      if (thread == nullptr) {
        ThreadSuspendByPeerWarning(soa,
                                   ::android::base::WARNING,
                                    "No such thread for suspend",
                                    peer);
        return nullptr;
      }
      if (!Contains(thread)) {
        VLOG(threads) << "SuspendThreadByPeer failed for unattached thread: "
            << reinterpret_cast<void*>(thread);
        return nullptr;
      }
      // IsSuspended on the current thread will fail as the current thread is changed into
      // Runnable above. As the suspend count is now raised if this is the current thread
      // it will self suspend on transition to Runnable, making it hard to work with. It's simpler
      // to just explicitly handle the current thread in the callers to this code.
      CHECK_NE(thread, self) << "Attempt to suspend the current thread for the debugger";
      VLOG(threads) << "SuspendThreadByPeer found thread: " << *thread;
      {
        MutexLock suspend_count_mu(self, *Locks::thread_suspend_count_lock_);
        if (LIKELY(self->GetSuspendCount() == 0 && thread->GetFlipFunction() == nullptr)) {
          thread->IncrementSuspendCount(self, nullptr, &wrapped_barrier, reason);
          if (thread->IsSuspended()) {
            // See the discussion in mutator_gc_coord.md for the race here.
            thread->RemoveFirstSuspend1Barrier();
            if (!thread->HasActiveSuspendBarrier()) {
              thread->AtomicClearFlag(ThreadFlag::kActiveSuspendBarrier);
            }
            is_suspended = true;
          }
          DCHECK_GT(thread->GetSuspendCount(), 0);
          break;
        }
        // Else we either hold the suspend count lock but another thread is trying to suspend us,
        // making it unsafe to try to suspend another thread in case we get a cycle.  Or we're
        // currently in the middle of a flip, and could otherwise encounter b/31683379. In either
        // case, start the loop again, which will allow this thread to be suspended.
      }
    }
    // All locks are released, and we should quickly exit the suspend-unfriendly state. Retry.
    CHECK(iter_count <= kMaxSuspendRetries);
    usleep(kThreadSuspendSleepUs);
  }
  // Now wait for target to decrement suspend barrier.
  if (is_suspended || WaitForSuspendBarrier(&wrapped_barrier.barrier_)) {
    // wrapped_barrier.barrier_ has been decremented and will no longer be accessed.
    VLOG(threads) << "SuspendThreadByPeer thread suspended: " << *thread;
    if (ATraceEnabled()) {
      std::string name;
      thread->GetThreadName(name);
      ATraceBegin(
          StringPrintf("SuspendThreadByPeer suspended %s for peer=%p", name.c_str(), peer).c_str());
    }
    return thread;
  } else {
    LOG(WARNING) << "Suspended thread state_and_flags: " << thread->StateAndFlagsAsHexString();
    // thread still has a pointer to wrapped_barrier. Returning and continuing would be unsafe
    // without additional cleanup.
    {
      ScopedObjectAccess soa(self);
      ThreadSuspendByPeerWarning(
          soa, ::android::base::FATAL, "SuspendThreadByPeer timed out", peer);
    }
    UNREACHABLE();
  }
}

static void ThreadSuspendByThreadIdWarning(LogSeverity severity,
                                           const char* message,
                                           uint32_t thread_id) {
  LOG(severity) << StringPrintf("%s: %d", message, thread_id);
}

Thread* ThreadList::SuspendThreadByThreadId(uint32_t thread_id, SuspendReason reason) {
  bool is_suspended = false;
  Thread* const self = Thread::Current();
  CHECK_NE(thread_id, kInvalidThreadId);
  VLOG(threads) << "SuspendThreadByThreadId starting";
  Thread* thread;
  WrappedSuspend1Barrier wrapped_barrier{};
  for (int iter_count = 1;; ++iter_count) {
    {
      // Note: this will transition to runnable and potentially suspend.
      ScopedObjectAccess soa(self);
      MutexLock thread_list_mu(self, *Locks::thread_list_lock_);
      thread = FindThreadByThreadId(thread_id);
      if (thread == nullptr) {
        // There's a race in inflating a lock and the owner giving up ownership and then dying.
        ThreadSuspendByThreadIdWarning(::android::base::WARNING,
                                       "No such thread id for suspend",
                                       thread_id);
        return nullptr;
      }
      DCHECK(Contains(thread));
      CHECK_NE(thread, self) << "Attempt to suspend the current thread for the debugger";
      VLOG(threads) << "SuspendThreadByThreadId found thread: " << *thread;
      {
        MutexLock suspend_count_mu(self, *Locks::thread_suspend_count_lock_);
        if (LIKELY(self->GetSuspendCount() == 0 && thread->GetFlipFunction() == nullptr)) {
          thread->IncrementSuspendCount(self, nullptr, &wrapped_barrier, reason);
          if (thread->IsSuspended()) {
            // See the discussion in mutator_gc_coord.md for the race here.
            thread->RemoveFirstSuspend1Barrier();
            if (!thread->HasActiveSuspendBarrier()) {
              thread->AtomicClearFlag(ThreadFlag::kActiveSuspendBarrier);
            }
            is_suspended = true;
          }
          DCHECK_GT(thread->GetSuspendCount(), 0);
          break;
        }
        // Else we either hold the suspend count lock but another thread is trying to suspend us,
        // making it unsafe to try to suspend another thread in case we get a cycle.  Or we're
        // currently in the middle of a flip, and could otherwise encounter b/31683379. In either
        // case, start the loop again, which will allow this thread to be suspended.
      }
    }
    // All locks are released, and we should quickly exit the suspend-unfriendly state. Retry.
    CHECK(iter_count <= kMaxSuspendRetries);
    usleep(kThreadSuspendSleepUs);
  }
  // Now wait for target to decrement suspend barrier.
  if (is_suspended || WaitForSuspendBarrier(&wrapped_barrier.barrier_)) {
    // wrapped_barrier.barrier_ has been decremented and will no longer be accessed.
    VLOG(threads) << "SuspendThreadByThreadId thread suspended: " << *thread;
    if (ATraceEnabled()) {
      std::string name;
      thread->GetThreadName(name);
      ATraceBegin(
          StringPrintf("SuspendThreadByPeer suspended %s for id=%d", name.c_str(), thread_id)
              .c_str());
    }
    return thread;
  } else {
    // thread still has a pointer to wrapped_barrier. Returning and continuing would be unsafe
    // without additional cleanup.
    ThreadSuspendByThreadIdWarning(
        ::android::base::FATAL, "SuspendThreadByThreadId timed out", thread_id);
    UNREACHABLE();
  }
}

Thread* ThreadList::FindThreadByThreadId(uint32_t thread_id) {
  for (const auto& thread : list_) {
    if (thread->GetThreadId() == thread_id) {
      return thread;
    }
  }
  return nullptr;
}

Thread* ThreadList::FindThreadByTid(int tid) {
  for (const auto& thread : list_) {
    if (thread->GetTid() == tid) {
      return thread;
    }
  }
  return nullptr;
}

void ThreadList::WaitForOtherNonDaemonThreadsToExit(bool check_no_birth) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  Thread* self = Thread::Current();
  Locks::mutator_lock_->AssertNotHeld(self);
  while (true) {
    Locks::runtime_shutdown_lock_->Lock(self);
    if (check_no_birth) {
      // No more threads can be born after we start to shutdown.
      CHECK(Runtime::Current()->IsShuttingDownLocked());
      CHECK_EQ(Runtime::Current()->NumberOfThreadsBeingBorn(), 0U);
    } else {
      if (Runtime::Current()->NumberOfThreadsBeingBorn() != 0U) {
        // Awkward. Shutdown_cond_ is private, but the only live thread may not be registered yet.
        // Fortunately, this is used mostly for testing, and not performance-critical.
        Locks::runtime_shutdown_lock_->Unlock(self);
        usleep(1000);
        continue;
      }
    }
    MutexLock mu(self, *Locks::thread_list_lock_);
    Locks::runtime_shutdown_lock_->Unlock(self);
    // Also wait for any threads that are unregistering to finish. This is required so that no
    // threads access the thread list after it is deleted. TODO: This may not work for user daemon
    // threads since they could unregister at the wrong time.
    bool done = unregistering_count_ == 0;
    if (done) {
      for (const auto& thread : list_) {
        if (thread != self && !thread->IsDaemon()) {
          done = false;
          break;
        }
      }
    }
    if (done) {
      break;
    }
    // Wait for another thread to exit before re-checking.
    Locks::thread_exit_cond_->Wait(self);
  }
}

void ThreadList::SuspendAllDaemonThreadsForShutdown() {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  Thread* self = Thread::Current();
  size_t daemons_left = 0;
  {
    // Tell all the daemons it's time to suspend.
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    for (const auto& thread : list_) {
      // This is only run after all non-daemon threads have exited, so the remainder should all be
      // daemons.
      CHECK(thread->IsDaemon()) << *thread;
      if (thread != self) {
        thread->IncrementSuspendCount(self);
        ++daemons_left;
      }
      // We are shutting down the runtime, set the JNI functions of all the JNIEnvs to be
      // the sleep forever one.
      thread->GetJniEnv()->SetFunctionsToRuntimeShutdownFunctions();
    }
  }
  if (daemons_left == 0) {
    // No threads left; safe to shut down.
    return;
  }
  // There is not a clean way to shut down if we have daemons left. We have no mechanism for
  // killing them and reclaiming thread stacks. We also have no mechanism for waiting until they
  // have truly finished touching the memory we are about to deallocate. We do the best we can with
  // timeouts.
  //
  // If we have any daemons left, wait until they are (a) suspended and (b) they are not stuck
  // in a place where they are about to access runtime state and are not in a runnable state.
  // We attempt to do the latter by just waiting long enough for things to
  // quiesce. Examples: Monitor code or waking up from a condition variable.
  //
  // Give the threads a chance to suspend, complaining if they're slow. (a)
  bool have_complained = false;
  static constexpr size_t kTimeoutMicroseconds = 2000 * 1000;
  static constexpr size_t kSleepMicroseconds = 1000;
  bool all_suspended = false;
  for (size_t i = 0; !all_suspended && i < kTimeoutMicroseconds / kSleepMicroseconds; ++i) {
    bool found_running = false;
    {
      MutexLock mu(self, *Locks::thread_list_lock_);
      for (const auto& thread : list_) {
        if (thread != self && thread->GetState() == ThreadState::kRunnable) {
          if (!have_complained) {
            LOG(WARNING) << "daemon thread not yet suspended: " << *thread;
            have_complained = true;
          }
          found_running = true;
        }
      }
    }
    if (found_running) {
      // Sleep briefly before checking again. Max total sleep time is kTimeoutMicroseconds.
      usleep(kSleepMicroseconds);
    } else {
      all_suspended = true;
    }
  }
  if (!all_suspended) {
    // We can get here if a daemon thread executed a fastnative native call, so that it
    // remained in runnable state, and then made a JNI call after we called
    // SetFunctionsToRuntimeShutdownFunctions(), causing it to permanently stay in a harmless
    // but runnable state. See b/147804269 .
    LOG(WARNING) << "timed out suspending all daemon threads";
  }
  // Assume all threads are either suspended or somehow wedged.
  // Wait again for all the now "suspended" threads to actually quiesce. (b)
  static constexpr size_t kDaemonSleepTime = 400'000;
  usleep(kDaemonSleepTime);
  std::list<Thread*> list_copy;
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    // Half-way through the wait, set the "runtime deleted" flag, causing any newly awoken
    // threads to immediately go back to sleep without touching memory. This prevents us from
    // touching deallocated memory, but it also prevents mutexes from getting released. Thus we
    // only do this once we're reasonably sure that no system mutexes are still held.
    for (const auto& thread : list_) {
      DCHECK(thread == self || !all_suspended || thread->GetState() != ThreadState::kRunnable);
      // In the !all_suspended case, the target is probably sleeping.
      thread->GetJniEnv()->SetRuntimeDeleted();
      // Possibly contended Mutex acquisitions are unsafe after this.
      // Releasing thread_list_lock_ is OK, since it can't block.
    }
  }
  // Finally wait for any threads woken before we set the "runtime deleted" flags to finish
  // touching memory.
  usleep(kDaemonSleepTime);
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(hwaddress_sanitizer)
  // Sleep a bit longer with -fsanitize=address, since everything is slower.
  usleep(2 * kDaemonSleepTime);
#endif
#endif
  // At this point no threads should be touching our data structures anymore.
}

void ThreadList::Register(Thread* self) {
  DCHECK_EQ(self, Thread::Current());
  CHECK(!shut_down_);

  if (VLOG_IS_ON(threads)) {
    std::ostringstream oss;
    self->ShortDump(oss);  // We don't hold the mutator_lock_ yet and so cannot call Dump.
    LOG(INFO) << "ThreadList::Register() " << *self  << "\n" << oss.str();
  }

  // Atomically add self to the thread list and make its thread_suspend_count_ reflect ongoing
  // SuspendAll requests.
  MutexLock mu(self, *Locks::thread_list_lock_);
  MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
  if (suspend_all_count_ == 1) {
    self->IncrementSuspendCount(self);
  } else {
    DCHECK_EQ(suspend_all_count_, 0);
  }
  CHECK(!Contains(self));
  list_.push_back(self);
  if (gUseReadBarrier) {
    gc::collector::ConcurrentCopying* const cc =
        Runtime::Current()->GetHeap()->ConcurrentCopyingCollector();
    // Initialize according to the state of the CC collector.
    self->SetIsGcMarkingAndUpdateEntrypoints(cc->IsMarking());
    if (cc->IsUsingReadBarrierEntrypoints()) {
      self->SetReadBarrierEntrypoints();
    }
    self->SetWeakRefAccessEnabled(cc->IsWeakRefAccessEnabled());
  }
}

void ThreadList::Unregister(Thread* self, bool should_run_callbacks) {
  DCHECK_EQ(self, Thread::Current());
  CHECK_NE(self->GetState(), ThreadState::kRunnable);
  Locks::mutator_lock_->AssertNotHeld(self);
  if (self->tls32_.disable_thread_flip_count != 0) {
    LOG(FATAL) << "Incomplete PrimitiveArrayCritical section at exit: " << *self << "count = "
               << self->tls32_.disable_thread_flip_count;
  }

  VLOG(threads) << "ThreadList::Unregister() " << *self;

  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    ++unregistering_count_;
  }

  // Any time-consuming destruction, plus anything that can call back into managed code or
  // suspend and so on, must happen at this point, and not in ~Thread. The self->Destroy is what
  // causes the threads to join. It is important to do this after incrementing unregistering_count_
  // since we want the runtime to wait for the daemon threads to exit before deleting the thread
  // list.
  self->Destroy(should_run_callbacks);

  // If tracing, remember thread id and name before thread exits.
  Trace::StoreExitingThreadInfo(self);

  uint32_t thin_lock_id = self->GetThreadId();
  while (true) {
    // Remove and delete the Thread* while holding the thread_list_lock_ and
    // thread_suspend_count_lock_ so that the unregistering thread cannot be suspended.
    // Note: deliberately not using MutexLock that could hold a stale self pointer.
    {
      MutexLock mu(self, *Locks::thread_list_lock_);
      if (!Contains(self)) {
        std::string thread_name;
        self->GetThreadName(thread_name);
        std::ostringstream os;
        DumpNativeStack(os, GetTid(), "  native: ", nullptr);
        LOG(ERROR) << "Request to unregister unattached thread " << thread_name << "\n" << os.str();
        break;
      } else {
        MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
        if (!self->IsSuspended()) {
          list_.remove(self);
          break;
        }
      }
    }
    // In the case where we are not suspended yet, sleep to leave other threads time to execute.
    // This is important if there are realtime threads. b/111277984
    usleep(1);
    // We failed to remove the thread due to a suspend request, loop and try again.
  }
  delete self;

  // Release the thread ID after the thread is finished and deleted to avoid cases where we can
  // temporarily have multiple threads with the same thread id. When this occurs, it causes
  // problems in FindThreadByThreadId / SuspendThreadByThreadId.
  ReleaseThreadId(nullptr, thin_lock_id);

  // Clear the TLS data, so that the underlying native thread is recognizably detached.
  // (It may wish to reattach later.)
#ifdef __BIONIC__
  __get_tls()[TLS_SLOT_ART_THREAD_SELF] = nullptr;
#else
  CHECK_PTHREAD_CALL(pthread_setspecific, (Thread::pthread_key_self_, nullptr), "detach self");
  Thread::self_tls_ = nullptr;
#endif

  // Signal that a thread just detached.
  MutexLock mu(nullptr, *Locks::thread_list_lock_);
  --unregistering_count_;
  Locks::thread_exit_cond_->Broadcast(nullptr);
}

void ThreadList::ForEach(void (*callback)(Thread*, void*), void* context) {
  for (const auto& thread : list_) {
    callback(thread, context);
  }
}

void ThreadList::VisitRootsForSuspendedThreads(RootVisitor* visitor) {
  Thread* const self = Thread::Current();
  std::vector<Thread*> threads_to_visit;

  // Tell threads to suspend and copy them into list.
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    for (Thread* thread : list_) {
      thread->IncrementSuspendCount(self);
      if (thread == self || thread->IsSuspended()) {
        threads_to_visit.push_back(thread);
      } else {
        thread->DecrementSuspendCount(self);
      }
    }
  }

  // Visit roots without holding thread_list_lock_ and thread_suspend_count_lock_ to prevent lock
  // order violations.
  for (Thread* thread : threads_to_visit) {
    thread->VisitRoots(visitor, kVisitRootFlagAllRoots);
  }

  // Restore suspend counts.
  {
    MutexLock mu2(self, *Locks::thread_suspend_count_lock_);
    for (Thread* thread : threads_to_visit) {
      thread->DecrementSuspendCount(self);
    }
  }
}

void ThreadList::VisitRoots(RootVisitor* visitor, VisitRootFlags flags) const {
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  for (const auto& thread : list_) {
    thread->VisitRoots(visitor, flags);
  }
}

void ThreadList::VisitReflectiveTargets(ReflectiveValueVisitor *visitor) const {
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  for (const auto& thread : list_) {
    thread->VisitReflectiveTargets(visitor);
  }
}

void ThreadList::SweepInterpreterCaches(IsMarkedVisitor* visitor) const {
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  for (const auto& thread : list_) {
    thread->SweepInterpreterCache(visitor);
  }
}

uint32_t ThreadList::AllocThreadId(Thread* self) {
  MutexLock mu(self, *Locks::allocated_thread_ids_lock_);
  for (size_t i = 0; i < allocated_ids_.size(); ++i) {
    if (!allocated_ids_[i]) {
      allocated_ids_.set(i);
      return i + 1;  // Zero is reserved to mean "invalid".
    }
  }
  LOG(FATAL) << "Out of internal thread ids";
  UNREACHABLE();
}

void ThreadList::ReleaseThreadId(Thread* self, uint32_t id) {
  MutexLock mu(self, *Locks::allocated_thread_ids_lock_);
  --id;  // Zero is reserved to mean "invalid".
  DCHECK(allocated_ids_[id]) << id;
  allocated_ids_.reset(id);
}

ScopedSuspendAll::ScopedSuspendAll(const char* cause, bool long_suspend) {
  Runtime::Current()->GetThreadList()->SuspendAll(cause, long_suspend);
}

ScopedSuspendAll::~ScopedSuspendAll() {
  Runtime::Current()->GetThreadList()->ResumeAll();
}

}  // namespace art
