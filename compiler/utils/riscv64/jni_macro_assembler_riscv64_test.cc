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

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <fstream>
#include <map>
#include <regex>

#include "gtest/gtest.h"

#include "jni/quick/calling_convention.h"
#include "utils/riscv64/jni_macro_assembler_riscv64.h"
#include "utils/assembler_test_base.h"

#include "base/macros.h"
#include "base/malloc_arena_pool.h"

namespace art HIDDEN {
namespace riscv64 {

class JniMacroAssemblerRiscv64Test : public AssemblerTestBase {
 public:
  JniMacroAssemblerRiscv64Test() : pool_(), allocator_(&pool_), assembler_(&allocator_) { }

 protected:
  InstructionSet GetIsa() override { return InstructionSet::kRiscv64; }

  void DriverStr(const std::string& assembly_text, const std::string& test_name) {
    assembler_.FinalizeCode();
    size_t cs = assembler_.CodeSize();
    std::vector<uint8_t> data(cs);
    MemoryRegion code(&data[0], data.size());
    assembler_.CopyInstructions(code);
    Driver(data, assembly_text, test_name);
  }

  static Riscv64ManagedRegister AsManaged(XRegister reg) {
    return Riscv64ManagedRegister::FromXRegister(reg);
  }

  static Riscv64ManagedRegister AsManaged(FRegister reg) {
    return Riscv64ManagedRegister::FromFRegister(reg);
  }

  static const size_t kWordSize = 4u;
  static const size_t kDoubleWordSize = 8u;

