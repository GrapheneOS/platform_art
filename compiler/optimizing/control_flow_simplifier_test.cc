/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "control_flow_simplifier.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

namespace art HIDDEN {

class ControlFlowSimplifierTest : public OptimizingUnitTest {
 protected:
  HPhi* ConstructBasicGraphForSelect(HBasicBlock* return_block, HInstruction* instr) {
    HParameterValue* bool_param = MakeParam(DataType::Type::kBool);
    HIntConstant* const1 =  graph_->GetIntConstant(1);

    auto [if_block, then_block, else_block] = CreateDiamondPattern(return_block, bool_param);

    AddOrInsertInstruction(then_block, instr);
    HPhi* phi = MakePhi(return_block, {instr, const1});
    return phi;
  }

  bool CheckGraphAndTryControlFlowSimplifier() {
    graph_->BuildDominatorTree();
    EXPECT_TRUE(CheckGraph());
    return HControlFlowSimplifier(graph_, /*handles*/ nullptr, /*stats*/ nullptr).Run();
  }
};

// HDivZeroCheck might throw and should not be hoisted from the conditional to an unconditional.
TEST_F(ControlFlowSimplifierTest, testZeroCheckPreventsSelect) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HDivZeroCheck* instr = new (GetAllocator()) HDivZeroCheck(param, 0);
  HPhi* phi = ConstructBasicGraphForSelect(return_block, instr);

  ManuallyBuildEnvFor(instr, {param, graph_->GetIntConstant(1)});

  EXPECT_FALSE(CheckGraphAndTryControlFlowSimplifier());
  EXPECT_INS_RETAINED(phi);
}

// Test that ControlFlowSimplifier succeeds with HAdd.
TEST_F(ControlFlowSimplifierTest, testSelectWithAdd) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HAdd* instr = new (GetAllocator()) HAdd(DataType::Type::kInt32, param, param, /*dex_pc=*/ 0);
  HPhi* phi = ConstructBasicGraphForSelect(return_block, instr);
  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());
  EXPECT_INS_REMOVED(phi);
}

// Test that ControlFlowSimplifier succeeds if there is an additional `HPhi` with identical inputs.
TEST_F(ControlFlowSimplifierTest, testSelectWithAddAndExtraPhi) {
  // Create a graph with three blocks merging to the `return_block`.
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HInstruction* const0 = graph_->GetIntConstant(0);
  auto [if_block1, left, mid] = CreateDiamondPattern(return_block, bool_param1);
  HBasicBlock* if_block2 = AddNewBlock();
  if_block1->ReplaceSuccessor(mid, if_block2);
  HBasicBlock* right = AddNewBlock();
  if_block2->AddSuccessor(mid);
  if_block2->AddSuccessor(right);
  HIf* if2 = MakeIf(if_block2, bool_param2);
  right->AddSuccessor(return_block);
  MakeGoto(right);
  ASSERT_TRUE(PredecessorsEqual(return_block, {left, mid, right}));
  HAdd* add = MakeBinOp<HAdd>(right, DataType::Type::kInt32, param, param);
  HPhi* phi1 = MakePhi(return_block, {param, param, add});
  HPhi* phi2 = MakePhi(return_block, {param, const0, const0});

  // Prevent second `HSelect` match. Do not rely on the "instructions per branch" limit.
  MakeInvokeStatic(left, DataType::Type::kVoid, {}, {});

  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());

  ASSERT_BLOCK_RETAINED(left);
  ASSERT_BLOCK_REMOVED(mid);
  ASSERT_BLOCK_REMOVED(right);
  HInstruction* select = if2->GetPrevious();  // `HSelect` is inserted before `HIf`.
  ASSERT_TRUE(select->IsSelect());
  ASSERT_INS_RETAINED(phi1);
  ASSERT_TRUE(InputsEqual(phi1, {param, select}));
  ASSERT_INS_RETAINED(phi2);
  ASSERT_TRUE(InputsEqual(phi2, {param, const0}));
}

// Test `HSelect` optimization in an irreducible loop.
TEST_F(ControlFlowSimplifierTest, testSelectInIrreducibleLoop) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [split, left_header, right_header, body] = CreateIrreducibleLoop(return_block);

  HParameterValue* split_param = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param = MakeParam(DataType::Type::kBool);
  HParameterValue* n_param = MakeParam(DataType::Type::kInt32);

  MakeIf(split, split_param);

  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  auto [left_phi, right_phi, add] =
      MakeLinearIrreducibleLoopVar(left_header, right_header, body, const1, const0, const1);
  HCondition* condition = MakeCondition(left_header, kCondGE, left_phi, n_param);
  MakeIf(left_header, condition);

  auto [if_block, then_block, else_block] = CreateDiamondPattern(body, bool_param);
  HPhi* phi = MakePhi(body, {const1, const0});

  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());
  HLoopInformation* loop_info = left_header->GetLoopInformation();
  ASSERT_TRUE(loop_info != nullptr);
  ASSERT_TRUE(loop_info->IsIrreducible());

  EXPECT_INS_REMOVED(phi);
  ASSERT_TRUE(if_block->GetFirstInstruction()->IsSelect());

  ASSERT_EQ(if_block, add->GetBlock());  // Moved when merging blocks.

  for (HBasicBlock* removed_block : {then_block, else_block, body}) {
    ASSERT_BLOCK_REMOVED(removed_block);
    uint32_t removed_block_id = removed_block->GetBlockId();
    ASSERT_FALSE(loop_info->GetBlocks().IsBitSet(removed_block_id)) << removed_block_id;
  }
}

}  // namespace art
