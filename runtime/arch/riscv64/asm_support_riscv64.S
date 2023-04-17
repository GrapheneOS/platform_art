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

#ifndef ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_S_
#define ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_S_

#include "asm_support_riscv64.h"
#include "interpreter/cfi_asm_support.h"

// Define special registers.

// Register holding Thread::Current().
#define xSELF s1


.macro ENTRY name
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    // ART-compiled functions have OatQuickMethodHeader but assembly functions do not.
    // Prefix the assembly code with 0xFFs, which means there is no method header.
    .byte 0xFF, 0xFF, 0xFF, 0xFF
    // Cache alignment for function entry.
    // Use 0xFF as the last 4 bytes of alignment stand for OatQuickMethodHeader.
    .balign 16, 0xFF
\name:
    .cfi_startproc
.endm


.macro END name
    .cfi_endproc
.endm


.macro UNDEFINED name
    ENTRY \name
        unimp
    END \name
.endm


// The spec is not clear whether the CFA is part of the saved state and tools differ in the
// behaviour, so explicitly set the CFA to avoid any ambiguity.
.macro CFI_RESTORE_STATE_AND_DEF_CFA reg, offset
    .cfi_restore_state
    .cfi_def_cfa \reg, \offset
.endm


.macro INCREASE_FRAME frame_adjustment
    addi sp, sp, -(\frame_adjustment)
    .cfi_adjust_cfa_offset (\frame_adjustment)
.endm


.macro DECREASE_FRAME frame_adjustment
    addi sp, sp, (\frame_adjustment)
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm


.macro SAVE_GPR reg, offset
    sd \reg, (\offset)(sp)
    .cfi_rel_offset \reg, (\offset)
.endm


.macro RESTORE_GPR reg, offset
    ld \reg, (\offset)(sp)
    .cfi_restore \reg
.endm


.macro SAVE_FPR reg, offset
    fsd \reg, (\offset)(sp)
    .cfi_rel_offset \reg, (\offset)
.endm


.macro RESTORE_FPR reg, offset
    fld \reg, (\offset)(sp)
    .cfi_restore \reg
.endm


.macro LOAD_RUNTIME_INSTANCE reg
#if __has_feature(hwaddress_sanitizer)
#error "ART does not support HWASAN on RISC-V yet"
#else
    la \reg, _ZN3art7Runtime9instance_E
#endif
    ld \reg, 0(\reg)
.endm


// We need to save callee-save GPRs on the stack as they may contain references, and must be
// visible to GC (unless the called method holds mutator lock and prevents GC from happening).
// FP callee-saves shall be preserved by whatever runtime function we call, so they do not need
// to be saved.
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 8*(1 + 8 + 7 + 11 + 1))
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(RISCV64) size not as expected."
#endif
    // stack slot (0*8)(sp) is for ArtMethod*

    SAVE_FPR fa0, (1*8)
    SAVE_FPR fa1, (2*8)
    SAVE_FPR fa2, (3*8)
    SAVE_FPR fa3, (4*8)
    SAVE_FPR fa4, (5*8)
    SAVE_FPR fa5, (6*8)
    SAVE_FPR fa6, (7*8)
    SAVE_FPR fa7, (8*8)

    SAVE_GPR fp,  (9*8)  // x8, frame pointer

    // a0 is the method pointer
    SAVE_GPR a1,  (10*8)  // x11
    SAVE_GPR a2,  (11*8)  // x12
    SAVE_GPR a3,  (12*8)  // x13
    SAVE_GPR a4,  (13*8)  // x14
    SAVE_GPR a5,  (14*8)  // x15
    SAVE_GPR a6,  (15*8)  // x16
    SAVE_GPR a7,  (16*8)  // x17

    // s1 is the ART thread register
    SAVE_GPR s2,  (17*8)  // x18
    SAVE_GPR s3,  (18*8)  // x19
    SAVE_GPR s4,  (19*8)  // x20
    SAVE_GPR s5,  (20*8)  // x21
    SAVE_GPR s6,  (21*8)  // x22
    SAVE_GPR s7,  (22*8)  // x23
    SAVE_GPR s8,  (23*8)  // x24
    SAVE_GPR s9,  (24*8)  // x25
    SAVE_GPR s10, (25*8)  // x26
    SAVE_GPR s11, (26*8)  // x27

    SAVE_GPR ra,  (27*8)  // x1, return address
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL base
    // stack slot (0*8)(sp) is for ArtMethod*

    RESTORE_FPR fa0, (1*8)
    RESTORE_FPR fa1, (2*8)
    RESTORE_FPR fa2, (3*8)
    RESTORE_FPR fa3, (4*8)
    RESTORE_FPR fa4, (5*8)
    RESTORE_FPR fa5, (6*8)
    RESTORE_FPR fa6, (7*8)
    RESTORE_FPR fa7, (8*8)

    RESTORE_GPR fp,  (9*8)  // x8, frame pointer

    // a0 is the method pointer
    RESTORE_GPR a1,  (10*8)  // x11
    RESTORE_GPR a2,  (11*8)  // x12
    RESTORE_GPR a3,  (12*8)  // x13
    RESTORE_GPR a4,  (13*8)  // x14
    RESTORE_GPR a5,  (14*8)  // x15
    RESTORE_GPR a6,  (15*8)  // x16
    RESTORE_GPR a7,  (16*8)  // x17

    // s1 is the ART thread register
    RESTORE_GPR s2,  (17*8)  // x18
    RESTORE_GPR s3,  (18*8)  // x19
    RESTORE_GPR s4,  (19*8)  // x20
    RESTORE_GPR s5,  (20*8)  // x21
    RESTORE_GPR s6,  (21*8)  // x22
    RESTORE_GPR s7,  (22*8)  // x23
    RESTORE_GPR s8,  (23*8)  // x24
    RESTORE_GPR s9,  (24*8)  // x25
    RESTORE_GPR s10, (25*8)  // x26
    RESTORE_GPR s11, (26*8)  // x27

    RESTORE_GPR ra,  (27*8)  // x1, return address
