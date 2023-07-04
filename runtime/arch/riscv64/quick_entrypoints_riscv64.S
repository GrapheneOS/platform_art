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


.macro INVOKE_STUB_CREATE_FRAME
    // Save RA, FP, xSELF (current thread), A4, A5 (they will be needed in the invoke stub return).
    INCREASE_FRAME 48
    // Slot (8*0) is used for `ArtMethod*` (if no args), args or padding, see below.
    SAVE_GPR xSELF, (8*1)
    SAVE_GPR a4,    (8*2)
    SAVE_GPR a5,    (8*3)
    SAVE_GPR fp,    (8*4)  // Store FP just under the return address.
    SAVE_GPR ra,    (8*5)

    // Make the new FP point to the location where we stored the old FP.
    // Some stack-walking tools may rely on this simply-linked list of saved FPs.
    addi fp, sp, (8*4)  // save frame pointer
    .cfi_def_cfa fp, 48 - (8*4)

    // We already have space for `ArtMethod*` on the stack but we need space for args above
    // the `ArtMethod*`, so add sufficient space now, pushing the `ArtMethod*` slot down.
    addi t0, a2, 0xf    // Reserve space for arguments and
    andi t0, t0, ~0xf   // round up for 16-byte stack alignment.
    sub  sp, sp, t0

    mv xSELF, a3

    // Copy arguments on stack (4 bytes per slot):
    //   A1: source address
    //   A2: arguments length
    //   T0: destination address if there are any args.

    beqz a2, 2f      // loop through 4-byte arguments from the last to the first
    addi t0, sp, 8   // destination address is bottom of the stack + 8 bytes for ArtMethod* (null)
1:
    addi a2, a2, -4
    add  t1, a1, a2  // T1 is the source address of the next copied argument
    lw   t2, (t1)    // T2 is the 4 bytes at address T1
    add  t1, t0, a2  // T1 is the destination address of the next copied argument
    sw   t2, (t1)    // save T2 at the destination address T1
    bnez a2, 1b
2:
    sd zero, (sp)  // Store null into ArtMethod* at bottom of frame.
.endm


.macro INVOKE_STUB_CALL_AND_RETURN
    // Call the method.
    ld   t0, ART_METHOD_QUICK_CODE_OFFSET_64(a0)
    jalr t0

    addi sp, fp, -(8*4)  // restore SP (see `INVOKE_STUB_CREATE_FRAME`)
    .cfi_def_cfa sp, 48

    // Restore ra, fp, xSELF (current thread) a4 (shorty), a5 (result pointer) from stack.
    RESTORE_GPR xSELF, (8*1)
    RESTORE_GPR a4,    (8*2)
    RESTORE_GPR a5,    (8*3)
    RESTORE_GPR fp,    (8*4)
    RESTORE_GPR ra,    (8*5)
    DECREASE_FRAME 48

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


// The size of the handler emitted by `INVOKE_STUB_LOAD_REG` below.
#define INVOKE_STUB_LOAD_REG_SIZE 8

// The offset within `INVOKE_STUB_LOAD_REG` for skipping arguments.
#define INVOKE_STUB_LOAD_REG_SKIP_OFFSET 6

// Macro for loading an argument into a register.
//  load - instruction used for loading,
//  reg - the register to load,
//  args - pointer to next argument,
//  size - the size of the register - 4 or 8 bytes, used as an offset for the load,
//  handler_reg - the register with the address of the handler (points to this handler on entry),
//  handler_diff - the difference in bytes from the current to the next handler,
//  cont - the base name of the label for continuing the shorty processing loop,
//  sfx - suffix added to all labels to make labels unique for different users.
.macro INVOKE_STUB_LOAD_REG load, reg, args, size, handler_reg, handler_diff, cont, sfx
.Linvoke_stub_\load\reg\sfx:
    \load  \reg, -\size(\args)
    c.addi \handler_reg, \handler_diff
.org .Linvoke_stub_\load\reg\sfx + INVOKE_STUB_LOAD_REG_SKIP_OFFSET  // Enforce skip offset.
    c.j    \cont\sfx
.org .Linvoke_stub_\load\reg\sfx + INVOKE_STUB_LOAD_REG_SIZE  // Enforce handler size.
.endm


