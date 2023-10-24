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

#include "intrinsics_riscv64.h"

#include "code_generator_riscv64.h"
#include "intrinsics_utils.h"

namespace art HIDDEN {
namespace riscv64 {

using IntrinsicSlowPathRISCV64 = IntrinsicSlowPath<InvokeDexCallingConventionVisitorRISCV64,
                                                   SlowPathCodeRISCV64,
                                                   Riscv64Assembler>;

bool IntrinsicLocationsBuilderRISCV64::TryDispatch(HInvoke* invoke) {
  Dispatch(invoke);
  LocationSummary* res = invoke->GetLocations();
  if (res == nullptr) {
    return false;
  }
  return res->Intrinsified();
}

Riscv64Assembler* IntrinsicCodeGeneratorRISCV64::GetAssembler() {
  return codegen_->GetAssembler();
}

#define __ assembler->

static void CreateFPToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void CreateIntToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
}

void IntrinsicLocationsBuilderRISCV64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  __ FMvXD(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsFpuRegister<FRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  __ FMvDX(locations->Out().AsFpuRegister<FRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  __ FMvXW(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsFpuRegister<FRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  __ FMvWX(locations->Out().AsFpuRegister<FRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitDoubleIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitDoubleIsInfinite(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ FClassD(out, locations->InAt(0).AsFpuRegister<FRegister>());
  __ Andi(out, out, kPositiveInfinity | kNegativeInfinity);
  __ Snez(out, out);
}

void IntrinsicLocationsBuilderRISCV64::VisitFloatIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitFloatIsInfinite(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ FClassS(out, locations->InAt(0).AsFpuRegister<FRegister>());
  __ Andi(out, out, kPositiveInfinity | kNegativeInfinity);
  __ Snez(out, out);
}

static void CreateIntToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

template <typename EmitOp>
void EmitMemoryPeek(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekByte(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekByte(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Lb(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Lw(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Lh(rd, rs1, 0); });
}

static void CreateIntIntToVoidLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
}

template <typename EmitOp>
void EmitMemoryPoke(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->InAt(1).AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPokeByte(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPokeByte(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ Sb(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ Sw(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ Sd(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ Sh(rs2, rs1, 0); });
}

template <typename EmitOp>
void EmitIntegralUnOp(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerReverseBytes(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    // There is no 32-bit reverse bytes instruction.
    __ Rev8(rd, rs1);
    __ Srai(rd, rd, 32);
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Rev8(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitShortReverseBytes(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    // There is no 16-bit reverse bytes instruction.
    __ Rev8(rd, rs1);
    __ Srai(rd, rd, 48);
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerBitCount(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Cpopw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongBitCount(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Cpop(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    ScratchRegisterScope srs(assembler);
    XRegister tmp = srs.AllocateXRegister();
    XRegister tmp2 = srs.AllocateXRegister();
    __ Clzw(tmp, rs1);
    __ Li(tmp2, INT64_C(-0x80000000));
    __ Srlw(tmp2, tmp2, tmp);
    __ And(rd, rs1, tmp2);  // Make sure the result is zero if the input is zero.
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongHighestOneBit(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    ScratchRegisterScope srs(assembler);
    XRegister tmp = srs.AllocateXRegister();
    XRegister tmp2 = srs.AllocateXRegister();
    __ Clz(tmp, rs1);
    __ Li(tmp2, INT64_C(-0x8000000000000000));
    __ Srl(tmp2, tmp2, tmp);
    __ And(rd, rs1, tmp2);  // Make sure the result is zero if the input is zero.
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    ScratchRegisterScope srs(assembler);
    XRegister tmp = srs.AllocateXRegister();
    __ NegW(tmp, rs1);
    __ And(rd, rs1, tmp);
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongLowestOneBit(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) {
    ScratchRegisterScope srs(assembler);
    XRegister tmp = srs.AllocateXRegister();
    __ Neg(tmp, rs1);
    __ And(rd, rs1, tmp);
  });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Clzw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Clz(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Ctzw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Ctz(rd, rs1); });
}

static void GenerateVisitStringIndexOf(HInvoke* invoke,
                                       Riscv64Assembler* assembler,
                                       CodeGeneratorRISCV64* codegen,
                                       bool start_at_zero) {
  LocationSummary* locations = invoke->GetLocations();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Check for code points > 0xFFFF. Either a slow-path check when we don't know statically,
  // or directly dispatch for a large constant, or omit slow-path for a small constant or a char.
  SlowPathCodeRISCV64* slow_path = nullptr;
  HInstruction* code_point = invoke->InputAt(1);
  if (code_point->IsIntConstant()) {
    if (static_cast<uint32_t>(code_point->AsIntConstant()->GetValue()) > 0xFFFFU) {
      // Always needs the slow-path. We could directly dispatch to it, but this case should be
      // rare, so for simplicity just put the full slow-path down and branch unconditionally.
      slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathRISCV64(invoke);
      codegen->AddSlowPath(slow_path);
      __ J(slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
      return;
    }
  } else if (code_point->GetType() != DataType::Type::kUint16) {
    slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathRISCV64(invoke);
    codegen->AddSlowPath(slow_path);
    ScratchRegisterScope srs(assembler);
    XRegister tmp = srs.AllocateXRegister();
    __ Srliw(tmp, locations->InAt(1).AsRegister<XRegister>(), 16);
    __ Bnez(tmp, slow_path->GetEntryLabel());
  }

  if (start_at_zero) {
    // Start-index = 0.
    XRegister tmp_reg = locations->GetTemp(0).AsRegister<XRegister>();
    __ Li(tmp_reg, 0);
  }

  codegen->InvokeRuntime(kQuickIndexOf, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickIndexOf, int32_t, void*, uint32_t, uint32_t>();

  if (slow_path != nullptr) {
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderRISCV64::VisitStringIndexOf(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kInt32));

  // Need to send start_index=0.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
}

void IntrinsicCodeGeneratorRISCV64::VisitStringIndexOf(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ true);
}

void IntrinsicLocationsBuilderRISCV64::VisitStringIndexOfAfter(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kInt32));
}

void IntrinsicCodeGeneratorRISCV64::VisitStringIndexOfAfter(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ false);
}

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(RISCV64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(RISCV64)

}  // namespace riscv64
}  // namespace art
