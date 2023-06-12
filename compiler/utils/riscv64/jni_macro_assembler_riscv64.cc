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

#include "jni_macro_assembler_riscv64.h"

#include "entrypoints/quick/quick_entrypoints.h"
#include "managed_register_riscv64.h"
#include "offsets.h"
#include "thread.h"

namespace art {
namespace riscv64 {

#define __ asm_.

Riscv64JNIMacroAssembler::~Riscv64JNIMacroAssembler() {
}

void Riscv64JNIMacroAssembler::FinalizeCode() {
  __ FinalizeCode();
}

void Riscv64JNIMacroAssembler::BuildFrame(size_t frame_size,
                                          ManagedRegister method_reg,
                                          ArrayRef<const ManagedRegister> callee_save_regs) {
  // TODO(riscv64): Implement this.
  UNUSED(frame_size, method_reg, callee_save_regs);
}

void Riscv64JNIMacroAssembler::RemoveFrame(size_t frame_size,
                                           ArrayRef<const ManagedRegister> callee_save_regs,
                                           bool may_suspend) {
  // TODO(riscv64): Implement this.
  UNUSED(frame_size, callee_save_regs, may_suspend);
}

void Riscv64JNIMacroAssembler::IncreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    CHECK_ALIGNED(adjust, kStackAlignment);
    __ AddConst64(SP, SP, -adjust);
    __ cfi().AdjustCFAOffset(adjust);
  }
}

void Riscv64JNIMacroAssembler::DecreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    CHECK_ALIGNED(adjust, kStackAlignment);
    __ AddConst64(SP, SP, adjust);
    __ cfi().AdjustCFAOffset(-adjust);
  }
}

ManagedRegister Riscv64JNIMacroAssembler::CoreRegisterWithSize(ManagedRegister src, size_t size) {
  DCHECK(src.AsRiscv64().IsXRegister());
  DCHECK(size == 4u || size == 8u) << size;
  return src;
}

void Riscv64JNIMacroAssembler::Store(FrameOffset offs, ManagedRegister m_src, size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(offs, m_src, size);
}

void Riscv64JNIMacroAssembler::Store(ManagedRegister base,
                                     MemberOffset offs,
                                     ManagedRegister m_src,
                                     size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(base, offs, m_src, size);
}

void Riscv64JNIMacroAssembler::StoreRawPtr(FrameOffset offs, ManagedRegister m_src) {
  // TODO(riscv64): Implement this.
  UNUSED(offs, m_src);
}

void Riscv64JNIMacroAssembler::StoreStackPointerToThread(ThreadOffset64 thr_offs, bool tag_sp) {
  // TODO(riscv64): Implement this.
  UNUSED(thr_offs, tag_sp);
}

void Riscv64JNIMacroAssembler::Load(ManagedRegister m_dest, FrameOffset src, size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(m_dest, src, size);
}

void Riscv64JNIMacroAssembler::Load(ManagedRegister m_dest,
                                    ManagedRegister m_base,
                                    MemberOffset offs,
                                    size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(m_dest, m_base, offs, size);
}

void Riscv64JNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister m_dest, ThreadOffset64 offs) {
  // TODO(riscv64): Implement this.
  UNUSED(m_dest, offs);
}

void Riscv64JNIMacroAssembler::MoveArguments(ArrayRef<ArgumentLocation> dests,
                                             ArrayRef<ArgumentLocation> srcs,
                                             ArrayRef<FrameOffset> refs) {
  // TODO(riscv64): Implement this.
  UNUSED(dests, srcs, refs);
}

void Riscv64JNIMacroAssembler::Move(ManagedRegister m_dest, ManagedRegister m_src, size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(m_dest, m_src, size);
}

void Riscv64JNIMacroAssembler::Move(ManagedRegister m_dest, size_t value) {
  // TODO(riscv64): Implement this.
  UNUSED(m_dest, value);
}

void Riscv64JNIMacroAssembler::SignExtend(ManagedRegister mreg, size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(mreg, size);
}

void Riscv64JNIMacroAssembler::ZeroExtend(ManagedRegister mreg, size_t size) {
  // TODO(riscv64): Implement this.
  UNUSED(mreg, size);
}

void Riscv64JNIMacroAssembler::GetCurrentThread(ManagedRegister tr) {
  // TODO(riscv64): Implement this.
  UNUSED(tr);
}

void Riscv64JNIMacroAssembler::GetCurrentThread(FrameOffset offset) {
  // TODO(riscv64): Implement this.
  UNUSED(offset);
}

void Riscv64JNIMacroAssembler::DecodeJNITransitionOrLocalJObject(ManagedRegister m_reg,
                                                                 JNIMacroLabel* slow_path,
                                                                 JNIMacroLabel* resume) {
  // TODO(riscv64): Implement this.
  UNUSED(m_reg, slow_path, resume);
}

