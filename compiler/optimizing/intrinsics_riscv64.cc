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

static void CreateFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;

  locations->SetInAt(0, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
  locations->SetOut(calling_convention.GetReturnLocation(invoke->GetType()));
}

static void CreateFPFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(1)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;

  locations->SetInAt(0, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
  locations->SetInAt(1, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(1)));
  locations->SetOut(calling_convention.GetReturnLocation(invoke->GetType()));
}

static void CreateFpFpFpToFpNoOverlapLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 3U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(1)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(2)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetInAt(2, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void CreateFPToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
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

static void CreateIntToIntNoOverlapLocations(ArenaAllocator* allocator, HInvoke* invoke) {
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
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekByte(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Lb(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Lw(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderRISCV64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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

static void CreateIntIntToIntSlowPathCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // Force kOutputOverlap; see comments in IntrinsicSlowPath::EmitNativeCode.
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
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

static void GenerateReverseBytes(Riscv64Assembler* assembler,
                                 Location rd,
                                 XRegister rs1,
                                 DataType::Type type) {
  switch (type) {
    case DataType::Type::kUint16:
      // There is no 16-bit reverse bytes instruction.
      __ Rev8(rd.AsRegister<XRegister>(), rs1);
      __ Srli(rd.AsRegister<XRegister>(), rd.AsRegister<XRegister>(), 48);
      break;
    case DataType::Type::kInt16:
      // There is no 16-bit reverse bytes instruction.
      __ Rev8(rd.AsRegister<XRegister>(), rs1);
      __ Srai(rd.AsRegister<XRegister>(), rd.AsRegister<XRegister>(), 48);
      break;
    case DataType::Type::kInt32:
      // There is no 32-bit reverse bytes instruction.
      __ Rev8(rd.AsRegister<XRegister>(), rs1);
      __ Srai(rd.AsRegister<XRegister>(), rd.AsRegister<XRegister>(), 32);
      break;
    case DataType::Type::kInt64:
      __ Rev8(rd.AsRegister<XRegister>(), rs1);
      break;
    case DataType::Type::kFloat32:
      // There is no 32-bit reverse bytes instruction.
      __ Rev8(rs1, rs1);  // Note: Clobbers `rs1`.
      __ Srai(rs1, rs1, 32);
      __ FMvWX(rd.AsFpuRegister<FRegister>(), rs1);
      break;
    case DataType::Type::kFloat64:
      __ Rev8(rs1, rs1);  // Note: Clobbers `rs1`.
      __ FMvDX(rd.AsFpuRegister<FRegister>(), rs1);
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << type;
      UNREACHABLE();
  }
}

static void GenerateReverseBytes(Riscv64Assembler* assembler,
                                 HInvoke* invoke,
                                 DataType::Type type) {
  DCHECK_EQ(type, invoke->GetType());
  LocationSummary* locations = invoke->GetLocations();
  GenerateReverseBytes(
      assembler, locations->Out(), locations->InAt(0).AsRegister<XRegister>(), type);
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerReverseBytes(HInvoke* invoke) {
  GenerateReverseBytes(GetAssembler(), invoke, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  GenerateReverseBytes(GetAssembler(), invoke, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderRISCV64::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitShortReverseBytes(HInvoke* invoke) {
  GenerateReverseBytes(GetAssembler(), invoke, DataType::Type::kInt16);
}

template <typename EmitOp>
void EmitIntegralUnOp(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerBitCount(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Cpopw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongBitCount(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongBitCount(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Cpop(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Clzw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Clz(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  EmitIntegralUnOp(invoke, [&](XRegister rd, XRegister rs1) { __ Ctzw(rd, rs1); });
}

void IntrinsicLocationsBuilderRISCV64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntNoOverlapLocations(allocator_, invoke);
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

static void EmitLoadReserved(Riscv64Assembler* assembler,
                             DataType::Type type,
                             XRegister ptr,
                             XRegister old_value,
                             AqRl aqrl) {
  switch (type) {
    case DataType::Type::kInt32:
      __ LrW(old_value, ptr, aqrl);
      break;
    case DataType::Type::kReference:
      __ LrW(old_value, ptr, aqrl);
      // TODO(riscv64): The `ZextW()` macro currently emits `SLLI+SRLI` which are from the
      // base "I" instruction set. When the assembler is updated to use a single-instruction
      // `ZextW()` macro, either the ADD.UW, or the C.ZEXT.W (16-bit encoding), we need to
      // rewrite this to avoid these non-"I" instructions. We could, for example, sign-extend
      // the reference and do the CAS as `Int32`.
      __ ZextW(old_value, old_value);
      break;
    case DataType::Type::kInt64:
      __ LrD(old_value, ptr, aqrl);
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << type;
      UNREACHABLE();
  }
}

static void EmitStoreConditional(Riscv64Assembler* assembler,
                                 DataType::Type type,
                                 XRegister ptr,
                                 XRegister store_result,
                                 XRegister to_store,
                                 AqRl aqrl) {
  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kReference:
      __ ScW(store_result, to_store, ptr, aqrl);
      break;
    case DataType::Type::kInt64:
      __ ScD(store_result, to_store, ptr, aqrl);
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << type;
      UNREACHABLE();
  }
}

static void GenerateCompareAndSet(Riscv64Assembler* assembler,
                                  DataType::Type type,
                                  std::memory_order order,
                                  bool strong,
                                  Riscv64Label* cmp_failure,
                                  XRegister ptr,
                                  XRegister new_value,
                                  XRegister old_value,
                                  XRegister mask,
                                  XRegister masked,
                                  XRegister store_result,
                                  XRegister expected,
                                  XRegister expected2 = kNoXRegister) {
  DCHECK(!DataType::IsFloatingPointType(type));
  DCHECK_GE(DataType::Size(type), 4u);

  // The `expected2` is valid only for reference slow path and represents the unmarked old value
  // from the main path attempt to emit CAS when the marked old value matched `expected`.
  DCHECK_IMPLIES(expected2 != kNoXRegister, type == DataType::Type::kReference);

  AqRl load_aqrl = AqRl::kNone;
  AqRl store_aqrl = AqRl::kNone;
  if (order == std::memory_order_acquire) {
    load_aqrl = AqRl::kAcquire;
  } else if (order == std::memory_order_release) {
    store_aqrl = AqRl::kRelease;
  } else if (order == std::memory_order_seq_cst) {
    load_aqrl = AqRl::kAqRl;
    store_aqrl = AqRl::kRelease;
  } else {
    DCHECK(order == std::memory_order_relaxed);
  }

  // repeat: {
  //   old_value = [ptr];  // Load exclusive.
  //   cmp_value = old_value & mask;  // Extract relevant bits if applicable.
  //   if (cmp_value != expected && cmp_value != expected2) goto cmp_failure;
  //   store_result = failed([ptr] <- new_value);  // Store exclusive.
  // }
  // if (strong) {
  //   if (store_result) goto repeat;  // Repeat until compare fails or store exclusive succeeds.
  // } else {
  //   store_result = store_result ^ 1;  // Report success as 1, failure as 0.
  // }
  //
  // (If `mask` is not valid, `expected` is compared with `old_value` instead of `cmp_value`.)
  // (If `expected2` is not valid, the `cmp_value == expected2` part is not emitted.)

  // Note: We're using "bare" local branches to enforce that they shall not be expanded
  // and the scrach register `TMP` shall not be clobbered if taken. Taking the branch to
  // `cmp_failure` can theoretically clobber `TMP` (if outside the 1 MiB range).
  Riscv64Label loop;
  if (strong) {
    __ Bind(&loop);
  }
  EmitLoadReserved(assembler, type, ptr, old_value, load_aqrl);
  XRegister to_store = new_value;
  if (mask != kNoXRegister) {
    DCHECK_EQ(expected2, kNoXRegister);
    DCHECK_NE(masked, kNoXRegister);
    __ And(masked, old_value, mask);
    __ Bne(masked, expected, cmp_failure);
    // The `old_value` does not need to be preserved as the caller shall use `masked`
    // to return the old value if needed.
    to_store = old_value;
    // TODO(riscv64): We could XOR the old and new value before the loop and use XOR here
    // instead of the ANDN+OR. (The `new_value` is either Zero or a temporary we can clobber.)
    __ Andn(to_store, old_value, mask);
    __ Or(to_store, to_store, new_value);
  } else if (expected2 != kNoXRegister) {
    Riscv64Label match2;
    __ Beq(old_value, expected2, &match2, /*is_bare=*/ true);
    __ Bne(old_value, expected, cmp_failure);
    __ Bind(&match2);
  } else {
    __ Bne(old_value, expected, cmp_failure);
  }
  EmitStoreConditional(assembler, type, ptr, store_result, to_store, store_aqrl);
  if (strong) {
    __ Bnez(store_result, &loop, /*is_bare=*/ true);
  } else {
    // Flip the `store_result` register to indicate success by 1 and failure by 0.
    __ Xori(store_result, store_result, 1);
  }
}

class ReadBarrierCasSlowPathRISCV64 : public SlowPathCodeRISCV64 {
 public:
  ReadBarrierCasSlowPathRISCV64(HInvoke* invoke,
                                std::memory_order order,
                                bool strong,
                                XRegister base,
                                XRegister offset,
                                XRegister expected,
                                XRegister new_value,
                                XRegister old_value,
                                XRegister old_value_temp,
                                XRegister store_result,
                                bool update_old_value,
                                CodeGeneratorRISCV64* riscv64_codegen)
      : SlowPathCodeRISCV64(invoke),
        order_(order),
        strong_(strong),
        base_(base),
        offset_(offset),
        expected_(expected),
        new_value_(new_value),
        old_value_(old_value),
        old_value_temp_(old_value_temp),
        store_result_(store_result),
        update_old_value_(update_old_value),
        mark_old_value_slow_path_(nullptr),
        update_old_value_slow_path_(nullptr) {
    // We need to add slow paths now, it is too late when emitting slow path code.
    Location old_value_loc = Location::RegisterLocation(old_value);
    Location old_value_temp_loc = Location::RegisterLocation(old_value_temp);
    if (kUseBakerReadBarrier) {
      mark_old_value_slow_path_ = riscv64_codegen->AddGcRootBakerBarrierBarrierSlowPath(
          invoke, old_value_temp_loc, kBakerReadBarrierTemp);
      if (update_old_value_) {
        update_old_value_slow_path_ = riscv64_codegen->AddGcRootBakerBarrierBarrierSlowPath(
            invoke, old_value_loc, kBakerReadBarrierTemp);
      }
    } else {
      Location base_loc = Location::RegisterLocation(base);
      Location index = Location::RegisterLocation(offset);
      mark_old_value_slow_path_ = riscv64_codegen->AddReadBarrierSlowPath(
          invoke, old_value_temp_loc, old_value_loc, base_loc, /*offset=*/ 0u, index);
      if (update_old_value_) {
        update_old_value_slow_path_ = riscv64_codegen->AddReadBarrierSlowPath(
            invoke, old_value_loc, old_value_temp_loc, base_loc, /*offset=*/ 0u, index);
      }
    }
  }

  const char* GetDescription() const override { return "ReadBarrierCasSlowPathRISCV64"; }

  void EmitNativeCode(CodeGenerator* codegen) override {
    CodeGeneratorRISCV64* riscv64_codegen = down_cast<CodeGeneratorRISCV64*>(codegen);
    Riscv64Assembler* assembler = riscv64_codegen->GetAssembler();
    __ Bind(GetEntryLabel());

    // Mark the `old_value_` from the main path and compare with `expected_`.
    DCHECK(mark_old_value_slow_path_ != nullptr);
    if (kUseBakerReadBarrier) {
      __ Mv(old_value_temp_, old_value_);
      riscv64_codegen->EmitBakerReadBarierMarkingCheck(mark_old_value_slow_path_,
                                                       Location::RegisterLocation(old_value_temp_),
                                                       kBakerReadBarrierTemp);
    } else {
      __ J(mark_old_value_slow_path_->GetEntryLabel());
      __ Bind(mark_old_value_slow_path_->GetExitLabel());
    }
    Riscv64Label move_marked_old_value;
    __ Bne(old_value_temp_, expected_, update_old_value_ ? &move_marked_old_value : GetExitLabel());

    // The `old_value` we have read did not match `expected` (which is always a to-space
    // reference) but after the read barrier the marked to-space value matched, so the
    // `old_value` must be a from-space reference to the same object. Do the same CAS loop
    // as the main path but check for both `expected` and the unmarked old value
    // representing the to-space and from-space references for the same object.

    ScratchRegisterScope srs(assembler);
    XRegister tmp_ptr = srs.AllocateXRegister();
    XRegister store_result =
        store_result_ != kNoXRegister ? store_result_ : srs.AllocateXRegister();

    // Recalculate the `tmp_ptr` from main path potentially clobbered by the read barrier above
    // or by an expanded conditional branch (clobbers `TMP` if beyond 1MiB).
    __ Add(tmp_ptr, base_, offset_);

    Riscv64Label mark_old_value;
    GenerateCompareAndSet(riscv64_codegen->GetAssembler(),
                          DataType::Type::kReference,
                          order_,
                          strong_,
                          /*cmp_failure=*/ update_old_value_ ? &mark_old_value : GetExitLabel(),
                          tmp_ptr,
                          new_value_,
                          /*old_value=*/ old_value_temp_,
                          /*mask=*/ kNoXRegister,
                          /*masked=*/ kNoXRegister,
                          store_result,
                          expected_,
                          /*expected2=*/ old_value_);
    if (update_old_value_) {
      // To reach this point, the `old_value_temp_` must be either a from-space or a to-space
      // reference of the `expected_` object. Update the `old_value_` to the to-space reference.
      __ Mv(old_value_, expected_);
    } else if (strong_) {
      // Load success value to the result register.
      // `GenerateCompareAndSet()` does not emit code to indicate success for a strong CAS.
      // TODO(riscv64): We could just jump to an identical instruction in the fast-path.
      // This would require an additional label as we would have two different slow path exits.
      __ Li(store_result, 1);
    }
    __ J(GetExitLabel());

    if (update_old_value_) {
      // TODO(riscv64): If we initially saw a from-space reference and then saw
      // a different reference, can the latter be also a from-space reference?
      // (Shouldn't every reference write store a to-space reference?)
      DCHECK(update_old_value_slow_path_ != nullptr);
      __ Bind(&mark_old_value);
      if (kUseBakerReadBarrier) {
        DCHECK(update_old_value_slow_path_ == nullptr);
        __ Mv(old_value_, old_value_temp_);
        riscv64_codegen->EmitBakerReadBarierMarkingCheck(update_old_value_slow_path_,
                                                         Location::RegisterLocation(old_value_),
                                                         kBakerReadBarrierTemp);
      } else {
        // Note: We could redirect the `failure` above directly to the entry label and bind
        // the exit label in the main path, but the main path would need to access the
        // `update_old_value_slow_path_`. To keep the code simple, keep the extra jumps.
        __ J(update_old_value_slow_path_->GetEntryLabel());
        __ Bind(update_old_value_slow_path_->GetExitLabel());
      }
      __ J(GetExitLabel());

      __ Bind(&move_marked_old_value);
      __ Mv(old_value_, old_value_temp_);
      __ J(GetExitLabel());
    }
  }

 private:
  // Use RA as temp. It is clobbered in the slow path anyway.
  static constexpr Location kBakerReadBarrierTemp = Location::RegisterLocation(RA);

  std::memory_order order_;
  bool strong_;
  XRegister base_;
  XRegister offset_;
  XRegister expected_;
  XRegister new_value_;
  XRegister old_value_;
  XRegister old_value_temp_;
  XRegister store_result_;
  bool update_old_value_;
  SlowPathCodeRISCV64* mark_old_value_slow_path_;
  SlowPathCodeRISCV64* update_old_value_slow_path_;
};

enum class GetAndUpdateOp {
  kSet,
  kAdd,
  kAddWithByteSwap,
  kAnd,
  kOr,
  kXor
};

class VarHandleSlowPathRISCV64 : public IntrinsicSlowPathRISCV64 {
 public:
  VarHandleSlowPathRISCV64(HInvoke* invoke, std::memory_order order)
      : IntrinsicSlowPathRISCV64(invoke),
        order_(order),
        return_success_(false),
        strong_(false),
        get_and_update_op_(GetAndUpdateOp::kAdd) {
  }

  Riscv64Label* GetByteArrayViewCheckLabel() {
    return &byte_array_view_check_label_;
  }

  Riscv64Label* GetNativeByteOrderLabel() {
    return &native_byte_order_label_;
  }

  void SetCompareAndSetOrExchangeArgs(bool return_success, bool strong) {
    if (return_success) {
      DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kCompareAndSet);
    } else {
      DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kCompareAndExchange);
    }
    return_success_ = return_success;
    strong_ = strong;
  }

  void SetGetAndUpdateOp(GetAndUpdateOp get_and_update_op) {
    DCHECK(GetAccessModeTemplate() == mirror::VarHandle::AccessModeTemplate::kGetAndUpdate);
    get_and_update_op_ = get_and_update_op;
  }

  void EmitNativeCode(CodeGenerator* codegen_in) override {
    if (GetByteArrayViewCheckLabel()->IsLinked()) {
      EmitByteArrayViewCode(codegen_in);
    }
    IntrinsicSlowPathRISCV64::EmitNativeCode(codegen_in);
  }

 private:
  HInvoke* GetInvoke() const {
    return GetInstruction()->AsInvoke();
  }

  mirror::VarHandle::AccessModeTemplate GetAccessModeTemplate() const {
    return mirror::VarHandle::GetAccessModeTemplateByIntrinsic(GetInvoke()->GetIntrinsic());
  }

  void EmitByteArrayViewCode(CodeGenerator* codegen_in);

  Riscv64Label byte_array_view_check_label_;
  Riscv64Label native_byte_order_label_;
  // Shared parameter for all VarHandle intrinsics.
  std::memory_order order_;
  // Extra arguments for GenerateVarHandleCompareAndSetOrExchange().
  bool return_success_;
  bool strong_;
  // Extra argument for GenerateVarHandleGetAndUpdate().
  GetAndUpdateOp get_and_update_op_;
};

// Generate subtype check without read barriers.
static void GenerateSubTypeObjectCheckNoReadBarrier(CodeGeneratorRISCV64* codegen,
                                                    SlowPathCodeRISCV64* slow_path,
                                                    XRegister object,
                                                    XRegister type,
                                                    bool object_can_be_null = true) {
  Riscv64Assembler* assembler = codegen->GetAssembler();

  const MemberOffset class_offset = mirror::Object::ClassOffset();
  const MemberOffset super_class_offset = mirror::Class::SuperClassOffset();

  Riscv64Label success;
  if (object_can_be_null) {
    __ Beqz(object, &success);
  }

  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();

  // Note: The `type` can be `TMP`. We're using "bare" local branches to enforce that they shall
  // not be expanded and the scrach register `TMP` shall not be clobbered if taken. Taking the
  // branch to the slow path can theoretically clobber `TMP` (if outside the 1 MiB range).
  __ Loadwu(temp, object, class_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp);
  Riscv64Label loop;
  __ Bind(&loop);
  __ Beq(type, temp, &success, /*is_bare=*/ true);
  // We may not have another scratch register for `Loadwu()`. Use `Lwu()` directly.
  DCHECK(IsInt<12>(super_class_offset.Int32Value()));
  __ Lwu(temp, temp, super_class_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp);
  __ Beqz(temp, slow_path->GetEntryLabel());
  __ J(&loop, /*is_bare=*/ true);
  __ Bind(&success);
}

// Check access mode and the primitive type from VarHandle.varType.
// Check reference arguments against the VarHandle.varType; for references this is a subclass
// check without read barrier, so it can have false negatives which we handle in the slow path.
static void GenerateVarHandleAccessModeAndVarTypeChecks(HInvoke* invoke,
                                                        CodeGeneratorRISCV64* codegen,
                                                        SlowPathCodeRISCV64* slow_path,
                                                        DataType::Type type) {
  mirror::VarHandle::AccessMode access_mode =
      mirror::VarHandle::GetAccessModeByIntrinsic(invoke->GetIntrinsic());
  Primitive::Type primitive_type = DataTypeToPrimitive(type);

  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister varhandle = locations->InAt(0).AsRegister<XRegister>();

  const MemberOffset var_type_offset = mirror::VarHandle::VarTypeOffset();
  const MemberOffset access_mode_bit_mask_offset = mirror::VarHandle::AccessModesBitMaskOffset();
  const MemberOffset primitive_type_offset = mirror::Class::PrimitiveTypeOffset();

  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();
  XRegister temp2 = srs.AllocateXRegister();

  // Check that the operation is permitted.
  __ Loadw(temp, varhandle, access_mode_bit_mask_offset.Int32Value());
  DCHECK_LT(enum_cast<uint32_t>(access_mode), 31u);  // We cannot avoid the shift below.
  __ Slliw(temp, temp, 31 - enum_cast<uint32_t>(access_mode));  // Shift tested bit to sign bit.
  __ Bgez(temp, slow_path->GetEntryLabel());  // If not permitted, go to slow path.

  // For primitive types, we do not need a read barrier when loading a reference only for loading
  // constant field through the reference. For reference types, we deliberately avoid the read
  // barrier, letting the slow path handle the false negatives.
  __ Loadw(temp, varhandle, var_type_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp);

  // Check the varType.primitiveType field against the type we're trying to use.
  __ Loadhu(temp2, temp, primitive_type_offset.Int32Value());
  if (primitive_type == Primitive::kPrimNot) {
    static_assert(Primitive::kPrimNot == 0);
    __ Bnez(temp2, slow_path->GetEntryLabel());
  } else {
    __ Li(temp, enum_cast<int32_t>(primitive_type));  // `temp` can be clobbered.
    __ Bne(temp2, temp, slow_path->GetEntryLabel());
  }

  srs.FreeXRegister(temp2);

  if (type == DataType::Type::kReference) {
    // Check reference arguments against the varType.
    // False negatives due to varType being an interface or array type
    // or due to the missing read barrier are handled by the slow path.
    size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
    uint32_t arguments_start = /* VarHandle object */ 1u + expected_coordinates_count;
    uint32_t number_of_arguments = invoke->GetNumberOfArguments();
    for (size_t arg_index = arguments_start; arg_index != number_of_arguments; ++arg_index) {
      HInstruction* arg = invoke->InputAt(arg_index);
      DCHECK_EQ(arg->GetType(), DataType::Type::kReference);
      if (!arg->IsNullConstant()) {
        XRegister arg_reg = locations->InAt(arg_index).AsRegister<XRegister>();
        GenerateSubTypeObjectCheckNoReadBarrier(codegen, slow_path, arg_reg, temp);
      }
    }
  }
}

static void GenerateVarHandleStaticFieldCheck(HInvoke* invoke,
                                              CodeGeneratorRISCV64* codegen,
                                              SlowPathCodeRISCV64* slow_path) {
  Riscv64Assembler* assembler = codegen->GetAssembler();
  XRegister varhandle = invoke->GetLocations()->InAt(0).AsRegister<XRegister>();

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();

  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();

  // Check that the VarHandle references a static field by checking that coordinateType0 == null.
  // Do not emit read barrier (or unpoison the reference) for comparing to null.
  __ Loadwu(temp, varhandle, coordinate_type0_offset.Int32Value());
  __ Bnez(temp, slow_path->GetEntryLabel());
}

static void GenerateVarHandleInstanceFieldChecks(HInvoke* invoke,
                                                 CodeGeneratorRISCV64* codegen,
                                                 SlowPathCodeRISCV64* slow_path) {
  VarHandleOptimizations optimizations(invoke);
  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister varhandle = locations->InAt(0).AsRegister<XRegister>();
  XRegister object = locations->InAt(1).AsRegister<XRegister>();

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();
  const MemberOffset coordinate_type1_offset = mirror::VarHandle::CoordinateType1Offset();

  // Null-check the object.
  if (!optimizations.GetSkipObjectNullCheck()) {
    __ Beqz(object, slow_path->GetEntryLabel());
  }

  if (!optimizations.GetUseKnownBootImageVarHandle()) {
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();

    // Check that the VarHandle references an instance field by checking that
    // coordinateType1 == null. coordinateType0 should not be null, but this is handled by the
    // type compatibility check with the source object's type, which will fail for null.
    __ Loadwu(temp, varhandle, coordinate_type1_offset.Int32Value());
    // No need for read barrier or unpoisoning of coordinateType1 for comparison with null.
    __ Bnez(temp, slow_path->GetEntryLabel());

    // Check that the object has the correct type.
    // We deliberately avoid the read barrier, letting the slow path handle the false negatives.
    __ Loadwu(temp, varhandle, coordinate_type0_offset.Int32Value());
    codegen->MaybeUnpoisonHeapReference(temp);
    GenerateSubTypeObjectCheckNoReadBarrier(
        codegen, slow_path, object, temp, /*object_can_be_null=*/ false);
  }
}

static void GenerateVarHandleArrayChecks(HInvoke* invoke,
                                         CodeGeneratorRISCV64* codegen,
                                         VarHandleSlowPathRISCV64* slow_path) {
  VarHandleOptimizations optimizations(invoke);
  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister varhandle = locations->InAt(0).AsRegister<XRegister>();
  XRegister object = locations->InAt(1).AsRegister<XRegister>();
  XRegister index = locations->InAt(2).AsRegister<XRegister>();
  DataType::Type value_type =
      GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
  Primitive::Type primitive_type = DataTypeToPrimitive(value_type);

  const MemberOffset coordinate_type0_offset = mirror::VarHandle::CoordinateType0Offset();
  const MemberOffset coordinate_type1_offset = mirror::VarHandle::CoordinateType1Offset();
  const MemberOffset component_type_offset = mirror::Class::ComponentTypeOffset();
  const MemberOffset primitive_type_offset = mirror::Class::PrimitiveTypeOffset();
  const MemberOffset class_offset = mirror::Object::ClassOffset();
  const MemberOffset array_length_offset = mirror::Array::LengthOffset();

  // Null-check the object.
  if (!optimizations.GetSkipObjectNullCheck()) {
    __ Beqz(object, slow_path->GetEntryLabel());
  }

  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();
  XRegister temp2 = srs.AllocateXRegister();

  // Check that the VarHandle references an array, byte array view or ByteBuffer by checking
  // that coordinateType1 != null. If that's true, coordinateType1 shall be int.class and
  // coordinateType0 shall not be null but we do not explicitly verify that.
  __ Loadwu(temp, varhandle, coordinate_type1_offset.Int32Value());
  // No need for read barrier or unpoisoning of coordinateType1 for comparison with null.
  __ Beqz(temp, slow_path->GetEntryLabel());

  // Check object class against componentType0.
  //
  // This is an exact check and we defer other cases to the runtime. This includes
  // conversion to array of superclass references, which is valid but subsequently
  // requires all update operations to check that the value can indeed be stored.
  // We do not want to perform such extra checks in the intrinsified code.
  //
  // We do this check without read barrier, so there can be false negatives which we
  // defer to the slow path. There shall be no false negatives for array classes in the
  // boot image (including Object[] and primitive arrays) because they are non-movable.
  __ Loadwu(temp, varhandle, coordinate_type0_offset.Int32Value());
  __ Loadwu(temp2, object, class_offset.Int32Value());
  __ Bne(temp, temp2, slow_path->GetEntryLabel());

  // Check that the coordinateType0 is an array type. We do not need a read barrier
  // for loading constant reference fields (or chains of them) for comparison with null,
  // nor for finally loading a constant primitive field (primitive type) below.
  codegen->MaybeUnpoisonHeapReference(temp);
  __ Loadwu(temp2, temp, component_type_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp2);
  __ Beqz(temp2, slow_path->GetEntryLabel());

  // Check that the array component type matches the primitive type.
  __ Loadhu(temp, temp2, primitive_type_offset.Int32Value());
  if (primitive_type == Primitive::kPrimNot) {
    static_assert(Primitive::kPrimNot == 0);
    __ Bnez(temp, slow_path->GetEntryLabel());
  } else {
    // With the exception of `kPrimNot` (handled above), `kPrimByte` and `kPrimBoolean`,
    // we shall check for a byte array view in the slow path.
    // The check requires the ByteArrayViewVarHandle.class to be in the boot image,
    // so we cannot emit that if we're JITting without boot image.
    bool boot_image_available =
        codegen->GetCompilerOptions().IsBootImage() ||
        !Runtime::Current()->GetHeap()->GetBootImageSpaces().empty();
    bool can_be_view = (DataType::Size(value_type) != 1u) && boot_image_available;
    Riscv64Label* slow_path_label =
        can_be_view ? slow_path->GetByteArrayViewCheckLabel() : slow_path->GetEntryLabel();
    __ Li(temp2, enum_cast<int32_t>(primitive_type));
    __ Bne(temp, temp2, slow_path_label);
  }

  // Check for array index out of bounds.
  __ Loadw(temp, object, array_length_offset.Int32Value());
  __ Bgeu(index, temp, slow_path->GetEntryLabel());
}

static void GenerateVarHandleCoordinateChecks(HInvoke* invoke,
                                              CodeGeneratorRISCV64* codegen,
                                              VarHandleSlowPathRISCV64* slow_path) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  if (expected_coordinates_count == 0u) {
    GenerateVarHandleStaticFieldCheck(invoke, codegen, slow_path);
  } else if (expected_coordinates_count == 1u) {
    GenerateVarHandleInstanceFieldChecks(invoke, codegen, slow_path);
  } else {
    DCHECK_EQ(expected_coordinates_count, 2u);
    GenerateVarHandleArrayChecks(invoke, codegen, slow_path);
  }
}

static VarHandleSlowPathRISCV64* GenerateVarHandleChecks(HInvoke* invoke,
                                                         CodeGeneratorRISCV64* codegen,
                                                         std::memory_order order,
                                                         DataType::Type type) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  VarHandleOptimizations optimizations(invoke);
  if (optimizations.GetUseKnownBootImageVarHandle()) {
    DCHECK_NE(expected_coordinates_count, 2u);
    if (expected_coordinates_count == 0u || optimizations.GetSkipObjectNullCheck()) {
      return nullptr;
    }
  }

  VarHandleSlowPathRISCV64* slow_path =
      new (codegen->GetScopedAllocator()) VarHandleSlowPathRISCV64(invoke, order);
  codegen->AddSlowPath(slow_path);

  if (!optimizations.GetUseKnownBootImageVarHandle()) {
    GenerateVarHandleAccessModeAndVarTypeChecks(invoke, codegen, slow_path, type);
  }
  GenerateVarHandleCoordinateChecks(invoke, codegen, slow_path);

  return slow_path;
}

struct VarHandleTarget {
  XRegister object;  // The object holding the value to operate on.
  XRegister offset;  // The offset of the value to operate on.
};

static VarHandleTarget GetVarHandleTarget(HInvoke* invoke) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  LocationSummary* locations = invoke->GetLocations();

  VarHandleTarget target;
  // The temporary allocated for loading the offset.
  target.offset = locations->GetTemp(0u).AsRegister<XRegister>();
  // The reference to the object that holds the value to operate on.
  target.object = (expected_coordinates_count == 0u)
      ? locations->GetTemp(1u).AsRegister<XRegister>()
      : locations->InAt(1).AsRegister<XRegister>();
  return target;
}

static void GenerateVarHandleTarget(HInvoke* invoke,
                                    const VarHandleTarget& target,
                                    CodeGeneratorRISCV64* codegen) {
  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister varhandle = locations->InAt(0).AsRegister<XRegister>();
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);

  if (expected_coordinates_count <= 1u) {
    if (VarHandleOptimizations(invoke).GetUseKnownBootImageVarHandle()) {
      ScopedObjectAccess soa(Thread::Current());
      ArtField* target_field = GetBootImageVarHandleField(invoke);
      if (expected_coordinates_count == 0u) {
        ObjPtr<mirror::Class> declaring_class = target_field->GetDeclaringClass();
        if (Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(declaring_class)) {
          uint32_t boot_image_offset = CodeGenerator::GetBootImageOffset(declaring_class);
          codegen->LoadBootImageRelRoEntry(target.object, boot_image_offset);
        } else {
          codegen->LoadTypeForBootImageIntrinsic(
              target.object,
              TypeReference(&declaring_class->GetDexFile(), declaring_class->GetDexTypeIndex()));
        }
      }
      __ Li(target.offset, target_field->GetOffset().Uint32Value());
    } else {
      // For static fields, we need to fill the `target.object` with the declaring class,
      // so we can use `target.object` as temporary for the `ArtField*`. For instance fields,
      // we do not need the declaring class, so we can forget the `ArtField*` when
      // we load the `target.offset`, so use the `target.offset` to hold the `ArtField*`.
      XRegister field = (expected_coordinates_count == 0) ? target.object : target.offset;

      const MemberOffset art_field_offset = mirror::FieldVarHandle::ArtFieldOffset();
      const MemberOffset offset_offset = ArtField::OffsetOffset();

      // Load the ArtField*, the offset and, if needed, declaring class.
      __ Loadd(field, varhandle, art_field_offset.Int32Value());
      __ Loadwu(target.offset, field, offset_offset.Int32Value());
      if (expected_coordinates_count == 0u) {
        codegen->GenerateGcRootFieldLoad(
            invoke,
            Location::RegisterLocation(target.object),
            field,
            ArtField::DeclaringClassOffset().Int32Value(),
            codegen->GetCompilerReadBarrierOption());
      }
    }
  } else {
    DCHECK_EQ(expected_coordinates_count, 2u);
    DataType::Type value_type =
        GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
    MemberOffset data_offset = mirror::Array::DataOffset(DataType::Size(value_type));

    XRegister index = locations->InAt(2).AsRegister<XRegister>();
    __ Li(target.offset, data_offset.Int32Value());
    codegen->GetInstructionVisitor()->ShNAdd(target.offset, index, target.offset, value_type);
  }
}

static LocationSummary* CreateVarHandleCommonLocations(HInvoke* invoke,
                                                       CodeGeneratorRISCV64* codegen) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  DataType::Type return_type = invoke->GetType();

  ArenaAllocator* allocator = invoke->GetBlock()->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  // Require coordinates in registers. These are the object holding the value
  // to operate on (except for static fields) and index (for arrays and views).
  for (size_t i = 0; i != expected_coordinates_count; ++i) {
    locations->SetInAt(/* VarHandle object */ 1u + i, Location::RequiresRegister());
  }
  if (return_type != DataType::Type::kVoid) {
    if (DataType::IsFloatingPointType(return_type)) {
      locations->SetOut(Location::RequiresFpuRegister());
    } else {
      locations->SetOut(Location::RequiresRegister());
    }
  }
  uint32_t arguments_start = /* VarHandle object */ 1u + expected_coordinates_count;
  uint32_t number_of_arguments = invoke->GetNumberOfArguments();
  for (size_t arg_index = arguments_start; arg_index != number_of_arguments; ++arg_index) {
    HInstruction* arg = invoke->InputAt(arg_index);
    if (IsZeroBitPattern(arg)) {
      locations->SetInAt(arg_index, Location::ConstantLocation(arg));
    } else if (DataType::IsFloatingPointType(arg->GetType())) {
      locations->SetInAt(arg_index, Location::RequiresFpuRegister());
    } else {
      locations->SetInAt(arg_index, Location::RequiresRegister());
    }
  }

  // Add a temporary for offset.
  if (codegen->EmitNonBakerReadBarrier() &&
      GetExpectedVarHandleCoordinatesCount(invoke) == 0u) {  // For static fields.
    // To preserve the offset value across the non-Baker read barrier slow path
    // for loading the declaring class, use a fixed callee-save register.
    constexpr int first_callee_save = CTZ(kRiscv64CalleeSaveRefSpills);
    locations->AddTemp(Location::RegisterLocation(first_callee_save));
  } else {
    locations->AddTemp(Location::RequiresRegister());
  }
  if (expected_coordinates_count == 0u) {
    // Add a temporary to hold the declaring class.
    locations->AddTemp(Location::RequiresRegister());
  }

  return locations;
}

static void CreateVarHandleGetLocations(HInvoke* invoke, CodeGeneratorRISCV64* codegen) {
  VarHandleOptimizations optimizations(invoke);
  if (optimizations.GetDoNotIntrinsify()) {
    return;
  }

  if (codegen->EmitNonBakerReadBarrier() &&
      invoke->GetType() == DataType::Type::kReference &&
      invoke->GetIntrinsic() != Intrinsics::kVarHandleGet &&
      invoke->GetIntrinsic() != Intrinsics::kVarHandleGetOpaque) {
    // Unsupported for non-Baker read barrier because the artReadBarrierSlow() ignores
    // the passed reference and reloads it from the field. This gets the memory visibility
    // wrong for Acquire/Volatile operations. b/173104084
    return;
  }

  CreateVarHandleCommonLocations(invoke, codegen);
}

DataType::Type IntTypeForFloatingPointType(DataType::Type fp_type) {
  DCHECK(DataType::IsFloatingPointType(fp_type));
  return (fp_type == DataType::Type::kFloat32) ? DataType::Type::kInt32 : DataType::Type::kInt64;
}

static void GenerateVarHandleGet(HInvoke* invoke,
                                 CodeGeneratorRISCV64* codegen,
                                 std::memory_order order,
                                 bool byte_swap = false) {
  DataType::Type type = invoke->GetType();
  DCHECK_NE(type, DataType::Type::kVoid);

  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = codegen->GetAssembler();
  Location out = locations->Out();

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathRISCV64* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, type);
    GenerateVarHandleTarget(invoke, target, codegen);
    if (slow_path != nullptr) {
      __ Bind(slow_path->GetNativeByteOrderLabel());
    }
  }

  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(acquire_barrier || order == std::memory_order_relaxed);

  if (seq_cst_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }

  // Load the value from the target location.
  if (type == DataType::Type::kReference && codegen->EmitBakerReadBarrier()) {
    // TODO(riscv64): Revisit when we add checking if the holder is black.
    Location index_and_temp_loc = Location::RegisterLocation(target.offset);
    codegen->GenerateReferenceLoadWithBakerReadBarrier(invoke,
                                                       out,
                                                       target.object,
                                                       /*offset=*/ 0,
                                                       index_and_temp_loc,
                                                       index_and_temp_loc,
                                                       /*needs_null_check=*/ false);
    DCHECK(!byte_swap);
  } else {
    ScratchRegisterScope srs(assembler);
    XRegister address = srs.AllocateXRegister();
    __ Add(address, target.object, target.offset);
    Location load_loc = out;
    DataType::Type load_type = type;
    if (byte_swap && DataType::IsFloatingPointType(type)) {
      load_loc = Location::RegisterLocation(target.offset);  // Load to the offset temporary.
      load_type = IntTypeForFloatingPointType(type);
    }
    codegen->GetInstructionVisitor()->Load(load_loc, address, /*offset=*/ 0, load_type);
    if (type == DataType::Type::kReference) {
      DCHECK(!byte_swap);
      Location object_loc = Location::RegisterLocation(target.object);
      Location offset_loc = Location::RegisterLocation(target.offset);
      codegen->MaybeGenerateReadBarrierSlow(
          invoke, out, out, object_loc, /*offset=*/ 0u, /*index=*/ offset_loc);
    } else if (byte_swap) {
      GenerateReverseBytes(assembler, out, load_loc.AsRegister<XRegister>(), type);
    }
  }

  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
  }

  if (slow_path != nullptr) {
    DCHECK(!byte_swap);
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleGet(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleGet(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_relaxed);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleGetOpaque(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleGetOpaque(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_relaxed);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleGetAcquire(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleGetAcquire(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_acquire);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleGetVolatile(HInvoke* invoke) {
  CreateVarHandleGetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleGetVolatile(HInvoke* invoke) {
  GenerateVarHandleGet(invoke, codegen_, std::memory_order_seq_cst);
}

static void CreateVarHandleSetLocations(HInvoke* invoke, CodeGeneratorRISCV64* codegen) {
  VarHandleOptimizations optimizations(invoke);
  if (optimizations.GetDoNotIntrinsify()) {
    return;
  }

  CreateVarHandleCommonLocations(invoke, codegen);
}

static void GenerateVarHandleSet(HInvoke* invoke,
                                 CodeGeneratorRISCV64* codegen,
                                 std::memory_order order,
                                 bool byte_swap = false) {
  uint32_t value_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, value_index);

  Riscv64Assembler* assembler = codegen->GetAssembler();
  Location value = invoke->GetLocations()->InAt(value_index);

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathRISCV64* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, value_type);
    GenerateVarHandleTarget(invoke, target, codegen);
    if (slow_path != nullptr) {
      __ Bind(slow_path->GetNativeByteOrderLabel());
    }
  }

  {
    ScratchRegisterScope srs(assembler);
    XRegister address = srs.AllocateXRegister();
    __ Add(address, target.object, target.offset);

    if (byte_swap) {
      DCHECK(!value.IsConstant());  // Zero uses the main path as it does not need a byte swap.
      // The offset is no longer needed, so reuse the offset temporary for the byte-swapped value.
      Location new_value = Location::RegisterLocation(target.offset);
      if (DataType::IsFloatingPointType(value_type)) {
        value_type = IntTypeForFloatingPointType(value_type);
        codegen->MoveLocation(new_value, value, value_type);
        value = new_value;
      }
      GenerateReverseBytes(assembler, new_value, value.AsRegister<XRegister>(), value_type);
      value = new_value;
    }

    if (order == std::memory_order_seq_cst) {
      codegen->GetInstructionVisitor()->StoreSeqCst(value, address, /*offset=*/ 0, value_type);
    } else {
      if (order == std::memory_order_release) {
        codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyStore);
      } else {
        DCHECK(order == std::memory_order_relaxed);
      }
      codegen->GetInstructionVisitor()->Store(value, address, /*offset=*/ 0, value_type);
    }
  }

  if (CodeGenerator::StoreNeedsWriteBarrier(value_type, invoke->InputAt(value_index))) {
    codegen->MarkGCCard(target.object, value.AsRegister<XRegister>(), /* emit_null_check= */ true);
  }

  if (slow_path != nullptr) {
    DCHECK(!byte_swap);
    __ Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleSet(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleSet(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_relaxed);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleSetOpaque(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleSetOpaque(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_relaxed);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleSetRelease(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleSetRelease(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_release);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleSetVolatile(HInvoke* invoke) {
  CreateVarHandleSetLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleSetVolatile(HInvoke* invoke) {
  GenerateVarHandleSet(invoke, codegen_, std::memory_order_seq_cst);
}

static bool ScratchXRegisterNeeded(Location loc, DataType::Type type, bool byte_swap) {
  if (loc.IsConstant()) {
    DCHECK(loc.GetConstant()->IsZeroBitPattern());
    return false;
  }
  return DataType::IsFloatingPointType(type) || DataType::Size(type) < 4u || byte_swap;
}

static void CreateVarHandleCompareAndSetOrExchangeLocations(HInvoke* invoke,
                                                            CodeGeneratorRISCV64* codegen,
                                                            bool return_success) {
  VarHandleOptimizations optimizations(invoke);
  if (optimizations.GetDoNotIntrinsify()) {
    return;
  }

  uint32_t expected_index = invoke->GetNumberOfArguments() - 2;
  uint32_t new_value_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, new_value_index);
  DCHECK_EQ(value_type, GetDataTypeFromShorty(invoke, expected_index));

  if (value_type == DataType::Type::kReference && codegen->EmitNonBakerReadBarrier()) {
    // Unsupported for non-Baker read barrier because the artReadBarrierSlow() ignores
    // the passed reference and reloads it from the field. This breaks the read barriers
    // in slow path in different ways. The marked old value may not actually be a to-space
    // reference to the same object as `old_value`, breaking slow path assumptions. And
    // for CompareAndExchange, marking the old value after comparison failure may actually
    // return the reference to `expected`, erroneously indicating success even though we
    // did not set the new value. (And it also gets the memory visibility wrong.) b/173104084
    return;
  }

  LocationSummary* locations = CreateVarHandleCommonLocations(invoke, codegen);
  DCHECK_EQ(expected_index, 1u + GetExpectedVarHandleCoordinatesCount(invoke));

  if (codegen->EmitNonBakerReadBarrier()) {
    // We need callee-save registers for both the class object and offset instead of
    // the temporaries reserved in CreateVarHandleCommonLocations().
    static_assert(POPCOUNT(kRiscv64CalleeSaveRefSpills) >= 2u);
    uint32_t first_callee_save = CTZ(kRiscv64CalleeSaveRefSpills);
    uint32_t second_callee_save = CTZ(kRiscv64CalleeSaveRefSpills ^ (1u << first_callee_save));
    if (expected_index == 1u) {  // For static fields.
      DCHECK_EQ(locations->GetTempCount(), 2u);
      DCHECK(locations->GetTemp(0u).Equals(Location::RequiresRegister()));
      DCHECK(locations->GetTemp(1u).Equals(Location::RegisterLocation(first_callee_save)));
      locations->SetTempAt(0u, Location::RegisterLocation(second_callee_save));
    } else {
      DCHECK_EQ(locations->GetTempCount(), 1u);
      DCHECK(locations->GetTemp(0u).Equals(Location::RequiresRegister()));
      locations->SetTempAt(0u, Location::RegisterLocation(first_callee_save));
    }
  }

  if (value_type == DataType::Type::kReference && codegen->EmitReadBarrier()) {
    // Add a temporary for the `old_value_temp` in the slow path, `tmp_ptr` is scratch register.
    locations->AddTemp(Location::RequiresRegister());
  } else {
    Location expected = locations->InAt(expected_index);
    Location new_value = locations->InAt(new_value_index);
    size_t data_size = DataType::Size(value_type);
    bool small = (data_size < 4u);
    bool byte_swap =
        (expected_index == 3u) && (value_type != DataType::Type::kReference && data_size != 1u);
    bool fp = DataType::IsFloatingPointType(value_type);
    size_t temps_needed =
        // The offset temp is used for the `tmp_ptr`.
        1u +
        // For small values, we need a temp for the `mask`, `masked` and maybe also for the `shift`.
        (small ? (return_success ? 2u : 3u) : 0u) +
        // Some cases need modified copies of `new_value` and `expected`.
        (ScratchXRegisterNeeded(expected, value_type, byte_swap) ? 1u : 0u) +
        (ScratchXRegisterNeeded(new_value, value_type, byte_swap) ? 1u : 0u) +
        // We need a scratch register either for the old value or for the result of SC.
        // If we need to return a floating point old value, we need a temp for each.
        ((!return_success && fp) ? 2u : 1u);
    size_t scratch_registers_available = 2u;
    DCHECK_EQ(scratch_registers_available,
              ScratchRegisterScope(codegen->GetAssembler()).AvailableXRegisters());
    size_t old_temp_count = locations->GetTempCount();
    DCHECK_EQ(old_temp_count, (expected_index == 1u) ? 2u : 1u);
    if (temps_needed > old_temp_count + scratch_registers_available) {
      locations->AddRegisterTemps(temps_needed - (old_temp_count + scratch_registers_available));
    }
  }
}

static XRegister PrepareXRegister(CodeGeneratorRISCV64* codegen,
                                  Location loc,
                                  DataType::Type type,
                                  XRegister shift,
                                  XRegister mask,
                                  bool byte_swap,
                                  ScratchRegisterScope* srs) {
  DCHECK_EQ(shift == kNoXRegister, mask == kNoXRegister);
  DCHECK_EQ(shift == kNoXRegister, DataType::Size(type) >= 4u);
  if (loc.IsConstant()) {
    // The `shift`/`mask` and `byte_swap` are irrelevant for zero input.
    DCHECK(loc.GetConstant()->IsZeroBitPattern());
    return Zero;
  }

  Location result = loc;
  if (DataType::IsFloatingPointType(type)) {
    type = IntTypeForFloatingPointType(type);
    result = Location::RegisterLocation(srs->AllocateXRegister());
    codegen->MoveLocation(result, loc, type);
    loc = result;
  } else if (byte_swap || shift != kNoXRegister) {
    result = Location::RegisterLocation(srs->AllocateXRegister());
  }
  if (byte_swap) {
    if (type == DataType::Type::kInt16) {
      type = DataType::Type::kUint16;  // Do the masking as part of the byte swap.
    }
    GenerateReverseBytes(codegen->GetAssembler(), result, loc.AsRegister<XRegister>(), type);
    loc = result;
  }
  if (shift != kNoXRegister) {
    Riscv64Assembler* assembler = codegen->GetAssembler();
    __ Sllw(result.AsRegister<XRegister>(), loc.AsRegister<XRegister>(), shift);
    DCHECK_NE(type, DataType::Type::kUint8);
    if (type != DataType::Type::kUint16 && type != DataType::Type::kBool) {
      __ And(result.AsRegister<XRegister>(), result.AsRegister<XRegister>(), mask);
    }
  }
  return result.AsRegister<XRegister>();
}

static void GenerateVarHandleCompareAndSetOrExchange(HInvoke* invoke,
                                                     CodeGeneratorRISCV64* codegen,
                                                     std::memory_order order,
                                                     bool return_success,
                                                     bool strong,
                                                     bool byte_swap = false) {
  DCHECK(return_success || strong);

  uint32_t expected_index = invoke->GetNumberOfArguments() - 2;
  uint32_t new_value_index = invoke->GetNumberOfArguments() - 1;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, new_value_index);
  DCHECK_EQ(value_type, GetDataTypeFromShorty(invoke, expected_index));

  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location expected = locations->InAt(expected_index);
  Location new_value = locations->InAt(new_value_index);
  Location out = locations->Out();

  VarHandleTarget target = GetVarHandleTarget(invoke);
  VarHandleSlowPathRISCV64* slow_path = nullptr;
  if (!byte_swap) {
    slow_path = GenerateVarHandleChecks(invoke, codegen, order, value_type);
    GenerateVarHandleTarget(invoke, target, codegen);
    if (slow_path != nullptr) {
      slow_path->SetCompareAndSetOrExchangeArgs(return_success, strong);
      __ Bind(slow_path->GetNativeByteOrderLabel());
    }
  }

  // This needs to be before we allocate the scratch registers, as MarkGCCard also uses them.
  if (CodeGenerator::StoreNeedsWriteBarrier(value_type, invoke->InputAt(new_value_index))) {
    // Mark card for object assuming new value is stored.
    bool new_value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(target.object, new_value.AsRegister<XRegister>(), new_value_can_be_null);
  }

  // Scratch registers may be needed for `new_value` and `expected`.
  ScratchRegisterScope srs(assembler);
  DCHECK_EQ(srs.AvailableXRegisters(), 2u);
  size_t available_scratch_registers =
      (ScratchXRegisterNeeded(expected, value_type, byte_swap) ? 0u : 1u) +
      (ScratchXRegisterNeeded(new_value, value_type, byte_swap) ? 0u : 1u);

  // Reuse the `offset` temporary for the pointer to the target location,
  // except for references that need the offset for the read barrier.
  DCHECK_EQ(target.offset, locations->GetTemp(0u).AsRegister<XRegister>());
  size_t next_temp = 1u;
  XRegister tmp_ptr = target.offset;
  if (value_type == DataType::Type::kReference && codegen->EmitReadBarrier()) {
    DCHECK_EQ(available_scratch_registers, 2u);
    available_scratch_registers -= 1u;
    DCHECK_EQ(expected_index, 1u + GetExpectedVarHandleCoordinatesCount(invoke));
    next_temp = expected_index == 1u ? 2u : 1u;  // Preserve the class register for static field.
    tmp_ptr = srs.AllocateXRegister();
  }
  __ Add(tmp_ptr, target.object, target.offset);

  auto get_temp = [&]() {
    if (available_scratch_registers != 0u) {
      available_scratch_registers -= 1u;
      return srs.AllocateXRegister();
    } else {
      XRegister temp = locations->GetTemp(next_temp).AsRegister<XRegister>();
      next_temp += 1u;
      return temp;
    }
  };

  XRegister shift = kNoXRegister;
  XRegister mask = kNoXRegister;
  XRegister masked = kNoXRegister;
  size_t data_size = DataType::Size(value_type);
  if (data_size < 4u) {
    // When returning "success" and not the old value, we shall not need the `shift` after
    // the raw CAS operation, so use the output register as a temporary here.
    shift = return_success ? locations->Out().AsRegister<XRegister>() : get_temp();
    mask = get_temp();
    masked = get_temp();
    __ Andi(shift, tmp_ptr, 3);
    __ Andi(tmp_ptr, tmp_ptr, -4);
    __ Slli(shift, shift, WhichPowerOf2(kBitsPerByte));
    __ Li(mask, (1 << (data_size * kBitsPerByte)) - 1);
    __ Sllw(mask, mask, shift);
  }

  // Move floating point values to scratch registers and apply shift, mask and byte swap if needed.
  // Note that float/double CAS uses bitwise comparison, rather than the operator==.
  XRegister expected_reg =
      PrepareXRegister(codegen, expected, value_type, shift, mask, byte_swap, &srs);
  XRegister new_value_reg =
      PrepareXRegister(codegen, new_value, value_type, shift, mask, byte_swap, &srs);
  DataType::Type cas_type = DataType::IsFloatingPointType(value_type)
      ? IntTypeForFloatingPointType(value_type)
      : (data_size >= 4u ? value_type : DataType::Type::kInt32);

  // Prepare registers for old value and the result of the store conditional.
  XRegister old_value;
  XRegister store_result;
  if (return_success) {
    // Use a temp for the old value and the output register for the store conditional result.
    old_value = get_temp();
    store_result = out.AsRegister<XRegister>();
  } else if (DataType::IsFloatingPointType(value_type)) {
    // We need two temporary registers.
    old_value = get_temp();
    store_result = get_temp();
  } else {
    // Use the output register for the old value and a temp for the store conditional result.
    old_value = out.AsRegister<XRegister>();
    store_result = get_temp();
  }

  Riscv64Label exit_loop_label;
  Riscv64Label* exit_loop = &exit_loop_label;
  Riscv64Label* cmp_failure = &exit_loop_label;

  if (value_type == DataType::Type::kReference && codegen->EmitReadBarrier()) {
    // The `old_value_temp` is used first for marking the `old_value` and then for the unmarked
    // reloaded old value for subsequent CAS in the slow path. It cannot be a scratch register.
    XRegister old_value_temp = locations->GetTemp(next_temp).AsRegister<XRegister>();
    ++next_temp;
    // If we are returning the old value rather than the success,
    // use a scratch register for the store result in the slow path.
    XRegister slow_path_store_result = return_success ? store_result : kNoXRegister;
    ReadBarrierCasSlowPathRISCV64* rb_slow_path =
        new (codegen->GetScopedAllocator()) ReadBarrierCasSlowPathRISCV64(
            invoke,
            order,
            strong,
            target.object,
            target.offset,
            expected_reg,
            new_value_reg,
            old_value,
            old_value_temp,
            slow_path_store_result,
            /*update_old_value=*/ !return_success,
            codegen);
    codegen->AddSlowPath(rb_slow_path);
    exit_loop = rb_slow_path->GetExitLabel();
    cmp_failure = rb_slow_path->GetEntryLabel();
  }

  if (return_success) {
    // Pre-populate the result register with failure for the case when the old value
    // differs and we do not execute the store conditional.
    __ Li(store_result, 0);
  }
  GenerateCompareAndSet(codegen->GetAssembler(),
                        cas_type,
                        order,
                        strong,
                        cmp_failure,
                        tmp_ptr,
                        new_value_reg,
                        old_value,
                        mask,
                        masked,
                        store_result,
                        expected_reg);
  if (return_success && strong) {
    // Load success value to the result register.
    // `GenerateCompareAndSet()` does not emit code to indicate success for a strong CAS.
    __ Li(store_result, 1);
  }
  __ Bind(exit_loop);

  if (return_success) {
    // Nothing to do, the result register already contains 1 on success and 0 on failure.
  } else if (byte_swap) {
    // Do not apply shift in `GenerateReverseBytes()` for small types.
    DataType::Type swap_type = data_size < 4u ? DataType::Type::kInt32 : value_type;
    // Also handles moving to FP registers.
    GenerateReverseBytes(assembler, out, old_value, swap_type);
    if (data_size < 4u) {
      DCHECK(Location::RegisterLocation(old_value).Equals(out));
      __ Sllw(old_value, old_value, shift);
      if (value_type == DataType::Type::kUint16) {
        __ Srliw(old_value, old_value, 16);
      } else {
        DCHECK_EQ(value_type, DataType::Type::kInt16);
        __ Sraiw(old_value, old_value, 16);
      }
    }
  } else if (DataType::IsFloatingPointType(value_type)) {
    codegen->MoveLocation(out, Location::RegisterLocation(old_value), value_type);
  } else if (data_size < 4u) {
    __ Srl(old_value, masked, shift);
    if (value_type == DataType::Type::kInt8) {
      __ SextB(old_value, old_value);
    } else if (value_type == DataType::Type::kInt16) {
      __ SextH(old_value, old_value);
    }
  }

  if (slow_path != nullptr) {
    DCHECK(!byte_swap);
    __ Bind(slow_path->GetExitLabel());
  }

  // Check that we have allocated the right number of temps. We may need more registers
  // for byte swapped CAS in the slow path, so skip this check for the main path in that case.
  bool has_byte_swap =
      (expected_index == 3u) && (value_type != DataType::Type::kReference && data_size != 1u);
  if ((!has_byte_swap || byte_swap) && next_temp != locations->GetTempCount()) {
    // We allocate a temporary register for the class object for a static field `VarHandle` but
    // we do not update the `next_temp` if it's otherwise unused after the address calculation.
    CHECK_EQ(expected_index, 1u);
    CHECK_EQ(next_temp, 1u);
    CHECK_EQ(locations->GetTempCount(), 2u);
  }
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleCompareAndExchange(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleCompareAndExchange(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleCompareAndExchangeAcquire(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleCompareAndExchangeAcquire(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_acquire, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleCompareAndExchangeRelease(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ false);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleCompareAndExchangeRelease(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_release, /*return_success=*/ false, /*strong=*/ true);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleCompareAndSet(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleCompareAndSet(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ true);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleWeakCompareAndSet(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleWeakCompareAndSet(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleWeakCompareAndSetAcquire(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleWeakCompareAndSetAcquire(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_acquire, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleWeakCompareAndSetPlain(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleWeakCompareAndSetPlain(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_relaxed, /*return_success=*/ true, /*strong=*/ false);
}

void IntrinsicLocationsBuilderRISCV64::VisitVarHandleWeakCompareAndSetRelease(HInvoke* invoke) {
  CreateVarHandleCompareAndSetOrExchangeLocations(invoke, codegen_, /*return_success=*/ true);
}

void IntrinsicCodeGeneratorRISCV64::VisitVarHandleWeakCompareAndSetRelease(HInvoke* invoke) {
  GenerateVarHandleCompareAndSetOrExchange(
      invoke, codegen_, std::memory_order_release, /*return_success=*/ true, /*strong=*/ false);
}

static void GenerateVarHandleGetAndUpdate(HInvoke* invoke,
                                          CodeGeneratorRISCV64* codegen,
                                          GetAndUpdateOp get_and_update_op,
                                          std::memory_order order,
                                          bool byte_swap = false) {
  UNUSED(invoke, codegen, get_and_update_op, order, byte_swap);
  LOG(FATAL) << "Unimplemented!";
}

void VarHandleSlowPathRISCV64::EmitByteArrayViewCode(CodeGenerator* codegen_in) {
  DCHECK(GetByteArrayViewCheckLabel()->IsLinked());
  CodeGeneratorRISCV64* codegen = down_cast<CodeGeneratorRISCV64*>(codegen_in);
  Riscv64Assembler* assembler = codegen->GetAssembler();
  HInvoke* invoke = GetInvoke();
  mirror::VarHandle::AccessModeTemplate access_mode_template = GetAccessModeTemplate();
  DataType::Type value_type =
      GetVarHandleExpectedValueType(invoke, /*expected_coordinates_count=*/ 2u);
  DCHECK_NE(value_type, DataType::Type::kReference);
  size_t size = DataType::Size(value_type);
  DCHECK_GT(size, 1u);
  LocationSummary* locations = invoke->GetLocations();
  XRegister varhandle = locations->InAt(0).AsRegister<XRegister>();
  XRegister object = locations->InAt(1).AsRegister<XRegister>();
  XRegister index = locations->InAt(2).AsRegister<XRegister>();

  MemberOffset class_offset = mirror::Object::ClassOffset();
  MemberOffset array_length_offset = mirror::Array::LengthOffset();
  MemberOffset data_offset = mirror::Array::DataOffset(Primitive::kPrimByte);
  MemberOffset native_byte_order_offset = mirror::ByteArrayViewVarHandle::NativeByteOrderOffset();

  __ Bind(GetByteArrayViewCheckLabel());

  VarHandleTarget target = GetVarHandleTarget(invoke);
  {
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();
    XRegister temp2 = srs.AllocateXRegister();

    // The main path checked that the coordinateType0 is an array class that matches
    // the class of the actual coordinate argument but it does not match the value type.
    // Check if the `varhandle` references a ByteArrayViewVarHandle instance.
    __ Loadwu(temp, varhandle, class_offset.Int32Value());
    codegen->MaybeUnpoisonHeapReference(temp);
    codegen->LoadClassRootForIntrinsic(temp2, ClassRoot::kJavaLangInvokeByteArrayViewVarHandle);
    __ Bne(temp, temp2, GetEntryLabel());

    // Check for array index out of bounds.
    __ Loadw(temp, object, array_length_offset.Int32Value());
    __ Bgeu(index, temp, GetEntryLabel());
    __ Addi(temp2, index, size - 1u);
    __ Bgeu(temp2, temp, GetEntryLabel());

    // Construct the target.
    __ Addi(target.offset, index, data_offset.Int32Value());

    // Alignment check. For unaligned access, go to the runtime.
    DCHECK(IsPowerOfTwo(size));
    __ Andi(temp, target.offset, size - 1u);
    __ Bnez(temp, GetEntryLabel());

    // Byte order check. For native byte order return to the main path.
    if (access_mode_template == mirror::VarHandle::AccessModeTemplate::kSet &&
        IsZeroBitPattern(invoke->InputAt(invoke->GetNumberOfArguments() - 1u))) {
      // There is no reason to differentiate between native byte order and byte-swap
      // for setting a zero bit pattern. Just return to the main path.
      __ J(GetNativeByteOrderLabel());
      return;
    }
    __ Loadbu(temp, varhandle, native_byte_order_offset.Int32Value());
    __ Bnez(temp, GetNativeByteOrderLabel());
  }

  switch (access_mode_template) {
    case mirror::VarHandle::AccessModeTemplate::kGet:
      GenerateVarHandleGet(invoke, codegen, order_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kSet:
      GenerateVarHandleSet(invoke, codegen, order_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kCompareAndSet:
    case mirror::VarHandle::AccessModeTemplate::kCompareAndExchange:
      GenerateVarHandleCompareAndSetOrExchange(
          invoke, codegen, order_, return_success_, strong_, /*byte_swap=*/ true);
      break;
    case mirror::VarHandle::AccessModeTemplate::kGetAndUpdate:
      GenerateVarHandleGetAndUpdate(
          invoke, codegen, get_and_update_op_, order_, /*byte_swap=*/ true);
      break;
  }
  __ J(GetExitLabel());
}

void IntrinsicLocationsBuilderRISCV64::VisitThreadCurrentThread(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorRISCV64::VisitThreadCurrentThread(HInvoke* invoke) {
  Riscv64Assembler* assembler = GetAssembler();
  XRegister out = invoke->GetLocations()->Out().AsRegister<XRegister>();
  __ Loadwu(out, TR, Thread::PeerOffset<kRiscv64PointerSize>().Int32Value());
}

void IntrinsicLocationsBuilderRISCV64::VisitReachabilityFence(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::Any());
}

void IntrinsicCodeGeneratorRISCV64::VisitReachabilityFence([[maybe_unused]] HInvoke* invoke) {}

static void GenerateDivideUnsigned(HInvoke* invoke, CodeGeneratorRISCV64* codegen) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = codegen->GetAssembler();
  DataType::Type type = invoke->GetType();
  DCHECK(type == DataType::Type::kInt32 || type == DataType::Type::kInt64);

  XRegister dividend = locations->InAt(0).AsRegister<XRegister>();
  XRegister divisor = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  // Check if divisor is zero, bail to managed implementation to handle.
  SlowPathCodeRISCV64* slow_path =
      new (codegen->GetScopedAllocator()) IntrinsicSlowPathRISCV64(invoke);
  codegen->AddSlowPath(slow_path);
  __ Beqz(divisor, slow_path->GetEntryLabel());

  if (type == DataType::Type::kInt32) {
    __ Divuw(out, dividend, divisor);
  } else {
    __ Divu(out, dividend, divisor);
  }

  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderRISCV64::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  GenerateDivideUnsigned(invoke, codegen_);
}

void IntrinsicLocationsBuilderRISCV64::VisitLongDivideUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongDivideUnsigned(HInvoke* invoke) {
  GenerateDivideUnsigned(invoke, codegen_);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathFmaDouble(HInvoke* invoke) {
  CreateFpFpFpToFpNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathFmaDouble(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  FRegister n = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister m = locations->InAt(1).AsFpuRegister<FRegister>();
  FRegister a = locations->InAt(2).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  __ FMAddD(out, n, m, a);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathFmaFloat(HInvoke* invoke) {
  CreateFpFpFpToFpNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathFmaFloat(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  FRegister n = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister m = locations->InAt(1).AsFpuRegister<FRegister>();
  FRegister a = locations->InAt(2).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  __ FMAddS(out, n, m, a);
}


void IntrinsicLocationsBuilderRISCV64::VisitMathCos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathCos(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCos, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathSin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathSin(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickSin, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathAcos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathAcos(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAcos, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathAsin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathAsin(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAsin, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathAtan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathAtan(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAtan, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathAtan2(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathAtan2(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAtan2, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathPow(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathPow(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickPow, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathCbrt(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathCbrt(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCbrt, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathCosh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathCosh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCosh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathExp(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathExp(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickExp, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathExpm1(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathExpm1(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickExpm1, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathHypot(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathHypot(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickHypot, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathLog(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathLog(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickLog, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathLog10(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathLog10(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickLog10, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathNextAfter(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathNextAfter(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickNextAfter, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathSinh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathSinh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickSinh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathTan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathTan(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickTan, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathTanh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathTanh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickTanh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderRISCV64::VisitMathSqrt(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathSqrt(HInvoke* invoke) {
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kFloat64);

  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  __ FSqrtD(out, in);
}

static void GenDoubleRound(Riscv64Assembler* assembler, HInvoke* invoke, FPRoundingMode mode) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  FRegister ftmp = srs.AllocateFRegister();
  Riscv64Label done;

  // Load 2^52
  __ LoadConst64(tmp, 0x4330000000000000L);
  __ FMvDX(ftmp, tmp);
  __ FAbsD(out, in);
  __ FLtD(tmp, out, ftmp);

  // Set output as the input if input greater than the max
  __ FMvD(out, in);
  __ Beqz(tmp, &done);

  // Convert with rounding mode
  __ FCvtLD(tmp, in, mode);
  __ FCvtDL(ftmp, tmp, mode);

  // Set the signed bit
  __ FSgnjD(out, ftmp, in);
  __ Bind(&done);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathFloor(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathFloor(HInvoke* invoke) {
  GenDoubleRound(GetAssembler(), invoke, FPRoundingMode::kRDN);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathCeil(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathCeil(HInvoke* invoke) {
  GenDoubleRound(GetAssembler(), invoke, FPRoundingMode::kRUP);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathRint(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathRint(HInvoke* invoke) {
  GenDoubleRound(GetAssembler(), invoke, FPRoundingMode::kRNE);
}

void GenMathRound(CodeGeneratorRISCV64* codegen, HInvoke* invoke, DataType::Type type) {
  Riscv64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  ScratchRegisterScope srs(assembler);
  FRegister ftmp = srs.AllocateFRegister();
  Riscv64Label done;

  // Check NaN
  codegen->GetInstructionVisitor()->FClass(out, in, type);
  __ Slti(out, out, kFClassNaNMinValue);
  __ Beqz(out, &done);

  if (type == DataType::Type::kFloat64) {
    // Add 0.5 (0x3fe0000000000000), rounding down (towards negative infinity).
    __ LoadConst64(out, 0x3fe0000000000000L);
    __ FMvDX(ftmp, out);
    __ FAddD(ftmp, ftmp, in, FPRoundingMode::kRDN);

    // Convert to managed `long`, rounding down (towards negative infinity).
    __ FCvtLD(out, ftmp, FPRoundingMode::kRDN);
  } else {
    // Add 0.5 (0x3f000000), rounding down (towards negative infinity).
    __ LoadConst32(out, 0x3f000000);
    __ FMvWX(ftmp, out);
    __ FAddS(ftmp, ftmp, in, FPRoundingMode::kRDN);

    // Convert to managed `int`, rounding down (towards negative infinity).
    __ FCvtWS(out, ftmp, FPRoundingMode::kRDN);
  }

  __ Bind(&done);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathRoundDouble(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathRoundDouble(HInvoke* invoke) {
  GenMathRound(codegen_, invoke, DataType::Type::kFloat64);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathRoundFloat(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitMathRoundFloat(HInvoke* invoke) {
  GenMathRound(codegen_, invoke, DataType::Type::kFloat32);
}

void IntrinsicLocationsBuilderRISCV64::VisitMathMultiplyHigh(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorRISCV64::VisitMathMultiplyHigh(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Riscv64Assembler* assembler = GetAssembler();
  DCHECK(invoke->GetType() == DataType::Type::kInt64);

  XRegister x = locations->InAt(0).AsRegister<XRegister>();
  XRegister y = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  // Get high 64 of the multiply
  __ Mulh(out, x, y);
}

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(RISCV64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(RISCV64)

}  // namespace riscv64
}  // namespace art
