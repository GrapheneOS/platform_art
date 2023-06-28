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
#include "arch/riscv64/registers_riscv64.h"
#include "base/macros.h"
#include "intrinsics_list.h"

namespace art {
namespace riscv64 {

static constexpr XRegister kCoreCalleeSaves[] = {
    // S1(TR) is excluded as the ART thread register.
    S0, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, RA
};

static constexpr FRegister kFpuCalleeSaves[] = {
    FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7, FS8, FS9, FS10, FS11
};

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
    INTRINSICS_LIST(IS_UNIMPLEMENTED)
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
                    ArrayRef<const bool>(detail::kIsIntrinsicUnimplemented)) {
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::GenerateFrameEntry() { LOG(FATAL) << "Unimplemented"; }
void CodeGeneratorRISCV64::GenerateFrameExit() { LOG(FATAL) << "Unimplemented"; }

void CodeGeneratorRISCV64::Bind(HBasicBlock* block) {
  UNUSED(block);
  LOG(FATAL) << "Unimplemented";
}

size_t CodeGeneratorRISCV64::GetSIMDRegisterWidth() const {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorRISCV64::MoveConstant(Location destination, int32_t value) {
  UNUSED(destination);
  UNUSED(value);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}
void CodeGeneratorRISCV64::MoveLocation(Location dst, Location src, DataType::Type dst_type) {
  UNUSED(dst);
  UNUSED(src);
  UNUSED(dst_type);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}
void CodeGeneratorRISCV64::AddLocationAsTemp(Location location, LocationSummary* locations) {
  UNUSED(location);
  UNUSED(locations);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorRISCV64::SetupBlockedRegisters() const {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorRISCV64::SaveCoreRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorRISCV64::RestoreCoreRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorRISCV64::SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorRISCV64::RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorRISCV64::DumpCoreRegister(std::ostream& stream, int reg) const {
  UNUSED(stream);
  UNUSED(reg);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorRISCV64::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  UNUSED(stream);
  UNUSED(reg);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorRISCV64::Finalize() {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

// Generate code to invoke a runtime entry point.
void CodeGeneratorRISCV64::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  UNUSED(entrypoint);
  UNUSED(instruction);
  UNUSED(dex_pc);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

// Generate code to invoke a runtime entry point, but do not record
// PC-related information in a stack map.
void CodeGeneratorRISCV64::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                               HInstruction* instruction,
                                                               SlowPathCode* slow_path) {
  UNUSED(entry_point_offset);
  UNUSED(instruction);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::IncreaseFrame(size_t adjustment) {
  UNUSED(adjustment);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::DecreaseFrame(size_t adjustment) {
  UNUSED(adjustment);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::GenerateNop() { LOG(FATAL) << "Unimplemented"; }

void CodeGeneratorRISCV64::GenerateImplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}
void CodeGeneratorRISCV64::GenerateExplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

HLoadString::LoadKind CodeGeneratorRISCV64::GetSupportedLoadStringKind(
    HLoadString::LoadKind desired_string_load_kind) {
  UNUSED(desired_string_load_kind);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

HLoadClass::LoadKind CodeGeneratorRISCV64::GetSupportedLoadClassKind(
    HLoadClass::LoadKind desired_class_load_kind) {
  UNUSED(desired_class_load_kind);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

HInvokeStaticOrDirect::DispatchInfo CodeGeneratorRISCV64::GetSupportedInvokeStaticOrDirectDispatch(
    const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) {
  UNUSED(desired_dispatch_info);
  UNUSED(method);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
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

void CodeGeneratorRISCV64::GenerateVirtualCall(HInvokeVirtual* invoke,
                                               Location temp,
                                               SlowPathCode* slow_path) {
  UNUSED(temp);
  UNUSED(invoke);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorRISCV64::MoveFromReturnRegister(Location trg, DataType::Type type) {
  UNUSED(trg);
  UNUSED(type);
  LOG(FATAL) << "Unimplemented";
}

}  // namespace riscv64
}  // namespace art
