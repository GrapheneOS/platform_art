/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "interpreter/interpreter_common.h"
#include "nterp.h"

/*
 * Definitions for targets that support nterp.
 */

namespace art {

namespace interpreter {

bool IsNterpSupported() { return !kPoisonHeapReferences && kReserveMarkingRegister; }

bool CanRuntimeUseNterp() REQUIRES_SHARED(Locks::mutator_lock_) {
  Runtime* runtime = Runtime::Current();
  instrumentation::Instrumentation* instr = runtime->GetInstrumentation();
  // If the runtime is interpreter only, we currently don't use nterp as some
  // parts of the runtime (like instrumentation) make assumption on an
  // interpreter-only runtime to always be in a switch-like interpreter.
  return IsNterpSupported() && !runtime->IsJavaDebuggable() && !instr->EntryExitStubsInstalled() &&
         !instr->InterpretOnly() && !runtime->IsAotCompiler() &&
         !instr->NeedsSlowInterpreterForListeners() &&
         // An async exception has been thrown. We need to go to the switch interpreter. nterp
         // doesn't know how to deal with these so we could end up never dealing with it if we are
         // in an infinite loop.
         !runtime->AreAsyncExceptionsThrown() &&
         (runtime->GetJit() == nullptr || !runtime->GetJit()->JitAtFirstUse());
}

// The entrypoint for nterp, which ArtMethods can directly point to.
extern "C" void ExecuteNterpImpl() REQUIRES_SHARED(Locks::mutator_lock_);

const void* GetNterpEntryPoint() {
  return reinterpret_cast<const void*>(interpreter::ExecuteNterpImpl);
}

// Another entrypoint, which does a clinit check at entry.
extern "C" void ExecuteNterpWithClinitImpl() REQUIRES_SHARED(Locks::mutator_lock_);

const void* GetNterpWithClinitEntryPoint() {
  return reinterpret_cast<const void*>(interpreter::ExecuteNterpWithClinitImpl);
}

/*
 * Verify some constants used by the nterp interpreter.
 */
void CheckNterpAsmConstants() {
  /*
   * If we're using computed goto instruction transitions, make sure
   * none of the handlers overflows the byte limit.  This won't tell
   * which one did, but if any one is too big the total size will
   * overflow.
   */
  const int width = kNterpHandlerSize;
  ptrdiff_t interp_size = reinterpret_cast<uintptr_t>(artNterpAsmInstructionEnd) -
                          reinterpret_cast<uintptr_t>(artNterpAsmInstructionStart);
  if ((interp_size == 0) || (interp_size != (art::kNumPackedOpcodes * width))) {
    LOG(FATAL) << "ERROR: unexpected asm interp size " << interp_size
               << "(did an instruction handler exceed " << width << " bytes?)";
  }
}

}  // namespace interpreter
}  // namespace art
