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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_RISCV64_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_RISCV64_H_

#include "android-base/logging.h"
#include "arch/riscv64/registers_riscv64.h"
#include "base/macros.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "optimizing/locations.h"
#include "utils/riscv64/assembler_riscv64.h"

namespace art {

namespace riscv64 {

// InvokeDexCallingConvention registers
static constexpr XRegister kParameterCoreRegisters[] = {A1, A2, A3, A4, A5, A6, A7};
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);

static constexpr FRegister kParameterFpuRegisters[] = {FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7};
static constexpr size_t kParameterFpuRegistersLength = arraysize(kParameterFpuRegisters);

// InvokeRuntimeCallingConvention registers
static constexpr XRegister kRuntimeParameterCoreRegisters[] = {A0, A1, A2, A3, A4, A5, A6, A7};
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);

static constexpr FRegister kRuntimeParameterFpuRegisters[] = {
    FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7
};
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterFpuRegisters);

#define UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(V) INTRINSICS_LIST(V)

// Method register on invoke.
static const XRegister kArtMethodRegister = A0;

class CodeGeneratorRISCV64;

class InvokeDexCallingConvention : public CallingConvention<XRegister, FRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFpuRegisters,
                          kParameterFpuRegistersLength,
                          kRiscv64PointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorRISCV64 : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorRISCV64() {}
  virtual ~InvokeDexCallingConventionVisitorRISCV64() {}

  Location GetNextLocation(DataType::Type type) override;
  Location GetReturnLocation(DataType::Type type) const override;
  Location GetMethodLocation() const override;

 private:
  InvokeDexCallingConvention calling_convention;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorRISCV64);
};

class SlowPathCodeRISCV64 : public SlowPathCode {
 public:
  explicit SlowPathCodeRISCV64(HInstruction* instruction)
      : SlowPathCode(instruction), entry_label_(), exit_label_() {}

  Riscv64Label* GetEntryLabel() { return &entry_label_; }
  Riscv64Label* GetExitLabel() { return &exit_label_; }

 private:
  Riscv64Label entry_label_;
  Riscv64Label exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeRISCV64);
};

class LocationsBuilderRISCV64 : public HGraphVisitor {
 public:
  LocationsBuilderRISCV64(HGraph* graph, CodeGeneratorRISCV64* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super) void Visit##name(H##name* instr) override;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_RISCV64(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) override {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName() << " (id "
               << instruction->GetId() << ")";
  }

 protected:
  void HandleInvoke(HInvoke* invoke);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);
  Location RegisterOrZeroConstant(HInstruction* instruction);
  Location FpuRegisterOrConstantForStore(HInstruction* instruction);

  InvokeDexCallingConventionVisitorRISCV64 parameter_visitor_;

  CodeGeneratorRISCV64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderRISCV64);
};

