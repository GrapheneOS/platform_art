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

#include "fault_handler.h"

extern "C" void art_quick_throw_stack_overflow();
extern "C" void art_quick_throw_null_pointer_exception_from_signal();
extern "C" void art_quick_implicit_suspend();

// RISCV64 specific fault handler functions (or stubs if unimplemented yet).

namespace art {

uintptr_t FaultManager::GetFaultPc(siginfo_t*, void*) {
  LOG(FATAL) << "FaultManager::GetFaultPc is not implemented for RISC-V";
  return 0;
}

uintptr_t FaultManager::GetFaultSp(void*) {
  LOG(FATAL) << "FaultManager::GetFaultSp is not implemented for RISC-V";
  return 0;
}

bool NullPointerHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "NullPointerHandler::Action is not implemented for RISC-V";
  return false;
}

bool SuspensionHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "SuspensionHandler::Action is not implemented for RISC-V";
  return false;
}

bool StackOverflowHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "StackOverflowHandler::Action is not implemented for RISC-V";
  return false;
}

}  // namespace art
