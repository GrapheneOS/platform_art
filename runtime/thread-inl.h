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

#ifndef ART_RUNTIME_THREAD_INL_H_
#define ART_RUNTIME_THREAD_INL_H_

#include "thread.h"

#include "arch/instruction_set.h"
#include "base/aborting.h"
#include "base/casts.h"
#include "base/mutex-inl.h"
#include "base/time_utils.h"
#include "jni/jni_env_ext.h"
#include "managed_stack-inl.h"
#include "obj_ptr.h"
#include "suspend_reason.h"
#include "thread-current-inl.h"
#include "thread_pool.h"

namespace art {

// Quickly access the current thread from a JNIEnv.
static inline Thread* ThreadForEnv(JNIEnv* env) {
  JNIEnvExt* full_env(down_cast<JNIEnvExt*>(env));
  return full_env->GetSelf();
}

inline void Thread::AllowThreadSuspension() {
  CheckSuspend();
  // Invalidate the current thread's object pointers (ObjPtr) to catch possible moving GC bugs due
  // to missing handles.
  PoisonObjectPointers();
}

inline void Thread::CheckSuspend(bool implicit) {
  DCHECK_EQ(Thread::Current(), this);
  while (true) {
    StateAndFlags state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
    if (LIKELY(!state_and_flags.IsAnyOfFlagsSet(SuspendOrCheckpointRequestFlags()))) {
      break;
    } else if (state_and_flags.IsFlagSet(ThreadFlag::kCheckpointRequest)) {
      RunCheckpointFunction();
    } else if (state_and_flags.IsFlagSet(ThreadFlag::kSuspendRequest)) {
      FullSuspendCheck(implicit);
      implicit = false;  // We do not need to `MadviseAwayAlternateSignalStack()` anymore.
    } else {
      DCHECK(state_and_flags.IsFlagSet(ThreadFlag::kEmptyCheckpointRequest));
      RunEmptyCheckpoint();
    }
  }
  if (implicit) {
    // For implicit suspend check we want to `madvise()` away
    // the alternate signal stack to avoid wasting memory.
    MadviseAwayAlternateSignalStack();
  }
}

inline void Thread::CheckEmptyCheckpointFromWeakRefAccess(BaseMutex* cond_var_mutex) {
  Thread* self = Thread::Current();
  DCHECK_EQ(self, this);
  for (;;) {
    if (ReadFlag(ThreadFlag::kEmptyCheckpointRequest)) {
      RunEmptyCheckpoint();
      // Check we hold only an expected mutex when accessing weak ref.
      if (kIsDebugBuild) {
        for (int i = kLockLevelCount - 1; i >= 0; --i) {
          BaseMutex* held_mutex = self->GetHeldMutex(static_cast<LockLevel>(i));
          if (held_mutex != nullptr &&
              held_mutex != GetMutatorLock() &&
              held_mutex != cond_var_mutex) {
            CHECK(Locks::IsExpectedOnWeakRefAccess(held_mutex))
                << "Holding unexpected mutex " << held_mutex->GetName()
                << " when accessing weak ref";
          }
        }
      }
    } else {
      break;
    }
  }
}

inline void Thread::CheckEmptyCheckpointFromMutex() {
  DCHECK_EQ(Thread::Current(), this);
  for (;;) {
    if (ReadFlag(ThreadFlag::kEmptyCheckpointRequest)) {
      RunEmptyCheckpoint();
    } else {
      break;
    }
  }
}

inline ThreadState Thread::SetState(ThreadState new_state) {
  // Should only be used to change between suspended states.
  // Cannot use this code to change into or from Runnable as changing to Runnable should
  // fail if the `ThreadFlag::kSuspendRequest` is set and changing from Runnable might
  // miss passing an active suspend barrier.
  DCHECK_NE(new_state, ThreadState::kRunnable);
  if (kIsDebugBuild && this != Thread::Current()) {
    std::string name;
    GetThreadName(name);
    LOG(FATAL) << "Thread \"" << name << "\"(" << this << " != Thread::Current()="
               << Thread::Current() << ") changing state to " << new_state;
  }

  while (true) {
    StateAndFlags old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
    CHECK_NE(old_state_and_flags.GetState(), ThreadState::kRunnable)
        << new_state << " " << *this << " " << *Thread::Current();
    StateAndFlags new_state_and_flags = old_state_and_flags.WithState(new_state);
    bool done =
        tls32_.state_and_flags.CompareAndSetWeakRelaxed(old_state_and_flags.GetValue(),
                                                        new_state_and_flags.GetValue());
    if (done) {
      return static_cast<ThreadState>(old_state_and_flags.GetState());
    }
  }
}

inline bool Thread::IsThreadSuspensionAllowable() const {
  if (tls32_.no_thread_suspension != 0) {
    return false;
  }
  for (int i = kLockLevelCount - 1; i >= 0; --i) {
    if (i != kMutatorLock &&
        i != kUserCodeSuspensionLock &&
        GetHeldMutex(static_cast<LockLevel>(i)) != nullptr) {
      return false;
    }
  }
  // Thread autoanalysis isn't able to understand that the GetHeldMutex(...) or AssertHeld means we
  // have the mutex meaning we need to do this hack.
  auto is_suspending_for_user_code = [this]() NO_THREAD_SAFETY_ANALYSIS {
    return tls32_.user_code_suspend_count != 0;
  };
  if (GetHeldMutex(kUserCodeSuspensionLock) != nullptr && is_suspending_for_user_code()) {
    return false;
  }
  return true;
}

inline void Thread::AssertThreadSuspensionIsAllowable(bool check_locks) const {
  if (kIsDebugBuild) {
    if (gAborting == 0) {
      CHECK_EQ(0u, tls32_.no_thread_suspension) << tlsPtr_.last_no_thread_suspension_cause;
    }
    if (check_locks) {
      bool bad_mutexes_held = false;
      for (int i = kLockLevelCount - 1; i >= 0; --i) {
        // We expect no locks except the mutator lock. User code suspension lock is OK as long as
        // we aren't going to be held suspended due to SuspendReason::kForUserCode.
        if (i != kMutatorLock && i != kUserCodeSuspensionLock) {
          BaseMutex* held_mutex = GetHeldMutex(static_cast<LockLevel>(i));
          if (held_mutex != nullptr) {
            LOG(ERROR) << "holding \"" << held_mutex->GetName()
                      << "\" at point where thread suspension is expected";
            bad_mutexes_held = true;
          }
        }
      }
      // Make sure that if we hold the user_code_suspension_lock_ we aren't suspending due to
      // user_code_suspend_count which would prevent the thread from ever waking up.  Thread
      // autoanalysis isn't able to understand that the GetHeldMutex(...) or AssertHeld means we
      // have the mutex meaning we need to do this hack.
      auto is_suspending_for_user_code = [this]() NO_THREAD_SAFETY_ANALYSIS {
        return tls32_.user_code_suspend_count != 0;
      };
      if (GetHeldMutex(kUserCodeSuspensionLock) != nullptr && is_suspending_for_user_code()) {
        LOG(ERROR) << "suspending due to user-code while holding \""
                   << Locks::user_code_suspension_lock_->GetName() << "\"! Thread would never "
                   << "wake up.";
        bad_mutexes_held = true;
      }
      if (gAborting == 0) {
        CHECK(!bad_mutexes_held);
      }
    }
  }
}

inline void Thread::TransitionToSuspendedAndRunCheckpoints(ThreadState new_state) {
  DCHECK_NE(new_state, ThreadState::kRunnable);
  while (true) {
    StateAndFlags old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
    DCHECK_EQ(old_state_and_flags.GetState(), ThreadState::kRunnable);
    if (UNLIKELY(old_state_and_flags.IsFlagSet(ThreadFlag::kCheckpointRequest))) {
      RunCheckpointFunction();
      continue;
    }
    if (UNLIKELY(old_state_and_flags.IsFlagSet(ThreadFlag::kEmptyCheckpointRequest))) {
      RunEmptyCheckpoint();
      continue;
    }
    // Change the state but keep the current flags (kCheckpointRequest is clear).
    DCHECK(!old_state_and_flags.IsFlagSet(ThreadFlag::kCheckpointRequest));
    DCHECK(!old_state_and_flags.IsFlagSet(ThreadFlag::kEmptyCheckpointRequest));
    StateAndFlags new_state_and_flags = old_state_and_flags.WithState(new_state);

    // CAS the value, ensuring that prior memory operations are visible to any thread
    // that observes that we are suspended.
    bool done =
        tls32_.state_and_flags.CompareAndSetWeakRelease(old_state_and_flags.GetValue(),
                                                        new_state_and_flags.GetValue());
    if (LIKELY(done)) {
      break;
    }
  }
}

inline void Thread::PassActiveSuspendBarriers() {
  while (true) {
    StateAndFlags state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
    if (LIKELY(!state_and_flags.IsFlagSet(ThreadFlag::kCheckpointRequest) &&
               !state_and_flags.IsFlagSet(ThreadFlag::kEmptyCheckpointRequest) &&
               !state_and_flags.IsFlagSet(ThreadFlag::kActiveSuspendBarrier))) {
      break;
    } else if (state_and_flags.IsFlagSet(ThreadFlag::kActiveSuspendBarrier)) {
      PassActiveSuspendBarriers(this);
    } else {
      // Impossible
      LOG(FATAL) << "Fatal, thread transitioned into suspended without running the checkpoint";
    }
  }
}

inline void Thread::TransitionFromRunnableToSuspended(ThreadState new_state) {
  // Note: JNI stubs inline a fast path of this method that transitions to suspended if
  // there are no flags set and then clears the `held_mutexes[kMutatorLock]` (this comes
  // from a specialized `BaseMutex::RegisterAsLockedImpl(., kMutatorLock)` inlined from
  // the `GetMutatorLock()->TransitionFromRunnableToSuspended(this)` below).
  // Therefore any code added here (other than debug build assertions) should be gated
  // on some flag being set, so that the JNI stub can take the slow path to get here.
  AssertThreadSuspensionIsAllowable();
  PoisonObjectPointersIfDebug();
  DCHECK_EQ(this, Thread::Current());
  // Change to non-runnable state, thereby appearing suspended to the system.
  TransitionToSuspendedAndRunCheckpoints(new_state);
  // Mark the release of the share of the mutator lock.
  GetMutatorLock()->TransitionFromRunnableToSuspended(this);
  // Once suspended - check the active suspend barrier flag
  PassActiveSuspendBarriers();
}

inline ThreadState Thread::TransitionFromSuspendedToRunnable() {
  // Note: JNI stubs inline a fast path of this method that transitions to Runnable if
  // there are no flags set and then stores the mutator lock to `held_mutexes[kMutatorLock]`
  // (this comes from a specialized `BaseMutex::RegisterAsUnlockedImpl(., kMutatorLock)`
  // inlined from the `GetMutatorLock()->TransitionFromSuspendedToRunnable(this)` below).
  // Therefore any code added here (other than debug build assertions) should be gated
  // on some flag being set, so that the JNI stub can take the slow path to get here.
  StateAndFlags old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
  ThreadState old_state = old_state_and_flags.GetState();
  DCHECK_NE(old_state, ThreadState::kRunnable);
  while (true) {
    GetMutatorLock()->AssertNotHeld(this);  // Otherwise we starve GC.
    // Optimize for the return from native code case - this is the fast path.
    // Atomically change from suspended to runnable if no suspend request pending.
    constexpr uint32_t kCheckedFlags =
        SuspendOrCheckpointRequestFlags() |
        enum_cast<uint32_t>(ThreadFlag::kActiveSuspendBarrier) |
        FlipFunctionFlags();
    if (LIKELY(!old_state_and_flags.IsAnyOfFlagsSet(kCheckedFlags))) {
      // CAS the value with a memory barrier.
      StateAndFlags new_state_and_flags = old_state_and_flags.WithState(ThreadState::kRunnable);
      if (LIKELY(tls32_.state_and_flags.CompareAndSetWeakAcquire(old_state_and_flags.GetValue(),
                                                                 new_state_and_flags.GetValue()))) {
        // Mark the acquisition of a share of the mutator lock.
        GetMutatorLock()->TransitionFromSuspendedToRunnable(this);
        break;
      }
    } else if (old_state_and_flags.IsFlagSet(ThreadFlag::kActiveSuspendBarrier)) {
      PassActiveSuspendBarriers(this);
    } else if (UNLIKELY(old_state_and_flags.IsFlagSet(ThreadFlag::kCheckpointRequest) ||
                        old_state_and_flags.IsFlagSet(ThreadFlag::kEmptyCheckpointRequest))) {
      // Checkpoint flags should not be set while in suspended state.
      static_assert(static_cast<std::underlying_type_t<ThreadState>>(ThreadState::kRunnable) == 0u);
      LOG(FATAL) << "Transitioning to Runnable with checkpoint flag,"
                 // Note: Keeping unused flags. If they are set, it points to memory corruption.
                 << " flags=" << old_state_and_flags.WithState(ThreadState::kRunnable).GetValue()
                 << " state=" << old_state_and_flags.GetState();
    } else if (old_state_and_flags.IsFlagSet(ThreadFlag::kSuspendRequest)) {
      // Wait while our suspend count is non-zero.

      // We pass null to the MutexLock as we may be in a situation where the
      // runtime is shutting down. Guarding ourselves from that situation
      // requires to take the shutdown lock, which is undesirable here.
      Thread* thread_to_pass = nullptr;
      if (kIsDebugBuild && !IsDaemon()) {
        // We know we can make our debug locking checks on non-daemon threads,
        // so re-enable them on debug builds.
        thread_to_pass = this;
      }
      MutexLock mu(thread_to_pass, *Locks::thread_suspend_count_lock_);
      ScopedTransitioningToRunnable scoped_transitioning_to_runnable(this);
      // Reload state and flags after locking the mutex.
      old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
      DCHECK_EQ(old_state, old_state_and_flags.GetState());
      while (old_state_and_flags.IsFlagSet(ThreadFlag::kSuspendRequest)) {
        // Re-check when Thread::resume_cond_ is notified.
        Thread::resume_cond_->Wait(thread_to_pass);
        // Reload state and flags after waiting.
        old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
        DCHECK_EQ(old_state, old_state_and_flags.GetState());
      }
      DCHECK_EQ(GetSuspendCount(), 0);
    } else if (UNLIKELY(old_state_and_flags.IsFlagSet(ThreadFlag::kRunningFlipFunction)) ||
               UNLIKELY(old_state_and_flags.IsFlagSet(ThreadFlag::kWaitingForFlipFunction))) {
      // The thread should be suspended while another thread is running the flip function.
      static_assert(static_cast<std::underlying_type_t<ThreadState>>(ThreadState::kRunnable) == 0u);
      LOG(FATAL) << "Transitioning to Runnable while another thread is running the flip function,"
                 // Note: Keeping unused flags. If they are set, it points to memory corruption.
                 << " flags=" << old_state_and_flags.WithState(ThreadState::kRunnable).GetValue()
                 << " state=" << old_state_and_flags.GetState();
    } else {
      DCHECK(old_state_and_flags.IsFlagSet(ThreadFlag::kPendingFlipFunction));
      // CAS the value with a memory barrier.
      // Do not set `ThreadFlag::kRunningFlipFunction` as no other thread can run
      // the flip function for a thread that is not suspended.
      StateAndFlags new_state_and_flags = old_state_and_flags.WithState(ThreadState::kRunnable)
          .WithoutFlag(ThreadFlag::kPendingFlipFunction);
      if (LIKELY(tls32_.state_and_flags.CompareAndSetWeakAcquire(old_state_and_flags.GetValue(),
                                                                 new_state_and_flags.GetValue()))) {
        // Mark the acquisition of a share of the mutator lock.
        GetMutatorLock()->TransitionFromSuspendedToRunnable(this);
        // Run the flip function.
        RunFlipFunction(this, /*notify=*/ false);
        break;
      }
    }
    // Reload state and flags.
    old_state_and_flags = GetStateAndFlags(std::memory_order_relaxed);
    DCHECK_EQ(old_state, old_state_and_flags.GetState());
  }
  return static_cast<ThreadState>(old_state);
}

inline mirror::Object* Thread::AllocTlab(size_t bytes) {
  DCHECK_GE(TlabSize(), bytes);
  ++tlsPtr_.thread_local_objects;
  mirror::Object* ret = reinterpret_cast<mirror::Object*>(tlsPtr_.thread_local_pos);
  tlsPtr_.thread_local_pos += bytes;
  return ret;
}

inline bool Thread::PushOnThreadLocalAllocationStack(mirror::Object* obj) {
  DCHECK_LE(tlsPtr_.thread_local_alloc_stack_top, tlsPtr_.thread_local_alloc_stack_end);
  if (tlsPtr_.thread_local_alloc_stack_top < tlsPtr_.thread_local_alloc_stack_end) {
    // There's room.
    DCHECK_LE(reinterpret_cast<uint8_t*>(tlsPtr_.thread_local_alloc_stack_top) +
              sizeof(StackReference<mirror::Object>),
              reinterpret_cast<uint8_t*>(tlsPtr_.thread_local_alloc_stack_end));
    DCHECK(tlsPtr_.thread_local_alloc_stack_top->AsMirrorPtr() == nullptr);
    tlsPtr_.thread_local_alloc_stack_top->Assign(obj);
    ++tlsPtr_.thread_local_alloc_stack_top;
    return true;
  }
  return false;
}

inline bool Thread::GetWeakRefAccessEnabled() const {
  DCHECK(gUseReadBarrier);
  DCHECK(this == Thread::Current());
  WeakRefAccessState s = tls32_.weak_ref_access_enabled.load(std::memory_order_relaxed);
  if (LIKELY(s == WeakRefAccessState::kVisiblyEnabled)) {
    return true;
  }
  s = tls32_.weak_ref_access_enabled.load(std::memory_order_acquire);
  if (s == WeakRefAccessState::kVisiblyEnabled) {
    return true;
  } else if (s == WeakRefAccessState::kDisabled) {
    return false;
  }
  DCHECK(s == WeakRefAccessState::kEnabled)
      << "state = " << static_cast<std::underlying_type_t<WeakRefAccessState>>(s);
  // The state is only changed back to DISABLED during a checkpoint. Thus no other thread can
  // change the value concurrently here. No other thread reads the value we store here, so there
  // is no need for a release store.
  tls32_.weak_ref_access_enabled.store(WeakRefAccessState::kVisiblyEnabled,
                                       std::memory_order_relaxed);
  return true;
}

inline void Thread::SetThreadLocalAllocationStack(StackReference<mirror::Object>* start,
                                                  StackReference<mirror::Object>* end) {
  DCHECK(Thread::Current() == this) << "Should be called by self";
  DCHECK(start != nullptr);
  DCHECK(end != nullptr);
  DCHECK_ALIGNED(start, sizeof(StackReference<mirror::Object>));
  DCHECK_ALIGNED(end, sizeof(StackReference<mirror::Object>));
  DCHECK_LT(start, end);
  tlsPtr_.thread_local_alloc_stack_end = end;
  tlsPtr_.thread_local_alloc_stack_top = start;
}

inline void Thread::RevokeThreadLocalAllocationStack() {
  if (kIsDebugBuild) {
    // Note: self is not necessarily equal to this thread since thread may be suspended.
    Thread* self = Thread::Current();
    DCHECK(this == self || IsSuspended() || GetState() == ThreadState::kWaitingPerformingGc)
        << GetState() << " thread " << this << " self " << self;
  }
  tlsPtr_.thread_local_alloc_stack_end = nullptr;
  tlsPtr_.thread_local_alloc_stack_top = nullptr;
}

inline void Thread::PoisonObjectPointersIfDebug() {
  if (kObjPtrPoisoning) {
    Thread::Current()->PoisonObjectPointers();
  }
}

inline bool Thread::ModifySuspendCount(Thread* self,
                                       int delta,
                                       AtomicInteger* suspend_barrier,
                                       SuspendReason reason) {
  if (delta > 0 && ((gUseReadBarrier && this != self) || suspend_barrier != nullptr)) {
    // When delta > 0 (requesting a suspend), ModifySuspendCountInternal() may fail either if
    // active_suspend_barriers is full or we are in the middle of a thread flip. Retry in a loop.
    while (true) {
      if (LIKELY(ModifySuspendCountInternal(self, delta, suspend_barrier, reason))) {
        return true;
      } else {
        // Failure means the list of active_suspend_barriers is full or we are in the middle of a
        // thread flip, we should release the thread_suspend_count_lock_ (to avoid deadlock) and
        // wait till the target thread has executed or Thread::PassActiveSuspendBarriers() or the
        // flip function. Note that we could not simply wait for the thread to change to a suspended
        // state, because it might need to run checkpoint function before the state change or
        // resumes from the resume_cond_, which also needs thread_suspend_count_lock_.
        //
        // The list of active_suspend_barriers is very unlikely to be full since more than
        // kMaxSuspendBarriers threads need to execute SuspendAllInternal() simultaneously, and
        // target thread stays in kRunnable in the mean time.
        Locks::thread_suspend_count_lock_->ExclusiveUnlock(self);
        NanoSleep(100000);
        Locks::thread_suspend_count_lock_->ExclusiveLock(self);
      }
    }
  } else {
    return ModifySuspendCountInternal(self, delta, suspend_barrier, reason);
  }
}

inline ShadowFrame* Thread::PushShadowFrame(ShadowFrame* new_top_frame) {
  new_top_frame->CheckConsistentVRegs();
  return tlsPtr_.managed_stack.PushShadowFrame(new_top_frame);
}

inline ShadowFrame* Thread::PopShadowFrame() {
  return tlsPtr_.managed_stack.PopShadowFrame();
}

inline uint8_t* Thread::GetStackEndForInterpreter(bool implicit_overflow_check) const {
  uint8_t* end = tlsPtr_.stack_end + (implicit_overflow_check
      ? GetStackOverflowReservedBytes(kRuntimeISA)
          : 0);
  if (kIsDebugBuild) {
    // In a debuggable build, but especially under ASAN, the access-checks interpreter has a
    // potentially humongous stack size. We don't want to take too much of the stack regularly,
    // so do not increase the regular reserved size (for compiled code etc) and only report the
    // virtually smaller stack to the interpreter here.
    end += GetStackOverflowReservedBytes(kRuntimeISA);
  }
  return end;
}

inline void Thread::ResetDefaultStackEnd() {
  // Our stacks grow down, so we want stack_end_ to be near there, but reserving enough room
  // to throw a StackOverflowError.
  tlsPtr_.stack_end = tlsPtr_.stack_begin + GetStackOverflowReservedBytes(kRuntimeISA);
}

}  // namespace art

#endif  // ART_RUNTIME_THREAD_INL_H_
