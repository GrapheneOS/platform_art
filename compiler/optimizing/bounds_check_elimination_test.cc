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

#include "bounds_check_elimination.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "gvn.h"
#include "induction_var_analysis.h"
#include "instruction_simplifier.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

#include "gtest/gtest.h"

namespace art HIDDEN {

/**
 * Fixture class for the BoundsCheckElimination tests.
 */
class BoundsCheckEliminationTest : public OptimizingUnitTest {
 public:
  void RunBCE() {
    graph_->SetHasBoundsChecks(true);
    graph_->BuildDominatorTree();

    InstructionSimplifier(graph_, /* codegen= */ nullptr).Run();

    GVNOptimization(graph_).Run();

    HInductionVarAnalysis induction(graph_);
    induction.Run();

    BoundsCheckElimination(graph_, &induction).Run();
  }

  HInstruction* BuildSSAGraph1(int initial, int increment, IfCondition cond = kCondGE);
  HInstruction* BuildSSAGraph2(int initial, int increment = -1, IfCondition cond = kCondLE);
  HInstruction* BuildSSAGraph3(int initial, int increment, IfCondition cond);
  HInstruction* BuildSSAGraph4(int initial, IfCondition cond = kCondGE);
};


// if (i < 0) { array[i] = 1; // Can't eliminate. }
// else if (i >= array.length) { array[i] = 1; // Can't eliminate. }
// else { array[i] = 1; // Can eliminate. }
TEST_F(BoundsCheckEliminationTest, NarrowingRangeArrayBoundsElimination) {
  CreateGraph();
  HBasicBlock* entry = AddNewBlock();
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);

  HBasicBlock* block1 = AddNewBlock();
  HInstruction* cmp = MakeCondition(block1, kCondGE, parameter2, constant_0);
  MakeIf(block1, cmp);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = AddNewBlock();
  HNullCheck* null_check = MakeNullCheck(block2, parameter1);
  HArrayLength* array_length = MakeArrayLength(block2, null_check);
  HBoundsCheck* bounds_check2 = MakeBoundsCheck(block2, parameter2, array_length);
  MakeArraySet(block2, null_check, bounds_check2, constant_1, DataType::Type::kInt32);

  HBasicBlock* block3 = AddNewBlock();
  null_check = MakeNullCheck(block3, parameter1);
  array_length = MakeArrayLength(block3, null_check);
  cmp = MakeCondition(block3, kCondLT, parameter2, array_length);
  MakeIf(block3, cmp);

  HBasicBlock* block4 = AddNewBlock();
  null_check = MakeNullCheck(block4, parameter1);
  array_length = MakeArrayLength(block4, null_check);
  HBoundsCheck* bounds_check4 = MakeBoundsCheck(block4, parameter2, array_length);
  MakeArraySet(block4, null_check, bounds_check4, constant_1, DataType::Type::kInt32);

  HBasicBlock* block5 = AddNewBlock();
  null_check = MakeNullCheck(block5, parameter1);
  array_length = MakeArrayLength(block5, null_check);
  HBoundsCheck* bounds_check5 = MakeBoundsCheck(block5, parameter2, array_length);
  MakeArraySet(block5, null_check, bounds_check5, constant_1, DataType::Type::kInt32);

  HBasicBlock* exit = AddNewBlock();
  block2->AddSuccessor(exit);
  block4->AddSuccessor(exit);
  block5->AddSuccessor(exit);
  MakeExit(exit);

  block1->AddSuccessor(block3);  // True successor
  block1->AddSuccessor(block2);  // False successor

  block3->AddSuccessor(block5);  // True successor
  block3->AddSuccessor(block4);  // False successor

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check2));
  ASSERT_FALSE(IsRemoved(bounds_check4));
  ASSERT_TRUE(IsRemoved(bounds_check5));
}

