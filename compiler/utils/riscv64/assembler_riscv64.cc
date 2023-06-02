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

// Split 32-bit offset into an `imm20` for LUI/AUIPC and
// a signed 12-bit short offset for ADDI/JALR/etc.
ALWAYS_INLINE static inline std::pair<uint32_t, int32_t> SplitOffset(int32_t offset) {
  // The highest 0x800 values are out of range.
  DCHECK_LT(offset, 0x7ffff800);
  // Round `offset` to nearest 4KiB offset because short offset has range [-0x800, 0x800).
  int32_t near_offset = (offset + 0x800) & ~0xfff;
  // Calculate the short offset.
  int32_t short_offset = offset - near_offset;
  DCHECK(IsInt<12>(short_offset));
  // Extract the `imm20`.
  uint32_t imm20 = static_cast<uint32_t>(near_offset) >> 12;
  // Return the result as a pair.
  return std::make_pair(imm20, short_offset);
}

void Riscv64Assembler::FinalizeCode() {
  ReserveJumpTableSpace();
  EmitLiterals();
  PromoteBranches();
}

void Riscv64Assembler::FinalizeInstructions(const MemoryRegion& region) {
  EmitBranches();
  EmitJumpTables();
  Assembler::FinalizeInstructions(region);
  PatchCFI();
}

void Riscv64Assembler::Emit(uint32_t value) {
  if (overwriting_) {
    // Branches to labels are emitted into their placeholders here.
    buffer_.Store<uint32_t>(overwrite_location_, value);
    overwrite_location_ += sizeof(uint32_t);
  } else {
    // Other instructions are simply appended at the end here.
    AssemblerBuffer::EnsureCapacity ensured(&buffer_);
    buffer_.Emit<uint32_t>(value);
  }
}

/////////////////////////////// RV64 VARIANTS extension ///////////////////////////////

//////////////////////////////// RV64 "I" Instructions ////////////////////////////////

// LUI/AUIPC (RV32I, with sign-extension on RV64I), opcode = 0x17, 0x37

void Riscv64Assembler::Lui(XRegister rd, uint32_t imm20) {
  EmitU(imm20, rd, 0x37);
}

void Riscv64Assembler::Auipc(XRegister rd, uint32_t imm20) {
  EmitU(imm20, rd, 0x17);
}

// Jump instructions (RV32I), opcode = 0x67, 0x6f

void Riscv64Assembler::Jal(XRegister rd, int32_t offset) {
  EmitJ(offset, rd, 0x6F);
}

void Riscv64Assembler::Jalr(XRegister rd, XRegister rs1, int32_t offset) {
  EmitI(offset, rs1, 0x0, rd, 0x67);
}

// Branch instructions, opcode = 0x63 (subfunc from 0x0 ~ 0x7), 0x67, 0x6f

void Riscv64Assembler::Beq(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x0, 0x63);
}

void Riscv64Assembler::Bne(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x1, 0x63);
}

void Riscv64Assembler::Blt(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x4, 0x63);
}

void Riscv64Assembler::Bge(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x5, 0x63);
}

void Riscv64Assembler::Bltu(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x6, 0x63);
}

void Riscv64Assembler::Bgeu(XRegister rs1, XRegister rs2, int32_t offset) {
  EmitB(offset, rs2, rs1, 0x7, 0x63);
}

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

// Fence instruction (RV32I): opcode = 0xf, funct3 = 0
void Riscv64Assembler::Fence(uint32_t pred, uint32_t succ) {
  DCHECK(IsUint<4>(pred));
  DCHECK(IsUint<4>(succ));
  EmitI(/* normal fence */ 0x0 << 8 | pred << 4 | succ, 0x0, 0x0, 0x0, 0xf);
}

//////////////////////////////// RV64 "I" Instructions  END ////////////////////////////////

/////////////////////////// RV64 "Zifencei" Instructions  START ////////////////////////////

// "Zifencei" Standard Extension, opcode = 0xf, funct3 = 1
void Riscv64Assembler::FenceI() { EmitI(0x0, 0x0, 0x1, 0x0, 0xf); }

//////////////////////////// RV64 "Zifencei" Instructions  END /////////////////////////////

/////////////////////////////// RV64 "M" Instructions  START ///////////////////////////////

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

//////////////////////////////// RV64 "M" Instructions  END ////////////////////////////////

/////////////////////////////// RV64 "A" Instructions  START ///////////////////////////////

