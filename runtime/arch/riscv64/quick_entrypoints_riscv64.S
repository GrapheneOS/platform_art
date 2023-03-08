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
#include "interpreter/cfi_asm_support.h"


// Wrap ExecuteSwitchImpl in assembly method which specifies DEX PC for unwinding.
//  Argument 0: a0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: a1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: a2: The value of DEX PC (memory address of the methods bytecode).
ENTRY ExecuteSwitchImplAsm
    INCREASE_FRAME 16
    SAVE_GPR s1 0
    SAVE_GPR ra 8

    mv s1, a2   // s1 = DEX PC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(0 /* a0 */, 9 /* s1, a.k.a. x9 */, 0)
    jalr a1     // Call the wrapped method.

    RESTORE_GPR s1 0
    RESTORE_GPR ra 8
    DECREASE_FRAME 16
    ret
END ExecuteSwitchImplAsm


.macro INVOKE_STUB_CREATE_FRAME
    // Save ra, fp, xSELF (current thread) a4, a5 (they will be needed in the invoke stub return)
    // and callee-save regs s3 - s5 that are clobbered here and in art_quick_invoke_(static_)_stub.
    INCREASE_FRAME 64
    SAVE_GPR fp    (8*0)
    SAVE_GPR xSELF (8*1)
    SAVE_GPR a4    (8*2)
    SAVE_GPR a5    (8*3)
    SAVE_GPR s3    (8*4)
    SAVE_GPR s4    (8*5)
    SAVE_GPR s5    (8*6)
    SAVE_GPR ra    (8*7)

    mv fp, sp  // save frame pointer
    .cfi_def_cfa_register fp

    addi t0, a2, (__SIZEOF_POINTER__ + 0xf) // Reserve space for ArtMethod*, arguments and
    andi t0, t0, ~0xf                       // round up for 16-byte stack alignment.
    sub  sp, sp, t0

    mv xSELF, a3

    // Copy arguments on stack (4 bytes per slot):
    //   a1: source address
    //   a2: arguments length
    //   s3: destination address.

    add s3, sp, 8  // destination address is bottom of the stack + 8 bytes for ArtMethod* (null)

    beqz a2, 2f      // loop through 4-byte arguments from the last to the first
1:
    addi a2, a2, -4
    add  t0, a1, a2  // t0 is the source address of the next copied argument
    lw   t1, (t0)    // t1 is the 4 bytes at address t0
    add  t0, s3, a2  // t0 is the destination address of the next copied argument
    sw   t1, (t0)    // save t1 at the destination address t0
    bnez a2, 1b
2:
    sd zero, (sp)  // Store null into ArtMethod* at bottom of frame.
.endm


.macro INVOKE_STUB_CALL_AND_RETURN
    // Call the method.
    ld   t0, ART_METHOD_QUICK_CODE_OFFSET_64(a0)
    jalr t0

    mv sp, fp  // restore frame pointer
    .cfi_def_cfa_register sp

    // Restore ra, fp, xSELF (current thread) a4 (shorty), a5 (result pointer) and callee-save
    // regs s3 - s5 from stack.
    RESTORE_GPR fp    (8*0)
    RESTORE_GPR xSELF (8*1)
    RESTORE_GPR a4    (8*2)
    RESTORE_GPR a5    (8*3)
    RESTORE_GPR s3    (8*4)
    RESTORE_GPR s4    (8*5)
    RESTORE_GPR s5    (8*6)
    RESTORE_GPR ra    (8*7)

    DECREASE_FRAME 64

    // Load result type (1-byte symbol) from a5.
    // Check result type and store the correct register into the jvalue in memory at a4 address.
    lbu t0, (a5)

    li t1, 'V'  // void (do not store result at all)
    beq t1, t0, 1f

    li t1, 'D'  // double
    beq t1, t0, 2f

    li t1, 'F'  // float
    beq t1, t0, 3f

    // Otherwise, result is in a0 (either 8 or 4 bytes, but it is fine to store 8 bytes as the
    // upper bytes in a0 in that case are zero, and jvalue has enough space).
    sd a0, (a4)
