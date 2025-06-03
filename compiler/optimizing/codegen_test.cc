/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <functional>
#include <memory>

#include "base/macros.h"
#include "base/utils.h"
#include "builder.h"
#include "codegen_test_utils.h"
#include "dex/dex_file.h"
#include "dex/dex_instruction.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "register_allocator_linear_scan.h"
#include "utils/arm/assembler_arm_vixl.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/x86/managed_register_x86.h"

#include "gtest/gtest.h"

namespace art HIDDEN {

// Return all combinations of ISA and code generator that are executable on
// hardware, or on simulator, and that we'd like to test.
static ::std::vector<CodegenTargetConfig> GetTargetConfigs() {
  ::std::vector<CodegenTargetConfig> v;
  ::std::vector<CodegenTargetConfig> test_config_candidates = {
#ifdef ART_ENABLE_CODEGEN_arm
    // TODO: Should't this be `kThumb2` instead of `kArm` here?
    CodegenTargetConfig(InstructionSet::kArm, create_codegen_arm_vixl32),
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
    CodegenTargetConfig(InstructionSet::kArm64, create_codegen_arm64),
#endif
#ifdef ART_ENABLE_CODEGEN_x86
    CodegenTargetConfig(InstructionSet::kX86, create_codegen_x86),
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
    CodegenTargetConfig(InstructionSet::kX86_64, create_codegen_x86_64),
#endif
  };

  for (const CodegenTargetConfig& test_config : test_config_candidates) {
    if (CanExecuteISA(test_config.GetInstructionSet())) {
      v.push_back(test_config);
    }
  }

  return v;
}

class CodegenTest : public CommonCompilerTest, public OptimizingUnitTestHelper {
 protected:
  void TestCode(const std::vector<uint16_t>& data, bool has_result = false, int32_t expected = 0);
  void TestCodeLong(const std::vector<uint16_t>& data, bool has_result, int64_t expected);
  void TestComparison(IfCondition condition,
                      int64_t i,
                      int64_t j,
                      DataType::Type type,
                      const CodegenTargetConfig target_config);
  void TestPackedSwitch(const CodegenTargetConfig target_config);
  void TestVectorComparison(IfCondition condition,
                            int64_t lhs_value,
                            int64_t rhs_value,
                            DataType::Type type,
                            CodeGenerator* codegen);
};

void CodegenTest::TestCode(const std::vector<uint16_t>& data, bool has_result, int32_t expected) {
  for (const CodegenTargetConfig& target_config : GetTargetConfigs()) {
    ResetPoolAndAllocator();
    HGraph* graph = CreateCFG(data);
    // Remove suspend checks, they cannot be executed in this context.
    RemoveSuspendChecks(graph);
    std::unique_ptr<CompilerOptions> compiler_options =
        CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
    RunCode(target_config, *compiler_options, graph, [](HGraph*) {}, has_result, expected);
  }
}

void CodegenTest::TestCodeLong(const std::vector<uint16_t>& data,
                               bool has_result, int64_t expected) {
  for (const CodegenTargetConfig& target_config : GetTargetConfigs()) {
    ResetPoolAndAllocator();
    HGraph* graph = CreateCFG(data, DataType::Type::kInt64);
    // Remove suspend checks, they cannot be executed in this context.
    RemoveSuspendChecks(graph);
    std::unique_ptr<CompilerOptions> compiler_options =
        CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
    RunCode(target_config, *compiler_options, graph, [](HGraph*) {}, has_result, expected);
  }
}

TEST_F(CodegenTest, ReturnVoid) {
  const std::vector<uint16_t> data = ZERO_REGISTER_CODE_ITEM(Instruction::RETURN_VOID);
  TestCode(data);
}

TEST_F(CodegenTest, CFG1) {
  const std::vector<uint16_t> data = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, CFG2) {
  const std::vector<uint16_t> data = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x100,
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, CFG3) {
  const std::vector<uint16_t> data1 = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO | 0x200,
    Instruction::RETURN_VOID,
    Instruction::GOTO | 0xFF00);

  TestCode(data1);

  const std::vector<uint16_t> data2 = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO_16, 3,
    Instruction::RETURN_VOID,
    Instruction::GOTO_16, 0xFFFF);

  TestCode(data2);

  const std::vector<uint16_t> data3 = ZERO_REGISTER_CODE_ITEM(
    Instruction::GOTO_32, 4, 0,
    Instruction::RETURN_VOID,
    Instruction::GOTO_32, 0xFFFF, 0xFFFF);

  TestCode(data3);
}