void Riscv64Assembler::LrW(XRegister rd, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x2, aqrl, 0x0, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::LrD(XRegister rd, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x2, aqrl, 0x0, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::ScW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x3, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::ScD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x3, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoSwapW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x1, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoSwapD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x1, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoAddW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x0, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoAddD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x0, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoXorW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x4, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoXorD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x4, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoAndW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0xc, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoAndD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0xc, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoOrW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x8, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoOrD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x8, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoMinW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x10, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoMinD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x10, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoMaxW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x14, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoMaxD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x14, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoMinuW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x18, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoMinuD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x18, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

void Riscv64Assembler::AmoMaxuW(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x1c, aqrl, rs2, rs1, 0x2, rd, 0x2f);
}

void Riscv64Assembler::AmoMaxuD(XRegister rd, XRegister rs2, XRegister rs1, uint32_t aqrl) {
  EmitR4(0x1c, aqrl, rs2, rs1, 0x3, rd, 0x2f);
}

/////////////////////////////// RV64 "A" Instructions  END ///////////////////////////////

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

void Riscv64Assembler::FMvD(FRegister rd, FRegister rs) { FSgnjD(rd, rs, rs); }

void Riscv64Assembler::FAbsD(FRegister rd, FRegister rs) { FSgnjxD(rd, rs, rs); }

void Riscv64Assembler::FNegD(FRegister rd, FRegister rs) { FSgnjnD(rd, rs, rs); }

void Riscv64Assembler::Beqz(XRegister rs, int32_t offset) {
  Beq(rs, Zero, offset);
}

void Riscv64Assembler::Bnez(XRegister rs, int32_t offset) {
  Bne(rs, Zero, offset);
}

void Riscv64Assembler::Blez(XRegister rt, int32_t offset) {
  Bge(Zero, rt, offset);
}

void Riscv64Assembler::Bgez(XRegister rt, int32_t offset) {
  Bge(rt, Zero, offset);
}

void Riscv64Assembler::Bltz(XRegister rt, int32_t offset) {
  Blt(rt, Zero, offset);
}

void Riscv64Assembler::Bgtz(XRegister rt, int32_t offset) {
  Blt(Zero, rt, offset);
}

void Riscv64Assembler::Bgt(XRegister rs, XRegister rt, int32_t offset) {
  Blt(rt, rs, offset);
}

void Riscv64Assembler::Ble(XRegister rs, XRegister rt, int32_t offset) {
  Bge(rt, rs, offset);
}

void Riscv64Assembler::Bgtu(XRegister rs, XRegister rt, int32_t offset) {
  Bltu(rt, rs, offset);
}

void Riscv64Assembler::Bleu(XRegister rs, XRegister rt, int32_t offset) {
  Bgeu(rt, rs, offset);
}

void Riscv64Assembler::J(int32_t offset) { Jal(Zero, offset); }

void Riscv64Assembler::Jal(int32_t offset) { Jal(RA, offset); }

void Riscv64Assembler::Jr(XRegister rs) { Jalr(Zero, rs, 0); }

void Riscv64Assembler::Jalr(XRegister rs) { Jalr(RA, rs, 0); }

void Riscv64Assembler::Jalr(XRegister rd, XRegister rs) { Jalr(rd, rs, 0); }

void Riscv64Assembler::Ret() { Jalr(Zero, RA, 0); }

void Riscv64Assembler::Beqz(XRegister rs, Riscv64Label* label, bool is_bare) {
  Beq(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Bnez(XRegister rs, Riscv64Label* label, bool is_bare) {
  Bne(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Blez(XRegister rs, Riscv64Label* label, bool is_bare) {
  Ble(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Bgez(XRegister rs, Riscv64Label* label, bool is_bare) {
  Bge(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Bltz(XRegister rs, Riscv64Label* label, bool is_bare) {
  Blt(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Bgtz(XRegister rs, Riscv64Label* label, bool is_bare) {
  Bgt(rs, Zero, label, is_bare);
}

void Riscv64Assembler::Beq(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondEQ, rs, rt);
}

void Riscv64Assembler::Bne(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondNE, rs, rt);
}

void Riscv64Assembler::Ble(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLE, rs, rt);
}

void Riscv64Assembler::Bge(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGE, rs, rt);
}

void Riscv64Assembler::Blt(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLT, rs, rt);
}

void Riscv64Assembler::Bgt(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGT, rs, rt);
}

void Riscv64Assembler::Bleu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLEU, rs, rt);
}

void Riscv64Assembler::Bgeu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGEU, rs, rt);
}

void Riscv64Assembler::Bltu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLTU, rs, rt);
}

void Riscv64Assembler::Bgtu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGTU, rs, rt);
}

void Riscv64Assembler::Jal(XRegister rd, Riscv64Label* label, bool is_bare) {
  Buncond(label, rd, is_bare);
}

void Riscv64Assembler::J(Riscv64Label* label, bool is_bare) {
  Jal(Zero, label, is_bare);
}

void Riscv64Assembler::Jal(Riscv64Label* label, bool is_bare) {
  Jal(RA, label, is_bare);
}

void Riscv64Assembler::Lw(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 4u);
  LoadLiteral(literal, rd, Branch::kLiteral);
}

void Riscv64Assembler::Lwu(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 4u);
  LoadLiteral(literal, rd, Branch::kLiteralUnsigned);
}

