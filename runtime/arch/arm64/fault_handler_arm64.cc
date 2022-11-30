/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "fault_handler.h"

#include <sys/ucontext.h>

#include "arch/instruction_set.h"
#include "art_method.h"
#include "base/enums.h"
#include "base/hex_dump.h"
#include "base/logging.h"  // For VLOG.
#include "base/macros.h"
#include "registers_arm64.h"
#include "runtime_globals.h"
#include "thread-current-inl.h"

extern "C" void art_quick_throw_stack_overflow();
extern "C" void art_quick_throw_null_pointer_exception_from_signal();
extern "C" void art_quick_implicit_suspend();

//
// ARM64 specific fault handler functions.
//

namespace art {

void FaultManager::GetMethodAndReturnPcAndSp(siginfo_t* siginfo,
                                             void* context,
                                             ArtMethod** out_method,
                                             uintptr_t* out_return_pc,
                                             uintptr_t* out_sp,
                                             bool* out_is_stack_overflow) {
  struct ucontext *uc = reinterpret_cast<struct ucontext *>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);

  // SEGV_MTEAERR (Async MTE fault) is delivered at an arbitrary point after the actual fault.
  // Register contents, including PC and SP, are unrelated to the fault and can only confuse ART
  // signal handlers.
  if (siginfo->si_signo == SIGSEGV && siginfo->si_code == SEGV_MTEAERR) {
    return;
  }

  *out_sp = static_cast<uintptr_t>(sc->sp);
  VLOG(signals) << "sp: " << *out_sp;
  if (*out_sp == 0) {
    return;
  }

  // In the case of a stack overflow, the stack is not valid and we can't
  // get the method from the top of the stack.  However it's in x0.
  uintptr_t* fault_addr = reinterpret_cast<uintptr_t*>(sc->fault_address);
  uintptr_t* overflow_addr = reinterpret_cast<uintptr_t*>(
      reinterpret_cast<uint8_t*>(*out_sp) - GetStackOverflowReservedBytes(InstructionSet::kArm64));
  if (overflow_addr == fault_addr) {
    *out_method = reinterpret_cast<ArtMethod*>(sc->regs[0]);
    *out_is_stack_overflow = true;
  } else {
    // The method is at the top of the stack.
    *out_method = *reinterpret_cast<ArtMethod**>(*out_sp);
    *out_is_stack_overflow = false;
  }

  // Work out the return PC.  This will be the address of the instruction
  // following the faulting ldr/str instruction.
  VLOG(signals) << "pc: " << std::hex
      << static_cast<void*>(reinterpret_cast<uint8_t*>(sc->pc));

  *out_return_pc = sc->pc + 4;
}

bool NullPointerHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info, void* context) {
  if (!IsValidImplicitCheck(info)) {
    return false;
  }
  // The code that looks for the catch location needs to know the value of the
  // PC at the point of call.  For Null checks we insert a GC map that is immediately after
  // the load/store instruction that might cause the fault.

  struct ucontext *uc = reinterpret_cast<struct ucontext*>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);

  // Push the gc map location to the stack and pass the fault address in LR.
  sc->sp -= sizeof(uintptr_t);
  *reinterpret_cast<uintptr_t*>(sc->sp) = sc->pc + 4;
  sc->regs[30] = reinterpret_cast<uintptr_t>(info->si_addr);

  sc->pc = reinterpret_cast<uintptr_t>(art_quick_throw_null_pointer_exception_from_signal);
  VLOG(signals) << "Generating null pointer exception";
  return true;
}

// A suspend check is done using the following instruction:
//      0x...: f94002b5  ldr x21, [x21, #0]
// To check for a suspend check, we examine the instruction that caused the fault (at PC).
bool SuspensionHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info ATTRIBUTE_UNUSED,
                               void* context) {
  constexpr uint32_t kSuspendCheckRegister = 21;
  constexpr uint32_t checkinst =
      0xf9400000 | (kSuspendCheckRegister << 5) | (kSuspendCheckRegister << 0);

  struct ucontext *uc = reinterpret_cast<struct ucontext *>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);
  VLOG(signals) << "checking suspend";

  uint32_t inst = *reinterpret_cast<uint32_t*>(sc->pc);
  VLOG(signals) << "inst: " << std::hex << inst << " checkinst: " << checkinst;
  if (inst != checkinst) {
    // The instruction is not good, not ours.
    return false;
  }

  // This is a suspend check.
  VLOG(signals) << "suspend check match";

  // Set LR so that after the suspend check it will resume after the
  // `ldr x21, [x21,#0]` instruction that triggered the suspend check.
  sc->regs[30] = sc->pc + 4;
  // Arrange for the signal handler to return to `art_quick_implicit_suspend()`.
  sc->pc = reinterpret_cast<uintptr_t>(art_quick_implicit_suspend);

  // Now remove the suspend trigger that caused this fault.
  Thread::Current()->RemoveSuspendTrigger();
  VLOG(signals) << "removed suspend trigger invoking test suspend";

  return true;
}

bool StackOverflowHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info ATTRIBUTE_UNUSED,
                                  void* context) {
  struct ucontext *uc = reinterpret_cast<struct ucontext *>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);
  VLOG(signals) << "stack overflow handler with sp at " << std::hex << &uc;
  VLOG(signals) << "sigcontext: " << std::hex << sc;

  uintptr_t sp = sc->sp;
  VLOG(signals) << "sp: " << std::hex << sp;

  uintptr_t fault_addr = sc->fault_address;
  VLOG(signals) << "fault_addr: " << std::hex << fault_addr;
  VLOG(signals) << "checking for stack overflow, sp: " << std::hex << sp <<
      ", fault_addr: " << fault_addr;

  uintptr_t overflow_addr = sp - GetStackOverflowReservedBytes(InstructionSet::kArm64);

  // Check that the fault address is the value expected for a stack overflow.
  if (fault_addr != overflow_addr) {
    VLOG(signals) << "Not a stack overflow";
    return false;
  }

  VLOG(signals) << "Stack overflow found";

  // Now arrange for the signal handler to return to art_quick_throw_stack_overflow.
  // The value of LR must be the same as it was when we entered the code that
  // caused this fault.  This will be inserted into a callee save frame by
  // the function to which this handler returns (art_quick_throw_stack_overflow).
  sc->pc = reinterpret_cast<uintptr_t>(art_quick_throw_stack_overflow);

  // The kernel will now return to the address in sc->pc.
  return true;
}
}       // namespace art
