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

namespace art HIDDEN {
namespace riscv64 {

struct RISCV64CpuRegisterCompare {
  bool operator()(const XRegister& a, const XRegister& b) const { return a < b; }
};

class AssemblerRISCV64Test : public AssemblerTest<Riscv64Assembler,
                                                  Riscv64Label,
                                                  XRegister,
                                                  FRegister,
                                                  int32_t> {
 public:
  using Base = AssemblerTest<Riscv64Assembler,
                             Riscv64Label,
                             XRegister,
                             FRegister,
                             int32_t>;

  AssemblerRISCV64Test()
      : instruction_set_features_(Riscv64InstructionSetFeatures::FromVariant("default", nullptr)) {}

 protected:
  Riscv64Assembler* CreateAssembler(ArenaAllocator* allocator) override {
    return new (allocator) Riscv64Assembler(allocator, instruction_set_features_.get());
  }

  InstructionSet GetIsa() override { return InstructionSet::kRiscv64; }

  // Clang's assembler takes advantage of certain extensions for emitting constants with `li`
  // but our assembler does not. For now, we use a simple `-march` to avoid the divergence.
  // TODO(riscv64): Implement these more efficient patterns in assembler.
  void SetUseSimpleMarch(bool value) {
    use_simple_march_ = value;
  }

  std::vector<std::string> GetAssemblerCommand() override {
    std::vector<std::string> result = Base::GetAssemblerCommand();
    if (use_simple_march_) {
      auto it = std::find_if(result.begin(),
                             result.end(),
                             [](const std::string& s) { return StartsWith(s, "-march="); });
      CHECK(it != result.end());
      *it = "-march=rv64imafd";
    }
    return result;
  }

  std::vector<std::string> GetDisassemblerCommand() override {
    std::vector<std::string> result = Base::GetDisassemblerCommand();
    if (use_simple_march_) {
      auto it = std::find_if(result.begin(),
                             result.end(),
                             [](const std::string& s) { return StartsWith(s, "--mattr="); });
      CHECK(it != result.end());
      *it = "--mattr=+F,+D,+A";
    }
    return result;
  }

  void SetUpHelpers() override {
    if (secondary_register_names_.empty()) {
      secondary_register_names_.emplace(Zero, "zero");
      secondary_register_names_.emplace(RA, "ra");
      secondary_register_names_.emplace(SP, "sp");
      secondary_register_names_.emplace(GP, "gp");
      secondary_register_names_.emplace(TP, "tp");
      secondary_register_names_.emplace(T0, "t0");
      secondary_register_names_.emplace(T1, "t1");
      secondary_register_names_.emplace(T2, "t2");
      secondary_register_names_.emplace(S0, "s0");  // s0/fp
      secondary_register_names_.emplace(S1, "s1");
      secondary_register_names_.emplace(A0, "a0");
      secondary_register_names_.emplace(A1, "a1");
      secondary_register_names_.emplace(A2, "a2");
      secondary_register_names_.emplace(A3, "a3");
      secondary_register_names_.emplace(A4, "a4");
      secondary_register_names_.emplace(A5, "a5");
      secondary_register_names_.emplace(A6, "a6");
      secondary_register_names_.emplace(A7, "a7");
      secondary_register_names_.emplace(S2, "s2");
      secondary_register_names_.emplace(S3, "s3");
      secondary_register_names_.emplace(S4, "s4");
      secondary_register_names_.emplace(S5, "s5");
      secondary_register_names_.emplace(S6, "s6");
      secondary_register_names_.emplace(S7, "s7");
      secondary_register_names_.emplace(S8, "s8");
      secondary_register_names_.emplace(S9, "s9");
      secondary_register_names_.emplace(S10, "s10");
      secondary_register_names_.emplace(S11, "s11");
      secondary_register_names_.emplace(T3, "t3");
      secondary_register_names_.emplace(T4, "t4");
      secondary_register_names_.emplace(T5, "t5");
      secondary_register_names_.emplace(T6, "t6");
    }
  }

  void TearDown() override {
    AssemblerTest::TearDown();
  }

  std::vector<Riscv64Label> GetAddresses() override {
    UNIMPLEMENTED(FATAL) << "Feature not implemented yet";
    UNREACHABLE();
  }

  ArrayRef<const XRegister> GetRegisters() override {
    static constexpr XRegister kXRegisters[] = {
        Zero,
        RA,
        SP,
        GP,
        TP,
        T0,
        T1,
        T2,
        S0,
        S1,
        A0,
        A1,
        A2,
        A3,
        A4,
        A5,
        A6,
        A7,
        S2,
        S3,
        S4,
        S5,
        S6,
        S7,
        S8,
        S9,
        S10,
        S11,
        T3,
        T4,
        T5,
        T6,
    };
    return ArrayRef<const XRegister>(kXRegisters);
  }

  ArrayRef<const FRegister> GetFPRegisters() override {
    static constexpr FRegister kFRegisters[] = {
        FT0,
        FT1,
        FT2,
        FT3,
        FT4,
        FT5,
        FT6,
        FT7,
        FS0,
        FS1,
        FA0,
        FA1,
        FA2,
        FA3,
        FA4,
        FA5,
        FA6,
        FA7,
        FS2,
        FS3,
        FS4,
        FS5,
        FS6,
        FS7,
        FS8,
        FS9,
        FS10,
        FS11,
        FT8,
        FT9,
        FT10,
        FT11,
    };
    return ArrayRef<const FRegister>(kFRegisters);
  }

  std::string GetSecondaryRegisterName(const XRegister& reg) override {
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
    for (XRegister reg : GetRegisters()) {
      ScratchRegisterScope srs(GetAssembler());
      srs.ExcludeXRegister(reg);
      std::string rd = GetRegisterName(reg);
      emit_load_const(reg, -1);
      expected += "li " + rd + ", -1\n";
      emit_load_const(reg, 0);
      expected += "li " + rd + ", 0\n";
      emit_load_const(reg, 1);
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
                                        PrintBcond&& print_bcond,
                                        bool is_bare) {
    XRegister rs = A0;
    __ Beqz(rs, label, is_bare);
    __ Bnez(rs, label, is_bare);
    __ Blez(rs, label, is_bare);
    __ Bgez(rs, label, is_bare);
    __ Bltz(rs, label, is_bare);
    __ Bgtz(rs, label, is_bare);
    XRegister rt = A1;
    __ Beq(rs, rt, label, is_bare);
    __ Bne(rs, rt, label, is_bare);
    __ Ble(rs, rt, label, is_bare);
    __ Bge(rs, rt, label, is_bare);
    __ Blt(rs, rt, label, is_bare);
    __ Bgt(rs, rt, label, is_bare);
    __ Bleu(rs, rt, label, is_bare);
    __ Bgeu(rs, rt, label, is_bare);
    __ Bltu(rs, rt, label, is_bare);
    __ Bgtu(rs, rt, label, is_bare);

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
                        PrintBcond&& print_bcond,
                        bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    expected += EmitBcondForAllConditions(&label, target_label + "f", print_bcond, is_bare);
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
                         PrintBcond&& print_bcond,
                         bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(gap_size);
    expected += EmitBcondForAllConditions(&label, target_label + "b", print_bcond, is_bare);
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
                          PrintBcond&& print_bcond,
                          bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    __ Beq(A0, A1, &label, is_bare);
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
                           PrintBcond&& print_bcond,
                           bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(nops_size);
    __ Beq(A0, A1, &label, is_bare);
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
                        PrintJalRd&& print_jalrd,
                        bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    for (XRegister reg : GetRegisters()) {
      __ Jal(reg, &label, is_bare);
      expected += print_jalrd(reg, label_name + "f");
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
                         PrintJalRd&& print_jalrd,
                         bool is_bare = false) {
    std::string expected;
    Riscv64Label label;
    __ Bind(&label);
    expected += label_name + ":\n";
    expected += EmitNops(gap_size);
    for (XRegister reg : GetRegisters()) {
      __ Jal(reg, &label, is_bare);
      expected += print_jalrd(reg, label_name + "b");
    }
    DriverStr(expected, test_name);
  }

  auto GetEmitJ(bool is_bare = false) {
    return [=](Riscv64Label* label) { __ J(label, is_bare); };
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

    std::string expected;
    for (XRegister rd : GetRegisters()) {
      std::string rd_name = GetRegisterName(rd);
      std::string addi_rd = ART_FORMAT("addi{} {}, ", suffix, rd_name);
      std::string add_rd = ART_FORMAT("add{} {}, ", suffix, rd_name);
      for (XRegister rs1 : GetRegisters()) {
        ScratchRegisterScope srs(GetAssembler());
        srs.ExcludeXRegister(rs1);
        srs.ExcludeXRegister(rd);

        std::string rs1_name = GetRegisterName(rs1);
        std::string tmp_name = GetRegisterName((rs1 != TMP) ? TMP : TMP2);
        std::string addi_tmp = ART_FORMAT("addi{} {}, ", suffix, tmp_name);

        for (int64_t imm : kImm12s) {
          emit_op(rd, rs1, imm);
          expected += ART_FORMAT("{}{}, {}\n", addi_rd, rs1_name, std::to_string(imm));
        }

        auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
          for (int64_t imm : imms) {
            emit_op(rd, rs1, imm);
            expected += ART_FORMAT("{}{}, {}\n", addi_tmp, rs1_name, std::to_string(adjustment));
            expected +=
                ART_FORMAT("{}{}, {}\n", addi_rd, tmp_name, std::to_string(imm - adjustment));
          }
        };
        emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveValues), 0x7ff);
        emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeValues), -0x800);

        for (int64_t imm : large_values) {
          emit_op(rd, rs1, imm);
          expected += ART_FORMAT("li {}, {}\n", tmp_name, std::to_string(imm));
          expected += ART_FORMAT("{}{}, {}\n", add_rd, rs1_name, tmp_name);
        }
      }
    }
    DriverStr(expected, test_name);
  }

  template <typename GetTemp, typename EmitOp>
  std::string RepeatLoadStoreArbitraryOffset(const std::string& head,
                                             GetTemp&& get_temp,
                                             EmitOp&& emit_op) {
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

    std::string expected;
    for (XRegister rs1 : GetRegisters()) {
      XRegister tmp = get_temp(rs1);
      if (tmp == kNoXRegister) {
        continue;  // Unsupported register combination.
      }
      std::string tmp_name = GetRegisterName(tmp);
      ScratchRegisterScope srs(GetAssembler());
      srs.ExcludeXRegister(rs1);
      std::string rs1_name = GetRegisterName(rs1);

      for (int64_t imm : kImm12s) {
        emit_op(rs1, imm);
        expected += ART_FORMAT("{}, {}({})\n", head, std::to_string(imm), rs1_name);
      }

      auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
        for (int64_t imm : imms) {
          emit_op(rs1, imm);
          expected +=
              ART_FORMAT("addi {}, {}, {}\n", tmp_name, rs1_name, std::to_string(adjustment));
          expected += ART_FORMAT("{}, {}({})\n", head, std::to_string(imm - adjustment), tmp_name);
        }
      };
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign8), 0x7f8);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign4), 0x7fc);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign2), 0x7fe);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsNoAlign), 0x7ff);
      emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeOffsets), -0x800);

      for (int64_t imm : kSplitOffsets) {
        emit_op(rs1, imm);
        uint32_t imm20 = ((imm >> 12) + ((imm >> 11) & 1)) & 0xfffff;
        int32_t small_offset = (imm & 0xfff) - ((imm & 0x800) << 1);
        expected += ART_FORMAT("lui {}, {}\n", tmp_name, std::to_string(imm20));
        expected += ART_FORMAT("add {}, {}, {}\n", tmp_name, tmp_name, rs1_name);
        expected += ART_FORMAT("{},{}({})\n", head, std::to_string(small_offset), tmp_name);
      }

      for (int64_t imm : kSpecialOffsets) {
        emit_op(rs1, imm);
        expected += ART_FORMAT("lui {}, 0x80000\n", tmp_name);
        expected +=
            ART_FORMAT("addiw {}, {}, {}\n", tmp_name, tmp_name, std::to_string(imm - 0x80000000));
        expected += ART_FORMAT("add {}, {}, {}\n", tmp_name, tmp_name, rs1_name);
        expected += ART_FORMAT("{}, ({})\n", head, tmp_name);
      }
    }
    return expected;
  }

  void TestLoadStoreArbitraryOffset(const std::string& test_name,
                                    const std::string& insn,
                                    void (Riscv64Assembler::*fn)(XRegister, XRegister, int32_t),
                                    bool is_store) {
    std::string expected;
    for (XRegister rd : GetRegisters()) {
      ScratchRegisterScope srs(GetAssembler());
      srs.ExcludeXRegister(rd);
      auto get_temp = [&](XRegister rs1) {
        if (is_store) {
          return (rs1 != TMP && rd != TMP)
              ? TMP
              : (rs1 != TMP2 && rd != TMP2) ? TMP2 : kNoXRegister;
        } else {
          return rs1 != TMP ? TMP : TMP2;
        }
      };
      expected += RepeatLoadStoreArbitraryOffset(
          insn + " " + GetRegisterName(rd),
          get_temp,
          [&](XRegister rs1, int64_t offset) { (GetAssembler()->*fn)(rd, rs1, offset); });
    }
    DriverStr(expected, test_name);
  }

  void TestFPLoadStoreArbitraryOffset(const std::string& test_name,
                                      const std::string& insn,
                                      void (Riscv64Assembler::*fn)(FRegister, XRegister, int32_t)) {
    std::string expected;
    for (FRegister rd : GetFPRegisters()) {
      expected += RepeatLoadStoreArbitraryOffset(
          insn + " " + GetFPRegName(rd),
          [&](XRegister rs1) { return rs1 != TMP ? TMP : TMP2; },
          [&](XRegister rs1, int64_t offset) { (GetAssembler()->*fn)(rd, rs1, offset); });
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
    for (XRegister reg : GetRegisters()) {
      if (reg != Zero) {
        __ Loadw(reg, narrow_literal);
        print_load("lw", reg, "2");
        __ Loadwu(reg, narrow_literal);
        print_load("lwu", reg, "2");
        __ Loadd(reg, wide_literal);
        print_load("ld", reg, "3");
      }
    }
    std::string tmp = GetRegisterName(TMP);
    auto print_fp_load = [&](const std::string& load, FRegister rd, const std::string& label) {
      std::string rd_name = GetFPRegName(rd);
      expected += "1:\n"
                  "auipc " + tmp + ", %pcrel_hi(" + label + "f)\n" +
                  load + " " + rd_name + ", %pcrel_lo(1b)(" + tmp + ")\n";
    };
    for (FRegister freg : GetFPRegisters()) {
      __ FLoadw(freg, narrow_literal);
      print_fp_load("flw", freg, "2");
      __ FLoadd(freg, wide_literal);
      print_fp_load("fld", freg, "3");
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

  std::string RepeatFFFFRoundingMode(
      void (Riscv64Assembler::*f)(FRegister, FRegister, FRegister, FRegister, FPRoundingMode),
      const std::string& fmt) {
    CHECK(f != nullptr);
    std::string str;
    for (FRegister reg1 : GetFPRegisters()) {
      for (FRegister reg2 : GetFPRegisters()) {
        for (FRegister reg3 : GetFPRegisters()) {
          for (FRegister reg4 : GetFPRegisters()) {
            for (FPRoundingMode rm : kRoundingModes) {
              (GetAssembler()->*f)(reg1, reg2, reg3, reg4, rm);

              std::string base = fmt;
              ReplaceReg(REG1_TOKEN, GetFPRegName(reg1), &base);
              ReplaceReg(REG2_TOKEN, GetFPRegName(reg2), &base);
              ReplaceReg(REG3_TOKEN, GetFPRegName(reg3), &base);
              ReplaceReg(REG4_TOKEN, GetFPRegName(reg4), &base);
              ReplaceRoundingMode(rm, &base);
              str += base;
              str += "\n";
            }
          }
        }
      }
    }
    return str;
  }

  std::string RepeatFFFRoundingMode(
      void (Riscv64Assembler::*f)(FRegister, FRegister, FRegister, FPRoundingMode),
      const std::string& fmt) {
    CHECK(f != nullptr);
    std::string str;
    for (FRegister reg1 : GetFPRegisters()) {
      for (FRegister reg2 : GetFPRegisters()) {
        for (FRegister reg3 : GetFPRegisters()) {
          for (FPRoundingMode rm : kRoundingModes) {
            (GetAssembler()->*f)(reg1, reg2, reg3, rm);

            std::string base = fmt;
            ReplaceReg(REG1_TOKEN, GetFPRegName(reg1), &base);
            ReplaceReg(REG2_TOKEN, GetFPRegName(reg2), &base);
            ReplaceReg(REG3_TOKEN, GetFPRegName(reg3), &base);
            ReplaceRoundingMode(rm, &base);
            str += base;
            str += "\n";
          }
        }
      }
    }
    return str;
  }

  template <typename Reg1, typename Reg2>
  std::string RepeatTemplatedRegistersRoundingMode(
      void (Riscv64Assembler::*f)(Reg1, Reg2, FPRoundingMode),
      ArrayRef<const Reg1> reg1_registers,
      ArrayRef<const Reg2> reg2_registers,
      std::string (Base::*GetName1)(const Reg1&),
      std::string (Base::*GetName2)(const Reg2&),
      const std::string& fmt) {
    CHECK(f != nullptr);
    std::string str;
    for (Reg1 reg1 : reg1_registers) {
      for (Reg2 reg2 : reg2_registers) {
        for (FPRoundingMode rm : kRoundingModes) {
          (GetAssembler()->*f)(reg1, reg2, rm);

          std::string base = fmt;
          ReplaceReg(REG1_TOKEN, (this->*GetName1)(reg1), &base);
          ReplaceReg(REG2_TOKEN, (this->*GetName2)(reg2), &base);
          ReplaceRoundingMode(rm, &base);
          str += base;
          str += "\n";
        }
      }
    }
    return str;
  }

  std::string RepeatFFRoundingMode(
      void (Riscv64Assembler::*f)(FRegister, FRegister, FPRoundingMode),
      const std::string& fmt) {
    return RepeatTemplatedRegistersRoundingMode(f,
                                                GetFPRegisters(),
                                                GetFPRegisters(),
                                                &AssemblerRISCV64Test::GetFPRegName,
                                                &AssemblerRISCV64Test::GetFPRegName,
                                                fmt);
  }

  std::string RepeatrFRoundingMode(
      void (Riscv64Assembler::*f)(XRegister, FRegister, FPRoundingMode),
      const std::string& fmt) {
    return RepeatTemplatedRegistersRoundingMode(f,
                                                GetRegisters(),
                                                GetFPRegisters(),
                                                &Base::GetSecondaryRegisterName,
                                                &AssemblerRISCV64Test::GetFPRegName,
                                                fmt);
  }

  std::string RepeatFrRoundingMode(
      void (Riscv64Assembler::*f)(FRegister, XRegister, FPRoundingMode),
      const std::string& fmt) {
    return RepeatTemplatedRegistersRoundingMode(f,
                                                GetFPRegisters(),
                                                GetRegisters(),
                                                &AssemblerRISCV64Test::GetFPRegName,
                                                &Base::GetSecondaryRegisterName,
                                                fmt);
  }

  template <typename InvalidAqRl>
  std::string RepeatRRAqRl(void (Riscv64Assembler::*f)(XRegister, XRegister, AqRl),
                           const std::string& fmt,
                           InvalidAqRl&& invalid_aqrl) {
    CHECK(f != nullptr);
    std::string str;
    for (XRegister reg1 : GetRegisters()) {
      for (XRegister reg2 : GetRegisters()) {
        for (AqRl aqrl : kAqRls) {
          if (invalid_aqrl(aqrl)) {
            continue;
          }
          (GetAssembler()->*f)(reg1, reg2, aqrl);

          std::string base = fmt;
          ReplaceReg(REG1_TOKEN, GetRegisterName(reg1), &base);
          ReplaceReg(REG2_TOKEN, GetRegisterName(reg2), &base);
          ReplaceAqRl(aqrl, &base);
          str += base;
          str += "\n";
        }
      }
    }
    return str;
  }

  template <typename InvalidAqRl>
  std::string RepeatRRRAqRl(void (Riscv64Assembler::*f)(XRegister, XRegister, XRegister, AqRl),
                            const std::string& fmt,
                            InvalidAqRl&& invalid_aqrl) {
    CHECK(f != nullptr);
    std::string str;
    for (XRegister reg1 : GetRegisters()) {
      for (XRegister reg2 : GetRegisters()) {
        for (XRegister reg3 : GetRegisters()) {
          for (AqRl aqrl : kAqRls) {
            if (invalid_aqrl(aqrl)) {
              continue;
            }
            (GetAssembler()->*f)(reg1, reg2, reg3, aqrl);

            std::string base = fmt;
            ReplaceReg(REG1_TOKEN, GetRegisterName(reg1), &base);
            ReplaceReg(REG2_TOKEN, GetRegisterName(reg2), &base);
            ReplaceReg(REG3_TOKEN, GetRegisterName(reg3), &base);
            ReplaceAqRl(aqrl, &base);
            str += base;
            str += "\n";
          }
        }
      }
    }
    return str;
  }

  std::string RepeatRRRAqRl(void (Riscv64Assembler::*f)(XRegister, XRegister, XRegister, AqRl),
                            const std::string& fmt) {
    return RepeatRRRAqRl(f, fmt, [](AqRl) { return false; });
  }

  std::string RepeatCsrrX(void (Riscv64Assembler::*f)(XRegister, uint32_t, XRegister),
                          const std::string& fmt) {
    CHECK(f != nullptr);
    std::vector<int64_t> csrs = CreateImmediateValuesBits(12, /*as_uint=*/ true);
    std::string str;
    for (XRegister reg1 : GetRegisters()) {
      for (int64_t csr : csrs) {
        for (XRegister reg2 : GetRegisters()) {
          (GetAssembler()->*f)(reg1, dchecked_integral_cast<uint32_t>(csr), reg2);

          std::string base = fmt;
          ReplaceReg(REG1_TOKEN, GetRegisterName(reg1), &base);
          ReplaceCsrrImm(CSR_TOKEN, csr, &base);
          ReplaceReg(REG2_TOKEN, GetRegisterName(reg2), &base);
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
    for (XRegister reg : GetRegisters()) {
      for (int64_t csr : csrs) {
        for (int64_t uimm : uimms) {
          (GetAssembler()->*f)(
              reg, dchecked_integral_cast<uint32_t>(csr), dchecked_integral_cast<uint32_t>(uimm));

          std::string base = fmt;
          ReplaceReg(REG_TOKEN, GetRegisterName(reg), &base);
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
    for (XRegister reg : GetRegisters()) {
      for (int64_t csr : csrs) {
        emit_csrrx(dchecked_integral_cast<uint32_t>(csr), reg);

        std::string base = fmt;
        ReplaceReg(REG_TOKEN, GetRegisterName(reg), &base);
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
  static constexpr const char* RM_TOKEN = "{rm}";
  static constexpr const char* AQRL_TOKEN = "{aqrl}";
  static constexpr const char* CSR_TOKEN = "{csr}";
  static constexpr const char* UIMM_TOKEN = "{uimm}";

  static constexpr AqRl kAqRls[] = { AqRl::kNone, AqRl::kRelease, AqRl::kAcquire, AqRl::kAqRl };

  static constexpr FPRoundingMode kRoundingModes[] = {
      FPRoundingMode::kRNE,
      FPRoundingMode::kRTZ,
      FPRoundingMode::kRDN,
      FPRoundingMode::kRUP,
      FPRoundingMode::kRMM,
      FPRoundingMode::kDYN
  };

  void ReplaceRoundingMode(FPRoundingMode rm, /*inout*/ std::string* str) {
    const char* replacement;
    switch (rm) {
      case FPRoundingMode::kRNE:
        replacement = "rne";
        break;
      case FPRoundingMode::kRTZ:
        replacement = "rtz";
        break;
      case FPRoundingMode::kRDN:
        replacement = "rdn";
        break;
      case FPRoundingMode::kRUP:
        replacement = "rup";
        break;
      case FPRoundingMode::kRMM:
        replacement = "rmm";
        break;
      case FPRoundingMode::kDYN:
        replacement = "dyn";
        break;
      default:
        LOG(FATAL) << "Unexpected value for rm: " << enum_cast<uint32_t>(rm);
        UNREACHABLE();
    }
    size_t rm_index = str->find(RM_TOKEN);
    EXPECT_NE(rm_index, std::string::npos);
    if (rm_index != std::string::npos) {
      str->replace(rm_index, ConstexprStrLen(RM_TOKEN), replacement);
    }
  }

  void ReplaceAqRl(AqRl aqrl, /*inout*/ std::string* str) {
    const char* replacement;
    switch (aqrl) {
      case AqRl::kNone:
        replacement = "";
        break;
      case AqRl::kRelease:
        replacement = ".rl";
        break;
      case AqRl::kAcquire:
        replacement = ".aq";
        break;
      case AqRl::kAqRl:
        replacement = ".aqrl";
        break;
      default:
        LOG(FATAL) << "Unexpected value for `aqrl`: " << enum_cast<uint32_t>(aqrl);
        UNREACHABLE();
    }
    size_t aqrl_index = str->find(AQRL_TOKEN);
    EXPECT_NE(aqrl_index, std::string::npos);
    if (aqrl_index != std::string::npos) {
      str->replace(aqrl_index, ConstexprStrLen(AQRL_TOKEN), replacement);
    }
  }

  static void ReplaceCsrrImm(const std::string& imm_token,
                             int64_t imm,
                             /*inout*/ std::string* str) {
    size_t imm_index = str->find(imm_token);
    EXPECT_NE(imm_index, std::string::npos);
    if (imm_index != std::string::npos) {
      str->replace(imm_index, imm_token.length(), std::to_string(imm));
    }
  }

  std::map<XRegister, std::string, RISCV64CpuRegisterCompare> secondary_register_names_;

  std::unique_ptr<const Riscv64InstructionSetFeatures> instruction_set_features_;
  bool use_simple_march_ = false;
};

TEST_F(AssemblerRISCV64Test, Toolchain) { EXPECT_TRUE(CheckTools()); }

TEST_F(AssemblerRISCV64Test, Lui) {
  DriverStr(RepeatRIb(&Riscv64Assembler::Lui, 20, "lui {reg}, {imm}"), "Lui");
}

TEST_F(AssemblerRISCV64Test, Auipc) {
  DriverStr(RepeatRIb(&Riscv64Assembler::Auipc, 20, "auipc {reg}, {imm}"), "Auipc");
}

TEST_F(AssemblerRISCV64Test, Jal) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Jal, -19, 2, "jal {reg}, {imm}\n"), "Jal");
}

TEST_F(AssemblerRISCV64Test, Jalr) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIb(&Riscv64Assembler::Jalr, -12, "jalr {reg1}, {reg2}, {imm}\n"), "Jalr");
}

TEST_F(AssemblerRISCV64Test, Beq) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Beq, -11, 2, "beq {reg1}, {reg2}, {imm}\n"), "Beq");
}

TEST_F(AssemblerRISCV64Test, Bne) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bne, -11, 2, "bne {reg1}, {reg2}, {imm}\n"), "Bne");
}

