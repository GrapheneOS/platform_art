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

#include "assembler_riscv64.h"

#include "base/bit_utils.h"
#include "base/casts.h"
#include "base/memory_region.h"

namespace art {
namespace riscv64 {

static_assert(static_cast<size_t>(kRiscv64PointerSize) == kRiscv64DoublewordSize,
              "Unexpected Riscv64 pointer size.");
static_assert(kRiscv64PointerSize == PointerSize::k64, "Unexpected Riscv64 pointer size.");

void Riscv64Assembler::FinalizeCode() {
}

void Riscv64Assembler::FinalizeInstructions(const MemoryRegion& region) {
  Assembler::FinalizeInstructions(region);
}

void Riscv64Assembler::Emit(uint32_t value) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  buffer_.Emit<uint32_t>(value);
}

/////////////////////////////// RV64 VARIANTS extension ///////////////////////////////

/////////////////////////////// RV64 "IM" Instructions ///////////////////////////////

// Load instructions (RV32I+RV64I): opcode = 0x03, funct3 from 0x0 ~ 0x6

void Riscv64Assembler::Lb(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x0, rd, 0x03);
}

void Riscv64Assembler::Lh(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x1, rd, 0x03);
}

void Riscv64Assembler::Lw(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x2, rd, 0x03);
}

void Riscv64Assembler::Ld(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x3, rd, 0x03);
}

void Riscv64Assembler::Lbu(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x4, rd, 0x03);
}

void Riscv64Assembler::Lhu(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x5, rd, 0x03);
}

void Riscv64Assembler::Lwu(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x6, rd, 0x3);
}

// Store instructions (RV32I+RV64I): opcode = 0x23, funct3 from 0x0 ~ 0x3

void Riscv64Assembler::Sb(XRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x0, 0x23);
}

void Riscv64Assembler::Sh(XRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x1, 0x23);
}

void Riscv64Assembler::Sw(XRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x2, 0x23);
}

void Riscv64Assembler::Sd(XRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x3, 0x23);
}

// IMM ALU instructions (RV32I): opcode = 0x13, funct3 from 0x0 ~ 0x7

void Riscv64Assembler::Addi(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x0, rd, 0x13);
}

void Riscv64Assembler::Slti(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x2, rd, 0x13);
}

void Riscv64Assembler::Sltiu(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x3, rd, 0x13);
}

void Riscv64Assembler::Xori(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x4, rd, 0x13);
}

void Riscv64Assembler::Ori(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x6, rd, 0x13);
}

void Riscv64Assembler::Andi(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x7, rd, 0x13);
}

// 0x1 Split: 0x0(6b) + imm12(6b)
void Riscv64Assembler::Slli(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 64) << shamt;
  EmitI6(0x0, shamt, rs1, 0x1, rd, 0x13);
}

// 0x5 Split: 0x0(6b) + imm12(6b)
void Riscv64Assembler::Srli(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 64) << shamt;
  EmitI6(0x0, shamt, rs1, 0x5, rd, 0x13);
}

// 0x5 Split: 0x10(6b) + imm12(6b)
void Riscv64Assembler::Srai(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 64) << shamt;
  EmitI6(0x10, shamt, rs1, 0x5, rd, 0x13);
}

// ALU instructions (RV32I): opcode = 0x33, funct3 from 0x0 ~ 0x7

void Riscv64Assembler::Add(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x0, rd, 0x33);
}

void Riscv64Assembler::Sub(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x20, rs2, rs1, 0x0, rd, 0x33);
}

void Riscv64Assembler::Slt(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x02, rd, 0x33);
}

void Riscv64Assembler::Sltu(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x03, rd, 0x33);
}

void Riscv64Assembler::Xor(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x04, rd, 0x33);
}

void Riscv64Assembler::Or(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x06, rd, 0x33);
}

void Riscv64Assembler::And(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x07, rd, 0x33);
}

