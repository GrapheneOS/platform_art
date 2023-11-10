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

namespace art HIDDEN {
namespace riscv64 {

class ScratchRegisterScope;

static constexpr size_t kRiscv64HalfwordSize = 2;
static constexpr size_t kRiscv64WordSize = 4;
static constexpr size_t kRiscv64DoublewordSize = 8;
static constexpr size_t kRiscv64FloatRegSizeInBytes = 8;

enum class FPRoundingMode : uint32_t {
  kRNE = 0x0,  // Round to Nearest, ties to Even
  kRTZ = 0x1,  // Round towards Zero
  kRDN = 0x2,  // Round Down (towards −Infinity)
  kRUP = 0x3,  // Round Up (towards +Infinity)
  kRMM = 0x4,  // Round to Nearest, ties to Max Magnitude
  kDYN = 0x7,  // Dynamic rounding mode
  kDefault = kDYN,
  // Some instructions never need to round even though the spec includes the RM field.
  // To simplify testing, emit the RM as 0 by default for these instructions because that's what
  // `clang` does and because the `llvm-objdump` fails to disassemble the other rounding modes.
  kIgnored = 0
};

enum class AqRl : uint32_t {
  kNone    = 0x0,
  kRelease = 0x1,
  kAcquire = 0x2,
  kAqRl    = kRelease | kAcquire
};

// the type for fence
enum FenceType {
  kFenceNone = 0,
  kFenceWrite = 1,
  kFenceRead = 2,
  kFenceOutput = 4,
  kFenceInput = 8,
  kFenceDefault = 0xf,
};

// Used to test the values returned by FClassS/FClassD.
enum FPClassMaskType {
  kNegativeInfinity  = 0x001,
  kNegativeNormal    = 0x002,
  kNegativeSubnormal = 0x004,
  kNegativeZero      = 0x008,
  kPositiveZero      = 0x010,
  kPositiveSubnormal = 0x020,
  kPositiveNormal    = 0x040,
  kPositiveInfinity  = 0x080,
  kSignalingNaN      = 0x100,
  kQuietNaN          = 0x200,
};

class Riscv64Label : public Label {
 public:
  Riscv64Label() : prev_branch_id_(kNoPrevBranchId) {}

  Riscv64Label(Riscv64Label&& src) noexcept
      // NOLINTNEXTLINE - src.prev_branch_id_ is valid after the move
      : Label(std::move(src)), prev_branch_id_(src.prev_branch_id_) {}

 private:
  static constexpr uint32_t kNoPrevBranchId = std::numeric_limits<uint32_t>::max();

  uint32_t prev_branch_id_;  // To get distance from preceding branch, if any.

  friend class Riscv64Assembler;
  DISALLOW_COPY_AND_ASSIGN(Riscv64Label);
};

// Assembler literal is a value embedded in code, retrieved using a PC-relative load.
class Literal {
 public:
  static constexpr size_t kMaxSize = 8;

  Literal(uint32_t size, const uint8_t* data) : label_(), size_(size) {
    DCHECK_LE(size, Literal::kMaxSize);
    memcpy(data_, data, size);
  }

  template <typename T>
  T GetValue() const {
    DCHECK_EQ(size_, sizeof(T));
    T value;
    memcpy(&value, data_, sizeof(T));
    return value;
  }

  uint32_t GetSize() const { return size_; }

  const uint8_t* GetData() const { return data_; }

  Riscv64Label* GetLabel() { return &label_; }

  const Riscv64Label* GetLabel() const { return &label_; }

 private:
  Riscv64Label label_;
  const uint32_t size_;
  uint8_t data_[kMaxSize];

  DISALLOW_COPY_AND_ASSIGN(Literal);
};

// Jump table: table of labels emitted after the code and before the literals. Similar to literals.
class JumpTable {
 public:
  explicit JumpTable(ArenaVector<Riscv64Label*>&& labels) : label_(), labels_(std::move(labels)) {}