TEST_F(CodegenTest, CFG4) {
  const std::vector<uint16_t> data = ZERO_REGISTER_CODE_ITEM(
    Instruction::RETURN_VOID,
    Instruction::GOTO | 0x100,
    Instruction::GOTO | 0xFE00);

  TestCode(data);
}

TEST_F(CodegenTest, CFG5) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 3,
    Instruction::GOTO | 0x100,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, IntConstant) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN_VOID);

  TestCode(data);
}

TEST_F(CodegenTest, Return1) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN | 0);

  TestCode(data, true, 0);
}

TEST_F(CodegenTest, Return2) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 0 | 1 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 0);
}

TEST_F(CodegenTest, Return3) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnIf1) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::IF_EQ, 3,
    Instruction::RETURN | 0 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnIf2) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 1 << 12,
    Instruction::IF_EQ | 0 << 4 | 1 << 8, 3,
    Instruction::RETURN | 0 << 8,
    Instruction::RETURN | 1 << 8);

  TestCode(data, true, 0);
}

// Exercise bit-wise (one's complement) not-int instruction.
#define NOT_INT_TEST(TEST_NAME, INPUT, EXPECTED_OUTPUT)           \
TEST_F(CodegenTest, TEST_NAME) {                                  \
  const int32_t input = INPUT;                                    \
  const uint16_t input_lo = Low16Bits(input);                     \
  const uint16_t input_hi = High16Bits(input);                    \
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(     \
      Instruction::CONST | 0 << 8, input_lo, input_hi,            \
      Instruction::NOT_INT | 1 << 8 | 0 << 12 ,                   \
      Instruction::RETURN | 1 << 8);                              \
                                                                  \
  TestCode(data, true, EXPECTED_OUTPUT);                          \
}

NOT_INT_TEST(ReturnNotIntMinus2, -2, 1)
NOT_INT_TEST(ReturnNotIntMinus1, -1, 0)
NOT_INT_TEST(ReturnNotInt0, 0, -1)
NOT_INT_TEST(ReturnNotInt1, 1, -2)
NOT_INT_TEST(ReturnNotIntINT32_MIN, -2147483648, 2147483647)  // (2^31) - 1
NOT_INT_TEST(ReturnNotIntINT32_MINPlus1, -2147483647, 2147483646)  // (2^31) - 2
NOT_INT_TEST(ReturnNotIntINT32_MAXMinus1, 2147483646, -2147483647)  // -(2^31) - 1
NOT_INT_TEST(ReturnNotIntINT32_MAX, 2147483647, -2147483648)  // -(2^31)

#undef NOT_INT_TEST

// Exercise bit-wise (one's complement) not-long instruction.
#define NOT_LONG_TEST(TEST_NAME, INPUT, EXPECTED_OUTPUT)                 \
TEST_F(CodegenTest, TEST_NAME) {                                         \
  const int64_t input = INPUT;                                           \
  const uint16_t word0 = Low16Bits(Low32Bits(input));   /* LSW. */       \
  const uint16_t word1 = High16Bits(Low32Bits(input));                   \
  const uint16_t word2 = Low16Bits(High32Bits(input));                   \
  const uint16_t word3 = High16Bits(High32Bits(input)); /* MSW. */       \
  const std::vector<uint16_t> data = FOUR_REGISTERS_CODE_ITEM(           \
      Instruction::CONST_WIDE | 0 << 8, word0, word1, word2, word3,      \
      Instruction::NOT_LONG | 2 << 8 | 0 << 12,                          \
      Instruction::RETURN_WIDE | 2 << 8);                                \
                                                                         \
  TestCodeLong(data, true, EXPECTED_OUTPUT);                             \
}

NOT_LONG_TEST(ReturnNotLongMinus2, INT64_C(-2), INT64_C(1))
NOT_LONG_TEST(ReturnNotLongMinus1, INT64_C(-1), INT64_C(0))
NOT_LONG_TEST(ReturnNotLong0, INT64_C(0), INT64_C(-1))
NOT_LONG_TEST(ReturnNotLong1, INT64_C(1), INT64_C(-2))

NOT_LONG_TEST(ReturnNotLongINT32_MIN,
              INT64_C(-2147483648),
              INT64_C(2147483647))  // (2^31) - 1
NOT_LONG_TEST(ReturnNotLongINT32_MINPlus1,
              INT64_C(-2147483647),
              INT64_C(2147483646))  // (2^31) - 2
NOT_LONG_TEST(ReturnNotLongINT32_MAXMinus1,
              INT64_C(2147483646),
              INT64_C(-2147483647))  // -(2^31) - 1