1:
    ret

2:  // double: result in fa0 (8 bytes)
    fsd fa0, (a4)
    ret

3:  // float: result in fa0 (4 bytes)
    fsw fa0, (a4)
    ret
.endm


ENTRY art_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
END art_deliver_pending_exception


#define LOADREG_SIZE 10


// Macro for loading a parameter into a register. Arguments:
//   - counter: a register with offset into the jump table
//   - register: the name of the register to be loaded
//   - return: a label where to jump in the end
// S3 points to the next argument to be loaded (saved on stack by INVOKE_STUB_CREATE_FRAME).
.macro LOADREG_I4 counter register return
    lw   \register, (s3)
    addi s3, s3, 4
    add  \counter, \counter, LOADREG_SIZE
    j    \return
.endm
.macro LOADREG_I8 counter register return
    ld   \register, (s3)
    addi s3, s3, 8
    add  \counter, \counter, LOADREG_SIZE
    j    \return
.endm
.macro LOADREG_F4 counter register return
    flw  \register, (s3)
    addi s3, s3, 4
    add  \counter, \counter, LOADREG_SIZE
    j    \return
.endm
.macro LOADREG_F8 counter register return
    fld  \register, (s3)
    addi s3, s3, 8
    add  \counter, \counter, LOADREG_SIZE
    j    \return
.endm


// A debug mode macro that checks if the specified LOADREG_* procedure has the expected size.
// This is needed because we do not know in advance which instructions the assembler / linker may
// choose to compress, so we cannot determine procedure size from the number of instructions in it.
.macro LOADREG_SIZE_CHECK proc reg
#ifndef NDEBUG
    la t0, 2f
    la t1, 1f
    sub t0, t0, t1
    li t1, LOADREG_SIZE
    beq t0, t1, 2f      // skip both SIGILL and the artificial LOADREG_* call
    unimp               // crash with SIGILL as the LOADREG_* macro has unexpected length
1:
    \proc t0 \reg 2f    // unreachable, arguments do not matter
2:
#endif
.endm


.macro LOADREG_SIZE_CHECK_ALL
    LOADREG_SIZE_CHECK LOADREG_I4 t0
    LOADREG_SIZE_CHECK LOADREG_I8 t0
    LOADREG_SIZE_CHECK LOADREG_F4 ft0
    LOADREG_SIZE_CHECK LOADREG_F8 ft0
.endm


// Helper macro to share the common part of art_quick_invoke_stub and art_quick_invoke_static_stub.
// Fill registers a1 - a7 and fa0 - fa7 with parameters in a loop, parse shorty to determine which
// register to load next. Parameters that do not fit into registers remain on stack.
//
.macro INVOKE_STUB_LOOP maxgprs storeA4 storeA8 storeF4 storeF8 start end
    LOADREG_SIZE_CHECK_ALL  // check that the jump table below has expected slot size

    // Load addresses for routines that load registers.
    la  t3, \storeA4
    la  t4, \storeA8
    la  t5, \storeF4
    la  t6, \storeF8

    // Initialize routine offsets to 0 for integers (s4) and floating point (s5).
    mv  s4, zero
    mv  s5, zero

    mv   t0, a5                // Load shorty address.

    // Loop to fill registers.
\start:
    addi t0, t0, 1             // Increment (first increment skips return type).
    lb   t1, (t0)              // Load next character in signature.
    beqz t1, \end              // Exit at end of signature. Shorty is null-terminated.

    li   t2, 'F'               // Check if current shorty character is 'F' for float.
    bne  t1, t2, 1f
    li   t2, 8 * LOADREG_SIZE  // Maximum 8 FPRs are available.
    beq  s5, t2, 4f            // Skip this load if all registers full.
    add  t2, t5, s5            // Calculate subroutine to jump to for loading float in a register.
    jr   t2

