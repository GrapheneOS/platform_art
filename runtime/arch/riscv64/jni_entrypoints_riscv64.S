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

#include "asm_support_riscv64.S"

UNDEFINED art_jni_method_start
UNDEFINED art_jni_method_end
UNDEFINED art_jni_read_barrier
UNDEFINED art_jni_method_entry_hook
UNDEFINED art_jni_lock_object_no_inline
UNDEFINED art_jni_lock_object
UNDEFINED art_jni_unlock_object_no_inline
UNDEFINED art_jni_unlock_object


// 8 argument GPRS: a0 - a7 and 8 argument FPRs: fa0 - fa7
#define ALL_ARGS_SIZE (8 * (8 + 8))


.macro SAVE_ALL_ARGS_INCREASE_FRAME extra_space
    // Reserve space for all argument registers, plus the extra space.
    INCREASE_FRAME (ALL_ARGS_SIZE + \extra_space)

    // Argument GPRs a0 - a7.
    SAVE_GPR a0, (8*0)
    SAVE_GPR a1, (8*1)
    SAVE_GPR a2, (8*2)
    SAVE_GPR a3, (8*3)
    SAVE_GPR a4, (8*4)
    SAVE_GPR a5, (8*5)
    SAVE_GPR a6, (8*6)
    SAVE_GPR a7, (8*7)

    // Argument FPRs fa0 - fa7.
    SAVE_FPR fa0, (8*8)
    SAVE_FPR fa1, (8*9)
    SAVE_FPR fa2, (8*10)
    SAVE_FPR fa3, (8*11)
    SAVE_FPR fa4, (8*12)
    SAVE_FPR fa5, (8*13)
    SAVE_FPR fa6, (8*14)
    SAVE_FPR fa7, (8*15)
.endm


.macro RESTORE_ALL_ARGS_DECREASE_FRAME extra_space
    // Argument GPRs a0 - a7.
    RESTORE_GPR a0, (8*0)
    RESTORE_GPR a1, (8*1)
    RESTORE_GPR a2, (8*2)
    RESTORE_GPR a3, (8*3)
    RESTORE_GPR a4, (8*4)
    RESTORE_GPR a5, (8*5)
    RESTORE_GPR a6, (8*6)
    RESTORE_GPR a7, (8*7)

    // Argument FPRs fa0 - fa7.
    RESTORE_FPR fa0, (8*8)
    RESTORE_FPR fa1, (8*9)
    RESTORE_FPR fa2, (8*10)
    RESTORE_FPR fa3, (8*11)
    RESTORE_FPR fa4, (8*12)
    RESTORE_FPR fa5, (8*13)
    RESTORE_FPR fa6, (8*14)
    RESTORE_FPR fa7, (8*15)

    DECREASE_FRAME (ALL_ARGS_SIZE + \extra_space)
.endm


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
    li   t1, (ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
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


// JNI dlsym lookup stub for @CriticalNative.
ENTRY art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method is t6 (loaded by art_quick_generic_jni_trampoline).
    // Bit 0 set means generic JNI.
    // For generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    andi  t6, t6, 1
    beqz  t6, 1f
    j     art_jni_dlsym_lookup_stub
1:
    // TODO(riscv64): implement for code paths other than generic JNI trampoline.
    unimp
END art_jni_dlsym_lookup_critical_stub