.endm


.macro SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL reg
    // ArtMethod* is in reg, store it at the bottom of the stack.
    sd \reg, (sp)

    // Place sp in Thread::Current()->top_quick_frame.
    sd sp, THREAD_TOP_QUICK_FRAME_OFFSET(xSELF)
.endm


.macro SETUP_CALLEE_SAVE_FRAME_COMMON tmpreg, offset
    // art::Runtime* tmpreg = art::Runtime::instance_;
    LOAD_RUNTIME_INSTANCE \tmpreg

    // ArtMethod* tmpreg = Runtime::instance_->callee_save_methods_[<callee-save-frame-type>];
    ld  \tmpreg, \offset(\tmpreg)

    SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL \tmpreg
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    SETUP_CALLEE_SAVE_FRAME_COMMON t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL a0
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
.endm


.macro SAVE_ALL_CALLEE_SAVES
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 8*(12 + 11 + 1 + 1 + 1))
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(RISCV64) size not as expected."
#endif
    // stack slot (0*8)(sp) is for ArtMethod*
    // stack slot (1*8)(sp) is for padding

    // FP callee-saves.
    SAVE_FPR fs0,  (8*2)   // f8
    SAVE_FPR fs1,  (8*3)   // f9
    SAVE_FPR fs2,  (8*4)   // f18
    SAVE_FPR fs3,  (8*5)   // f19
    SAVE_FPR fs4,  (8*6)   // f20
    SAVE_FPR fs5,  (8*7)   // f21
    SAVE_FPR fs6,  (8*8)   // f22
    SAVE_FPR fs7,  (8*9)   // f23
    SAVE_FPR fs8,  (8*10)  // f24
    SAVE_FPR fs9,  (8*11)  // f25
    SAVE_FPR fs10, (8*12)  // f26
    SAVE_FPR fs11, (8*13)  // f27

    // GP callee-saves
    SAVE_GPR s0,  (8*14)  // x8/fp, frame pointer
    // s1 is the ART thread register
    SAVE_GPR s2,  (8*15)  // x18
    SAVE_GPR s3,  (8*16)  // x19
    SAVE_GPR s4,  (8*17)  // x20
    SAVE_GPR s5,  (8*18)  // x21
    SAVE_GPR s6,  (8*19)  // x22
    SAVE_GPR s7,  (8*20)  // x23
    SAVE_GPR s8,  (8*21)  // x24
    SAVE_GPR s9,  (8*22)  // x25
    SAVE_GPR s10, (8*23)  // x26
    SAVE_GPR s11, (8*24)  // x27

    SAVE_GPR ra,  (8*25)  // x1, return address
.endm


.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES
    SETUP_CALLEE_SAVE_FRAME_COMMON t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET
.endm