TEST_F(AssemblerRISCV64Test, Blt) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Blt, -11, 2, "blt {reg1}, {reg2}, {imm}\n"), "Blt");
}

TEST_F(AssemblerRISCV64Test, Bge) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bge, -11, 2, "bge {reg1}, {reg2}, {imm}\n"), "Bge");
}

TEST_F(AssemblerRISCV64Test, Bltu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bltu, -11, 2, "bltu {reg1}, {reg2}, {imm}\n"), "Bltu");
}

TEST_F(AssemblerRISCV64Test, Bgeu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bgeu, -11, 2, "bgeu {reg1}, {reg2}, {imm}\n"), "Bgeu");
}

TEST_F(AssemblerRISCV64Test, Lb) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lb, -12, "lb {reg1}, {imm}({reg2})"), "Lb");
}

TEST_F(AssemblerRISCV64Test, Lh) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lh, -12, "lh {reg1}, {imm}({reg2})"), "Lh");
}

TEST_F(AssemblerRISCV64Test, Lw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lw, -12, "lw {reg1}, {imm}({reg2})"), "Lw");
}

TEST_F(AssemblerRISCV64Test, Ld) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Ld, -12, "ld {reg1}, {imm}({reg2})"), "Ld");
}

TEST_F(AssemblerRISCV64Test, Lbu) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lbu, -12, "lbu {reg1}, {imm}({reg2})"), "Lbu");
}