  MallocArenaPool pool_;
  ArenaAllocator allocator_;
  Riscv64JNIMacroAssembler assembler_;
};

#define __ assembler_.

TEST_F(JniMacroAssemblerRiscv64Test, StackFrame) {
  std::string expected;

  std::unique_ptr<JniCallingConvention> jni_conv = JniCallingConvention::Create(
      &allocator_,
      /*is_static=*/ false,
      /*is_synchronized=*/ false,
      /*is_fast_native=*/ false,
      /*is_critical_native=*/ false,
      /*shorty=*/ "V",
      InstructionSet::kRiscv64);
  size_t frame_size = jni_conv->FrameSize();
  ManagedRegister method_reg = AsManaged(A0);
  ArrayRef<const ManagedRegister> callee_save_regs = jni_conv->CalleeSaveRegisters();

  __ BuildFrame(frame_size, method_reg, callee_save_regs);
  expected += "addi sp, sp, -208\n"
              "sd ra, 200(sp)\n"
              "sd s11, 192(sp)\n"
              "sd s10, 184(sp)\n"
              "sd s9, 176(sp)\n"
              "sd s8, 168(sp)\n"
              "sd s7, 160(sp)\n"
              "sd s6, 152(sp)\n"
              "sd s5, 144(sp)\n"
              "sd s4, 136(sp)\n"
              "sd s3, 128(sp)\n"
              "sd s2, 120(sp)\n"
              "sd s0, 112(sp)\n"
              "fsd fs11, 104(sp)\n"
              "fsd fs10, 96(sp)\n"
              "fsd fs9, 88(sp)\n"
              "fsd fs8, 80(sp)\n"
              "fsd fs7, 72(sp)\n"
              "fsd fs6, 64(sp)\n"
              "fsd fs5, 56(sp)\n"
              "fsd fs4, 48(sp)\n"
              "fsd fs3, 40(sp)\n"
              "fsd fs2, 32(sp)\n"
              "fsd fs1, 24(sp)\n"
              "fsd fs0, 16(sp)\n"
              "sd a0, 0(sp)\n";

  __ RemoveFrame(frame_size, callee_save_regs, /*may_suspend=*/ false);
  expected += "fld fs0, 16(sp)\n"
              "fld fs1, 24(sp)\n"
              "fld fs2, 32(sp)\n"
              "fld fs3, 40(sp)\n"
              "fld fs4, 48(sp)\n"
              "fld fs5, 56(sp)\n"
              "fld fs6, 64(sp)\n"
              "fld fs7, 72(sp)\n"
              "fld fs8, 80(sp)\n"
              "fld fs9, 88(sp)\n"
              "fld fs10, 96(sp)\n"
              "fld fs11, 104(sp)\n"
              "ld s0, 112(sp)\n"
              "ld s2, 120(sp)\n"
              "ld s3, 128(sp)\n"
              "ld s4, 136(sp)\n"
              "ld s5, 144(sp)\n"
              "ld s6, 152(sp)\n"
              "ld s7, 160(sp)\n"
              "ld s8, 168(sp)\n"
              "ld s9, 176(sp)\n"
              "ld s10, 184(sp)\n"
              "ld s11, 192(sp)\n"
              "ld ra, 200(sp)\n"
              "addi sp, sp, 208\n"
              "ret\n";

  DriverStr(expected, "StackFrame");
}

TEST_F(JniMacroAssemblerRiscv64Test, ChangeFrameSize) {
  std::string expected;

  __ IncreaseFrameSize(128);
  expected += "addi sp, sp, -128\n";
  __ DecreaseFrameSize(128);
  expected += "addi sp, sp, 128\n";

  __ IncreaseFrameSize(0);  // No-op
  __ DecreaseFrameSize(0);  // No-op

  __ IncreaseFrameSize(2048);
  expected += "addi sp, sp, -2048\n";
  __ DecreaseFrameSize(2048);
  expected += "addi t6, sp, 2047\n"
              "addi sp, t6, 1\n";

  __ IncreaseFrameSize(4096);
  expected += "addi t6, sp, -2048\n"
              "addi sp, t6, -2048\n";
  __ DecreaseFrameSize(4096);
  expected += "lui t6, 1\n"
              "add sp, sp, t6\n";

  __ IncreaseFrameSize(6 * KB);
  expected += "addi t6, zero, -3\n"
              "slli t6, t6, 11\n"
              "add sp, sp, t6\n";
  __ DecreaseFrameSize(6 * KB);
  expected += "addi t6, zero, 3\n"
              "slli t6, t6, 11\n"
              "add sp, sp, t6\n";

  __ IncreaseFrameSize(6 * KB + 16);
  expected += "lui t6, 0xffffe\n"
              "addiw t6, t6, 2048-16\n"
              "add sp, sp, t6\n";
  __ DecreaseFrameSize(6 * KB + 16);
  expected += "lui t6, 2\n"
              "addiw t6, t6, 16-2048\n"
              "add sp, sp, t6\n";

  DriverStr(expected, "ChangeFrameSize");
}

TEST_F(JniMacroAssemblerRiscv64Test, Store) {
  std::string expected;

  __ Store(FrameOffset(0), AsManaged(A0), kWordSize);
  expected += "sw a0, 0(sp)\n";
  __ Store(FrameOffset(2048), AsManaged(S0), kDoubleWordSize);
  expected += "addi t6, sp, 0x7f8\n"
              "sd s0, 8(t6)\n";

  __ Store(AsManaged(A1), MemberOffset(256), AsManaged(S2), kDoubleWordSize);
  expected += "sd s2, 256(a1)\n";
  __ Store(AsManaged(S3), MemberOffset(4 * KB), AsManaged(T1), kWordSize);
  expected += "lui t6, 1\n"
              "add t6, t6, s3\n"
              "sw t1, 0(t6)\n";

  __ Store(AsManaged(A3), MemberOffset(384), AsManaged(FA5), kDoubleWordSize);
  expected += "fsd fa5, 384(a3)\n";
  __ Store(AsManaged(S4), MemberOffset(4 * KB + 16), AsManaged(FT10), kWordSize);
  expected += "lui t6, 1\n"
              "add t6, t6, s4\n"
              "fsw ft10, 16(t6)\n";

  __ StoreRawPtr(FrameOffset(128), AsManaged(A7));
  expected += "sd a7, 128(sp)\n";
  __ StoreRawPtr(FrameOffset(6 * KB), AsManaged(S11));
  expected += "lui t6, 2\n"
              "add t6, t6, sp\n"
              "sd s11, -2048(t6)\n";

  __ StoreStackPointerToThread(ThreadOffset64(512), /*tag_sp=*/ false);
  expected += "sd sp, 512(s1)\n";
  __ StoreStackPointerToThread(ThreadOffset64(3 * KB), /*tag_sp=*/ true);
  expected += "ori t5, sp, 0x2\n"
              "addi t6, s1, 0x7f8\n"
              "sd t5, 0x408(t6)\n";

  DriverStr(expected, "Store");
}

TEST_F(JniMacroAssemblerRiscv64Test, Load) {
  std::string expected;

  __ Load(AsManaged(A0), FrameOffset(0), kWordSize);
  expected += "lw a0, 0(sp)\n";
  __ Load(AsManaged(S0), FrameOffset(2048), kDoubleWordSize);
  expected += "addi t6, sp, 0x7f8\n"
              "ld s0, 8(t6)\n";

  __ Load(AsManaged(S2), AsManaged(A1), MemberOffset(256), kDoubleWordSize);
  expected += "ld s2, 256(a1)\n";
  __ Load(AsManaged(T1), AsManaged(S3), MemberOffset(4 * KB), kWordSize);
  expected += "lui t6, 1\n"
              "add t6, t6, s3\n"
              "lw t1, 0(t6)\n";

  __ Load(AsManaged(FA5), AsManaged(A3), MemberOffset(384), kDoubleWordSize);
  expected += "fld fa5, 384(a3)\n";
  __ Load(AsManaged(FT10), AsManaged(S4), MemberOffset(4 * KB + 16), kWordSize);
  expected += "lui t6, 1\n"
              "add t6, t6, s4\n"
              "flw ft10, 16(t6)\n";

  __ LoadRawPtrFromThread(AsManaged(A7), ThreadOffset64(512));
  expected += "ld a7, 512(s1)\n";
  __ LoadRawPtrFromThread(AsManaged(S11), ThreadOffset64(3 * KB));
  expected += "addi t6, s1, 0x7f8\n"
              "ld s11, 0x408(t6)\n";

  DriverStr(expected, "Load");
}

TEST_F(JniMacroAssemblerRiscv64Test, MoveArguments) {
  // TODO(riscv64): Test `MoveArguments()`.
  // We do not add the test yet while there is an outstanding FIXME in `MoveArguments()`.
}

TEST_F(JniMacroAssemblerRiscv64Test, Move) {
  std::string expected;

  __ Move(AsManaged(A0), AsManaged(A1), kWordSize);
  expected += "mv a0, a1\n";
  __ Move(AsManaged(A2), AsManaged(A3), kDoubleWordSize);
  expected += "mv a2, a3\n";

  __ Move(AsManaged(A4), AsManaged(A4), kWordSize);  // No-op.
  __ Move(AsManaged(A5), AsManaged(A5), kDoubleWordSize);  // No-op.

  DriverStr(expected, "Move");
}

TEST_F(JniMacroAssemblerRiscv64Test, GetCurrentThread) {
  std::string expected;

  __ GetCurrentThread(AsManaged(A0));
  expected += "mv a0, s1\n";

  __ GetCurrentThread(FrameOffset(256));
  expected += "sd s1, 256(sp)\n";
  __ GetCurrentThread(FrameOffset(3 * KB));
  expected += "addi t6, sp, 0x7f8\n"
              "sd s1, 0x408(t6)\n";

  DriverStr(expected, "GetCurrentThread");
}

TEST_F(JniMacroAssemblerRiscv64Test, JumpCodePointer) {
  std::string expected;

  __ Jump(AsManaged(A0), Offset(24));
  expected += "ld t6, 24(a0)\n"
              "jr t6\n";

  __ Jump(AsManaged(S2), Offset(2048));
  expected += "addi t6, s2, 0x7f8\n"
              "ld t6, 8(t6)\n"
              "jr t6\n";

  DriverStr(expected, "JumpCodePointer");
}

TEST_F(JniMacroAssemblerRiscv64Test, Call) {
  std::string expected;

  __ Call(AsManaged(A0), Offset(32));
  expected += "ld ra, 32(a0)\n"
              "jalr ra\n";

  __ Call(AsManaged(S2), Offset(2048));
  expected += "addi t6, s2, 0x7f8\n"
              "ld ra, 8(t6)\n"
              "jalr ra\n";

  __ CallFromThread(ThreadOffset64(256));
  expected += "ld ra, 256(s1)\n"
              "jalr ra\n";

  __ CallFromThread(ThreadOffset64(3 * KB));
  expected += "addi t6, s1, 0x7f8\n"
              "ld ra, 0x408(t6)\n"
              "jalr ra\n";

  DriverStr(expected, "Call");
}

TEST_F(JniMacroAssemblerRiscv64Test, Exception) {
  std::string expected;

  ThreadOffset64 exception_offset = Thread::ExceptionOffset<kArm64PointerSize>();
  ThreadOffset64 deliver_offset = QUICK_ENTRYPOINT_OFFSET(kArm64PointerSize, pDeliverException);

  std::unique_ptr<JNIMacroLabel> slow_path = __ CreateLabel();

  __ ExceptionPoll(slow_path.get());
  expected += "ld t6, " + std::to_string(exception_offset.Int32Value()) + "(s1)\n"
              "bnez t6, 1f\n";

  __ RemoveFrame(/*frame_size=*/ 0u,
                 /*callee_save_regs=*/ ArrayRef<const ManagedRegister>(),
                 /*may_suspend=*/ false);
  expected += "ret\n";

  __ Bind(slow_path.get());
  expected += "1:\n";

  __ DeliverPendingException();
  expected += "ld a0, " + std::to_string(exception_offset.Int32Value()) + "(s1)\n"
              "ld ra, " + std::to_string(deliver_offset.Int32Value()) + "(s1)\n"
              "jalr ra\n"
              "ebreak\n";

  DriverStr(expected, "Exception");
}

TEST_F(JniMacroAssemblerRiscv64Test, JumpLabel) {
  std::string expected;

  std::unique_ptr<JNIMacroLabel> target = __ CreateLabel();
  std::unique_ptr<JNIMacroLabel> back = __ CreateLabel();

  __ Jump(target.get());
  expected += "j 2f\n";

  __ Bind(back.get());
  expected += "1:\n";

  __ Move(AsManaged(A0), AsManaged(A1), static_cast<size_t>(kRiscv64PointerSize));
  expected += "mv a0, a1\n";

  __ Bind(target.get());
  expected += "2:\n";

  __ Jump(back.get());
  expected += "j 1b\n";

  DriverStr(expected, "JumpLabel");
}

TEST_F(JniMacroAssemblerRiscv64Test, ReadBarrier) {
  // TODO(riscv64): Test `TestGcMarking()` and `TestMarkBit()`.
}

TEST_F(JniMacroAssemblerRiscv64Test, TestByteAndJumpIfNotZero) {
  // TODO(riscv64): Test `TestByteAndJumpIfNotZero()`.
}

#undef __

}  // namespace riscv64
}  // namespace art