1:
    li   t2, 'D'               // Check if current shorty character is 'D' for double.
    bne  t1, t2, 2f
    li   t2, 8 * LOADREG_SIZE  // Maximum 8 FPRs are available.
    beq  s5, t2, 5f            // Skip this load if all registers full.
    add  t2, t6, s5            // Calculate subroutine to jump to for loading double in a register.
    jr   t2

2:
    li   t2, 'J'               // Check if current shorty character is 'J' for long.
    bne  t1, t2, 3f
    li   t2, \maxgprs * LOADREG_SIZE
    beq  s4, t2, 5f            // Skip this load if all registers full.
    add  t2, t4, s4            // Calculate subroutine to jump to for loading long in a register.
    jr   t2

3:                             // Everything else takes one vReg (4 bytes).
    li   t2, \maxgprs * LOADREG_SIZE
    beq  s4, t2, 4f            // Skip this load if all registers full.
    add  t2, t3, s4            // Calculate subroutine to jump to for loading 4 bytes in a register.
    jr   t2

4:  // advance 4 bytes
    addi s3, s3, 4
    j    \start

5:  // advance 8 bytes
    addi s3, s3, 8
    j    \start
.endm


// void art_quick_invoke_stub(ArtMethod* method,   // a0
//                            uint32_t*  args,     // a1
//                            uint32_t   argsize,  // a2
//                            Thread*    self,     // a3
//                            JValue*    result,   // a4
//                            char*      shorty)   // a5
ENTRY art_quick_invoke_stub
    INVOKE_STUB_CREATE_FRAME

    lw   a1, (s3)   // Load `this`.
    addi s3, s3, 4  // Increment argument pointer.

    INVOKE_STUB_LOOP 6 /* max GPRs available: a0 is for ArtMethod*, a1 is for `this` */ \
        .LstoreA4_1 .LstoreA8_1 .LstoreF4_1 .LstoreF8_1 .LfillRegisters1 .LcallFunction1

.LstoreA4_1:
    LOADREG_I4 s4 a2 .LfillRegisters1
    LOADREG_I4 s4 a3 .LfillRegisters1
    LOADREG_I4 s4 a4 .LfillRegisters1
    LOADREG_I4 s4 a5 .LfillRegisters1
    LOADREG_I4 s4 a6 .LfillRegisters1
    LOADREG_I4 s4 a7 .LfillRegisters1

.LstoreA8_1:
    LOADREG_I8 s4 a2 .LfillRegisters1
    LOADREG_I8 s4 a3 .LfillRegisters1
    LOADREG_I8 s4 a4 .LfillRegisters1
    LOADREG_I8 s4 a5 .LfillRegisters1
    LOADREG_I8 s4 a6 .LfillRegisters1
    LOADREG_I8 s4 a7 .LfillRegisters1

.LstoreF4_1:
    LOADREG_F4 s5 fa0 .LfillRegisters1
    LOADREG_F4 s5 fa1 .LfillRegisters1
    LOADREG_F4 s5 fa2 .LfillRegisters1
    LOADREG_F4 s5 fa3 .LfillRegisters1
    LOADREG_F4 s5 fa4 .LfillRegisters1
    LOADREG_F4 s5 fa5 .LfillRegisters1
    LOADREG_F4 s5 fa6 .LfillRegisters1
    LOADREG_F4 s5 fa7 .LfillRegisters1

.LstoreF8_1:
    LOADREG_F8 s5 fa0 .LfillRegisters1
    LOADREG_F8 s5 fa1 .LfillRegisters1
    LOADREG_F8 s5 fa2 .LfillRegisters1
    LOADREG_F8 s5 fa3 .LfillRegisters1
    LOADREG_F8 s5 fa4 .LfillRegisters1
    LOADREG_F8 s5 fa5 .LfillRegisters1
    LOADREG_F8 s5 fa6 .LfillRegisters1
    LOADREG_F8 s5 fa7 .LfillRegisters1