// if (i > 0) {
//   // Positive number plus MAX_INT will overflow and be negative.
//   int j = i + Integer.MAX_VALUE;
//   if (j < array.length) array[j] = 1;  // Can't eliminate.
// }
TEST_F(BoundsCheckEliminationTest, OverflowArrayBoundsElimination) {
  CreateGraph();
  HBasicBlock* entry = AddNewBlock();
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_max_int = graph_->GetIntConstant(INT_MAX);

  HBasicBlock* block1 = AddNewBlock();
  HInstruction* cmp = MakeCondition(block1, kCondLE, parameter2, constant_0);
  MakeIf(block1, cmp);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = AddNewBlock();
  HInstruction* add = MakeBinOp<HAdd>(block2, DataType::Type::kInt32, parameter2, constant_max_int);
  HNullCheck* null_check = MakeNullCheck(block2, parameter1);
  HArrayLength* array_length = MakeArrayLength(block2, null_check);
  HInstruction* cmp2 = MakeCondition(block2, kCondGE, add, array_length);
  MakeIf(block2, cmp2);

  HBasicBlock* block3 = AddNewBlock();
  HBoundsCheck* bounds_check = MakeBoundsCheck(block3, add, array_length);
  MakeArraySet(block3, null_check, bounds_check, constant_1, DataType::Type::kInt32);

  HBasicBlock* exit = AddNewBlock();
  MakeExit(exit);
  block1->AddSuccessor(exit);    // true successor
  block1->AddSuccessor(block2);  // false successor
  block2->AddSuccessor(exit);    // true successor
  block2->AddSuccessor(block3);  // false successor
  block3->AddSuccessor(exit);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check));
}

// if (i < array.length) {
//   int j = i - Integer.MAX_VALUE;
//   j = j - Integer.MAX_VALUE;  // j is (i+2) after subtracting MAX_INT twice
//   if (j > 0) array[j] = 1;    // Can't eliminate.
// }
TEST_F(BoundsCheckEliminationTest, UnderflowArrayBoundsElimination) {
  CreateGraph();
  HBasicBlock* entry = AddNewBlock();
  graph_->SetEntryBlock(entry);
  HInstruction* parameter1 = MakeParam(DataType::Type::kReference);  // array
  HInstruction* parameter2 = MakeParam(DataType::Type::kInt32);  // i

  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_max_int = graph_->GetIntConstant(INT_MAX);

  HBasicBlock* block1 = AddNewBlock();
  HNullCheck* null_check = MakeNullCheck(block1, parameter1);
  HArrayLength* array_length = MakeArrayLength(block1, null_check);
  HInstruction* cmp = MakeCondition(block1, kCondGE, parameter2, array_length);
  MakeIf(block1, cmp);
  entry->AddSuccessor(block1);

  HBasicBlock* block2 = AddNewBlock();
  HInstruction* sub1 =
      MakeBinOp<HSub>(block2, DataType::Type::kInt32, parameter2, constant_max_int);
  HInstruction* sub2 = MakeBinOp<HSub>(block2, DataType::Type::kInt32, sub1, constant_max_int);
  HInstruction* cmp2 = MakeCondition(block2, kCondLE, sub2, constant_0);
  MakeIf(block2, cmp2);

  HBasicBlock* block3 = AddNewBlock();
  HBoundsCheck* bounds_check = MakeBoundsCheck(block3, sub2, array_length);
  MakeArraySet(block3, null_check, bounds_check, constant_1, DataType::Type::kInt32);

  HBasicBlock* exit = AddNewBlock();
  MakeExit(exit);
  block1->AddSuccessor(exit);    // true successor
  block1->AddSuccessor(block2);  // false successor
  block2->AddSuccessor(exit);    // true successor
  block2->AddSuccessor(block3);  // false successor
  block3->AddSuccessor(exit);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check));
}

// array[6] = 1; // Can't eliminate.
// array[5] = 1; // Can eliminate.
// array[4] = 1; // Can eliminate.
TEST_F(BoundsCheckEliminationTest, ConstantArrayBoundsElimination) {
  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();

  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* constant_5 = graph_->GetIntConstant(5);
  HInstruction* constant_4 = graph_->GetIntConstant(4);
  HInstruction* constant_6 = graph_->GetIntConstant(6);
  HInstruction* constant_1 = graph_->GetIntConstant(1);

  HNullCheck* null_check = MakeNullCheck(block, parameter);
  HArrayLength* array_length = MakeArrayLength(block, null_check);
  HBoundsCheck* bounds_check6 = MakeBoundsCheck(block, constant_6, array_length);
  MakeArraySet(block, null_check, bounds_check6, constant_1, DataType::Type::kInt32);

  null_check = MakeNullCheck(block, parameter);
  array_length = MakeArrayLength(block, null_check);
  HBoundsCheck* bounds_check5 = MakeBoundsCheck(block, constant_5, array_length);
  MakeArraySet(block, null_check, bounds_check5, constant_1, DataType::Type::kInt32);

  null_check = MakeNullCheck(block, parameter);
  array_length = MakeArrayLength(block, null_check);
  HBoundsCheck* bounds_check4 = MakeBoundsCheck(block, constant_4, array_length);
  MakeArraySet(block, null_check, bounds_check4, constant_1, DataType::Type::kInt32);

  RunBCE();

  ASSERT_FALSE(IsRemoved(bounds_check6));
  ASSERT_TRUE(IsRemoved(bounds_check5));
  ASSERT_TRUE(IsRemoved(bounds_check4));
}

