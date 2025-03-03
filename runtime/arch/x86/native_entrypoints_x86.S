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

#include "asm_support_x86.S"
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
//  Argument 0: ESP+4: The context pointer for ExecuteSwitchImpl.
//  Argument 1: ESP+8: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: ESP+12: The value of DEX PC (memory address of the methods bytecode).
DEFINE_FUNCTION ExecuteSwitchImplAsm
    PUSH ebx                 // Spill EBX; Increments ESP, so arg0 is at ESP+8 now.
    mov 12(%esp), %eax       // EAX = C++ templated interpreter function
    mov 16(%esp), %ebx       // EBX = DEX PC (callee save register)
    mov 8(%esp), %ecx        // ECX = Context argument for the function
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* EAX */, 3 /* EBX */, 0)

    sub LITERAL(4), %esp     // Alignment padding
    CFI_ADJUST_CFA_OFFSET(4)
    push %ecx                // Push argument
    CFI_ADJUST_CFA_OFFSET(4)
    call *%eax               // Call the wrapped function
    addl LITERAL(8), %esp
    CFI_ADJUST_CFA_OFFSET(-8)

    POP ebx                  // Restore EBX
    ret
END_FUNCTION ExecuteSwitchImplAsm

    /*
     * Jni dlsym lookup stub.
     */
DEFINE_FUNCTION art_jni_dlsym_lookup_stub
    INCREASE_FRAME 8              // Align stack.
    pushl %fs:THREAD_SELF_OFFSET  // Pass Thread::Current().
    CFI_ADJUST_CFA_OFFSET(4)
    // Call artFindNativeMethod() for normal native and artFindNativeMethodRunnable()
    // for @FastNative or @CriticalNative.
    movl (%esp), %eax                                // Thread* self
    movl THREAD_TOP_QUICK_FRAME_OFFSET(%eax), %eax   // uintptr_t tagged_quick_frame
    andl LITERAL(TAGGED_JNI_SP_MASK_TOGGLED32), %eax // ArtMethod** sp
    movl (%eax), %eax                                // ArtMethod* method
#ifdef ART_USE_RESTRICTED_MODE
    // Critical native methods are disabled and treated as normal native methods instead.
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE), ART_METHOD_ACCESS_FLAGS_OFFSET(%eax)
#else
    testl LITERAL(ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE), \
          ART_METHOD_ACCESS_FLAGS_OFFSET(%eax)
#endif
    jne .Llookup_stub_fast_or_critical_native
    call SYMBOL(artFindNativeMethod)  // (Thread*)
    jmp .Llookup_stub_continue
.Llookup_stub_fast_or_critical_native:
    call SYMBOL(artFindNativeMethodRunnable)  // (Thread*)
.Llookup_stub_continue:
    DECREASE_FRAME 12             // Remove argument & padding.
    testl %eax, %eax              // Check if returned method code is null.
    jz .Lno_native_code_found     // If null, jump to return to handle.
    jmp *%eax                     // Otherwise, tail call to intended method.
.Lno_native_code_found:
    ret
END_FUNCTION art_jni_dlsym_lookup_stub