void Riscv64Assembler::Ld(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 8u);
  LoadLiteral(literal, rd, Branch::kLiteralLong);
}

/////////////////////////////// RV64 MACRO Instructions END ///////////////////////////////

const Riscv64Assembler::Branch::BranchInfo Riscv64Assembler::Branch::branch_info_[] = {
    // Short branches (can be promoted to longer).
    {4, 0, Riscv64Assembler::Branch::kOffset13},  // kCondBranch
    {4, 0, Riscv64Assembler::Branch::kOffset21},  // kUncondBranch
    {4, 0, Riscv64Assembler::Branch::kOffset21},  // kCall
    // Short branches (can't be promoted to longer).
    {4, 0, Riscv64Assembler::Branch::kOffset13},  // kBareCondBranch
    {4, 0, Riscv64Assembler::Branch::kOffset21},  // kBareUncondBranch
    {4, 0, Riscv64Assembler::Branch::kOffset21},  // kBareCall

    // Medium branch.
    {8, 4, Riscv64Assembler::Branch::kOffset21},  // kCondBranch21

    // Long branches.
    {12, 4, Riscv64Assembler::Branch::kOffset32},  // kLongCondBranch
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLongUncondBranch
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLongCall

    // label.
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLabel

    // literals.
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLiteral
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLiteralUnsigned
    {8, 0, Riscv64Assembler::Branch::kOffset32},  // kLiteralLong
};

void Riscv64Assembler::Branch::InitShortOrLong(Riscv64Assembler::Branch::OffsetBits offset_size,
                                               Riscv64Assembler::Branch::Type short_type,
                                               Riscv64Assembler::Branch::Type long_type,
                                               Riscv64Assembler::Branch::Type longest_type) {
  Riscv64Assembler::Branch::Type type = short_type;
  if (offset_size > branch_info_[type].offset_size) {
    type = long_type;
    if (offset_size > branch_info_[type].offset_size) {
      type = longest_type;
    }
  }
  type_ = type;
}

void Riscv64Assembler::Branch::InitializeType(Type initial_type) {
  OffsetBits offset_size_needed = GetOffsetSizeNeeded(location_, target_);

  switch (initial_type) {
    case kCondBranch:
      if (condition_ != kUncond) {
        InitShortOrLong(offset_size_needed, kCondBranch, kCondBranch21, kLongCondBranch);
        break;
      }
      FALLTHROUGH_INTENDED;
    case kUncondBranch:
      InitShortOrLong(offset_size_needed, kUncondBranch, kLongUncondBranch, kLongUncondBranch);
      break;
    case kCall:
      InitShortOrLong(offset_size_needed, kCall, kLongCall, kLongCall);
      break;
    case kBareCondBranch:
      if (condition_ != kUncond) {
        type_ = kBareCondBranch;
        CHECK_LE(offset_size_needed, GetOffsetSize());
        break;
      }
      FALLTHROUGH_INTENDED;
    case kBareUncondBranch:
      type_ = kBareUncondBranch;
      CHECK_LE(offset_size_needed, GetOffsetSize());
      break;
    case kBareCall:
      type_ = kBareCall;
      CHECK_LE(offset_size_needed, GetOffsetSize());
      break;
    case kLabel:
      type_ = initial_type;
      break;
    case kLiteral:
    case kLiteralUnsigned:
    case kLiteralLong:
      CHECK(!IsResolved());
      type_ = initial_type;
      break;
    default:
      LOG(FATAL) << "Unexpected branch type " << enum_cast<uint32_t>(initial_type);
      UNREACHABLE();
  }

  old_type_ = type_;
}

bool Riscv64Assembler::Branch::IsNop(BranchCondition condition, XRegister lhs, XRegister rhs) {
  switch (condition) {
    case kCondNE:
    case kCondLT:
    case kCondGT:
    case kCondLTU:
    case kCondGTU:
      return lhs == rhs;
    default:
      return false;
  }
}

bool Riscv64Assembler::Branch::IsUncond(BranchCondition condition, XRegister lhs, XRegister rhs) {
  switch (condition) {
    case kUncond:
      return true;
    case kCondEQ:
    case kCondGE:
    case kCondLE:
    case kCondLEU:
    case kCondGEU:
      return lhs == rhs;
    default:
      return false;
  }
}

Riscv64Assembler::Branch::Branch(uint32_t location, uint32_t target, XRegister rd, bool is_bare)
    : old_location_(location),
      location_(location),
      target_(target),
      lhs_reg_(rd),
      rhs_reg_(Zero),
      condition_(kUncond) {
  InitializeType(
      (rd != Zero ? (is_bare ? kBareCall : kCall) : (is_bare ? kBareUncondBranch : kUncondBranch)));
}