.LcallFunction1:
    INVOKE_STUB_CALL_AND_RETURN
END art_quick_invoke_stub


// void art_quick_invoke_static_stub(ArtMethod* method,   // a0
//                                   uint32_t*  args,     // a1
//                                   uint32_t   argsize,  // a2
//                                   Thread*    self,     // a3
//                                   JValue*    result,   // a4
//                                   char*      shorty)   // a5
ENTRY art_quick_invoke_static_stub
    INVOKE_STUB_CREATE_FRAME

    // The only difference with art_quick_invoke_stub is the absence of `this` in a1.

    INVOKE_STUB_LOOP 7 /* max GPRs available: a0 is for ArtMethod* */ \
        .LstoreA4_2 .LstoreA8_2 .LstoreF4_2 .LstoreF8_2 .LfillRegisters2 .LcallFunction2

.LstoreA4_2:
    LOADREG_I4 s4 a1 .LfillRegisters2
    LOADREG_I4 s4 a2 .LfillRegisters2
    LOADREG_I4 s4 a3 .LfillRegisters2
    LOADREG_I4 s4 a4 .LfillRegisters2
    LOADREG_I4 s4 a5 .LfillRegisters2
    LOADREG_I4 s4 a6 .LfillRegisters2
    LOADREG_I4 s4 a7 .LfillRegisters2

.LstoreA8_2:
    LOADREG_I8 s4 a1 .LfillRegisters2
    LOADREG_I8 s4 a2 .LfillRegisters2
    LOADREG_I8 s4 a3 .LfillRegisters2
    LOADREG_I8 s4 a4 .LfillRegisters2
    LOADREG_I8 s4 a5 .LfillRegisters2
    LOADREG_I8 s4 a6 .LfillRegisters2
    LOADREG_I8 s4 a7 .LfillRegisters2

.LstoreF4_2:
    LOADREG_F4 s5 fa0 .LfillRegisters2
    LOADREG_F4 s5 fa1 .LfillRegisters2
    LOADREG_F4 s5 fa2 .LfillRegisters2
    LOADREG_F4 s5 fa3 .LfillRegisters2
    LOADREG_F4 s5 fa4 .LfillRegisters2
    LOADREG_F4 s5 fa5 .LfillRegisters2
    LOADREG_F4 s5 fa6 .LfillRegisters2
    LOADREG_F4 s5 fa7 .LfillRegisters2

.LstoreF8_2:
    LOADREG_F8 s5 fa0 .LfillRegisters2
    LOADREG_F8 s5 fa1 .LfillRegisters2
    LOADREG_F8 s5 fa2 .LfillRegisters2
    LOADREG_F8 s5 fa3 .LfillRegisters2
    LOADREG_F8 s5 fa4 .LfillRegisters2
    LOADREG_F8 s5 fa5 .LfillRegisters2
    LOADREG_F8 s5 fa6 .LfillRegisters2
    LOADREG_F8 s5 fa7 .LfillRegisters2
.LcallFunction2:

    INVOKE_STUB_CALL_AND_RETURN
END art_quick_invoke_static_stub