class InstructionCodeGeneratorRISCV64 : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorRISCV64(HGraph* graph, CodeGeneratorRISCV64* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super) void Visit##name(H##name* instr) override;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_RISCV64(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) override {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName() << " (id "
               << instruction->GetId() << ")";
  }

  Riscv64Assembler* GetAssembler() const { return assembler_; }

  void GenerateMemoryBarrier(MemBarrierKind kind);

 protected:
  void GenerateClassInitializationCheck(SlowPathCodeRISCV64* slow_path, XRegister class_reg);
  void GenerateBitstringTypeCheckCompare(HTypeCheckInstruction* check, XRegister temp);
  void GenerateSuspendCheck(HSuspendCheck* check, HBasicBlock* successor);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction,
                      const FieldInfo& field_info,
                      bool value_can_be_null);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

  void GenerateMinMaxInt(LocationSummary* locations, bool is_min);
  void GenerateMinMaxFP(LocationSummary* locations, bool is_min, DataType::Type type);
  void GenerateMinMax(HBinaryOperation* minmax, bool is_min);

  // Generate a heap reference load using one register `out`:
  //
  //   out <- *(out + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a read barrier and
  // shall be a register in that case; it may be an invalid location
  // otherwise.
  void GenerateReferenceLoadOneRegister(HInstruction* instruction,
                                        Location out,
                                        uint32_t offset,
                                        Location maybe_temp,
                                        ReadBarrierOption read_barrier_option);
  // Generate a heap reference load using two different registers
  // `out` and `obj`:
  //
  //   out <- *(obj + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a Baker's (fast
  // path) read barrier and shall be a register in that case; it may
  // be an invalid location otherwise.
  void GenerateReferenceLoadTwoRegisters(HInstruction* instruction,
                                         Location out,
                                         Location obj,
                                         uint32_t offset,
                                         Location maybe_temp,
                                         ReadBarrierOption read_barrier_option);

  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers (if any).
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               XRegister obj,
                               uint32_t offset,
                               ReadBarrierOption read_barrier_option,
                               Riscv64Label* label_low = nullptr);

  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             Riscv64Label* true_target,
                             Riscv64Label* false_target);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemIntegral(HBinaryOperation* instruction);
  void GenerateIntLongCompare(IfCondition cond, bool is64bit, LocationSummary* locations);
  // When the function returns `false` it means that the condition holds if `dst` is non-zero
  // and doesn't hold if `dst` is zero. If it returns `true`, the roles of zero and non-zero
  // `dst` are exchanged.
  bool MaterializeIntLongCompare(IfCondition cond,
                                 bool is64bit,
                                 LocationSummary* input_locations,
                                 XRegister dst);
  void GenerateIntLongCompareAndBranch(IfCondition cond,
                                       bool is64bit,
                                       LocationSummary* locations,
                                       Riscv64Label* label);
  void GenerateFpCompare(IfCondition cond,
                         bool gt_bias,
                         DataType::Type type,
                         LocationSummary* locations);
  // When the function returns `false` it means that the condition holds if `dst` is non-zero
  // and doesn't hold if `dst` is zero. If it returns `true`, the roles of zero and non-zero
  // `dst` are exchanged.
  bool MaterializeFpCompare(IfCondition cond,
                            bool gt_bias,
                            DataType::Type type,
                            LocationSummary* input_locations,
                            XRegister dst);
  void GenerateFpCompareAndBranch(IfCondition cond,
                                  bool gt_bias,
                                  DataType::Type type,
                                  LocationSummary* locations,
                                  Riscv64Label* label);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);
  void GenPackedSwitchWithCompares(XRegister value_reg,
                                   int32_t lower_bound,
                                   uint32_t num_entries,
                                   HBasicBlock* switch_block,
                                   HBasicBlock* default_block);
  void GenTableBasedPackedSwitch(XRegister value_reg,
                                 int32_t lower_bound,
                                 uint32_t num_entries,
                                 HBasicBlock* switch_block,
                                 HBasicBlock* default_block);
  int32_t VecAddress(LocationSummary* locations,
                     size_t size,
                     /*out*/ XRegister* adjusted_base);
  void GenConditionalMove(HSelect* select);

  Riscv64Assembler* const assembler_;
  CodeGeneratorRISCV64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorRISCV64);
};

class CodeGeneratorRISCV64 : public CodeGenerator {
 public:
  CodeGeneratorRISCV64(HGraph* graph,
                       const CompilerOptions& compiler_options,
                       OptimizingCompilerStats* stats = nullptr);
  virtual ~CodeGeneratorRISCV64() {}

  void GenerateFrameEntry() override;
  void GenerateFrameExit() override;

  void Bind(HBasicBlock* block) override;

  size_t GetWordSize() const override { return kRiscv64WordSize; }

  bool SupportsPredicatedSIMD() const override {
    // TODO(riscv64): Check the vector extension.
    return false;
  }