Riscv64Assembler::Branch::Branch(uint32_t location,
                                 uint32_t target,
                                 Riscv64Assembler::BranchCondition condition,
                                 XRegister lhs_reg,
                                 XRegister rhs_reg,
                                 bool is_bare)
    : old_location_(location),
      location_(location),
      target_(target),
      lhs_reg_(lhs_reg),
      rhs_reg_(rhs_reg),
      condition_(condition) {
  DCHECK_NE(condition, kUncond);
  DCHECK(!IsNop(condition, lhs_reg, rhs_reg));
  DCHECK(!IsUncond(condition, lhs_reg, rhs_reg));
  InitializeType(is_bare ? kBareCondBranch : kCondBranch);
}

Riscv64Assembler::Branch::Branch(uint32_t location,
                                 uint32_t target,
                                 XRegister rd,
                                 Type label_or_literal_type)
    : old_location_(location),
      location_(location),
      target_(target),
      lhs_reg_(rd),
      rhs_reg_(Zero),
      condition_(kUncond) {
  CHECK_NE(rd , Zero);
  InitializeType(label_or_literal_type);
}

Riscv64Assembler::BranchCondition Riscv64Assembler::Branch::OppositeCondition(
    Riscv64Assembler::BranchCondition cond) {
  switch (cond) {
    case kCondEQ:
      return kCondNE;
    case kCondNE:
      return kCondEQ;
    case kCondLT:
      return kCondGE;
    case kCondGE:
      return kCondLT;
    case kCondLE:
      return kCondGT;
    case kCondGT:
      return kCondLE;
    case kCondLTU:
      return kCondGEU;
    case kCondGEU:
      return kCondLTU;
    case kCondLEU:
      return kCondGTU;
    case kCondGTU:
      return kCondLEU;
    case kUncond:
      LOG(FATAL) << "Unexpected branch condition " << enum_cast<uint32_t>(cond);
      UNREACHABLE();
  }
}

Riscv64Assembler::Branch::Type Riscv64Assembler::Branch::GetType() const { return type_; }

Riscv64Assembler::BranchCondition Riscv64Assembler::Branch::GetCondition() const {
    return condition_;
}

XRegister Riscv64Assembler::Branch::GetLeftRegister() const { return lhs_reg_; }

XRegister Riscv64Assembler::Branch::GetRightRegister() const { return rhs_reg_; }

uint32_t Riscv64Assembler::Branch::GetTarget() const { return target_; }

uint32_t Riscv64Assembler::Branch::GetLocation() const { return location_; }

uint32_t Riscv64Assembler::Branch::GetOldLocation() const { return old_location_; }

uint32_t Riscv64Assembler::Branch::GetLength() const { return branch_info_[type_].length; }

uint32_t Riscv64Assembler::Branch::GetOldLength() const { return branch_info_[old_type_].length; }

uint32_t Riscv64Assembler::Branch::GetEndLocation() const { return GetLocation() + GetLength(); }

uint32_t Riscv64Assembler::Branch::GetOldEndLocation() const {
  return GetOldLocation() + GetOldLength();
}

bool Riscv64Assembler::Branch::IsBare() const {
  switch (type_) {
    case kBareUncondBranch:
    case kBareCondBranch:
    case kBareCall:
      return true;
    default:
      return false;
  }
}

bool Riscv64Assembler::Branch::IsResolved() const { return target_ != kUnresolved; }

Riscv64Assembler::Branch::OffsetBits Riscv64Assembler::Branch::GetOffsetSize() const {
  return branch_info_[type_].offset_size;
}

Riscv64Assembler::Branch::OffsetBits Riscv64Assembler::Branch::GetOffsetSizeNeeded(
    uint32_t location, uint32_t target) {
  // For unresolved targets assume the shortest encoding
  // (later it will be made longer if needed).
  if (target == kUnresolved) {
    return kOffset13;
  }
  int64_t distance = static_cast<int64_t>(target) - location;
  if (IsInt<kOffset13>(distance)) {
    return kOffset13;
  } else if (IsInt<kOffset21>(distance)) {
    return kOffset21;
  } else {
    return kOffset32;
  }
}

void Riscv64Assembler::Branch::Resolve(uint32_t target) { target_ = target; }

void Riscv64Assembler::Branch::Relocate(uint32_t expand_location, uint32_t delta) {
  // All targets should be resolved before we start promoting branches.
  DCHECK(IsResolved());
  if (location_ > expand_location) {
    location_ += delta;
  }
  if (target_ > expand_location) {
    target_ += delta;
  }
}