TEST_F(AssemblerRISCV64Test, Lhu) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lhu, -12, "lhu {reg1}, {imm}({reg2})"), "Lhu");
}

TEST_F(AssemblerRISCV64Test, Lwu) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Lwu, -12, "lwu {reg1}, {imm}({reg2})"), "Lwu");
}

TEST_F(AssemblerRISCV64Test, Sb) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sb, -12, "sb {reg1}, {imm}({reg2})"), "Sb");
}

TEST_F(AssemblerRISCV64Test, Sh) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sh, -12, "sh {reg1}, {imm}({reg2})"), "Sh");
}

TEST_F(AssemblerRISCV64Test, Sw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sw, -12, "sw {reg1}, {imm}({reg2})"), "Sw");
}

TEST_F(AssemblerRISCV64Test, Sd) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sd, -12, "sd {reg1}, {imm}({reg2})"), "Sd");
}

TEST_F(AssemblerRISCV64Test, Addi) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Addi, -12, "addi {reg1}, {reg2}, {imm}"), "Addi");
}

TEST_F(AssemblerRISCV64Test, Slti) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Slti, -12, "slti {reg1}, {reg2}, {imm}"), "Slti");
}

TEST_F(AssemblerRISCV64Test, Sltiu) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sltiu, -12, "sltiu {reg1}, {reg2}, {imm}"), "Sltiu");
}

TEST_F(AssemblerRISCV64Test, Xori) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Xori, 11, "xori {reg1}, {reg2}, {imm}"), "Xori");
}

