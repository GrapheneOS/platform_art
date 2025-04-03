/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "java_lang_Thread.h"

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "art_field-inl.h"
#include "art_field.h"
#include "art_method.h"
#include "base/casts.h"
#include "base/utils.h"
#include "class_linker.h"
#include "class_root-inl.h"
#include "common_throws.h"
#include "handle_scope-inl.h"
#include "handle_scope.h"
#include "interpreter/shadow_frame-inl.h"
#include "interpreter/shadow_frame.h"
#include "jni.h"
#include "jni/jni_internal.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "mirror/object.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"
#include "mirror/object_reference.h"
#include "monitor.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_utf_chars.h"
#include "obj_ptr.h"
#include "runtime.h"
#include "scoped_fast_native_object_access-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "stack_reference.h"
#include "thread.h"
#include "thread_list.h"
#include "verify_object.h"
#include "well_known_classes.h"

namespace art HIDDEN {

static jobject Thread_currentThread(JNIEnv* env, jclass) {
  ScopedFastNativeObjectAccess soa(env);
  return soa.AddLocalReference<jobject>(soa.Self()->GetPeer());
}

static jboolean Thread_interrupted(JNIEnv* env, jclass) {
  return static_cast<JNIEnvExt*>(env)->GetSelf()->Interrupted() ? JNI_TRUE : JNI_FALSE;
}

static jboolean Thread_isInterrupted(JNIEnv* env, jobject java_thread) {
  ScopedFastNativeObjectAccess soa(env);
  MutexLock mu(soa.Self(), *Locks::thread_list_lock_);
  Thread* thread = Thread::FromManagedThread(soa, java_thread);
  return (thread != nullptr) ? thread->IsInterrupted() : JNI_FALSE;
}

static void Thread_nativeCreate(JNIEnv* env, jclass, jobject java_thread, jlong stack_size,
                                jboolean daemon) {
  // There are sections in the zygote that forbid thread creation.
  Runtime* runtime = Runtime::Current();
  if (runtime->IsZygote() && runtime->IsZygoteNoThreadSection()) {
    jclass internal_error = env->FindClass("java/lang/InternalError");
    CHECK(internal_error != nullptr);
    env->ThrowNew(internal_error, "Cannot create threads in zygote");
    return;
  }

  Thread::CreateNativeThread(env, java_thread, stack_size, daemon == JNI_TRUE);
}

static jint Thread_nativeGetStatus(JNIEnv* env, jobject java_thread, jboolean has_been_started) {
  // Ordinals from Java's Thread.State.
  const jint kJavaNew = 0;
  const jint kJavaRunnable = 1;
  const jint kJavaBlocked = 2;
  const jint kJavaWaiting = 3;
  const jint kJavaTimedWaiting = 4;
  const jint kJavaTerminated = 5;

  ScopedObjectAccess soa(env);
  ThreadState internal_thread_state =
      (has_been_started ? ThreadState::kTerminated : ThreadState::kStarting);
  MutexLock mu(soa.Self(), *Locks::thread_list_lock_);
  Thread* thread = Thread::FromManagedThread(soa, java_thread);
  if (thread != nullptr) {
    internal_thread_state = thread->GetState();
  }
  switch (internal_thread_state) {
    case ThreadState::kTerminated:                     return kJavaTerminated;
    case ThreadState::kRunnable:                       return kJavaRunnable;
    case ThreadState::kObsoleteRunnable:               break;  // Obsolete value.
    case ThreadState::kTimedWaiting:                   return kJavaTimedWaiting;
    case ThreadState::kSleeping:                       return kJavaTimedWaiting;
    case ThreadState::kBlocked:                        return kJavaBlocked;
    case ThreadState::kWaiting:                        return kJavaWaiting;
    case ThreadState::kStarting:                       return kJavaNew;
    case ThreadState::kNative:                         return kJavaRunnable;
    case ThreadState::kWaitingForTaskProcessor:        return kJavaWaiting;
    case ThreadState::kWaitingForLockInflation:        return kJavaWaiting;
    case ThreadState::kWaitingForGcToComplete:         return kJavaWaiting;
    case ThreadState::kWaitingPerformingGc:            return kJavaWaiting;
    case ThreadState::kWaitingForCheckPointsToRun:     return kJavaWaiting;
    case ThreadState::kWaitingForDebuggerSend:         return kJavaWaiting;
    case ThreadState::kWaitingForDebuggerToAttach:     return kJavaWaiting;
    case ThreadState::kWaitingInMainDebuggerLoop:      return kJavaWaiting;
    case ThreadState::kWaitingForDebuggerSuspension:   return kJavaWaiting;
    case ThreadState::kWaitingForDeoptimization:       return kJavaWaiting;
    case ThreadState::kWaitingForGetObjectsAllocated:  return kJavaWaiting;
    case ThreadState::kWaitingForJniOnLoad:            return kJavaWaiting;
    case ThreadState::kWaitingForSignalCatcherOutput:  return kJavaWaiting;
    case ThreadState::kWaitingInMainSignalCatcherLoop: return kJavaWaiting;
    case ThreadState::kWaitingForMethodTracingStart:   return kJavaWaiting;
    case ThreadState::kWaitingForVisitObjects:         return kJavaWaiting;
    case ThreadState::kWaitingWeakGcRootRead:          return kJavaRunnable;
    case ThreadState::kWaitingForGcThreadFlip:         return kJavaWaiting;
    case ThreadState::kNativeForAbort:                 return kJavaWaiting;
    case ThreadState::kSuspended:                      return kJavaRunnable;
    case ThreadState::kInvalidState:                   break;
    // Don't add a 'default' here so the compiler can spot incompatible enum changes.
  }
  LOG(ERROR) << "Unexpected thread state: " << internal_thread_state;
  return -1;  // Unreachable.
}

static jboolean Thread_holdsLock(JNIEnv* env, jclass, jobject java_object) {
  ScopedObjectAccess soa(env);
  ObjPtr<mirror::Object> object = soa.Decode<mirror::Object>(java_object);
  if (object == nullptr) {
    ThrowNullPointerException("object == null");
    return JNI_FALSE;
  }
  Thread* thread = soa.Self();
  return thread->HoldsLock(object);
}

static void Thread_interrupt0(JNIEnv* env, jobject java_thread) {
  ScopedFastNativeObjectAccess soa(env);
  MutexLock mu(soa.Self(), *Locks::thread_list_lock_);
  Thread* thread = Thread::FromManagedThread(soa, java_thread);
  if (thread != nullptr) {
    thread->Interrupt(soa.Self());
  }
}

static void Thread_setNativeName(JNIEnv* env, jobject peer, jstring java_name) {
  ScopedUtfChars name(env, java_name);
  {
    ScopedObjectAccess soa(env);
    if (soa.Decode<mirror::Object>(peer) == soa.Self()->GetPeer()) {
      soa.Self()->SetThreadName(name.c_str());
      return;
    }
  }
  // Suspend thread to avoid it from killing itself while we set its name. We don't just hold the
  // thread list lock to avoid this, as setting the thread name causes mutator to lock/unlock
  // in the DDMS send code.
  ThreadList* thread_list = Runtime::Current()->GetThreadList();
  // Take suspend thread lock to avoid races with threads trying to suspend this one.
  Thread* thread = thread_list->SuspendThreadByPeer(peer, SuspendReason::kInternal);
  if (thread != nullptr) {
    {
      ScopedObjectAccess soa(env);
      thread->SetThreadName(name.c_str());
    }
    bool resumed = thread_list->Resume(thread, SuspendReason::kInternal);
    DCHECK(resumed);
  }
}

/*
 * Alter the priority of the specified thread.  "new_priority" will range
 * from Thread.MIN_PRIORITY to Thread.MAX_PRIORITY (1-10), with "normal"
 * threads at Thread.NORM_PRIORITY (5).
 */
static void Thread_setPriority0(JNIEnv* env, jobject java_thread, jint new_priority) {
  ScopedObjectAccess soa(env);
  MutexLock mu(soa.Self(), *Locks::thread_list_lock_);
  Thread* thread = Thread::FromManagedThread(soa, java_thread);
  if (thread != nullptr) {
    thread->SetNativePriority(new_priority);
  }
}

static void Thread_sleep(JNIEnv* env, jclass, jobject java_lock, jlong ms, jint ns) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> lock = soa.Decode<mirror::Object>(java_lock);
  Monitor::Wait(Thread::Current(), lock.Ptr(), ms, ns, true, ThreadState::kSleeping);
}