NOT_LONG_TEST(ReturnNotLongINT32_MAX,
              INT64_C(2147483647),
              INT64_C(-2147483648))  // -(2^31)

// Note that the C++ compiler won't accept
// INT64_C(-9223372036854775808) (that is, INT64_MIN) as a valid
// int64_t literal, so we use INT64_C(-9223372036854775807)-1 instead.
NOT_LONG_TEST(ReturnNotINT64_MIN,
              INT64_C(-9223372036854775807)-1,
              INT64_C(9223372036854775807));  // (2^63) - 1
NOT_LONG_TEST(ReturnNotINT64_MINPlus1,
              INT64_C(-9223372036854775807),
              INT64_C(9223372036854775806));  // (2^63) - 2
NOT_LONG_TEST(ReturnNotLongINT64_MAXMinus1,
              INT64_C(9223372036854775806),
              INT64_C(-9223372036854775807));  // -(2^63) - 1
NOT_LONG_TEST(ReturnNotLongINT64_MAX,
              INT64_C(9223372036854775807),
              INT64_C(-9223372036854775807)-1);  // -(2^63)

#undef NOT_LONG_TEST

TEST_F(CodegenTest, IntToLongOfLongToInt) {
  const int64_t input = INT64_C(4294967296);             // 2^32
  const uint16_t word0 = Low16Bits(Low32Bits(input));    // LSW.
  const uint16_t word1 = High16Bits(Low32Bits(input));
  const uint16_t word2 = Low16Bits(High32Bits(input));
  const uint16_t word3 = High16Bits(High32Bits(input));  // MSW.
  const std::vector<uint16_t> data = FIVE_REGISTERS_CODE_ITEM(
      Instruction::CONST_WIDE | 0 << 8, word0, word1, word2, word3,
      Instruction::CONST_WIDE | 2 << 8, 1, 0, 0, 0,
      Instruction::ADD_LONG | 0, 0 << 8 | 2,             // v0 <- 2^32 + 1
      Instruction::LONG_TO_INT | 4 << 8 | 0 << 12,
      Instruction::INT_TO_LONG | 2 << 8 | 4 << 12,
      Instruction::RETURN_WIDE | 2 << 8);

  TestCodeLong(data, true, 1);
}

TEST_F(CodegenTest, ReturnAdd1) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::ADD_INT, 1 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd2) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::ADD_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd3) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::ADD_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnAdd4) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::ADD_INT_LIT16, 3,
    Instruction::RETURN);

  TestCode(data, true, 7);
}

TEST_F(CodegenTest, ReturnMulInt) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::MUL_INT, 1 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulInt2addr) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 3 << 12 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::MUL_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulLong) {
  const std::vector<uint16_t> data = FOUR_REGISTERS_CODE_ITEM(
    Instruction::CONST_WIDE | 0 << 8, 3, 0, 0, 0,
    Instruction::CONST_WIDE | 2 << 8, 4, 0, 0, 0,
    Instruction::MUL_LONG, 2 << 8 | 0,
    Instruction::RETURN_WIDE);

  TestCodeLong(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulLong2addr) {
  const std::vector<uint16_t> data = FOUR_REGISTERS_CODE_ITEM(
    Instruction::CONST_WIDE | 0 << 8, 3, 0, 0, 0,
    Instruction::CONST_WIDE | 2 << 8, 4, 0, 0, 0,
    Instruction::MUL_LONG_2ADDR | 2 << 12,
    Instruction::RETURN_WIDE);

  TestCodeLong(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulIntLit8) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::MUL_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, ReturnMulIntLit16) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::MUL_INT_LIT16, 3,
    Instruction::RETURN);

  TestCode(data, true, 12);
}

TEST_F(CodegenTest, NonMaterializedCondition) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    HGraph* graph = CreateGraph();

    HBasicBlock* entry = AddNewBlock();
    graph->SetEntryBlock(entry);
    MakeGoto(entry);

    HBasicBlock* first_block = AddNewBlock();
    entry->AddSuccessor(first_block);
    HIntConstant* constant0 = graph->GetIntConstant(0);
    HIntConstant* constant1 = graph->GetIntConstant(1);
    HInstruction* equal = MakeCondition(first_block, kCondEQ, constant0, constant0);
    MakeIf(first_block, equal);

    HBasicBlock* then_block = AddNewBlock();
    HBasicBlock* else_block = AddNewBlock();
    HBasicBlock* exit_block = AddExitBlock();

    first_block->AddSuccessor(then_block);
    first_block->AddSuccessor(else_block);
    then_block->AddSuccessor(exit_block);
    else_block->AddSuccessor(exit_block);

    MakeExit(exit_block);
    MakeReturn(then_block, constant0);
    MakeReturn(else_block, constant1);

    ASSERT_FALSE(equal->IsEmittedAtUseSite());
    graph->BuildDominatorTree();
    std::unique_ptr<CompilerOptions> compiler_options =
        CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
    PrepareForRegisterAllocation(graph, *compiler_options).Run();
    ASSERT_TRUE(equal->IsEmittedAtUseSite());

    auto hook_before_codegen = [](HGraph* graph_in) {
      HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
      HParallelMove* move = new (graph_in->GetAllocator()) HParallelMove(graph_in->GetAllocator());
      block->InsertInstructionBefore(move, block->GetLastInstruction());
    };

    RunCode(target_config, *compiler_options, graph, hook_before_codegen, true, 0);
  }
}

