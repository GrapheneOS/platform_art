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

#include "assembler_riscv64.h"

#include <inttypes.h>

#include <map>

#include "base/bit_utils.h"
#include "utils/assembler_test.h"

#define __ GetAssembler()->

namespace art {
namespace riscv64 {

struct RISCV64CpuRegisterCompare {
  bool operator()(const riscv64::XRegister& a, const riscv64::XRegister& b) const { return a < b; }
};

class AssemblerRISCV64Test : public AssemblerTest<riscv64::Riscv64Assembler,
                                                  riscv64::Riscv64Label,
                                                  riscv64::XRegister,
                                                  riscv64::FRegister,
                                                  int32_t> {
 public:
  using Base = AssemblerTest<riscv64::Riscv64Assembler,
                             riscv64::Riscv64Label,
                             riscv64::XRegister,
                             riscv64::FRegister,
                             int32_t>;

  AssemblerRISCV64Test()
      : instruction_set_features_(Riscv64InstructionSetFeatures::FromVariant("default", nullptr)) {}

 protected:
  riscv64::Riscv64Assembler* CreateAssembler(ArenaAllocator* allocator) override {
    return new (allocator) riscv64::Riscv64Assembler(allocator, instruction_set_features_.get());
  }

  InstructionSet GetIsa() override { return InstructionSet::kRiscv64; }

  void SetUpHelpers() override {
    if (registers_.size() == 0) {
      registers_.push_back(new riscv64::XRegister(riscv64::Zero));
      registers_.push_back(new riscv64::XRegister(riscv64::RA));
      registers_.push_back(new riscv64::XRegister(riscv64::SP));
      registers_.push_back(new riscv64::XRegister(riscv64::GP));
      registers_.push_back(new riscv64::XRegister(riscv64::TP));
      registers_.push_back(new riscv64::XRegister(riscv64::T0));
      registers_.push_back(new riscv64::XRegister(riscv64::T1));
      registers_.push_back(new riscv64::XRegister(riscv64::T2));
      registers_.push_back(new riscv64::XRegister(riscv64::S0));
      registers_.push_back(new riscv64::XRegister(riscv64::S1));
      registers_.push_back(new riscv64::XRegister(riscv64::A0));
      registers_.push_back(new riscv64::XRegister(riscv64::A1));
      registers_.push_back(new riscv64::XRegister(riscv64::A2));
      registers_.push_back(new riscv64::XRegister(riscv64::A3));
      registers_.push_back(new riscv64::XRegister(riscv64::A4));
      registers_.push_back(new riscv64::XRegister(riscv64::A5));
      registers_.push_back(new riscv64::XRegister(riscv64::A6));
      registers_.push_back(new riscv64::XRegister(riscv64::A7));
      registers_.push_back(new riscv64::XRegister(riscv64::S2));
      registers_.push_back(new riscv64::XRegister(riscv64::S3));
      registers_.push_back(new riscv64::XRegister(riscv64::S4));
      registers_.push_back(new riscv64::XRegister(riscv64::S5));
      registers_.push_back(new riscv64::XRegister(riscv64::S6));
      registers_.push_back(new riscv64::XRegister(riscv64::S7));
      registers_.push_back(new riscv64::XRegister(riscv64::S8));
      registers_.push_back(new riscv64::XRegister(riscv64::S9));
      registers_.push_back(new riscv64::XRegister(riscv64::S10));
      registers_.push_back(new riscv64::XRegister(riscv64::S11));
      registers_.push_back(new riscv64::XRegister(riscv64::T3));
      registers_.push_back(new riscv64::XRegister(riscv64::T4));
      registers_.push_back(new riscv64::XRegister(riscv64::T5));
      registers_.push_back(new riscv64::XRegister(riscv64::T6));

      secondary_register_names_.emplace(riscv64::XRegister(riscv64::Zero), "zero");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::RA), "ra");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::SP), "sp");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::GP), "gp");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::TP), "tp");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T0), "t0");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T1), "t1");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T2), "t2");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S0), "s0");  // s0/fp
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S1), "s1");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A0), "a0");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A1), "a1");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A2), "a2");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A3), "a3");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A4), "a4");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A5), "a5");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A6), "a6");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::A7), "a7");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S2), "s2");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S3), "s3");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S4), "s4");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S5), "s5");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S6), "s6");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S7), "s7");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S8), "s8");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S9), "s9");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S10), "s10");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::S11), "s11");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T3), "t3");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T4), "t4");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T5), "t5");
      secondary_register_names_.emplace(riscv64::XRegister(riscv64::T6), "t6");

      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT0));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT1));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT2));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT3));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT4));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT5));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT6));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT7));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS0));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS1));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA0));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA1));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA2));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA3));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA4));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA5));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA6));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FA7));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS2));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS3));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS4));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS5));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS6));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS7));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS8));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS9));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS10));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FS11));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT8));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT9));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT10));
      fp_registers_.push_back(new riscv64::FRegister(riscv64::FT11));
    }
  }

  void TearDown() override {
    AssemblerTest::TearDown();
    STLDeleteElements(&registers_);
    STLDeleteElements(&fp_registers_);
  }

  std::vector<riscv64::Riscv64Label> GetAddresses() override {
    UNIMPLEMENTED(FATAL) << "Feature not implemented yet";
    UNREACHABLE();
  }

  std::vector<riscv64::XRegister*> GetRegisters() override { return registers_; }

  std::vector<riscv64::FRegister*> GetFPRegisters() override { return fp_registers_; }

  std::string GetSecondaryRegisterName(const riscv64::XRegister& reg) override {
    CHECK(secondary_register_names_.find(reg) != secondary_register_names_.end());
    return secondary_register_names_[reg];
  }

  int32_t CreateImmediate(int64_t imm_value) override {
    return dchecked_integral_cast<int32_t>(imm_value);
  }

  template <typename Emit>
  std::string RepeatInsn(size_t count, const std::string& insn, Emit&& emit) {
    std::string result;
    for (; count != 0u; --count) {
      result += insn;
      emit();
    }
    return result;
  }

  std::string EmitNops(size_t size) {
    // TODO(riscv64): Support "C" Standard Extension.
    DCHECK_ALIGNED(size, sizeof(uint32_t));
    const size_t num_nops = size / sizeof(uint32_t);
    return RepeatInsn(num_nops, "nop\n", [&]() { __ Nop(); });
  }

  template <typename EmitLoadConst>
  void TestLoadConst64(const std::string& test_name,
                       bool can_use_tmp,
                       EmitLoadConst&& emit_load_const) {
    std::string expected;
    // Test standard immediates. Unlike other instructions, `Li()` accepts an `int64_t` but
    // this is unsupported by `CreateImmediate()`, so we cannot use `RepeatRIb()` for these.
    // Note: This `CreateImmediateValuesBits()` call does not produce any values where
    // `LoadConst64()` would emit different code from `Li()`.
    for (int64_t value : CreateImmediateValuesBits(64, /*as_uint=*/ false)) {
      emit_load_const(A0, value);
      expected += "li a0, " + std::to_string(value) + "\n";
    }
    // Test various registers with a few small values.
    // (Even Zero is an accepted register even if that does not really load the requested value.)
    for (XRegister* reg : GetRegisters()) {
      if (can_use_tmp && *reg == TMP) {
        continue;  // Not a valid target register.
      }
      std::string rd = GetRegisterName(*reg);
      emit_load_const(*reg, -1);
      expected += "li " + rd + ", -1\n";
      emit_load_const(*reg, 0);
      expected += "li " + rd + ", 0\n";
      emit_load_const(*reg, 1);
      expected += "li " + rd + ", 1\n";
    }
    // Test some significant values. Some may just repeat the tests above but other values
    // show some complex patterns, even exposing a value where clang (and therefore also this
    // assembler) does not generate the shortest sequence.
    // For the following values, `LoadConst64()` emits the same code as `Li()`.
    int64_t test_values1[] = {
        // Small values, either ADDI, ADDI+SLLI, LUI, or LUI+ADDIW.
        // The ADDI+LUI is presumably used to allow shorter code for RV64C.
        -4097, -4096, -4095, -2176, -2049, -2048, -2047, -1025, -1024, -1023, -2, -1,
        0, 1, 2, 1023, 1024, 1025, 2047, 2048, 2049, 2176, 4095, 4096, 4097,
        // Just below std::numeric_limits<int32_t>::min()
        INT64_C(-0x80000001),  // LUI+ADDI
        INT64_C(-0x80000800),  // LUI+ADDI
        INT64_C(-0x80000801),  // LUI+ADDIW+SLLI+ADDI; LUI+ADDI+ADDI would be shorter.
        INT64_C(-0x80000800123),  // LUI+ADDIW+SLLI+ADDI
        INT64_C(0x0123450000000123),  // LUI+SLLI+ADDI
        INT64_C(-0x7654300000000123),  // LUI+SLLI+ADDI
        INT64_C(0x0fffffffffff0000),  // LUI+SRLI
        INT64_C(0x0ffffffffffff000),  // LUI+SRLI
        INT64_C(0x0ffffffffffff010),  // LUI+ADDIW+SRLI
        INT64_C(0x0fffffffffffff10),  // ADDI+SLLI+ADDI; LUI+ADDIW+SRLI would be same length.
        INT64_C(0x0fffffffffffff80),  // ADDI+SRLI
        INT64_C(0x0ffffffff7ffff80),  // LUI+ADDI+SRLI
        INT64_C(0x0123450000001235),  // LUI+SLLI+ADDI+SLLI+ADDI
        INT64_C(0x0123450000001234),  // LUI+SLLI+ADDI+SLLI
        INT64_C(0x0000000fff808010),  // LUI+SLLI+SRLI
        INT64_C(0x00000000fff80801),  // LUI+SLLI+SRLI
        INT64_C(0x00000000ffffffff),  // ADDI+SRLI
        INT64_C(0x00000001ffffffff),  // ADDI+SRLI
        INT64_C(0x00000003ffffffff),  // ADDI+SRLI
        INT64_C(0x00000000ffc00801),  // LUI+ADDIW+SLLI+ADDI
        INT64_C(0x00000001fffff7fe),  // ADDI+SLLI+SRLI
    };
    for (int64_t value : test_values1) {
      emit_load_const(A0, value);
      expected += "li a0, " + std::to_string(value) + "\n";
    }
    // For the following values, `LoadConst64()` emits different code than `Li()`.
    std::pair<int64_t, const char*> test_values2[] = {
        // Li:        LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI
        // LoadConst: LUI+ADDIW+LUI+ADDIW+SLLI+ADD (using TMP)
        { INT64_C(0x1234567812345678),
          "li {reg1}, 0x12345678 / 8\n"  // Trailing zero bits in high word are handled by SLLI.
          "li {reg2}, 0x12345678\n"
          "slli {reg1}, {reg1}, 32 + 3\n"
          "add {reg1}, {reg1}, {reg2}\n" },
        { INT64_C(0x1234567887654321),
          "li {reg1}, 0x12345678 + 1\n"  // One higher to compensate for negative TMP.
          "li {reg2}, 0x87654321 - 0x100000000\n"
          "slli {reg1}, {reg1}, 32\n"
          "add {reg1}, {reg1}, {reg2}\n" },
        { INT64_C(-0x1234567887654321),
          "li {reg1}, -0x12345678 - 1\n"  // High 32 bits of the constant.
          "li {reg2}, 0x100000000 - 0x87654321\n"  // Low 32 bits of the constant.
          "slli {reg1}, {reg1}, 32\n"
          "add {reg1}, {reg1}, {reg2}\n" },

        // Li:        LUI+SLLI+ADDI+SLLI+ADDI+SLLI
        // LoadConst: LUI+LUI+SLLI+ADD (using TMP)
        { INT64_C(0x1234500012345000),
          "lui {reg1}, 0x12345\n"
          "lui {reg2}, 0x12345\n"
          "slli {reg1}, {reg1}, 44 - 12\n"
          "add {reg1}, {reg1}, {reg2}\n" },
        { INT64_C(0x0123450012345000),
          "lui {reg1}, 0x12345\n"
          "lui {reg2}, 0x12345\n"
          "slli {reg1}, {reg1}, 40 - 12\n"
          "add {reg1}, {reg1}, {reg2}\n" },

        // Li:        LUI+ADDIW+SLLI+ADDI+SLLI+ADDI
        // LoadConst: LUI+LUI+ADDIW+SLLI+ADD (using TMP)
        { INT64_C(0x0001234512345678),
          "lui {reg1}, 0x12345\n"
          "li {reg2}, 0x12345678\n"
          "slli {reg1}, {reg1}, 32 - 12\n"
          "add {reg1}, {reg1}, {reg2}\n" },
        { INT64_C(0x0012345012345678),
          "lui {reg1}, 0x12345\n"
          "li {reg2}, 0x12345678\n"
          "slli {reg1}, {reg1}, 36 - 12\n"
          "add {reg1}, {reg1}, {reg2}\n" },
    };
    for (auto [value, fmt] : test_values2) {
      emit_load_const(A0, value);
      if (can_use_tmp) {
        std::string base = fmt;
        ReplaceReg(REG1_TOKEN, GetRegisterName(A0), &base);
        ReplaceReg(REG2_TOKEN, GetRegisterName(TMP), &base);
        expected += base;
      } else {
        expected += "li a0, " + std::to_string(value) + "\n";
      }
    }

    DriverStr(expected, test_name);
  }

  auto GetPrintBcond() {
    return [](const std::string& cond,
              [[maybe_unused]] const std::string& opposite_cond,
              const std::string& args,
              const std::string& target) {
      return "b" + cond + args + ", " + target + "\n";
    };
  }

  auto GetPrintBcondOppositeAndJ(const std::string& skip_label) {
    return [=]([[maybe_unused]] const std::string& cond,
               const std::string& opposite_cond,
               const std::string& args,
               const std::string& target) {
      return "b" + opposite_cond + args + ", " + skip_label + "f\n" +
             "j " + target + "\n" +
             skip_label + ":\n";
    };
  }

  auto GetPrintBcondOppositeAndTail(const std::string& skip_label, const std::string& base_label) {
    return [=]([[maybe_unused]] const std::string& cond,
               const std::string& opposite_cond,
               const std::string& args,
               const std::string& target) {
      return "b" + opposite_cond + args + ", " + skip_label + "f\n" +
             base_label + ":\n" +
             "auipc t6, %pcrel_hi(" + target + ")\n" +
             "jalr x0, %pcrel_lo(" + base_label + "b)(t6)\n" +
             skip_label + ":\n";
    };
  }

  // Helper function for basic tests that all branch conditions map to the correct opcodes,
  // whether with branch expansion (a conditional branch with opposite condition over an
  // unconditional branch) or without.
  template <typename PrintBcond>
  std::string EmitBcondForAllConditions(Riscv64Label* label,
                                        const std::string& target,
                                        PrintBcond&& print_bcond) {
    XRegister rs = A0;
    __ Beqz(rs, label);
    __ Bnez(rs, label);
    __ Blez(rs, label);
    __ Bgez(rs, label);
    __ Bltz(rs, label);
    __ Bgtz(rs, label);
    XRegister rt = A1;
    __ Beq(rs, rt, label);
    __ Bne(rs, rt, label);
    __ Ble(rs, rt, label);
    __ Bge(rs, rt, label);
    __ Blt(rs, rt, label);
    __ Bgt(rs, rt, label);
    __ Bleu(rs, rt, label);
    __ Bgeu(rs, rt, label);
    __ Bltu(rs, rt, label);
    __ Bgtu(rs, rt, label);

    return
        print_bcond("eq", "ne", "z a0", target) +
        print_bcond("ne", "eq", "z a0", target) +
        print_bcond("le", "gt", "z a0", target) +
        print_bcond("ge", "lt", "z a0", target) +
        print_bcond("lt", "ge", "z a0", target) +
        print_bcond("gt", "le", "z a0", target) +
        print_bcond("eq", "ne", " a0, a1", target) +
        print_bcond("ne", "eq", " a0, a1", target) +
        print_bcond("le", "gt", " a0, a1", target) +
        print_bcond("ge", "lt", " a0, a1", target) +
        print_bcond("lt", "ge", " a0, a1", target) +
        print_bcond("gt", "le", " a0, a1", target) +
        print_bcond("leu", "gtu", " a0, a1", target) +
        print_bcond("geu", "ltu", " a0, a1", target) +
        print_bcond("ltu", "geu", " a0, a1", target) +
        print_bcond("gtu", "leu", " a0, a1", target);
  }

  // Test Bcond for forward branches with all conditions.
  // The gap must be such that either all branches expand, or none does.
  template <typename PrintBcond>
  void TestBcondForward(const std::string& test_name,
                        size_t gap_size,
                        const std::string& target_label,
                        PrintBcond&& print_bcond) {
    std::string expected;
    Riscv64Label label;
    expected += EmitBcondForAllConditions(&label, target_label + "f", print_bcond);
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += target_label + ":\n";
    DriverStr(expected, test_name);
  }

  // Test Bcond for backward branches with all conditions.
  // The gap must be such that either all branches expand, or none does.
  template <typename PrintBcond>
  void TestBcondBackward(const std::string& test_name,
                         size_t gap_size,
                         const std::string& target_label,
                         PrintBcond&& print_bcond) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(gap_size);
    expected += EmitBcondForAllConditions(&label, target_label + "b", print_bcond);
    DriverStr(expected, test_name);
  }

  size_t MaxOffset13BackwardDistance() {
    return 4 * KB;
  }

  size_t MaxOffset13ForwardDistance() {
    // TODO(riscv64): Support "C" Standard Extension, max forward distance 4KiB - 2.
    return 4 * KB - 4;
  }

  size_t MaxOffset21BackwardDistance() {
    return 1 * MB;
  }

  size_t MaxOffset21ForwardDistance() {
    // TODO(riscv64): Support "C" Standard Extension, max forward distance 1MiB - 2.
    return 1 * MB - 4;
  }

  template <typename PrintBcond>
  void TestBeqA0A1Forward(const std::string& test_name,
                          size_t nops_size,
                          const std::string& target_label,
                          PrintBcond&& print_bcond) {
    std::string expected;
    Riscv64Label label;
    __ Beq(A0, A1, &label);
    expected += print_bcond("eq", "ne", " a0, a1", target_label + "f");
    expected += EmitNops(nops_size);
    __ Bind(&label);
    expected += target_label + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename PrintBcond>
  void TestBeqA0A1Backward(const std::string& test_name,
                           size_t nops_size,
                           const std::string& target_label,
                           PrintBcond&& print_bcond) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(nops_size);
    __ Beq(A0, A1, &label);
    expected += print_bcond("eq", "ne", " a0, a1", target_label + "b");
    DriverStr(expected, test_name);
  }

  // Test a branch setup where expanding one branch causes expanding another branch
  // which causes expanding another branch, etc. The argument `cascade` determines
  // whether we push the first branch to expand, or not.
  template <typename PrintBcond>
  void TestBeqA0A1MaybeCascade(const std::string& test_name,
                               bool cascade,
                               PrintBcond&& print_bcond) {
    const size_t kNumBeqs = MaxOffset13ForwardDistance() / sizeof(uint32_t) / 2u;
    auto label_name = [](size_t i) { return  ".L" + std::to_string(i); };

    std::string expected;
    std::vector<Riscv64Label> labels(kNumBeqs);
    for (size_t i = 0; i != kNumBeqs; ++i) {
      __ Beq(A0, A1, &labels[i]);
      expected += print_bcond("eq", "ne", " a0, a1", label_name(i));
    }
    if (cascade) {
      expected += EmitNops(sizeof(uint32_t));
    }
    for (size_t i = 0; i != kNumBeqs; ++i) {
      expected += EmitNops(2 * sizeof(uint32_t));
      __ Bind(&labels[i]);
      expected += label_name(i) + ":\n";
    }
    DriverStr(expected, test_name);
  }

  auto GetPrintJalRd() {
    return [=](XRegister rd, const std::string& target) {
      std::string rd_name = GetRegisterName(rd);
      return "jal " + rd_name + ", " + target + "\n";
    };
  }

  auto GetPrintCallRd(const std::string& base_label) {
    return [=](XRegister rd, const std::string& target) {
      std::string rd_name = GetRegisterName(rd);
      std::string temp_name = (rd != Zero) ? rd_name : GetRegisterName(TMP);
      return base_label + ":\n" +
             "auipc " + temp_name + ", %pcrel_hi(" + target + ")\n" +
             "jalr " + rd_name + ", %pcrel_lo(" + base_label + "b)(" + temp_name + ")\n";
    };
  }

  template <typename PrintJalRd>
  void TestJalRdForward(const std::string& test_name,
                        size_t gap_size,
                        const std::string& label_name,
                        PrintJalRd&& print_jalrd) {
    std::string expected;
    Riscv64Label label;
    for (XRegister* reg : GetRegisters()) {
      __ Jal(*reg, &label);
      expected += print_jalrd(*reg, label_name + "f");
    }
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += label_name + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename PrintJalRd>
  void TestJalRdBackward(const std::string& test_name,
                         size_t gap_size,
                         const std::string& label_name,
                         PrintJalRd&& print_jalrd) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += label_name + ":\n";
    expected += EmitNops(gap_size);
    for (XRegister* reg : GetRegisters()) {
      __ Jal(*reg, &label);
      expected += print_jalrd(*reg, label_name + "b");
    }
    DriverStr(expected, test_name);
  }

  auto GetEmitJ() {
    return [=](Riscv64Label* label) { __ J(label); };
  }

  auto GetEmitJal() {
    return [=](Riscv64Label* label) { __ Jal(label); };
  }

  auto GetPrintJ() {
    return [=](const std::string& target) {
      return "j " + target + "\n";
    };
  }

  auto GetPrintJal() {
    return [=](const std::string& target) {
      return "jal " + target + "\n";
    };
  }

  auto GetPrintTail(const std::string& base_label) {
    return [=](const std::string& target) {
      return base_label + ":\n" +
             "auipc t6, %pcrel_hi(" + target + ")\n" +
             "jalr x0, %pcrel_lo(" + base_label + "b)(t6)\n";
    };
  }

  auto GetPrintCall(const std::string& base_label) {
    return [=](const std::string& target) {
      return base_label + ":\n" +
             "auipc ra, %pcrel_hi(" + target + ")\n" +
             "jalr ra, %pcrel_lo(" + base_label + "b)(ra)\n";
    };
  }

  template <typename EmitBuncond, typename PrintBuncond>
  void TestBuncondForward(const std::string& test_name,
                          size_t gap_size,
                          const std::string& label_name,
                          EmitBuncond&& emit_buncond,
                          PrintBuncond&& print_buncond) {
    std::string expected;
    Riscv64Label label;
    emit_buncond(&label);
    expected += print_buncond(label_name + "f");
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += label_name + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename EmitBuncond, typename PrintBuncond>
  void TestBuncondBackward(const std::string& test_name,
                           size_t gap_size,
                           const std::string& label_name,
                           EmitBuncond&& emit_buncond,
                           PrintBuncond&& print_buncond) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += label_name + ":\n";
    expected += EmitNops(gap_size);
    emit_buncond(&label);
    expected += print_buncond(label_name + "b");
    DriverStr(expected, test_name);
  }

  template <typename EmitOp>
  void TestAddConst(const std::string& test_name,
                    size_t bits,
                    const std::string& suffix,
                    EmitOp&& emit_op) {
    int64_t kImm12s[] = {
        0, 1, 2, 0xff, 0x100, 0x1ff, 0x200, 0x3ff, 0x400, 0x7ff,
        -1, -2, -0x100, -0x101, -0x200, -0x201, -0x400, -0x401, -0x800,
    };
    int64_t kSimplePositiveValues[] = {
        0x800, 0x801, 0xbff, 0xc00, 0xff0, 0xff7, 0xff8, 0xffb, 0xffc, 0xffd, 0xffe,
    };
    int64_t kSimpleNegativeValues[] = {
        -0x801, -0x802, -0xbff, -0xc00, -0xff0, -0xff8, -0xffc, -0xffe, -0xfff, -0x1000,
    };
    std::vector<int64_t> large_values = CreateImmediateValuesBits(bits, /*as_uint=*/ false);
    auto kept_end = std::remove_if(large_values.begin(),
                                   large_values.end(),
                                   [](int64_t value) { return IsInt<13>(value); });
    large_values.erase(kept_end, large_values.end());
    large_values.push_back(0xfff);

    std::string tmp_name = GetRegisterName(TMP);

    std::string expected;
    for (XRegister* rd : GetRegisters()) {
      std::string rd_name = GetRegisterName(*rd);
      std::string addi_rd = "addi" + suffix + " " + rd_name + ", ";
      std::string add_rd = "add" + suffix + " " + rd_name + ", ";
      for (XRegister* rs1 : GetRegisters()) {
        // TMP can be the destination register but not the source register.
        if (*rs1 == TMP) {
          continue;
        }
        std::string rs1_name = GetRegisterName(*rs1);

        for (int64_t imm : kImm12s) {
          emit_op(*rd, *rs1, imm);
          expected += addi_rd + rs1_name + ", " + std::to_string(imm) + "\n";
        }

        auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
          for (int64_t imm : imms) {
            emit_op(*rd, *rs1, imm);
            expected += addi_rd + rs1_name + ", " + std::to_string(adjustment) + "\n" +
                        addi_rd + rd_name + ", " + std::to_string(imm - adjustment) + "\n";
          }
        };
        emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveValues), 0x7ff);
        emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeValues), -0x800);

        for (int64_t imm : large_values) {
          emit_op(*rd, *rs1, imm);
          expected += "li " + tmp_name + ", " + std::to_string(imm) + "\n" +
                      add_rd + rs1_name + ", " + tmp_name + "\n";
        }
      }
    }
    DriverStr(expected, test_name);
  }

  template <typename EmitOp>
  std::string RepeatLoadStoreArbitraryOffset(const std::string& head, EmitOp&& emit_op) {
    int64_t kImm12s[] = {
        0, 1, 2, 0xff, 0x100, 0x1ff, 0x200, 0x3ff, 0x400, 0x7ff,
        -1, -2, -0x100, -0x101, -0x200, -0x201, -0x400, -0x401, -0x800,
    };
    int64_t kSimplePositiveOffsetsAlign8[] = {
        0x800, 0x801, 0xbff, 0xc00, 0xff0, 0xff4, 0xff6, 0xff7
    };
    int64_t kSimplePositiveOffsetsAlign4[] = {
        0xff8, 0xff9, 0xffa, 0xffb
    };
    int64_t kSimplePositiveOffsetsAlign2[] = {
        0xffc, 0xffd
    };
    int64_t kSimplePositiveOffsetsNoAlign[] = {
        0xffe
    };
    int64_t kSimpleNegativeOffsets[] = {
        -0x801, -0x802, -0xbff, -0xc00, -0xff0, -0xff8, -0xffc, -0xffe, -0xfff, -0x1000,
    };
    int64_t kSplitOffsets[] = {
        0xfff, 0x1000, 0x1001, 0x17ff, 0x1800, 0x1fff, 0x2000, 0x2001, 0x27ff, 0x2800,
        0x7fffe7ff, 0x7fffe800, 0x7fffefff, 0x7ffff000, 0x7ffff001, 0x7ffff7ff,
        -0x1001, -0x1002, -0x17ff, -0x1800, -0x1801, -0x2000, -0x2001, -0x2800, -0x2801,
        -0x7ffff000, -0x7ffff001, -0x7ffff800, -0x7ffff801, -0x7fffffff, -0x80000000,
    };
    int64_t kSpecialOffsets[] = {
        0x7ffff800, 0x7ffff801, 0x7ffffffe, 0x7fffffff
    };

    std::string tmp_name = GetRegisterName(TMP);
    std::string expected;
    for (XRegister* rs1 : GetRegisters()) {
      if (*rs1 == TMP) {
        continue;  // TMP cannot be the address base register.
      }
      std::string rs1_name = GetRegisterName(*rs1);

      for (int64_t imm : kImm12s) {
        emit_op(*rs1, imm);
        expected += head + ", " + std::to_string(imm) + "(" + rs1_name + ")" + "\n";
      }

      auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
        for (int64_t imm : imms) {
          emit_op(*rs1, imm);
          expected +=
              "addi " + tmp_name + ", " + rs1_name + ", " + std::to_string(adjustment) + "\n" +
              head + ", " + std::to_string(imm - adjustment) + "(" + tmp_name + ")" + "\n";
        }
      };
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign8), 0x7f8);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign4), 0x7fc);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign2), 0x7fe);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsNoAlign), 0x7ff);
      emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeOffsets), -0x800);

      for (int64_t imm : kSplitOffsets) {
        emit_op(*rs1, imm);
        uint32_t imm20 = ((imm >> 12) + ((imm >> 11) & 1)) & 0xfffff;
        int32_t small_offset = (imm & 0xfff) - ((imm & 0x800) << 1);
        expected += "lui " + tmp_name + ", " + std::to_string(imm20) + "\n"
                    "add " + tmp_name + ", " + tmp_name + ", " + rs1_name + "\n" +
                    head + ", " + std::to_string(small_offset) + "(" + tmp_name + ")\n";
      }

      for (int64_t imm : kSpecialOffsets) {
        emit_op(*rs1, imm);
        expected +=
            "lui " + tmp_name + ", 0x80000\n"
            "addiw " + tmp_name + ", " + tmp_name + ", " + std::to_string(imm - 0x80000000) + "\n" +
            "add " + tmp_name + ", " + tmp_name + ", " + rs1_name + "\n" +
            head + ", (" + tmp_name + ")\n";
      }
    }
    return expected;
  }

  void TestLoadStoreArbitraryOffset(const std::string& test_name,
                                    const std::string& insn,
                                    void (Riscv64Assembler::*fn)(XRegister, XRegister, int32_t),
                                    bool is_store) {
    std::string expected;
    for (XRegister* rd : GetRegisters()) {
      // TMP can be the target register for loads but not for stores where loading the
      // adjusted address to TMP would clobber the value we want to store.
      if (is_store && *rd == TMP) {
        continue;
      }
      expected += RepeatLoadStoreArbitraryOffset(
          insn + " " + GetRegisterName(*rd),
          [&](XRegister rs1, int64_t offset) { (GetAssembler()->*fn)(*rd, rs1, offset); });
    }
    DriverStr(expected, test_name);
  }

  void TestFPLoadStoreArbitraryOffset(const std::string& test_name,
                                      const std::string& insn,
                                      void (Riscv64Assembler::*fn)(FRegister, XRegister, int32_t)) {
    std::string expected;
    for (FRegister* rd : GetFPRegisters()) {
      expected += RepeatLoadStoreArbitraryOffset(
          insn + " " + GetFPRegName(*rd),
          [&](XRegister rs1, int64_t offset) { (GetAssembler()->*fn)(*rd, rs1, offset); });
    }
    DriverStr(expected, test_name);
  }

  void TestLoadLiteral(const std::string& test_name, bool with_padding_for_long) {
    std::string expected;
    Literal* narrow_literal = __ NewLiteral<uint32_t>(0x12345678);
    Literal* wide_literal = __ NewLiteral<uint64_t>(0x1234567887654321);
    auto print_load = [&](const std::string& load, XRegister rd, const std::string& label) {
      std::string rd_name = GetRegisterName(rd);
      expected += "1:\n"
                  "auipc " + rd_name + ", %pcrel_hi(" + label + "f)\n" +
                  load + " " + rd_name + ", %pcrel_lo(1b)(" + rd_name + ")\n";
    };
    for (XRegister* reg : GetRegisters()) {
      if (*reg != Zero) {
        __ Loadw(*reg, narrow_literal);
        print_load("lw", *reg, "2");
        __ Loadwu(*reg, narrow_literal);
        print_load("lwu", *reg, "2");
        __ Loadd(*reg, wide_literal);
        print_load("ld", *reg, "3");
      }
    }
    std::string tmp = GetRegisterName(TMP);
    auto print_fp_load = [&](const std::string& load, FRegister rd, const std::string& label) {
      std::string rd_name = GetFPRegName(rd);
      expected += "1:\n"
                  "auipc " + tmp + ", %pcrel_hi(" + label + "f)\n" +
                  load + " " + rd_name + ", %pcrel_lo(1b)(" + tmp + ")\n";
    };
    for (FRegister* freg : GetFPRegisters()) {
      __ FLoadw(*freg, narrow_literal);
      print_fp_load("flw", *freg, "2");
      __ FLoadd(*freg, wide_literal);
      print_fp_load("fld", *freg, "3");
    }
    // All literal loads above emit 8 bytes of code. The narrow literal shall emit 4 bytes of code.
    // If we do not add another instruction, we shall end up with padding before the long literal.
    expected += EmitNops(with_padding_for_long ? 0u : sizeof(uint32_t));
    expected += "2:\n"
                ".4byte 0x12345678\n" +
                std::string(with_padding_for_long ? ".4byte 0\n" : "") +
                "3:\n"
                ".8byte 0x1234567887654321\n";
    DriverStr(expected, test_name);
  }

  std::string RepeatRRAqRl(void (Riscv64Assembler::*f)(XRegister, XRegister, uint32_t),
                           const std::string& fmt) {
    CHECK(f != nullptr);
    std::vector<int64_t> imms = CreateImmediateValuesBits(2, /*as_uint=*/ true);
    std::string str;
    for (XRegister* reg1 : GetRegisters()) {
      for (XRegister* reg2 : GetRegisters()) {
        for (int64_t imm : imms) {
          (GetAssembler()->*f)(*reg1, *reg2, dchecked_integral_cast<uint32_t>(imm));

          std::string base = fmt;
          ReplaceReg(REG1_TOKEN, GetRegisterName(*reg1), &base);
          ReplaceReg(REG2_TOKEN, GetRegisterName(*reg2), &base);
          ReplaceAqRl(imm, &base);
          str += base;
          str += "\n";
        }
      }
    }
    return str;
  }

  std::string RepeatRRRAqRl(void (Riscv64Assembler::*f)(XRegister, XRegister, XRegister, uint32_t),
                            const std::string& fmt) {
    CHECK(f != nullptr);
    std::vector<int64_t> imms = CreateImmediateValuesBits(2, /*as_uint=*/ true);
    std::string str;
    for (XRegister* reg1 : GetRegisters()) {
      for (XRegister* reg2 : GetRegisters()) {
        for (XRegister* reg3 : GetRegisters()) {
          for (int64_t imm : imms) {
            (GetAssembler()->*f)(*reg1, *reg2, *reg3, dchecked_integral_cast<uint32_t>(imm));

            std::string base = fmt;
            ReplaceReg(REG1_TOKEN, GetRegisterName(*reg1), &base);
            ReplaceReg(REG2_TOKEN, GetRegisterName(*reg2), &base);
            ReplaceReg(REG3_TOKEN, GetRegisterName(*reg3), &base);
            ReplaceAqRl(imm, &base);
            str += base;
            str += "\n";
          }
        }
      }
    }
    return str;
  }

  std::string RepeatCsrrX(void (Riscv64Assembler::*f)(XRegister, uint32_t, XRegister),
                          const std::string& fmt) {
    CHECK(f != nullptr);
    std::vector<int64_t> csrs = CreateImmediateValuesBits(12, /*as_uint=*/ true);
    std::string str;
    for (XRegister* reg1 : GetRegisters()) {
      for (int64_t csr : csrs) {
        for (XRegister* reg2 : GetRegisters()) {
          (GetAssembler()->*f)(*reg1, dchecked_integral_cast<uint32_t>(csr), *reg2);

          std::string base = fmt;
          ReplaceReg(REG1_TOKEN, GetRegisterName(*reg1), &base);
          ReplaceCsrrImm(CSR_TOKEN, csr, &base);
          ReplaceReg(REG2_TOKEN, GetRegisterName(*reg2), &base);
          str += base;
          str += "\n";
        }
      }
    }
    return str;
  }

  std::string RepeatCsrrXi(void (Riscv64Assembler::*f)(XRegister, uint32_t, uint32_t),
                           const std::string& fmt) {
    CHECK(f != nullptr);
    std::vector<int64_t> csrs = CreateImmediateValuesBits(12, /*as_uint=*/ true);
    std::vector<int64_t> uimms = CreateImmediateValuesBits(2, /*as_uint=*/ true);
    std::string str;
    for (XRegister* reg : GetRegisters()) {
      for (int64_t csr : csrs) {
        for (int64_t uimm : uimms) {
          (GetAssembler()->*f)(
              *reg, dchecked_integral_cast<uint32_t>(csr), dchecked_integral_cast<uint32_t>(uimm));

          std::string base = fmt;
          ReplaceReg(REG_TOKEN, GetRegisterName(*reg), &base);
          ReplaceCsrrImm(CSR_TOKEN, csr, &base);
          ReplaceCsrrImm(UIMM_TOKEN, uimm, &base);
          str += base;
          str += "\n";
        }
      }
    }
    return str;
  }

  template <typename EmitCssrX>
  void TestCsrrXMacro(const std::string& test_name,
                      const std::string& fmt,
                      EmitCssrX&& emit_csrrx) {
    std::vector<int64_t> csrs = CreateImmediateValuesBits(12, /*as_uint=*/ true);
    std::string expected;
    for (XRegister* reg : GetRegisters()) {
      for (int64_t csr : csrs) {
        emit_csrrx(dchecked_integral_cast<uint32_t>(csr), *reg);

        std::string base = fmt;
        ReplaceReg(REG_TOKEN, GetRegisterName(*reg), &base);
        ReplaceCsrrImm(CSR_TOKEN, csr, &base);
        expected += base;
        expected += "\n";
      }
    }
    DriverStr(expected, test_name);
  }

  template <typename EmitCssrXi>
  void TestCsrrXiMacro(const std::string& test_name,
                       const std::string& fmt,
                       EmitCssrXi&& emit_csrrxi) {
    std::vector<int64_t> csrs = CreateImmediateValuesBits(12, /*as_uint=*/ true);
    std::vector<int64_t> uimms = CreateImmediateValuesBits(2, /*as_uint=*/ true);
    std::string expected;
    for (int64_t csr : csrs) {
      for (int64_t uimm : uimms) {
        emit_csrrxi(dchecked_integral_cast<uint32_t>(csr), dchecked_integral_cast<uint32_t>(uimm));

        std::string base = fmt;
        ReplaceCsrrImm(CSR_TOKEN, csr, &base);
        ReplaceCsrrImm(UIMM_TOKEN, uimm, &base);
        expected += base;
        expected += "\n";
      }
    }
    DriverStr(expected, test_name);
  }

 private:
  static constexpr const char* AQRL_TOKEN = "{aqrl}";
  static constexpr const char* CSR_TOKEN = "{csr}";
  static constexpr const char* UIMM_TOKEN = "{uimm}";

  void ReplaceAqRl(int64_t aqrl, /*inout*/ std::string* str) {
    const char* replacement;
    switch (aqrl) {
      case 0:
        replacement = "";
        break;
      case 1:
        replacement = ".rl";
        break;
      case 2:
        replacement = ".aq";
        break;
      case 3:
        replacement = ".aqrl";
        break;
      default:
        LOG(FATAL) << "Unexpected value for `aqrl`: " << aqrl;
        UNREACHABLE();
    }
    size_t aqrl_index = str->find(AQRL_TOKEN);
    if (aqrl_index != std::string::npos) {
      str->replace(aqrl_index, ConstexprStrLen(AQRL_TOKEN), replacement);
    }
  }

  static void ReplaceCsrrImm(const std::string& imm_token,
                             int64_t imm,
                             /*inout*/ std::string* str) {
    size_t imm_index = str->find(imm_token);
    if (imm_index != std::string::npos) {
      str->replace(imm_index, imm_token.length(), std::to_string(imm));
    }
  }

  std::vector<riscv64::XRegister*> registers_;
  std::map<riscv64::XRegister, std::string, RISCV64CpuRegisterCompare> secondary_register_names_;

  std::vector<riscv64::FRegister*> fp_registers_;

  std::unique_ptr<const Riscv64InstructionSetFeatures> instruction_set_features_;
};