void Riscv64Assembler::Sll(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x01, rd, 0x33);
}

void Riscv64Assembler::Srl(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x05, rd, 0x33);
}

void Riscv64Assembler::Sra(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x20, rs2, rs1, 0x05, rd, 0x33);
}

// 32bit Imm ALU instructions (RV64I): opcode = 0x1b, funct3 from 0x0, 0x1, 0x5

void Riscv64Assembler::Addiw(XRegister rd, XRegister rs1, int32_t imm12) {
  EmitI(imm12, rs1, 0x0, rd, 0x1b);
}

void Riscv64Assembler::Slliw(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 32) << shamt;
  EmitR(0x0, shamt, rs1, 0x1, rd, 0x1b);
}

void Riscv64Assembler::Srliw(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 32) << shamt;
  EmitR(0x0, shamt, rs1, 0x5, rd, 0x1b);
}

void Riscv64Assembler::Sraiw(XRegister rd, XRegister rs1, int32_t shamt) {
  CHECK(static_cast<uint32_t>(shamt) < 32) << shamt;
  EmitR(0x20, shamt, rs1, 0x5, rd, 0x1b);
}

// 32bit ALU instructions (RV64I): opcode = 0x3b, funct3 from 0x0 ~ 0x7

void Riscv64Assembler::Addw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x0, rd, 0x3b);
}

void Riscv64Assembler::Subw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x20, rs2, rs1, 0x0, rd, 0x3b);
}

void Riscv64Assembler::Sllw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x1, rd, 0x3b);
}

void Riscv64Assembler::Srlw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x0, rs2, rs1, 0x5, rd, 0x3b);
}

void Riscv64Assembler::Sraw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x20, rs2, rs1, 0x5, rd, 0x3b);
}

// RV32M Standard Extension: opcode = 0x33, funct3 from 0x0 ~ 0x7

void Riscv64Assembler::Mul(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x0, rd, 0x33);
}

void Riscv64Assembler::Mulh(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x1, rd, 0x33);
}

void Riscv64Assembler::Mulhsu(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x2, rd, 0x33);
}

void Riscv64Assembler::Mulhu(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x3, rd, 0x33);
}

void Riscv64Assembler::Div(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x4, rd, 0x33);
}

void Riscv64Assembler::Divu(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x5, rd, 0x33);
}

void Riscv64Assembler::Rem(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x6, rd, 0x33);
}

void Riscv64Assembler::Remu(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x7, rd, 0x33);
}

// RV64M Standard Extension: opcode = 0x3b, funct3 0x0 and from 0x4 ~ 0x7

void Riscv64Assembler::Mulw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x0, rd, 0x3b);
}

void Riscv64Assembler::Divw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x4, rd, 0x3b);
}

void Riscv64Assembler::Divuw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x5, rd, 0x3b);
}

void Riscv64Assembler::Remw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x6, rd, 0x3b);
}

void Riscv64Assembler::Remuw(XRegister rd, XRegister rs1, XRegister rs2) {
  EmitR(0x1, rs2, rs1, 0x7, rd, 0x3b);
}

/////////////////////////////// RV64 "IM" Instructions  END ///////////////////////////////

/////////////////////////////// RV64 "FD" Instructions  START ///////////////////////////////

// FP load/store instructions (RV32F+RV32D): opcode = 0x07, 0x27

void Riscv64Assembler::FLw(FRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x2, rd, 0x07);
}

void Riscv64Assembler::FLd(FRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x3, rd, 0x07);
}

void Riscv64Assembler::FSw(FRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x2, 0x27);
}

void Riscv64Assembler::FSd(FRegister rs2, XRegister rs1, int32_t offset) {
  EmitS(offset, rs2, rs1, 0x3, 0x27);
}

// FP FMA instructions (RV32F+RV32D): opcode = 0x43, 0x47, 0x4b, 0x4f

void Riscv64Assembler::FMAddS(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x0, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x43);
}