TEST_F(CodegenTest, MaterializedCondition1) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    // Check that condition are materialized correctly. A materialized condition
    // should yield `1` if it evaluated to true, and `0` otherwise.
    // We force the materialization of comparisons for different combinations of

    // inputs and check the results.

    int lhs[] = {1, 2, -1, 2, 0xabc};
    int rhs[] = {2, 1, 2, -1, 0xabc};

    for (size_t i = 0; i < arraysize(lhs); i++) {
      HBasicBlock* code_block = InitEntryMainExitGraph();

      HIntConstant* cst_lhs = graph_->GetIntConstant(lhs[i]);
      HIntConstant* cst_rhs = graph_->GetIntConstant(rhs[i]);
      HInstruction* cmp_lt = MakeCondition(code_block, kCondLT, cst_lhs, cst_rhs);
      MakeReturn(code_block, cmp_lt);

      graph_->BuildDominatorTree();
      auto hook_before_codegen = [](HGraph* graph_in) {
        HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
        HParallelMove* move =
            new (graph_in->GetAllocator()) HParallelMove(graph_in->GetAllocator());
        block->InsertInstructionBefore(move, block->GetLastInstruction());
      };
      std::unique_ptr<CompilerOptions> compiler_options =
          CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
      RunCode(target_config, *compiler_options, graph_, hook_before_codegen, true, lhs[i] < rhs[i]);
    }
  }
}

TEST_F(CodegenTest, MaterializedCondition2) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    // Check that HIf correctly interprets a materialized condition.
    // We force the materialization of comparisons for different combinations of
    // inputs. An HIf takes the materialized combination as input and returns a
    // value that we verify.

    int lhs[] = {1, 2, -1, 2, 0xabc};
    int rhs[] = {2, 1, 2, -1, 0xabc};


    for (size_t i = 0; i < arraysize(lhs); i++) {
      HGraph* graph = CreateGraph();

      HBasicBlock* entry_block = AddNewBlock();
      graph->SetEntryBlock(entry_block);
      MakeGoto(entry_block);

      HBasicBlock* if_block = AddNewBlock();
      HBasicBlock* if_true_block = AddNewBlock();
      HBasicBlock* if_false_block = AddNewBlock();
      HBasicBlock* exit_block = AddExitBlock();
      MakeExit(exit_block);

      graph->SetEntryBlock(entry_block);
      entry_block->AddSuccessor(if_block);
      if_block->AddSuccessor(if_true_block);
      if_block->AddSuccessor(if_false_block);
      if_true_block->AddSuccessor(exit_block);
      if_false_block->AddSuccessor(exit_block);

      HIntConstant* cst_lhs = graph->GetIntConstant(lhs[i]);
      HIntConstant* cst_rhs = graph->GetIntConstant(rhs[i]);
      HInstruction* cmp_lt = MakeCondition(if_block, kCondLT, cst_lhs, cst_rhs);
      // We insert a fake instruction to separate the HIf from the HLessThan
      // and force the materialization of the condition.
      HInstruction* force_materialization =
          new (GetAllocator()) HMemoryBarrier(MemBarrierKind::kAnyAny, 0);
      if_block->AddInstruction(force_materialization);
      MakeIf(if_block, cmp_lt);

      HIntConstant* cst_lt = graph->GetIntConstant(1);
      MakeReturn(if_true_block, cst_lt);
      HIntConstant* cst_ge = graph->GetIntConstant(0);
      MakeReturn(if_false_block, cst_ge);

      graph->BuildDominatorTree();
      auto hook_before_codegen = [](HGraph* graph_in) {
        HBasicBlock* block = graph_in->GetEntryBlock()->GetSuccessors()[0];
        HParallelMove* move =
            new (graph_in->GetAllocator()) HParallelMove(graph_in->GetAllocator());
        block->InsertInstructionBefore(move, block->GetLastInstruction());
      };
      std::unique_ptr<CompilerOptions> compiler_options =
          CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
      RunCode(target_config, *compiler_options, graph, hook_before_codegen, true, lhs[i] < rhs[i]);
    }
  }
}

