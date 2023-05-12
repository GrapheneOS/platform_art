%def header():
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/riscv64/asm_support_riscv64.S"

/**
 * RISC-V 64 ABI general notes
 *
 * References
 * - https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-cc.adoc
 * - runtime/arch/riscv64/registers_riscv64.h
 *
 * 32 general purposes registers
 * - fixed purpose: zero, ra, sp, gp, tp, s1
 *     gp/scs: shadow call stack - do not clobber!
 *     s1/tr: ART thread register - do not clobber!
 * - temporaries: t0-t6
 * - arguments: a0-a7
 * - callee saved: ra, s0/fp, s2-s11
 *     s0 is flexible, available to use as a frame pointer if needed.
 *
 * 32 floating point registers
 * - temporaries: ft0-ft11
 * - arguments: fa0-fa7
 * - callee saved: fs0-fs11
 */

// Android references
//   Bytecodes: https://source.android.com/docs/core/runtime/dalvik-bytecode
//   Instruction formats: https://source.android.com/docs/core/runtime/instruction-formats

// Fixed register usages in Nterp.
//    nickname  ABI    reg   purpose
#define xSELF    s1  // x9,   Thread* self pointer
#define xFP      s2  // x18,  interpreted frame pointer: to access locals and args
#define xPC      s3  // x19,  interpreted program counter: to fetch instructions
#define xINST    s4  // x20,  first 16-bit code unit of current instruction
#define xIBASE   s5  // x21,  interpreted instruction base pointer: for computed goto
#define xREFS    s6  // x22,  base of object references of dex registers

#define CFI_TMP  10  // DWARF register number for       a0/x10
#define CFI_DEX  19  // DWARF register number for xPC  /s3/x19
#define CFI_REFS 22  // DWARF register number for xREFS/s6/x22

// Synchronization
// This code follows the RISC-V atomics ABI specification [1].
//
// Object publication.
// new-instance and new-array operations must first perform a `fence w,w` "constructor fence" to
// ensure their new object references are correctly published with a subsequent SET_VREG_OBJECT.
//
// Volatile load/store.
// A volatile load is implemented as: fence rw,rw ; load ; fence r,rw.
// A 32-bit or 64-bit volatile store is implemented as: amoswap.{w,d}.rl
// A volatile store for a narrower type is implemented as: fence rw,w ; store ; fence rw,rw
//
// [1] https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-atomic.adoc

// An assembly entry for nterp.
.macro OAT_ENTRY name
    .type \name, @function
    .hidden \name
    .global \name
    .balign 16
\name:
.endm

.macro SIZE name
    .size \name, .-\name
.endm

// Similar to ENTRY but without the CFI directives.
.macro NAME_START name
    .type \name, @function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    /* Cache alignment for function entry */
    .balign 16
\name:
.endm