ENTRY art_quick_generic_jni_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0

    // Save sp, so we can have static CFI info.
    mv   fp, sp
    .cfi_def_cfa_register fp

    li   t0, GENERIC_JNI_TRAMPOLINE_RESERVED_AREA
    sub  sp, sp, t0

    mv   a0, xSELF    // Thread*
    mv   a1, fp       // SP for the managed frame.
    mv   a2, sp       // reserved area for arguments and other saved data (up to managed frame)
    call artQuickGenericJniTrampoline

    // Check for error (class init check or locking for synchronized native method can throw).
    beqz a0, .Lexception_in_native

    mv   t0, a0       // save pointer to native method code into temporary

    // Load argument GPRs from stack (saved there by artQuickGenericJniTrampoline).
    ld  a0, 8*0(sp)   // JniEnv* for the native method
    ld  a1, 8*1(sp)
    ld  a2, 8*2(sp)
    ld  a3, 8*3(sp)
    ld  a4, 8*4(sp)
    ld  a5, 8*5(sp)
    ld  a6, 8*6(sp)
    ld  a7, 8*7(sp)

    // Load argument FPRs from stack (saved there by artQuickGenericJniTrampoline).
    fld  fa0, 8*8(sp)
    fld  fa1, 8*9(sp)
    fld  fa2, 8*10(sp)
    fld  fa3, 8*11(sp)
    fld  fa4, 8*12(sp)
    fld  fa5, 8*13(sp)
    fld  fa6, 8*14(sp)
    fld  fa7, 8*15(sp)

    ld  t6, 8*16(sp)  // @CriticalNative arg, used by art_jni_dlsym_lookup_critical_stub

    ld  t1, 8*17(sp)  // restore stack
    mv  sp, t1

    jalr  t0  // call native method

    // result sign extension is handled in C code, prepare for artQuickGenericJniEndTrampoline call:
    // uint64_t artQuickGenericJniEndTrampoline(Thread* self,       // a0
    //                                          jvalue result,      // a1 (need to move from a0)
    //                                          uint64_t result_f)  // a2 (need to move from fa0)
    mv  a1, a0
    mv  a0, xSELF
    fmv.x.d  a2, fa0
    call artQuickGenericJniEndTrampoline

    // Pending exceptions possible.
    ld   t0, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez t0, .Lexception_in_native

    // Tear down the alloca.
    mv   sp, fp
    .cfi_remember_state
    .cfi_def_cfa_register sp

    LOAD_RUNTIME_INSTANCE a1
    lb   a1, RUN_EXIT_HOOKS_OFFSET_FROM_RUNTIME_INSTANCE(a1)
    bnez a1, .Lcall_method_exit_hook

.Lcall_method_exit_hook_done:
    // This does not clobber the result register a0. a1 is not used for result as the managed code
    // does not have a 128-bit type. Alternatively we could restore a subset of these registers.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    fmv.d.x  fa0, a0
    ret
    CFI_RESTORE_STATE_AND_DEF_CFA sp FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcall_method_exit_hook:
    fmv.d.x  fa0, a0
    li   a4, FRAME_SIZE_SAVE_REFS_AND_ARGS
    jal  art_quick_method_exit_hook
    j    .Lcall_method_exit_hook_done

.Lexception_in_native:
    // Move to a1 then sp to please assembler.
    ld   a1, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
    addi sp, a1, -1  // Remove the GenericJNI tag.
    call art_deliver_pending_exception
END art_quick_generic_jni_trampoline


ENTRY art_quick_to_interpreter_bridge
    SETUP_SAVE_REFS_AND_ARGS_FRAME

    // uint64_t artQuickToInterpreterBridge(ArtMethod* method, Thread* self, ArtMethod** sp)
    // a0 will contain ArtMethod*
    mv   a1, xSELF
    mv   a2, sp
    call artQuickToInterpreterBridge

    // TODO: no need to restore arguments in this case.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    fmv.d.x  fa0, a0  // copy the result to FP result register

    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG t0
END art_quick_to_interpreter_bridge


    .extern artMethodExitHook
ENTRY art_quick_method_exit_hook
    SETUP_SAVE_EVERYTHING_FRAME

    addi a3, sp, SAVE_EVERYTHING_FRAME_OFFSET_FA0  // FP result ptr in kSaveEverything frame
    addi a2, sp, SAVE_EVERYTHING_FRAME_OFFSET_A0   // integer result ptr in kSaveEverything frame
    addi a1, sp, FRAME_SIZE_SAVE_EVERYTHING        // ArtMethod**
    mv   a0, xSELF                                 // Thread::Current
    call artMethodExitHook                         // (Thread*, ArtMethod**, gpr_res*, fpr_res*,
                                                   // frame_size)

    // Normal return.
    RESTORE_SAVE_EVERYTHING_FRAME
    ret
END art_quick_method_exit_hook


