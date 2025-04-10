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

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "code_generator.h"
#include "dex/dex_file.h"
#include "dex/dex_instruction.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "prepare_for_register_allocation.h"
#include "ssa_liveness_analysis.h"

namespace art HIDDEN {

class LiveRangesTest : public CommonCompilerTest, public OptimizingUnitTestHelper {
 protected:
  // Define a shortcut for the `kLivenessPositionsPerInstruction`.
  static constexpr size_t kLppi = kLivenessPositionsPerInstruction;

  HGraph* BuildGraph(const std::vector<uint16_t>& data);

  std::unique_ptr<CompilerOptions> compiler_options_;
};

HGraph* LiveRangesTest::BuildGraph(const std::vector<uint16_t>& data) {
  HGraph* graph = CreateCFG(data);
  compiler_options_ = CommonCompilerTest::CreateCompilerOptions(kRuntimeISA, "default");
  // Suspend checks implementation may change in the future, and this test relies
  // on how instructions are ordered.
  RemoveSuspendChecks(graph);
  // `Inline` conditions into ifs.
  PrepareForRegisterAllocation(graph, *compiler_options_).Run();
  return graph;
}

TEST_F(LiveRangesTest, CFG1) {
  /*
   * Test the following snippet:
   *  return 0;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: goto
   *           |
   *       4: return
   *           |
   *       6: exit
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN);

  HGraph* graph = BuildGraph(data);

  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  LiveInterval* interval = liveness.GetInstructionFromSsaIndex(0)->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  // Last use is the return instruction.
  ASSERT_EQ(4u * kLppi, range->GetEnd());
  HBasicBlock* block = graph->GetBlocks()[1];
  ASSERT_TRUE(block->GetLastInstruction()->IsReturn());
  ASSERT_EQ(4u * kLppi, block->GetLastInstruction()->GetLifetimePosition());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

TEST_F(LiveRangesTest, CFG2) {
  /*
   * Test the following snippet:
   *  var a = 0;
   *  if (0 == 0) {
   *  } else {
   *  }
   *  return a;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: goto
   *           |
   *       4: equal
   *       5: if
   *       /       \
   *    7: goto   9: goto
   *       \       /
   *       11: return
   *         |
   *       13: exit
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 3,
    Instruction::GOTO | 0x100,
    Instruction::RETURN | 0 << 8);

  HGraph* graph = BuildGraph(data);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  LiveInterval* interval = liveness.GetInstructionFromSsaIndex(0)->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  // Last use is the return instruction.
  ASSERT_EQ(11u * kLppi, range->GetEnd());
  HBasicBlock* block = graph->GetBlocks()[3];
  ASSERT_TRUE(block->GetLastInstruction()->IsReturn());
  ASSERT_EQ(11u * kLppi, block->GetLastInstruction()->GetLifetimePosition());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

TEST_F(LiveRangesTest, CFG3) {
  /*
   * Test the following snippet:
   *  var a = 0;
   *  if (0 == 0) {
   *  } else {
   *    a = 4;
   *  }
   *  return a;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: constant4
   *       3: goto
   *           |
   *       5: equal
   *       6: if
   *       /       \
   *    8: goto   10: goto
   *       \       /
   *       11: phi
   *       12: return
   *         |
   *       14: exit
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 3,
    Instruction::CONST_4 | 4 << 12 | 0,
    Instruction::RETURN | 0 << 8);

  HGraph* graph = BuildGraph(data);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  // Test for the 4 constant.
  LiveInterval* interval = liveness.GetInstructionFromSsaIndex(1)->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(2u * kLppi, range->GetStart());
  // Last use is the phi at the return block so instruction is live until
  // the end of the then block.
  ASSERT_EQ(9u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the 0 constant.
  interval = liveness.GetInstructionFromSsaIndex(0)->GetLiveInterval();
  // The then branch is a hole for this constant, therefore its interval has 2 ranges.
  // First range starts from the definition and ends at the if block.
  range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  // 14 is the end of the if block.
  ASSERT_EQ(7u * kLppi, range->GetEnd());
  // Second range is the else block.
  range = range->GetNext();
  ASSERT_EQ(9u * kLppi, range->GetStart());
  // Last use is the phi at the return block.
  ASSERT_EQ(11u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the phi.
  interval = liveness.GetInstructionFromSsaIndex(2)->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(11u * kLppi, liveness.GetInstructionFromSsaIndex(2)->GetLifetimePosition());
  ASSERT_EQ(11u * kLppi, range->GetStart());
  ASSERT_EQ(12u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

TEST_F(LiveRangesTest, Loop1) {
  /*
   * Test the following snippet:
   *  var a = 0;
   *  while (a == a) {
   *    a = 4;
   *  }
   *  return 5;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: constant5
   *       3: constant4
   *       4: goto
   *           |
   *       6: goto
   *           |
   *       7: phi
   *       8: equal
   *       9: if +++++
   *        |       \ +
   *        |     11: goto
   *        |
   *       13: return
   *         |
   *       15: exit
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */

  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 4,
    Instruction::CONST_4 | 4 << 12 | 0,
    Instruction::GOTO | 0xFD00,
    Instruction::CONST_4 | 5 << 12 | 1 << 8,
    Instruction::RETURN | 1 << 8);

