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
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitIntegerReverseBytes(HInvoke* invoke) {
  GenerateReverseBytes(GetAssembler(), invoke, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorRISCV64::VisitLongReverseBytes(HInvoke* invoke) {
  GenerateReverseBytes(GetAssembler(), invoke, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderRISCV64::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
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

  // Note: The `type` can be `TMP`. Taken branches to `success` and `loop` should be near and never
  // expand. Only the branch to `slow_path` can theoretically expand and clobber `TMP` when taken.
  // (`TMP` is clobbered only if the target distance is at least 1MiB.)
  // FIXME(riscv64): Use "bare" branches. (And add some assembler tests for them.)
  __ Loadwu(temp, object, class_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp);
  Riscv64Label loop;
  __ Bind(&loop);
  __ Beq(type, temp, &success);
  // We may not have another scratch register for `Loadwu()`. Use `Lwu()` directly.
  DCHECK(IsInt<12>(super_class_offset.Int32Value()));
  __ Lwu(temp, temp, super_class_offset.Int32Value());
  codegen->MaybeUnpoisonHeapReference(temp);
  __ Beqz(temp, slow_path->GetEntryLabel());
  __ J(&loop);
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
        codegen->GetInstructionVisitor()->GenerateGcRootFieldLoad(
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
      load_type = (type == DataType::Type::kFloat32) ? DataType::Type::kInt32
                                                     : DataType::Type::kInt64;
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
        value_type = (value_type == DataType::Type::kFloat32) ? DataType::Type::kInt32
                                                              : DataType::Type::kInt64;
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

static void GenerateVarHandleCompareAndSetOrExchange(HInvoke* invoke,
                                                     CodeGeneratorRISCV64* codegen,
                                                     std::memory_order order,
                                                     bool return_success,
                                                     bool strong,
                                                     bool byte_swap = false) {
  UNUSED(invoke, codegen, order, return_success, strong, byte_swap);
  LOG(FATAL) << "Unimplemented!";
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

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(RISCV64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(RISCV64)

}  // namespace riscv64
}  // namespace art