TEST_F(CodegenTest, ReturnDivIntLit8) {
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::DIV_INT_LIT8, 3 << 8 | 0,
    Instruction::RETURN);

  TestCode(data, true, 1);
}

TEST_F(CodegenTest, ReturnDivInt2Addr) {
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 4 << 12 | 0,
    Instruction::CONST_4 | 2 << 12 | 1 << 8,
    Instruction::DIV_INT_2ADDR | 1 << 12,
    Instruction::RETURN);

  TestCode(data, true, 2);
}

static bool GetExpectedResultFromComparison(IfCondition condition, int64_t lhs, int64_t rhs) {
  const uint64_t unsigned_lhs = lhs;
  const uint64_t unsigned_rhs = rhs;
  switch (condition) {
    case kCondEQ:
      return lhs == rhs;
    case kCondNE:
      return lhs != rhs;
    case kCondLT:
      return lhs < rhs;
    case kCondLE:
      return lhs <= rhs;
    case kCondGT:
      return lhs > rhs;
    case kCondGE:
      return lhs >= rhs;
    case kCondB:
      return unsigned_lhs < unsigned_rhs;
    case kCondBE:
      return unsigned_lhs <= unsigned_rhs;
    case kCondA:
      return unsigned_lhs > unsigned_rhs;
    case kCondAE:
      return unsigned_lhs >= unsigned_rhs;
  }
  LOG(FATAL) << "Condition '" << enum_cast<uint32_t>(condition) << "' not supported: ";
  UNREACHABLE();
}

// Helper method.
void CodegenTest::TestComparison(IfCondition condition,
                                 int64_t i,
                                 int64_t j,
                                 DataType::Type type,
                                 const CodegenTargetConfig target_config) {
  HBasicBlock* block = InitEntryMainExitGraph();

  HInstruction* op1;
  HInstruction* op2;
  if (type == DataType::Type::kInt32) {
    op1 = graph_->GetIntConstant(i);
    op2 = graph_->GetIntConstant(j);
  } else {
    DCHECK_EQ(type, DataType::Type::kInt64);
    op1 = graph_->GetLongConstant(i);
    op2 = graph_->GetLongConstant(j);
  }

  HInstruction* comparison = MakeCondition(block, condition, op1, op2);
  MakeReturn(block, comparison);

  graph_->BuildDominatorTree();
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
  bool expected_result = GetExpectedResultFromComparison(condition, i, j);
  RunCode(target_config, *compiler_options, graph_, [](HGraph*) {}, true, expected_result);
}

TEST_F(CodegenTest, ComparisonsInt) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    for (int64_t i = -1; i <= 1; i++) {
      for (int64_t j = -1; j <= 1; j++) {
        for (int cond = kCondFirst; cond <= kCondLast; cond++) {
          TestComparison(
              static_cast<IfCondition>(cond), i, j, DataType::Type::kInt32, target_config);
        }
      }
    }
  }
}

TEST_F(CodegenTest, ComparisonsLong) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    for (int64_t i = -1; i <= 1; i++) {
      for (int64_t j = -1; j <= 1; j++) {
        for (int cond = kCondFirst; cond <= kCondLast; cond++) {
          TestComparison(
              static_cast<IfCondition>(cond), i, j, DataType::Type::kInt64, target_config);
        }
      }
    }
  }
}

