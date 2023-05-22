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

#ifndef ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_
#define ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "arch/riscv64/instruction_set_features_riscv64.h"
#include "base/arena_containers.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/macros.h"
#include "managed_register_riscv64.h"
#include "utils/assembler.h"
#include "utils/label.h"

namespace art {
namespace riscv64 {

enum class FPRoundingMode : uint32_t {
  kRNE = 0x0,  // Round to Nearest, ties to Even
  kRTZ = 0x1,  // Round towards Zero
  kRDN = 0x2,  // Round Down (towards −Infinity)
  kRUP = 0x3,  // Round Up (towards +Infinity)
  kRMM = 0x4,  // Round to Nearest, ties to Max Magnitude
  kDYN = 0x7,  // Dynamic rounding mode
  kDefault = kDYN
};

static constexpr size_t kRiscv64HalfwordSize = 2;
static constexpr size_t kRiscv64WordSize = 4;
static constexpr size_t kRiscv64DoublewordSize = 8;

class Riscv64Label : public Label {
};

class Riscv64Assembler final : public Assembler {
 public:
  explicit Riscv64Assembler(ArenaAllocator* allocator,
                            const Riscv64InstructionSetFeatures* instruction_set_features = nullptr)
      : Assembler(allocator) {
    UNUSED(instruction_set_features);
  }

  virtual ~Riscv64Assembler() {
  }

  size_t CodeSize() const override { return Assembler::CodeSize(); }
  DebugFrameOpCodeWriterForAssembler& cfi() { return Assembler::cfi(); }

  // According to "The RISC-V Instruction Set Manual"

  // LUI/AUIPC (RV32I, with sign-extension on RV64I), opcode = 0x17, 0x37
  // Note: These take a 20-bit unsigned value to align with the clang assembler for testing,
  // but the value stored in the register shall actually be sign-extended to 64 bits.
  void Lui(XRegister rd, uint32_t imm20);
  void Auipc(XRegister rd, uint32_t imm20);

  // Jump instructions (RV32I), opcode = 0x67, 0x6f
  void Jal(XRegister rd, int32_t offset);
  void Jalr(XRegister rd, XRegister rs1, int32_t offset);

  // Branch instructions (RV32I), opcode = 0x63, funct3 from 0x0 ~ 0x1 and 0x4 ~ 0x7
  void Beq(XRegister rs1, XRegister rs2, int32_t offset);
  void Bne(XRegister rs1, XRegister rs2, int32_t offset);
  void Blt(XRegister rs1, XRegister rs2, int32_t offset);
  void Bge(XRegister rs1, XRegister rs2, int32_t offset);
  void Bltu(XRegister rs1, XRegister rs2, int32_t offset);
  void Bgeu(XRegister rs1, XRegister rs2, int32_t offset);

  // Load instructions (RV32I+RV64I): opcode = 0x03, funct3 from 0x0 ~ 0x6
  void Lb(XRegister rd, XRegister rs1, int32_t offset);
  void Lh(XRegister rd, XRegister rs1, int32_t offset);
  void Lw(XRegister rd, XRegister rs1, int32_t offset);
  void Ld(XRegister rd, XRegister rs1, int32_t offset);
  void Lbu(XRegister rd, XRegister rs1, int32_t offset);
  void Lhu(XRegister rd, XRegister rs1, int32_t offset);
  void Lwu(XRegister rd, XRegister rs1, int32_t offset);

  // Store instructions (RV32I+RV64I): opcode = 0x23, funct3 from 0x0 ~ 0x3
  void Sb(XRegister rs2, XRegister rs1, int32_t offset);
  void Sh(XRegister rs2, XRegister rs1, int32_t offset);
  void Sw(XRegister rs2, XRegister rs1, int32_t offset);
  void Sd(XRegister rs2, XRegister rs1, int32_t offset);

  // IMM ALU instructions (RV32I): opcode = 0x13, funct3 from 0x0 ~ 0x7
  void Addi(XRegister rd, XRegister rs1, int32_t imm12);
  void Slti(XRegister rd, XRegister rs1, int32_t imm12);
  void Sltiu(XRegister rd, XRegister rs1, int32_t imm12);
  void Xori(XRegister rd, XRegister rs1, int32_t imm12);
  void Ori(XRegister rd, XRegister rs1, int32_t imm12);
  void Andi(XRegister rd, XRegister rs1, int32_t imm12);
  void Slli(XRegister rd, XRegister rs1, int32_t shamt);
  void Srli(XRegister rd, XRegister rs1, int32_t shamt);
  void Srai(XRegister rd, XRegister rs1, int32_t shamt);