TEST_F(AssemblerRISCV64Test, Toolchain) { EXPECT_TRUE(CheckTools()); }

TEST_F(AssemblerRISCV64Test, Lui) {
  DriverStr(RepeatRIb(&riscv64::Riscv64Assembler::Lui, 20, "lui {reg}, {imm}"), "Lui");
}

TEST_F(AssemblerRISCV64Test, Auipc) {
  DriverStr(RepeatRIb(&riscv64::Riscv64Assembler::Auipc, 20, "auipc {reg}, {imm}"), "Auipc");
}

TEST_F(AssemblerRISCV64Test, Jal) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Jal, -19, 2, "jal {reg}, {imm}\n"),
            "Jal");
}

TEST_F(AssemblerRISCV64Test, Jalr) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Jalr, -12, "jalr {reg1}, {reg2}, {imm}\n"),
            "Jalr");
}

TEST_F(AssemblerRISCV64Test, Beq) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Beq, -11, 2, "beq {reg1}, {reg2}, {imm}\n"),
            "Beq");
}

TEST_F(AssemblerRISCV64Test, Bne) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bne, -11, 2, "bne {reg1}, {reg2}, {imm}\n"),
            "Bne");
}

TEST_F(AssemblerRISCV64Test, Blt) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Blt, -11, 2, "blt {reg1}, {reg2}, {imm}\n"),
            "Blt");
}