// Tests a PackedSwitch in a very large HGraph; validates that the switch jump table is in
// range for the PC-relative load in the codegen visitor.
void CodegenTest::TestPackedSwitch(const CodegenTargetConfig target_config) {
  HBasicBlock* return_block = InitEntryMainExitGraph();
  constexpr DataType::Type data_type = DataType::Type::kInt32;

  // A number of entries - we are interested to test jump table implementation.
  constexpr size_t kNumSwitchEntries = 10;

  // Number of jump targets (including a 'default' case).
  constexpr size_t kNumBB = kNumSwitchEntries + 1;
  // Some arbitrary value to be used as input.
  constexpr int kInputValue = kNumBB - 4;

  HInstruction* input = graph_->GetIntConstant(kInputValue);
  HIntConstant* constant_1 = graph_->GetIntConstant(1);

  HBasicBlock* switch_block = AddNewBlock();
  entry_block_->ReplaceSuccessor(return_block, switch_block);

  HPackedSwitch* hswitch = new (GetAllocator()) HPackedSwitch(0, kNumSwitchEntries, input);
  switch_block->AddInstruction(hswitch);

  std::vector<HInstruction*> phi_inputs {};

  // Add switch jump target blocks.
  for (int i = 0; i < kNumBB; i++) {
    HBasicBlock* case_block = AddNewBlock();
    case_block->AddPredecessor(switch_block);
    case_block->AddSuccessor(return_block);

    HIntConstant* case_value = graph_->GetIntConstant(i);
    HAdd* add = MakeBinOp<HAdd>(case_block, data_type, input, case_value);
    phi_inputs.emplace_back(add);

    MakeGoto(case_block);
  }

  HPhi* phi = MakePhi(return_block, phi_inputs);
  HInstruction* return_val = phi;

  // Emit a huge number of HAdds - to simulate a very large HGraph.
  constexpr int kNumOfAdds = 2 * 1024 * 1024;
  for (int i = 0; i < kNumOfAdds; i++) {
    return_val = MakeBinOp<HAdd>(return_block, data_type, return_val, constant_1);
  }

  MakeReturn(return_block, return_val);

  graph_->BuildDominatorTree();
  EXPECT_TRUE(CheckGraph());

  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(target_config.GetInstructionSet(), "default");
  RunCode(target_config,
          *compiler_options,
          graph_,
          [](HGraph*) {}, true, kNumOfAdds + 2 * kInputValue);
}

TEST_F(CodegenTest, PackedSwitchInHugeMethod) {
  for (CodegenTargetConfig target_config : GetTargetConfigs()) {
    TestPackedSwitch(target_config);
  }
}

#ifdef ART_ENABLE_CODEGEN_arm
TEST_F(CodegenTest, ARMVIXLParallelMoveResolver) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kThumb2, "default");
  HGraph* graph = CreateGraph();
  arm::CodeGeneratorARMVIXL codegen(graph, *compiler_options);

  codegen.Initialize();

  // This will result in calling EmitSwap -> void ParallelMoveResolverARMVIXL::Exchange(int mem1,
  // int mem2) which was faulty (before the fix). So previously GPR and FP scratch registers were
  // used as temps; however GPR scratch register is required for big stack offsets which don't fit
  // LDR encoding. So the following code is a regression test for that situation.
  HParallelMove* move = new (graph->GetAllocator()) HParallelMove(graph->GetAllocator());
  move->AddMove(Location::StackSlot(0), Location::StackSlot(8192), DataType::Type::kInt32, nullptr);
  move->AddMove(Location::StackSlot(8192), Location::StackSlot(0), DataType::Type::kInt32, nullptr);
  codegen.GetMoveResolver()->EmitNativeCode(move);

  codegen.Finalize();
}
#endif

#ifdef ART_ENABLE_CODEGEN_arm64
// Regression test for b/34760542.
TEST_F(CodegenTest, ARM64ParallelMoveResolverB34760542) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);

  codegen.Initialize();

  // The following ParallelMove used to fail this assertion:
  //
  //   Assertion failed (!available->IsEmpty())
  //
  // in vixl::aarch64::UseScratchRegisterScope::AcquireNextAvailable,
  // because of the following situation:
  //
  //   1. a temp register (IP0) is allocated as a scratch register by
  //      the parallel move resolver to solve a cycle (swap):
  //
  //        [ source=DS0 destination=DS257 type=PrimDouble instruction=null ]
  //        [ source=DS257 destination=DS0 type=PrimDouble instruction=null ]
  //
  //   2. within CodeGeneratorARM64::MoveLocation, another temp
  //      register (IP1) is allocated to generate the swap between two
  //      double stack slots;
  //
  //   3. VIXL requires a third temp register to emit the `Ldr` or
  //      `Str` operation from CodeGeneratorARM64::MoveLocation (as
  //      one of the stack slots' offsets cannot be encoded as an
  //      immediate), but the pool of (core) temp registers is now
  //      empty.
  //
  // The solution used so far is to use a floating-point temp register
  // (D31) in step #2, so that IP1 is available for step #3.

  HParallelMove* move = new (graph->GetAllocator()) HParallelMove(graph->GetAllocator());
  move->AddMove(Location::DoubleStackSlot(0),
                Location::DoubleStackSlot(257),
                DataType::Type::kFloat64,
                nullptr);
  move->AddMove(Location::DoubleStackSlot(257),
                Location::DoubleStackSlot(0),
                DataType::Type::kFloat64,
                nullptr);
  codegen.GetMoveResolver()->EmitNativeCode(move);

  codegen.Finalize();
}