/*
 * Causes the thread to temporarily pause and allow other threads to execute.
 *
 * The exact behavior is poorly defined.  Some discussion here:
 *   http://www.cs.umd.edu/~pugh/java/memoryModel/archive/0944.html
 */
static void Thread_yield(JNIEnv*, jobject) {
  sched_yield();
}

enum PinningReason {
  kNoReason = 0,
  kNativeMethod = 1,
  kMonitor = 2,
  kUnsupportedFrame = 3,
};

struct VirtualThreadParkingVisitor final : public StackVisitor {
  explicit VirtualThreadParkingVisitor(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
      : StackVisitor(thread, nullptr, StackVisitor::StackWalkKind::kIncludeInlinedFrames, true),
        shadow_frame_count_(0),
        reason_(kNoReason) {}
  bool VisitFrame() override REQUIRES_SHARED(Locks::mutator_lock_) {
    ShadowFrame* shadow_frame = GetCurrentShadowFrame();

    if (shadow_frame == nullptr) {
      // Stack walking continues only if the only non-interpreted frame is
      // Thread.parkVirtualInternal until JIT and AOT frame is supported.
      ArtMethod** quick_frame = GetCurrentQuickFrame();
      ArtMethod* method = quick_frame != nullptr ? *quick_frame : nullptr;
      if (method != nullptr && method->IsNative()) {
        if (method == WellKnownClasses::java_lang_Thread_parkVirtualInternal) {
          return true;
        }

        reason_ = kNativeMethod;
        return false;
      }

      reason_ = kUnsupportedFrame;
      return false;
    }

    if (!shadow_frame->GetLockCountData().IsEmpty()) {
      reason_ = kMonitor;
      return false;
    }

    // If the verifier was able to verify the locks are balanced, the interpreter won't update the
    // lock cound data. We need to walk the stack to find the locks here. Or should we just have an
    // increment/decrement counter?
    Monitor::VisitLocks(this, LockVisitingCallback, this);
    if (reason_ == kMonitor) {
      return false;
    }
    DCHECK(reason_ == kNoReason);

    shadow_frame_count_++;
    shadow_frames_.push_back(shadow_frame);

    return true;
  }

  static void LockVisitingCallback(ObjPtr<mirror::Object> obj, void* visitor) {
    DCHECK(!obj.IsNull());
    reinterpret_cast<VirtualThreadParkingVisitor*>(visitor)->reason_ = kMonitor;
  }

  std::vector<ShadowFrame*> shadow_frames_;
  size_t shadow_frame_count_;
  PinningReason reason_;
};

static void Thread_parkVirtualInternal(
    JNIEnv* env, jobject, jobject v_context, jobject parked_states, jobject vm_error) {
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(env);
  StackHandleScope<4> hs(soa.Self());
  ClassLinker* cl = Runtime::Current()->GetClassLinker();

  auto v_context_h = hs.NewHandle(soa.Decode<mirror::Object>(v_context));
  auto parked_states_h = hs.NewHandle(soa.Decode<mirror::Object>(parked_states));
  auto opeer_h = hs.NewHandle(self->GetPeer());

  VirtualThreadParkingVisitor dump_visitor(self);
  dump_visitor.WalkStack();

  DCHECK_NE(dump_visitor.reason_, kUnsupportedFrame) << "JIT / AOT frame isn't supported.";
  if (dump_visitor.reason_ != kNoReason) {
    WellKnownClasses::dalvik_system_VirtualThreadContext_pinnedCarrierThread->SetObject<false>(
        v_context_h.Get(), opeer_h.Get());
    // Return to the java code to park the carrier thread
    return;
  }

  size_t num_frames = dump_visitor.shadow_frames_.size();

  DCHECK(!Runtime::Current()->IsActiveTransaction());

  Runtime* runtime = Runtime::Current();
  auto frames_h = hs.NewHandle(mirror::ObjectArray<mirror::Object>::Alloc(
      self,
      soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_VirtualThreadFrame__array),
      num_frames));
  for (size_t i = 0; i < num_frames; i++) {
    const ShadowFrame* sf = dump_visitor.shadow_frames_[i];
    DCHECK(sf != nullptr);
    sf->CheckConsistentVRegs();

    size_t num_vergs = sf->NumberOfVRegs();
    int32_t non_vref_size = ShadowFrame::ComputeSizeWithoutReferences(num_vergs);

    StackHandleScope<4> hs2(soa.Self());
    auto vtf = hs2.NewHandle(WellKnownClasses::dalvik_system_VirtualThreadFrame.Get()->Alloc(
        self, Runtime::Current()->GetHeap()->GetCurrentAllocator()));
    auto frame_bytes = hs2.NewHandle(mirror::ByteArray::Alloc(self, non_vref_size));
    auto declaring_class = hs2.NewHandle(sf->GetMethod()->GetDeclaringClass());
    auto refs = hs2.NewHandle(mirror::ObjectArray<mirror::Object>::Alloc(
        self, GetClassRoot<mirror::ObjectArray<mirror::Object>>(), num_vergs));
    DCHECK(!vtf.IsNull());
    DCHECK(!frame_bytes.IsNull());
    DCHECK(!refs.IsNull());

    bool areRefsAllNull = true;
    for (uint32_t j = 0; j < sf->NumberOfVRegs(); j++) {
      ObjPtr<mirror::Object> obj = sf->GetVRegReference(j);
      if (obj != nullptr) {
        refs->Set(j, obj);
        areRefsAllNull = false;
      }
    }

    if (!areRefsAllNull) {
      WellKnownClasses::dalvik_system_VirtualThreadFrame_refs->SetObject<false>(vtf.Get(),
                                                                                refs.Get());
    }
    frame_bytes->Memcpy(0, reinterpret_cast<const int8_t*>(sf), 0, non_vref_size);
    WellKnownClasses::dalvik_system_VirtualThreadFrame_frame->SetObject<false>(vtf.Get(),
                                                                               frame_bytes.Get());
    WellKnownClasses::dalvik_system_VirtualThreadFrame_declaringClass->SetObject<false>(
        vtf.Get(), declaring_class.Get());
    frames_h->Set(i, vtf.Get());
  }