uint32_t Riscv64Assembler::Branch::PromoteIfNeeded() {
  // All targets should be resolved before we start promoting branches.
  DCHECK(IsResolved());
  Type old_type = type_;
  switch (type_) {
    // Short branches (can be promoted to longer).
    case kCondBranch: {
      OffsetBits needed_size = GetOffsetSizeNeeded(GetOffsetLocation(), target_);
      if (needed_size <= GetOffsetSize()) {
        return 0u;
      }
      // The offset remains the same for `kCondBranch21` for forward branches.
      DCHECK_EQ(branch_info_[kCondBranch21].length - branch_info_[kCondBranch21].pc_offset,
                branch_info_[kCondBranch].length - branch_info_[kCondBranch].pc_offset);
      if (target_ <= location_) {
        // Calculate the needed size for kCondBranch21.
        needed_size =
            GetOffsetSizeNeeded(location_ + branch_info_[kCondBranch21].pc_offset, target_);
      }
      type_ = (needed_size <= branch_info_[kCondBranch21].offset_size)
          ? kCondBranch21
          : kLongCondBranch;
      break;
    }
    case kUncondBranch:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongUncondBranch;
      break;
    case kCall:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongCall;
      break;
    // Medium branch (can be promoted to long).
    case kCondBranch21:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongCondBranch;
      break;
    default:
      // Other branch types cannot be promoted.
      DCHECK_LE(GetOffsetSizeNeeded(GetOffsetLocation(), target_), GetOffsetSize()) << type_;
      return 0u;
  }
  DCHECK(type_ != old_type);
  DCHECK_GT(branch_info_[type_].length, branch_info_[old_type].length);
  return branch_info_[type_].length - branch_info_[old_type].length;
}

uint32_t Riscv64Assembler::Branch::GetOffsetLocation() const {
  return location_ + branch_info_[type_].pc_offset;
}

int32_t Riscv64Assembler::Branch::GetOffset() const {
  CHECK(IsResolved());
  // Calculate the byte distance between instructions and also account for
  // different PC-relative origins.
  uint32_t offset_location = GetOffsetLocation();
  int32_t offset = static_cast<int32_t>(target_ - offset_location);
  DCHECK_EQ(offset, static_cast<int64_t>(target_) - static_cast<int64_t>(offset_location));
  return offset;
}

void Riscv64Assembler::EmitBcond(BranchCondition cond,
                                 XRegister rs,
                                 XRegister rt,
                                 int32_t offset) {
  switch (cond) {
#define DEFINE_CASE(COND, cond) \
    case kCond##COND:           \
      B##cond(rs, rt, offset);  \
      break;
    DEFINE_CASE(EQ, eq)
    DEFINE_CASE(NE, ne)
    DEFINE_CASE(LT, lt)
    DEFINE_CASE(GE, ge)
    DEFINE_CASE(LE, le)
    DEFINE_CASE(GT, gt)
    DEFINE_CASE(LTU, ltu)
    DEFINE_CASE(GEU, geu)
    DEFINE_CASE(LEU, leu)
    DEFINE_CASE(GTU, gtu)
#undef DEFINE_CASE
    case kUncond:
      LOG(FATAL) << "Unexpected branch condition " << enum_cast<uint32_t>(cond);
      UNREACHABLE();
  }
}

void Riscv64Assembler::EmitBranch(Riscv64Assembler::Branch* branch) {
  CHECK(overwriting_);
  overwrite_location_ = branch->GetLocation();
  const int32_t offset = branch->GetOffset();
  BranchCondition condition = branch->GetCondition();
  XRegister lhs = branch->GetLeftRegister();
  XRegister rhs = branch->GetRightRegister();

  auto emit_auipc_and_next = [&](XRegister reg, auto next) {
    CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
    auto [imm20, short_offset] = SplitOffset(offset);
    Auipc(reg, imm20);
    next(short_offset);
  };

  switch (branch->GetType()) {
    // Short branches.
    case Branch::kUncondBranch:
    case Branch::kBareUncondBranch:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      J(offset);
      break;
    case Branch::kCondBranch:
    case Branch::kBareCondBranch:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      EmitBcond(condition, lhs, rhs, offset);
      break;
    case Branch::kCall:
    case Branch::kBareCall:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      DCHECK(lhs != Zero);
      Jal(lhs, offset);
      break;

    // Medium branch.
    case Branch::kCondBranch21:
      EmitBcond(Branch::OppositeCondition(condition), lhs, rhs, branch->GetLength());
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      J(offset);
      break;

    // Long branches.
    case Branch::kLongCondBranch:
      EmitBcond(Branch::OppositeCondition(condition), lhs, rhs, branch->GetLength());
      FALLTHROUGH_INTENDED;
    case Branch::kLongUncondBranch:
      emit_auipc_and_next(TMP, [&](int32_t short_offset) { Jalr(Zero, TMP, short_offset); });
      break;
    case Branch::kLongCall:
      DCHECK(lhs != Zero);
      emit_auipc_and_next(lhs, [&](int32_t short_offset) { Jalr(lhs, lhs, short_offset); });
      break;

    // label.
    case Branch::kLabel:
      emit_auipc_and_next(lhs, [&](int32_t short_offset) { Addi(lhs, lhs, short_offset); });
      break;
    // literals.
    case Branch::kLiteral:
      emit_auipc_and_next(lhs, [&](int32_t short_offset) { Lw(lhs, lhs, short_offset); });
      break;
    case Branch::kLiteralUnsigned:
      emit_auipc_and_next(lhs, [&](int32_t short_offset) { Lwu(lhs, lhs, short_offset); });
      break;
    case Branch::kLiteralLong:
      emit_auipc_and_next(lhs, [&](int32_t short_offset) { Ld(lhs, lhs, short_offset); });
      break;
  }
  CHECK_EQ(overwrite_location_, branch->GetEndLocation());
  CHECK_LE(branch->GetLength(), static_cast<uint32_t>(Branch::kMaxBranchLength));
}