// On entry a0 is uintptr_t* gprs_ and a1 is uint64_t* fprs_.
// Both must reside on the stack, between current sp and target sp.
ENTRY art_quick_do_long_jump
    // Load FPRs
    fld  ft0,  8*0(a1)   // f0
    fld  ft1,  8*1(a1)   // f1
    fld  ft2,  8*2(a1)   // f2
    fld  ft3,  8*3(a1)   // f3
    fld  ft4,  8*4(a1)   // f4
    fld  ft5,  8*5(a1)   // f5
    fld  ft6,  8*6(a1)   // f6
    fld  ft7,  8*7(a1)   // f7
    fld  fs0,  8*8(a1)   // f8
    fld  fs1,  8*9(a1)   // f9
    fld  fa0,  8*10(a1)  // f10
    fld  fa1,  8*11(a1)  // f11
    fld  fa2,  8*12(a1)  // f12
    fld  fa3,  8*13(a1)  // f13
    fld  fa4,  8*14(a1)  // f14
    fld  fa5,  8*15(a1)  // f15
    fld  fa6,  8*16(a1)  // f16
    fld  fa7,  8*17(a1)  // f17
    fld  fs2,  8*18(a1)  // f18
    fld  fs3,  8*19(a1)  // f19
    fld  fs4,  8*20(a1)  // f20
    fld  fs5,  8*21(a1)  // f21
    fld  fs6,  8*22(a1)  // f22
    fld  fs7,  8*23(a1)  // f23
    fld  fs8,  8*24(a1)  // f24
    fld  fs9,  8*25(a1)  // f25
    fld  fs10, 8*26(a1)  // f26
    fld  fs11, 8*27(a1)  // f27
    fld  ft8,  8*28(a1)  // f28
    fld  ft9,  8*29(a1)  // f29
    fld  ft10, 8*30(a1)  // f30
    fld  ft11, 8*31(a1)  // f31

    // Load GPRs.
    // Skip slot 8*0(a0) for zero/x0 as it is hard-wired zero.
    ld  ra,   8*1(a0)   // x1
    // Skip slot 8*2(a0) for sp/x2 as it is set below.
    // Skip slot 8*3(a0) for platform-specific thread pointer tp/x3.
    // Skip slot 8*4(a0) for platform-specific global pointer gp/x4.
    // Skip slot 8*5(a0) for t0/x5 as it is clobbered below.
    // Skip slot 8*6(a0) for t1/x6 as it is clobbered below.
    ld  t2,   8*7(a0)   // x7
    ld  s0,   8*8(a0)   // x8
    ld  s1,   8*9(a0)   // x9
    // Delay loading a0 as the base is in a0.
    ld  a1,   8*11(a0)  // x11
    ld  a2,   8*12(a0)  // x12
    ld  a3,   8*13(a0)  // x13
    ld  a4,   8*14(a0)  // x14
    ld  a5,   8*15(a0)  // x15
    ld  a6,   8*16(a0)  // x16
    ld  a7,   8*17(a0)  // x17
    // Skip slot 8*18(a0) for shadow stack s2/x18.
    ld  s3,   8*19(a0)  // x19
    ld  s4,   8*20(a0)  // x20
    ld  s5,   8*21(a0)  // x21
    ld  s6,   8*22(a0)  // x22
    ld  s7,   8*23(a0)  // x23
    ld  s8,   8*24(a0)  // x24
    ld  s9,   8*25(a0)  // x25
    ld  s10,  8*26(a0)  // x26
    ld  s11,  8*27(a0)  // x27
    ld  t3,   8*28(a0)  // x28
    ld  t4,   8*29(a0)  // x29
    ld  t5,   8*30(a0)  // x30
    ld  t6,   8*31(a0)  // x31

    // Load sp to t0.
    ld  t0, 8*2(a0)

    // Load PC to t1, it is in the last stack slot.
    ld  t1, 8*32(a0)

    // Now load a0.
    ld  a0, 8*10(a0)  // x10

    // Set sp. Do not access fprs_ and gprs_ from now, they are below sp.
    mv sp, t0

    jr  t1