void Riscv64Assembler::FMAddD(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x1, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x43);
}

void Riscv64Assembler::FMSubS(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x0, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x47);
}

void Riscv64Assembler::FMSubD(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x1, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x47);
}

void Riscv64Assembler::FNMSubS(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x0, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x4b);
}

void Riscv64Assembler::FNMSubD(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x1, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x4b);
}

void Riscv64Assembler::FNMAddS(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x0, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x4f);
}

void Riscv64Assembler::FNMAddD(
    FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm) {
  EmitR4(rs3, 0x1, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x4f);
}

// Simple FP instructions (RV32F+RV32D): opcode = 0x53, funct7 = 0b0XXXX0D

void Riscv64Assembler::FAddS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x0, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FAddD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x1, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FSubS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x4, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FSubD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x5, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FMulS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x8, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FMulD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0x9, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FDivS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0xc, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FDivD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm) {
  EmitR(0xd, rs2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FSqrtS(FRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x2c, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FSqrtD(FRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x2d, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FSgnjS(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x10, rs2, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FSgnjD(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x11, rs2, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FSgnjnS(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x10, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FSgnjnD(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x11, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FSgnjxS(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x10, rs2, rs1, 0x2, rd, 0x53);
}

void Riscv64Assembler::FSgnjxD(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x11, rs2, rs1, 0x2, rd, 0x53);
}

void Riscv64Assembler::FMinS(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x14, rs2, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FMinD(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x15, rs2, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FMaxS(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x14, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FMaxD(FRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x15, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FCvtSD(FRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x20, 0x1, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtDS(FRegister rd, FRegister rs1, FPRoundingMode frm) {
  // Note: The `frm` is useless, the result can represent every value of the source exactly.
  EmitR(0x21, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

// FP compare instructions (RV32F+RV32D): opcode = 0x53, funct7 = 0b101000D

void Riscv64Assembler::FEqS(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x50, rs2, rs1, 0x2, rd, 0x53);
}

void Riscv64Assembler::FEqD(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x51, rs2, rs1, 0x2, rd, 0x53);
}

void Riscv64Assembler::FLtS(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x50, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FLtD(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x51, rs2, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FLeS(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x50, rs2, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FLeD(XRegister rd, FRegister rs1, FRegister rs2) {
  EmitR(0x51, rs2, rs1, 0x0, rd, 0x53);
}

// FP conversion instructions (RV32F+RV32D+RV64F+RV64D): opcode = 0x53, funct7 = 0b110X00D

void Riscv64Assembler::FCvtWS(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x60, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtWD(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x61, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtWuS(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x60, 0x1, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtWuD(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x61, 0x1, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtLS(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x60, 0x2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtLD(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x61, 0x2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtLuS(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x60, 0x3, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtLuD(XRegister rd, FRegister rs1, FPRoundingMode frm) {
  EmitR(0x61, 0x3, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtSW(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x68, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtDW(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  // Note: The `frm` is useless, the result can represent every value of the source exactly.
  EmitR(0x69, 0x0, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtSWu(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x68, 0x1, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtDWu(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  // Note: The `frm` is useless, the result can represent every value of the source exactly.
  EmitR(0x69, 0x1, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtSL(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x68, 0x2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtDL(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x69, 0x2, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtSLu(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x68, 0x3, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

void Riscv64Assembler::FCvtDLu(FRegister rd, XRegister rs1, FPRoundingMode frm) {
  EmitR(0x69, 0x3, rs1, enum_cast<uint32_t>(frm), rd, 0x53);
}

// FP move instructions (RV32F+RV32D): opcode = 0x53, funct3 = 0x0, funct7 = 0b111X00D

void Riscv64Assembler::FMvXW(XRegister rd, FRegister rs1) {
  EmitR(0x70, 0x0, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FMvXD(XRegister rd, FRegister rs1) {
  EmitR(0x71, 0x0, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FMvWX(FRegister rd, XRegister rs1) {
  EmitR(0x78, 0x0, rs1, 0x0, rd, 0x53);
}

void Riscv64Assembler::FMvDX(FRegister rd, XRegister rs1) {
  EmitR(0x79, 0x0, rs1, 0x0, rd, 0x53);
}

// FP classify instructions (RV32F+RV32D): opcode = 0x53, funct3 = 0x1, funct7 = 0b111X00D

void Riscv64Assembler::FClassS(XRegister rd, FRegister rs1) {
  EmitR(0x70, 0x0, rs1, 0x1, rd, 0x53);
}

void Riscv64Assembler::FClassD(XRegister rd, FRegister rs1) {
  EmitR(0x71, 0x0, rs1, 0x1, rd, 0x53);
}

/////////////////////////////// RV64 "FD" Instructions  END ///////////////////////////////

////////////////////////////// RV64 MACRO Instructions  START ///////////////////////////////

// Pseudo instructions

void Riscv64Assembler::Nop() { Addi(Zero, Zero, 0); }

void Riscv64Assembler::Mv(XRegister rd, XRegister rs) { Addi(rd, rs, 0); }

void Riscv64Assembler::Not(XRegister rd, XRegister rs) { Xori(rd, rs, -1); }

void Riscv64Assembler::Neg(XRegister rd, XRegister rs) { Sub(rd, Zero, rs); }

void Riscv64Assembler::NegW(XRegister rd, XRegister rs) { Subw(rd, Zero, rs); }

void Riscv64Assembler::SextB(XRegister rd, XRegister rs) {
  Slli(rd, rs, kXlen - 8u);
  Srai(rd, rd, kXlen - 8u);
}

void Riscv64Assembler::SextH(XRegister rd, XRegister rs) {
  Slli(rd, rs, kXlen - 16u);
  Srai(rd, rd, kXlen - 16u);
}

void Riscv64Assembler::SextW(XRegister rd, XRegister rs) { Addiw(rd, rs, 0); }

void Riscv64Assembler::ZextB(XRegister rd, XRegister rs) { Andi(rd, rs, 0xff); }

void Riscv64Assembler::ZextH(XRegister rd, XRegister rs) {
  Slli(rd, rs, kXlen - 16u);
  Srli(rd, rd, kXlen - 16u);
}

void Riscv64Assembler::ZextW(XRegister rd, XRegister rs) {
  Slli(rd, rs, kXlen - 32u);
  Srli(rd, rd, kXlen - 32u);
}

void Riscv64Assembler::Seqz(XRegister rd, XRegister rs) { Sltiu(rd, rs, 1); }

void Riscv64Assembler::Snez(XRegister rd, XRegister rs) { Sltu(rd, Zero, rs); }

void Riscv64Assembler::Sltz(XRegister rd, XRegister rs) { Slt(rd, rs, Zero); }

void Riscv64Assembler::Sgtz(XRegister rd, XRegister rs) { Slt(rd, Zero, rs); }

void Riscv64Assembler::FMvS(FRegister rd, FRegister rs) { FSgnjS(rd, rs, rs); }

void Riscv64Assembler::FAbsS(FRegister rd, FRegister rs) { FSgnjxS(rd, rs, rs); }

void Riscv64Assembler::FNegS(FRegister rd, FRegister rs) { FSgnjnS(rd, rs, rs); }

void Riscv64Assembler::FMvD(FRegister rd, FRegister rs) { FSgnjS(rd, rs, rs); }

void Riscv64Assembler::FAbsD(FRegister rd, FRegister rs) { FSgnjxS(rd, rs, rs); }

void Riscv64Assembler::FNegD(FRegister rd, FRegister rs) { FSgnjnS(rd, rs, rs); }

/////////////////////////////// RV64 MACRO Instructions END ///////////////////////////////

/////////////////////////////// RV64 VARIANTS extension end ////////////

}  // namespace riscv64
}  // namespace art