void Riscv64Assembler::EmitBranches() {
  CHECK(!overwriting_);
  // Switch from appending instructions at the end of the buffer to overwriting
  // existing instructions (branch placeholders) in the buffer.
  overwriting_ = true;
  for (auto& branch : branches_) {
    EmitBranch(&branch);
  }
  overwriting_ = false;
}

void Riscv64Assembler::FinalizeLabeledBranch(Riscv64Label* label) {
  // TODO(riscv64): Support "C" Standard Extension - length may not be a multiple of 4.
  DCHECK_ALIGNED(branches_.back().GetLength(), sizeof(uint32_t));
  uint32_t length = branches_.back().GetLength() / sizeof(uint32_t);
  if (!label->IsBound()) {
    // Branch forward (to a following label), distance is unknown.
    // The first branch forward will contain 0, serving as the terminator of
    // the list of forward-reaching branches.
    Emit(label->position_);
    length--;
    // Now make the label object point to this branch
    // (this forms a linked list of branches preceding this label).
    uint32_t branch_id = branches_.size() - 1;
    label->LinkTo(branch_id);
  }
  // Reserve space for the branch.
  for (; length != 0u; --length) {
    Nop();
  }
}

void Riscv64Assembler::Bcond(
    Riscv64Label* label, bool is_bare, BranchCondition condition, XRegister lhs, XRegister rhs) {
  // TODO(riscv64): Should an assembler perform these optimizations, or should we remove them?
  // If lhs = rhs, this can be a NOP.
  if (Branch::IsNop(condition, lhs, rhs)) {
    return;
  }
  if (Branch::IsUncond(condition, lhs, rhs)) {
    Buncond(label, Zero, is_bare);
    return;
  }

  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, condition, lhs, rhs, is_bare);
  FinalizeLabeledBranch(label);
}

void Riscv64Assembler::Buncond(Riscv64Label* label, XRegister rd, bool is_bare) {
  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, rd, is_bare);
  FinalizeLabeledBranch(label);
}

void Riscv64Assembler::LoadLiteral(Literal* literal, XRegister rd, Branch::Type literal_type) {
  Riscv64Label* label = literal->GetLabel();
  DCHECK(!label->IsBound());
  branches_.emplace_back(buffer_.Size(), Branch::kUnresolved, rd, literal_type);
  FinalizeLabeledBranch(label);
}

Riscv64Assembler::Branch* Riscv64Assembler::GetBranch(uint32_t branch_id) {
  CHECK_LT(branch_id, branches_.size());
  return &branches_[branch_id];
}

const Riscv64Assembler::Branch* Riscv64Assembler::GetBranch(uint32_t branch_id) const {
  CHECK_LT(branch_id, branches_.size());
  return &branches_[branch_id];
}

void Riscv64Assembler::Bind(Riscv64Label* label) {
  CHECK(!label->IsBound());
  uint32_t bound_pc = buffer_.Size();

  // Walk the list of branches referring to and preceding this label.
  // Store the previously unknown target addresses in them.
  while (label->IsLinked()) {
    uint32_t branch_id = label->Position();
    Branch* branch = GetBranch(branch_id);
    branch->Resolve(bound_pc);

    uint32_t branch_location = branch->GetLocation();
    // Extract the location of the previous branch in the list (walking the list backwards;
    // the previous branch ID was stored in the space reserved for this branch).
    uint32_t prev = buffer_.Load<uint32_t>(branch_location);

    // On to the previous branch in the list...
    label->position_ = prev;
  }

  // Now make the label object contain its own location (relative to the end of the preceding
  // branch, if any; it will be used by the branches referring to and following this label).
  uint32_t prev_branch_id = Riscv64Label::kNoPrevBranchId;
  if (!branches_.empty()) {
    prev_branch_id = branches_.size() - 1u;
    const Branch* prev_branch = GetBranch(prev_branch_id);
    bound_pc -= prev_branch->GetEndLocation();
  }
  label->prev_branch_id_ = prev_branch_id;
  label->BindTo(bound_pc);
}

void Riscv64Assembler::LoadLabelAddress(XRegister rd, Riscv64Label* label) {
  DCHECK_NE(rd, Zero);
  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, rd, Branch::kLabel);
  FinalizeLabeledBranch(label);
}