TEST_F(AssemblerRISCV64Test, Ori) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Ori, -12, "ori {reg1}, {reg2}, {imm}"), "Ori");
}

TEST_F(AssemblerRISCV64Test, Andi) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Andi, -12, "andi {reg1}, {reg2}, {imm}"), "Andi");
}

TEST_F(AssemblerRISCV64Test, Slli) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Slli, 6, "slli {reg1}, {reg2}, {imm}"), "Slli");
}

TEST_F(AssemblerRISCV64Test, Srli) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Srli, 6, "srli {reg1}, {reg2}, {imm}"), "Srli");
}

TEST_F(AssemblerRISCV64Test, Srai) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Srai, 6, "srai {reg1}, {reg2}, {imm}"), "Srai");
}

TEST_F(AssemblerRISCV64Test, Add) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Add, "add {reg1}, {reg2}, {reg3}"), "Add");
}

TEST_F(AssemblerRISCV64Test, Sub) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sub, "sub {reg1}, {reg2}, {reg3}"), "Sub");
}

TEST_F(AssemblerRISCV64Test, Slt) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Slt, "slt {reg1}, {reg2}, {reg3}"), "Slt");
}

TEST_F(AssemblerRISCV64Test, Sltu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sltu, "sltu {reg1}, {reg2}, {reg3}"), "Sltu");
}

TEST_F(AssemblerRISCV64Test, Xor) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Xor, "xor {reg1}, {reg2}, {reg3}"), "Xor");
}

TEST_F(AssemblerRISCV64Test, Or) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Or, "or {reg1}, {reg2}, {reg3}"), "Or");
}

TEST_F(AssemblerRISCV64Test, And) {
  DriverStr(RepeatRRR(&Riscv64Assembler::And, "and {reg1}, {reg2}, {reg3}"), "And");
}

TEST_F(AssemblerRISCV64Test, Sll) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sll, "sll {reg1}, {reg2}, {reg3}"), "Sll");
}

TEST_F(AssemblerRISCV64Test, Srl) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Srl, "srl {reg1}, {reg2}, {reg3}"), "Srl");
}

TEST_F(AssemblerRISCV64Test, Sra) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sra, "sra {reg1}, {reg2}, {reg3}"), "Sra");
}

TEST_F(AssemblerRISCV64Test, Addiw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Addiw, -12, "addiw {reg1}, {reg2}, {imm}"), "Addiw");
}

TEST_F(AssemblerRISCV64Test, Slliw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Slliw, 5, "slliw {reg1}, {reg2}, {imm}"), "Slliw");
}

TEST_F(AssemblerRISCV64Test, Srliw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Srliw, 5, "srliw {reg1}, {reg2}, {imm}"), "Srliw");
}

TEST_F(AssemblerRISCV64Test, Sraiw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Sraiw, 5, "sraiw {reg1}, {reg2}, {imm}"), "Sraiw");
}

TEST_F(AssemblerRISCV64Test, Addw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Addw, "addw {reg1}, {reg2}, {reg3}"), "Addw");
}

TEST_F(AssemblerRISCV64Test, Subw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Subw, "subw {reg1}, {reg2}, {reg3}"), "Subw");
}

TEST_F(AssemblerRISCV64Test, Sllw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sllw, "sllw {reg1}, {reg2}, {reg3}"), "Sllw");
}

TEST_F(AssemblerRISCV64Test, Srlw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Srlw, "srlw {reg1}, {reg2}, {reg3}"), "Srlw");
}

TEST_F(AssemblerRISCV64Test, Sraw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sraw, "sraw {reg1}, {reg2}, {reg3}"), "Sraw");
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
  DriverStr(RepeatRRR(&Riscv64Assembler::Mul, "mul {reg1}, {reg2}, {reg3}"), "Mul");
}

TEST_F(AssemblerRISCV64Test, Mulh) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Mulh, "mulh {reg1}, {reg2}, {reg3}"), "Mulh");
}

TEST_F(AssemblerRISCV64Test, Mulhsu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Mulhsu, "mulhsu {reg1}, {reg2}, {reg3}"), "Mulhsu");
}

TEST_F(AssemblerRISCV64Test, Mulhu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Mulhu, "mulhu {reg1}, {reg2}, {reg3}"), "Mulhu");
}

TEST_F(AssemblerRISCV64Test, Div) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Div, "div {reg1}, {reg2}, {reg3}"), "Div");
}

TEST_F(AssemblerRISCV64Test, Divu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Divu, "divu {reg1}, {reg2}, {reg3}"), "Divu");
}

TEST_F(AssemblerRISCV64Test, Rem) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Rem, "rem {reg1}, {reg2}, {reg3}"), "Rem");
}

TEST_F(AssemblerRISCV64Test, Remu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Remu, "remu {reg1}, {reg2}, {reg3}"), "Remu");
}

TEST_F(AssemblerRISCV64Test, Mulw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Mulw, "mulw {reg1}, {reg2}, {reg3}"), "Mulw");
}

TEST_F(AssemblerRISCV64Test, Divw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Divw, "divw {reg1}, {reg2}, {reg3}"), "Divw");
}

TEST_F(AssemblerRISCV64Test, Divuw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Divuw, "divuw {reg1}, {reg2}, {reg3}"), "Divuw");
}

TEST_F(AssemblerRISCV64Test, Remw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Remw, "remw {reg1}, {reg2}, {reg3}"), "Remw");
}

TEST_F(AssemblerRISCV64Test, Remuw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Remuw, "remuw {reg1}, {reg2}, {reg3}"), "Remuw");
}

TEST_F(AssemblerRISCV64Test, LrW) {
  auto invalid_aqrl = [](AqRl aqrl) { return aqrl == AqRl::kRelease; };
  DriverStr(RepeatRRAqRl(&Riscv64Assembler::LrW, "lr.w{aqrl} {reg1}, ({reg2})", invalid_aqrl),
            "LrW");
}

TEST_F(AssemblerRISCV64Test, LrD) {
  auto invalid_aqrl = [](AqRl aqrl) { return aqrl == AqRl::kRelease; };
  DriverStr(RepeatRRAqRl(&Riscv64Assembler::LrD, "lr.d{aqrl} {reg1}, ({reg2})", invalid_aqrl),
            "LrD");
}

TEST_F(AssemblerRISCV64Test, ScW) {
  auto invalid_aqrl = [](AqRl aqrl) { return aqrl == AqRl::kAcquire; };
  DriverStr(
      RepeatRRRAqRl(&Riscv64Assembler::ScW, "sc.w{aqrl} {reg1}, {reg2}, ({reg3})", invalid_aqrl),
      "ScW");
}

TEST_F(AssemblerRISCV64Test, ScD) {
  auto invalid_aqrl = [](AqRl aqrl) { return aqrl == AqRl::kAcquire; };
  DriverStr(
      RepeatRRRAqRl(&Riscv64Assembler::ScD, "sc.d{aqrl} {reg1}, {reg2}, ({reg3})", invalid_aqrl),
      "ScD");
}

TEST_F(AssemblerRISCV64Test, AmoSwapW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoSwapW, "amoswap.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoSwapW");
}

TEST_F(AssemblerRISCV64Test, AmoSwapD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoSwapD, "amoswap.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoSwapD");
}

TEST_F(AssemblerRISCV64Test, AmoAddW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoAddW, "amoadd.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAddW");
}

TEST_F(AssemblerRISCV64Test, AmoAddD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoAddD, "amoadd.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAddD");
}

TEST_F(AssemblerRISCV64Test, AmoXorW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoXorW, "amoxor.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoXorW");
}

TEST_F(AssemblerRISCV64Test, AmoXorD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoXorD, "amoxor.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoXorD");
}

TEST_F(AssemblerRISCV64Test, AmoAndW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoAndW, "amoand.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAndW");
}

TEST_F(AssemblerRISCV64Test, AmoAndD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoAndD, "amoand.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoAndD");
}

TEST_F(AssemblerRISCV64Test, AmoOrW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoOrW, "amoor.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoOrW");
}

TEST_F(AssemblerRISCV64Test, AmoOrD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoOrD, "amoor.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoOrD");
}

TEST_F(AssemblerRISCV64Test, AmoMinW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMinW, "amomin.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinW");
}

TEST_F(AssemblerRISCV64Test, AmoMinD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMinD, "amomin.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinD");
}

TEST_F(AssemblerRISCV64Test, AmoMaxW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMaxW, "amomax.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxW");
}

TEST_F(AssemblerRISCV64Test, AmoMaxD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMaxD, "amomax.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxD");
}

TEST_F(AssemblerRISCV64Test, AmoMinuW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMinuW, "amominu.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinuW");
}

TEST_F(AssemblerRISCV64Test, AmoMinuD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMinuD, "amominu.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMinuD");
}

TEST_F(AssemblerRISCV64Test, AmoMaxuW) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMaxuW, "amomaxu.w{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxuW");
}

TEST_F(AssemblerRISCV64Test, AmoMaxuD) {
  DriverStr(RepeatRRRAqRl(&Riscv64Assembler::AmoMaxuD, "amomaxu.d{aqrl} {reg1}, {reg2}, ({reg3})"),
            "AmoMaxuD");
}

TEST_F(AssemblerRISCV64Test, Csrrw) {
  DriverStr(RepeatCsrrX(&Riscv64Assembler::Csrrw, "csrrw {reg1}, {csr}, {reg2}"), "Csrrw");
}

TEST_F(AssemblerRISCV64Test, Csrrs) {
  DriverStr(RepeatCsrrX(&Riscv64Assembler::Csrrs, "csrrs {reg1}, {csr}, {reg2}"), "Csrrs");
}

TEST_F(AssemblerRISCV64Test, Csrrc) {
  DriverStr(RepeatCsrrX(&Riscv64Assembler::Csrrc, "csrrc {reg1}, {csr}, {reg2}"), "Csrrc");
}

TEST_F(AssemblerRISCV64Test, Csrrwi) {
  DriverStr(RepeatCsrrXi(&Riscv64Assembler::Csrrwi, "csrrwi {reg}, {csr}, {uimm}"), "Csrrwi");
}

TEST_F(AssemblerRISCV64Test, Csrrsi) {
  DriverStr(RepeatCsrrXi(&Riscv64Assembler::Csrrsi, "csrrsi {reg}, {csr}, {uimm}"), "Csrrsi");
}