// Fill registers a1 to a7 and fa0 to fa7 with parameters.
// Parse the passed shorty to determine which register to load.
//  a5 - shorty,
//  t0 - points to arguments on the stack if any (undefined for static method without args),
//  sfx - suffix added to all labels to make labels unique for different users.
.macro INVOKE_STUB_LOAD_ALL_ARGS sfx
    addi t1, a5, 1                     // Load shorty address, plus one to skip the return type.

    // Load this (if instance method) and record the number of GPRs to fill.
    .ifc \sfx, _instance
        lw   a1, (t0)                  // Load "this" parameter,
        addi t0, t0, 4                 // and increment arg pointer.
        .equ NUM_GPRS_TO_FILL, 6
    .else
        .equ NUM_GPRS_TO_FILL, 7
    .endif
    .equ NUM_FPRS_TO_FILL, 8

    // Load addresses for routines that load argument GPRs and FPRs.
    lla  t4, .Lreg_handlers_start\sfx  // First handler for non-FP args.
    addi t5, t4, (3 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)  // First handler for FP args.

    // Loop to fill registers.
.Lfill_regs\sfx:
    lb   t2, (t1)                      // Load next character in signature,
    addi t1, t1, 1                     // and increment.
    beqz t2, .Lcall_method\sfx         // Exit at end of signature. Shorty 0 terminated.

    li   t3, 'L'
    beq  t2, t3, .Lload_reference\sfx  // Is this a reference?

    li   t3, 'J'
    beq  t2, t3, .Lload_long\sfx       // Is this a long?

    li   t3, 'F'
    beq  t2, t3, .Lload_float\sfx      // Is this a float?

    li   t3, 'D'
    beq  t2, t3, .Lload_double\sfx     // Is this a double?

    // Everything else uses a 4-byte value sign-extened to a 64 bit GPR.
    addi t0, t0, 4
    jalr x0, 0(t4)

.Lload_reference\sfx:
    addi t0, t0, 4
    jalr x0, (NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)(t4)

.Lload_long\sfx:
    addi t0, t0, 8
    jalr x0, (2 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)(t4)

.Lload_float\sfx:
    addi t0, t0, 4
    jalr x0, 0(t5)

.Lload_double\sfx:
    addi t0, t0, 8
    jalr x0, (NUM_FPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)(t5)

.Lreg_handlers_start\sfx:

// Handlers for loading other args (not reference/long/float/double) into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG lw, a1, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG lw, a2, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lw, a3, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lw, a4, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lw, a5, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lw, a6, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lw, a7, t0, 4, t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading reference args into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG lwu, a1, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG lwu, a2, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lwu, a3, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lwu, a4, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lwu, a5, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lwu, a6, t0, 4, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG lwu, a7, t0, 4, t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading long args into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG ld, a1, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG ld, a2, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld, a3, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld, a4, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld, a5, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld, a6, t0, 8, t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld, a7, t0, 8, t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading floats into FPRs.
    INVOKE_STUB_LOAD_REG flw, fa0, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa1, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa2, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa3, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa4, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa5, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa6, t0, 4, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG flw, fa7, t0, 4, t5, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading doubles into FPRs.
    INVOKE_STUB_LOAD_REG fld, fa0, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa1, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa2, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa3, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa4, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa5, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa6, t0, 8, t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld, fa7, t0, 8, t5, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

.Lcall_method\sfx:
.endm