  size_t GetSlowPathFPWidth() const override {
    LOG(FATAL) << "CodeGeneratorRISCV64::GetSlowPathFPWidth is unimplemented";
    UNREACHABLE();
  }

  size_t GetCalleePreservedFPWidth() const override {
    LOG(FATAL) << "CodeGeneratorRISCV64::GetCalleePreservedFPWidth is unimplemented";
    UNREACHABLE();
  };

  size_t GetSIMDRegisterWidth() const override;

  uintptr_t GetAddressOf(HBasicBlock* block) override {
    UNUSED(block);
    LOG(FATAL) << "CodeGeneratorRISCV64::GetAddressOf is unimplemented";
    UNREACHABLE();
  };

  void Initialize() override { LOG(FATAL) << "unimplemented"; }

  void MoveConstant(Location destination, int32_t value) override;
  void MoveLocation(Location dst, Location src, DataType::Type dst_type) override;
  void AddLocationAsTemp(Location location, LocationSummary* locations) override;

  HGraphVisitor* GetInstructionVisitor() override {
    LOG(FATAL) << "unimplemented";
    UNREACHABLE();
  }

  Riscv64Assembler* GetAssembler() override { return &assembler_; }
  const Riscv64Assembler& GetAssembler() const override { return assembler_; }

  HGraphVisitor* GetLocationBuilder() override { return &location_builder_; }

  void SetupBlockedRegisters() const override;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id) override;
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id) override;
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) override;
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) override;

  void DumpCoreRegister(std::ostream& stream, int reg) const override;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const override;

  InstructionSet GetInstructionSet() const override { return InstructionSet::kRiscv64; }

  uint32_t GetPreferredSlotsAlignment() const override {
    LOG(FATAL) << "Unimplemented";
    UNREACHABLE();
  }

  void Finalize() override;

  // Generate code to invoke a runtime entry point.
  void InvokeRuntime(QuickEntrypointEnum entrypoint,
                     HInstruction* instruction,
                     uint32_t dex_pc,
                     SlowPathCode* slow_path = nullptr) override;

  // Generate code to invoke a runtime entry point, but do not record
  // PC-related information in a stack map.
  void InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                           HInstruction* instruction,
                                           SlowPathCode* slow_path);

  // TODO(riscv64): Add ParallelMoveResolverRISCV64 Later
  ParallelMoveResolver* GetMoveResolver() override {
    LOG(FATAL) << "Unimplemented";
    UNREACHABLE();
  }

  bool NeedsTwoRegisters([[maybe_unused]] DataType::Type type) const override { return false; }

  void IncreaseFrame(size_t adjustment) override;
  void DecreaseFrame(size_t adjustment) override;

  void GenerateNop() override;

  void GenerateImplicitNullCheck(HNullCheck* instruction) override;
  void GenerateExplicitNullCheck(HNullCheck* instruction) override;

  // Check if the desired_string_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadString::LoadKind GetSupportedLoadStringKind(
      HLoadString::LoadKind desired_string_load_kind) override;

  // Check if the desired_class_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadClass::LoadKind GetSupportedLoadClassKind(
      HLoadClass::LoadKind desired_class_load_kind) override;

  // Check if the desired_dispatch_info is supported. If it is, return it,
  // otherwise return a fall-back info that should be used instead.
  HInvokeStaticOrDirect::DispatchInfo GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) override;

  void LoadMethod(MethodLoadKind load_kind, Location temp, HInvoke* invoke);
  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                  Location temp,
                                  SlowPathCode* slow_path = nullptr) override;
  void GenerateVirtualCall(HInvokeVirtual* invoke,
                           Location temp,
                           SlowPathCode* slow_path = nullptr) override;
  void MoveFromReturnRegister(Location trg, DataType::Type type) override;

 private:
  Riscv64Assembler assembler_;
  LocationsBuilderRISCV64 location_builder_;
};

}  // namespace riscv64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_RISCV64_H_