TEST_F(AssemblerRISCV64Test, Bge) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bge, -11, 2, "bge {reg1}, {reg2}, {imm}\n"),
            "Bge");
}

TEST_F(AssemblerRISCV64Test, Bltu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bltu, -11, 2, "bltu {reg1}, {reg2}, {imm}\n"),
            "Bltu");
}

TEST_F(AssemblerRISCV64Test, Bgeu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bgeu, -11, 2, "bgeu {reg1}, {reg2}, {imm}\n"),
            "Bgeu");
}

TEST_F(AssemblerRISCV64Test, Lb) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lb, -12, "lb {reg1}, {imm}({reg2})"), "Lb");
}

TEST_F(AssemblerRISCV64Test, Lh) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lh, -12, "lh {reg1}, {imm}({reg2})"), "Lh");
}

TEST_F(AssemblerRISCV64Test, Lw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lw, -12, "lw {reg1}, {imm}({reg2})"), "Lw");
}

TEST_F(AssemblerRISCV64Test, Ld) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Ld, -12, "ld {reg1}, {imm}({reg2})"), "Ld");
}

TEST_F(AssemblerRISCV64Test, Lbu) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lbu, -12, "lbu {reg1}, {imm}({reg2})"), "Lbu");
}

TEST_F(AssemblerRISCV64Test, Lhu) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lhu, -12, "lhu {reg1}, {imm}({reg2})"), "Lhu");
}