.macro SETUP_SAVE_EVERYTHING_FRAME
#if (FRAME_SIZE_SAVE_EVERYTHING != 8*(1 + 32 + 27))
#error "FRAME_SIZE_SAVE_EVERYTHING(ARM64) size not as expected."
#endif
    INCREASE_FRAME FRAME_SIZE_SAVE_EVERYTHING

    // stack slot (8*0)(sp) is for ArtMethod*

    // 32 slots for FPRs
    SAVE_FPR ft0,  8*1   // f0
    SAVE_FPR ft1,  8*2   // f1
    SAVE_FPR ft2,  8*3   // f2
    SAVE_FPR ft3,  8*4   // f3
    SAVE_FPR ft4,  8*5   // f4
    SAVE_FPR ft5,  8*6   // f5
    SAVE_FPR ft6,  8*7   // f6
    SAVE_FPR ft7,  8*8   // f7
    SAVE_FPR fs0,  8*9   // f8
    SAVE_FPR fs1,  8*10  // f9
#define SAVE_EVERYTHING_FRAME_OFFSET_FA0 (8*11)
    SAVE_FPR fa0,  8*11  // f10, offset must equal SAVE_EVERYTHING_FRAME_OFFSET_FA0
    SAVE_FPR fa1,  8*12  // f11
    SAVE_FPR fa2,  8*13  // f12
    SAVE_FPR fa3,  8*14  // f13
    SAVE_FPR fa4,  8*15  // f14
    SAVE_FPR fa5,  8*16  // f15
    SAVE_FPR fa6,  8*17  // f16
    SAVE_FPR fa7,  8*18  // f17
    SAVE_FPR fs2,  8*19  // f18
    SAVE_FPR fs3,  8*20  // f19
    SAVE_FPR fs4,  8*21  // f20
    SAVE_FPR fs5,  8*22  // f21
    SAVE_FPR fs6,  8*23  // f22
    SAVE_FPR fs7,  8*24  // f23
    SAVE_FPR fs8,  8*25  // f24
    SAVE_FPR fs9,  8*26  // f25
    SAVE_FPR fs10, 8*27  // f26
    SAVE_FPR fs11, 8*28  // f27
    SAVE_FPR ft8,  8*29  // f28
    SAVE_FPR ft9,  8*30  // f29
    SAVE_FPR ft10, 8*31  // f30
    SAVE_FPR ft11, 8*32  // f31

    // 27 slots for GPRs (excluded: zero/x0, sp/x2, gp/x3, tp/x4, s1/x9 -- the ART thread register)
    SAVE_GPR t0,  8*33  // x5
    SAVE_GPR t1,  8*34  // x6
    SAVE_GPR t2,  8*35  // x7
    SAVE_GPR s0,  8*36  // x8
#define SAVE_EVERYTHING_FRAME_OFFSET_A0 (8*37)
    SAVE_GPR a0,  8*37  // x10, offset must equal SAVE_EVERYTHING_FRAME_OFFSET_A0
    SAVE_GPR a1,  8*38  // x11
    SAVE_GPR a2,  8*39  // x12
    SAVE_GPR a3,  8*40  // x13
    SAVE_GPR a4,  8*41  // x14
    SAVE_GPR a5,  8*42  // x15
    SAVE_GPR a6,  8*43  // x16
    SAVE_GPR a7,  8*44  // x17
    SAVE_GPR s2,  8*45  // x18
    SAVE_GPR s3,  8*46  // x19
    SAVE_GPR s4,  8*47  // x20
    SAVE_GPR s5,  8*48  // x21
    SAVE_GPR s6,  8*49  // x22
    SAVE_GPR s7,  8*50  // x23
    SAVE_GPR s8,  8*51  // x24
    SAVE_GPR s9,  8*52  // x25
    SAVE_GPR s10, 8*53  // x26
    SAVE_GPR s11, 8*54  // x27
    SAVE_GPR t3,  8*55  // x28
    SAVE_GPR t4,  8*56  // x29
    SAVE_GPR t5,  8*57  // x30
    SAVE_GPR t6,  8*58  // x31

    SAVE_GPR ra,  8*59  // x1, return address

    SETUP_CALLEE_SAVE_FRAME_COMMON t0, RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET
.endm


.macro RESTORE_SAVE_EVERYTHING_FRAME
    // stack slot (8*0)(sp) is for ArtMethod*

    // 32 slots for FPRs
    RESTORE_FPR ft0,  (8*1)   // f0
    RESTORE_FPR ft1,  (8*2)   // f1
    RESTORE_FPR ft2,  (8*3)   // f2
    RESTORE_FPR ft3,  (8*4)   // f3
    RESTORE_FPR ft4,  (8*5)   // f4
    RESTORE_FPR ft5,  (8*6)   // f5
    RESTORE_FPR ft6,  (8*7)   // f6
    RESTORE_FPR ft7,  (8*8)   // f7
    RESTORE_FPR fs0,  (8*9)   // f8
    RESTORE_FPR fs1,  (8*10)  // f9