// for (int i=initial; i<array.length; i+=increment) { array[i] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph1(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);

  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* constant_10 = graph_->GetIntConstant(10);

  auto [phi, add] = MakeLinearLoopVar(loop_header, loop_body, initial, increment);
  HInstruction* null_check = MakeNullCheck(loop_header, parameter);
  HInstruction* array_length = MakeArrayLength(loop_header, null_check);
  DCHECK(cond == kCondGE || cond == kCondGT) << cond;
  HInstruction* cmp = MakeCondition(loop_header, cond, phi, array_length);
  MakeIf(loop_header, cmp);

  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, phi, array_length);
  MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1a) {
  // for (int i=0; i<array.length; i++) { array[i] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1b) {
  // for (int i=1; i<array.length; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(1, 1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1c) {
  // for (int i=-1; i<array.length; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(-1, 1);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1d) {
  // for (int i=0; i<=array.length; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 1, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1e) {
  // for (int i=0; i<array.length; i += 2) {
  //   array[i] = 10; // Can't eliminate due to overflow concern. }
  HInstruction* bounds_check = BuildSSAGraph1(0, 2);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination1f) {
  // for (int i=1; i<array.length; i += 2) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph1(1, 2);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// for (int i=array.length; i>0; i+=increment) { array[i-1] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph2(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);

  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* constant_initial = graph_->GetIntConstant(initial);
  HInstruction* constant_increment = graph_->GetIntConstant(increment);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);
  HInstruction* constant_10 = graph_->GetIntConstant(10);

  HInstruction* null_check = MakeNullCheck(pre_header, parameter);
  HInstruction* array_length = MakeArrayLength(pre_header, null_check);

  auto [phi, add] = MakeLinearLoopVar(loop_header, loop_body, array_length, constant_minus_1);
  DCHECK(cond == kCondLE || cond == kCondLT) << cond;
  HInstruction* cmp = MakeCondition(loop_header, cond, phi, constant_initial);
  MakeIf(loop_header, cmp);

  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, add, array_length);
  MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2a) {
  // for (int i=array.length; i>0; i--) { array[i-1] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph2(0);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2b) {
  // for (int i=array.length; i>1; i--) { array[i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2c) {
  // for (int i=array.length; i>-1; i--) { array[i-1] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(-1);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2d) {
  // for (int i=array.length; i>=0; i--) { array[i-1] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(0, -1, kCondLT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination2e) {
  // for (int i=array.length; i>0; i-=2) { array[i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph2(0, -2);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// int[] array = new int[10];
// for (int i=0; i<10; i+=increment) { array[i] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph3(int initial,
                                                         int increment,
                                                         IfCondition cond) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);

  HInstruction* constant_10 = graph_->GetIntConstant(10);

  // We pass a bogus constant for the class to avoid mocking one.
  HInstruction* new_array =
      MakeNewArray(pre_header, /* cls= */ constant_10, /* length= */ constant_10);

  auto [phi, add] = MakeLinearLoopVar(loop_header, loop_body, initial, increment);
  DCHECK(cond == kCondGE || cond == kCondGT) << cond;
  HInstruction* cmp = MakeCondition(loop_header, cond, phi, constant_10);
  MakeIf(loop_header, cmp);

  HNullCheck* null_check = MakeNullCheck(loop_body, new_array);
  HArrayLength* array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, phi, array_length);
  MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3a) {
  // int[] array = new int[10];
  // for (int i=0; i<10; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(0, 1, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3b) {
  // int[] array = new int[10];
  // for (int i=1; i<10; i++) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(1, 1, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3c) {
  // int[] array = new int[10];
  // for (int i=0; i<=10; i++) { array[i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(0, 1, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination3d) {
  // int[] array = new int[10];
  // for (int i=1; i<10; i+=8) { array[i] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph3(1, 8, kCondGE);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

// for (int i=initial; i<array.length; i++) { array[array.length-i-1] = 10; }
HInstruction* BoundsCheckEliminationTest::BuildSSAGraph4(int initial, IfCondition cond) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);

  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* constant_10 = graph_->GetIntConstant(10);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);

  auto [phi, add] = MakeLinearLoopVar(loop_header, loop_body, initial, /*increment=*/ 1);
  HInstruction* null_check = MakeNullCheck(loop_header, parameter);
  HInstruction* array_length = MakeArrayLength(loop_header, null_check);
  DCHECK(cond == kCondGE || cond == kCondGT) << cond;
  HInstruction* cmp = MakeCondition(loop_header, cond, phi, array_length);
  MakeIf(loop_header, cmp);

  null_check = MakeNullCheck(loop_body, parameter);
  array_length = MakeArrayLength(loop_body, null_check);
  HInstruction* sub = MakeBinOp<HSub>(loop_body, DataType::Type::kInt32, array_length, phi);
  HInstruction* add_minus_1 =
      MakeBinOp<HAdd>(loop_body, DataType::Type::kInt32, sub, constant_minus_1);
  HInstruction* bounds_check = MakeBoundsCheck(loop_body, add_minus_1, array_length);
  MakeArraySet(loop_body, null_check, bounds_check, constant_10, DataType::Type::kInt32);

  return bounds_check;
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4a) {
  // for (int i=0; i<array.length; i++) { array[array.length-i-1] = 10; // Can eliminate with gvn. }
  HInstruction* bounds_check = BuildSSAGraph4(0);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4b) {
  // for (int i=1; i<array.length; i++) { array[array.length-i-1] = 10; // Can eliminate. }
  HInstruction* bounds_check = BuildSSAGraph4(1);
  RunBCE();
  ASSERT_TRUE(IsRemoved(bounds_check));
}