Literal* Riscv64Assembler::NewLiteral(size_t size, const uint8_t* data) {
  // We don't support byte and half-word literals.
  if (size == 4u) {
    literals_.emplace_back(size, data);
    return &literals_.back();
  } else {
    DCHECK_EQ(size, 8u);
    long_literals_.emplace_back(size, data);
    return &long_literals_.back();
  }
}

JumpTable* Riscv64Assembler::CreateJumpTable(ArenaVector<Riscv64Label*>&& labels) {
  jump_tables_.emplace_back(std::move(labels));
  JumpTable* table = &jump_tables_.back();
  DCHECK(!table->GetLabel()->IsBound());
  return table;
}

uint32_t Riscv64Assembler::GetLabelLocation(const Riscv64Label* label) const {
  CHECK(label->IsBound());
  uint32_t target = label->Position();
  if (label->prev_branch_id_ != Riscv64Label::kNoPrevBranchId) {
    // Get label location based on the branch preceding it.
    const Branch* prev_branch = GetBranch(label->prev_branch_id_);
    target += prev_branch->GetEndLocation();
  }
  return target;
}

uint32_t Riscv64Assembler::GetAdjustedPosition(uint32_t old_position) {
  // We can reconstruct the adjustment by going through all the branches from the beginning
  // up to the `old_position`. Since we expect `GetAdjustedPosition()` to be called in a loop
  // with increasing `old_position`, we can use the data from last `GetAdjustedPosition()` to
  // continue where we left off and the whole loop should be O(m+n) where m is the number
  // of positions to adjust and n is the number of branches.
  if (old_position < last_old_position_) {
    last_position_adjustment_ = 0;
    last_old_position_ = 0;
    last_branch_id_ = 0;
  }
  while (last_branch_id_ != branches_.size()) {
    const Branch* branch = GetBranch(last_branch_id_);
    if (branch->GetLocation() >= old_position + last_position_adjustment_) {
      break;
    }
    last_position_adjustment_ += branch->GetLength() - branch->GetOldLength();
    ++last_branch_id_;
  }
  last_old_position_ = old_position;
  return old_position + last_position_adjustment_;
}

void Riscv64Assembler::ReserveJumpTableSpace() {
  if (!jump_tables_.empty()) {
    for (JumpTable& table : jump_tables_) {
      Riscv64Label* label = table.GetLabel();
      Bind(label);

      // Bulk ensure capacity, as this may be large.
      size_t orig_size = buffer_.Size();
      size_t required_capacity = orig_size + table.GetSize();
      if (required_capacity > buffer_.Capacity()) {
        buffer_.ExtendCapacity(required_capacity);
      }
#ifndef NDEBUG
      buffer_.has_ensured_capacity_ = true;
#endif

      // Fill the space with placeholder data as the data is not final
      // until the branches have been promoted. And we shouldn't
      // be moving uninitialized data during branch promotion.
      for (size_t cnt = table.GetData().size(), i = 0; i < cnt; ++i) {
        buffer_.Emit<uint32_t>(0x1abe1234u);
      }

#ifndef NDEBUG
      buffer_.has_ensured_capacity_ = false;
#endif
    }
  }
}

void Riscv64Assembler::PromoteBranches() {
  // Promote short branches to long as necessary.
  bool changed;
  do {
    changed = false;
    for (auto& branch : branches_) {
      CHECK(branch.IsResolved());
      uint32_t delta = branch.PromoteIfNeeded();
      // If this branch has been promoted and needs to expand in size,
      // relocate all branches by the expansion size.
      if (delta != 0u) {
        changed = true;
        uint32_t expand_location = branch.GetLocation();
        for (auto& branch2 : branches_) {
          branch2.Relocate(expand_location, delta);
        }
      }
    }
  } while (changed);

  // Account for branch expansion by resizing the code buffer
  // and moving the code in it to its final location.
  size_t branch_count = branches_.size();
  if (branch_count > 0) {
    // Resize.
    Branch& last_branch = branches_[branch_count - 1];
    uint32_t size_delta = last_branch.GetEndLocation() - last_branch.GetOldEndLocation();
    uint32_t old_size = buffer_.Size();
    buffer_.Resize(old_size + size_delta);
    // Move the code residing between branch placeholders.
    uint32_t end = old_size;
    for (size_t i = branch_count; i > 0;) {
      Branch& branch = branches_[--i];
      uint32_t size = end - branch.GetOldEndLocation();
      buffer_.Move(branch.GetEndLocation(), branch.GetOldEndLocation(), size);
      end = branch.GetOldLocation();
    }
  }

  // Align 64-bit literals by moving them up by 4 bytes if needed.
  // This can increase the PC-relative distance but all literals are accessed with AUIPC+Load(imm12)
  // without branch promotion, so this late adjustment cannot take them out of instruction range.
  if (!long_literals_.empty()) {
    uint32_t first_literal_location = GetLabelLocation(long_literals_.front().GetLabel());
    size_t lit_size = long_literals_.size() * sizeof(uint64_t);
    size_t buf_size = buffer_.Size();
    // 64-bit literals must be at the very end of the buffer.
    CHECK_EQ(first_literal_location + lit_size, buf_size);
    if (!IsAligned<sizeof(uint64_t)>(first_literal_location)) {
      // Insert the padding.
      buffer_.Resize(buf_size + sizeof(uint32_t));
      buffer_.Move(first_literal_location + sizeof(uint32_t), first_literal_location, lit_size);
      DCHECK(!overwriting_);
      overwriting_ = true;
      overwrite_location_ = first_literal_location;
      Emit(0);  // Illegal instruction.
      overwriting_ = false;
      // Increase target addresses in literal and address loads by 4 bytes in order for correct
      // offsets from PC to be generated.
      for (auto& branch : branches_) {
        uint32_t target = branch.GetTarget();
        if (target >= first_literal_location) {
          branch.Resolve(target + sizeof(uint32_t));
        }
      }
      // If after this we ever call GetLabelLocation() to get the location of a 64-bit literal,
      // we need to adjust the location of the literal's label as well.
      for (Literal& literal : long_literals_) {
        // Bound label's position is negative, hence decrementing it instead of incrementing.
        literal.GetLabel()->position_ -= sizeof(uint32_t);
      }
    }
  }
}