TEST_F(AssemblerRISCV64Test, Csrrci) {
  DriverStr(RepeatCsrrXi(&Riscv64Assembler::Csrrci, "csrrci {reg}, {csr}, {uimm}"), "Csrrci");
}

TEST_F(AssemblerRISCV64Test, FLw) {
  DriverStr(RepeatFRIb(&Riscv64Assembler::FLw, -12, "flw {reg1}, {imm}({reg2})"), "FLw");
}

TEST_F(AssemblerRISCV64Test, FLd) {
  DriverStr(RepeatFRIb(&Riscv64Assembler::FLd, -12, "fld {reg1}, {imm}({reg2})"), "FLw");
}

TEST_F(AssemblerRISCV64Test, FSw) {
  DriverStr(RepeatFRIb(&Riscv64Assembler::FSw, 2, "fsw {reg1}, {imm}({reg2})"), "FSw");
}

TEST_F(AssemblerRISCV64Test, FSd) {
  DriverStr(RepeatFRIb(&Riscv64Assembler::FSd, 2, "fsd {reg1}, {imm}({reg2})"), "FSd");
}

TEST_F(AssemblerRISCV64Test, FMAddS) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FMAddS,
                                   "fmadd.s {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FMAddS");
}

TEST_F(AssemblerRISCV64Test, FMAddS_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FMAddS, "fmadd.s {reg1}, {reg2}, {reg3}, {reg4}"),
            "FMAddS_Default");
}

TEST_F(AssemblerRISCV64Test, FMAddD) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FMAddD,
                                   "fmadd.d {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FMAddD");
}

TEST_F(AssemblerRISCV64Test, FMAddD_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FMAddD, "fmadd.d {reg1}, {reg2}, {reg3}, {reg4}"),
            "FMAddD_Default");
}

TEST_F(AssemblerRISCV64Test, FMSubS) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FMSubS,
                                   "fmsub.s {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FMSubS");
}

TEST_F(AssemblerRISCV64Test, FMSubS_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FMSubS, "fmsub.s {reg1}, {reg2}, {reg3}, {reg4}"),
            "FMSubS_Default");
}

TEST_F(AssemblerRISCV64Test, FMSubD) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FMSubD,
                                  "fmsub.d {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FMSubD");
}

TEST_F(AssemblerRISCV64Test, FMSubD_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FMSubD, "fmsub.d {reg1}, {reg2}, {reg3}, {reg4}"),
            "FMSubD_Default");
}

TEST_F(AssemblerRISCV64Test, FNMSubS) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FNMSubS,
                                   "fnmsub.s {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FNMSubS");
}

TEST_F(AssemblerRISCV64Test, FNMSubS_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FNMSubS, "fnmsub.s {reg1}, {reg2}, {reg3}, {reg4}"),
            "FNMSubS_Default");
}

TEST_F(AssemblerRISCV64Test, FNMSubD) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FNMSubD,
                                   "fnmsub.d {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FNMSubD");
}

TEST_F(AssemblerRISCV64Test, FNMSubD_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FNMSubD, "fnmsub.d {reg1}, {reg2}, {reg3}, {reg4}"),
            "FNMSubD_Default");
}

TEST_F(AssemblerRISCV64Test, FNMAddS) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FNMAddS,
                                   "fnmadd.s {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FNMAddS");
}

TEST_F(AssemblerRISCV64Test, FNMAddS_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FNMAddS, "fnmadd.s {reg1}, {reg2}, {reg3}, {reg4}"),
            "FNMAddS_Default");
}

TEST_F(AssemblerRISCV64Test, FNMAddD) {
  DriverStr(RepeatFFFFRoundingMode(&Riscv64Assembler::FNMAddD,
                                   "fnmadd.d {reg1}, {reg2}, {reg3}, {reg4}, {rm}"), "FNMAddD");
}

TEST_F(AssemblerRISCV64Test, FNMAddD_Default) {
  DriverStr(RepeatFFFF(&Riscv64Assembler::FNMAddD, "fnmadd.d {reg1}, {reg2}, {reg3}, {reg4}"),
            "FNMAddD_Default");
}

TEST_F(AssemblerRISCV64Test, FAddS) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FAddS, "fadd.s {reg1}, {reg2}, {reg3}, {rm}"),
            "FAddS");
}

TEST_F(AssemblerRISCV64Test, FAddS_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FAddS, "fadd.s {reg1}, {reg2}, {reg3}"), "FAddS_Default");
}

TEST_F(AssemblerRISCV64Test, FAddD) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FAddD, "fadd.d {reg1}, {reg2}, {reg3}, {rm}"),
            "FAddD");
}

TEST_F(AssemblerRISCV64Test, FAddD_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FAddD, "fadd.d {reg1}, {reg2}, {reg3}"), "FAddD_Default");
}

TEST_F(AssemblerRISCV64Test, FSubS) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FSubS, "fsub.s {reg1}, {reg2}, {reg3}, {rm}"),
            "FSubS");
}

TEST_F(AssemblerRISCV64Test, FSubS_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSubS, "fsub.s {reg1}, {reg2}, {reg3}"), "FSubS_Default");
}

TEST_F(AssemblerRISCV64Test, FSubD) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FSubD, "fsub.d {reg1}, {reg2}, {reg3}, {rm}"),
            "FSubD");
}

TEST_F(AssemblerRISCV64Test, FSubD_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSubD, "fsub.d {reg1}, {reg2}, {reg3}"), "FSubD_Default");
}

TEST_F(AssemblerRISCV64Test, FMulS) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FMulS, "fmul.s {reg1}, {reg2}, {reg3}, {rm}"),
            "FMulS");
}

TEST_F(AssemblerRISCV64Test, FMulS_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMulS, "fmul.s {reg1}, {reg2}, {reg3}"), "FMulS_Default");
}

TEST_F(AssemblerRISCV64Test, FMulD) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FMulD, "fmul.d {reg1}, {reg2}, {reg3}, {rm}"),
            "FMulD");
}

TEST_F(AssemblerRISCV64Test, FMulD_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMulD, "fmul.d {reg1}, {reg2}, {reg3}"), "FMulD_Default");
}

TEST_F(AssemblerRISCV64Test, FDivS) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FDivS, "fdiv.s {reg1}, {reg2}, {reg3}, {rm}"),
            "FDivS");
}

TEST_F(AssemblerRISCV64Test, FDivS_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FDivS, "fdiv.s {reg1}, {reg2}, {reg3}"), "FDivS_Default");
}

TEST_F(AssemblerRISCV64Test, FDivD) {
  DriverStr(RepeatFFFRoundingMode(&Riscv64Assembler::FDivD, "fdiv.d {reg1}, {reg2}, {reg3}, {rm}"),
            "FDivD");
}

TEST_F(AssemblerRISCV64Test, FDivD_Default) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FDivD, "fdiv.d {reg1}, {reg2}, {reg3}"), "FDivD_Default");
}

TEST_F(AssemblerRISCV64Test, FSqrtS) {
  DriverStr(RepeatFFRoundingMode(&Riscv64Assembler::FSqrtS, "fsqrt.s {reg1}, {reg2}, {rm}"),
            "FSqrtS");
}

TEST_F(AssemblerRISCV64Test, FSqrtS_Default) {
  DriverStr(RepeatFF(&Riscv64Assembler::FSqrtS, "fsqrt.s {reg1}, {reg2}"), "FSqrtS_Default");
}

TEST_F(AssemblerRISCV64Test, FSqrtD) {
  DriverStr(RepeatFFRoundingMode(&Riscv64Assembler::FSqrtD, "fsqrt.d {reg1}, {reg2}, {rm}"),
            "FSqrtD");
}

TEST_F(AssemblerRISCV64Test, FSqrtD_Default) {
  DriverStr(RepeatFF(&Riscv64Assembler::FSqrtD, "fsqrt.d {reg1}, {reg2}"), "FSqrtD_Default");
}

TEST_F(AssemblerRISCV64Test, FSgnjS) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjS, "fsgnj.s {reg1}, {reg2}, {reg3}"), "FSgnjS");
}

TEST_F(AssemblerRISCV64Test, FSgnjD) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjD, "fsgnj.d {reg1}, {reg2}, {reg3}"), "FSgnjD");
}

TEST_F(AssemblerRISCV64Test, FSgnjnS) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjnS, "fsgnjn.s {reg1}, {reg2}, {reg3}"), "FSgnjnS");
}

TEST_F(AssemblerRISCV64Test, FSgnjnD) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjnD, "fsgnjn.d {reg1}, {reg2}, {reg3}"), "FSgnjnD");
}

TEST_F(AssemblerRISCV64Test, FSgnjxS) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjxS, "fsgnjx.s {reg1}, {reg2}, {reg3}"), "FSgnjxS");
}

TEST_F(AssemblerRISCV64Test, FSgnjxD) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FSgnjxD, "fsgnjx.d {reg1}, {reg2}, {reg3}"), "FSgnjxD");
}

TEST_F(AssemblerRISCV64Test, FMinS) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMinS, "fmin.s {reg1}, {reg2}, {reg3}"), "FMinS");
}

TEST_F(AssemblerRISCV64Test, FMinD) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMinD, "fmin.d {reg1}, {reg2}, {reg3}"), "FMinD");
}

TEST_F(AssemblerRISCV64Test, FMaxS) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMaxS, "fmax.s {reg1}, {reg2}, {reg3}"), "FMaxS");
}

TEST_F(AssemblerRISCV64Test, FMaxD) {
  DriverStr(RepeatFFF(&Riscv64Assembler::FMaxD, "fmax.d {reg1}, {reg2}, {reg3}"), "FMaxD");
}

TEST_F(AssemblerRISCV64Test, FCvtSD) {
  DriverStr(RepeatFFRoundingMode(&Riscv64Assembler::FCvtSD, "fcvt.s.d {reg1}, {reg2}, {rm}"),
            "FCvtSD");
}

TEST_F(AssemblerRISCV64Test, FCvtSD_Default) {
  DriverStr(RepeatFF(&Riscv64Assembler::FCvtSD, "fcvt.s.d {reg1}, {reg2}"), "FCvtSD_Default");
}