#if SAVE_EVERYTHING_FRAME_OFFSET_FA0 != (8*11)
#error "unexpected SAVE_EVERYTHING_FRAME_OFFSET_FA0"
#endif
    RESTORE_FPR fa0,  (8*11)  // f10, offset must equal SAVE_EVERYTHING_FRAME_OFFSET_FA0
    RESTORE_FPR fa1,  (8*12)  // f11
    RESTORE_FPR fa2,  (8*13)  // f12
    RESTORE_FPR fa3,  (8*14)  // f13
    RESTORE_FPR fa4,  (8*15)  // f14
    RESTORE_FPR fa5,  (8*16)  // f15
    RESTORE_FPR fa6,  (8*17)  // f16
    RESTORE_FPR fa7,  (8*18)  // f17
    RESTORE_FPR fs2,  (8*19)  // f18
    RESTORE_FPR fs3,  (8*20)  // f19
    RESTORE_FPR fs4,  (8*21)  // f20
    RESTORE_FPR fs5,  (8*22)  // f21
    RESTORE_FPR fs6,  (8*23)  // f22
    RESTORE_FPR fs7,  (8*24)  // f23
    RESTORE_FPR fs8,  (8*25)  // f24
    RESTORE_FPR fs9,  (8*26)  // f25
    RESTORE_FPR fs10, (8*27)  // f26
    RESTORE_FPR fs11, (8*28)  // f27
    RESTORE_FPR ft8,  (8*29)  // f28
    RESTORE_FPR ft9,  (8*30)  // f29
    RESTORE_FPR ft10, (8*31)  // f30
    RESTORE_FPR ft11, (8*32)  // f31

    // 26 slots for GPRs (excluded: zero/x0, sp/x2, gp/x3, tp/x4, s1/x9 -- the ART thread register)
    RESTORE_GPR t0,  (8*33)  // x5
    RESTORE_GPR t1,  (8*34)  // x6
    RESTORE_GPR t2,  (8*35)  // x7
    RESTORE_GPR s0,  (8*36)  // x8
#if SAVE_EVERYTHING_FRAME_OFFSET_A0 != (8*37)
#error "unexpected SAVE_EVERYTHING_FRAME_OFFSET_A0"
#endif
    RESTORE_GPR a0,  (8*37)  // x10, offset must equal SAVE_EVERYTHING_FRAME_OFFSET_A0
    RESTORE_GPR a1,  (8*38)  // x11
    RESTORE_GPR a2,  (8*39)  // x12
    RESTORE_GPR a3,  (8*40)  // x13
    RESTORE_GPR a4,  (8*41)  // x14
    RESTORE_GPR a5,  (8*42)  // x15
    RESTORE_GPR a6,  (8*43)  // x16
    RESTORE_GPR a7,  (8*44)  // x17
    RESTORE_GPR s2,  (8*45)  // x18
    RESTORE_GPR s3,  (8*46)  // x19
    RESTORE_GPR s4,  (8*47)  // x20
    RESTORE_GPR s5,  (8*48)  // x21
    RESTORE_GPR s6,  (8*49)  // x22
    RESTORE_GPR s7,  (8*50)  // x23
    RESTORE_GPR s8,  (8*51)  // x24
    RESTORE_GPR s9,  (8*52)  // x25
    RESTORE_GPR s10, (8*53)  // x26
    RESTORE_GPR s11, (8*54)  // x27
    RESTORE_GPR t3,  (8*55)  // x28
    RESTORE_GPR t4,  (8*56)  // x29
    RESTORE_GPR t5,  (8*57)  // x30
    RESTORE_GPR t6,  (8*58)  // x31

    RESTORE_GPR ra,  (8*59)  // x1, return address

    DECREASE_FRAME FRAME_SIZE_SAVE_EVERYTHING
.endm


// Macro that calls through to artDeliverPendingExceptionFromCode, where the pending exception is
// Thread::Current()->exception_ when the runtime method frame is ready.
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    mv a0, xSELF
    call artDeliverPendingExceptionFromCode  // Point of no return.
    unimp                                    // Unreachable.
.endm


// Macro that calls through to artDeliverPendingExceptionFromCode, where the pending exception is
// Thread::Current()->exception_.
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.endm


.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_REG reg
    ld \reg, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez \reg, 1f
    ret
1:
    DELIVER_PENDING_EXCEPTION
.endm

#endif  // ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_S_