void Riscv64JNIMacroAssembler::VerifyObject([[maybe_unused]] ManagedRegister m_src,
                                            [[maybe_unused]] bool could_be_null) {
  // TODO: not validating references.
}

void Riscv64JNIMacroAssembler::VerifyObject([[maybe_unused]] FrameOffset src,
                                            [[maybe_unused]] bool could_be_null) {
  // TODO: not validating references.
}

void Riscv64JNIMacroAssembler::Jump(ManagedRegister m_base, Offset offs) {
  Riscv64ManagedRegister base = m_base.AsRiscv64();
  CHECK(base.IsXRegister()) << base;
  XRegister scratch = TMP;
  __ Loadd(scratch, base.AsXRegister(), offs.Int32Value());
  __ Jr(scratch);
}

void Riscv64JNIMacroAssembler::Call(ManagedRegister m_base, Offset offs) {
  Riscv64ManagedRegister base = m_base.AsRiscv64();
  CHECK(base.IsXRegister()) << base;
  XRegister scratch = TMP;
  __ Loadd(scratch, base.AsXRegister(), offs.Int32Value());
  __ Jalr(scratch);
}


void Riscv64JNIMacroAssembler::CallFromThread(ThreadOffset64 offset) {
  Call(Riscv64ManagedRegister::FromXRegister(TR), offset);
}

void Riscv64JNIMacroAssembler::TryToTransitionFromRunnableToNative(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs) {
  // TODO(riscv64): Implement this.
  UNUSED(label, scratch_regs);
}

void Riscv64JNIMacroAssembler::TryToTransitionFromNativeToRunnable(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs,
    ManagedRegister return_reg) {
  // TODO(riscv64): Implement this.
  UNUSED(label, scratch_regs, return_reg);
}

void Riscv64JNIMacroAssembler::SuspendCheck(JNIMacroLabel* label) {
  // TODO(riscv64): Implement this.
  UNUSED(label);
}

void Riscv64JNIMacroAssembler::ExceptionPoll(JNIMacroLabel* label) {
  // TODO(riscv64): Implement this.
  UNUSED(label);
}

void Riscv64JNIMacroAssembler::DeliverPendingException() {
  // TODO(riscv64): Implement this.
}

std::unique_ptr<JNIMacroLabel> Riscv64JNIMacroAssembler::CreateLabel() {
  return std::unique_ptr<JNIMacroLabel>(new Riscv64JNIMacroLabel());
}

void Riscv64JNIMacroAssembler::Jump(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ J(down_cast<Riscv64Label*>(Riscv64JNIMacroLabel::Cast(label)->AsRiscv64()));
}

void Riscv64JNIMacroAssembler::TestGcMarking(JNIMacroLabel* label, JNIMacroUnaryCondition cond) {
  CHECK(label != nullptr);

  DCHECK_EQ(Thread::IsGcMarkingSize(), 4u);
  DCHECK(gUseReadBarrier);

  XRegister test_reg = TMP;
  int32_t is_gc_marking_offset = Thread::IsGcMarkingOffset<kArm64PointerSize>().Int32Value();
  __ Loadw(test_reg, TR, is_gc_marking_offset);
  switch (cond) {
    case JNIMacroUnaryCondition::kZero:
      __ Beqz(test_reg, down_cast<Riscv64Label*>(Riscv64JNIMacroLabel::Cast(label)->AsRiscv64()));
      break;
    case JNIMacroUnaryCondition::kNotZero:
      __ Bnez(test_reg, down_cast<Riscv64Label*>(Riscv64JNIMacroLabel::Cast(label)->AsRiscv64()));
      break;
    default:
      LOG(FATAL) << "Not implemented unary condition: " << static_cast<int>(cond);
      UNREACHABLE();
  }
}

void Riscv64JNIMacroAssembler::TestMarkBit(ManagedRegister ref,
                                           JNIMacroLabel* label,
                                           JNIMacroUnaryCondition cond) {
  // TODO(riscv64): Implement this.
  UNUSED(ref, label, cond);
}

void Riscv64JNIMacroAssembler::TestByteAndJumpIfNotZero(uintptr_t address, JNIMacroLabel* label) {
  XRegister test_reg = TMP;
  int32_t small_offset = dchecked_integral_cast<int32_t>(address & 0xfff) -
                         dchecked_integral_cast<int32_t>((address & 0x800) << 1);
  int32_t remainder = static_cast<int64_t>(address) - small_offset;
  __ Li(test_reg, remainder);
  __ Lb(test_reg, test_reg, small_offset);
  __ Bnez(test_reg, down_cast<Riscv64Label*>(Riscv64JNIMacroLabel::Cast(label)->AsRiscv64()));
}

void Riscv64JNIMacroAssembler::Bind(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ Bind(Riscv64JNIMacroLabel::Cast(label)->AsRiscv64());
}

#undef ___

}  // namespace riscv64
}  // namespace art