// This conversion is lossless, so the rounding mode is meaningless and the assembler we're
// testing against does not even accept the rounding mode argument, so this test is disabled.
TEST_F(AssemblerRISCV64Test, DISABLED_FCvtDS) {
  DriverStr(RepeatFFRoundingMode(&Riscv64Assembler::FCvtDS, "fcvt.d.s {reg1}, {reg2}, {rm}"),
            "FCvtDS");
}

TEST_F(AssemblerRISCV64Test, FCvtDS_Default) {
  DriverStr(RepeatFF(&Riscv64Assembler::FCvtDS, "fcvt.d.s {reg1}, {reg2}"), "FCvtDS_Default");
}

TEST_F(AssemblerRISCV64Test, FEqS) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FEqS, "feq.s {reg1}, {reg2}, {reg3}"), "FEqS");
}

TEST_F(AssemblerRISCV64Test, FEqD) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FEqD, "feq.d {reg1}, {reg2}, {reg3}"), "FEqD");
}

TEST_F(AssemblerRISCV64Test, FLtS) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FLtS, "flt.s {reg1}, {reg2}, {reg3}"), "FLtS");
}

TEST_F(AssemblerRISCV64Test, FLtD) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FLtD, "flt.d {reg1}, {reg2}, {reg3}"), "FLtD");
}

TEST_F(AssemblerRISCV64Test, FLeS) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FLeS, "fle.s {reg1}, {reg2}, {reg3}"), "FLeS");
}

TEST_F(AssemblerRISCV64Test, FLeD) {
  DriverStr(RepeatRFF(&Riscv64Assembler::FLeD, "fle.d {reg1}, {reg2}, {reg3}"), "FLeD");
}

TEST_F(AssemblerRISCV64Test, FCvtWS) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtWS, "fcvt.w.s {reg1}, {reg2}, {rm}"),
            "FCvtWS");
}

TEST_F(AssemblerRISCV64Test, FCvtWS_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtWS, "fcvt.w.s {reg1}, {reg2}"), "FCvtWS_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtWD) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtWD, "fcvt.w.d {reg1}, {reg2}, {rm}"),
            "FCvtWD");
}

TEST_F(AssemblerRISCV64Test, FCvtWD_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtWD, "fcvt.w.d {reg1}, {reg2}"), "FCvtWD_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtWuS) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtWuS, "fcvt.wu.s {reg1}, {reg2}, {rm}"),
            "FCvtWuS");
}

TEST_F(AssemblerRISCV64Test, FCvtWuS_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtWuS, "fcvt.wu.s {reg1}, {reg2}"), "FCvtWuS_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtWuD) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtWuD, "fcvt.wu.d {reg1}, {reg2}, {rm}"),
            "FCvtWuD");
}

TEST_F(AssemblerRISCV64Test, FCvtWuD_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtWuD, "fcvt.wu.d {reg1}, {reg2}"), "FCvtWuD_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtLS) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtLS, "fcvt.l.s {reg1}, {reg2}, {rm}"),
            "FCvtLS");
}

TEST_F(AssemblerRISCV64Test, FCvtLS_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtLS, "fcvt.l.s {reg1}, {reg2}"), "FCvtLS_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtLD) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtLD, "fcvt.l.d {reg1}, {reg2}, {rm}"),
            "FCvtLD");
}

TEST_F(AssemblerRISCV64Test, FCvtLD_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtLD, "fcvt.l.d {reg1}, {reg2}"), "FCvtLD_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtLuS) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtLuS, "fcvt.lu.s {reg1}, {reg2}, {rm}"),
            "FCvtLuS");
}

TEST_F(AssemblerRISCV64Test, FCvtLuS_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtLuS, "fcvt.lu.s {reg1}, {reg2}"), "FCvtLuS_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtLuD) {
  DriverStr(RepeatrFRoundingMode(&Riscv64Assembler::FCvtLuD, "fcvt.lu.d {reg1}, {reg2}, {rm}"),
            "FCvtLuD");
}

TEST_F(AssemblerRISCV64Test, FCvtLuD_Default) {
  DriverStr(RepeatrF(&Riscv64Assembler::FCvtLuD, "fcvt.lu.d {reg1}, {reg2}"), "FCvtLuD_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtSW) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtSW, "fcvt.s.w {reg1}, {reg2}, {rm}"),
            "FCvtSW");
}

TEST_F(AssemblerRISCV64Test, FCvtSW_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtSW, "fcvt.s.w {reg1}, {reg2}"), "FCvtSW_Default");
}

// This conversion is lossless, so the rounding mode is meaningless and the assembler we're
// testing against does not even accept the rounding mode argument, so this test is disabled.
TEST_F(AssemblerRISCV64Test, DISABLED_FCvtDW) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtDW, "fcvt.d.w {reg1}, {reg2}, {rm}"),
            "FCvtDW");
}

TEST_F(AssemblerRISCV64Test, FCvtDW_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtDW, "fcvt.d.w {reg1}, {reg2}"), "FCvtDW_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtSWu) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtSWu, "fcvt.s.wu {reg1}, {reg2}, {rm}"),
            "FCvtSWu");
}

TEST_F(AssemblerRISCV64Test, FCvtSWu_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtSWu, "fcvt.s.wu {reg1}, {reg2}"), "FCvtSWu_Default");
}

// This conversion is lossless, so the rounding mode is meaningless and the assembler we're
// testing against does not even accept the rounding mode argument, so this test is disabled.
TEST_F(AssemblerRISCV64Test, DISABLED_FCvtDWu) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtDWu, "fcvt.d.wu {reg1}, {reg2}, {rm}"),
            "FCvtDWu");
}

TEST_F(AssemblerRISCV64Test, FCvtDWu_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtDWu, "fcvt.d.wu {reg1}, {reg2}"), "FCvtDWu_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtSL) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtSL, "fcvt.s.l {reg1}, {reg2}, {rm}"),
            "FCvtSL");
}

TEST_F(AssemblerRISCV64Test, FCvtSL_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtSL, "fcvt.s.l {reg1}, {reg2}"), "FCvtSL_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtDL) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtDL, "fcvt.d.l {reg1}, {reg2}, {rm}"),
            "FCvtDL");
}

TEST_F(AssemblerRISCV64Test, FCvtDL_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtDL, "fcvt.d.l {reg1}, {reg2}"), "FCvtDL_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtSLu) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtSLu, "fcvt.s.lu {reg1}, {reg2}, {rm}"),
            "FCvtSLu");
}

TEST_F(AssemblerRISCV64Test, FCvtSLu_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtSLu, "fcvt.s.lu {reg1}, {reg2}"), "FCvtSLu_Default");
}

TEST_F(AssemblerRISCV64Test, FCvtDLu) {
  DriverStr(RepeatFrRoundingMode(&Riscv64Assembler::FCvtDLu, "fcvt.d.lu {reg1}, {reg2}, {rm}"),
            "FCvtDLu");
}

TEST_F(AssemblerRISCV64Test, FCvtDLu_Default) {
  DriverStr(RepeatFr(&Riscv64Assembler::FCvtDLu, "fcvt.d.lu {reg1}, {reg2}"), "FCvtDLu_Default");
}

TEST_F(AssemblerRISCV64Test, FMvXW) {
  DriverStr(RepeatRF(&Riscv64Assembler::FMvXW, "fmv.x.w {reg1}, {reg2}"), "FMvXW");
}

TEST_F(AssemblerRISCV64Test, FMvXD) {
  DriverStr(RepeatRF(&Riscv64Assembler::FMvXD, "fmv.x.d {reg1}, {reg2}"), "FMvXD");
}

TEST_F(AssemblerRISCV64Test, FMvWX) {
  DriverStr(RepeatFR(&Riscv64Assembler::FMvWX, "fmv.w.x {reg1}, {reg2}"), "FMvWX");
}

TEST_F(AssemblerRISCV64Test, FMvDX) {
  DriverStr(RepeatFR(&Riscv64Assembler::FMvDX, "fmv.d.x {reg1}, {reg2}"), "FMvDX");
}

TEST_F(AssemblerRISCV64Test, FClassS) {
  DriverStr(RepeatRF(&Riscv64Assembler::FClassS, "fclass.s {reg1}, {reg2}"), "FClassS");
}

TEST_F(AssemblerRISCV64Test, FClassD) {
  DriverStr(RepeatrF(&Riscv64Assembler::FClassD, "fclass.d {reg1}, {reg2}"), "FClassD");
}

TEST_F(AssemblerRISCV64Test, AddUw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::AddUw, "add.uw {reg1}, {reg2}, {reg3}"), "AddUw");
}

TEST_F(AssemblerRISCV64Test, Sh1Add) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh1Add, "sh1add {reg1}, {reg2}, {reg3}"), "Sh1Add");
}

TEST_F(AssemblerRISCV64Test, Sh1AddUw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh1AddUw, "sh1add.uw {reg1}, {reg2}, {reg3}"), "Sh1AddUw");
}

TEST_F(AssemblerRISCV64Test, Sh2Add) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh2Add, "sh2add {reg1}, {reg2}, {reg3}"), "Sh2Add");
}

TEST_F(AssemblerRISCV64Test, Sh2AddUw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh2AddUw, "sh2add.uw {reg1}, {reg2}, {reg3}"), "Sh2AddUw");
}

TEST_F(AssemblerRISCV64Test, Sh3Add) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh3Add, "sh3add {reg1}, {reg2}, {reg3}"), "Sh3Add");
}

TEST_F(AssemblerRISCV64Test, Sh3AddUw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Sh3AddUw, "sh3add.uw {reg1}, {reg2}, {reg3}"), "Sh3AddUw");
}

TEST_F(AssemblerRISCV64Test, SlliUw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::SlliUw, 6, "slli.uw {reg1}, {reg2}, {imm}"), "SlliUw");
}

TEST_F(AssemblerRISCV64Test, Andn) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Andn, "andn {reg1}, {reg2}, {reg3}"), "Andn");
}

TEST_F(AssemblerRISCV64Test, Orn) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Orn, "orn {reg1}, {reg2}, {reg3}"), "Orn");
}