TEST_F(AssemblerRISCV64Test, Lwu) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Lwu, -12, "lwu {reg1}, {imm}({reg2})"), "Lwu");
}

TEST_F(AssemblerRISCV64Test, Sb) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sb, -12, "sb {reg1}, {imm}({reg2})"), "Sb");
}

TEST_F(AssemblerRISCV64Test, Sh) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sh, -12, "sh {reg1}, {imm}({reg2})"), "Sh");
}

TEST_F(AssemblerRISCV64Test, Sw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sw, -12, "sw {reg1}, {imm}({reg2})"), "Sw");
}

TEST_F(AssemblerRISCV64Test, Sd) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sd, -12, "sd {reg1}, {imm}({reg2})"), "Sd");
}

TEST_F(AssemblerRISCV64Test, Addi) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Addi, -12, "addi {reg1}, {reg2}, {imm}"),
            "Addi");
}

TEST_F(AssemblerRISCV64Test, Slti) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Slti, -12, "slti {reg1}, {reg2}, {imm}"),
            "Slti");
}

TEST_F(AssemblerRISCV64Test, Sltiu) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sltiu, -12, "sltiu {reg1}, {reg2}, {imm}"),
            "Sltiu");
}

TEST_F(AssemblerRISCV64Test, Xori) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Xori, 11, "xori {reg1}, {reg2}, {imm}"), "Xori");
}

TEST_F(AssemblerRISCV64Test, Ori) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Ori, -12, "ori {reg1}, {reg2}, {imm}"), "Ori");
}

TEST_F(AssemblerRISCV64Test, Andi) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Andi, -12, "andi {reg1}, {reg2}, {imm}"),
            "Andi");
}

TEST_F(AssemblerRISCV64Test, Slli) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Slli, 6, "slli {reg1}, {reg2}, {imm}"), "Slli");
}

TEST_F(AssemblerRISCV64Test, Srli) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Srli, 6, "srli {reg1}, {reg2}, {imm}"), "Srli");
}

TEST_F(AssemblerRISCV64Test, Srai) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Srai, 6, "srai {reg1}, {reg2}, {imm}"), "Srai");
}

TEST_F(AssemblerRISCV64Test, Add) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Add, "add {reg1}, {reg2}, {reg3}"), "Add");
}

TEST_F(AssemblerRISCV64Test, Sub) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sub, "sub {reg1}, {reg2}, {reg3}"), "Sub");
}

TEST_F(AssemblerRISCV64Test, Slt) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Slt, "slt {reg1}, {reg2}, {reg3}"), "Slt");
}

TEST_F(AssemblerRISCV64Test, Sltu) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sltu, "sltu {reg1}, {reg2}, {reg3}"), "Sltu");
}