  size_t GetSize() const { return labels_.size() * sizeof(int32_t); }

  const ArenaVector<Riscv64Label*>& GetData() const { return labels_; }

  Riscv64Label* GetLabel() { return &label_; }

  const Riscv64Label* GetLabel() const { return &label_; }

 private:
  Riscv64Label label_;
  ArenaVector<Riscv64Label*> labels_;

  DISALLOW_COPY_AND_ASSIGN(JumpTable);
};

class Riscv64Assembler final : public Assembler {
 public:
  explicit Riscv64Assembler(ArenaAllocator* allocator,
                            const Riscv64InstructionSetFeatures* instruction_set_features = nullptr)
      : Assembler(allocator),
        branches_(allocator->Adapter(kArenaAllocAssembler)),
        finalized_(false),
        overwriting_(false),
        overwrite_location_(0),
        literals_(allocator->Adapter(kArenaAllocAssembler)),
        long_literals_(allocator->Adapter(kArenaAllocAssembler)),
        jump_tables_(allocator->Adapter(kArenaAllocAssembler)),
        last_position_adjustment_(0),
        last_old_position_(0),
        last_branch_id_(0),
        available_scratch_core_registers_((1u << TMP) | (1u << TMP2)),
        available_scratch_fp_registers_(1u << FTMP) {
    UNUSED(instruction_set_features);
    cfi().DelayEmittingAdvancePCs();
  }

