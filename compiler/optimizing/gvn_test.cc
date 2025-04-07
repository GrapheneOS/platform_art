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

#include "gvn.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

namespace art HIDDEN {

class GVNTest : public OptimizingUnitTest {};

TEST_F(GVNTest, LocalFieldElimination) {
  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  MakeIFieldGet(block, parameter, DataType::Type::kReference, MemberOffset(42));
  HInstruction* to_remove =
      MakeIFieldGet(block, parameter, DataType::Type::kReference, MemberOffset(42));
  HInstruction* different_offset =
      MakeIFieldGet(block, parameter, DataType::Type::kReference, MemberOffset(43));
  // Kill the value.
  MakeIFieldSet(block, parameter, parameter, MemberOffset(42));
  HInstruction* use_after_kill =
      MakeIFieldGet(block, parameter, DataType::Type::kReference, MemberOffset(42));

  ASSERT_EQ(to_remove->GetBlock(), block);
  ASSERT_EQ(different_offset->GetBlock(), block);
  ASSERT_EQ(use_after_kill->GetBlock(), block);

  graph_->BuildDominatorTree();
  GVNOptimization(graph_).Run();

  ASSERT_TRUE(to_remove->GetBlock() == nullptr);
  ASSERT_EQ(different_offset->GetBlock(), block);
  ASSERT_EQ(use_after_kill->GetBlock(), block);
}

TEST_F(GVNTest, GlobalFieldElimination) {
  HBasicBlock* join = InitEntryMainExitGraphWithReturnVoid();
  auto [block, then, else_] = CreateDiamondPattern(join);

  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  HInstruction* field_get =
      MakeIFieldGet(block, parameter, DataType::Type::kBool, MemberOffset(42));
  MakeIf(block, field_get);

  MakeIFieldGet(then, parameter, DataType::Type::kBool, MemberOffset(42));
  MakeIFieldGet(else_, parameter, DataType::Type::kBool, MemberOffset(42));
  MakeIFieldGet(join, parameter, DataType::Type::kBool, MemberOffset(42));

  graph_->BuildDominatorTree();
  GVNOptimization(graph_).Run();

  // Check that all field get instructions have been GVN'ed.
  ASSERT_TRUE(then->GetFirstInstruction()->IsGoto());
  ASSERT_TRUE(else_->GetFirstInstruction()->IsGoto());
  ASSERT_TRUE(join->GetFirstInstruction()->IsReturnVoid());
}

TEST_F(GVNTest, LoopFieldElimination) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);
  loop_header->SwapSuccessors();  // Move the loop exit to the "else" successor.

  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  MakeIFieldGet(pre_header, parameter, DataType::Type::kBool, MemberOffset(42));

  HInstruction* field_get_in_loop_header =
      MakeIFieldGet(loop_header, parameter, DataType::Type::kBool, MemberOffset(42));
  MakeIf(loop_header, field_get_in_loop_header);

  // Kill inside the loop body to prevent field gets inside the loop header
  // and the body to be GVN'ed.
  HInstruction* field_set =
      MakeIFieldSet(loop_body, parameter, parameter, DataType::Type::kBool, MemberOffset(42));
  HInstruction* field_get_in_loop_body =
      MakeIFieldGet(loop_body, parameter, DataType::Type::kBool, MemberOffset(42));

  HInstruction* field_get_in_return_block =
      MakeIFieldGet(return_block, parameter, DataType::Type::kBool, MemberOffset(42));

  ASSERT_EQ(field_get_in_loop_header->GetBlock(), loop_header);
  ASSERT_EQ(field_get_in_loop_body->GetBlock(), loop_body);
  ASSERT_EQ(field_get_in_return_block->GetBlock(), return_block);

  graph_->BuildDominatorTree();
  GVNOptimization(graph_).Run();

  // Check that all field get instructions are still there.
  ASSERT_EQ(field_get_in_loop_header->GetBlock(), loop_header);
  ASSERT_EQ(field_get_in_loop_body->GetBlock(), loop_body);
  // The `return_block` is dominated by the `loop_header`, whose field get
  // does not get killed by the loop flags.
  ASSERT_TRUE(field_get_in_return_block->GetBlock() == nullptr);

  // Now remove the field set, and check that all field get instructions have been GVN'ed.
  loop_body->RemoveInstruction(field_set);
  GVNOptimization(graph_).Run();

  ASSERT_TRUE(field_get_in_loop_header->GetBlock() == nullptr);
  ASSERT_TRUE(field_get_in_loop_body->GetBlock() == nullptr);
  ASSERT_TRUE(field_get_in_return_block->GetBlock() == nullptr);
}

