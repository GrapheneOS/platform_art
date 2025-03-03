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

#include "asm_support_arm64.S"
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
//  Argument 0: x0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: x1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: x2: The value of DEX PC (memory address of the methods bytecode).
ENTRY ExecuteSwitchImplAsm
    SAVE_TWO_REGS_INCREASE_FRAME x19, xLR, 16
    mov x19, x2                                   // x19 = DEX PC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* x0 */, 19 /* x19 */, 0)
    blr x1                                        // Call the wrapped method.
    RESTORE_TWO_REGS_DECREASE_FRAME x19, xLR, 16
    ret
END ExecuteSwitchImplAsm

    /*
     * Jni dlsym lookup stub.
     */
    .extern artFindNativeMethod
    .extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub
    // spill regs.
    SAVE_ALL_ARGS_INCREASE_FRAME 2 * 8
    stp   x29, x30, [sp, ALL_ARGS_SIZE]
    .cfi_rel_offset x29, ALL_ARGS_SIZE
    .cfi_rel_offset x30, ALL_ARGS_SIZE + 8
    add   x29, sp, ALL_ARGS_SIZE

    mov x0, xSELF   // pass Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    ldr   xIP0, [x0, #THREAD_TOP_QUICK_FRAME_OFFSET]      // uintptr_t tagged_quick_frame
    bic   xIP0, xIP0, #TAGGED_JNI_SP_MASK                 // ArtMethod** sp
    ldr   xIP0, [xIP0]                                    // ArtMethod* method
    ldr   xIP0, [xIP0, #ART_METHOD_ACCESS_FLAGS_OFFSET]   // uint32_t access_flags
#ifdef ART_USE_RESTRICTED_MODE
    // Critical native methods are disabled and treated as normal native methods instead.
    mov   xIP1, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE)
#else
    mov   xIP1, #(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
#endif
    tst   xIP0, xIP1
    b.ne  .Llookup_stub_fast_or_critical_native
    bl    artFindNativeMethod
    b     .Llookup_stub_continue
    .Llookup_stub_fast_or_critical_native:
    bl    artFindNativeMethodRunnable
.Llookup_stub_continue:
    mov   x17, x0    // store result in scratch reg.

    // load spill regs.
    ldp   x29, x30, [sp, #ALL_ARGS_SIZE]
    .cfi_restore x29
    .cfi_restore x30
    RESTORE_ALL_ARGS_DECREASE_FRAME 2 * 8

    cbz   x17, 1f   // is method code null ?
    br    x17       // if non-null, tail call to method's code.

1:
    ret             // restore regs and return to caller to handle exception.
END art_jni_dlsym_lookup_stub