  HGraph* graph = BuildGraph(data);
  RemoveSuspendChecks(graph);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  // Test for the 0 constant.
  LiveInterval* interval = graph->GetIntConstant(0)->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  // Last use is the loop phi so instruction is live until
  // the end of the pre loop header.
  ASSERT_EQ(7u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the 4 constant.
  interval = graph->GetIntConstant(4)->GetLiveInterval();
  range = interval->GetFirstRange();
  // The instruction is live until the end of the loop.
  ASSERT_EQ(3u * kLppi, range->GetStart());
  ASSERT_EQ(12u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the 5 constant.
  interval = graph->GetIntConstant(5)->GetLiveInterval();
  range = interval->GetFirstRange();
  // The instruction is live until the return instruction after the loop.
  ASSERT_EQ(2u * kLppi, range->GetStart());
  ASSERT_EQ(13u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the phi.
  interval = liveness.GetInstructionFromSsaIndex(3)->GetLiveInterval();
  range = interval->GetFirstRange();
  // Instruction is input of non-materialized Equal and hence live until If.
  ASSERT_EQ(7u * kLppi, range->GetStart());
  ASSERT_EQ(9u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

TEST_F(LiveRangesTest, Loop2) {
  /*
   * Test the following snippet:
   *  var a = 0;
   *  while (a == a) {
   *    a = a + a;
   *  }
   *  return a;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: goto
   *           |
   *       4: goto
   *           |
   *       5: phi
   *       6: equal
   *       7: if +++++
   *        |       \ +
   *        |     9: add
   *        |     10: goto
   *        |
   *       12: return
   *         |
   *       14: exit
   * We want to make sure the phi at 5 has a lifetime hole after the add at 10.
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */

  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 6,
    Instruction::ADD_INT, 0, 0,
    Instruction::GOTO | 0xFB00,
    Instruction::RETURN | 0 << 8);

  HGraph* graph = BuildGraph(data);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  // Test for the 0 constant.
  HIntConstant* constant = liveness.GetInstructionFromSsaIndex(0)->AsIntConstant();
  LiveInterval* interval = constant->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  // Last use is the loop phi so instruction is live until
  // the end of the pre loop header.
  ASSERT_EQ(5u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the loop phi.
  HPhi* phi = liveness.GetInstructionFromSsaIndex(1)->AsPhi();
  interval = phi->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(5u * kLppi, range->GetStart());
  ASSERT_EQ(9u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  range = range->GetNext();
  ASSERT_TRUE(range != nullptr);
  ASSERT_EQ(11u * kLppi, range->GetStart());
  ASSERT_EQ(12u * kLppi, range->GetEnd());

  // Test for the add instruction.
  HAdd* add = liveness.GetInstructionFromSsaIndex(2)->AsAdd();
  interval = add->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(36u, range->GetStart());
  ASSERT_EQ(44u, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

TEST_F(LiveRangesTest, CFG4) {
  /*
   * Test the following snippet:
   *  var a = 0;
   *  var b = 4;
   *  if (a == a) {
   *    a = b + a;
   *  } else {
   *    a = b + a
   *  }
   *  return b;
   *
   * Which becomes the following graph (numbered by lifetime position):
   *       1: constant0
   *       2: constant4
   *       3: goto
   *           |
   *       5: equal
   *       6: if
   *       /       \
   *    8: add    11: add
   *    9: goto   12: goto
   *       \       /
   *       13: phi
   *       14: return
   *         |
   *       16: exit
   * We want to make sure the constant0 has a lifetime hole after the 8: add.
   * (Above positions are multiplied by `kLivenessPositionsPerInstruction`, or `kLppi` for short.)
   */
  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 4 << 12 | 1 << 8,
    Instruction::IF_EQ, 5,
    Instruction::ADD_INT, 1 << 8,
    Instruction::GOTO | 0x300,
    Instruction::ADD_INT, 1 << 8,
    Instruction::RETURN);

  HGraph* graph = BuildGraph(data);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, codegen.get(), GetScopedAllocator());
  liveness.Analyze();

  // Test for the 0 constant.
  LiveInterval* interval = liveness.GetInstructionFromSsaIndex(0)->GetLiveInterval();
  LiveRange* range = interval->GetFirstRange();
  ASSERT_EQ(1u * kLppi, range->GetStart());
  ASSERT_EQ(8u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  range = range->GetNext();
  ASSERT_TRUE(range != nullptr);
  ASSERT_EQ(10u * kLppi, range->GetStart());
  ASSERT_EQ(11u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the 4 constant.
  interval = liveness.GetInstructionFromSsaIndex(1)->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(2u * kLppi, range->GetStart());
  ASSERT_EQ(8u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  range = range->GetNext();
  ASSERT_EQ(10u * kLppi, range->GetStart());
  ASSERT_EQ(11u * kLppi + kLivenessPositionOfNormalUse, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the first add.
  HAdd* add = liveness.GetInstructionFromSsaIndex(2)->AsAdd();
  interval = add->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(8u * kLppi, range->GetStart());
  ASSERT_EQ(10u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  // Test for the second add.
  add = liveness.GetInstructionFromSsaIndex(3)->AsAdd();
  interval = add->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(11u * kLppi, range->GetStart());
  ASSERT_EQ(13u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);

  HPhi* phi = liveness.GetInstructionFromSsaIndex(4)->AsPhi();
  ASSERT_TRUE(phi->GetUses().HasExactlyOneElement());
  interval = phi->GetLiveInterval();
  range = interval->GetFirstRange();
  ASSERT_EQ(13u * kLppi, range->GetStart());
  ASSERT_EQ(14u * kLppi, range->GetEnd());
  ASSERT_TRUE(range->GetNext() == nullptr);
}

}  // namespace art