TEST_F(AssemblerRISCV64Test, Xnor) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Xnor, "xnor {reg1}, {reg2}, {reg3}"), "Xnor");
}

TEST_F(AssemblerRISCV64Test, Clz) {
  DriverStr(RepeatRR(&Riscv64Assembler::Clz, "clz {reg1}, {reg2}"), "Clz");
}

TEST_F(AssemblerRISCV64Test, Clzw) {
  DriverStr(RepeatRR(&Riscv64Assembler::Clzw, "clzw {reg1}, {reg2}"), "Clzw");
}

TEST_F(AssemblerRISCV64Test, Ctz) {
  DriverStr(RepeatRR(&Riscv64Assembler::Ctz, "ctz {reg1}, {reg2}"), "Ctz");
}

TEST_F(AssemblerRISCV64Test, Ctzw) {
  DriverStr(RepeatRR(&Riscv64Assembler::Ctzw, "ctzw {reg1}, {reg2}"), "Ctzw");
}

TEST_F(AssemblerRISCV64Test, Cpop) {
  DriverStr(RepeatRR(&Riscv64Assembler::Cpop, "cpop {reg1}, {reg2}"), "Cpop");
}

TEST_F(AssemblerRISCV64Test, Cpopw) {
  DriverStr(RepeatRR(&Riscv64Assembler::Cpopw, "cpopw {reg1}, {reg2}"), "Cpopw");
}

TEST_F(AssemblerRISCV64Test, Min) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Min, "min {reg1}, {reg2}, {reg3}"), "Min");
}

TEST_F(AssemblerRISCV64Test, Minu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Minu, "minu {reg1}, {reg2}, {reg3}"), "Minu");
}

TEST_F(AssemblerRISCV64Test, Max) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Max, "max {reg1}, {reg2}, {reg3}"), "Max");
}

TEST_F(AssemblerRISCV64Test, Maxu) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Maxu, "maxu {reg1}, {reg2}, {reg3}"), "Maxu");
}

TEST_F(AssemblerRISCV64Test, Rol) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Rol, "rol {reg1}, {reg2}, {reg3}"), "Rol");
}

TEST_F(AssemblerRISCV64Test, Rolw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Rolw, "rolw {reg1}, {reg2}, {reg3}"), "Rolw");
}

TEST_F(AssemblerRISCV64Test, Ror) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Ror, "ror {reg1}, {reg2}, {reg3}"), "Ror");
}

TEST_F(AssemblerRISCV64Test, Rorw) {
  DriverStr(RepeatRRR(&Riscv64Assembler::Rorw, "rorw {reg1}, {reg2}, {reg3}"), "Rorw");
}

TEST_F(AssemblerRISCV64Test, Rori) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Rori, 6, "rori {reg1}, {reg2}, {imm}"), "Rori");
}

TEST_F(AssemblerRISCV64Test, Roriw) {
  DriverStr(RepeatRRIb(&Riscv64Assembler::Roriw, 5, "roriw {reg1}, {reg2}, {imm}"), "Roriw");
}

TEST_F(AssemblerRISCV64Test, OrcB) {
  DriverStr(RepeatRR(&Riscv64Assembler::OrcB, "orc.b {reg1}, {reg2}"), "OrcB");
}

TEST_F(AssemblerRISCV64Test, Rev8) {
  DriverStr(RepeatRR(&Riscv64Assembler::Rev8, "rev8 {reg1}, {reg2}"), "Rev8");
}

// Pseudo instructions.
TEST_F(AssemblerRISCV64Test, Nop) {
  __ Nop();
  DriverStr("addi zero,zero,0", "Nop");
}

TEST_F(AssemblerRISCV64Test, Li) {
  SetUseSimpleMarch(true);
  TestLoadConst64("Li",
                  /*can_use_tmp=*/ false,
                  [&](XRegister rd, int64_t value) { __ Li(rd, value); });
}

TEST_F(AssemblerRISCV64Test, Mv) {
  DriverStr(RepeatRR(&Riscv64Assembler::Mv, "addi {reg1}, {reg2}, 0"), "Mv");
}

TEST_F(AssemblerRISCV64Test, Not) {
  DriverStr(RepeatRR(&Riscv64Assembler::Not, "xori {reg1}, {reg2}, -1"), "Not");
}

TEST_F(AssemblerRISCV64Test, Neg) {
  DriverStr(RepeatRR(&Riscv64Assembler::Neg, "sub {reg1}, x0, {reg2}"), "Neg");
}

TEST_F(AssemblerRISCV64Test, NegW) {
  DriverStr(RepeatRR(&Riscv64Assembler::NegW, "subw {reg1}, x0, {reg2}"), "Neg");
}

TEST_F(AssemblerRISCV64Test, SextB) {
  // Note: SEXT.B from the Zbb extension is not supported.
  DriverStr(RepeatRR(&Riscv64Assembler::SextB,
                     "slli {reg1}, {reg2}, 56\n"
                     "srai {reg1}, {reg1}, 56"),
            "SextB");
}

TEST_F(AssemblerRISCV64Test, SextH) {
  // Note: SEXT.H from the Zbb extension is not supported.
  DriverStr(RepeatRR(&Riscv64Assembler::SextH,
                     "slli {reg1}, {reg2}, 48\n"
                     "srai {reg1}, {reg1}, 48"),
            "SextH");
}

TEST_F(AssemblerRISCV64Test, SextW) {
  DriverStr(RepeatRR(&Riscv64Assembler::SextW, "addiw {reg1}, {reg2}, 0\n"), "SextW");
}

TEST_F(AssemblerRISCV64Test, ZextB) {
  DriverStr(RepeatRR(&Riscv64Assembler::ZextB, "andi {reg1}, {reg2}, 255"), "ZextB");
}

TEST_F(AssemblerRISCV64Test, ZextH) {
  // Note: ZEXT.H from the Zbb extension is not supported.
  DriverStr(RepeatRR(&Riscv64Assembler::ZextH,
                     "slli {reg1}, {reg2}, 48\n"
                     "srli {reg1}, {reg1}, 48"),
            "SextH");
}

TEST_F(AssemblerRISCV64Test, ZextW) {
  DriverStr(RepeatRR(&Riscv64Assembler::ZextW,
                     "slli {reg1}, {reg2}, 32\n"
                     "srli {reg1}, {reg1}, 32"),
            "ZextW");
}

TEST_F(AssemblerRISCV64Test, Seqz) {
  DriverStr(RepeatRR(&Riscv64Assembler::Seqz, "sltiu {reg1}, {reg2}, 1\n"), "Seqz");
}

TEST_F(AssemblerRISCV64Test, Snez) {
  DriverStr(RepeatRR(&Riscv64Assembler::Snez, "sltu {reg1}, zero, {reg2}\n"), "Snez");
}

TEST_F(AssemblerRISCV64Test, Sltz) {
  DriverStr(RepeatRR(&Riscv64Assembler::Sltz, "slt {reg1}, {reg2}, zero\n"), "Sltz");
}

TEST_F(AssemblerRISCV64Test, Sgtz) {
  DriverStr(RepeatRR(&Riscv64Assembler::Sgtz, "slt {reg1}, zero, {reg2}\n"), "Sgtz");
}

TEST_F(AssemblerRISCV64Test, FMvS) {
  DriverStr(RepeatFF(&Riscv64Assembler::FMvS, "fsgnj.s {reg1}, {reg2}, {reg2}\n"), "FMvS");
}

TEST_F(AssemblerRISCV64Test, FAbsS) {
  DriverStr(RepeatFF(&Riscv64Assembler::FAbsS, "fsgnjx.s {reg1}, {reg2}, {reg2}\n"), "FAbsS");
}

TEST_F(AssemblerRISCV64Test, FNegS) {
  DriverStr(RepeatFF(&Riscv64Assembler::FNegS, "fsgnjn.s {reg1}, {reg2}, {reg2}\n"), "FNegS");
}

TEST_F(AssemblerRISCV64Test, FMvD) {
  DriverStr(RepeatFF(&Riscv64Assembler::FMvD, "fsgnj.d {reg1}, {reg2}, {reg2}\n"), "FMvD");
}

TEST_F(AssemblerRISCV64Test, FAbsD) {
  DriverStr(RepeatFF(&Riscv64Assembler::FAbsD, "fsgnjx.d {reg1}, {reg2}, {reg2}\n"), "FAbsD");
}

TEST_F(AssemblerRISCV64Test, FNegD) {
  DriverStr(RepeatFF(&Riscv64Assembler::FNegD, "fsgnjn.d {reg1}, {reg2}, {reg2}\n"), "FNegD");
}

TEST_F(AssemblerRISCV64Test, Beqz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Beqz, -11, 2, "beq {reg}, zero, {imm}\n"), "Beqz");
}

TEST_F(AssemblerRISCV64Test, Bnez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Bnez, -11, 2, "bne {reg}, zero, {imm}\n"), "Bnez");
}

TEST_F(AssemblerRISCV64Test, Blez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Blez, -11, 2, "bge zero, {reg}, {imm}\n"), "Blez");
}

TEST_F(AssemblerRISCV64Test, Bgez) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Bgez, -11, 2, "bge {reg}, zero, {imm}\n"), "Bgez");
}

TEST_F(AssemblerRISCV64Test, Bltz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Bltz, -11, 2, "blt {reg}, zero, {imm}\n"), "Bltz");
}

TEST_F(AssemblerRISCV64Test, Bgtz) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRIbS(&Riscv64Assembler::Bgtz, -11, 2, "blt zero, {reg}, {imm}\n"), "Bgtz");
}

TEST_F(AssemblerRISCV64Test, Bgt) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bgt, -11, 2, "blt {reg2}, {reg1}, {imm}\n"), "Bgt");
}

TEST_F(AssemblerRISCV64Test, Ble) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Ble, -11, 2, "bge {reg2}, {reg1}, {imm}\n"), "Bge");
}

TEST_F(AssemblerRISCV64Test, Bgtu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bgtu, -11, 2, "bltu {reg2}, {reg1}, {imm}\n"), "Bgtu");
}

TEST_F(AssemblerRISCV64Test, Bleu) {
  // TODO(riscv64): Change "-11, 2" to "-12, 1" for "C" Standard Extension.
  DriverStr(RepeatRRIbS(&Riscv64Assembler::Bleu, -11, 2, "bgeu {reg2}, {reg1}, {imm}\n"), "Bgeu");
}