TEST_F(AssemblerRISCV64Test, Xor) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Xor, "xor {reg1}, {reg2}, {reg3}"), "Xor");
}

TEST_F(AssemblerRISCV64Test, Or) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Or, "or {reg1}, {reg2}, {reg3}"), "Or");
}

TEST_F(AssemblerRISCV64Test, And) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::And, "and {reg1}, {reg2}, {reg3}"), "And");
}

TEST_F(AssemblerRISCV64Test, Sll) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sll, "sll {reg1}, {reg2}, {reg3}"), "Sll");
}

TEST_F(AssemblerRISCV64Test, Srl) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Srl, "srl {reg1}, {reg2}, {reg3}"), "Srl");
}

TEST_F(AssemblerRISCV64Test, Sra) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sra, "sra {reg1}, {reg2}, {reg3}"), "Sra");
}

TEST_F(AssemblerRISCV64Test, Addiw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Addiw, -12, "addiw {reg1}, {reg2}, {imm}"),
            "Addiw");
}

TEST_F(AssemblerRISCV64Test, Slliw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Slliw, 5, "slliw {reg1}, {reg2}, {imm}"),
            "Slliw");
}

TEST_F(AssemblerRISCV64Test, Srliw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Srliw, 5, "srliw {reg1}, {reg2}, {imm}"),
            "Srliw");
}

TEST_F(AssemblerRISCV64Test, Sraiw) {
  DriverStr(RepeatRRIb(&riscv64::Riscv64Assembler::Sraiw, 5, "sraiw {reg1}, {reg2}, {imm}"),
            "Sraiw");
}

TEST_F(AssemblerRISCV64Test, Addw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Addw, "addw {reg1}, {reg2}, {reg3}"), "Addw");
}

TEST_F(AssemblerRISCV64Test, Subw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Subw, "subw {reg1}, {reg2}, {reg3}"), "Subw");
}

TEST_F(AssemblerRISCV64Test, Sllw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sllw, "sllw {reg1}, {reg2}, {reg3}"), "Sllw");
}

TEST_F(AssemblerRISCV64Test, Srlw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Srlw, "srlw {reg1}, {reg2}, {reg3}"), "Srlw");
}

TEST_F(AssemblerRISCV64Test, Sraw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Sraw, "sraw {reg1}, {reg2}, {reg3}"), "Sraw");
}

TEST_F(AssemblerRISCV64Test, Ecall) {
  __ Ecall();
  DriverStr("ecall\n", "Ecall");
}

TEST_F(AssemblerRISCV64Test, Ebreak) {
  __ Ebreak();
  DriverStr("ebreak\n", "Ebreak");
}

TEST_F(AssemblerRISCV64Test, Fence) {
  auto get_fence_type_string = [](uint32_t fence_type) {
    CHECK_LE(fence_type, 0xfu);
    std::string result;
    if ((fence_type & kFenceInput) != 0u) {
      result += "i";
    }
    if ((fence_type & kFenceOutput) != 0u) {
      result += "o";
    }
    if ((fence_type & kFenceRead) != 0u) {
      result += "r";
    }
    if ((fence_type & kFenceWrite) != 0u) {
      result += "w";
    }
    if (result.empty()) {
      result += "0";
    }
    return result;
  };

  std::string expected;
  // Note: The `pred` and `succ` are 4 bits each.
  // Some combinations are not really useful but the assembler can emit them all.
  for (uint32_t pred = 0u; pred != 0x10; ++pred) {
    for (uint32_t succ = 0u; succ != 0x10; ++succ) {
      __ Fence(pred, succ);
      expected +=
          "fence " + get_fence_type_string(pred) + ", " + get_fence_type_string(succ) + "\n";
    }
  }
  DriverStr(expected, "Fence");
}

TEST_F(AssemblerRISCV64Test, FenceTso) {
  __ FenceTso();
  DriverStr("fence.tso", "FenceTso");
}

TEST_F(AssemblerRISCV64Test, FenceI) {
  __ FenceI();
  DriverStr("fence.i", "FenceI");
}

TEST_F(AssemblerRISCV64Test, Mul) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Mul, "mul {reg1}, {reg2}, {reg3}"), "Mul");
}

TEST_F(AssemblerRISCV64Test, Mulh) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Mulh, "mulh {reg1}, {reg2}, {reg3}"), "Mulh");
}

TEST_F(AssemblerRISCV64Test, Mulhsu) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Mulhsu, "mulhsu {reg1}, {reg2}, {reg3}"), "Mulhsu");
}

TEST_F(AssemblerRISCV64Test, Mulhu) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Mulhu, "mulhu {reg1}, {reg2}, {reg3}"), "Mulhu");
}

TEST_F(AssemblerRISCV64Test, Div) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Div, "div {reg1}, {reg2}, {reg3}"), "Div");
}

TEST_F(AssemblerRISCV64Test, Divu) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Divu, "divu {reg1}, {reg2}, {reg3}"), "Divu");
}

TEST_F(AssemblerRISCV64Test, Rem) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Rem, "rem {reg1}, {reg2}, {reg3}"), "Rem");
}

TEST_F(AssemblerRISCV64Test, Remu) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Remu, "remu {reg1}, {reg2}, {reg3}"), "Remu");
}

TEST_F(AssemblerRISCV64Test, Mulw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Mulw, "mulw {reg1}, {reg2}, {reg3}"), "Mulw");
}

TEST_F(AssemblerRISCV64Test, Divw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Divw, "divw {reg1}, {reg2}, {reg3}"), "Divw");
}

TEST_F(AssemblerRISCV64Test, Divuw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Divuw, "divuw {reg1}, {reg2}, {reg3}"), "Divuw");
}

TEST_F(AssemblerRISCV64Test, Remw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Remw, "remw {reg1}, {reg2}, {reg3}"), "Remw");
}

TEST_F(AssemblerRISCV64Test, Remuw) {
  DriverStr(RepeatRRR(&riscv64::Riscv64Assembler::Remuw, "remuw {reg1}, {reg2}, {reg3}"), "Remuw");
}

TEST_F(AssemblerRISCV64Test, LrW) {
  DriverStr(RepeatRRAqRl(&riscv64::Riscv64Assembler::LrW, "lr.w{aqrl} {reg1}, ({reg2})"), "LrW");
}

TEST_F(AssemblerRISCV64Test, LrD) {
  DriverStr(RepeatRRAqRl(&riscv64::Riscv64Assembler::LrD, "lr.d{aqrl} {reg1}, ({reg2})"), "LrD");
}

TEST_F(AssemblerRISCV64Test, ScW) {
  DriverStr(RepeatRRRAqRl(&riscv64::Riscv64Assembler::ScW, "sc.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "ScW");
}

TEST_F(AssemblerRISCV64Test, ScD) {
  DriverStr(RepeatRRRAqRl(&riscv64::Riscv64Assembler::ScD, "sc.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "ScD");
}

TEST_F(AssemblerRISCV64Test, AmoSwapW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoSwapW, "amoswap.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoSwapW");
}

TEST_F(AssemblerRISCV64Test, AmoSwapD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoSwapD, "amoswap.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoSwapD");
}

TEST_F(AssemblerRISCV64Test, AmoAddW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoAddW, "amoadd.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAddW");
}

TEST_F(AssemblerRISCV64Test, AmoAddD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoAddD, "amoadd.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAddD");
}

TEST_F(AssemblerRISCV64Test, AmoXorW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoXorW, "amoxor.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoXorW");
}

TEST_F(AssemblerRISCV64Test, AmoXorD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoXorD, "amoxor.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoXorD");
}

TEST_F(AssemblerRISCV64Test, AmoAndW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoAndW, "amoand.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAndW");
}

TEST_F(AssemblerRISCV64Test, AmoAndD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoAndD, "amoand.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAndD");
}

TEST_F(AssemblerRISCV64Test, AmoOrW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoOrW, "amoor.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoOrW");
}

TEST_F(AssemblerRISCV64Test, AmoOrD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoOrD, "amoor.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoOrD");
}

TEST_F(AssemblerRISCV64Test, AmoMinW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMinW, "amomin.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinW");
}

TEST_F(AssemblerRISCV64Test, AmoMinD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMinD, "amomin.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinD");
}

TEST_F(AssemblerRISCV64Test, AmoMaxW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMaxW, "amomax.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxW");
}

TEST_F(AssemblerRISCV64Test, AmoMaxD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMaxD, "amomax.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxD");
}

TEST_F(AssemblerRISCV64Test, AmoMinuW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMinuW, "amominu.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinuW");
}

TEST_F(AssemblerRISCV64Test, AmoMinuD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMinuD, "amominu.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinuD");
}

TEST_F(AssemblerRISCV64Test, AmoMaxuW) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMaxuW, "amomaxu.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxuW");
}

TEST_F(AssemblerRISCV64Test, AmoMaxuD) {
  DriverStr(RepeatRRRAqRl(
                &riscv64::Riscv64Assembler::AmoMaxuD, "amomaxu.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxuD");
}

TEST_F(AssemblerRISCV64Test, Csrrw) {
  DriverStr(RepeatCsrrX(&riscv64::Riscv64Assembler::Csrrw, "csrrw {reg1}, {csr}, {reg2}"),
            "Csrrw");
}

TEST_F(AssemblerRISCV64Test, Csrrs) {
  DriverStr(RepeatCsrrX(&riscv64::Riscv64Assembler::Csrrs, "csrrs {reg1}, {csr}, {reg2}"),
            "Csrrs");
}

TEST_F(AssemblerRISCV64Test, Csrrc) {
  DriverStr(RepeatCsrrX(&riscv64::Riscv64Assembler::Csrrc, "csrrc {reg1}, {csr}, {reg2}"),
            "Csrrc");
}

TEST_F(AssemblerRISCV64Test, Csrrwi) {
  DriverStr(RepeatCsrrXi(&riscv64::Riscv64Assembler::Csrrwi, "csrrwi {reg}, {csr}, {uimm}"),
            "Csrrwi");
}

TEST_F(AssemblerRISCV64Test, Csrrsi) {
  DriverStr(RepeatCsrrXi(&riscv64::Riscv64Assembler::Csrrsi, "csrrsi {reg}, {csr}, {uimm}"),
            "Csrrsi");
}

TEST_F(AssemblerRISCV64Test, Csrrci) {
  DriverStr(RepeatCsrrXi(&riscv64::Riscv64Assembler::Csrrci, "csrrci {reg}, {csr}, {uimm}"),
            "Csrrci");
}

TEST_F(AssemblerRISCV64Test, FLw) {
  DriverStr(RepeatFRIb(&riscv64::Riscv64Assembler::FLw, -12, "flw {reg1}, {imm}({reg2})"), "FLw");
}

TEST_F(AssemblerRISCV64Test, FLd) {
  DriverStr(RepeatFRIb(&riscv64::Riscv64Assembler::FLd, -12, "fld {reg1}, {imm}({reg2})"), "FLw");
}

TEST_F(AssemblerRISCV64Test, FSw) {
  DriverStr(RepeatFRIb(&riscv64::Riscv64Assembler::FSw, 2, "fsw {reg1}, {imm}({reg2})"), "FSw");
}

TEST_F(AssemblerRISCV64Test, FSd) {
  DriverStr(RepeatFRIb(&riscv64::Riscv64Assembler::FSd, 2, "fsd {reg1}, {imm}({reg2})"), "FSd");
}

TEST_F(AssemblerRISCV64Test, FMAddS) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FMAddS,
                       "fmadd.s {reg1}, {reg2}, {reg3}, {reg4}"), "FMAddS");
}