  // ALU instructions (RV32I): opcode = 0x33, funct3 from 0x0 ~ 0x7
  void Add(XRegister rd, XRegister rs1, XRegister rs2);
  void Sub(XRegister rd, XRegister rs1, XRegister rs2);
  void Slt(XRegister rd, XRegister rs1, XRegister rs2);
  void Sltu(XRegister rd, XRegister rs1, XRegister rs2);
  void Xor(XRegister rd, XRegister rs1, XRegister rs2);
  void Or(XRegister rd, XRegister rs1, XRegister rs2);
  void And(XRegister rd, XRegister rs1, XRegister rs2);
  void Sll(XRegister rd, XRegister rs1, XRegister rs2);
  void Srl(XRegister rd, XRegister rs1, XRegister rs2);
  void Sra(XRegister rd, XRegister rs1, XRegister rs2);

  // 32bit Imm ALU instructions (RV64I): opcode = 0x1b, funct3 from 0x0, 0x1, 0x5
  void Addiw(XRegister rd, XRegister rs1, int32_t imm12);
  void Slliw(XRegister rd, XRegister rs1, int32_t shamt);
  void Srliw(XRegister rd, XRegister rs1, int32_t shamt);
  void Sraiw(XRegister rd, XRegister rs1, int32_t shamt);

  // 32bit ALU instructions (RV64I): opcode = 0x3b, funct3 from 0x0 ~ 0x7
  void Addw(XRegister rd, XRegister rs1, XRegister rs2);
  void Subw(XRegister rd, XRegister rs1, XRegister rs2);
  void Sllw(XRegister rd, XRegister rs1, XRegister rs2);
  void Srlw(XRegister rd, XRegister rs1, XRegister rs2);
  void Sraw(XRegister rd, XRegister rs1, XRegister rs2);

