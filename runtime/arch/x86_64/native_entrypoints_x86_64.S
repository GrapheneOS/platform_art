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

#include "asm_support_x86_64.S"
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
//  Argument 0: RDI: The context pointer for ExecuteSwitchImpl.
//  Argument 1: RSI: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: RDX: The value of DEX PC (memory address of the methods bytecode).
DEFINE_FUNCTION ExecuteSwitchImplAsm
    PUSH rbx                 // Spill RBX
    movq %rdx, %rbx          // RBX = DEX PC (callee save register)
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* RAX */, 3 /* RBX */, 0)

    call *%rsi               // Call the wrapped function

    POP rbx                  // Restore RBX
    ret
END_FUNCTION ExecuteSwitchImplAsm

    /*
     * Jni dlsym lookup stub.
     */
DEFINE_FUNCTION art_jni_dlsym_lookup_stub
    // Save callee and GPR args.
    PUSH_ARG r9   // Arg.
    PUSH_ARG r8   // Arg.
    PUSH_ARG rdi  // Arg. (JniEnv for normal and @FastNative)
    PUSH_ARG rsi  // Arg.
    PUSH_ARG rdx  // Arg.
    PUSH_ARG rcx  // Arg.
    // Create space for FPR args, plus padding for alignment
    INCREASE_FRAME 72
    // Save FPRs.
    movq %xmm0, 0(%rsp)
    movq %xmm1, 8(%rsp)
    movq %xmm2, 16(%rsp)
    movq %xmm3, 24(%rsp)
    movq %xmm4, 32(%rsp)
    movq %xmm5, 40(%rsp)
    movq %xmm6, 48(%rsp)
    movq %xmm7, 56(%rsp)
    // prepare call
    movq %gs:THREAD_SELF_OFFSET, %rdi      // RDI := Thread::Current()
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    movq THREAD_TOP_QUICK_FRAME_OFFSET(%rdi), %rax   // uintptr_t tagged_quick_frame
    andq LITERAL(TAGGED_JNI_SP_MASK_TOGGLED64), %rax // ArtMethod** sp
    movq (%rax), %rax                                // ArtMethod* method
#ifdef ART_USE_RESTRICTED_MODE
    // Critical native methods are disabled and treated as normal native methods instead.
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%rax)
#else
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%rax)
#endif
    jne .Llookup_stub_fast_or_critical_native
    call SYMBOL(artFindNativeMethod)  // (Thread*)
    jmp .Llookup_stub_continue
.Llookup_stub_fast_or_critical_native:
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
.Llookup_stub_continue:
    // restore arguments
    movq 0(%rsp), %xmm0
    movq 8(%rsp), %xmm1
    movq 16(%rsp), %xmm2
    movq 24(%rsp), %xmm3
    movq 32(%rsp), %xmm4
    movq 40(%rsp), %xmm5
    movq 48(%rsp), %xmm6
    movq 56(%rsp), %xmm7
    DECREASE_FRAME 72
    POP_ARG rcx  // Arg.
    POP_ARG rdx  // Arg.
    POP_ARG rsi  // Arg.
    POP_ARG rdi  // Arg. (JniEnv for normal and @FastNative)
    POP_ARG r8   // Arg.
    POP_ARG r9   // Arg.
    testq %rax, %rax              // check if returned method code is null
    jz .Lno_native_code_found     // if null, jump to return to handle
    jmp *%rax                     // otherwise, tail call to intended method
.Lno_native_code_found:
    ret
END_FUNCTION art_jni_dlsym_lookup_stub