// Check that ParallelMoveResolver works fine for ARM64 for both cases when SIMD is on and off.
TEST_F(CodegenTest, ARM64ParallelMoveResolverSIMD) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);

  codegen.Initialize();

  graph->SetHasTraditionalSIMD(true);
  for (int i = 0; i < 2; i++) {
    HParallelMove* move = new (graph->GetAllocator()) HParallelMove(graph->GetAllocator());
    move->AddMove(Location::SIMDStackSlot(0),
                  Location::SIMDStackSlot(257),
                  DataType::Type::kFloat64,
                  nullptr);
    move->AddMove(Location::SIMDStackSlot(257),
                  Location::SIMDStackSlot(0),
                  DataType::Type::kFloat64,
                  nullptr);
    move->AddMove(Location::FpuRegisterLocation(0),
                  Location::FpuRegisterLocation(1),
                  DataType::Type::kFloat64,
                  nullptr);
    move->AddMove(Location::FpuRegisterLocation(1),
                  Location::FpuRegisterLocation(0),
                  DataType::Type::kFloat64,
                  nullptr);
    codegen.GetMoveResolver()->EmitNativeCode(move);
    graph->SetHasTraditionalSIMD(false);
  }

  codegen.Finalize();
}

// Check that ART ISA Features are propagated to VIXL for arm64 (using cortex-a75 as example).
TEST_F(CodegenTest, ARM64IsaVIXLFeaturesA75) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "cortex-a75");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);
  vixl::CPUFeatures* features = codegen.GetVIXLAssembler()->GetCPUFeatures();

  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kCRC32));
  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kDotProduct));
  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kFPHalf));
  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kNEONHalf));
  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kAtomics));
}

// Check that ART ISA Features are propagated to VIXL for arm64 (using cortex-a53 as example).
TEST_F(CodegenTest, ARM64IsaVIXLFeaturesA53) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "cortex-a53");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);
  vixl::CPUFeatures* features = codegen.GetVIXLAssembler()->GetCPUFeatures();

  EXPECT_TRUE(features->Has(vixl::CPUFeatures::kCRC32));
  EXPECT_FALSE(features->Has(vixl::CPUFeatures::kDotProduct));
  EXPECT_FALSE(features->Has(vixl::CPUFeatures::kFPHalf));
  EXPECT_FALSE(features->Has(vixl::CPUFeatures::kNEONHalf));
  EXPECT_FALSE(features->Has(vixl::CPUFeatures::kAtomics));
}

constexpr static size_t kExpectedFPSpillSize = 8 * vixl::aarch64::kDRegSizeInBytes;

// The following two tests check that for both SIMD and non-SIMD graphs exactly 64-bit is
// allocated on stack per callee-saved FP register to be preserved in the frame entry as
// ABI states.
TEST_F(CodegenTest, ARM64FrameSizeSIMD) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);

  codegen.Initialize();
  graph->SetHasTraditionalSIMD(true);

  DCHECK_EQ(arm64::callee_saved_fp_registers.GetCount(), 8);
  vixl::aarch64::CPURegList reg_list = arm64::callee_saved_fp_registers;
  while (!reg_list.IsEmpty()) {
    uint32_t reg_code = reg_list.PopLowestIndex().GetCode();
    codegen.AddAllocatedRegister(Location::FpuRegisterLocation(reg_code));
  }
  codegen.ComputeSpillMask();

  EXPECT_EQ(codegen.GetFpuSpillSize(), kExpectedFPSpillSize);
}

TEST_F(CodegenTest, ARM64FrameSizeNoSIMD) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default");
  HGraph* graph = CreateGraph();
  arm64::CodeGeneratorARM64 codegen(graph, *compiler_options);

  codegen.Initialize();
  graph->SetHasTraditionalSIMD(false);
  graph->SetHasPredicatedSIMD(false);

  DCHECK_EQ(arm64::callee_saved_fp_registers.GetCount(), 8);
  vixl::aarch64::CPURegList reg_list = arm64::callee_saved_fp_registers;
  while (!reg_list.IsEmpty()) {
    uint32_t reg_code = reg_list.PopLowestIndex().GetCode();
    codegen.AddAllocatedRegister(Location::FpuRegisterLocation(reg_code));
  }
  codegen.ComputeSpillMask();

  EXPECT_EQ(codegen.GetFpuSpillSize(), kExpectedFPSpillSize);
}