TEST_F(AssemblerRISCV64Test, FMAddD) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FMAddD,
                       "fmadd.d {reg1}, {reg2}, {reg3}, {reg4}"), "FMAddD");
}

TEST_F(AssemblerRISCV64Test, FMSubS) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FMSubS,
                       "fmsub.s {reg1}, {reg2}, {reg3}, {reg4}"), "FMSubS");
}

TEST_F(AssemblerRISCV64Test, FMSubD) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FMSubD,
                       "fmsub.d {reg1}, {reg2}, {reg3}, {reg4}"), "FMSubD");
}

TEST_F(AssemblerRISCV64Test, FNMSubS) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FNMSubS,
                       "fnmsub.s {reg1}, {reg2}, {reg3}, {reg4}"), "FNMSubS");
}

TEST_F(AssemblerRISCV64Test, FNMSubD) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FNMSubD,
                       "fnmsub.d {reg1}, {reg2}, {reg3}, {reg4}"), "FNMSubD");
}

TEST_F(AssemblerRISCV64Test, FNMAddS) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FNMAddS,
                       "fnmadd.s {reg1}, {reg2}, {reg3}, {reg4}"), "FNMAddS");
}

TEST_F(AssemblerRISCV64Test, FNMAddD) {
  DriverStr(RepeatFFFF(&riscv64::Riscv64Assembler::FNMAddD,
                       "fnmadd.d {reg1}, {reg2}, {reg3}, {reg4}"), "FNMAddD");
}

TEST_F(AssemblerRISCV64Test, FAddS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FAddS, "fadd.s {reg1}, {reg2}, {reg3}"), "FAddS");
}

TEST_F(AssemblerRISCV64Test, FAddD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FAddD, "fadd.d {reg1}, {reg2}, {reg3}"), "FAddD");
}

TEST_F(AssemblerRISCV64Test, FSubS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSubS, "fsub.s {reg1}, {reg2}, {reg3}"), "FSubS");
}

TEST_F(AssemblerRISCV64Test, FSubD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSubD, "fsub.d {reg1}, {reg2}, {reg3}"), "FSubD");
}

TEST_F(AssemblerRISCV64Test, FMulS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMulS, "fmul.s {reg1}, {reg2}, {reg3}"), "FMulS");
}

TEST_F(AssemblerRISCV64Test, FMulD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMulD, "fmul.d {reg1}, {reg2}, {reg3}"), "FMulD");
}

TEST_F(AssemblerRISCV64Test, FDivS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FDivS, "fdiv.s {reg1}, {reg2}, {reg3}"), "FDivS");
}

TEST_F(AssemblerRISCV64Test, FDivD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FDivD, "fdiv.d {reg1}, {reg2}, {reg3}"), "FDivD");
}

TEST_F(AssemblerRISCV64Test, FSqrtS) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FSqrtS, "fsqrt.s {reg1}, {reg2}"), "FSqrtS");
}

TEST_F(AssemblerRISCV64Test, FSqrtD) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FSqrtD, "fsqrt.d {reg1}, {reg2}"), "FSqrtD");
}

TEST_F(AssemblerRISCV64Test, FSgnjS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjS, "fsgnj.s {reg1}, {reg2}, {reg3}"),
            "FSgnjS");
}

TEST_F(AssemblerRISCV64Test, FSgnjD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjD, "fsgnj.d {reg1}, {reg2}, {reg3}"),
            "FSgnjD");
}

TEST_F(AssemblerRISCV64Test, FSgnjnS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjnS, "fsgnjn.s {reg1}, {reg2}, {reg3}"),
            "FSgnjnS");
}

TEST_F(AssemblerRISCV64Test, FSgnjnD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjnD, "fsgnjn.d {reg1}, {reg2}, {reg3}"),
            "FSgnjnD");
}

TEST_F(AssemblerRISCV64Test, FSgnjxS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjxS, "fsgnjx.s {reg1}, {reg2}, {reg3}"),
            "FSgnjxS");
}

TEST_F(AssemblerRISCV64Test, FSgnjxD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FSgnjxD, "fsgnjx.d {reg1}, {reg2}, {reg3}"),
            "FSgnjxD");
}

TEST_F(AssemblerRISCV64Test, FMinS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMinS, "fmin.s {reg1}, {reg2}, {reg3}"), "FMinS");
}

TEST_F(AssemblerRISCV64Test, FMinD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMinD, "fmin.d {reg1}, {reg2}, {reg3}"), "FMinD");
}

TEST_F(AssemblerRISCV64Test, FMaxS) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMaxS, "fmax.s {reg1}, {reg2}, {reg3}"), "FMaxS");
}

TEST_F(AssemblerRISCV64Test, FMaxD) {
  DriverStr(RepeatFFF(&riscv64::Riscv64Assembler::FMaxD, "fmax.d {reg1}, {reg2}, {reg3}"), "FMaxD");
}

TEST_F(AssemblerRISCV64Test, FCvtSD) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FCvtSD, "fcvt.s.d {reg1}, {reg2}"), "FCvtSD");
}

TEST_F(AssemblerRISCV64Test, FCvtDS) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FCvtDS, "fcvt.d.s {reg1}, {reg2}"), "FCvtDS");
}

TEST_F(AssemblerRISCV64Test, FEqS) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FEqS, "feq.s {reg1}, {reg2}, {reg3}"), "FEqS");
}

TEST_F(AssemblerRISCV64Test, FEqD) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FEqD, "feq.d {reg1}, {reg2}, {reg3}"), "FEqD");
}

TEST_F(AssemblerRISCV64Test, FLtS) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FLtS, "flt.s {reg1}, {reg2}, {reg3}"), "FLtS");
}

TEST_F(AssemblerRISCV64Test, FLtD) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FLtD, "flt.d {reg1}, {reg2}, {reg3}"), "FLtD");
}

TEST_F(AssemblerRISCV64Test, FLeS) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FLeS, "fle.s {reg1}, {reg2}, {reg3}"), "FLeS");
}

TEST_F(AssemblerRISCV64Test, FLeD) {
  DriverStr(RepeatRFF(&riscv64::Riscv64Assembler::FLeD, "fle.d {reg1}, {reg2}, {reg3}"), "FLeD");
}

TEST_F(AssemblerRISCV64Test, FCvtWS) {
  DriverStr(RepeatrF(&riscv64::Riscv64Assembler::FCvtWS, "fcvt.w.s {reg1}, {reg2}"), "FCvtWS");
}

TEST_F(AssemblerRISCV64Test, FCvtWD) {
  DriverStr(RepeatrF(&riscv64::Riscv64Assembler::FCvtWD, "fcvt.w.d {reg1}, {reg2}"), "FCvtWD");
}

TEST_F(AssemblerRISCV64Test, FCvtWuS) {
  DriverStr(RepeatrF(&riscv64::Riscv64Assembler::FCvtWuS, "fcvt.wu.s {reg1}, {reg2}"), "FCvtWuS");
}

TEST_F(AssemblerRISCV64Test, FCvtWuD) {
  DriverStr(RepeatrF(&riscv64::Riscv64Assembler::FCvtWuD, "fcvt.wu.d {reg1}, {reg2}"), "FCvtWuD");
}

TEST_F(AssemblerRISCV64Test, FCvtLS) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FCvtLS, "fcvt.l.s {reg1}, {reg2}"), "FCvtLS");
}

TEST_F(AssemblerRISCV64Test, FCvtLD) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FCvtLD, "fcvt.l.d {reg1}, {reg2}"), "FCvtLD");
}

TEST_F(AssemblerRISCV64Test, FCvtLuS) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FCvtLuS, "fcvt.lu.s {reg1}, {reg2}"), "FCvtLuS");
}

TEST_F(AssemblerRISCV64Test, FCvtLuD) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FCvtLuD, "fcvt.lu.d {reg1}, {reg2}"), "FCvtLuD");
}

TEST_F(AssemblerRISCV64Test, FCvtSW) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtSW, "fcvt.s.w {reg1}, {reg2}"), "FCvtSW");
}

TEST_F(AssemblerRISCV64Test, FCvtDW) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtDW, "fcvt.d.w {reg1}, {reg2}"), "FCvtDW");
}

TEST_F(AssemblerRISCV64Test, FCvtSWu) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtSWu, "fcvt.s.wu {reg1}, {reg2}"), "FCvtSWu");
}

TEST_F(AssemblerRISCV64Test, FCvtDWu) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtDWu, "fcvt.d.wu {reg1}, {reg2}"), "FCvtDWu");
}

TEST_F(AssemblerRISCV64Test, FCvtSL) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtSL, "fcvt.s.l {reg1}, {reg2}"), "FCvtSL");
}

TEST_F(AssemblerRISCV64Test, FCvtDL) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtDL, "fcvt.d.l {reg1}, {reg2}"), "FCvtDL");
}

TEST_F(AssemblerRISCV64Test, FCvtSLu) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtSLu, "fcvt.s.lu {reg1}, {reg2}"), "FCvtSLu");
}

TEST_F(AssemblerRISCV64Test, FCvtDLu) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FCvtDLu, "fcvt.d.lu {reg1}, {reg2}"), "FCvtDLu");
}

TEST_F(AssemblerRISCV64Test, FMvXW) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FMvXW, "fmv.x.w {reg1}, {reg2}"), "FMvXW");
}

TEST_F(AssemblerRISCV64Test, FMvXD) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FMvXD, "fmv.x.d {reg1}, {reg2}"), "FMvXD");
}

TEST_F(AssemblerRISCV64Test, FMvWX) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FMvWX, "fmv.w.x {reg1}, {reg2}"), "FMvWX");
}

TEST_F(AssemblerRISCV64Test, FMvDX) {
  DriverStr(RepeatFR(&riscv64::Riscv64Assembler::FMvDX, "fmv.d.x {reg1}, {reg2}"), "FMvDX");
}

TEST_F(AssemblerRISCV64Test, FClassS) {
  DriverStr(RepeatRF(&riscv64::Riscv64Assembler::FClassS, "fclass.s {reg1}, {reg2}"), "FClassS");
}

TEST_F(AssemblerRISCV64Test, FClassD) {
  DriverStr(RepeatrF(&riscv64::Riscv64Assembler::FClassD, "fclass.d {reg1}, {reg2}"), "FClassD");
}

// Pseudo instructions.
TEST_F(AssemblerRISCV64Test, Nop) {
  __ Nop();
  DriverStr("addi zero,zero,0", "Nop");
}

TEST_F(AssemblerRISCV64Test, Li) {
  TestLoadConst64("Li",
                  /*can_use_tmp=*/ false,
                  [&](XRegister rd, int64_t value) { __ Li(rd, value); });
}

TEST_F(AssemblerRISCV64Test, Mv) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Mv, "addi {reg1}, {reg2}, 0"), "Mv");
}

TEST_F(AssemblerRISCV64Test, Not) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Not, "xori {reg1}, {reg2}, -1"), "Not");
}

TEST_F(AssemblerRISCV64Test, Neg) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Neg, "sub {reg1}, x0, {reg2}"), "Neg");
}

TEST_F(AssemblerRISCV64Test, NegW) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::NegW, "subw {reg1}, x0, {reg2}"), "Neg");
}

TEST_F(AssemblerRISCV64Test, SextB) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::SextB,
                     "slli {reg1}, {reg2}, 56\n"
                     "srai {reg1}, {reg1}, 56"),
            "SextB");
}