  // RV32M Standard Extension: opcode = 0x33, funct3 from 0x0 ~ 0x7
  void Mul(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulh(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulhsu(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulhu(XRegister rd, XRegister rs1, XRegister rs2);
  void Div(XRegister rd, XRegister rs1, XRegister rs2);
  void Divu(XRegister rd, XRegister rs1, XRegister rs2);
  void Rem(XRegister rd, XRegister rs1, XRegister rs2);
  void Remu(XRegister rd, XRegister rs1, XRegister rs2);

  // RV64M Standard Extension: opcode = 0x3b, funct3 0x0 and from 0x4 ~ 0x7
  void Mulw(XRegister rd, XRegister rs1, XRegister rs2);
  void Divw(XRegister rd, XRegister rs1, XRegister rs2);
  void Divuw(XRegister rd, XRegister rs1, XRegister rs2);
  void Remw(XRegister rd, XRegister rs1, XRegister rs2);
  void Remuw(XRegister rd, XRegister rs1, XRegister rs2);

  // FP load/store instructions (RV32F+RV32D): opcode = 0x07, 0x27
  void FLw(FRegister rd, XRegister rs1, int32_t offset);
  void FLd(FRegister rd, XRegister rs1, int32_t offset);
  void FSw(FRegister rs2, XRegister rs1, int32_t offset);
  void FSd(FRegister rs2, XRegister rs1, int32_t offset);

  // FP FMA instructions (RV32F+RV32D): opcode = 0x43, 0x47, 0x4b, 0x4f
  void FMAddS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMAddD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMSubS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMSubD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMSubS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMSubD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMAddS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMAddD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);

  // FP FMA instruction helpers passing the default rounding mode.
  void FMAddS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMAddS(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMAddD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMAddD(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMSubS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMSubS(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMSubD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMSubD(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMSubS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMSubS(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMSubD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMSubD(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMAddS(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMAddS(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMAddD(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMAddD(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }

  // Simple FP instructions (RV32F+RV32D): opcode = 0x53, funct7 = 0b0XXXX0D
  void FAddS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FAddD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FSubS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FSubD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FMulS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FMulD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FDivS(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FDivD(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  void FSqrtS(FRegister rd, FRegister rs1, FPRoundingMode frm);
  void FSqrtD(FRegister rd, FRegister rs1, FPRoundingMode frm);
  void FSgnjS(FRegister rd, FRegister rs1, FRegister rs2);
  void FSgnjD(FRegister rd, FRegister rs1, FRegister rs2);
  void FSgnjnS(FRegister rd, FRegister rs1, FRegister rs2);
  void FSgnjnD(FRegister rd, FRegister rs1, FRegister rs2);
  void FSgnjxS(FRegister rd, FRegister rs1, FRegister rs2);
  void FSgnjxD(FRegister rd, FRegister rs1, FRegister rs2);
  void FMinS(FRegister rd, FRegister rs1, FRegister rs2);
  void FMinD(FRegister rd, FRegister rs1, FRegister rs2);
  void FMaxS(FRegister rd, FRegister rs1, FRegister rs2);
  void FMaxD(FRegister rd, FRegister rs1, FRegister rs2);
  void FCvtSD(FRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtDS(FRegister rd, FRegister rs1, FPRoundingMode frm);

  // Simple FP instruction helpers passing the default rounding mode.
  void FAddS(FRegister rd, FRegister rs1, FRegister rs2) {
    FAddS(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FAddD(FRegister rd, FRegister rs1, FRegister rs2) {
    FAddD(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FSubS(FRegister rd, FRegister rs1, FRegister rs2) {
    FSubS(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FSubD(FRegister rd, FRegister rs1, FRegister rs2) {
    FSubD(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FMulS(FRegister rd, FRegister rs1, FRegister rs2) {
    FMulS(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FMulD(FRegister rd, FRegister rs1, FRegister rs2) {
    FMulD(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FDivS(FRegister rd, FRegister rs1, FRegister rs2) {
    FDivS(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FDivD(FRegister rd, FRegister rs1, FRegister rs2) {
    FDivD(rd, rs1, rs2, FPRoundingMode::kDefault);
  }
  void FSqrtS(FRegister rd, FRegister rs1) {
    FSqrtS(rd, rs1, FPRoundingMode::kDefault);
  }
  void FSqrtD(FRegister rd, FRegister rs1) {
    FSqrtD(rd, rs1, FPRoundingMode::kDefault);
  }
  void FCvtSD(FRegister rd, FRegister rs1) {
    FCvtSD(rd, rs1, FPRoundingMode::kDefault);
  }
  void FCvtDS(FRegister rd, FRegister rs1) {
    FCvtDS(rd, rs1, FPRoundingMode::kDefault);
  }

  // FP compare instructions (RV32F+RV32D): opcode = 0x53, funct7 = 0b101000D
  void FEqS(XRegister rd, FRegister rs1, FRegister rs2);
  void FEqD(XRegister rd, FRegister rs1, FRegister rs2);
  void FLtS(XRegister rd, FRegister rs1, FRegister rs2);
  void FLtD(XRegister rd, FRegister rs1, FRegister rs2);
  void FLeS(XRegister rd, FRegister rs1, FRegister rs2);
  void FLeD(XRegister rd, FRegister rs1, FRegister rs2);

  // FP conversion instructions (RV32F+RV32D+RV64F+RV64D): opcode = 0x53, funct7 = 0b110X00D
  void FCvtWS(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtWD(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtWuS(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtWuD(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtLS(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtLD(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtLuS(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtLuD(XRegister rd, FRegister rs1, FPRoundingMode frm);
  void FCvtSW(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtDW(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtSWu(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtDWu(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtSL(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtDL(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtSLu(FRegister rd, XRegister rs1, FPRoundingMode frm);
  void FCvtDLu(FRegister rd, XRegister rs1, FPRoundingMode frm);

  // FP conversion instruction helpers passing the default rounding mode.
  void FCvtWS(XRegister rd, FRegister rs1) { FCvtWS(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtWD(XRegister rd, FRegister rs1) { FCvtWD(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtWuS(XRegister rd, FRegister rs1) { FCvtWuS(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtWuD(XRegister rd, FRegister rs1) { FCvtWuD(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtLS(XRegister rd, FRegister rs1) { FCvtLS(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtLD(XRegister rd, FRegister rs1) { FCvtLD(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtLuS(XRegister rd, FRegister rs1) { FCvtLuS(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtLuD(XRegister rd, FRegister rs1) { FCvtLuD(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtSW(FRegister rd, XRegister rs1) { FCvtSW(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtDW(FRegister rd, XRegister rs1) { FCvtDW(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtSWu(FRegister rd, XRegister rs1) { FCvtSWu(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtDWu(FRegister rd, XRegister rs1) { FCvtDWu(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtSL(FRegister rd, XRegister rs1) { FCvtSL(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtDL(FRegister rd, XRegister rs1) { FCvtDL(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtSLu(FRegister rd, XRegister rs1) { FCvtSLu(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtDLu(FRegister rd, XRegister rs1) { FCvtDLu(rd, rs1, FPRoundingMode::kDefault); }

  // FP move instructions (RV32F+RV32D): opcode = 0x53, funct3 = 0x0, funct7 = 0b111X00D
  void FMvXW(XRegister rd, FRegister rs1);
  void FMvXD(XRegister rd, FRegister rs1);
  void FMvWX(FRegister rd, XRegister rs1);
  void FMvDX(FRegister rd, XRegister rs1);

  // FP classify instructions (RV32F+RV32D): opcode = 0x53, funct3 = 0x1, funct7 = 0b111X00D
  void FClassS(XRegister rd, FRegister rs1);
  void FClassD(XRegister rd, FRegister rs1);

  ////////////////////////////// RV64 MACRO Instructions  START ///////////////////////////////
  // These pseudo instructions are from "RISC-V Assembly Programmer's Manual".

  void Nop();
  void Mv(XRegister rd, XRegister rs);
  void Not(XRegister rd, XRegister rs);
  void Neg(XRegister rd, XRegister rs);
  void NegW(XRegister rd, XRegister rs);
  void SextB(XRegister rd, XRegister rs);
  void SextH(XRegister rd, XRegister rs);
  void SextW(XRegister rd, XRegister rs);
  void ZextB(XRegister rd, XRegister rs);
  void ZextH(XRegister rd, XRegister rs);
  void ZextW(XRegister rd, XRegister rs);
  void Seqz(XRegister rd, XRegister rs);
  void Snez(XRegister rd, XRegister rs);
  void Sltz(XRegister rd, XRegister rs);
  void Sgtz(XRegister rd, XRegister rs);
  void FMvS(FRegister rd, FRegister rs);
  void FAbsS(FRegister rd, FRegister rs);
  void FNegS(FRegister rd, FRegister rs);
  void FMvD(FRegister rd, FRegister rs);
  void FAbsD(FRegister rd, FRegister rs);
  void FNegD(FRegister rd, FRegister rs);

  // Branch pseudo instructions
  void Beqz(XRegister rs, int32_t offset);
  void Bnez(XRegister rs, int32_t offset);
  void Blez(XRegister rs, int32_t offset);
  void Bgez(XRegister rs, int32_t offset);
  void Bltz(XRegister rs, int32_t offset);
  void Bgtz(XRegister rs, int32_t offset);
  void Bgt(XRegister rs, XRegister rt, int32_t offset);
  void Ble(XRegister rs, XRegister rt, int32_t offset);
  void Bgtu(XRegister rs, XRegister rt, int32_t offset);
  void Bleu(XRegister rs, XRegister rt, int32_t offset);

  // Jump pseudo instructions
  void J(int32_t offset);
  void Jal(int32_t offset);
  void Jr(XRegister rs);
  void Jalr(XRegister rs);
  void Jalr(XRegister rd, XRegister rs);
  void Ret();

  /////////////////////////////// RV64 MACRO Instructions END ///////////////////////////////

  void Bind([[maybe_unused]] Label* label) override {
    UNIMPLEMENTED(FATAL) << "TODO: Support branches.";
  }
  void Jump([[maybe_unused]] Label* label) override {
    UNIMPLEMENTED(FATAL) << "Do not use Jump for RISCV64";
  }

 public:
  // Emit data (e.g. encoded instruction or immediate) to the instruction stream.
  void Emit(uint32_t value);

  // Emit slow paths queued during assembly and promote short branches to long if needed.
  void FinalizeCode() override;

  // Emit branches and finalize all instructions.
  void FinalizeInstructions(const MemoryRegion& region) override;

 private:
  template <typename Reg1, typename Reg2>
  void EmitI(int32_t imm12, Reg1 rs1, uint32_t funct3, Reg2 rd, uint32_t opcode) {
    DCHECK(IsInt<12>(imm12)) << imm12;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = static_cast<uint32_t>(imm12) << 20 | static_cast<uint32_t>(rs1) << 15 |
                        funct3 << 12 | static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  template <typename Reg1, typename Reg2, typename Reg3>
  void EmitR(uint32_t funct7, Reg1 rs2, Reg2 rs1, uint32_t funct3, Reg3 rd, uint32_t opcode) {
    DCHECK(IsUint<7>(funct7));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs2)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = funct7 << 25 | static_cast<uint32_t>(rs2) << 20 |
                        static_cast<uint32_t>(rs1) << 15 | funct3 << 12 |
                        static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  template <typename Reg1, typename Reg2, typename Reg3, typename Reg4>
  void EmitR4(Reg1 rs3,
              uint32_t funct2,
              Reg2 rs2,
              Reg3 rs1,
              uint32_t funct3,
              Reg4 rd,
              uint32_t opcode) {
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs3)));
    DCHECK(IsUint<2>(funct2));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs2)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = static_cast<uint32_t>(rs3) << 27 | static_cast<uint32_t>(funct2) << 25 |
                        static_cast<uint32_t>(rs2) << 20 | static_cast<uint32_t>(rs1) << 15 |
                        static_cast<uint32_t>(funct3) << 12 | static_cast<uint32_t>(rd) << 7 |
                        opcode;
    Emit(encoding);
  }

  template <typename Reg1, typename Reg2>
  void EmitS(int32_t imm12, Reg1 rs2, Reg2 rs1, uint32_t funct3, uint32_t opcode) {
    DCHECK(IsInt<12>(imm12)) << imm12;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs2)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = (static_cast<uint32_t>(imm12) & 0xFE0) << 20 |
                        static_cast<uint32_t>(rs2) << 20 | static_cast<uint32_t>(rs1) << 15 |
                        static_cast<uint32_t>(funct3) << 12 |
                        (static_cast<uint32_t>(imm12) & 0x1F) << 7 | opcode;
    Emit(encoding);
  }

  void EmitI6(uint32_t funct6,
              uint32_t imm6,
              XRegister rs1,
              uint32_t funct3,
              XRegister rd,
              uint32_t opcode) {
    DCHECK(IsUint<6>(funct6));
    DCHECK(IsUint<6>(imm6)) << imm6;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = funct6 << 26 | static_cast<uint32_t>(imm6) << 20 |
                        static_cast<uint32_t>(rs1) << 15 | funct3 << 12 |
                        static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  void EmitB(int32_t offset, XRegister rs2, XRegister rs1, uint32_t funct3, uint32_t opcode) {
    DCHECK_ALIGNED(offset, 2);
    DCHECK(IsInt<13>(offset)) << offset;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs2)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<7>(opcode));
    uint32_t imm12 = (static_cast<uint32_t>(offset) >> 1) & 0xfffu;
    uint32_t encoding = (imm12 & 0x800u) << (31 - 11) | (imm12 & 0x03f0u) << (25 - 4) |
                        static_cast<uint32_t>(rs2) << 20 | static_cast<uint32_t>(rs1) << 15 |
                        static_cast<uint32_t>(funct3) << 12 |
                        (imm12 & 0xfu) << 8 | (imm12 & 0x400u) >> (10 - 7) | opcode;
    Emit(encoding);
  }

  void EmitU(uint32_t imm20, XRegister rd, uint32_t opcode) {
    CHECK(IsUint<20>(imm20)) << imm20;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = imm20 << 12 | static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  void EmitJ(int32_t offset, XRegister rd, uint32_t opcode) {
    DCHECK_ALIGNED(offset, 2);
    CHECK(IsInt<21>(offset)) << offset;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t imm20 = (static_cast<uint32_t>(offset) >> 1) & 0xfffffu;
    uint32_t encoding = (imm20 & 0x80000u) << (31 - 19) | (imm20 & 0x03ffu) << 21 |
                        (imm20 & 0x400u) << (20 - 10) | (imm20 & 0x7f800u) << (12 - 11) |
                        static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  static constexpr uint32_t kXlen = 64;

  DISALLOW_COPY_AND_ASSIGN(Riscv64Assembler);
};

}  // namespace riscv64
}  // namespace art

#endif  // ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_
