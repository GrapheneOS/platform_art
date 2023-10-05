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

#include "jdk_internal_vm_Continuation.h"

#include "art_method-inl.h"
#include "dex/invoke_type.h"
#include "handle_scope-inl.h"
#include "jni.h"
#include "mirror/object.h"
#include "mirror/throwable.h"
#include "native/native_util.h"
#include "nativehelper/jni_macros.h"
#include "obj_ptr-inl.h"
#include "obj_ptr.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "scoped_thread_state_change.h"
#include "thread.h"
#include "virtual_thread_common.h"
#include "well_known_classes.h"

namespace art HIDDEN {

static jint Continuation_doYieldNative([[maybe_unused]] JNIEnv* env,
                                       jobject,
                                       jobject v_context,
                                       jobject parked_states,
                                       jobject vm_error) {
  ScopedObjectAccess soa(env);

  PinningReason reason = kNoReason;
  bool success = VirtualThreadPark(soa.Decode<mirror::Object>(v_context),
                                   soa.Decode<mirror::Object>(parked_states),
                                   soa.Decode<mirror::Throwable>(vm_error),
                                   /* is_continuation_api= */ true,
                                   reason);

  if (!success && reason == kNoReason) {
    // Expect a pending exception, and return to the java level to handle it
    DCHECK(Thread::Current()->IsExceptionPending());
    return kNoReason;
  }

  return reason;
}

static void Continuation_enterSpecial(
    JNIEnv* env, jobject, jobject cont, jboolean j_is_continue, jboolean j_is_virtual_thread) {
  Thread* self = Thread::Current();
  DCHECK(j_is_virtual_thread);
  DCHECK(!self->AreVirtualThreadFlagsEnabled(kIsVirtual));

  ScopedObjectAccess soa(env);
  ObjPtr<mirror::Object> continuation = soa.Decode<mirror::Object>(cont);
  ObjPtr<mirror::Object> v_context =
      WellKnownClasses::jdk_internal_vm_Continuation_virtualThreadContext->GetObject(continuation);
  ObjPtr<mirror::Object> parked_states =
      WellKnownClasses::dalvik_system_VirtualThreadContext_parkedStates->GetObject(v_context);

  bool is_continue = j_is_continue;
  DCHECK_NE(parked_states.IsNull(), is_continue) << "Likely a bug in the Continuation.java";
  if (parked_states.IsNull() == is_continue) {
    const char* msg = is_continue ? "Can't continue without the saved stack"
                                  : "saved stack shouldn't exist when a continuation start.";
    self->ThrowNewException("Ljava/lang/IllegalStateException;", msg);
    return;
  }

  uint8_t flags_mask = kIsVirtual | kContinuation | (is_continue ? kUnparking : 0);
  self->SetVirtualThreadFlags(flags_mask, true);

  WellKnownClasses::jdk_internal_vm_Continuation_enter->InvokeStatic<'V', 'L', 'Z'>(
      self, continuation, is_continue);

  // When a virtual thread is parked, clear the VirtualThreadParkingError used to
  // unwind the native stack.
  if (self->IsExceptionPending() &&
      self->AreVirtualThreadFlagsEnabled(VirtualThreadFlag::kParking)) {
    DCHECK(self->GetException()->GetClass()->DescriptorEquals(
        "Ldalvik/system/VirtualThreadParkingError;"));
    self->ClearException();
  }

  self->SetVirtualThreadFlags(kIsVirtual | kContinuation | kParking, false);
}

static const JNINativeMethod gMethods[] = {
    NATIVE_METHOD(
        Continuation,
        doYieldNative,
        "(Ldalvik/system/VirtualThreadContext;"
        "Ldalvik/system/VirtualThreadParkedStates;Ldalvik/system/VirtualThreadParkingError;)I"),
    NATIVE_METHOD(Continuation, enterSpecial, "(Ljdk/internal/vm/Continuation;ZZ)V"),
};

void register_jdk_internal_vm_Continuation(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("jdk/internal/vm/Continuation");
}

}  // namespace art