TEST_F(BoundsCheckEliminationTest, LoopArrayBoundsElimination4c) {
  // for (int i=0; i<=array.length; i++) { array[array.length-i] = 10; // Can't eliminate. }
  HInstruction* bounds_check = BuildSSAGraph4(0, kCondGT);
  RunBCE();
  ASSERT_FALSE(IsRemoved(bounds_check));
}

// Bubble sort:
// (Every array access bounds-check can be eliminated.)
// for (int i=0; i<array.length-1; i++) {
//   for (int j=0; j<array.length-i-1; j++) {
//     if (array[j] > array[j+1]) {
//       int temp = array[j+1];
//       array[j+1] = array[j];
//       array[j] = temp;
//     }
//   }
// }
TEST_F(BoundsCheckEliminationTest, BubbleSortArrayBoundsElimination) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [outer_pre_header, outer_header, outer_body_add] = CreateWhileLoop(return_block);
  auto [inner_pre_header, inner_header, inner_body_add] = CreateWhileLoop(outer_body_add);
  auto [inner_body_compare, inner_body_swap, skip_swap] = CreateDiamondPattern(inner_body_add);

  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* constant_0 = graph_->GetIntConstant(0);
  HInstruction* constant_minus_1 = graph_->GetIntConstant(-1);
  HInstruction* constant_1 = graph_->GetIntConstant(1);

  auto [phi_i, add_i] =
      MakeLinearLoopVar(outer_header, outer_body_add, /*initial=*/ 0, /*increment=*/ 1);
  HNullCheck* null_check = MakeNullCheck(outer_header, parameter);
  HArrayLength* array_length = MakeArrayLength(outer_header, null_check);
  HAdd* add = MakeBinOp<HAdd>(outer_header, DataType::Type::kInt32, array_length, constant_minus_1);
  HInstruction* cmp = MakeCondition(outer_header, kCondGE, phi_i, add);
  MakeIf(outer_header, cmp);

  auto [phi_j, add_j] =
      MakeLinearLoopVar(inner_header, inner_body_add, /*initial=*/ 0, /*increment=*/ 1);
  null_check = MakeNullCheck(inner_header, parameter);
  array_length = MakeArrayLength(inner_header, null_check);
  HSub* sub = MakeBinOp<HSub>(inner_header, DataType::Type::kInt32, array_length, phi_i);
  add = MakeBinOp<HAdd>(inner_header, DataType::Type::kInt32, sub, constant_minus_1);
  cmp = MakeCondition(inner_header, kCondGE, phi_j, add);
  MakeIf(inner_header, cmp);

  null_check = MakeNullCheck(inner_body_compare, parameter);
  array_length = MakeArrayLength(inner_body_compare, null_check);
  HBoundsCheck* bounds_check1 = MakeBoundsCheck(inner_body_compare, phi_j, array_length);
  HArrayGet* array_get_j =
      MakeArrayGet(inner_body_compare, null_check, bounds_check1, DataType::Type::kInt32);
  HInstruction* j_plus_1 =
      MakeBinOp<HAdd>(inner_body_compare, DataType::Type::kInt32, phi_j, constant_1);
  null_check = MakeNullCheck(inner_body_compare, parameter);
  array_length = MakeArrayLength(inner_body_compare, null_check);
  HBoundsCheck* bounds_check2 = MakeBoundsCheck(inner_body_compare, j_plus_1, array_length);
  HArrayGet* array_get_j_plus_1 =
      MakeArrayGet(inner_body_compare, null_check, bounds_check2, DataType::Type::kInt32);
  cmp = MakeCondition(inner_body_compare, kCondGE, array_get_j, array_get_j_plus_1);
  MakeIf(inner_body_compare, cmp);

  j_plus_1 = MakeBinOp<HAdd>(inner_body_swap, DataType::Type::kInt32, phi_j, constant_1);
  // temp = array[j+1]
  null_check = MakeNullCheck(inner_body_swap, parameter);
  array_length = MakeArrayLength(inner_body_swap, null_check);
  HInstruction* bounds_check3 = MakeBoundsCheck(inner_body_swap, j_plus_1, array_length);
  array_get_j_plus_1 =
      MakeArrayGet(inner_body_swap, null_check, bounds_check3, DataType::Type::kInt32);
  // array[j+1] = array[j]
  null_check = MakeNullCheck(inner_body_swap, parameter);
  array_length = MakeArrayLength(inner_body_swap, null_check);
  HInstruction* bounds_check4 = MakeBoundsCheck(inner_body_swap, phi_j, array_length);
  array_get_j = MakeArrayGet(inner_body_swap, null_check, bounds_check4, DataType::Type::kInt32);
  null_check = MakeNullCheck(inner_body_swap, parameter);
  array_length = MakeArrayLength(inner_body_swap, null_check);
  HInstruction* bounds_check5 = MakeBoundsCheck(inner_body_swap, j_plus_1, array_length);
  MakeArraySet(inner_body_swap, null_check, bounds_check5, array_get_j, DataType::Type::kInt32);
  // array[j] = temp
  null_check = MakeNullCheck(inner_body_swap, parameter);
  array_length = MakeArrayLength(inner_body_swap, null_check);
  HInstruction* bounds_check6 = MakeBoundsCheck(inner_body_swap, phi_j, array_length);
  MakeArraySet(
      inner_body_swap, null_check, bounds_check6, array_get_j_plus_1, DataType::Type::kInt32);

  RunBCE();  // gvn removes same bounds check already

  ASSERT_TRUE(IsRemoved(bounds_check1));
  ASSERT_TRUE(IsRemoved(bounds_check2));
  ASSERT_TRUE(IsRemoved(bounds_check3));
  ASSERT_TRUE(IsRemoved(bounds_check4));
  ASSERT_TRUE(IsRemoved(bounds_check5));
  ASSERT_TRUE(IsRemoved(bounds_check6));
}