void Riscv64Assembler::PatchCFI() {
  if (cfi().NumberOfDelayedAdvancePCs() == 0u) {
    return;
  }

  using DelayedAdvancePC = DebugFrameOpCodeWriterForAssembler::DelayedAdvancePC;
  const auto data = cfi().ReleaseStreamAndPrepareForDelayedAdvancePC();
  const std::vector<uint8_t>& old_stream = data.first;
  const std::vector<DelayedAdvancePC>& advances = data.second;

  // Refill our data buffer with patched opcodes.
  static constexpr size_t kExtraSpace = 16;  // Not every PC advance can be encoded in one byte.
  cfi().ReserveCFIStream(old_stream.size() + advances.size() + kExtraSpace);
  size_t stream_pos = 0;
  for (const DelayedAdvancePC& advance : advances) {
    DCHECK_GE(advance.stream_pos, stream_pos);
    // Copy old data up to the point where advance was issued.
    cfi().AppendRawData(old_stream, stream_pos, advance.stream_pos);
    stream_pos = advance.stream_pos;
    // Insert the advance command with its final offset.
    size_t final_pc = GetAdjustedPosition(advance.pc);
    cfi().AdvancePC(final_pc);
  }
  // Copy the final segment if any.
  cfi().AppendRawData(old_stream, stream_pos, old_stream.size());
}

void Riscv64Assembler::EmitJumpTables() {
  if (!jump_tables_.empty()) {
    CHECK(!overwriting_);
    // Switch from appending instructions at the end of the buffer to overwriting
    // existing instructions (here, jump tables) in the buffer.
    overwriting_ = true;

    for (JumpTable& table : jump_tables_) {
      Riscv64Label* table_label = table.GetLabel();
      uint32_t start = GetLabelLocation(table_label);
      overwrite_location_ = start;

      for (Riscv64Label* target : table.GetData()) {
        CHECK_EQ(buffer_.Load<uint32_t>(overwrite_location_), 0x1abe1234u);
        // The table will contain target addresses relative to the table start.
        uint32_t offset = GetLabelLocation(target) - start;
        Emit(offset);
      }
    }

    overwriting_ = false;
  }
}

void Riscv64Assembler::EmitLiterals() {
  if (!literals_.empty()) {
    for (Literal& literal : literals_) {
      Riscv64Label* label = literal.GetLabel();
      Bind(label);
      AssemblerBuffer::EnsureCapacity ensured(&buffer_);
      DCHECK_EQ(literal.GetSize(), 4u);
      for (size_t i = 0, size = literal.GetSize(); i != size; ++i) {
        buffer_.Emit<uint8_t>(literal.GetData()[i]);
      }
    }
  }
  if (!long_literals_.empty()) {
    // These need to be 8-byte-aligned but we shall add the alignment padding after the branch
    // promotion, if needed. Since all literals are accessed with AUIPC+Load(imm12) without branch
    // promotion, this late adjustment cannot take long literals out of instruction range.
    for (Literal& literal : long_literals_) {
      Riscv64Label* label = literal.GetLabel();
      Bind(label);
      AssemblerBuffer::EnsureCapacity ensured(&buffer_);
      DCHECK_EQ(literal.GetSize(), 8u);
      for (size_t i = 0, size = literal.GetSize(); i != size; ++i) {
        buffer_.Emit<uint8_t>(literal.GetData()[i]);
      }
    }
  }
}

/////////////////////////////// RV64 VARIANTS extension end ////////////

}  // namespace riscv64
}  // namespace art
