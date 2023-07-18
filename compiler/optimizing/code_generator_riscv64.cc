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

#include "code_generator_riscv64.h"

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "arch/riscv64/registers_riscv64.h"
#include "base/macros.h"
#include "code_generator_utils.h"
#include "dwarf/register.h"
#include "heap_poisoning.h"
#include "intrinsics_list.h"
#include "jit/profiling_info.h"
#include "mirror/class-inl.h"
#include "optimizing/nodes.h"
#include "stack_map_stream.h"
#include "utils/label.h"
#include "utils/riscv64/assembler_riscv64.h"
#include "utils/stack_checks.h"

namespace art {
namespace riscv64 {

static constexpr XRegister kCoreCalleeSaves[] = {
    // S1(TR) is excluded as the ART thread register.
    S0, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, RA
};

static constexpr FRegister kFpuCalleeSaves[] = {
    FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7, FS8, FS9, FS10, FS11
};

#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kRiscv64PointerSize, x).Int32Value()

Location RegisterOrZeroBitPatternLocation(HInstruction* instruction) {
  return IsZeroBitPattern(instruction)
      ? Location::ConstantLocation(instruction->AsConstant())
      : Location::RequiresRegister();
}

XRegister InputXRegisterOrZero(Location location) {
  if (location.IsConstant()) {
    DCHECK(location.GetConstant()->IsZeroBitPattern());
    return Zero;
  } else {
    return location.AsRegister<XRegister>();
  }
}

Location Riscv64ReturnLocation(DataType::Type return_type) {
  switch (return_type) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kUint32:
    case DataType::Type::kInt32:
    case DataType::Type::kReference:
    case DataType::Type::kUint64:
    case DataType::Type::kInt64:
      return Location::RegisterLocation(A0);

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      return Location::FpuRegisterLocation(FA0);

    case DataType::Type::kVoid:
      return Location::NoLocation();
  }
  UNREACHABLE();
}

Location InvokeDexCallingConventionVisitorRISCV64::GetReturnLocation(DataType::Type type) const {
  return Riscv64ReturnLocation(type);
}

Location InvokeDexCallingConventionVisitorRISCV64::GetMethodLocation() const {
  return Location::RegisterLocation(kArtMethodRegister);
}

Location InvokeDexCallingConventionVisitorRISCV64::GetNextLocation(DataType::Type type) {
  Location next_location;
  if (type == DataType::Type::kVoid) {
    LOG(FATAL) << "Unexpected parameter type " << type;
  }

  // Note: Unlike the RISC-V C/C++ calling convention, managed ABI does not use
  // GPRs to pass FP args when we run out of FPRs.
  if (DataType::IsFloatingPointType(type) &&
      float_index_ < calling_convention.GetNumberOfFpuRegisters()) {
    next_location =
        Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(float_index_++));
  } else if (!DataType::IsFloatingPointType(type) &&
             (gp_index_ < calling_convention.GetNumberOfRegisters())) {
    next_location = Location::RegisterLocation(calling_convention.GetRegisterAt(gp_index_++));
  } else {
    size_t stack_offset = calling_convention.GetStackOffsetOf(stack_index_);
    next_location = DataType::Is64BitType(type) ? Location::DoubleStackSlot(stack_offset) :
                                                  Location::StackSlot(stack_offset);
  }

  // Space on the stack is reserved for all arguments.
  stack_index_ += DataType::Is64BitType(type) ? 2 : 1;

  return next_location;
}

#define __ down_cast<CodeGeneratorRISCV64*>(codegen)->GetAssembler()->  // NOLINT