TEST_F(AssemblerRISCV64Test, SextH) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::SextH,
                     "slli {reg1}, {reg2}, 48\n"
                     "srai {reg1}, {reg1}, 48"),
            "SextH");
}

TEST_F(AssemblerRISCV64Test, SextW) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::SextW, "addiw {reg1}, {reg2}, 0\n"), "SextW");
}

TEST_F(AssemblerRISCV64Test, ZextB) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::ZextB, "andi {reg1}, {reg2}, 255"), "ZextB");
}

TEST_F(AssemblerRISCV64Test, ZextH) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::ZextH,
                     "slli {reg1}, {reg2}, 48\n"
                     "srli {reg1}, {reg1}, 48"),
            "SextH");
}

TEST_F(AssemblerRISCV64Test, ZextW) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::ZextW,
                     "slli {reg1}, {reg2}, 32\n"
                     "srli {reg1}, {reg1}, 32"),
            "SextW");
}

TEST_F(AssemblerRISCV64Test, Seqz) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Seqz, "sltiu {reg1}, {reg2}, 1\n"), "Seqz");
}

TEST_F(AssemblerRISCV64Test, Snez) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Snez, "sltu {reg1}, zero, {reg2}\n"), "Snez");
}

TEST_F(AssemblerRISCV64Test, Sltz) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Sltz, "slt {reg1}, {reg2}, zero\n"), "Sltz");
}

TEST_F(AssemblerRISCV64Test, Sgtz) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Sgtz, "slt {reg1}, zero, {reg2}\n"), "Sgtz");
}

TEST_F(AssemblerRISCV64Test, FMvS) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FMvS, "fsgnj.s {reg1}, {reg2}, {reg2}\n"), "FMvS");
}

TEST_F(AssemblerRISCV64Test, FAbsS) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FAbsS, "fsgnjx.s {reg1}, {reg2}, {reg2}\n"),
            "FAbsS");
}

TEST_F(AssemblerRISCV64Test, FNegS) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FNegS, "fsgnjn.s {reg1}, {reg2}, {reg2}\n"),
            "FNegS");
}

TEST_F(AssemblerRISCV64Test, FMvD) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FMvD, "fsgnj.d {reg1}, {reg2}, {reg2}\n"), "FMvD");
}

TEST_F(AssemblerRISCV64Test, FAbsD) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FAbsD, "fsgnjx.d {reg1}, {reg2}, {reg2}\n"),
            "FAbsD");
}

TEST_F(AssemblerRISCV64Test, FNegD) {
  DriverStr(RepeatFF(&riscv64::Riscv64Assembler::FNegD, "fsgnjn.d {reg1}, {reg2}, {reg2}\n"),
            "FNegD");
}

TEST_F(AssemblerRISCV64Test, Beqz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Beqz, -11, 2, "beq {reg}, zero, {imm}\n"),
            "Beqz");
}

TEST_F(AssemblerRISCV64Test, Bnez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Bnez, -11, 2, "bne {reg}, zero, {imm}\n"),
            "Bnez");
}

TEST_F(AssemblerRISCV64Test, Blez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Blez, -11, 2, "bge zero, {reg}, {imm}\n"),
            "Blez");
}

TEST_F(AssemblerRISCV64Test, Bgez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Bgez, -11, 2, "bge {reg}, zero, {imm}\n"),
            "Bgez");
}

TEST_F(AssemblerRISCV64Test, Bltz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Bltz, -11, 2, "blt {reg}, zero, {imm}\n"),
            "Bltz");
}

TEST_F(AssemblerRISCV64Test, Bgtz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&riscv64::Riscv64Assembler::Bgtz, -11, 2, "blt zero, {reg}, {imm}\n"),
            "Bgtz");
}

TEST_F(AssemblerRISCV64Test, Bgt) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bgt, -11, 2, "blt {reg2}, {reg1}, {imm}\n"),
            "Bgt");
}

TEST_F(AssemblerRISCV64Test, Ble) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Ble, -11, 2, "bge {reg2}, {reg1}, {imm}\n"),
            "Bge");
}

TEST_F(AssemblerRISCV64Test, Bgtu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bgtu, -11, 2, "bltu {reg2}, {reg1}, {imm}\n"),
            "Bgtu");
}

TEST_F(AssemblerRISCV64Test, Bleu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&riscv64::Riscv64Assembler::Bleu, -11, 2, "bgeu {reg2}, {reg1}, {imm}\n"),
            "Bgeu");
}

TEST_F(AssemblerRISCV64Test, J) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatIbS<int32_t>(&riscv64::Riscv64Assembler::J, -19, 2, "j {imm}\n"), "J");
}

TEST_F(AssemblerRISCV64Test, JalRA) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatIbS<int32_t>(&riscv64::Riscv64Assembler::Jal, -19, 2, "jal {imm}\n"), "JalRA");
}

TEST_F(AssemblerRISCV64Test, Jr) {
  DriverStr(RepeatR(&riscv64::Riscv64Assembler::Jr, "jr {reg}\n"), "Jr");
}

TEST_F(AssemblerRISCV64Test, JalrRA) {
  DriverStr(RepeatR(&riscv64::Riscv64Assembler::Jalr, "jalr {reg}\n"), "JalrRA");
}

TEST_F(AssemblerRISCV64Test, Jalr0) {
  DriverStr(RepeatRR(&riscv64::Riscv64Assembler::Jalr, "jalr {reg1}, {reg2}\n"), "Jalr0");
}

TEST_F(AssemblerRISCV64Test, Ret) {
  __ Ret();
  DriverStr("ret\n", "Ret");
}

TEST_F(AssemblerRISCV64Test, RdCycle) {
  DriverStr(RepeatR(&riscv64::Riscv64Assembler::RdCycle, "rdcycle {reg}\n"), "RdCycle");
}

TEST_F(AssemblerRISCV64Test, RdTime) {
  DriverStr(RepeatR(&riscv64::Riscv64Assembler::RdTime, "rdtime {reg}\n"), "RdTime");
}

TEST_F(AssemblerRISCV64Test, RdInstret) {
  DriverStr(RepeatR(&riscv64::Riscv64Assembler::RdInstret, "rdinstret {reg}\n"), "RdInstret");
}

TEST_F(AssemblerRISCV64Test, Csrr) {
  TestCsrrXMacro(
      "Csrr", "csrr {reg}, {csr}", [&](uint32_t csr, XRegister rd) { __ Csrr(rd, csr); });
}

TEST_F(AssemblerRISCV64Test, Csrw) {
  TestCsrrXMacro(
      "Csrw", "csrw {csr}, {reg}", [&](uint32_t csr, XRegister rs) { __ Csrw(csr, rs); });
}

TEST_F(AssemblerRISCV64Test, Csrs) {
  TestCsrrXMacro(
      "Csrs", "csrs {csr}, {reg}", [&](uint32_t csr, XRegister rs) { __ Csrs(csr, rs); });
}

TEST_F(AssemblerRISCV64Test, Csrc) {
  TestCsrrXMacro(
      "Csrc", "csrc {csr}, {reg}", [&](uint32_t csr, XRegister rs) { __ Csrc(csr, rs); });
}

TEST_F(AssemblerRISCV64Test, Csrwi) {
  TestCsrrXiMacro(
      "Csrwi", "csrwi {csr}, {uimm}", [&](uint32_t csr, uint32_t uimm) { __ Csrwi(csr, uimm); });
}

TEST_F(AssemblerRISCV64Test, Csrsi) {
  TestCsrrXiMacro(
      "Csrsi", "csrsi {csr}, {uimm}", [&](uint32_t csr, uint32_t uimm) { __ Csrsi(csr, uimm); });
}

TEST_F(AssemblerRISCV64Test, Csrci) {
  TestCsrrXiMacro(
      "Csrci", "csrci {csr}, {uimm}", [&](uint32_t csr, uint32_t uimm) { __ Csrci(csr, uimm); });
}

TEST_F(AssemblerRISCV64Test, LoadConst32) {
  // `LoadConst32()` emits the same code sequences as `Li()` for 32-bit values.
  DriverStr(RepeatRIb(&Riscv64Assembler::LoadConst32, -32, "li {reg}, {imm}"), "LoadConst32");
}

TEST_F(AssemblerRISCV64Test, LoadConst64) {
  TestLoadConst64("LoadConst64",
                  /*can_use_tmp=*/ true,
                  [&](XRegister rd, int64_t value) { __ LoadConst64(rd, value); });
}

TEST_F(AssemblerRISCV64Test, AddConst32) {
  auto emit_op = [&](XRegister rd, XRegister rs1, int64_t value) {
    __ AddConst32(rd, rs1, dchecked_integral_cast<int32_t>(value));
  };
  TestAddConst("AddConst32", 32, /*suffix=*/ "w", emit_op);
}

TEST_F(AssemblerRISCV64Test, AddConst64) {
  auto emit_op = [&](XRegister rd, XRegister rs1, int64_t value) {
    __ AddConst64(rd, rs1, value);
  };
  TestAddConst("AddConst64", 64, /*suffix=*/ "", emit_op);
}

