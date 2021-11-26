/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include <android-base/logging.h>

#include "art_method-inl.h"
#include "base/casts.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "indirect_reference_table.h"
#include "mirror/object-inl.h"
#include "palette/palette.h"
#include "thread-inl.h"
#include "verify_object.h"

// For methods that monitor JNI invocations and report their begin/end to
// palette hooks.
#define MONITOR_JNI(kind)                                \
  {                                                      \
    bool should_report = false;                          \
    PaletteShouldReportJniInvocations(&should_report);   \
    if (should_report) {                                 \
      kind(self->GetJniEnv());                           \
    }                                                    \
  }

namespace art {

static_assert(sizeof(IRTSegmentState) == sizeof(uint32_t), "IRTSegmentState size unexpected");
static_assert(std::is_trivial<IRTSegmentState>::value, "IRTSegmentState not trivial");

extern "C" void artJniReadBarrier(ArtMethod* method) {
  DCHECK(kUseReadBarrier);
  mirror::CompressedReference<mirror::Object>* declaring_class =
      method->GetDeclaringClassAddressWithoutBarrier();
  if (kUseBakerReadBarrier) {
    DCHECK(declaring_class->AsMirrorPtr() != nullptr)
        << "The class of a static jni call must not be null";
    // Check the mark bit and return early if it's already marked.
    if (LIKELY(declaring_class->AsMirrorPtr()->GetMarkBit() != 0)) {
      return;
    }
  }
  // Call the read barrier and update the handle.
  mirror::Object* to_ref = ReadBarrier::BarrierForRoot(declaring_class);
  declaring_class->Assign(to_ref);
}

// Called on entry to JNI, transition out of Runnable and release share of mutator_lock_.
extern void JniMethodStart(Thread* self) {
  if (kIsDebugBuild) {
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(!native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  // Transition out of runnable.
  self->TransitionFromRunnableToSuspended(ThreadState::kNative);
}

// TODO: NO_THREAD_SAFETY_ANALYSIS due to different control paths depending on fast JNI.
static void GoToRunnable(Thread* self) NO_THREAD_SAFETY_ANALYSIS {
  if (kIsDebugBuild) {
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(!native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  self->TransitionFromSuspendedToRunnable();
}

static void PopLocalReferences(uint32_t saved_local_ref_cookie, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  JNIEnvExt* env = self->GetJniEnv();
  if (UNLIKELY(env->IsCheckJniEnabled())) {
    env->CheckNoHeldMonitors();
  }
  env->SetLocalSegmentState(env->GetLocalRefCookie());
  env->SetLocalRefCookie(bit_cast<IRTSegmentState>(saved_local_ref_cookie));
}

// TODO: annotalysis disabled as monitor semantics are maintained in Java code.
extern "C" void artJniUnlockObject(mirror::Object* locked, Thread* self)
    NO_THREAD_SAFETY_ANALYSIS REQUIRES(!Roles::uninterruptible_) {
  // Note: No thread suspension is allowed for successful unlocking, otherwise plain
  // `mirror::Object*` return value saved by the assembly stub would need to be updated.
  uintptr_t old_poison_object_cookie = kIsDebugBuild ? self->GetPoisonObjectCookie() : 0u;
  // Save any pending exception over monitor exit call.
  ObjPtr<mirror::Throwable> saved_exception = nullptr;
  if (UNLIKELY(self->IsExceptionPending())) {
    saved_exception = self->GetException();
    self->ClearException();
  }
  // Decode locked object and unlock, before popping local references.
  locked->MonitorExit(self);
  if (UNLIKELY(self->IsExceptionPending())) {
    LOG(FATAL) << "Exception during implicit MonitorExit for synchronized native method:\n"
        << self->GetException()->Dump()
        << (saved_exception != nullptr
               ? "\nAn exception was already pending:\n" + saved_exception->Dump()
               : "");
    UNREACHABLE();
  }
  // Restore pending exception.
  if (saved_exception != nullptr) {
    self->SetException(saved_exception);
  }
  if (kIsDebugBuild) {
    DCHECK_EQ(old_poison_object_cookie, self->GetPoisonObjectCookie());
  }
}

// TODO: These should probably be templatized or macro-ized.
// Otherwise there's just too much repetitive boilerplate.

extern void JniMethodEnd(Thread* self) {
  GoToRunnable(self);
}

extern mirror::Object* JniDecodeReferenceResult(jobject result, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(!self->IsExceptionPending());
  ObjPtr<mirror::Object> o = self->DecodeJObject(result);
  // Process result.
  if (UNLIKELY(self->GetJniEnv()->IsCheckJniEnabled())) {
    // CheckReferenceResult can resolve types.
    StackHandleScope<1> hs(self);
    HandleWrapperObjPtr<mirror::Object> h_obj(hs.NewHandleWrapper(&o));
    CheckReferenceResult(h_obj, self);
  }
  VerifyObject(o);
  return o.Ptr();
}

// Common result handling for EndWithReference.
static mirror::Object* JniMethodEndWithReferenceHandleResult(jobject result, Thread* self)
    NO_THREAD_SAFETY_ANALYSIS {
  // Must decode before pop. The 'result' may not be valid in case of an exception, though.
  ObjPtr<mirror::Object> o;
  if (!self->IsExceptionPending()) {
    o = self->DecodeJObject(result);
  }
  // Process result.
  if (UNLIKELY(self->GetJniEnv()->IsCheckJniEnabled())) {
    // CheckReferenceResult can resolve types.
    StackHandleScope<1> hs(self);
    HandleWrapperObjPtr<mirror::Object> h_obj(hs.NewHandleWrapper(&o));
    CheckReferenceResult(h_obj, self);
  }
  VerifyObject(o);
  return o.Ptr();
}

extern mirror::Object* JniMethodEndWithReference(jobject result, Thread* self) {
  GoToRunnable(self);
  return JniMethodEndWithReferenceHandleResult(result, self);
}

extern uint64_t GenericJniMethodEnd(Thread* self,
                                    uint32_t saved_local_ref_cookie,
                                    jvalue result,
                                    uint64_t result_f,
                                    ArtMethod* called)
    // TODO: NO_THREAD_SAFETY_ANALYSIS as GoToRunnable() is NO_THREAD_SAFETY_ANALYSIS
    NO_THREAD_SAFETY_ANALYSIS {
  bool critical_native = called->IsCriticalNative();
  bool fast_native = called->IsFastNative();
  bool normal_native = !critical_native && !fast_native;

  // @CriticalNative does not do a state transition. @FastNative usually does not do a state
  // transition either but it performs a suspend check that may do state transitions.
  if (LIKELY(normal_native)) {
    MONITOR_JNI(PaletteNotifyEndJniInvocation);
    GoToRunnable(self);
  } else if (fast_native) {
    // When we are in @FastNative, we are already Runnable.
    DCHECK(Locks::mutator_lock_->IsSharedHeld(self));
    // Only do a suspend check on the way out of JNI just like compiled stubs.
    if (UNLIKELY(self->TestAllFlags())) {
      // In fast JNI mode we never transitioned out of runnable. Perform a suspend check if there
      // is a flag raised.
      self->CheckSuspend();
    }
  }
  // We need the mutator lock (i.e., calling GoToRunnable()) before accessing the shorty or the
  // locked object.
  if (called->IsSynchronized()) {
    DCHECK(normal_native) << "@FastNative/@CriticalNative and synchronize is not supported";
    ObjPtr<mirror::Object> lock = GetGenericJniSynchronizationObject(self, called);
    DCHECK(lock != nullptr);
    artJniUnlockObject(lock.Ptr(), self);
  }
  char return_shorty_char = called->GetShorty()[0];
  if (return_shorty_char == 'L') {
    uint64_t ret =
        reinterpret_cast<uint64_t>(JniMethodEndWithReferenceHandleResult(result.l, self));
    PopLocalReferences(saved_local_ref_cookie, self);
    return ret;
  } else {
    if (LIKELY(!critical_native)) {
      PopLocalReferences(saved_local_ref_cookie, self);
    }
    switch (return_shorty_char) {
      case 'F': {
        if (kRuntimeISA == InstructionSet::kX86) {
          // Convert back the result to float.
          double d = bit_cast<double, uint64_t>(result_f);
          return bit_cast<uint32_t, float>(static_cast<float>(d));
        } else {
          return result_f;
        }
      }
      case 'D':
        return result_f;
      case 'Z':
        return result.z;
      case 'B':
        return result.b;
      case 'C':
        return result.c;
      case 'S':
        return result.s;
      case 'I':
        return result.i;
      case 'J':
        return result.j;
      case 'V':
        return 0;
      default:
        LOG(FATAL) << "Unexpected return shorty character " << return_shorty_char;
        UNREACHABLE();
    }
  }
}

extern void JniMonitoredMethodStart(Thread* self) {
  JniMethodStart(self);
  MONITOR_JNI(PaletteNotifyBeginJniInvocation);
}

extern void JniMonitoredMethodEnd(Thread* self) {
  MONITOR_JNI(PaletteNotifyEndJniInvocation);
  JniMethodEnd(self);
}

extern mirror::Object* JniMonitoredMethodEndWithReference(jobject result, Thread* self) {
  MONITOR_JNI(PaletteNotifyEndJniInvocation);
  return JniMethodEndWithReference(result, self);
}

}  // namespace art