// Test that inner loops affect the side effects of the outer loop.
TEST_F(GVNTest, LoopSideEffects) {
  static const SideEffects kCanTriggerGC = SideEffects::CanTriggerGC();

  HBasicBlock* outer_loop_exit = InitEntryMainExitGraphWithReturnVoid();
  auto [outer_preheader, outer_loop_header, inner_loop_exit] = CreateWhileLoop(outer_loop_exit);
  outer_loop_header->SwapSuccessors();  // Move the loop exit to the "else" successor.
  auto [outer_loop_body, inner_loop_header, inner_loop_body] = CreateWhileLoop(inner_loop_exit);
  inner_loop_header->SwapSuccessors();  // Move the loop exit to the "else" successor.

  HInstruction* parameter = MakeParam(DataType::Type::kBool);
  MakeSuspendCheck(outer_loop_header);
  MakeIf(outer_loop_header, parameter);
  MakeSuspendCheck(inner_loop_header);
  MakeIf(inner_loop_header, parameter);

  graph_->BuildDominatorTree();

  ASSERT_TRUE(inner_loop_header->GetLoopInformation()->IsIn(
      *outer_loop_header->GetLoopInformation()));

  // Check that the only side effect of loops is to potentially trigger GC.
  {
    // Make one block with a side effect.
    MakeIFieldSet(entry_block_, parameter, parameter, DataType::Type::kReference, MemberOffset(42));

    SideEffectsAnalysis side_effects(graph_);
    side_effects.Run();

    ASSERT_TRUE(side_effects.GetBlockEffects(entry_block_).DoesAnyWrite());
    ASSERT_FALSE(side_effects.GetBlockEffects(outer_loop_body).DoesAnyWrite());
    ASSERT_FALSE(side_effects.GetLoopEffects(outer_loop_header).DoesAnyWrite());
    ASSERT_FALSE(side_effects.GetLoopEffects(inner_loop_header).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetLoopEffects(outer_loop_header).Equals(kCanTriggerGC));
    ASSERT_TRUE(side_effects.GetLoopEffects(inner_loop_header).Equals(kCanTriggerGC));
  }

  // Check that the side effects of the outer loop does not affect the inner loop.
  {
    MakeIFieldSet(
        outer_loop_body, parameter, parameter, DataType::Type::kReference, MemberOffset(42));

    SideEffectsAnalysis side_effects(graph_);
    side_effects.Run();

    ASSERT_TRUE(side_effects.GetBlockEffects(entry_block_).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetBlockEffects(outer_loop_body).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetLoopEffects(outer_loop_header).DoesAnyWrite());
    ASSERT_FALSE(side_effects.GetLoopEffects(inner_loop_header).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetLoopEffects(inner_loop_header).Equals(kCanTriggerGC));
  }

  // Check that the side effects of the inner loop affects the outer loop.
  {
    outer_loop_body->RemoveInstruction(outer_loop_body->GetFirstInstruction());
    MakeIFieldSet(
        inner_loop_body, parameter, parameter, DataType::Type::kReference, MemberOffset(42));

    SideEffectsAnalysis side_effects(graph_);
    side_effects.Run();

    ASSERT_TRUE(side_effects.GetBlockEffects(entry_block_).DoesAnyWrite());
    ASSERT_FALSE(side_effects.GetBlockEffects(outer_loop_body).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetLoopEffects(outer_loop_header).DoesAnyWrite());
    ASSERT_TRUE(side_effects.GetLoopEffects(inner_loop_header).DoesAnyWrite());
  }
}
}  // namespace art