// void art_quick_invoke_stub(ArtMethod* method,   // a0
//                            uint32_t*  args,     // a1
//                            uint32_t   argsize,  // a2
//                            Thread*    self,     // a3
//                            JValue*    result,   // a4
//                            char*      shorty)   // a5
ENTRY art_quick_invoke_stub
    INVOKE_STUB_CREATE_FRAME

    // Load args into registers.
    INVOKE_STUB_LOAD_ALL_ARGS _instance

    // Call the method and return.
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

    // Load args into registers.
    INVOKE_STUB_LOAD_ALL_ARGS _static

    // Call the method and return.
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

    mv   t2, a0       // save pointer to native method code into temporary

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

    ld  t0, 8*16(sp)  // @CriticalNative arg, used by art_jni_dlsym_lookup_critical_stub

    ld  t1, 8*17(sp)  // restore stack
    mv  sp, t1

    jalr  t2  // call native method

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
    CFI_REMEMBER_STATE
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
    CFI_RESTORE_STATE_AND_DEF_CFA sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcall_method_exit_hook:
    fmv.d.x  fa0, a0
    li   a4, FRAME_SIZE_SAVE_REFS_AND_ARGS
    call  art_quick_method_exit_hook
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
    SETUP_SAVE_EVERYTHING_FRAME \
        RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET

    // frame_size is passed in A4 from JITed code and `art_quick_generic_jni_trampoline`.
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
    // Skip slot 8*3(a0) for platform-specific thread pointer gp/x3.
    // Skip slot 8*4(a0) for platform-specific global pointer tp/x4.
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
    ld  s2,   8*18(a0)  // x18
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


// Called by managed code that is attempting to call a method on a proxy class. On entry a0 holds
// the proxy method and a1 holds the receiver. The frame size of the invoked proxy method agrees
// with kSaveRefsAndArgs frame.
.extern artQuickProxyInvokeHandler
ENTRY art_quick_proxy_invoke_handler
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0

    // uint64_t artQuickProxyInvokeHandler(ArtMethod* proxy_method,   // a0
    //                                     mirror::Object* receiver,  // a1
    //                                     Thread* self,              // a2
    //                                     ArtMethod** sp)            // a3
    mv    a2, xSELF                   // pass Thread::Current
    mv    a3, sp                      // pass sp
    call  artQuickProxyInvokeHandler  // (Method* proxy method, receiver, Thread*, sp)

    ld    a2, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez  a2, .Lexception_in_proxy    // success if no exception is pending
    CFI_REMEMBER_STATE
    RESTORE_SAVE_REFS_AND_ARGS_FRAME  // Restore frame
    fmv.d.x  fa0, a0                  // Store result in fa0 in case it was float or double
    ret                               // return on success

.Lexception_in_proxy:
    CFI_RESTORE_STATE_AND_DEF_CFA sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_proxy_invoke_handler


.macro ONE_ARG_RUNTIME_EXCEPTION c_name, cxx_name
.extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context.
    mv  a1, xSELF                     // pass Thread::Current.
    call \cxx_name                     // \cxx_name(arg, Thread*).
    ebreak
END \c_name
.endm

// Called by managed code to deliver an exception.
ONE_ARG_RUNTIME_EXCEPTION art_quick_deliver_exception, artDeliverExceptionFromCode

// Called to attempt to execute an obsolete method.
ONE_ARG_RUNTIME_EXCEPTION art_invoke_obsolete_method_stub, artInvokeObsoleteMethod


ENTRY art_quick_resolution_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME

    // const void* artQuickResolutionTrampoline(ArtMethod* called,         // a0
    //                                          mirror::Object* receiver,  // a1
    //                                          Thread* self,              // a2
    //                                          ArtMethod** sp)            // a3
    mv   a2, xSELF
    mv   a3, sp
    call artQuickResolutionTrampoline
    CFI_REMEMBER_STATE
    beqz a0, 1f
    mv   t0, a0    // Remember returned code pointer in t0.
    ld   a0, (sp)  // artQuickResolutionTrampoline puts called method in *sp.

    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    jr   t0
1:
    CFI_RESTORE_STATE_AND_DEF_CFA sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_resolution_trampoline


ENTRY art_quick_test_suspend
    SETUP_SAVE_EVERYTHING_FRAME \
        RUNTIME_SAVE_EVERYTHING_FOR_SUSPEND_CHECK_METHOD_OFFSET
    mv   a0, xSELF
    call artTestSuspendFromCode
    RESTORE_SAVE_EVERYTHING_FRAME
    ret
END


UNDEFINED art_quick_imt_conflict_trampoline
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
UNDEFINED art_quick_throw_array_bounds
UNDEFINED art_quick_throw_div_zero
UNDEFINED art_quick_throw_null_pointer_exception
UNDEFINED art_quick_throw_stack_overflow
UNDEFINED art_quick_throw_string_bounds
UNDEFINED art_quick_update_inline_cache
UNDEFINED art_quick_indexof