TEST_F(AssemblerRISCV64Test, BcondForward3KiB) {
  TestBcondForward("BcondForward3KiB", 3 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BcondBackward3KiB) {
  TestBcondBackward("BcondBackward3KiB", 3 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BcondForward5KiB) {
  TestBcondForward("BcondForward5KiB", 5 * KB, "1", GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BcondBackward5KiB) {
  TestBcondBackward("BcondBackward5KiB", 5 * KB, "1", GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BcondForward2MiB) {
  TestBcondForward("BcondForward2MiB", 2 * MB, "1", GetPrintBcondOppositeAndTail("2", "3"));
}

TEST_F(AssemblerRISCV64Test, BcondBackward2MiB) {
  TestBcondBackward("BcondBackward2MiB", 2 * MB, "1", GetPrintBcondOppositeAndTail("2", "3"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset13Forward) {
  TestBeqA0A1Forward("BeqA0A1MaxOffset13Forward",
                     MaxOffset13ForwardDistance() - /*BEQ*/ 4u,
                     "1",
                     GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset13Backward) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset13Forward",
                      MaxOffset13BackwardDistance(),
                      "1",
                      GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BeqA0A1OverMaxOffset13Forward) {
  TestBeqA0A1Forward("BeqA0A1OverMaxOffset13Forward",
                     MaxOffset13ForwardDistance() - /*BEQ*/ 4u + /*Exceed max*/ 4u,
                     "1",
                     GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1OverMaxOffset13Backward) {
  TestBeqA0A1Backward("BeqA0A1OverMaxOffset13Forward",
                      MaxOffset13BackwardDistance() + /*Exceed max*/ 4u,
                      "1",
                      GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset21Forward) {
  TestBeqA0A1Forward("BeqA0A1MaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u,
                     "1",
                     GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset21Backward) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset21Backward",
                      MaxOffset21BackwardDistance() - /*BNE*/ 4u,
                      "1",
                      GetPrintBcondOppositeAndJ("2"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1OverMaxOffset21Forward) {
  TestBeqA0A1Forward("BeqA0A1OverMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u + /*Exceed max*/ 4u,
                     "1",
                     GetPrintBcondOppositeAndTail("2", "3"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1OverMaxOffset21Backward) {
  TestBeqA0A1Backward("BeqA0A1OverMaxOffset21Backward",
                      MaxOffset21BackwardDistance() - /*BNE*/ 4u + /*Exceed max*/ 4u,
                      "1",
                      GetPrintBcondOppositeAndTail("2", "3"));
}

TEST_F(AssemblerRISCV64Test, BeqA0A1AlmostCascade) {
  TestBeqA0A1MaybeCascade("BeqA0A1AlmostCascade", /*cascade=*/ false, GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BeqA0A1Cascade) {
  TestBeqA0A1MaybeCascade(
      "BeqA0A1AlmostCascade", /*cascade=*/ true, GetPrintBcondOppositeAndJ("1"));
}

TEST_F(AssemblerRISCV64Test, BcondElimination) {
  Riscv64Label label;
  __ Bind(&label);
  __ Nop();
  for (XRegister* reg : GetRegisters()) {
    __ Bne(*reg, *reg, &label);
    __ Blt(*reg, *reg, &label);
    __ Bgt(*reg, *reg, &label);
    __ Bltu(*reg, *reg, &label);
    __ Bgtu(*reg, *reg, &label);
  }
  DriverStr("nop\n", "BcondElimination");
}

TEST_F(AssemblerRISCV64Test, BcondUnconditional) {
  Riscv64Label label;
  __ Bind(&label);
  __ Nop();
  for (XRegister* reg : GetRegisters()) {
    __ Beq(*reg, *reg, &label);
    __ Bge(*reg, *reg, &label);
    __ Ble(*reg, *reg, &label);
    __ Bleu(*reg, *reg, &label);
    __ Bgeu(*reg, *reg, &label);
  }
  std::string expected =
      "1:\n"
      "nop\n" +
      RepeatInsn(5u * GetRegisters().size(), "j 1b\n", []() {});
  DriverStr(expected, "BcondUnconditional");
}

TEST_F(AssemblerRISCV64Test, JalRdForward3KiB) {
  TestJalRdForward("JalRdForward3KiB", 3 * KB, "1", GetPrintJalRd());
}

TEST_F(AssemblerRISCV64Test, JalRdBackward3KiB) {
  TestJalRdBackward("JalRdBackward3KiB", 3 * KB, "1", GetPrintJalRd());
}

TEST_F(AssemblerRISCV64Test, JalRdForward2MiB) {
  TestJalRdForward("JalRdForward2MiB", 2 * MB, "1", GetPrintCallRd("2"));
}

TEST_F(AssemblerRISCV64Test, JalRdBackward2MiB) {
  TestJalRdBackward("JalRdBackward2MiB", 2 * MB, "1", GetPrintCallRd("2"));
}

TEST_F(AssemblerRISCV64Test, JForward3KiB) {
  TestBuncondForward("JForward3KiB", 3 * KB, "1", GetEmitJ(), GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JBackward3KiB) {
  TestBuncondBackward("JBackward3KiB", 3 * KB, "1", GetEmitJ(), GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JForward2MiB) {
  TestBuncondForward("JForward2MiB", 2 * MB, "1", GetEmitJ(), GetPrintTail("2"));
}

TEST_F(AssemblerRISCV64Test, JBackward2MiB) {
  TestBuncondBackward("JBackward2MiB", 2 * MB, "1", GetEmitJ(), GetPrintTail("2"));
}

TEST_F(AssemblerRISCV64Test, JMaxOffset21Forward) {
  TestBuncondForward("JMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u,
                     "1",
                     GetEmitJ(),
                     GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JMaxOffset21Backward) {
  TestBuncondBackward("JMaxOffset21Backward",
                      MaxOffset21BackwardDistance(),
                      "1",
                      GetEmitJ(),
                      GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JOverMaxOffset21Forward) {
  TestBuncondForward("JOverMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u + /*Exceed max*/ 4u,
                     "1",
                     GetEmitJ(),
                     GetPrintTail("2"));
}

TEST_F(AssemblerRISCV64Test, JOverMaxOffset21Backward) {
  TestBuncondBackward("JMaxOffset21Backward",
                      MaxOffset21BackwardDistance() + /*Exceed max*/ 4u,
                      "1",
                      GetEmitJ(),
                      GetPrintTail("2"));
}

TEST_F(AssemblerRISCV64Test, CallForward3KiB) {
  TestBuncondForward("CallForward3KiB", 3 * KB, "1", GetEmitJal(), GetPrintJal());
}

TEST_F(AssemblerRISCV64Test, CallBackward3KiB) {
  TestBuncondBackward("CallBackward3KiB", 3 * KB, "1", GetEmitJal(), GetPrintJal());
}

TEST_F(AssemblerRISCV64Test, CallForward2MiB) {
  TestBuncondForward("CallForward2MiB", 2 * MB, "1", GetEmitJal(), GetPrintCall("2"));
}

TEST_F(AssemblerRISCV64Test, CallBackward2MiB) {
  TestBuncondBackward("CallBackward2MiB", 2 * MB, "1", GetEmitJal(), GetPrintCall("2"));
}

TEST_F(AssemblerRISCV64Test, CallMaxOffset21Forward) {
  TestBuncondForward("CallMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u,
                     "1",
                     GetEmitJal(),
                     GetPrintJal());
}

TEST_F(AssemblerRISCV64Test, CallMaxOffset21Backward) {
  TestBuncondBackward("CallMaxOffset21Backward",
                      MaxOffset21BackwardDistance(),
                      "1",
                      GetEmitJal(),
                      GetPrintJal());
}

TEST_F(AssemblerRISCV64Test, CallOverMaxOffset21Forward) {
  TestBuncondForward("CallOverMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u + /*Exceed max*/ 4u,
                     "1",
                     GetEmitJal(),
                     GetPrintCall("2"));
}

TEST_F(AssemblerRISCV64Test, CallOverMaxOffset21Backward) {
  TestBuncondBackward("CallMaxOffset21Backward",
                      MaxOffset21BackwardDistance() + /*Exceed max*/ 4u,
                      "1",
                      GetEmitJal(),
                      GetPrintCall("2"));
}

TEST_F(AssemblerRISCV64Test, Loadb) {
  TestLoadStoreArbitraryOffset("Loadb", "lb", &Riscv64Assembler::Loadb, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadh) {
  TestLoadStoreArbitraryOffset("Loadh", "lh", &Riscv64Assembler::Loadh, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadw) {
  TestLoadStoreArbitraryOffset("Loadw", "lw", &Riscv64Assembler::Loadw, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadd) {
  TestLoadStoreArbitraryOffset("Loadd", "ld", &Riscv64Assembler::Loadd, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadbu) {
  TestLoadStoreArbitraryOffset("Loadbu", "lbu", &Riscv64Assembler::Loadbu, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadhu) {
  TestLoadStoreArbitraryOffset("Loadhu", "lhu", &Riscv64Assembler::Loadhu, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Loadwu) {
  TestLoadStoreArbitraryOffset("Loadwu", "lwu", &Riscv64Assembler::Loadwu, /*is_store=*/ false);
}

TEST_F(AssemblerRISCV64Test, Storeb) {
  TestLoadStoreArbitraryOffset("Storeb", "sb", &Riscv64Assembler::Storeb, /*is_store=*/ true);
}

TEST_F(AssemblerRISCV64Test, Storeh) {
  TestLoadStoreArbitraryOffset("Storeh", "sh", &Riscv64Assembler::Storeh, /*is_store=*/ true);
}

TEST_F(AssemblerRISCV64Test, Storew) {
  TestLoadStoreArbitraryOffset("Storew", "sw", &Riscv64Assembler::Storew, /*is_store=*/ true);
}

TEST_F(AssemblerRISCV64Test, Stored) {
  TestLoadStoreArbitraryOffset("Stored", "sd", &Riscv64Assembler::Stored, /*is_store=*/ true);
}

TEST_F(AssemblerRISCV64Test, FLoadw) {
  TestFPLoadStoreArbitraryOffset("FLoadw", "flw", &Riscv64Assembler::FLoadw);
}

TEST_F(AssemblerRISCV64Test, FLoadd) {
  TestFPLoadStoreArbitraryOffset("FLoadd", "fld", &Riscv64Assembler::FLoadd);
}

TEST_F(AssemblerRISCV64Test, FStorew) {
  TestFPLoadStoreArbitraryOffset("FStorew", "fsw", &Riscv64Assembler::FStorew);
}

TEST_F(AssemblerRISCV64Test, FStored) {
  TestFPLoadStoreArbitraryOffset("FStored", "fsd", &Riscv64Assembler::FStored);
}

TEST_F(AssemblerRISCV64Test, LoadLabelAddress) {
  std::string expected;
  constexpr size_t kNumLoadsForward = 4 * KB;
  constexpr size_t kNumLoadsBackward = 4 * KB;
  Riscv64Label label;
  auto emit_batch = [&](size_t num_loads, const std::string& target_label) {
    for (size_t i = 0; i != num_loads; ++i) {
      // Cycle through non-Zero registers.
      XRegister rd = enum_cast<XRegister>((i % (kNumberOfXRegisters - 1)) + 1);
      DCHECK_NE(rd, Zero);
      std::string rd_name = GetRegisterName(rd);
      __ LoadLabelAddress(rd, &label);
      expected += "1:\n"
                  "auipc " + rd_name + ", %pcrel_hi(" + target_label + ")\n"
                  "addi " + rd_name + ", " + rd_name + ", %pcrel_lo(1b)\n";
    }
  };
  emit_batch(kNumLoadsForward, "2f");
  __ Bind(&label);
  expected += "2:\n";
  emit_batch(kNumLoadsBackward, "2b");
  DriverStr(expected, "LoadLabelAddress");
}

TEST_F(AssemblerRISCV64Test, LoadLiteralWithPaddingForLong) {
  TestLoadLiteral("LoadLiteralWithPaddingForLong", /*with_padding_for_long=*/ true);
}

TEST_F(AssemblerRISCV64Test, LoadLiteralWithoutPaddingForLong) {
  TestLoadLiteral("LoadLiteralWithoutPaddingForLong", /*with_padding_for_long=*/ false);
}

TEST_F(AssemblerRISCV64Test, JumpTable) {
  std::string expected;
  expected += EmitNops(sizeof(uint32_t));
  Riscv64Label targets[4];
  uint32_t target_locations[4];
  JumpTable* jump_table = __ CreateJumpTable(ArenaVector<Riscv64Label*>(
      {&targets[0], &targets[1], &targets[2], &targets[3]}, __ GetAllocator()->Adapter()));
  for (size_t i : {0, 1, 2, 3}) {
    target_locations[i] = __ CodeSize();
    __ Bind(&targets[i]);
    expected += std::to_string(i) + ":\n";
    expected += EmitNops(sizeof(uint32_t));
  }
  __ LoadLabelAddress(A0, jump_table->GetLabel());
  expected += "4:\n"
              "auipc a0, %pcrel_hi(5f)\n"
              "addi a0, a0, %pcrel_lo(4b)\n";
  expected += EmitNops(sizeof(uint32_t));
  uint32_t label5_location = __ CodeSize();
  auto target_offset = [&](size_t i) {
    // Even with `-mno-relax`, clang assembler does not fully resolve `.4byte 0b - 5b`
    // and emits a relocation, so we need to calculate target offsets ourselves.
    return std::to_string(static_cast<int64_t>(target_locations[i] - label5_location));
  };
  expected += "5:\n"
              ".4byte " + target_offset(0) + "\n"
              ".4byte " + target_offset(1) + "\n"
              ".4byte " + target_offset(2) + "\n"
              ".4byte " + target_offset(3) + "\n";
  DriverStr(expected, "JumpTable");
}

#undef __

}  // namespace riscv64
}  // namespace art