END art_quick_do_long_jump


UNDEFINED art_quick_imt_conflict_trampoline
UNDEFINED art_invoke_obsolete_method_stub
UNDEFINED art_quick_proxy_invoke_handler
UNDEFINED art_quick_resolution_trampoline
UNDEFINED art_quick_deoptimize_from_compiled_code
UNDEFINED art_quick_string_builder_append
UNDEFINED art_quick_compile_optimized
UNDEFINED art_quick_method_entry_hook
UNDEFINED art_quick_check_instance_of
UNDEFINED art_quick_osr_stub

UNDEFINED art_quick_alloc_array_resolved_dlmalloc
UNDEFINED art_quick_alloc_array_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_dlmalloc
UNDEFINED art_quick_alloc_object_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_dlmalloc
UNDEFINED art_quick_alloc_object_initialized_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_object_dlmalloc
UNDEFINED art_quick_alloc_string_object_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_dlmalloc
UNDEFINED art_quick_alloc_string_from_string_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_rosalloc
UNDEFINED art_quick_alloc_array_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_rosalloc
UNDEFINED art_quick_alloc_array_resolved8_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_rosalloc
UNDEFINED art_quick_alloc_array_resolved16_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_rosalloc
UNDEFINED art_quick_alloc_array_resolved32_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_rosalloc
UNDEFINED art_quick_alloc_array_resolved64_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_rosalloc
UNDEFINED art_quick_alloc_object_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_rosalloc
UNDEFINED art_quick_alloc_object_initialized_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_rosalloc
UNDEFINED art_quick_alloc_object_with_checks_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_object_rosalloc
UNDEFINED art_quick_alloc_string_object_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_rosalloc
UNDEFINED art_quick_alloc_string_from_chars_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_rosalloc
UNDEFINED art_quick_alloc_string_from_string_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_bump_pointer
UNDEFINED art_quick_alloc_array_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_resolved_bump_pointer
UNDEFINED art_quick_alloc_object_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_initialized_bump_pointer
UNDEFINED art_quick_alloc_object_initialized_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_object_bump_pointer
UNDEFINED art_quick_alloc_string_object_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_string_bump_pointer
UNDEFINED art_quick_alloc_string_from_string_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved_tlab
UNDEFINED art_quick_alloc_array_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_tlab
UNDEFINED art_quick_alloc_array_resolved8_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_tlab
UNDEFINED art_quick_alloc_array_resolved16_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_tlab
UNDEFINED art_quick_alloc_array_resolved32_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_tlab
UNDEFINED art_quick_alloc_array_resolved64_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_tlab
UNDEFINED art_quick_alloc_object_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_tlab
UNDEFINED art_quick_alloc_object_initialized_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_tlab
UNDEFINED art_quick_alloc_object_with_checks_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_tlab
UNDEFINED art_quick_alloc_string_object_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_tlab
UNDEFINED art_quick_alloc_string_from_bytes_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_tlab
UNDEFINED art_quick_alloc_string_from_chars_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_tlab
UNDEFINED art_quick_alloc_string_from_string_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved_region
UNDEFINED art_quick_alloc_array_resolved_region_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region
UNDEFINED art_quick_alloc_array_resolved8_region_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region
UNDEFINED art_quick_alloc_array_resolved16_region_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region
UNDEFINED art_quick_alloc_array_resolved32_region_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region
UNDEFINED art_quick_alloc_array_resolved64_region_instrumented
UNDEFINED art_quick_alloc_object_resolved_region
UNDEFINED art_quick_alloc_object_resolved_region_instrumented
UNDEFINED art_quick_alloc_object_initialized_region
UNDEFINED art_quick_alloc_object_initialized_region_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region
UNDEFINED art_quick_alloc_object_with_checks_region_instrumented
UNDEFINED art_quick_alloc_string_object_region
UNDEFINED art_quick_alloc_string_object_region_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region
UNDEFINED art_quick_alloc_string_from_bytes_region_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region
UNDEFINED art_quick_alloc_string_from_chars_region_instrumented
UNDEFINED art_quick_alloc_string_from_string_region
UNDEFINED art_quick_alloc_string_from_string_region_instrumented
UNDEFINED art_quick_alloc_array_resolved_region_tlab
UNDEFINED art_quick_alloc_array_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region_tlab
UNDEFINED art_quick_alloc_array_resolved8_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region_tlab
UNDEFINED art_quick_alloc_array_resolved16_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region_tlab
UNDEFINED art_quick_alloc_array_resolved32_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region_tlab
UNDEFINED art_quick_alloc_array_resolved64_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_region_tlab
UNDEFINED art_quick_alloc_object_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_region_tlab
UNDEFINED art_quick_alloc_object_initialized_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region_tlab
UNDEFINED art_quick_alloc_object_with_checks_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_region_tlab
UNDEFINED art_quick_alloc_string_object_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region_tlab
UNDEFINED art_quick_alloc_string_from_chars_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_region_tlab
UNDEFINED art_quick_alloc_string_from_string_region_tlab_instrumented
UNDEFINED art_quick_initialize_static_storage
UNDEFINED art_quick_resolve_type_and_verify_access
UNDEFINED art_quick_resolve_type
UNDEFINED art_quick_resolve_method_handle
UNDEFINED art_quick_resolve_method_type
UNDEFINED art_quick_resolve_string
UNDEFINED art_quick_set8_instance
UNDEFINED art_quick_set8_static
UNDEFINED art_quick_set16_instance
UNDEFINED art_quick_set16_static
UNDEFINED art_quick_set32_instance
UNDEFINED art_quick_set32_static
UNDEFINED art_quick_set64_instance
UNDEFINED art_quick_set64_static
UNDEFINED art_quick_set_obj_instance
UNDEFINED art_quick_set_obj_static
UNDEFINED art_quick_get_byte_instance
UNDEFINED art_quick_get_boolean_instance
UNDEFINED art_quick_get_short_instance
UNDEFINED art_quick_get_char_instance
UNDEFINED art_quick_get32_instance
UNDEFINED art_quick_get64_instance
UNDEFINED art_quick_get_obj_instance
UNDEFINED art_quick_get_byte_static
UNDEFINED art_quick_get_boolean_static
UNDEFINED art_quick_get_short_static
UNDEFINED art_quick_get_char_static
UNDEFINED art_quick_get32_static
UNDEFINED art_quick_get64_static
UNDEFINED art_quick_get_obj_static
UNDEFINED art_quick_aput_obj
UNDEFINED art_quick_lock_object_no_inline
UNDEFINED art_quick_lock_object
UNDEFINED art_quick_unlock_object_no_inline
UNDEFINED art_quick_unlock_object
UNDEFINED art_quick_invoke_direct_trampoline_with_access_check
UNDEFINED art_quick_invoke_interface_trampoline_with_access_check
UNDEFINED art_quick_invoke_static_trampoline_with_access_check
UNDEFINED art_quick_invoke_super_trampoline_with_access_check
UNDEFINED art_quick_invoke_virtual_trampoline_with_access_check
UNDEFINED art_quick_invoke_polymorphic
UNDEFINED art_quick_invoke_custom
UNDEFINED art_quick_test_suspend
UNDEFINED art_quick_deliver_exception
UNDEFINED art_quick_throw_array_bounds
UNDEFINED art_quick_throw_div_zero
UNDEFINED art_quick_throw_null_pointer_exception
UNDEFINED art_quick_throw_stack_overflow
UNDEFINED art_quick_throw_string_bounds
UNDEFINED art_quick_update_inline_cache
UNDEFINED art_jni_monitored_method_start
UNDEFINED art_jni_monitored_method_end
UNDEFINED art_quick_indexof
