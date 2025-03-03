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

#include "asm_support_arm.S"
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
//  Argument 0: r0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: r1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: r2: The value of DEX PC (memory address of the methods bytecode).
ENTRY ExecuteSwitchImplAsm
    push {r4, lr}                                 // 2 words of callee saves.
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r4, 0
    .cfi_rel_offset lr, 4
    mov r4, r2                                    // r4 = DEX PC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* r0 */, 4 /* r4 */, 0)
    blx r1                                        // Call the wrapped method.
    pop {r4, pc}
END ExecuteSwitchImplAsm

    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub
    push   {r0, r1, r2, r3, lr}           @ spill regs
    .cfi_adjust_cfa_offset 20
    .cfi_rel_offset lr, 16
    sub    sp, #12                        @ pad stack pointer to align frame
    .cfi_adjust_cfa_offset 12

    mov    r0, rSELF                      @ pass Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    ldr    ip, [r0, #THREAD_TOP_QUICK_FRAME_OFFSET]   // uintptr_t tagged_quick_frame
    bic    ip, #TAGGED_JNI_SP_MASK                    // ArtMethod** sp
    ldr    ip, [ip]                                   // ArtMethod* method
    ldr    ip, [ip, #ART_METHOD_ACCESS_FLAGS_OFFSET]  // uint32_t access_flags
#ifdef ART_USE_RESTRICTED_MODE
    // Critical native methods are disabled and treated as normal native methods instead.
    tst    ip, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE)
#else
    tst    ip, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
#endif
    bne    .Llookup_stub_fast_or_critical_native
    blx    artFindNativeMethod
    b      .Llookup_stub_continue
.Llookup_stub_fast_or_critical_native:
    blx    artFindNativeMethodRunnable
.Llookup_stub_continue:
    mov    r12, r0                        @ save result in r12

    add    sp, #12                        @ restore stack pointer
    .cfi_adjust_cfa_offset -12
    CFI_REMEMBER_STATE
    cbz    r0, 1f                         @ is method code null?
    pop    {r0, r1, r2, r3, lr}           @ restore regs
    .cfi_adjust_cfa_offset -20
    .cfi_restore lr
    bx     r12                            @ if non-null, tail call to method's code
1:
    CFI_RESTORE_STATE_AND_DEF_CFA sp, 20
    pop    {r0, r1, r2, r3, pc}           @ restore regs and return to caller to handle exception
END art_jni_dlsym_lookup_stub