  if (self->IsExceptionPending()) {
    // It's likely a OOME. Let's throw it at the java level.
    // Consider handling it in another way, e.g. pinning virtual thread, in the future.
    return;
  }

  WellKnownClasses::dalvik_system_VirtualThreadParkedStates_frames->SetObject<false>(
      parked_states_h.Get(), frames_h.Get());
  WellKnownClasses::dalvik_system_VirtualThreadContext_parkedStates->SetObject<false>(
      v_context_h.Get(), parked_states_h.Get());
  self->SetVirtualThreadFlags(VirtualThreadFlag::kParking, true);

  // Throw a VirtualThreadParkingError to unwind the stack and park the virtual thread.
  env->Throw(reinterpret_cast<jthrowable>(vm_error));
}

static JNINativeMethod gMethods[] = {
    FAST_NATIVE_METHOD(Thread, currentThread, "()Ljava/lang/Thread;"),
    FAST_NATIVE_METHOD(Thread, interrupted, "()Z"),
    FAST_NATIVE_METHOD(Thread, isInterrupted, "()Z"),
    NATIVE_METHOD(Thread, nativeCreate, "(Ljava/lang/Thread;JZ)V"),
    NATIVE_METHOD(Thread, nativeGetStatus, "(Z)I"),
    NATIVE_METHOD(Thread, holdsLock, "(Ljava/lang/Object;)Z"),
    FAST_NATIVE_METHOD(Thread, interrupt0, "()V"),
    NATIVE_METHOD(Thread, setNativeName, "(Ljava/lang/String;)V"),
    NATIVE_METHOD(Thread, setPriority0, "(I)V"),
    FAST_NATIVE_METHOD(Thread, sleep, "(Ljava/lang/Object;JI)V"),
    NATIVE_METHOD(Thread, yield, "()V"),
    NATIVE_METHOD(Thread,
                  parkVirtualInternal,
                  "(Ldalvik/system/VirtualThreadContext;Ldalvik/system/"
                  "VirtualThreadParkedStates;Ldalvik/system/VirtualThreadParkingError;)V"),
};

void register_java_lang_Thread(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/Thread");
}

}  // namespace art
