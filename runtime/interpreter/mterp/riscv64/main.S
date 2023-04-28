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

// An assembly entry that has a OatQuickMethodHeader prefix.
.macro OAT_ENTRY name, end
    .type \name, @function
    .hidden \name
    .global \name
    .balign 16
    // Padding of 3 * 4 bytes to get 16 bytes alignment of code entry.
    .4byte 0, 0, 0
    // OatQuickMethodHeader `data_` field. Note that the top two bits must be clear.
    .4byte (\end - \name)
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
    /* XXX Cache alignment for function entry */
    .balign 16
\name:
.endm

.macro NAME_END name
  SIZE \name
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  XXX   ArtMethod* callee
 *  rest  method parameters
 */

OAT_ENTRY ExecuteNterpWithClinitImpl, EndExecuteNterpWithClinitImpl
    // For simplicity, we don't do a read barrier here, but instead rely
    // on art_quick_resolution_trampoline to always have a suspend point before
    // calling back here.
    unimp
EndExecuteNterpWithClinitImpl:

OAT_ENTRY ExecuteNterpImpl, EndExecuteNterpImpl
    .cfi_startproc
    unimp

%def fetch_from_thread_cache(dest_reg, miss_label):

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
// This is the logical end of ExecuteNterpImpl, where the frame info applies.
// EndExecuteNterpImpl includes the methods below as we want the runtime to
// see them as part of the Nterp PCs.
.cfi_endproc
NAME_END nterp_helper

// This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
// entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

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
    unimp
%def opcode_slow_path_start(name):
    NAME_START ${name}
%def opcode_slow_path_end(name):
    NAME_END ${name}