// int[] array = new int[10];
// for (int i=0; i<200; i++) {
//   array[i%10] = 10;            // Can eliminate
//   array[i%1] = 10;             // Can eliminate
//   array[i%200] = 10;           // Cannot eliminate
//   array[i%-10] = 10;           // Can eliminate
//   array[i%array.length] = 10;  // Can eliminate
//   array[param_i%10] = 10;      // Can't eliminate, when param_i < 0
// }
TEST_F(BoundsCheckEliminationTest, ModArrayBoundsElimination) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [pre_header, loop_header, loop_body] = CreateWhileLoop(return_block);

  HInstruction* param_i = MakeParam(DataType::Type::kInt32);
  HInstruction* constant_1 = graph_->GetIntConstant(1);
  HInstruction* constant_10 = graph_->GetIntConstant(10);
  HInstruction* constant_200 = graph_->GetIntConstant(200);
  HInstruction* constant_minus_10 = graph_->GetIntConstant(-10);

  // We pass a bogus constant for the class to avoid mocking one.
  HInstruction* new_array =
      MakeNewArray(pre_header, /* cls= */ constant_10, /* length= */ constant_10);

  auto [phi, add] = MakeLinearLoopVar(loop_header, loop_body, /*initial=*/ 0, /*increment=*/ 1);
  HInstruction* cmp = MakeCondition(loop_header, kCondGE, phi, constant_200);
  MakeIf(loop_header, cmp);

  //////////////////////////////////////////////////////////////////////////////////
  // LOOP BODY:
  // array[i % 10] = 10;
  HRem* i_mod_10 = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, phi, constant_10);
  HBoundsCheck* bounds_check_i_mod_10 = MakeBoundsCheck(loop_body, i_mod_10, constant_10);
  MakeArraySet(loop_body, new_array, bounds_check_i_mod_10, constant_10, DataType::Type::kInt32);

  // array[i % 1] = 10;
  HRem* i_mod_1 = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, phi, constant_1);
  HBoundsCheck* bounds_check_i_mod_1 = MakeBoundsCheck(loop_body, i_mod_1, constant_10);
  MakeArraySet(loop_body, new_array, bounds_check_i_mod_1, constant_10, DataType::Type::kInt32);

  // array[i % 200] = 10;
  HRem* i_mod_200 = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, phi, constant_200);
  HBoundsCheck* bounds_check_i_mod_200 = MakeBoundsCheck(loop_body, i_mod_200, constant_10);
  MakeArraySet(loop_body, new_array, bounds_check_i_mod_200, constant_10, DataType::Type::kInt32);

  // array[i % -10] = 10;
  HRem* i_mod_minus_10 = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, phi, constant_minus_10);
  HBoundsCheck* bounds_check_i_mod_minus_10 =
      MakeBoundsCheck(loop_body, i_mod_minus_10, constant_10);
  MakeArraySet(
      loop_body, new_array, bounds_check_i_mod_minus_10, constant_10, DataType::Type::kInt32);

  // array[i%array.length] = 10;
  HNullCheck* null_check = MakeNullCheck(loop_body, new_array);
  HArrayLength* array_length = MakeArrayLength(loop_body, null_check);
  HRem* i_mod_array_length = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, phi, array_length);
  HBoundsCheck* bounds_check_i_mod_array_len =
      MakeBoundsCheck(loop_body, i_mod_array_length, array_length);
  MakeArraySet(
      loop_body, null_check, bounds_check_i_mod_array_len, constant_10, DataType::Type::kInt32);

  // array[param_i % 10] = 10;
  HRem* param_i_mod_10 = MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, param_i, constant_10);
  HBoundsCheck* bounds_check_param_i_mod_10 =
      MakeBoundsCheck(loop_body, param_i_mod_10, constant_10);
  MakeArraySet(
      loop_body, new_array, bounds_check_param_i_mod_10, constant_10, DataType::Type::kInt32);

  // array[param_i%array.length] = 10;
  null_check = MakeNullCheck(loop_body, new_array);
  array_length = MakeArrayLength(loop_body, null_check);
  HRem* param_i_mod_array_length =
      MakeBinOp<HRem>(loop_body, DataType::Type::kInt32, param_i, array_length);
  HBoundsCheck* bounds_check_param_i_mod_array_len =
      MakeBoundsCheck(loop_body, param_i_mod_array_length, array_length);
  MakeArraySet(loop_body,
               null_check,
               bounds_check_param_i_mod_array_len,
               constant_10,
               DataType::Type::kInt32);

  //////////////////////////////////////////////////////////////////////////////////

  RunBCE();

  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_10));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_1));
  ASSERT_FALSE(IsRemoved(bounds_check_i_mod_200));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_minus_10));
  ASSERT_TRUE(IsRemoved(bounds_check_i_mod_array_len));
  ASSERT_FALSE(IsRemoved(bounds_check_param_i_mod_10));
}

}  // namespace art