void LocationsBuilderRISCV64::HandleInvoke(HInvoke* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

Location LocationsBuilderRISCV64::RegisterOrZeroConstant(HInstruction* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

Location LocationsBuilderRISCV64::FpuRegisterOrConstantForStore(HInstruction* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

class CompileOptimizedSlowPathRISCV64 : public SlowPathCodeRISCV64 {
 public:
  CompileOptimizedSlowPathRISCV64() : SlowPathCodeRISCV64(/*instruction=*/ nullptr) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    uint32_t entrypoint_offset =
        GetThreadOffset<kRiscv64PointerSize>(kQuickCompileOptimized).Int32Value();
    __ Bind(GetEntryLabel());
    __ Loadd(RA, TR, entrypoint_offset);
    // Note: we don't record the call here (and therefore don't generate a stack
    // map), as the entrypoint should never be suspended.
    __ Jalr(RA);
    __ J(GetExitLabel());
  }

  const char* GetDescription() const override { return "CompileOptimizedSlowPath"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompileOptimizedSlowPathRISCV64);
};

class SuspendCheckSlowPathRISCV64 : public SlowPathCodeRISCV64 {
 public:
  SuspendCheckSlowPathRISCV64(HSuspendCheck* instruction, HBasicBlock* successor)
      : SlowPathCodeRISCV64(instruction), successor_(successor) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    LocationSummary* locations = instruction_->GetLocations();
    CodeGeneratorRISCV64* riscv64_codegen = down_cast<CodeGeneratorRISCV64*>(codegen);
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);  // Only saves live vector registers for SIMD.
    riscv64_codegen->InvokeRuntime(kQuickTestSuspend, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickTestSuspend, void, void>();
    RestoreLiveRegisters(codegen, locations);  // Only restores live vector registers for SIMD.
    if (successor_ == nullptr) {
      __ J(GetReturnLabel());
    } else {
      __ J(riscv64_codegen->GetLabelOf(successor_));
    }
  }

  Riscv64Label* GetReturnLabel() {
    DCHECK(successor_ == nullptr);
    return &return_label_;
  }

  const char* GetDescription() const override { return "SuspendCheckSlowPathRISCV64"; }

  HBasicBlock* GetSuccessor() const { return successor_; }

 private:
  // If not null, the block to branch to after the suspend check.
  HBasicBlock* const successor_;

  // If `successor_` is null, the label to branch to after the suspend check.
  Riscv64Label return_label_;

  DISALLOW_COPY_AND_ASSIGN(SuspendCheckSlowPathRISCV64);
};

class NullCheckSlowPathRISCV64 : public SlowPathCodeRISCV64 {
 public:
  explicit NullCheckSlowPathRISCV64(HNullCheck* instr) : SlowPathCodeRISCV64(instr) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    CodeGeneratorRISCV64* riscv64_codegen = down_cast<CodeGeneratorRISCV64*>(codegen);
    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    riscv64_codegen->InvokeRuntime(
        kQuickThrowNullPointer, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowNullPointer, void, void>();
  }

  bool IsFatal() const override { return true; }

  const char* GetDescription() const override { return "NullCheckSlowPathRISCV64"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullCheckSlowPathRISCV64);
};

#undef __
#define __ down_cast<Riscv64Assembler*>(GetAssembler())->  // NOLINT

template <typename Reg,
          void (Riscv64Assembler::*opS)(Reg, FRegister, FRegister),
          void (Riscv64Assembler::*opD)(Reg, FRegister, FRegister)>
inline void InstructionCodeGeneratorRISCV64::FpBinOp(
    Reg rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  Riscv64Assembler* assembler = down_cast<CodeGeneratorRISCV64*>(codegen_)->GetAssembler();
  if (type == DataType::Type::kFloat32) {
    (assembler->*opS)(rd, rs1, rs2);
  } else {
    DCHECK_EQ(type, DataType::Type::kFloat64);
    (assembler->*opD)(rd, rs1, rs2);
  }
}

inline void InstructionCodeGeneratorRISCV64::FAdd(
    FRegister rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  FpBinOp<FRegister, &Riscv64Assembler::FAddS, &Riscv64Assembler::FAddD>(rd, rs1, rs2, type);
}

inline void InstructionCodeGeneratorRISCV64::FSub(
    FRegister rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  FpBinOp<FRegister, &Riscv64Assembler::FSubS, &Riscv64Assembler::FSubD>(rd, rs1, rs2, type);
}

inline void InstructionCodeGeneratorRISCV64::FEq(
    XRegister rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  FpBinOp<XRegister, &Riscv64Assembler::FEqS, &Riscv64Assembler::FEqD>(rd, rs1, rs2, type);
}

inline void InstructionCodeGeneratorRISCV64::FLt(
    XRegister rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  FpBinOp<XRegister, &Riscv64Assembler::FLtS, &Riscv64Assembler::FLtD>(rd, rs1, rs2, type);
}

inline void InstructionCodeGeneratorRISCV64::FLe(
    XRegister rd, FRegister rs1, FRegister rs2, DataType::Type type) {
  FpBinOp<XRegister, &Riscv64Assembler::FLeS, &Riscv64Assembler::FLeD>(rd, rs1, rs2, type);
}

InstructionCodeGeneratorRISCV64::InstructionCodeGeneratorRISCV64(HGraph* graph,
                                                                 CodeGeneratorRISCV64* codegen)
    : InstructionCodeGenerator(graph, codegen),
      assembler_(codegen->GetAssembler()),
      codegen_(codegen) {}

void InstructionCodeGeneratorRISCV64::GenerateClassInitializationCheck(
    SlowPathCodeRISCV64* slow_path, XRegister class_reg) {
  UNUSED(slow_path);
  UNUSED(class_reg);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateBitstringTypeCheckCompare(
    HTypeCheckInstruction* instruction, XRegister temp) {
  UNUSED(instruction);
  UNUSED(temp);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateSuspendCheck(HSuspendCheck* instruction,
                                                           HBasicBlock* successor) {
  if (instruction->IsNoOp()) {
    if (successor != nullptr) {
      __ J(codegen_->GetLabelOf(successor));
    }
    return;
  }

  if (codegen_->CanUseImplicitSuspendCheck()) {
    LOG(FATAL) << "Unimplemented ImplicitSuspendCheck";
    return;
  }

  SuspendCheckSlowPathRISCV64* slow_path =
      down_cast<SuspendCheckSlowPathRISCV64*>(instruction->GetSlowPath());

  if (slow_path == nullptr) {
    slow_path =
        new (codegen_->GetScopedAllocator()) SuspendCheckSlowPathRISCV64(instruction, successor);
    instruction->SetSlowPath(slow_path);
    codegen_->AddSlowPath(slow_path);
    if (successor != nullptr) {
      DCHECK(successor->IsLoopHeader());
    }
  } else {
    DCHECK_EQ(slow_path->GetSuccessor(), successor);
  }

  ScratchRegisterScope srs(GetAssembler());
  XRegister tmp = srs.AllocateXRegister();
  __ Loadw(tmp, TR, Thread::ThreadFlagsOffset<kRiscv64PointerSize>().Int32Value());
  static_assert(Thread::SuspendOrCheckpointRequestFlags() != std::numeric_limits<uint32_t>::max());
  static_assert(IsPowerOfTwo(Thread::SuspendOrCheckpointRequestFlags() + 1u));
  // Shift out other bits. Use an instruction that can be 16-bit with the "C" Standard Extension.
  __ Slli(tmp, tmp, CLZ(static_cast<uint64_t>(Thread::SuspendOrCheckpointRequestFlags())));
  if (successor == nullptr) {
    __ Bnez(tmp, slow_path->GetEntryLabel());
    __ Bind(slow_path->GetReturnLabel());
  } else {
    __ Beqz(tmp, codegen_->GetLabelOf(successor));
    __ J(slow_path->GetEntryLabel());
    // slow_path will return to GetLabelOf(successor).
  }
}

void InstructionCodeGeneratorRISCV64::GenerateMinMaxInt(LocationSummary* locations, bool is_min) {
  UNUSED(locations);
  UNUSED(is_min);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateMinMaxFP(LocationSummary* locations,
                                                       bool is_min,
                                                       DataType::Type type) {
  UNUSED(locations);
  UNUSED(is_min);
  UNUSED(type);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateMinMax(HBinaryOperation* instruction, bool is_min) {
  UNUSED(instruction);
  UNUSED(is_min);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateReferenceLoadOneRegister(
    HInstruction* instruction,
    Location out,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  UNUSED(instruction);
  UNUSED(out);
  UNUSED(offset);
  UNUSED(maybe_temp);
  UNUSED(read_barrier_option);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateReferenceLoadTwoRegisters(
    HInstruction* instruction,
    Location out,
    Location obj,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  UNUSED(instruction);
  UNUSED(out);
  UNUSED(offset);
  UNUSED(maybe_temp);
  UNUSED(read_barrier_option);
  UNUSED(obj);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateGcRootFieldLoad(HInstruction* instruction,
                                                              Location root,
                                                              XRegister obj,
                                                              uint32_t offset,
                                                              ReadBarrierOption read_barrier_option,
                                                              Riscv64Label* label_low) {
  UNUSED(instruction);
  UNUSED(root);
  UNUSED(obj);
  UNUSED(offset);
  UNUSED(read_barrier_option);
  UNUSED(label_low);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateTestAndBranch(HInstruction* instruction,
                                                            size_t condition_input_index,
                                                            Riscv64Label* true_target,
                                                            Riscv64Label* false_target) {
  HInstruction* cond = instruction->InputAt(condition_input_index);

  if (true_target == nullptr && false_target == nullptr) {
    // Nothing to do. The code always falls through.
    return;
  } else if (cond->IsIntConstant()) {
    // Constant condition, statically compared against "true" (integer value 1).
    if (cond->AsIntConstant()->IsTrue()) {
      if (true_target != nullptr) {
        __ J(true_target);
      }
    } else {
      DCHECK(cond->AsIntConstant()->IsFalse()) << cond->AsIntConstant()->GetValue();
      if (false_target != nullptr) {
        __ J(false_target);
      }
    }
    return;
  }

  // The following code generates these patterns:
  //  (1) true_target == nullptr && false_target != nullptr
  //        - opposite condition true => branch to false_target
  //  (2) true_target != nullptr && false_target == nullptr
  //        - condition true => branch to true_target
  //  (3) true_target != nullptr && false_target != nullptr
  //        - condition true => branch to true_target
  //        - branch to false_target
  if (IsBooleanValueOrMaterializedCondition(cond)) {
    // The condition instruction has been materialized, compare the output to 0.
    Location cond_val = instruction->GetLocations()->InAt(condition_input_index);
    DCHECK(cond_val.IsRegister());
    if (true_target == nullptr) {
      __ Beqz(cond_val.AsRegister<XRegister>(), false_target);
    } else {
      __ Bnez(cond_val.AsRegister<XRegister>(), true_target);
    }
  } else {
    // The condition instruction has not been materialized, use its inputs as
    // the comparison and its condition as the branch condition.
    HCondition* condition = cond->AsCondition();
    DataType::Type type = condition->InputAt(0)->GetType();
    LocationSummary* locations = condition->GetLocations();
    IfCondition if_cond = condition->GetCondition();
    Riscv64Label* branch_target = true_target;

    if (true_target == nullptr) {
      if_cond = condition->GetOppositeCondition();
      branch_target = false_target;
    }

    switch (type) {
      case DataType::Type::kFloat32:
      case DataType::Type::kFloat64:
        GenerateFpCondition(if_cond, condition->IsGtBias(), type, locations, branch_target);
        break;
      default:
        // Integral types and reference equality.
        GenerateIntLongCompareAndBranch(if_cond, locations, branch_target);
        break;
    }
  }

  // If neither branch falls through (case 3), the conditional branch to `true_target`
  // was already emitted (case 2) and we need to emit a jump to `false_target`.
  if (true_target != nullptr && false_target != nullptr) {
    __ J(false_target);
  }
}

void InstructionCodeGeneratorRISCV64::DivRemOneOrMinusOne(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::DivRemByPowerOfTwo(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateDivRemWithAnyConstant(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateDivRemIntegral(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenerateIntLongCondition(IfCondition cond,
                                                               LocationSummary* locations) {
  XRegister rd = locations->Out().AsRegister<XRegister>();
  XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
  Location rs2_location = locations->InAt(1);
  bool use_imm = rs2_location.IsConstant();
  int64_t imm = use_imm ? CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant()) : 0;
  XRegister rs2 = use_imm ? kNoXRegister : rs2_location.AsRegister<XRegister>();
  switch (cond) {
    case kCondEQ:
    case kCondNE:
      if (!use_imm) {
        __ Sub(rd, rs1, rs2);  // SUB is OK here even for 32-bit comparison.
      } else if (imm != 0) {
        DCHECK(IsInt<12>(-imm));
        __ Addi(rd, rs1, -imm);  // ADDI is OK here even for 32-bit comparison.
      }  // else test `rs1` directly without subtraction for `use_imm && imm == 0`.
      if (cond == kCondEQ) {
        __ Seqz(rd, (use_imm && imm == 0) ? rs1 : rd);
      } else {
        __ Snez(rd, (use_imm && imm == 0) ? rs1 : rd);
      }
      break;

    case kCondLT:
    case kCondGE:
      if (use_imm) {
        DCHECK(IsInt<12>(imm));
        __ Slti(rd, rs1, imm);
      } else {
        __ Slt(rd, rs1, rs2);
      }
      if (cond == kCondGE) {
        // Calculate `rs1 >= rhs` as `!(rs1 < rhs)` since there's only the SLT but no SGE.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondLE:
    case kCondGT:
      if (use_imm) {
        // Calculate `rs1 <= imm` as `rs1 < imm + 1`.
        DCHECK(IsInt<12>(imm + 1));  // The value that overflows would fail this check.
        __ Slti(rd, rs1, imm + 1);
      } else {
        __ Slt(rd, rs2, rs1);
      }
      if ((cond == kCondGT) == use_imm) {
        // Calculate `rs1 > imm` as `!(rs1 < imm + 1)` and calculate
        // `rs1 <= rs2` as `!(rs2 < rs1)` since there's only the SLT but no SGE.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondB:
    case kCondAE:
      if (use_imm) {
        // Sltiu sign-extends its 12-bit immediate operand before the comparison
        // and thus lets us compare directly with unsigned values in the ranges
        // [0, 0x7ff] and [0x[ffffffff]fffff800, 0x[ffffffff]ffffffff].
        DCHECK(IsInt<12>(imm));
        __ Sltiu(rd, rs1, imm);
      } else {
        __ Sltu(rd, rs1, rs2);
      }
      if (cond == kCondAE) {
        // Calculate `rs1 AE rhs` as `!(rs1 B rhs)` since there's only the SLTU but no SGEU.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondBE:
    case kCondA:
      if (use_imm) {
        // Calculate `rs1 BE imm` as `rs1 B imm + 1`.
        // Sltiu sign-extends its 12-bit immediate operand before the comparison
        // and thus lets us compare directly with unsigned values in the ranges
        // [0, 0x7ff] and [0x[ffffffff]fffff800, 0x[ffffffff]ffffffff].
        DCHECK(IsInt<12>(imm + 1));  // The value that overflows would fail this check.
        __ Sltiu(rd, rs1, imm + 1);
      } else {
        __ Sltu(rd, rs2, rs1);
      }
      if ((cond == kCondA) == use_imm) {
        // Calculate `rs1 A imm` as `!(rs1 B imm + 1)` and calculate
        // `rs1 BE rs2` as `!(rs2 B rs1)` since there's only the SLTU but no SGEU.
        __ Xori(rd, rd, 1);
      }
      break;
  }
}

void InstructionCodeGeneratorRISCV64::GenerateIntLongCompareAndBranch(IfCondition cond,
                                                                      LocationSummary* locations,
                                                                      Riscv64Label* label) {
  XRegister left = locations->InAt(0).AsRegister<XRegister>();
  Location right_location = locations->InAt(1);
  if (right_location.IsConstant()) {
    DCHECK_EQ(CodeGenerator::GetInt64ValueOf(right_location.GetConstant()), 0);
    switch (cond) {
      case kCondEQ:
      case kCondBE:  // <= 0 if zero
        __ Beqz(left, label);
        break;
      case kCondNE:
      case kCondA:  // > 0 if non-zero
        __ Bnez(left, label);
        break;
      case kCondLT:
        __ Bltz(left, label);
        break;
      case kCondGE:
        __ Bgez(left, label);
        break;
      case kCondLE:
        __ Blez(left, label);
        break;
      case kCondGT:
        __ Bgtz(left, label);
        break;
      case kCondB:  // always false
        break;
      case kCondAE:  // always true
        __ J(label);
        break;
    }
  } else {
    XRegister right_reg = right_location.AsRegister<XRegister>();
    switch (cond) {
      case kCondEQ:
        __ Beq(left, right_reg, label);
        break;
      case kCondNE:
        __ Bne(left, right_reg, label);
        break;
      case kCondLT:
        __ Blt(left, right_reg, label);
        break;
      case kCondGE:
        __ Bge(left, right_reg, label);
        break;
      case kCondLE:
        __ Ble(left, right_reg, label);
        break;
      case kCondGT:
        __ Bgt(left, right_reg, label);
        break;
      case kCondB:
        __ Bltu(left, right_reg, label);
        break;
      case kCondAE:
        __ Bgeu(left, right_reg, label);
        break;
      case kCondBE:
        __ Bleu(left, right_reg, label);
        break;
      case kCondA:
        __ Bgtu(left, right_reg, label);
        break;
    }
  }
}

void InstructionCodeGeneratorRISCV64::GenerateFpCondition(IfCondition cond,
                                                          bool gt_bias,
                                                          DataType::Type type,
                                                          LocationSummary* locations,
                                                          Riscv64Label* label) {
  // RISCV-V FP compare instructions yield the following values:
  //                      l<r  l=r  l>r Unordered
  //             FEQ l,r   0    1    0    0
  //             FLT l,r   1    0    0    0
  //             FLT r,l   0    0    1    0
  //             FLE l,r   1    1    0    0
  //             FLE r,l   0    1    1    0
  //
  // We can calculate the `Compare` results using the following formulas:
  //                      l<r  l=r  l>r Unordered
  //     Compare/gt_bias  -1    0    1    1       = ((FLE l,r) ^ 1) - (FLT l,r)
  //     Compare/lt_bias  -1    0    1   -1       = ((FLE r,l) - 1) + (FLT r,l)
  // These are emitted in `VisitCompare()`.
  //
  // This function emits a fused `Condition(Compare(., .), 0)`. If we compare the
  // `Compare` results above with 0, we get the following values and formulas:
  //                      l<r  l=r  l>r Unordered
  //     CondEQ/-          0    1    0    0       = (FEQ l, r)
  //     CondNE/-          1    0    1    1       = (FEQ l, r) ^ 1
  //     CondLT/gt_bias    1    0    0    0       = (FLT l,r)
  //     CondLT/lt_bias    1    0    0    1       = (FLE r,l) ^ 1
  //     CondLE/gt_bias    1    1    0    0       = (FLE l,r)
  //     CondLE/lt_bias    1    1    0    1       = (FLT r,l) ^ 1
  //     CondGT/gt_bias    0    0    1    1       = (FLE l,r) ^ 1
  //     CondGT/lt_bias    0    0    1    0       = (FLT r,l)
  //     CondGE/gt_bias    0    1    1    1       = (FLT l,r) ^ 1
  //     CondGE/lt_bias    0    1    1    0       = (FLE r,l)
  // (CondEQ/CondNE comparison with zero yields the same result with gt_bias and lt_bias.)
  //
  // If the condition is not materialized, the `^ 1` is not emitted,
  // instead the condition is reversed by emitting BEQZ instead of BNEZ.

  FRegister rs1 = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister rs2 = locations->InAt(1).AsFpuRegister<FRegister>();

  DCHECK_EQ(label != nullptr, locations->Out().IsInvalid());
  ScratchRegisterScope srs(GetAssembler());
  XRegister rd =
      (label != nullptr) ? srs.AllocateXRegister() : locations->Out().AsRegister<XRegister>();
  bool reverse_condition = false;

  switch (cond) {
    case kCondEQ:
      FEq(rd, rs1, rs2, type);
      break;
    case kCondNE:
      FEq(rd, rs1, rs2, type);
      reverse_condition = true;
      break;
    case kCondLT:
      if (gt_bias) {
        FLt(rd, rs1, rs2, type);
      } else {
        FLe(rd, rs2, rs1, type);
        reverse_condition = true;
      }
      break;
    case kCondLE:
      if (gt_bias) {
        FLe(rd, rs1, rs2, type);
      } else {
        FLt(rd, rs2, rs1, type);
        reverse_condition = true;
      }
      break;
    case kCondGT:
      if (gt_bias) {
        FLe(rd, rs1, rs2, type);
        reverse_condition = true;
      } else {
        FLt(rd, rs2, rs1, type);
      }
      break;
    case kCondGE:
      if (gt_bias) {
        FLt(rd, rs1, rs2, type);
        reverse_condition = true;
      } else {
        FLe(rd, rs2, rs1, type);
      }
      break;
    default:
      LOG(FATAL) << "Unexpected floating-point condition " << cond;
      UNREACHABLE();
  }

  if (label != nullptr) {
    if (reverse_condition) {
      __ Beqz(rd, label);
    } else {
      __ Bnez(rd, label);
    }
  } else {
    if (reverse_condition) {
      __ Xori(rd, rd, 1);
    }
  }
}

void InstructionCodeGeneratorRISCV64::HandleGoto(HInstruction* instruction,
                                                 HBasicBlock* successor) {
  if (successor->IsExitBlock()) {
    DCHECK(instruction->GetPrevious()->AlwaysThrows());
    return;  // no code needed
  }

  HBasicBlock* block = instruction->GetBlock();
  HInstruction* previous = instruction->GetPrevious();
  HLoopInformation* info = block->GetLoopInformation();

  if (info != nullptr && info->IsBackEdge(*block) && info->HasSuspendCheck()) {
    codegen_->MaybeIncrementHotness(/*is_frame_entry=*/ false);
    GenerateSuspendCheck(info->GetSuspendCheck(), successor);
    return;  // `GenerateSuspendCheck()` emitted the jump.
  }
  if (block->IsEntryBlock() && previous != nullptr && previous->IsSuspendCheck()) {
    GenerateSuspendCheck(previous->AsSuspendCheck(), nullptr);
  }
  if (!codegen_->GoesToNextBlock(block, successor)) {
    __ J(codegen_->GetLabelOf(successor));
  }
}

void InstructionCodeGeneratorRISCV64::GenPackedSwitchWithCompares(XRegister reg,
                                                                  int32_t lower_bound,
                                                                  uint32_t num_entries,
                                                                  HBasicBlock* switch_block,
                                                                  HBasicBlock* default_block) {
  UNUSED(reg);
  UNUSED(lower_bound);
  UNUSED(num_entries);
  UNUSED(switch_block);
  UNUSED(default_block);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::GenTableBasedPackedSwitch(XRegister reg,
                                                                int32_t lower_bound,
                                                                uint32_t num_entries,
                                                                HBasicBlock* switch_block,
                                                                HBasicBlock* default_block) {
  UNUSED(reg);
  UNUSED(lower_bound);
  UNUSED(num_entries);
  UNUSED(switch_block);
  UNUSED(default_block);
  LOG(FATAL) << "Unimplemented";
}

int32_t InstructionCodeGeneratorRISCV64::VecAddress(LocationSummary* locations,
                                                    size_t size,
                                                    /*out*/ XRegister* adjusted_base) {
  UNUSED(locations);
  UNUSED(size);
  UNUSED(adjusted_base);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void InstructionCodeGeneratorRISCV64::GenConditionalMove(HSelect* select) {
  UNUSED(select);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::HandleBinaryOp(HBinaryOperation* instruction) {
  DCHECK_EQ(instruction->InputCount(), 2u);
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type type = instruction->GetResultType();
  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      locations->SetInAt(0, Location::RequiresRegister());
      HInstruction* right = instruction->InputAt(1);
      bool can_use_imm = false;
      if (right->IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(right->AsConstant());
        can_use_imm = IsInt<12>(instruction->IsSub() ? -imm : imm);
      }
      if (can_use_imm) {
        locations->SetInAt(1, Location::ConstantLocation(right->AsConstant()));
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
      }
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;

    default:
      LOG(FATAL) << "Unexpected " << instruction->DebugName() << " type " << type;
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorRISCV64::HandleBinaryOp(HBinaryOperation* instruction) {
  DataType::Type type = instruction->GetType();
  LocationSummary* locations = instruction->GetLocations();

  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      XRegister rd = locations->Out().AsRegister<XRegister>();
      XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
      Location rs2_location = locations->InAt(1);

      bool use_imm = rs2_location.IsConstant();
      XRegister rs2 = use_imm ? kNoXRegister : rs2_location.AsRegister<XRegister>();
      int64_t imm = use_imm ? CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant()) : 0;

      if (instruction->IsAnd()) {
        if (use_imm) {
          __ Andi(rd, rs1, imm);
        } else {
          __ And(rd, rs1, rs2);
        }
      } else if (instruction->IsOr()) {
        if (use_imm) {
          __ Ori(rd, rs1, imm);
        } else {
          __ Or(rd, rs1, rs2);
        }
      } else if (instruction->IsXor()) {
        if (use_imm) {
          __ Xori(rd, rs1, imm);
        } else {
          __ Xor(rd, rs1, rs2);
        }
      } else {
        DCHECK(instruction->IsAdd() || instruction->IsSub());
        if (type == DataType::Type::kInt32) {
          if (use_imm) {
            __ Addiw(rd, rs1, instruction->IsSub() ? -imm : imm);
          } else if (instruction->IsAdd()) {
            __ Addw(rd, rs1, rs2);
          } else {
            DCHECK(instruction->IsSub());
            __ Subw(rd, rs1, rs2);
          }
        } else {
          if (use_imm) {
            __ Addi(rd, rs1, instruction->IsSub() ? -imm : imm);
          } else if (instruction->IsAdd()) {
            __ Add(rd, rs1, rs2);
          } else {
            DCHECK(instruction->IsSub());
            __ Sub(rd, rs1, rs2);
          }
        }
      }
      break;
    }
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64: {
      FRegister rd = locations->Out().AsFpuRegister<FRegister>();
      FRegister rs1 = locations->InAt(0).AsFpuRegister<FRegister>();
      FRegister rs2 = locations->InAt(1).AsFpuRegister<FRegister>();
      if (instruction->IsAdd()) {
        FAdd(rd, rs1, rs2, type);
      } else {
        DCHECK(instruction->IsSub());
        FSub(rd, rs1, rs2, type);
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected binary operation type " << type;
      UNREACHABLE();
  }
}

void LocationsBuilderRISCV64::HandleCondition(HCondition* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  switch (instruction->InputAt(0)->GetType()) {
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      break;

    default: {
      locations->SetInAt(0, Location::RequiresRegister());
      HInstruction* rhs = instruction->InputAt(1);
      bool use_imm = false;
      if (rhs->IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(rhs->AsConstant());
        if (instruction->IsEmittedAtUseSite()) {
          // For `HIf`, materialize all non-zero constants with an `HParallelMove`.
          // Note: For certain constants and conditions, the code could be improved.
          // For example, 2048 takes two instructions to materialize but the negative
          // -2048 could be embedded in ADDI for EQ/NE comparison.
          use_imm = (imm == 0);
        } else {
          // Constants that cannot be embedded in an instruction's 12-bit immediate shall be
          // materialized with an `HParallelMove`. This simplifies the code and avoids cases
          // with arithmetic overflow. Adjust the `imm` if needed for a particular instruction.
          switch (instruction->GetCondition()) {
            case kCondEQ:
            case kCondNE:
              imm = -imm;  // ADDI with negative immediate (there is no SUBI).
              break;
            case kCondLE:
            case kCondGT:
            case kCondBE:
            case kCondA:
              imm += 1;    // SLTI/SLTIU with adjusted immediate (there is no SLEI/SLEIU).
              break;
            default:
              break;
          }
          use_imm = IsInt<12>(imm);
        }
      }
      if (use_imm) {
        locations->SetInAt(1, Location::ConstantLocation(rhs->AsConstant()));
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
      }
      break;
    }
  }
  if (!instruction->IsEmittedAtUseSite()) {
    locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
  }
}

void InstructionCodeGeneratorRISCV64::HandleCondition(HCondition* instruction) {
  if (instruction->IsEmittedAtUseSite()) {
    return;
  }

  DataType::Type type = instruction->InputAt(0)->GetType();
  LocationSummary* locations = instruction->GetLocations();
  switch (type) {
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      GenerateFpCondition(instruction->GetCondition(), instruction->IsGtBias(), type, locations);
      return;
    default:
      // Integral types and reference equality.
      GenerateIntLongCondition(instruction->GetCondition(), locations);
      return;
  }
}

void LocationsBuilderRISCV64::HandleShift(HBinaryOperation* instruction) {
  DCHECK(instruction->IsShl() ||
         instruction->IsShr() ||
         instruction->IsUShr() ||
         instruction->IsRor());

  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type type = instruction->GetResultType();
  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected shift type " << type;
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorRISCV64::HandleShift(HBinaryOperation* instruction) {
  DCHECK(instruction->IsShl() ||
         instruction->IsShr() ||
         instruction->IsUShr() ||
         instruction->IsRor());
  LocationSummary* locations = instruction->GetLocations();
  DataType::Type type = instruction->GetType();

  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      XRegister rd = locations->Out().AsRegister<XRegister>();
      XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
      Location rs2_location = locations->InAt(1);

      if (rs2_location.IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant());
        uint32_t shamt =
            imm & (type == DataType::Type::kInt32 ? kMaxIntShiftDistance : kMaxLongShiftDistance);

        if (shamt == 0) {
          if (rd != rs1) {
            __ Mv(rd, rs1);
          }
        } else if (type == DataType::Type::kInt32) {
          if (instruction->IsShl()) {
            __ Slliw(rd, rs1, shamt);
          } else if (instruction->IsShr()) {
            __ Sraiw(rd, rs1, shamt);
          } else if (instruction->IsUShr()) {
            __ Srliw(rd, rs1, shamt);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            __ Srliw(tmp, rs1, shamt);
            __ Slliw(rd, rs1, 32 - shamt);
            __ Or(rd, rd, tmp);
          }
        } else {
          if (instruction->IsShl()) {
            __ Slli(rd, rs1, shamt);
          } else if (instruction->IsShr()) {
            __ Srai(rd, rs1, shamt);
          } else if (instruction->IsUShr()) {
            __ Srli(rd, rs1, shamt);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            __ Srli(tmp, rs1, shamt);
            __ Slli(rd, rs1, 64 - shamt);
            __ Or(rd, rd, tmp);
          }
        }
      } else {
        XRegister rs2 = rs2_location.AsRegister<XRegister>();
        if (type == DataType::Type::kInt32) {
          if (instruction->IsShl()) {
            __ Sllw(rd, rs1, rs2);
          } else if (instruction->IsShr()) {
            __ Sraw(rd, rs1, rs2);
          } else if (instruction->IsUShr()) {
            __ Srlw(rd, rs1, rs2);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            XRegister tmp2 = srs.AllocateXRegister();
            __ Srlw(tmp, rs1, rs2);
            __ Sub(tmp2, Zero, rs2);  // tmp2 = -rs; we can use this instead of `32 - rs`
            __ Sllw(rd, rs1, tmp2);   // because only low 5 bits are used for SLLW.
            __ Or(rd, rd, tmp);
          }
        } else {
          if (instruction->IsShl()) {
            __ Sll(rd, rs1, rs2);
          } else if (instruction->IsShr()) {
            __ Sra(rd, rs1, rs2);
          } else if (instruction->IsUShr()) {
            __ Srl(rd, rs1, rs2);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            XRegister tmp2 = srs.AllocateXRegister();
            __ Srl(tmp, rs1, rs2);
            __ Sub(tmp2, Zero, rs2);  // tmp2 = -rs; we can use this instead of `64 - rs`
            __ Sll(rd, rs1, tmp2);    // because only low 6 bits are used for SLL.
            __ Or(rd, rd, tmp);
          }
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected shift operation type " << type;
  }
}

void LocationsBuilderRISCV64::HandleFieldSet(HInstruction* instruction,
                                             const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::HandleFieldSet(HInstruction* instruction,
                                                     const FieldInfo& field_info,
                                                     bool value_can_be_null) {
  UNUSED(instruction);
  UNUSED(field_info);
  UNUSED(value_can_be_null);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::HandleFieldGet(HInstruction* instruction,
                                             const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::HandleFieldGet(HInstruction* instruction,
                                                     const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitAbove(HAbove* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitAbove(HAbove* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitAbs(HAbs* abs) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(abs);
  switch (abs->GetResultType()) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unexpected abs type " << abs->GetResultType();
  }
}

void InstructionCodeGeneratorRISCV64::VisitAbs(HAbs* abs) {
  LocationSummary* locations = abs->GetLocations();
  switch (abs->GetResultType()) {
    case DataType::Type::kInt32: {
      XRegister in = locations->InAt(0).AsRegister<XRegister>();
      XRegister out = locations->Out().AsRegister<XRegister>();
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ Sraiw(tmp, in, 31);
      __ Xor(out, in, tmp);
      __ Subw(out, out, tmp);
      break;
    }
    case DataType::Type::kInt64: {
      XRegister in = locations->InAt(0).AsRegister<XRegister>();
      XRegister out = locations->Out().AsRegister<XRegister>();
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ Srai(tmp, in, 63);
      __ Xor(out, in, tmp);
      __ Sub(out, out, tmp);
      break;
    }
    case DataType::Type::kFloat32: {
      FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
      FRegister out = locations->Out().AsFpuRegister<FRegister>();
      __ FAbsS(out, in);
      break;
    }
    case DataType::Type::kFloat64: {
      FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
      FRegister out = locations->Out().AsFpuRegister<FRegister>();
      __ FAbsD(out, in);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected abs type " << abs->GetResultType();
  }
}

void LocationsBuilderRISCV64::VisitAdd(HAdd* instruction) {
  HandleBinaryOp(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitAdd(HAdd* instruction) {
  HandleBinaryOp(instruction);
}

void LocationsBuilderRISCV64::VisitAnd(HAnd* instruction) {
  HandleBinaryOp(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitAnd(HAnd* instruction) {
  HandleBinaryOp(instruction);
}

void LocationsBuilderRISCV64::VisitArrayGet(HArrayGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitArrayGet(HArrayGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitArrayLength(HArrayLength* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitArrayLength(HArrayLength* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitArraySet(HArraySet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitArraySet(HArraySet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitBelow(HBelow* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitBelow(HBelow* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitBooleanNot(HBooleanNot* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorRISCV64::VisitBooleanNot(HBooleanNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  __ Xori(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>(), 1);
}

void LocationsBuilderRISCV64::VisitBoundsCheck(HBoundsCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitBoundsCheck(HBoundsCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitBoundType(HBoundType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitBoundType(HBoundType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitCheckCast(HCheckCast* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitCheckCast(HCheckCast* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitClassTableGet(HClassTableGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitClassTableGet(HClassTableGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

static int32_t GetExceptionTlsOffset() {
  return Thread::ExceptionOffset<kRiscv64PointerSize>().Int32Value();
}

void LocationsBuilderRISCV64::VisitClearException(HClearException* instruction) {
  new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
}

void InstructionCodeGeneratorRISCV64::VisitClearException(
    [[maybe_unused]] HClearException* instruction) {
  __ Stored(Zero, TR, GetExceptionTlsOffset());
}

void LocationsBuilderRISCV64::VisitClinitCheck(HClinitCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitClinitCheck(HClinitCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitCompare(HCompare* instruction) {
  DataType::Type in_type = instruction->InputAt(0)->GetType();

  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);

  switch (in_type) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, RegisterOrZeroBitPatternLocation(instruction->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;

    default:
      LOG(FATAL) << "Unexpected type for compare operation " << in_type;
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorRISCV64::VisitCompare(HCompare* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  XRegister result = locations->Out().AsRegister<XRegister>();
  DataType::Type in_type = instruction->InputAt(0)->GetType();

  //  0 if: left == right
  //  1 if: left  > right
  // -1 if: left  < right
  switch (in_type) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      XRegister left = locations->InAt(0).AsRegister<XRegister>();
      XRegister right = InputXRegisterOrZero(locations->InAt(1));
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ Slt(tmp, left, right);
      __ Slt(result, right, left);
      __ Sub(result, result, tmp);
      break;
    }

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64: {
      FRegister left = locations->InAt(0).AsFpuRegister<FRegister>();
      FRegister right = locations->InAt(1).AsFpuRegister<FRegister>();
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      if (instruction->IsGtBias()) {
        // ((FLE l,r) ^ 1) - (FLT l,r); see `GenerateFpCondition()`.
        FLe(tmp, left, right, in_type);
        FLt(result, left, right, in_type);
        __ Xori(tmp, tmp, 1);
        __ Sub(result, tmp, result);
      } else {
        // ((FLE r,l) - 1) + (FLT r,l); see `GenerateFpCondition()`.
        FLe(tmp, right, left, in_type);
        FLt(result, right, left, in_type);
        __ Addi(tmp, tmp, -1);
        __ Add(result, result, tmp);
      }
      break;
    }

    default:
      LOG(FATAL) << "Unimplemented compare type " << in_type;
  }
}

void LocationsBuilderRISCV64::VisitConstructorFence(HConstructorFence* instruction) {
  instruction->SetLocations(nullptr);
}
void InstructionCodeGeneratorRISCV64::VisitConstructorFence(
    [[maybe_unused]] HConstructorFence* instruction) {
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
}

void LocationsBuilderRISCV64::VisitCurrentMethod(HCurrentMethod* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitCurrentMethod(HCurrentMethod* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitShouldDeoptimizeFlag(HShouldDeoptimizeFlag* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorRISCV64::VisitShouldDeoptimizeFlag(
    HShouldDeoptimizeFlag* instruction) {
  __ Loadw(instruction->GetLocations()->Out().AsRegister<XRegister>(),
           SP,
           codegen_->GetStackOffsetOfShouldDeoptimizeFlag());
}

void LocationsBuilderRISCV64::VisitDeoptimize(HDeoptimize* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitDeoptimize(HDeoptimize* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitDiv(HDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitDiv(HDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitDoubleConstant(HDoubleConstant* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorRISCV64::VisitDoubleConstant(
    [[maybe_unused]] HDoubleConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderRISCV64::VisitEqual(HEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitEqual(HEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitExit(HExit* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorRISCV64::VisitExit([[maybe_unused]] HExit* instruction) {}

void LocationsBuilderRISCV64::VisitFloatConstant(HFloatConstant* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorRISCV64::VisitFloatConstant(
    [[maybe_unused]] HFloatConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderRISCV64::VisitGoto(HGoto* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorRISCV64::VisitGoto(HGoto* instruction) {
  HandleGoto(instruction, instruction->GetSuccessor());
}

void LocationsBuilderRISCV64::VisitGreaterThan(HGreaterThan* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitGreaterThan(HGreaterThan* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitIf(HIf* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  if (IsBooleanValueOrMaterializedCondition(instruction->InputAt(0))) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorRISCV64::VisitIf(HIf* instruction) {
  HBasicBlock* true_successor = instruction->IfTrueSuccessor();
  HBasicBlock* false_successor = instruction->IfFalseSuccessor();
  Riscv64Label* true_target = codegen_->GoesToNextBlock(instruction->GetBlock(), true_successor)
      ? nullptr
      : codegen_->GetLabelOf(true_successor);
  Riscv64Label* false_target = codegen_->GoesToNextBlock(instruction->GetBlock(), false_successor)
      ? nullptr
      : codegen_->GetLabelOf(false_successor);
  GenerateTestAndBranch(instruction, /* condition_input_index= */ 0, true_target, false_target);
}

void LocationsBuilderRISCV64::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitPredicatedInstanceFieldGet(
    HPredicatedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitPredicatedInstanceFieldGet(
    HPredicatedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInstanceOf(HInstanceOf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInstanceOf(HInstanceOf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitIntConstant(HIntConstant* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetOut(Location::ConstantLocation(instruction));
}
void InstructionCodeGeneratorRISCV64::VisitIntConstant([[maybe_unused]] HIntConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderRISCV64::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokeUnresolved(HInvokeUnresolved* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokeUnresolved(HInvokeUnresolved* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokeInterface(HInvokeInterface* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokeInterface(HInvokeInterface* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokeStaticOrDirect(
    HInvokeStaticOrDirect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokeVirtual(HInvokeVirtual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokeVirtual(HInvokeVirtual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokePolymorphic(HInvokePolymorphic* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokePolymorphic(HInvokePolymorphic* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitInvokeCustom(HInvokeCustom* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitInvokeCustom(HInvokeCustom* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLessThan(HLessThan* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitLessThan(HLessThan* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitLoadClass(HLoadClass* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLoadClass(HLoadClass* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLoadException(HLoadException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLoadException(HLoadException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLoadMethodHandle(HLoadMethodHandle* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLoadMethodHandle(HLoadMethodHandle* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLoadMethodType(HLoadMethodType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLoadMethodType(HLoadMethodType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLoadString(HLoadString* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLoadString(HLoadString* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitLongConstant(HLongConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitLongConstant(HLongConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMax(HMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMax(HMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMemoryBarrier(HMemoryBarrier* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMemoryBarrier(HMemoryBarrier* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMethodEntryHook(HMethodEntryHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMethodEntryHook(HMethodEntryHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMethodExitHook(HMethodExitHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMethodExitHook(HMethodExitHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMin(HMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMin(HMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMonitorOperation(HMonitorOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMonitorOperation(HMonitorOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitMul(HMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitMul(HMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNeg(HNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNeg(HNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNewArray(HNewArray* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNewArray(HNewArray* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNewInstance(HNewInstance* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNewInstance(HNewInstance* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNop(HNop* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNop(HNop* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNot(HNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNot(HNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNotEqual(HNotEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitNotEqual(HNotEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderRISCV64::VisitNullConstant(HNullConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNullConstant(HNullConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitOr(HOr* instruction) {
  HandleBinaryOp(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitOr(HOr* instruction) {
  HandleBinaryOp(instruction);
}

void LocationsBuilderRISCV64::VisitPackedSwitch(HPackedSwitch* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitPackedSwitch(HPackedSwitch* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitParallelMove(HParallelMove* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitParallelMove(HParallelMove* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitParameterValue(HParameterValue* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  Location location = parameter_visitor_.GetNextLocation(instruction->GetType());
  if (location.IsStackSlot()) {
    location = Location::StackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  } else if (location.IsDoubleStackSlot()) {
    location = Location::DoubleStackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  }
  locations->SetOut(location);
}

void InstructionCodeGeneratorRISCV64::VisitParameterValue(
    [[maybe_unused]] HParameterValue* instruction) {
  // Nothing to do, the parameter is already at its location.
}

void LocationsBuilderRISCV64::VisitPhi(HPhi* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  for (size_t i = 0, e = locations->GetInputCount(); i < e; ++i) {
    locations->SetInAt(i, Location::Any());
  }
  locations->SetOut(Location::Any());
}

void InstructionCodeGeneratorRISCV64::VisitPhi([[maybe_unused]] HPhi* instruction) {
  LOG(FATAL) << "Unreachable";
}

void LocationsBuilderRISCV64::VisitRem(HRem* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitRem(HRem* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitReturn(HReturn* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type return_type = instruction->InputAt(0)->GetType();
  DCHECK_NE(return_type, DataType::Type::kVoid);
  locations->SetInAt(0, Riscv64ReturnLocation(return_type));
}

void InstructionCodeGeneratorRISCV64::VisitReturn(HReturn* instruction) {
  if (GetGraph()->IsCompilingOsr()) {
    // To simplify callers of an OSR method, we put a floating point return value
    // in both floating point and core return registers.
    switch (instruction->InputAt(0)->GetType()) {
      case DataType::Type::kFloat32:
        __ FMvXW(A0, FA0);
        break;
      case DataType::Type::kFloat64:
        __ FMvXD(A0, FA0);
        break;
      default:
        break;
    }
  }
  codegen_->GenerateFrameExit();
}

void LocationsBuilderRISCV64::VisitReturnVoid(HReturnVoid* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorRISCV64::VisitReturnVoid([[maybe_unused]] HReturnVoid* instruction) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderRISCV64::VisitRor(HRor* instruction) {
  HandleShift(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitRor(HRor* instruction) {
  HandleShift(instruction);
}

void LocationsBuilderRISCV64::VisitShl(HShl* instruction) {
  HandleShift(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitShl(HShl* instruction) {
  HandleShift(instruction);
}

void LocationsBuilderRISCV64::VisitShr(HShr* instruction) {
  HandleShift(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitShr(HShr* instruction) {
  HandleShift(instruction);
}

void LocationsBuilderRISCV64::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitStringBuilderAppend(HStringBuilderAppend* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitStringBuilderAppend(HStringBuilderAppend* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitSelect(HSelect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitSelect(HSelect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitSub(HSub* instruction) {
  HandleBinaryOp(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitSub(HSub* instruction) {
  HandleBinaryOp(instruction);
}

void LocationsBuilderRISCV64::VisitSuspendCheck(HSuspendCheck* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator())
      LocationSummary(instruction, LocationSummary::kCallOnSlowPath);
  // In suspend check slow path, usually there are no caller-save registers at all.
  // If SIMD instructions are present, however, we force spilling all live SIMD
  // registers in full width (since the runtime only saves/restores lower part).
  locations->SetCustomSlowPathCallerSaves(GetGraph()->HasSIMD() ? RegisterSet::AllFpu() :
                                                                  RegisterSet::Empty());
}

void InstructionCodeGeneratorRISCV64::VisitSuspendCheck(HSuspendCheck* instruction) {
  HBasicBlock* block = instruction->GetBlock();
  if (block->GetLoopInformation() != nullptr) {
    DCHECK(block->GetLoopInformation()->GetSuspendCheck() == instruction);
    // The back edge will generate the suspend check.
    return;
  }
  if (block->IsEntryBlock() && instruction->GetNext()->IsGoto()) {
    // The goto will generate the suspend check.
    return;
  }
  GenerateSuspendCheck(instruction, nullptr);
}

void LocationsBuilderRISCV64::VisitThrow(HThrow* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitThrow(HThrow* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitTryBoundary(HTryBoundary* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitTryBoundary(HTryBoundary* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitTypeConversion(HTypeConversion* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitTypeConversion(HTypeConversion* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitUShr(HUShr* instruction) {
  HandleShift(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitUShr(HUShr* instruction) {
  HandleShift(instruction);
}

void LocationsBuilderRISCV64::VisitXor(HXor* instruction) {
  HandleBinaryOp(instruction);
}

void InstructionCodeGeneratorRISCV64::VisitXor(HXor* instruction) {
  HandleBinaryOp(instruction);
}

void LocationsBuilderRISCV64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecReduce(HVecReduce* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecReduce(HVecReduce* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecCnv(HVecCnv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecCnv(HVecCnv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecNeg(HVecNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecNeg(HVecNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecAbs(HVecAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecAbs(HVecAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecNot(HVecNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecNot(HVecNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecAdd(HVecAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecAdd(HVecAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecSub(HVecSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecSub(HVecSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecMul(HVecMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecMul(HVecMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecDiv(HVecDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecDiv(HVecDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecMin(HVecMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecMin(HVecMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecMax(HVecMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecMax(HVecMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecAnd(HVecAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecAnd(HVecAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecAndNot(HVecAndNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecAndNot(HVecAndNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecOr(HVecOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecOr(HVecOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecXor(HVecXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecXor(HVecXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecShl(HVecShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecShl(HVecShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecShr(HVecShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecShr(HVecShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecUShr(HVecUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecUShr(HVecUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecSetScalars(HVecSetScalars* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecSetScalars(HVecSetScalars* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecMultiplyAccumulate(
    HVecMultiplyAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecDotProd(HVecDotProd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecDotProd(HVecDotProd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecLoad(HVecLoad* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecLoad(HVecLoad* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecStore(HVecStore* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecStore(HVecStore* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecPredWhile(HVecPredWhile* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecPredWhile(HVecPredWhile* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecCondition(HVecCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecCondition(HVecCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderRISCV64::VisitVecPredNot(HVecPredNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorRISCV64::VisitVecPredNot(HVecPredNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

namespace detail {

// Mark which intrinsics we don't have handcrafted code for.
template <Intrinsics T>
struct IsUnimplemented {
  bool is_unimplemented = false;
};

#define TRUE_OVERRIDE(Name, ...)                \
  template <>                                   \
  struct IsUnimplemented<Intrinsics::k##Name> { \
    bool is_unimplemented = true;               \
  };
UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(TRUE_OVERRIDE)
#undef TRUE_OVERRIDE

static constexpr bool kIsIntrinsicUnimplemented[] = {
    false,  // kNone
#define IS_UNIMPLEMENTED(Intrinsic, ...) \
    IsUnimplemented<Intrinsics::k##Intrinsic>().is_unimplemented,
    ART_INTRINSICS_LIST(IS_UNIMPLEMENTED)
#undef IS_UNIMPLEMENTED
};

}  // namespace detail

CodeGeneratorRISCV64::CodeGeneratorRISCV64(HGraph* graph,
                                           const CompilerOptions& compiler_options,
                                           OptimizingCompilerStats* stats)
    : CodeGenerator(graph,
                    kNumberOfXRegisters,
                    kNumberOfFRegisters,
                    /*number_of_register_pairs=*/ 0u,
                    ComputeRegisterMask(kCoreCalleeSaves, arraysize(kCoreCalleeSaves)),
                    ComputeRegisterMask(kFpuCalleeSaves, arraysize(kFpuCalleeSaves)),
                    compiler_options,
                    stats,
                    ArrayRef<const bool>(detail::kIsIntrinsicUnimplemented)),
      assembler_(graph->GetAllocator(),
                 compiler_options.GetInstructionSetFeatures()->AsRiscv64InstructionSetFeatures()),
      location_builder_(graph, this),
      instruction_visitor_(graph, this),
      block_labels_(nullptr) {
  // Always mark the RA register to be saved.
  AddAllocatedRegister(Location::RegisterLocation(RA));
}

void CodeGeneratorRISCV64::MaybeIncrementHotness(bool is_frame_entry) {
  if (GetCompilerOptions().CountHotnessInCompiledCode()) {
    ScratchRegisterScope srs(GetAssembler());
    XRegister method = is_frame_entry ? kArtMethodRegister : srs.AllocateXRegister();
    if (!is_frame_entry) {
      __ Loadd(method, SP, 0);
    }
    XRegister counter = srs.AllocateXRegister();
    __ Loadhu(counter, method, ArtMethod::HotnessCountOffset().Int32Value());
    Riscv64Label done;
    DCHECK_EQ(0u, interpreter::kNterpHotnessValue);
    __ Beqz(counter, &done);  // Can clobber `TMP` if taken.
    __ Addi(counter, counter, -1);
    // We may not have another scratch register available for `Storeh`()`,
    // so we must use the `Sh()` function directly.
    static_assert(IsInt<12>(ArtMethod::HotnessCountOffset().Int32Value()));
    __ Sh(counter, method, ArtMethod::HotnessCountOffset().Int32Value());
    __ Bind(&done);
  }

  if (GetGraph()->IsCompilingBaseline() && !Runtime::Current()->IsAotCompiler()) {
    SlowPathCodeRISCV64* slow_path = new (GetScopedAllocator()) CompileOptimizedSlowPathRISCV64();
    AddSlowPath(slow_path);
    ProfilingInfo* info = GetGraph()->GetProfilingInfo();
    DCHECK(info != nullptr);
    DCHECK(!HasEmptyFrame());
    uint64_t address = reinterpret_cast64<uint64_t>(info);
    ScratchRegisterScope srs(GetAssembler());
    XRegister tmp = srs.AllocateXRegister();
    __ LoadConst64(tmp, address);
    XRegister counter = srs.AllocateXRegister();
    __ Loadhu(counter, tmp, ProfilingInfo::BaselineHotnessCountOffset().Int32Value());
    __ Beqz(counter, slow_path->GetEntryLabel());  // Can clobber `TMP` if taken.
    __ Addi(counter, counter, -1);
    // We do not have another scratch register available for `Storeh`()`,
    // so we must use the `Sh()` function directly.
    static_assert(IsInt<12>(ProfilingInfo::BaselineHotnessCountOffset().Int32Value()));
    __ Sh(counter, tmp, ProfilingInfo::BaselineHotnessCountOffset().Int32Value());
    __ Bind(slow_path->GetExitLabel());
  }
}

bool CodeGeneratorRISCV64::CanUseImplicitSuspendCheck() const {
  // TODO(riscv64): Implement implicit suspend checks to reduce code size.
  return false;
}

void CodeGeneratorRISCV64::GenerateMemoryBarrier(MemBarrierKind kind) {
  switch (kind) {
    case MemBarrierKind::kAnyAny:
    case MemBarrierKind::kAnyStore:
    case MemBarrierKind::kLoadAny:
    case MemBarrierKind::kStoreStore: {
      // TODO(riscv64): Use more specific fences.
      __ Fence();
      break;
    }

    default:
      LOG(FATAL) << "Unexpected memory barrier " << kind;
      UNREACHABLE();
  }
}

void CodeGeneratorRISCV64::GenerateFrameEntry() {
  // Check if we need to generate the clinit check. We will jump to the
  // resolution stub if the class is not initialized and the executing thread is
  // not the thread initializing it.
  // We do this before constructing the frame to get the correct stack trace if
  // an exception is thrown.
  if (GetCompilerOptions().ShouldCompileWithClinitCheck(GetGraph()->GetArtMethod())) {
    Riscv64Label resolution;
    Riscv64Label memory_barrier;

    static constexpr uint32_t kShiftedVisiblyInitializedValue =
        enum_cast<uint32_t>(ClassStatus::kVisiblyInitialized) << status_lsb_position;
    static constexpr uint32_t kShiftedInitializedValue =
        enum_cast<uint32_t>(ClassStatus::kInitialized) << status_lsb_position;
    static constexpr uint32_t kShiftedInitializingValue =
        enum_cast<uint32_t>(ClassStatus::kInitializing) << status_lsb_position;

    // We shall load the full 32-bit status word with sign-extension and compare as unsigned
    // to the sign-extended status values above. This yields the same comparison as loading and
    // materializing unsigned but the constant is materialized with a single LUI instruction.
    auto extend64 = [](uint32_t shifted_status_value) {
      DCHECK_GE(shifted_status_value, 0x80000000u);
      return static_cast<int64_t>(shifted_status_value) - (INT64_C(1) << 32);
    };

    ScratchRegisterScope srs(GetAssembler());
    XRegister tmp = srs.AllocateXRegister();
    XRegister tmp2 = srs.AllocateXRegister();

    // We don't emit a read barrier here to save on code size. We rely on the
    // resolution trampoline to do a clinit check before re-entering this code.
    __ Loadd(tmp2, kArtMethodRegister, ArtMethod::DeclaringClassOffset().Int32Value());
    __ Loadw(tmp, tmp2, mirror::Class::StatusOffset().SizeValue());  // Sign-extended.

    // Check if we're visibly initialized.
    __ Li(tmp2, extend64(kShiftedVisiblyInitializedValue));
    __ Bgeu(tmp, tmp2, &frame_entry_label_);  // Can clobber `TMP` if taken.

    // Check if we're initialized and jump to code that does a memory barrier if so.
    __ Li(tmp2, extend64(kShiftedInitializedValue));
    __ Bgeu(tmp, tmp2, &memory_barrier);  // Can clobber `TMP` if taken.

    // Check if we're initializing and the thread initializing is the one
    // executing the code.
    __ Li(tmp2, extend64(kShiftedInitializingValue));
    __ Bltu(tmp, tmp2, &resolution);  // Can clobber `TMP` if taken.

    __ Loadd(tmp2, kArtMethodRegister, ArtMethod::DeclaringClassOffset().Int32Value());
    __ Loadw(tmp, tmp2, mirror::Class::ClinitThreadIdOffset().Int32Value());
    __ Loadw(tmp2, TR, Thread::TidOffset<kRiscv64PointerSize>().Int32Value());
    __ Beq(tmp, tmp2, &frame_entry_label_);
    __ Bind(&resolution);

    // Jump to the resolution stub.
    ThreadOffset64 entrypoint_offset =
        GetThreadOffset<kRiscv64PointerSize>(kQuickQuickResolutionTrampoline);
    __ Loadd(tmp, TR, entrypoint_offset.Int32Value());
    __ Jr(tmp);

    __ Bind(&memory_barrier);
    GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }
  __ Bind(&frame_entry_label_);

  bool do_overflow_check =
      FrameNeedsStackCheck(GetFrameSize(), InstructionSet::kRiscv64) || !IsLeafMethod();

  if (do_overflow_check) {
    __ Loadw(
        Zero, SP, -static_cast<int32_t>(GetStackOverflowReservedBytes(InstructionSet::kRiscv64)));
    RecordPcInfo(nullptr, 0);
  }

  if (!HasEmptyFrame()) {
    // Make sure the frame size isn't unreasonably large.
    if (GetFrameSize() > GetStackOverflowReservedBytes(InstructionSet::kRiscv64)) {
      LOG(FATAL) << "Stack frame larger than "
                 << GetStackOverflowReservedBytes(InstructionSet::kRiscv64) << " bytes";
    }

    // Spill callee-saved registers.

    uint32_t frame_size = GetFrameSize();

    IncreaseFrame(frame_size);

    uint32_t offset = frame_size;
    for (size_t i = arraysize(kCoreCalleeSaves); i != 0; ) {
      --i;
      XRegister reg = kCoreCalleeSaves[i];
      if (allocated_registers_.ContainsCoreRegister(reg)) {
        offset -= kRiscv64DoublewordSize;
        __ Stored(reg, SP, offset);
        __ cfi().RelOffset(dwarf::Reg::Riscv64Core(reg), offset);
      }
    }

    for (size_t i = arraysize(kFpuCalleeSaves); i != 0; ) {
      --i;
      FRegister reg = kFpuCalleeSaves[i];
      if (allocated_registers_.ContainsFloatingPointRegister(reg)) {
        offset -= kRiscv64DoublewordSize;
        __ FStored(reg, SP, offset);
        __ cfi().RelOffset(dwarf::Reg::Riscv64Fp(reg), offset);
      }
    }

    // Save the current method if we need it. Note that we do not
    // do this in HCurrentMethod, as the instruction might have been removed
    // in the SSA graph.
    if (RequiresCurrentMethod()) {
      __ Stored(kArtMethodRegister, SP, 0);
    }

    if (GetGraph()->HasShouldDeoptimizeFlag()) {
      // Initialize should_deoptimize flag to 0.
      __ Storew(Zero, SP, GetStackOffsetOfShouldDeoptimizeFlag());
    }
  }
  MaybeIncrementHotness(/*is_frame_entry=*/ true);
}

void CodeGeneratorRISCV64::GenerateFrameExit() {
  __ cfi().RememberState();

  if (!HasEmptyFrame()) {
    // Restore callee-saved registers.

    // For better instruction scheduling restore RA before other registers.
    uint32_t offset = GetFrameSize();
    for (size_t i = arraysize(kCoreCalleeSaves); i != 0; ) {
      --i;
      XRegister reg = kCoreCalleeSaves[i];
      if (allocated_registers_.ContainsCoreRegister(reg)) {
        offset -= kRiscv64DoublewordSize;
        __ Loadd(reg, SP, offset);
        __ cfi().Restore(dwarf::Reg::Riscv64Core(reg));
      }
    }

    for (size_t i = arraysize(kFpuCalleeSaves); i != 0; ) {
      --i;
      FRegister reg = kFpuCalleeSaves[i];
      if (allocated_registers_.ContainsFloatingPointRegister(reg)) {
        offset -= kRiscv64DoublewordSize;
        __ FLoadd(reg, SP, offset);
        __ cfi().Restore(dwarf::Reg::Riscv64Fp(reg));
      }
    }

    DecreaseFrame(GetFrameSize());
  }

  __ Jr(RA);

  __ cfi().RestoreState();
  __ cfi().DefCFAOffset(GetFrameSize());
}

void CodeGeneratorRISCV64::Bind(HBasicBlock* block) { __ Bind(GetLabelOf(block)); }

void CodeGeneratorRISCV64::MoveConstant(Location destination, int32_t value) {
  DCHECK(destination.IsRegister());
  __ LoadConst32(destination.AsRegister<XRegister>(), value);
}

void CodeGeneratorRISCV64::MoveLocation(Location destination,
                                        Location source,
                                        DataType::Type dst_type) {
  if (source.Equals(destination)) {
    return;
  }

  // A valid move type can always be inferred from the destination and source locations.
  // When moving from and to a register, the `dst_type` can be used to generate 32-bit instead
  // of 64-bit moves but it's generally OK to use 64-bit moves for 32-bit values in registers.
  bool unspecified_type = (dst_type == DataType::Type::kVoid);
  // TODO(riscv64): Is the destination type known in all cases?
  // TODO(riscv64): Can unspecified `dst_type` move 32-bit GPR to FPR without NaN-boxing?
  CHECK(!unspecified_type);

  if (destination.IsRegister() || destination.IsFpuRegister()) {
    if (unspecified_type) {
      HConstant* src_cst = source.IsConstant() ? source.GetConstant() : nullptr;
      if (source.IsStackSlot() ||
          (src_cst != nullptr &&
           (src_cst->IsIntConstant() || src_cst->IsFloatConstant() || src_cst->IsNullConstant()))) {
        // For stack slots and 32-bit constants, a 32-bit type is appropriate.
        dst_type = destination.IsRegister() ? DataType::Type::kInt32 : DataType::Type::kFloat32;
      } else {
        // If the source is a double stack slot or a 64-bit constant, a 64-bit type
        // is appropriate. Else the source is a register, and since the type has not
        // been specified, we chose a 64-bit type to force a 64-bit move.
        dst_type = destination.IsRegister() ? DataType::Type::kInt64 : DataType::Type::kFloat64;
      }
    }
    DCHECK((destination.IsFpuRegister() && DataType::IsFloatingPointType(dst_type)) ||
           (destination.IsRegister() && !DataType::IsFloatingPointType(dst_type)));

    if (source.IsStackSlot() || source.IsDoubleStackSlot()) {
      // Move to GPR/FPR from stack
      if (DataType::IsFloatingPointType(dst_type)) {
        if (DataType::Is64BitType(dst_type)) {
          __ FLoadd(destination.AsFpuRegister<FRegister>(), SP, source.GetStackIndex());
        } else {
          __ FLoadw(destination.AsFpuRegister<FRegister>(), SP, source.GetStackIndex());
        }
      } else {
        if (DataType::Is64BitType(dst_type)) {
          __ Loadd(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        } else if (dst_type == DataType::Type::kReference) {
          __ Loadwu(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        } else {
          __ Loadw(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        }
      }
    } else if (source.IsConstant()) {
      // Move to GPR/FPR from constant
      // TODO(riscv64): Consider using literals for difficult-to-materialize 64-bit constants.
      int64_t value = GetInt64ValueOf(source.GetConstant()->AsConstant());
      ScratchRegisterScope srs(GetAssembler());
      XRegister gpr = DataType::IsFloatingPointType(dst_type)
          ? srs.AllocateXRegister()
          : destination.AsRegister<XRegister>();
      if (DataType::IsFloatingPointType(dst_type) && value == 0) {
        gpr = Zero;  // Note: The scratch register allocated above shall not be used.
      } else {
        // Note: For `float` we load the sign-extended value here as it can sometimes yield
        // a shorter instruction sequence. The higher 32 bits shall be ignored during the
        // transfer to FP reg and the result shall be correctly NaN-boxed.
        __ LoadConst64(gpr, value);
      }
      if (dst_type == DataType::Type::kFloat32) {
        __ FMvWX(destination.AsFpuRegister<FRegister>(), gpr);
      } else if (dst_type == DataType::Type::kFloat64) {
        __ FMvDX(destination.AsFpuRegister<FRegister>(), gpr);
      }
    } else if (source.IsRegister()) {
      if (destination.IsRegister()) {
        // Move to GPR from GPR
        __ Mv(destination.AsRegister<XRegister>(), source.AsRegister<XRegister>());
      } else {
        DCHECK(destination.IsFpuRegister());
        if (DataType::Is64BitType(dst_type)) {
          __ FMvDX(destination.AsFpuRegister<FRegister>(), source.AsRegister<XRegister>());
        } else {
          __ FMvWX(destination.AsFpuRegister<FRegister>(), source.AsRegister<XRegister>());
        }
      }
    } else if (source.IsFpuRegister()) {
      if (destination.IsFpuRegister()) {
        if (GetGraph()->HasSIMD()) {
          LOG(FATAL) << "Vector extension is unsupported";
          UNREACHABLE();
        } else {
          // Move to FPR from FPR
          if (dst_type == DataType::Type::kFloat32) {
            __ FMvS(destination.AsFpuRegister<FRegister>(), source.AsFpuRegister<FRegister>());
          } else {
            DCHECK_EQ(dst_type, DataType::Type::kFloat64);
            __ FMvD(destination.AsFpuRegister<FRegister>(), source.AsFpuRegister<FRegister>());
          }
        }
      } else {
        DCHECK(destination.IsRegister());
        if (DataType::Is64BitType(dst_type)) {
          __ FMvXD(destination.AsRegister<XRegister>(), source.AsFpuRegister<FRegister>());
        } else {
          __ FMvXW(destination.AsRegister<XRegister>(), source.AsFpuRegister<FRegister>());
        }
      }
    }
  } else if (destination.IsSIMDStackSlot()) {
    LOG(FATAL) << "SIMD is unsupported";
    UNREACHABLE();
  } else {  // The destination is not a register. It must be a stack slot.
    DCHECK(destination.IsStackSlot() || destination.IsDoubleStackSlot());
    if (source.IsRegister() || source.IsFpuRegister()) {
      if (unspecified_type) {
        if (source.IsRegister()) {
          dst_type = destination.IsStackSlot() ? DataType::Type::kInt32 : DataType::Type::kInt64;
        } else {
          dst_type =
              destination.IsStackSlot() ? DataType::Type::kFloat32 : DataType::Type::kFloat64;
        }
      }
      DCHECK((destination.IsDoubleStackSlot() == DataType::Is64BitType(dst_type)) &&
             (source.IsFpuRegister() == DataType::IsFloatingPointType(dst_type)));
      // Move to stack from GPR/FPR
      if (DataType::Is64BitType(dst_type)) {
        if (source.IsRegister()) {
          __ Stored(source.AsRegister<XRegister>(), SP, destination.GetStackIndex());
        } else {
          __ FStored(source.AsFpuRegister<FRegister>(), SP, destination.GetStackIndex());
        }
      } else {
        if (source.IsRegister()) {
          __ Storew(source.AsRegister<XRegister>(), SP, destination.GetStackIndex());
        } else {
          __ FStorew(source.AsFpuRegister<FRegister>(), SP, destination.GetStackIndex());
        }
      }
    } else if (source.IsConstant()) {
      // Move to stack from constant
      int64_t value = GetInt64ValueOf(source.GetConstant());
      ScratchRegisterScope srs(GetAssembler());
      XRegister gpr = (value != 0) ? srs.AllocateXRegister() : Zero;
      if (value != 0) {
        __ LoadConst64(gpr, value);
      }
      if (destination.IsStackSlot()) {
        __ Storew(gpr, SP, destination.GetStackIndex());
      } else {
        DCHECK(destination.IsDoubleStackSlot());
        __ Stored(gpr, SP, destination.GetStackIndex());
      }
    } else {
      DCHECK(source.IsStackSlot() || source.IsDoubleStackSlot());
      DCHECK_EQ(source.IsDoubleStackSlot(), destination.IsDoubleStackSlot());
      // Move to stack from stack
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      if (destination.IsStackSlot()) {
        __ Loadw(tmp, SP, source.GetStackIndex());
        __ Storew(tmp, SP, destination.GetStackIndex());
      } else {
        __ Loadd(tmp, SP, source.GetStackIndex());
        __ Stored(tmp, SP, destination.GetStackIndex());
      }
    }
  }
}

void CodeGeneratorRISCV64::AddLocationAsTemp(Location location, LocationSummary* locations) {
  if (location.IsRegister()) {
    locations->AddTemp(location);
  } else {
    UNIMPLEMENTED(FATAL) << "AddLocationAsTemp not implemented for location " << location;
  }
}

void CodeGeneratorRISCV64::SetupBlockedRegisters() const {
  // ZERO, GP, SP, RA, TP and TR(S1) are reserved and can't be allocated.
  blocked_core_registers_[Zero] = true;
  blocked_core_registers_[GP] = true;
  blocked_core_registers_[SP] = true;
  blocked_core_registers_[RA] = true;
  blocked_core_registers_[TP] = true;
  blocked_core_registers_[TR] = true;  // ART Thread register.

  // TMP(T6), TMP2(T5) and FTMP(FT11) are used as temporary/scratch registers.
  blocked_core_registers_[TMP] = true;
  blocked_core_registers_[TMP2] = true;
  blocked_fpu_registers_[FTMP] = true;

  if (GetGraph()->IsDebuggable()) {
    // Stubs do not save callee-save floating point registers. If the graph
    // is debuggable, we need to deal with these registers differently. For
    // now, just block them.
    for (size_t i = 0; i < arraysize(kFpuCalleeSaves); ++i) {
      blocked_fpu_registers_[kFpuCalleeSaves[i]] = true;
    }
  }
}

size_t CodeGeneratorRISCV64::SaveCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ Stored(XRegister(reg_id), SP, stack_index);
  return kRiscv64DoublewordSize;
}

size_t CodeGeneratorRISCV64::RestoreCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ Loadd(XRegister(reg_id), SP, stack_index);
  return kRiscv64DoublewordSize;
}

size_t CodeGeneratorRISCV64::SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  if (GetGraph()->HasSIMD()) {
    // TODO(riscv64): RISC-V vector extension.
    UNIMPLEMENTED(FATAL) << "Vector extension is unsupported";
    UNREACHABLE();
  }
  __ FStored(FRegister(reg_id), SP, stack_index);
  return kRiscv64FloatRegSizeInBytes;
}

size_t CodeGeneratorRISCV64::RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  if (GetGraph()->HasSIMD()) {
    // TODO(riscv64): RISC-V vector extension.
    UNIMPLEMENTED(FATAL) << "Vector extension is unsupported";
    UNREACHABLE();
  }
  __ FLoadd(FRegister(reg_id), SP, stack_index);
  return kRiscv64FloatRegSizeInBytes;
}

void CodeGeneratorRISCV64::DumpCoreRegister(std::ostream& stream, int reg) const {
  stream << XRegister(reg);
}

void CodeGeneratorRISCV64::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  stream << FRegister(reg);
}

void CodeGeneratorRISCV64::Finalize() {
  // Ensure that we fix up branches and literal loads and emit the literal pool.
  __ FinalizeCode();

  // Adjust native pc offsets in stack maps.
  StackMapStream* stack_map_stream = GetStackMapStream();
  for (size_t i = 0, num = stack_map_stream->GetNumberOfStackMaps(); i != num; ++i) {
    uint32_t old_position = stack_map_stream->GetStackMapNativePcOffset(i);
    uint32_t new_position = __ GetAdjustedPosition(old_position);
    DCHECK_GE(new_position, old_position);
    stack_map_stream->SetStackMapNativePcOffset(i, new_position);
  }

  // Adjust pc offsets for the disassembly information.
  if (disasm_info_ != nullptr) {
    GeneratedCodeInterval* frame_entry_interval = disasm_info_->GetFrameEntryInterval();
    frame_entry_interval->start = __ GetAdjustedPosition(frame_entry_interval->start);
    frame_entry_interval->end = __ GetAdjustedPosition(frame_entry_interval->end);
    for (auto& entry : *disasm_info_->GetInstructionIntervals()) {
      entry.second.start = __ GetAdjustedPosition(entry.second.start);
      entry.second.end = __ GetAdjustedPosition(entry.second.end);
    }
    for (auto& entry : *disasm_info_->GetSlowPathIntervals()) {
      entry.code_interval.start = __ GetAdjustedPosition(entry.code_interval.start);
      entry.code_interval.end = __ GetAdjustedPosition(entry.code_interval.end);
    }
  }

  CodeGenerator::Finalize();
}

// Generate code to invoke a runtime entry point.
void CodeGeneratorRISCV64::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  ValidateInvokeRuntime(entrypoint, instruction, slow_path);

  ThreadOffset64 entrypoint_offset = GetThreadOffset<kRiscv64PointerSize>(entrypoint);

  // TODO(riscv64): Reduce code size for AOT by using shared trampolines for slow path
  // runtime calls across the entire oat file.
  __ Loadd(RA, TR, entrypoint_offset.Int32Value());
  __ Jalr(RA);
  if (EntrypointRequiresStackMap(entrypoint)) {
    RecordPcInfo(instruction, dex_pc, slow_path);
  }
}

// Generate code to invoke a runtime entry point, but do not record
// PC-related information in a stack map.
void CodeGeneratorRISCV64::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                               HInstruction* instruction,
                                                               SlowPathCode* slow_path) {
  ValidateInvokeRuntimeWithoutRecordingPcInfo(instruction, slow_path);
  __ Loadd(RA, TR, entry_point_offset);
  __ Jalr(RA);
}

void CodeGeneratorRISCV64::IncreaseFrame(size_t adjustment) {
  int32_t adjustment32 = dchecked_integral_cast<int32_t>(adjustment);
  __ AddConst64(SP, SP, -adjustment32);
  GetAssembler()->cfi().AdjustCFAOffset(adjustment32);
}

void CodeGeneratorRISCV64::DecreaseFrame(size_t adjustment) {
  int32_t adjustment32 = dchecked_integral_cast<int32_t>(adjustment);
  __ AddConst64(SP, SP, adjustment32);
  GetAssembler()->cfi().AdjustCFAOffset(-adjustment32);
}

void CodeGeneratorRISCV64::GenerateNop() {
  __ Nop();
}

void CodeGeneratorRISCV64::GenerateImplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}
void CodeGeneratorRISCV64::GenerateExplicitNullCheck(HNullCheck* instruction) {
  SlowPathCodeRISCV64* slow_path = new (GetScopedAllocator()) NullCheckSlowPathRISCV64(instruction);
  AddSlowPath(slow_path);

  Location obj = instruction->GetLocations()->InAt(0);

  __ Beqz(obj.AsRegister<XRegister>(), slow_path->GetEntryLabel());
}

HLoadString::LoadKind CodeGeneratorRISCV64::GetSupportedLoadStringKind(
    HLoadString::LoadKind desired_string_load_kind) {
  switch (desired_string_load_kind) {
    case HLoadString::LoadKind::kBootImageLinkTimePcRelative:
    case HLoadString::LoadKind::kBootImageRelRo:
    case HLoadString::LoadKind::kBssEntry:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kJitBootImageAddress:
    case HLoadString::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kRuntimeCall:
      break;
  }
  return desired_string_load_kind;
}

HLoadClass::LoadKind CodeGeneratorRISCV64::GetSupportedLoadClassKind(
    HLoadClass::LoadKind desired_class_load_kind) {
  switch (desired_class_load_kind) {
    case HLoadClass::LoadKind::kInvalid:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    case HLoadClass::LoadKind::kReferrersClass:
      break;
    case HLoadClass::LoadKind::kBootImageLinkTimePcRelative:
    case HLoadClass::LoadKind::kBootImageRelRo:
    case HLoadClass::LoadKind::kBssEntry:
    case HLoadClass::LoadKind::kBssEntryPublic:
    case HLoadClass::LoadKind::kBssEntryPackage:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kJitBootImageAddress:
    case HLoadClass::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kRuntimeCall:
      break;
  }
  return desired_class_load_kind;
}

HInvokeStaticOrDirect::DispatchInfo CodeGeneratorRISCV64::GetSupportedInvokeStaticOrDirectDispatch(
    const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) {
  UNUSED(method);
  // On RISCV64 we support all dispatch types.
  return desired_dispatch_info;
}

void CodeGeneratorRISCV64::LoadMethod(MethodLoadKind load_kind, Location temp, HInvoke* invoke) {
  UNUSED(load_kind);
  UNUSED(temp);
  UNUSED(invoke);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                                      Location temp,
                                                      SlowPathCode* slow_path) {
  UNUSED(temp);
  UNUSED(invoke);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::MaybeGenerateInlineCacheCheck(HInstruction* instruction,
                                                         XRegister klass) {
  // We know the destination of an intrinsic, so no need to record inline caches.
  if (!instruction->GetLocations()->Intrinsified() &&
      GetGraph()->IsCompilingBaseline() &&
      !Runtime::Current()->IsAotCompiler()) {
    DCHECK(!instruction->GetEnvironment()->IsFromInlinedInvoke());
    ProfilingInfo* info = GetGraph()->GetProfilingInfo();
    DCHECK(info != nullptr);
    InlineCache* cache = info->GetInlineCache(instruction->GetDexPc());
    uint64_t address = reinterpret_cast64<uint64_t>(cache);
    Riscv64Label done;
    {
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ LoadConst64(tmp, address);
      __ Loadd(tmp, tmp, InlineCache::ClassesOffset().Int32Value());
      // Fast path for a monomorphic cache.
      __ Beq(klass, tmp, &done);
    }
    InvokeRuntime(kQuickUpdateInlineCache, instruction, instruction->GetDexPc());
    __ Bind(&done);
  }
}

void CodeGeneratorRISCV64::GenerateVirtualCall(HInvokeVirtual* invoke,
                                               Location temp_location,
                                               SlowPathCode* slow_path) {
  // Use the calling convention instead of the location of the receiver, as
  // intrinsics may have put the receiver in a different register. In the intrinsics
  // slow path, the arguments have been moved to the right place, so here we are
  // guaranteed that the receiver is the first register of the calling convention.
  InvokeDexCallingConvention calling_convention;
  XRegister receiver = calling_convention.GetRegisterAt(0);
  XRegister temp = temp_location.AsRegister<XRegister>();
  MemberOffset method_offset =
      mirror::Class::EmbeddedVTableEntryOffset(invoke->GetVTableIndex(), kRiscv64PointerSize);
  MemberOffset class_offset = mirror::Object::ClassOffset();
  Offset entry_point = ArtMethod::EntryPointFromQuickCompiledCodeOffset(kRiscv64PointerSize);

  // temp = object->GetClass();
  __ Loadwu(temp, receiver, class_offset.Int32Value());
  MaybeRecordImplicitNullCheck(invoke);
  // Instead of simply (possibly) unpoisoning `temp` here, we should
  // emit a read barrier for the previous class reference load.
  // However this is not required in practice, as this is an
  // intermediate/temporary reference and because the current
  // concurrent copying collector keeps the from-space memory
  // intact/accessible until the end of the marking phase (the
  // concurrent copying collector may not in the future).
  MaybeUnpoisonHeapReference(temp);

  // If we're compiling baseline, update the inline cache.
  MaybeGenerateInlineCacheCheck(invoke, temp);

  // temp = temp->GetMethodAt(method_offset);
  __ Loadd(temp, temp, method_offset.Int32Value());
  // RA = temp->GetEntryPoint();
  __ Loadd(RA, temp, entry_point.Int32Value());
  // RA();
  __ Jalr(RA);
  RecordPcInfo(invoke, invoke->GetDexPc(), slow_path);
}

void CodeGeneratorRISCV64::MoveFromReturnRegister(Location trg, DataType::Type type) {
  if (!trg.IsValid()) {
    DCHECK_EQ(type, DataType::Type::kVoid);
    return;
  }

  DCHECK_NE(type, DataType::Type::kVoid);

  if (DataType::IsIntegralType(type) || type == DataType::Type::kReference) {
    XRegister trg_reg = trg.AsRegister<XRegister>();
    XRegister res_reg = Riscv64ReturnLocation(type).AsRegister<XRegister>();
    if (trg_reg != res_reg) {
      __ Mv(trg_reg, res_reg);
    }
  } else {
    FRegister trg_reg = trg.AsFpuRegister<FRegister>();
    FRegister res_reg = Riscv64ReturnLocation(type).AsFpuRegister<FRegister>();
    if (trg_reg != res_reg) {
      __ FMvD(trg_reg, res_reg);  // 64-bit move is OK also for `float`.
    }
  }
}

void CodeGeneratorRISCV64::PoisonHeapReference(XRegister reg) {
  __ Sub(reg, Zero, reg);  // Negate the ref.
  __ ZextW(reg, reg);      // Zero-extend the 32-bit ref.
}

void CodeGeneratorRISCV64::UnpoisonHeapReference(XRegister reg) {
  __ Sub(reg, Zero, reg);  // Negate the ref.
  __ ZextW(reg, reg);      // Zero-extend the 32-bit ref.
}

inline void CodeGeneratorRISCV64::MaybePoisonHeapReference(XRegister reg) {
  if (kPoisonHeapReferences) {
    PoisonHeapReference(reg);
  }
}

inline void CodeGeneratorRISCV64::MaybeUnpoisonHeapReference(XRegister reg) {
  if (kPoisonHeapReferences) {
    UnpoisonHeapReference(reg);
  }
}

}  // namespace riscv64
}  // namespace art