  virtual ~Riscv64Assembler() {
    for (auto& branch : branches_) {
      CHECK(branch.IsResolved());
    }
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

  // Environment call and breakpoint (RV32I), opcode = 0x73
  void Ecall();
  void Ebreak();

  // Fence instruction (RV32I): opcode = 0xf, funct3 = 0
  void Fence(uint32_t pred = kFenceDefault, uint32_t succ = kFenceDefault);
  void FenceTso();

  // "Zifencei" Standard Extension, opcode = 0xf, funct3 = 1
  void FenceI();

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

  // RV32A/RV64A Standard Extension
  void LrW(XRegister rd, XRegister rs1, AqRl aqrl);
  void LrD(XRegister rd, XRegister rs1, AqRl aqrl);
  void ScW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void ScD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoSwapW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoSwapD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoAddW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoAddD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoXorW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoXorD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoAndW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoAndD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoOrW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoOrD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMinW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMinD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMaxW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMaxD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMinuW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMinuD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMaxuW(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);
  void AmoMaxuD(XRegister rd, XRegister rs2, XRegister rs1, AqRl aqrl);

  // "Zicsr" Standard Extension, opcode = 0x73, funct3 from 0x1 ~ 0x3 and 0x5 ~ 0x7
  void Csrrw(XRegister rd, uint32_t csr, XRegister rs1);
  void Csrrs(XRegister rd, uint32_t csr, XRegister rs1);
  void Csrrc(XRegister rd, uint32_t csr, XRegister rs1);
  void Csrrwi(XRegister rd, uint32_t csr, uint32_t uimm5);
  void Csrrsi(XRegister rd, uint32_t csr, uint32_t uimm5);
  void Csrrci(XRegister rd, uint32_t csr, uint32_t uimm5);

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
    FCvtDS(rd, rs1, FPRoundingMode::kIgnored);
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
  void FCvtDW(FRegister rd, XRegister rs1) { FCvtDW(rd, rs1, FPRoundingMode::kIgnored); }
  void FCvtSWu(FRegister rd, XRegister rs1) { FCvtSWu(rd, rs1, FPRoundingMode::kDefault); }
  void FCvtDWu(FRegister rd, XRegister rs1) { FCvtDWu(rd, rs1, FPRoundingMode::kIgnored); }
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

  // "Zba" Standard Extension, opcode = 0x1b, 0x33 or 0x3b, funct3 and funct7 varies.
  void AddUw(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh1Add(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh1AddUw(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh2Add(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh2AddUw(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh3Add(XRegister rd, XRegister rs1, XRegister rs2);
  void Sh3AddUw(XRegister rd, XRegister rs1, XRegister rs2);
  void SlliUw(XRegister rd, XRegister rs1, int32_t shamt);

  // "Zbb" Standard Extension, opcode = 0x13, 0x1b or 0x33, funct3 and funct7 varies.
  // Note: We do not support 32-bit sext.b, sext.h and zext.h from the Zbb extension.
  // (Neither does the clang-r498229's assembler which we currently test against.)
  void Andn(XRegister rd, XRegister rs1, XRegister rs2);
  void Orn(XRegister rd, XRegister rs1, XRegister rs2);
  void Xnor(XRegister rd, XRegister rs1, XRegister rs2);
  void Clz(XRegister rd, XRegister rs1);
  void Clzw(XRegister rd, XRegister rs1);
  void Ctz(XRegister rd, XRegister rs1);
  void Ctzw(XRegister rd, XRegister rs1);
  void Cpop(XRegister rd, XRegister rs1);
  void Cpopw(XRegister rd, XRegister rs1);
  void Min(XRegister rd, XRegister rs1, XRegister rs2);
  void Minu(XRegister rd, XRegister rs1, XRegister rs2);
  void Max(XRegister rd, XRegister rs1, XRegister rs2);
  void Maxu(XRegister rd, XRegister rs1, XRegister rs2);
  void Rol(XRegister rd, XRegister rs1, XRegister rs2);
  void Rolw(XRegister rd, XRegister rs1, XRegister rs2);
  void Ror(XRegister rd, XRegister rs1, XRegister rs2);
  void Rorw(XRegister rd, XRegister rs1, XRegister rs2);
  void Rori(XRegister rd, XRegister rs1, int32_t shamt);
  void Roriw(XRegister rd, XRegister rs1, int32_t shamt);
  void OrcB(XRegister rd, XRegister rs1);
  void Rev8(XRegister rd, XRegister rs1);

  ////////////////////////////// RV64 MACRO Instructions  START ///////////////////////////////
  // These pseudo instructions are from "RISC-V Assembly Programmer's Manual".

  void Nop();
  void Li(XRegister rd, int64_t imm);
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

  // Pseudo instructions for accessing control and status registers
  void RdCycle(XRegister rd);
  void RdTime(XRegister rd);
  void RdInstret(XRegister rd);
  void Csrr(XRegister rd, uint32_t csr);
  void Csrw(uint32_t csr, XRegister rs);
  void Csrs(uint32_t csr, XRegister rs);
  void Csrc(uint32_t csr, XRegister rs);
  void Csrwi(uint32_t csr, uint32_t uimm5);
  void Csrsi(uint32_t csr, uint32_t uimm5);
  void Csrci(uint32_t csr, uint32_t uimm5);

  // Load/store macros for arbitrary 32-bit offsets.
  void Loadb(XRegister rd, XRegister rs1, int32_t offset);
  void Loadh(XRegister rd, XRegister rs1, int32_t offset);
  void Loadw(XRegister rd, XRegister rs1, int32_t offset);
  void Loadd(XRegister rd, XRegister rs1, int32_t offset);
  void Loadbu(XRegister rd, XRegister rs1, int32_t offset);
  void Loadhu(XRegister rd, XRegister rs1, int32_t offset);
  void Loadwu(XRegister rd, XRegister rs1, int32_t offset);
  void Storeb(XRegister rs2, XRegister rs1, int32_t offset);
  void Storeh(XRegister rs2, XRegister rs1, int32_t offset);
  void Storew(XRegister rs2, XRegister rs1, int32_t offset);
  void Stored(XRegister rs2, XRegister rs1, int32_t offset);
  void FLoadw(FRegister rd, XRegister rs1, int32_t offset);
  void FLoadd(FRegister rd, XRegister rs1, int32_t offset);
  void FStorew(FRegister rs2, XRegister rs1, int32_t offset);
  void FStored(FRegister rs2, XRegister rs1, int32_t offset);

  // Macros for loading constants.
  void LoadConst32(XRegister rd, int32_t value);
  void LoadConst64(XRegister rd, int64_t value);

  // Macros for adding constants.
  void AddConst32(XRegister rd, XRegister rs1, int32_t value);
  void AddConst64(XRegister rd, XRegister rs1, int64_t value);

  // Jumps and branches to a label.
  void Beqz(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Bnez(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Blez(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Bgez(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Bltz(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Bgtz(XRegister rs, Riscv64Label* label, bool is_bare = false);
  void Beq(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bne(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Ble(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bge(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Blt(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bgt(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bleu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bgeu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bltu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Bgtu(XRegister rs, XRegister rt, Riscv64Label* label, bool is_bare = false);
  void Jal(XRegister rd, Riscv64Label* label, bool is_bare = false);
  void J(Riscv64Label* label, bool is_bare = false);
  void Jal(Riscv64Label* label, bool is_bare = false);

  // Literal load.
  void Loadw(XRegister rd, Literal* literal);
  void Loadwu(XRegister rd, Literal* literal);
  void Loadd(XRegister rd, Literal* literal);
  void FLoadw(FRegister rd, Literal* literal);
  void FLoadd(FRegister rd, Literal* literal);

  // Illegal instruction that triggers SIGILL.
  void Unimp();

  /////////////////////////////// RV64 MACRO Instructions END ///////////////////////////////

  void Bind(Label* label) override { Bind(down_cast<Riscv64Label*>(label)); }

  void Jump([[maybe_unused]] Label* label) override {
    UNIMPLEMENTED(FATAL) << "Do not use Jump for RISCV64";
  }

  void Jump(Riscv64Label* label) {
    J(label);
  }

  void Bind(Riscv64Label* label);

  // Load label address using PC-relative loads.
  void LoadLabelAddress(XRegister rd, Riscv64Label* label);

  // Create a new literal with a given value.
  // NOTE:Use `Identity<>` to force the template parameter to be explicitly specified.
  template <typename T>
  Literal* NewLiteral(typename Identity<T>::type value) {
    static_assert(std::is_integral<T>::value, "T must be an integral type.");
    return NewLiteral(sizeof(value), reinterpret_cast<const uint8_t*>(&value));
  }

  // Create a new literal with the given data.
  Literal* NewLiteral(size_t size, const uint8_t* data);

  // Create a jump table for the given labels that will be emitted when finalizing.
  // When the table is emitted, offsets will be relative to the location of the table.
  // The table location is determined by the location of its label (the label precedes
  // the table data) and should be loaded using LoadLabelAddress().
  JumpTable* CreateJumpTable(ArenaVector<Riscv64Label*>&& labels);

 public:
  // Emit slow paths queued during assembly, promote short branches to long if needed,
  // and emit branches.
  void FinalizeCode() override;

  // Returns the current location of a label.
  //
  // This function must be used instead of `Riscv64Label::GetPosition()`
  // which returns assembler's internal data instead of an actual location.
  //
  // The location can change during branch fixup in `FinalizeCode()`. Before that,
  // the location is not final and therefore not very useful to external users,
  // so they should preferably retrieve the location only after `FinalizeCode()`.
  uint32_t GetLabelLocation(const Riscv64Label* label) const;

  // Get the final position of a label after local fixup based on the old position
  // recorded before FinalizeCode().
  uint32_t GetAdjustedPosition(uint32_t old_position);

 private:
  enum BranchCondition : uint8_t {
    kCondEQ,
    kCondNE,
    kCondLT,
    kCondGE,
    kCondLE,
    kCondGT,
    kCondLTU,
    kCondGEU,
    kCondLEU,
    kCondGTU,
    kUncond,
  };

  // Note that PC-relative literal loads are handled as pseudo branches because they need
  // to be emitted after branch relocation to use correct offsets.
  class Branch {
   public:
    enum Type : uint8_t {
      // TODO(riscv64): Support 16-bit instructions ("C" Standard Extension).

      // Short branches (can be promoted to longer).
      kCondBranch,
      kUncondBranch,
      kCall,
      // Short branches (can't be promoted to longer).
      kBareCondBranch,
      kBareUncondBranch,
      kBareCall,

      // Medium branch (can be promoted to long).
      kCondBranch21,

      // Long branches.
      kLongCondBranch,
      kLongUncondBranch,
      kLongCall,

      // Label.
      kLabel,

      // Literals.
      kLiteral,
      kLiteralUnsigned,
      kLiteralLong,
      kLiteralFloat,
      kLiteralDouble,
    };

    // Bit sizes of offsets defined as enums to minimize chance of typos.
    enum OffsetBits {
      kOffset13 = 13,
      kOffset21 = 21,
      kOffset32 = 32,
    };

    static constexpr uint32_t kUnresolved = 0xffffffff;  // Unresolved target_
    static constexpr uint32_t kMaxBranchLength = 12;  // In bytes.

    struct BranchInfo {
      // Branch length in bytes.
      uint32_t length;
      // The offset in bytes of the PC used in the (only) PC-relative instruction from
      // the start of the branch sequence. RISC-V always uses the address of the PC-relative
      // instruction as the PC, so this is essentially the offset of that instruction.
      uint32_t pc_offset;
      // How large (in bits) a PC-relative offset can be for a given type of branch.
      OffsetBits offset_size;
    };
    static const BranchInfo branch_info_[/* Type */];

    // Unconditional branch or call.
    Branch(uint32_t location, uint32_t target, XRegister rd, bool is_bare);
    // Conditional branch.
    Branch(uint32_t location,
           uint32_t target,
           BranchCondition condition,
           XRegister lhs_reg,
           XRegister rhs_reg,
           bool is_bare);
    // Label address or literal.
    Branch(uint32_t location, uint32_t target, XRegister rd, Type label_or_literal_type);
    Branch(uint32_t location, uint32_t target, FRegister rd, Type literal_type);

    // Some conditional branches with lhs = rhs are effectively NOPs, while some
    // others are effectively unconditional.
    static bool IsNop(BranchCondition condition, XRegister lhs, XRegister rhs);
    static bool IsUncond(BranchCondition condition, XRegister lhs, XRegister rhs);

    static BranchCondition OppositeCondition(BranchCondition cond);

    Type GetType() const;
    BranchCondition GetCondition() const;
    XRegister GetLeftRegister() const;
    XRegister GetRightRegister() const;
    FRegister GetFRegister() const;
    uint32_t GetTarget() const;
    uint32_t GetLocation() const;
    uint32_t GetOldLocation() const;
    uint32_t GetLength() const;
    uint32_t GetOldLength() const;
    uint32_t GetEndLocation() const;
    uint32_t GetOldEndLocation() const;
    bool IsBare() const;
    bool IsResolved() const;

    // Returns the bit size of the signed offset that the branch instruction can handle.
    OffsetBits GetOffsetSize() const;

    // Calculates the distance between two byte locations in the assembler buffer and
    // returns the number of bits needed to represent the distance as a signed integer.
    static OffsetBits GetOffsetSizeNeeded(uint32_t location, uint32_t target);

    // Resolve a branch when the target is known.
    void Resolve(uint32_t target);

    // Relocate a branch by a given delta if needed due to expansion of this or another
    // branch at a given location by this delta (just changes location_ and target_).
    void Relocate(uint32_t expand_location, uint32_t delta);

    // If necessary, updates the type by promoting a short branch to a longer branch
    // based on the branch location and target. Returns the amount (in bytes) by
    // which the branch size has increased.
    uint32_t PromoteIfNeeded();

    // Returns the offset into assembler buffer that shall be used as the base PC for
    // offset calculation. RISC-V always uses the address of the PC-relative instruction
    // as the PC, so this is essentially the location of that instruction.
    uint32_t GetOffsetLocation() const;

    // Calculates and returns the offset ready for encoding in the branch instruction(s).
    int32_t GetOffset() const;

   private:
    // Completes branch construction by determining and recording its type.
    void InitializeType(Type initial_type);
    // Helper for the above.
    void InitShortOrLong(OffsetBits ofs_size, Type short_type, Type long_type, Type longest_type);

    uint32_t old_location_;  // Offset into assembler buffer in bytes.
    uint32_t location_;      // Offset into assembler buffer in bytes.
    uint32_t target_;        // Offset into assembler buffer in bytes.

    XRegister lhs_reg_;          // Left-hand side register in conditional branches or
                                 // destination register in calls or literals.
    XRegister rhs_reg_;          // Right-hand side register in conditional branches.
    FRegister freg_;             // Destination register in FP literals.
    BranchCondition condition_;  // Condition for conditional branches.

    Type type_;      // Current type of the branch.
    Type old_type_;  // Initial type of the branch.
  };

  // Branch and literal fixup.

  void EmitBcond(BranchCondition cond, XRegister rs, XRegister rt, int32_t offset);
  void EmitBranch(Branch* branch);
  void EmitBranches();
  void EmitJumpTables();
  void EmitLiterals();

  void FinalizeLabeledBranch(Riscv64Label* label);
  void Bcond(Riscv64Label* label,
             bool is_bare,
             BranchCondition condition,
             XRegister lhs,
             XRegister rhs);
  void Buncond(Riscv64Label* label, XRegister rd, bool is_bare);
  template <typename XRegisterOrFRegister>
  void LoadLiteral(Literal* literal, XRegisterOrFRegister rd, Branch::Type literal_type);

  Branch* GetBranch(uint32_t branch_id);
  const Branch* GetBranch(uint32_t branch_id) const;

  void ReserveJumpTableSpace();
  void PromoteBranches();
  void PatchCFI();

  // Emit data (e.g. encoded instruction or immediate) to the instruction stream.
  void Emit(uint32_t value);

  // Adjust base register and offset if needed for load/store with a large offset.
  void AdjustBaseAndOffset(XRegister& base, int32_t& offset, ScratchRegisterScope& srs);

  // Helper templates for loads/stores with 32-bit offsets.
  template <void (Riscv64Assembler::*insn)(XRegister, XRegister, int32_t)>
  void LoadFromOffset(XRegister rd, XRegister rs1, int32_t offset);
  template <void (Riscv64Assembler::*insn)(XRegister, XRegister, int32_t)>
  void StoreToOffset(XRegister rs2, XRegister rs1, int32_t offset);
  template <void (Riscv64Assembler::*insn)(FRegister, XRegister, int32_t)>
  void FLoadFromOffset(FRegister rd, XRegister rs1, int32_t offset);
  template <void (Riscv64Assembler::*insn)(FRegister, XRegister, int32_t)>
  void FStoreToOffset(FRegister rs2, XRegister rs1, int32_t offset);

  // Implementation helper for `Li()`, `LoadConst32()` and `LoadConst64()`.
  void LoadImmediate(XRegister rd, int64_t imm, bool can_use_tmp);

  // Emit helpers.

  // I-type instruction:
  //
  //    31                   20 19     15 14 12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . . . . . . . . | . . . . | . . | . . . . | . . . . . . ]
  //   [        imm11:0            rs1   funct3     rd        opcode   ]
  //   -----------------------------------------------------------------
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

  // R-type instruction:
  //
  //    31         25 24     20 19     15 14 12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . . . | . . . . | . . . . | . . | . . . . | . . . . . . ]
  //   [   funct7        rs2       rs1   funct3     rd        opcode   ]
  //   -----------------------------------------------------------------
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

  // R-type instruction variant for floating-point fused multiply-add/sub (F[N]MADD/ F[N]MSUB):
  //
  //    31     27  25 24     20 19     15 14 12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . | . | . . . . | . . . . | . . | . . . . | . . . . . . ]
  //   [  rs3     fmt    rs2       rs1   funct3     rd        opcode   ]
  //   -----------------------------------------------------------------
  template <typename Reg1, typename Reg2, typename Reg3, typename Reg4>
  void EmitR4(
      Reg1 rs3, uint32_t fmt, Reg2 rs2, Reg3 rs1, uint32_t funct3, Reg4 rd, uint32_t opcode) {
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs3)));
    DCHECK(IsUint<2>(fmt));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs2)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rs1)));
    DCHECK(IsUint<3>(funct3));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = static_cast<uint32_t>(rs3) << 27 | static_cast<uint32_t>(fmt) << 25 |
                        static_cast<uint32_t>(rs2) << 20 | static_cast<uint32_t>(rs1) << 15 |
                        static_cast<uint32_t>(funct3) << 12 | static_cast<uint32_t>(rd) << 7 |
                        opcode;
    Emit(encoding);
  }

  // S-type instruction:
  //
  //    31         25 24     20 19     15 14 12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . . . | . . . . | . . . . | . . | . . . . | . . . . . . ]
  //   [   imm11:5       rs2       rs1   funct3   imm4:0      opcode   ]
  //   -----------------------------------------------------------------
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

  // I-type instruction variant for shifts (SLLI / SRLI / SRAI):
  //
  //    31       26 25       20 19     15 14 12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . . | . . . . . | . . . . | . . | . . . . | . . . . . . ]
  //   [  imm11:6  imm5:0(shamt)   rs1   funct3     rd        opcode   ]
  //   -----------------------------------------------------------------
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

  // B-type instruction:
  //
  //   31 30       25 24     20 19     15 14 12 11    8 7 6           0
  //   -----------------------------------------------------------------
  //   [ | . . . . . | . . . . | . . . . | . . | . . . | | . . . . . . ]
  //  imm12 imm11:5      rs2       rs1   funct3 imm4:1 imm11  opcode   ]
  //   -----------------------------------------------------------------
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

  // U-type instruction:
  //
  //    31                                   12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . . . . | . . . . | . . . . . . ]
  //   [                imm31:12                    rd        opcode   ]
  //   -----------------------------------------------------------------
  void EmitU(uint32_t imm20, XRegister rd, uint32_t opcode) {
    CHECK(IsUint<20>(imm20)) << imm20;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<7>(opcode));
    uint32_t encoding = imm20 << 12 | static_cast<uint32_t>(rd) << 7 | opcode;
    Emit(encoding);
  }

  // J-type instruction:
  //
  //   31 30               21   19           12 11      7 6           0
  //   -----------------------------------------------------------------
  //   [ | . . . . . . . . . | | . . . . . . . | . . . . | . . . . . . ]
  //  imm20    imm10:1      imm11   imm19:12        rd        opcode   ]
  //   -----------------------------------------------------------------
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

  ArenaVector<Branch> branches_;

  // For checking that we finalize the code only once.
  bool finalized_;

  // Whether appending instructions at the end of the buffer or overwriting the existing ones.
  bool overwriting_;
  // The current overwrite location.
  uint32_t overwrite_location_;

  // Use `std::deque<>` for literal labels to allow insertions at the end
  // without invalidating pointers and references to existing elements.
  ArenaDeque<Literal> literals_;
  ArenaDeque<Literal> long_literals_;  // 64-bit literals separated for alignment reasons.

  // Jump table list.
  ArenaDeque<JumpTable> jump_tables_;

  // Data for `GetAdjustedPosition()`, see the description there.
  uint32_t last_position_adjustment_;
  uint32_t last_old_position_;
  uint32_t last_branch_id_;

  uint32_t available_scratch_core_registers_;
  uint32_t available_scratch_fp_registers_;

  static constexpr uint32_t kXlen = 64;

  friend class ScratchRegisterScope;

  DISALLOW_COPY_AND_ASSIGN(Riscv64Assembler);
};

class ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(Riscv64Assembler* assembler)
      : assembler_(assembler),
        old_available_scratch_core_registers_(assembler->available_scratch_core_registers_),
        old_available_scratch_fp_registers_(assembler->available_scratch_fp_registers_) {}

  ~ScratchRegisterScope() {
    assembler_->available_scratch_core_registers_ = old_available_scratch_core_registers_;
    assembler_->available_scratch_fp_registers_ = old_available_scratch_fp_registers_;
  }

  // Alocate a scratch `XRegister`. There must be an available register to allocate.
  XRegister AllocateXRegister() {
    CHECK_NE(assembler_->available_scratch_core_registers_, 0u);
    // Allocate the highest available scratch register (prefer TMP(T6) over TMP2(T5)).
    uint32_t reg_num = (BitSizeOf(assembler_->available_scratch_core_registers_) - 1u) -
                       CLZ(assembler_->available_scratch_core_registers_);
    assembler_->available_scratch_core_registers_ &= ~(1u << reg_num);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    return enum_cast<XRegister>(reg_num);
  }

  // Free a previously unavailable core register for use as a scratch register.
  // This can be an arbitrary register, not necessarly the usual `TMP` or `TMP2`.
  void FreeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    CHECK_EQ((1u << reg_num) & assembler_->available_scratch_core_registers_, 0u);
    assembler_->available_scratch_core_registers_ |= 1u << reg_num;
  }

  // The number of available scratch core registers.
  size_t AvailableXRegisters() {
    return POPCOUNT(assembler_->available_scratch_core_registers_);
  }

  // Make sure a core register is available for use as a scratch register.
  void IncludeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    assembler_->available_scratch_core_registers_ |= 1u << reg_num;
  }

  // Make sure a core register is not available for use as a scratch register.
  void ExcludeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    assembler_->available_scratch_core_registers_ &= ~(1u << reg_num);
  }

  // Alocate a scratch `FRegister`. There must be an available register to allocate.
  FRegister AllocateFRegister() {
    CHECK_NE(assembler_->available_scratch_fp_registers_, 0u);
    // Allocate the highest available scratch register (same as for core registers).
    uint32_t reg_num = (BitSizeOf(assembler_->available_scratch_fp_registers_) - 1u) -
                       CLZ(assembler_->available_scratch_fp_registers_);
    assembler_->available_scratch_fp_registers_ &= ~(1u << reg_num);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    return enum_cast<FRegister>(reg_num);
  }

  // Free a previously unavailable FP register for use as a scratch register.
  // This can be an arbitrary register, not necessarly the usual `FTMP`.
  void FreeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    CHECK_EQ((1u << reg_num) & assembler_->available_scratch_fp_registers_, 0u);
    assembler_->available_scratch_fp_registers_ |= 1u << reg_num;
  }

  // The number of available scratch FP registers.
  size_t AvailableFRegisters() {
    return POPCOUNT(assembler_->available_scratch_fp_registers_);
  }

  // Make sure an FP register is available for use as a scratch register.
  void IncludeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    assembler_->available_scratch_fp_registers_ |= 1u << reg_num;
  }

  // Make sure an FP register is not available for use as a scratch register.
  void ExcludeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    assembler_->available_scratch_fp_registers_ &= ~(1u << reg_num);
  }

 private:
  Riscv64Assembler* const assembler_;
  const uint32_t old_available_scratch_core_registers_;
  const uint32_t old_available_scratch_fp_registers_;

  DISALLOW_COPY_AND_ASSIGN(ScratchRegisterScope);
};

}  // namespace riscv64
}  // namespace art

#endif  // ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_
