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

class CodeGeneratorRISCV64;

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

  HGraphVisitor* GetLocationBuilder() override {
    LOG(FATAL) << "Unimplemented";
    UNREACHABLE();
  }

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
};

}  // namespace riscv64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_RISCV64_H_