TEST_F(AssemblerRISCV64Test, J) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatIbS<int32_t>(&Riscv64Assembler::J, -19, 2, "j {imm}\n"), "J");
}

TEST_F(AssemblerRISCV64Test, JalRA) {
  // TODO(riscv64): Change "-19, 2" to "-20, 1" for "C" Standard Extension.
  DriverStr(RepeatIbS<int32_t>(&Riscv64Assembler::Jal, -19, 2, "jal {imm}\n"), "JalRA");
}

TEST_F(AssemblerRISCV64Test, Jr) {
  DriverStr(RepeatR(&Riscv64Assembler::Jr, "jr {reg}\n"), "Jr");
}

TEST_F(AssemblerRISCV64Test, JalrRA) {
  DriverStr(RepeatR(&Riscv64Assembler::Jalr, "jalr {reg}\n"), "JalrRA");
}

TEST_F(AssemblerRISCV64Test, Jalr0) {
  DriverStr(RepeatRR(&Riscv64Assembler::Jalr, "jalr {reg1}, {reg2}\n"), "Jalr0");
}

TEST_F(AssemblerRISCV64Test, Ret) {
  __ Ret();
  DriverStr("ret\n", "Ret");
}

TEST_F(AssemblerRISCV64Test, RdCycle) {
  DriverStr(RepeatR(&Riscv64Assembler::RdCycle, "rdcycle {reg}\n"), "RdCycle");
}

TEST_F(AssemblerRISCV64Test, RdTime) {
  DriverStr(RepeatR(&Riscv64Assembler::RdTime, "rdtime {reg}\n"), "RdTime");
}

TEST_F(AssemblerRISCV64Test, RdInstret) {
  DriverStr(RepeatR(&Riscv64Assembler::RdInstret, "rdinstret {reg}\n"), "RdInstret");
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
  ScratchRegisterScope srs(GetAssembler());
  srs.ExcludeXRegister(TMP);
  srs.ExcludeXRegister(TMP2);
  DriverStr(RepeatRIb(&Riscv64Assembler::LoadConst32, -32, "li {reg}, {imm}"), "LoadConst32");
}

TEST_F(AssemblerRISCV64Test, LoadConst64) {
  SetUseSimpleMarch(true);
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
  SetUseSimpleMarch(true);
  auto emit_op = [&](XRegister rd, XRegister rs1, int64_t value) {
    __ AddConst64(rd, rs1, value);
  };
  TestAddConst("AddConst64", 64, /*suffix=*/ "", emit_op);
}

TEST_F(AssemblerRISCV64Test, BcondForward3KiB) {
  TestBcondForward("BcondForward3KiB", 3 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BcondForward3KiBBare) {
  TestBcondForward("BcondForward3KiB", 3 * KB, "1", GetPrintBcond(), /*is_bare=*/ true);
}

TEST_F(AssemblerRISCV64Test, BcondBackward3KiB) {
  TestBcondBackward("BcondBackward3KiB", 3 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BcondBackward3KiBBare) {
  TestBcondBackward("BcondBackward3KiB", 3 * KB, "1", GetPrintBcond(), /*is_bare=*/ true);
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

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset13ForwardBare) {
  TestBeqA0A1Forward("BeqA0A1MaxOffset13ForwardBare",
                     MaxOffset13ForwardDistance() - /*BEQ*/ 4u,
                     "1",
                     GetPrintBcond(),
                      /*is_bare=*/ true);
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset13Backward) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset13Forward",
                      MaxOffset13BackwardDistance(),
                      "1",
                      GetPrintBcond());
}

TEST_F(AssemblerRISCV64Test, BeqA0A1MaxOffset13BackwardBare) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset13ForwardBare",
                      MaxOffset13BackwardDistance(),
                      "1",
                      GetPrintBcond(),
                      /*is_bare=*/ true);
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
  for (XRegister reg : GetRegisters()) {
    __ Bne(reg, reg, &label);
    __ Blt(reg, reg, &label);
    __ Bgt(reg, reg, &label);
    __ Bltu(reg, reg, &label);
    __ Bgtu(reg, reg, &label);
  }
  DriverStr("nop\n", "BcondElimination");
}

TEST_F(AssemblerRISCV64Test, BcondUnconditional) {
  Riscv64Label label;
  __ Bind(&label);
  __ Nop();
  for (XRegister reg : GetRegisters()) {
    __ Beq(reg, reg, &label);
    __ Bge(reg, reg, &label);
    __ Ble(reg, reg, &label);
    __ Bleu(reg, reg, &label);
    __ Bgeu(reg, reg, &label);
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

TEST_F(AssemblerRISCV64Test, JalRdForward3KiBBare) {
  TestJalRdForward("JalRdForward3KiB", 3 * KB, "1", GetPrintJalRd(), /*is_bare=*/ true);
}

TEST_F(AssemblerRISCV64Test, JalRdBackward3KiB) {
  TestJalRdBackward("JalRdBackward3KiB", 3 * KB, "1", GetPrintJalRd());
}

TEST_F(AssemblerRISCV64Test, JalRdBackward3KiBBare) {
  TestJalRdBackward("JalRdBackward3KiB", 3 * KB, "1", GetPrintJalRd(), /*is_bare=*/ true);
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

TEST_F(AssemblerRISCV64Test, JForward3KiBBare) {
  TestBuncondForward("JForward3KiB", 3 * KB, "1", GetEmitJ(/*is_bare=*/ true), GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JBackward3KiB) {
  TestBuncondBackward("JBackward3KiB", 3 * KB, "1", GetEmitJ(), GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JBackward3KiBBare) {
  TestBuncondBackward("JBackward3KiB", 3 * KB, "1", GetEmitJ(/*is_bare=*/ true), GetPrintJ());
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

TEST_F(AssemblerRISCV64Test, JMaxOffset21ForwardBare) {
  TestBuncondForward("JMaxOffset21Forward",
                     MaxOffset21ForwardDistance() - /*J*/ 4u,
                     "1",
                     GetEmitJ(/*is_bare=*/ true),
                     GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JMaxOffset21Backward) {
  TestBuncondBackward("JMaxOffset21Backward",
                      MaxOffset21BackwardDistance(),
                      "1",
                      GetEmitJ(),
                      GetPrintJ());
}

TEST_F(AssemblerRISCV64Test, JMaxOffset21BackwardBare) {
  TestBuncondBackward("JMaxOffset21Backward",
                      MaxOffset21BackwardDistance(),
                      "1",
                      GetEmitJ(/*is_bare=*/ true),
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

TEST_F(AssemblerRISCV64Test, Unimp) {
  __ Unimp();
  DriverStr("unimp\n", "Unimp");
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
      expected += "1:\n";
      expected += ART_FORMAT("auipc {}, %pcrel_hi({})\n", rd_name, target_label);
      expected += ART_FORMAT("addi {}, {}, %pcrel_lo(1b)\n", rd_name, rd_name);
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

TEST_F(AssemblerRISCV64Test, ScratchRegisters) {
  ScratchRegisterScope srs(GetAssembler());
  ASSERT_EQ(2u, srs.AvailableXRegisters());  // Default: TMP(T6) and TMP2(T5).
  ASSERT_EQ(1u, srs.AvailableFRegisters());  // Default: FTMP(FT11).

  XRegister tmp = srs.AllocateXRegister();
  EXPECT_EQ(TMP, tmp);
  XRegister tmp2 = srs.AllocateXRegister();
  EXPECT_EQ(TMP2, tmp2);
  ASSERT_EQ(0u, srs.AvailableXRegisters());

  FRegister ftmp = srs.AllocateFRegister();
  EXPECT_EQ(FTMP, ftmp);
  ASSERT_EQ(0u, srs.AvailableFRegisters());

  // Test nesting.
  srs.FreeXRegister(A0);
  srs.FreeXRegister(A1);
  srs.FreeFRegister(FA0);
  srs.FreeFRegister(FA1);
  ASSERT_EQ(2u, srs.AvailableXRegisters());
  ASSERT_EQ(2u, srs.AvailableFRegisters());
  {
    ScratchRegisterScope srs2(GetAssembler());
    ASSERT_EQ(2u, srs2.AvailableXRegisters());
    ASSERT_EQ(2u, srs2.AvailableFRegisters());
    XRegister a1 = srs2.AllocateXRegister();
    EXPECT_EQ(A1, a1);
    XRegister a0 = srs2.AllocateXRegister();
    EXPECT_EQ(A0, a0);
    ASSERT_EQ(0u, srs2.AvailableXRegisters());
    FRegister fa1 = srs2.AllocateFRegister();
    EXPECT_EQ(FA1, fa1);
    FRegister fa0 = srs2.AllocateFRegister();
    EXPECT_EQ(FA0, fa0);
    ASSERT_EQ(0u, srs2.AvailableFRegisters());
  }
  ASSERT_EQ(2u, srs.AvailableXRegisters());
  ASSERT_EQ(2u, srs.AvailableFRegisters());

  srs.IncludeXRegister(A0);  // No-op as the register was already available.
  ASSERT_EQ(2u, srs.AvailableXRegisters());
  srs.IncludeFRegister(FA0);  // No-op as the register was already available.
  ASSERT_EQ(2u, srs.AvailableFRegisters());
  srs.IncludeXRegister(S0);
  ASSERT_EQ(3u, srs.AvailableXRegisters());
  srs.IncludeFRegister(FS0);
  ASSERT_EQ(3u, srs.AvailableFRegisters());

  srs.ExcludeXRegister(S1);  // No-op as the register was not available.
  ASSERT_EQ(3u, srs.AvailableXRegisters());
  srs.ExcludeFRegister(FS1);  // No-op as the register was not available.
  ASSERT_EQ(3u, srs.AvailableFRegisters());
  srs.ExcludeXRegister(A0);
  ASSERT_EQ(2u, srs.AvailableXRegisters());
  srs.ExcludeFRegister(FA0);
  ASSERT_EQ(2u, srs.AvailableFRegisters());
}

#undef __

}  // namespace riscv64
}  // namespace art