// This test checks that the result of the VecPredToBoolean instruction doesn't depend on
// conditional flags that can be updated by other instructions. For example:
//
//   VecPredWhile p0, opa, opb
//   Below opb, opa
//   VecPredToBoolean p0
//
// where Below updates conditions flags after VecPredWhile.
TEST_F(CodegenTest, ARM64SvePredicateToBoolean) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default", "sve");
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      HBasicBlock* block = InitEntryMainExitGraph();
      TestCodeGeneratorARM64 codegen(graph_, *compiler_options);
      if (!codegen.SupportsPredicatedSIMD()) {
        GTEST_SKIP() << "Predicated SIMD is not supported.";
      }

      HInstruction *opa = graph_->GetIntConstant(i);
      HInstruction *opb = graph_->GetIntConstant(j);
      HVecPredWhile *pred_while = MakeVecPredWhile(block,
                                                  opa,
                                                  opb,
                                                  HVecPredWhile::CondKind::kLO,
                                                  DataType::Type::kInt32);
      // Update condition flags by using Below instruction.
      MakeCondition(block, IfCondition::kCondB, opb, opa);
      HVecPredToBoolean *boolean = MakeVecPredToBoolean(block,
                                                        pred_while,
                                                        HVecPredToBoolean::PCondKind::kNFirst,
                                                        DataType::Type::kInt32);
      MakeReturn(block, boolean);

      graph_->SetHasPredicatedSIMD(true);
      graph_->BuildDominatorTree();

      if (CanExecute(codegen)) {
        RunCode(&codegen, graph_, [](HGraph*) {}, true, i >= j);
      }
    }
  }
}

void CodegenTest::TestVectorComparison(IfCondition condition,
                                       int64_t lhs_value,
                                       int64_t rhs_value,
                                       DataType::Type type,
                                       CodeGenerator* codegen) {
  HBasicBlock* block = entry_block_->GetSingleSuccessor();

  size_t vector_size_in_bytes = codegen->GetSIMDRegisterWidth();

  HVecPredSetAll* predicate = MakeVecPredSetAll(block,
                                                graph_->GetIntConstant(1),
                                                type,
                                                vector_size_in_bytes);
  HVecReplicateScalar* op1 = MakeVecReplicateScalar(block,
                                                    graph_->GetConstant(type, lhs_value),
                                                    type,
                                                    vector_size_in_bytes,
                                                    predicate);
  HVecReplicateScalar* op2 = MakeVecReplicateScalar(block,
                                                    graph_->GetConstant(type, rhs_value),
                                                    type,
                                                    vector_size_in_bytes,
                                                    predicate);
  HVecCondition* comparison = MakeVecCondition(block,
                                               condition,
                                               op1,
                                               op2,
                                               type,
                                               vector_size_in_bytes,
                                               predicate);
  HInstruction* boolean_return = MakeVecPredToBoolean(block,
                                                      comparison,
                                                      HVecPredToBoolean::PCondKind::kFirst,
                                                      type,
                                                      vector_size_in_bytes);
  MakeReturn(block, boolean_return);

  graph_->SetHasPredicatedSIMD(true);
  graph_->BuildDominatorTree();

  if (CanExecute(*codegen)) {
    bool expected_result = GetExpectedResultFromComparison(condition, lhs_value, rhs_value);
    RunCode(codegen, graph_, [](HGraph*) {}, true, expected_result);
  }
}

// Define tests ensuring that all types of conditions can be generated correctly and return the
// expected result.
#define DEFINE_CONDITION_TESTS(CondType)                                                         \
TEST_F(CodegenTest, ComparisonsVector##CondType) {                                               \
  std::unique_ptr<CompilerOptions> compiler_options =                                            \
      CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64, "default", "sve");       \
  for (int64_t i = -1; i <= 1; i++) {                                                            \
    for (int64_t j = -1; j <= 1; j++) {                                                          \
      for (int cond = kCondFirst; cond <= kCondLast; cond++) {                                   \
        InitEntryMainExitGraph();                                                                \
        TestCodeGeneratorARM64 codegen(graph_, *compiler_options);                               \
        if (!codegen.SupportsPredicatedSIMD()) {                                                 \
          GTEST_SKIP() << "Predicated SIMD is not supported.";                                   \
        }                                                                                        \
        TestVectorComparison(                                                                    \
            static_cast<IfCondition>(cond), i, j, DataType::Type::k##CondType, &codegen);        \
      }                                                                                          \
    }                                                                                            \
  }                                                                                              \
}
DEFINE_CONDITION_TESTS(Uint8)
DEFINE_CONDITION_TESTS(Int8)
DEFINE_CONDITION_TESTS(Uint16)
DEFINE_CONDITION_TESTS(Int16)
DEFINE_CONDITION_TESTS(Int32)
#undef DEFINE_CONDITION_TESTS

#endif

}  // namespace art
