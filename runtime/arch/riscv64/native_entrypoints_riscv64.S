/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "asm_support_riscv64.S"
#include "interpreter/cfi_asm_support.h"

/*
 * This file contains all native entrypoints that are called using the native ABI and do not
 * transition to the quick ABI. For example: the switch interpreter (using the native ABI) directly
 * calls ExecuteSwitchImplAsm and this code will always return back to the switch interpreter,
 * again using the native ABI. Because of this behaviour ExecuteSwitchImplAsm should be included in
 * this file. This is done so these native entrypoints can be compiled independently to quick
 * entrypoints for cases when the kRuntimeISA and kRuntimeQuickCodeISA do not match.
 *
 * See comment on StackType (thread.h) for definitions and examples of quick ABI/code and
 * native ABI/code.
 */

// Wrap ExecuteSwitchImpl in assembly method which specifies DEX PC for unwinding.
//  Argument 0: a0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: a1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: a2: The value of DEX PC (memory address of the methods bytecode).
ENTRY ExecuteSwitchImplAsm
    INCREASE_FRAME 16
    SAVE_GPR s1, 0
    SAVE_GPR ra, 8

    mv s1, a2   // s1 = DEX PC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* a0 */, 9 /* s1, a.k.a. x9 */, 0)
    jalr a1     // Call the wrapped method.

    RESTORE_GPR s1, 0
    RESTORE_GPR ra, 8
    DECREASE_FRAME 16
    ret
END ExecuteSwitchImplAsm

// JNI dlsym lookup stub.
.extern artFindNativeMethod
.extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub
    SAVE_ALL_ARGS_INCREASE_FRAME 2*8
    SAVE_GPR fp, (ALL_ARGS_SIZE + 0)
    SAVE_GPR ra, (ALL_ARGS_SIZE + 8)
    add  fp, sp, ALL_ARGS_SIZE

    // Call artFindNativeMethod for normal native.
    // Call artFindNativeMethodRunnable for @FastNative or @CriticalNative.
    // Both functions have a single argument: Thread::Current() in a0.
    mv   a0, xSELF
    ld   t0, THREAD_TOP_QUICK_FRAME_OFFSET(a0)   // uintptr_t tagged_quick_frame
    andi t0, t0, ~TAGGED_JNI_SP_MASK             // ArtMethod** sp
    ld   t0, (t0)                                // ArtMethod* method
    lw   t0, ART_METHOD_ACCESS_FLAGS_OFFSET(t0)  // uint32_t access_flags
#ifdef ART_USE_RESTRICTED_MODE
    // Critical native methods are disabled and treated as normal native methods instead.
    li   t1, (ACCESS_FLAGS_METHOD_IS_FAST_NATIVE)
#else
    li   t1, (ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
#endif
    and  t0, t0, t1
    bnez t0, .Llookup_stub_fast_or_critical_native
    call artFindNativeMethod
    j    .Llookup_stub_continue

.Llookup_stub_fast_or_critical_native:
    call  artFindNativeMethodRunnable

.Llookup_stub_continue:
    mv    t0, a0  // store result in a temp reg.
    RESTORE_GPR fp, (ALL_ARGS_SIZE + 0)
    RESTORE_GPR ra, (ALL_ARGS_SIZE + 8)
    RESTORE_ALL_ARGS_DECREASE_FRAME 2*8

    beqz  t0, 1f  // is method code null?
    jr    t0      // if non-null, tail call to method code.
1:
    ret           // restore regs and return to caller to handle exception.
END art_jni_dlsym_lookup_stub