.macro NAME_END name
  SIZE \name
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers (we're not holding
// references there), but there is no kDontSave runtime method. So just use the kSaveRefsOnly
// runtime method.
.macro NTERP_TRAMPOLINE name, helper
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME
    call \helper
    RESTORE_SAVE_REFS_ONLY_FRAME
    ld t0, THREAD_EXCEPTION_OFFSET(xSELF)
    bnez t0, nterp_deliver_pending_exception
    ret
END \name
.endm

// Unpack code items from dex format.
// Input: \code_item
// Output:
//   - \registers: register count
//   - \outs: out count
//   - \ins: in count
//   - \code_item: holds instruction array on exit
// Clobbers: t0
.macro FETCH_CODE_ITEM_INFO code_item, registers, outs, ins
    // Check LSB of \code_item. If 1, it's a compact dex file.
    andi t0, \code_item, 0x1
    beqz t0, 1f  // Regular dex.
    unimp  // Compact dex: unimplemented.
1:
    // Unpack values from regular dex format.
    lhu \registers, CODE_ITEM_REGISTERS_SIZE_OFFSET(\code_item)
    lhu \outs, CODE_ITEM_OUTS_SIZE_OFFSET(\code_item)
    lhu \ins, CODE_ITEM_INS_SIZE_OFFSET(\code_item)
    addi \code_item, \code_item, CODE_ITEM_INSNS_OFFSET
.endm

// Set up the stack to start executing the method.
// See runtime/nterp_helpers.cc for a diagram of the setup.
// Input:
//   - a0: ArtMethod*
//   - sp
//   - \code_item: CodeItem*
//   - \cfi_refs: DWARF register number for \refs
// Output:
//   - \code_item: pointer to instruction array `insns_*` on exit
//   - \refs: pointer to obj reference array
//   - \fp: pointer to dex register array
//   - \regs: count of dex registers
//   - \ins: count of in-registers
//   - \old_sp: old stack pointer
//   - sp modified
//
// Clobbers: t0, t1
.macro SETUP_STACK_FRAME code_item, cfi_refs, refs, fp, regs, ins, old_sp
    FETCH_CODE_ITEM_INFO \code_item, \regs, /*out count*/ t1, \ins

    // Compute required frame size: ((2 * \regs) + t1) * 4 + 24
    // - The register array and reference array are each \regs in length.
    // - The out array is t1 in length.
    // - Each register is 4 bytes.
    // - Additional 24 bytes for 3 fields: saved frame pointer, dex pc, and ArtMethod*.
    sll t0, \regs, 1
    add t0, t0, t1
    sll t0, t0, 2
    add t0, t0, 24

    // Compute new stack pointer in t0.
    sub t0, sp, t0
    // 16-byte alignment.
    andi t0, t0, ~0xF

    // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
    // pointer, below the reference array.
    sll t1, t1, 2  // 4 bytes per entry.
    add t1, t0, t1
    add t1, t1, 28  // 24 bytes from 3 fields mentioned earlier, plus 4 for alignment slack.
    andi \refs, t1, -__SIZEOF_POINTER__

    // Set \fp to base of register array, above the reference array. This region is already aligned.
    sll t1, \regs, 2
    add \fp, \refs, t1

    // Set up the stack pointer.
    mv \old_sp, sp
    .cfi_def_cfa_register \old_sp
    mv sp, t0
    sd \old_sp, -8(\refs)
    CFI_DEF_CFA_BREG_PLUS_UCONST \cfi_refs, -8, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Put nulls in reference array.
    beqz \regs, 2f
    mv t1, \refs  // t1 as iterator
1:
    // Write in 8-byte increments, so vreg(0) gets zero'ed too, if \regs is odd.
    sd zero, (t1)
    addi t1, t1, 8
    bltu t1, \fp, 1b
2:
    // Save the ArtMethod*.
    sd a0, (sp)
.endm

.macro EXPORT_PC
    sd xPC, -16(xREFS)
.endm

.macro TEST_IF_MARKING reg, label
    lb \reg, THREAD_IS_GC_MARKING_OFFSET(xSELF)
    bnez \reg, \label
.endm

.macro DO_SUSPEND_CHECK continue
    lw t0, THREAD_FLAGS_OFFSET(xSELF)
    andi t0, t0, THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    beqz t0, \continue
    EXPORT_PC
    call art_quick_test_suspend
.endm

// Clobbers: t0, t1
.macro WRITE_BARRIER_IF_OBJECT is_object, value, holder, uniq
    .if \is_object
    beqz \value, .Lskip_write_barrier_\uniq // No object, skip out.
    ld t0, THREAD_CARD_TABLE_OFFSET(xSELF)
    srli t1, \holder, CARD_TABLE_CARD_SHIFT
    add t1, t0, t1
    sb t0, (t1)
.Lskip_write_barrier_\uniq:
    .endif
.endm

// Fetch one or more half-word units from an offset past the current PC.
// The offset is specified in 16-bit code units.
//
// A \width flag allows reading 32 bits (2 units) or 64 bits (4 units) from the offset.
// The RISC-V ISA supports unaligned accesses for these wider loads.
//
// Default behavior loads one code unit with unsigned zext.
// The \signed flag is for signed sext, for shorter loads.
//
// Does not advance xPC.
.macro FETCH reg, count, signed=0, width=16
    .if \width == 16
      .if \signed
        lh  \reg, (\count*2)(xPC)
      .else
        lhu  \reg, (\count*2)(xPC)
      .endif
    .elseif \width == 32
      .if \signed
        lw  \reg, (\count*2)(xPC)
      .else
        lwu  \reg, (\count*2)(xPC)
      .endif
    .elseif \width == 64
      ld  \reg, (\count*2)(xPC)
    .else
      unimp  // impossible
    .endif
.endm

// Fetch the next instruction, from xPC into xINST.
// Does not advance xPC.
.macro FETCH_INST
    lhu xINST, (xPC)  // zero in upper 48 bits
.endm

// Fetch the next instruction, from xPC into xINST. Advance xPC by \count units, each 2 bytes.
//
// Immediates have a 12-bit offset range from xPC. Thus, \count can range from -1024 to 1023.
//
// Note: Must be placed AFTER anything that can throw an exception, or the exception catch may miss.
// Thus, this macro must be placed after EXPORT_PC.
.macro FETCH_ADVANCE_INST count
    lhu xINST, (\count*2)(xPC)  // zero in upper 48 bits
    addi xPC, xPC, (\count*2)
.endm

// Clobbers: \reg
.macro GET_INST_OPCODE reg
    and \reg, xINST, 0xFF
.endm

// Clobbers: \reg
.macro GOTO_OPCODE reg
    slliw \reg, \reg, ${handler_size_bits}
    add \reg, xIBASE, \reg
    jr \reg
.endm

// Clobbers: t0, t1
.macro FETCH_FROM_THREAD_CACHE reg, miss_label
    // See art::InterpreterCache::IndexOf() for computing index of key within cache array.
    // Entry address:
    //   xSELF + OFFSET + ((xPC>>2 & xFF) << 4)
    // = xSELF + OFFSET + ((xPC & xFF<<2) << 2)
    // = xSELF + ((OFFSET>>2 + (xPC & xFF<<2)) << 2)
    // => ANDI, ADD, SH2ADD
#if (THREAD_INTERPRETER_CACHE_SIZE_LOG2 != 8)
#error Expected interpreter cache array size = 256 elements
#endif
#if (THREAD_INTERPRETER_CACHE_SIZE_SHIFT != 2)
#error Expected interpreter cache entry size = 16 bytes
#endif
#if ((THREAD_INTERPRETER_CACHE_OFFSET & 0x3) != 0)
#error Expected interpreter cache offset to be 4-byte aligned
#endif
    andi t0, xPC, 0xFF << 2
    addi t0, t0, THREAD_INTERPRETER_CACHE_OFFSET >> 2
    sh2add t0, t0, xSELF  // t0 := entry's address
    ld t1, (t0)           // t1 := dex PC
    bne xPC, t1, \miss_label
    ld \reg, 8(t0)        // value: depends on context; see call site
.endm

// Inputs:
//   - a0
//   - xSELF
// Clobbers: t0
.macro CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot, if_not_hot
    lw t0, ART_METHOD_ACCESS_FLAGS_OFFSET(a0)
    // Send flag bit to MSB, branch if bit is unset.
    sll t0, t0, 63 - ART_METHOD_IS_MEMORY_SHARED_FLAG_BIT
    bgez t0, \if_hot

    lw t0, THREAD_SHARED_METHOD_HOTNESS_OFFSET(xSELF)
    beqz t0, \if_hot

    addi t0, t0, -1  // Reduce hotness
    sw t0,  THREAD_SHARED_METHOD_HOTNESS_OFFSET(xSELF)
    j \if_not_hot
.endm

// Increase method hotness before starting the method.
// Clobbers: a0, t0
.macro START_EXECUTING_INSTRUCTIONS
    ld a0, (sp)
    lhu t0, ART_METHOD_HOTNESS_COUNT_OFFSET(a0)
#if (NTERP_HOTNESS_VALUE != 0)
#error Expected 0 for hotness value
#endif
    // If the counter is at zero, handle it in the runtime.
    beqz t0, 3f
    addi t0, t0, -1
    sh t0, ART_METHOD_HOTNESS_COUNT_OFFSET(a0)
1:
    DO_SUSPEND_CHECK continue=2f
2:
    FETCH_INST
    GET_INST_OPCODE t0
    GOTO_OPCODE t0
3:
    CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot=4f, if_not_hot=1b
4:
    mv a1, zero  // dex_pc_ptr=nullptr
    mv a2, zero  // vergs=nullptr
    call nterp_hot_method
    j 2b
.endm

// Typed read, defaults to 32-bit read.
// Clobbers: \reg, \vreg
// Safe if \reg == \vreg.
.macro GET_VREG reg, vreg, is_wide=0
    .if \is_wide
      GET_VREG_WIDE \reg, \vreg
    .else
      slliw \vreg, \vreg, 2  // vreg id to byte offset
      add \vreg, xFP, \vreg  // vreg addr in register array
      lw \reg, (\vreg)       // reg := fp[vreg]
    .endif
.endm

// Typed write, defaults to 32-bit write.
// Clobbers: t0, \reg, \vreg
.macro SET_VREG reg, vreg, is_wide=0
    .if \is_wide
      SET_VREG_WIDE \reg, \vreg
    .else
      slliw \vreg, \vreg, 2  // vreg id to byte offset
      add t0, xFP, \vreg     // vreg addr in register array
      sw \reg, (t0)          // fp[vreg] := reg
      add t0, xREFS, \vreg   // vreg addr in reference array
      sw zero, (t0)          // refs[vreg] := null
    .endif
.endm

// 64 bit read
// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG_WIDE reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add \vreg, xFP, \vreg  // vreg addr in register array
    ld \reg, (\vreg)       // reg := fp[vreg](lo) | fp[vreg+1](hi)
.endm

// 64 bit write
// Clobbers: t0, \vreg
.macro SET_VREG_WIDE reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add t0, xFP, \vreg     // vreg addr in register array
    sd \reg, (t0)          // fp[vreg] := reg(lo) ; fp[vreg+1] := reg(hi)
    add t0, xREFS, \vreg   // vreg addr in reference array
    sd zero, (t0)          // refs[vreg] := null ; refs[vreg+1] := null
.endm

// Object read
// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG_OBJECT reg, vreg
    slliw \vreg, \vreg, 2    // vreg id to byte offset
    add \vreg, xREFS, \vreg  // vreg addr in reference array
    lw \reg, (\vreg)         // reg := refs[vreg]
.endm

// Object write
// Clobbers: t0, \vreg
.macro SET_VREG_OBJECT reg, vreg
    slliw \vreg, \vreg, 2  // vreg id to byte offset
    add t0, xFP, \vreg     // vreg addr in register array
    sw \reg, (t0)          // fp[vreg] := reg
    add t0, xREFS, \vreg   // vreg addr in reference array
    sw \reg, (t0)          // refs[vreg] := reg
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  a0     ArtMethod* callee
 *  a1-a7  method parameters
 */

OAT_ENTRY ExecuteNterpWithClinitImpl
    .cfi_startproc
    unimp
    .cfi_endproc
    .type EndExecuteNterpWithClinitImpl, @function
    .hidden EndExecuteNterpWithClinitImpl
    .global EndExecuteNterpWithClinitImpl
EndExecuteNterpWithClinitImpl:

OAT_ENTRY ExecuteNterpImpl
    .cfi_startproc

    // Check guard page for stack overflow.
    li t0, -STACK_OVERFLOW_RESERVED_BYTES
    add t0, t0, sp
    ld zero, (t0)

    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES

    ld xPC, ART_METHOD_DATA_OFFSET_64(a0)
    SETUP_STACK_FRAME xPC, CFI_REFS, xREFS, xFP, /*reg count*/ s7, /*in count*/s8, /*old sp*/s9

    beqz s8, .Lsetup_execution  // no args
    unimp

.Lsetup_execution:
    CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

    la xIBASE, artNterpAsmInstructionStart
    START_EXECUTING_INSTRUCTIONS
    // NOTE: no fallthrough
    // cfi info continues, and covers the whole nterp implementation.
    SIZE ExecuteNterpImpl

%def footer():
/*
 * ===========================================================================
 *  Common subroutines and data
 * ===========================================================================
 */

    .text
    .align  2


// Enclose all code below in a symbol (which gets printed in backtraces).
NAME_START nterp_helper

common_errDivideByZero:
    EXPORT_PC
    // Control doesn't return, but stack walking needs the return address.
    call art_quick_throw_div_zero

// EndExecuteNterpImpl includes the methods after .cfi_endproc, as we want the runtime to see them
// as part of the Nterp PCs. This label marks the end of PCs contained by the OatQuickMethodHeader
// created for the interpreter entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
.cfi_endproc
NAME_END nterp_helper

// Entrypoints into runtime.
NTERP_TRAMPOLINE nterp_get_class, NterpGetClass
NTERP_TRAMPOLINE nterp_get_static_field, NterpGetStaticField
NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod

ENTRY nterp_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
END nterp_deliver_pending_exception

// gen_mterp.py will inline the following definitions
// within [ExecuteNterpImpl, EndExecuteNterpImpl).
%def instruction_start():
    .type artNterpAsmInstructionStart, @function
    .hidden artNterpAsmInstructionStart
    .global artNterpAsmInstructionStart
artNterpAsmInstructionStart = .L_op_nop
    .text

%def instruction_end():
    .type artNterpAsmInstructionEnd, @function
    .hidden artNterpAsmInstructionEnd
    .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
    unimp

%def opcode_pre():
%   pass
%def opcode_name_prefix():
%   return "nterp_"
%def opcode_start():
    NAME_START nterp_${opcode}
%def opcode_end():
    NAME_END nterp_${opcode}
%def opcode_slow_path_start(name):
    NAME_START ${name}
%def opcode_slow_path_end(name):
    NAME_END ${name}
