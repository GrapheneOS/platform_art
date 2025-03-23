/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "load_store_elimination.h"

#include <initializer_list>
#include <memory>
#include <tuple>
#include <variant>

#include "base/iteration_range.h"
#include "compilation_kind.h"
#include "dex/dex_file_types.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "entrypoints/quick/quick_entrypoints_enum.h"
#include "gtest/gtest.h"
#include "handle_scope.h"
#include "load_store_analysis.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing/instruction_simplifier.h"
#include "optimizing/optimizing_compiler_stats.h"
#include "optimizing_unit_test.h"
#include "scoped_thread_state_change.h"

namespace art HIDDEN {

static constexpr bool kDebugLseTests = false;

#define CHECK_SUBROUTINE_FAILURE() \
  do {                             \
    if (HasFatalFailure()) {       \
      return;                      \
    }                              \
  } while (false)

template <typename SuperTest>
class LoadStoreEliminationTestBase : public SuperTest, public OptimizingUnitTestHelper {
 public:
  LoadStoreEliminationTestBase() {
    this->use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }

  void SetUp() override {
    SuperTest::SetUp();
    if (kDebugLseTests) {
      gLogVerbosity.compiler = true;
    }
  }

  void TearDown() override {
    SuperTest::TearDown();
    if (kDebugLseTests) {
      gLogVerbosity.compiler = false;
    }
  }

  void PerformLSE() {
    graph_->BuildDominatorTree();
    LoadStoreElimination lse(graph_, /*stats=*/nullptr);
    lse.Run();
    std::ostringstream oss;
    EXPECT_TRUE(CheckGraph(oss)) << oss.str();
  }

  void PerformLSE(const AdjacencyListGraph& blks) {
    // PerformLSE expects this to be empty, and the creation of
    // an `AdjacencyListGraph` computes it.
    graph_->ClearDominanceInformation();
    if (kDebugLseTests) {
      LOG(INFO) << "Pre LSE " << blks;
    }
    PerformLSE();
    if (kDebugLseTests) {
      LOG(INFO) << "Post LSE " << blks;
    }
  }

  // Create instructions shared among tests.
  void CreateEntryBlockInstructions() {
    HInstruction* c1 = graph_->GetIntConstant(1);
    HInstruction* c4 = graph_->GetIntConstant(4);
    i_add1_ = MakeBinOp<HAdd>(entry_block_, DataType::Type::kInt32, i_, c1);
    i_add4_ = MakeBinOp<HAdd>(entry_block_, DataType::Type::kInt32, i_, c4);
    MakeGoto(entry_block_);
  }

  // Create suspend check, linear loop variable and loop condition.
  // The `HPhi` for the loop variable can be easily retrieved as the only `HPhi` in the loop block.
  // The `HSuspendCheck` can be retrieved as the first non-Phi instruction from the loop block.
  void MakeSimpleLoopInstructions(HBasicBlock* loop,
                                  HBasicBlock* body,
                                  std::initializer_list<HInstruction*> suspend_check_env = {}) {
    CHECK(loop->GetInstructions().IsEmpty());
    CHECK_IMPLIES(loop != body, body->IsSingleGoto());
    HInstruction* c128 = graph_->GetIntConstant(128);
    MakeSuspendCheck(loop, suspend_check_env);
    auto [phi, increment] = MakeLinearLoopVar(loop, body, /*initial=*/ 0, /*increment=*/ 1);
    HInstruction* cmp = MakeCondition(loop, kCondGE, phi, c128);
    MakeIf(loop, cmp);
  }

  // Create a do-while loop with instructions:
  //   i = 0;
  //   do {
  //     HSuspendCheck;
  //     cmp = i < 128;
  //     ++i;
  //   } while (cmp);
  // Return the pre-header and loop block.
  std::tuple<HBasicBlock*, HBasicBlock*> CreateDoWhileLoopWithInstructions(
      HBasicBlock* loop_exit, std::initializer_list<HInstruction*> suspend_check_env = {}) {
    auto [pre_header, loop, back_edge] = CreateWhileLoop(loop_exit);
    MakeSimpleLoopInstructions(loop, loop, suspend_check_env);
    return {pre_header, loop};
  }

  // Create a for loop with instructions:
  //   for (int i = 0; i < 128; ++i) {
  //     HSuspendCheck;
  //   }
  // Return the pre-header, header and body blocks.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateForLoopWithInstructions(
      HBasicBlock* loop_exit, std::initializer_list<HInstruction*> suspend_check_env = {}) {
    auto [pre_header, loop_header, loop_body] = CreateWhileLoop(loop_exit);
    MakeSimpleLoopInstructions(loop_header, loop_body, suspend_check_env);
    return {pre_header, loop_header, loop_body};
  }

  // Create the major CFG used by tests:
  //    entry
  //      |
  //  pre_header
  //      |
  //    loop[]
  //      |
  //   return
  //      |
  //     exit
  void CreateTestControlFlowGraph() {
    InitGraphAndParameters();
    CreateEntryBlockInstructions();
    std::tie(pre_header_, loop_) =
        CreateDoWhileLoopWithInstructions(return_block_, /*suspend_check_env=*/ {array_, i_, j_});
    phi_ = loop_->GetFirstPhi()->AsPhi();
    suspend_check_ = loop_->GetFirstInstruction()->AsSuspendCheck();
  }

  // Create the diamond-shaped CFG:
  //      upper
  //      /   \
  //    left  right
  //      \   /
  //      down
  //
  // Return: the basic blocks forming the CFG in the following order {upper, left, right, down}.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateDiamondShapedCFG() {
    InitGraphAndParameters();
    CreateEntryBlockInstructions();

    auto [upper, left, right] = CreateDiamondPattern(return_block_);

    HInstruction* cmp = MakeCondition(upper, kCondGE, i_, j_);
    MakeIf(upper, cmp);

    return std::make_tuple(upper, left, right, return_block_);
  }

  // Add a HVecLoad instruction to the end of the provided basic block.
  //
  // Return: the created HVecLoad instruction.
  HInstruction* AddVecLoad(HBasicBlock* block, HInstruction* array, HInstruction* index) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    HInstruction* vload =
        new (GetAllocator()) HVecLoad(GetAllocator(),
                                      array,
                                      index,
                                      DataType::Type::kInt32,
                                      SideEffects::ArrayReadOfType(DataType::Type::kInt32),
                                      4,
                                      /*is_string_char_at*/ false,
                                      kNoDexPc);
    block->InsertInstructionBefore(vload, block->GetLastInstruction());
    return vload;
  }

  // Add a HVecStore instruction to the end of the provided basic block.
  // If no vdata is specified, generate HVecStore: array[index] = [1,1,1,1].
  //
  // Return: the created HVecStore instruction.
  HInstruction* AddVecStore(HBasicBlock* block,
                            HInstruction* array,
                            HInstruction* index,
                            HInstruction* vdata = nullptr) {
    DCHECK(block != nullptr);
    DCHECK(array != nullptr);
    DCHECK(index != nullptr);
    if (vdata == nullptr) {
      HInstruction* c1 = graph_->GetIntConstant(1);
      vdata = new (GetAllocator())
          HVecReplicateScalar(GetAllocator(), c1, DataType::Type::kInt32, 4, kNoDexPc);
      block->InsertInstructionBefore(vdata, block->GetLastInstruction());
    }
    HInstruction* vstore =
        new (GetAllocator()) HVecStore(GetAllocator(),
                                       array,
                                       index,
                                       vdata,
                                       DataType::Type::kInt32,
                                       SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
                                       4,
                                       kNoDexPc);
    block->InsertInstructionBefore(vstore, block->GetLastInstruction());
    return vstore;
  }

  void InitGraphAndParameters() {
    return_block_ = InitEntryMainExitGraphWithReturnVoid();
    array_ = MakeParam(DataType::Type::kInt32);
    i_ = MakeParam(DataType::Type::kInt32);
    j_ = MakeParam(DataType::Type::kInt32);
  }

  HBasicBlock* return_block_;
  HBasicBlock* pre_header_;
  HBasicBlock* loop_;

  HInstruction* array_;
  HInstruction* i_;
  HInstruction* j_;
  HInstruction* i_add1_;
  HInstruction* i_add4_;
  HInstruction* suspend_check_;

  HPhi* phi_;
};

class LoadStoreEliminationTest : public LoadStoreEliminationTestBase<CommonCompilerTest> {};

TEST_F(LoadStoreEliminationTest, ArrayGetSetElimination) {
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  // array[1] = 1;
  // x = array[1];  <--- Remove.
  // y = array[2];
  // array[1] = 1;  <--- Remove, since it stores same value.
  // array[i] = 3;  <--- MAY alias.
  // array[1] = 1;  <--- Cannot remove, even if it stores the same value.
  MakeArraySet(entry_block_, array_, c1, c1);
  HInstruction* load1 = MakeArrayGet(entry_block_, array_, c1, DataType::Type::kInt32);
  HInstruction* load2 = MakeArrayGet(entry_block_, array_, c2, DataType::Type::kInt32);
  HInstruction* store1 = MakeArraySet(entry_block_, array_, c1, c1);
  MakeArraySet(entry_block_, array_, i_, c3);
  HInstruction* store2 = MakeArraySet(entry_block_, array_, c1, c1);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load1));
  ASSERT_FALSE(IsRemoved(load2));
  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue1) {
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);

  // Test LSE handling same value stores on array.
  // array[1] = 1;
  // array[2] = 1;
  // array[1] = 1;  <--- Can remove.
  // array[1] = 2;  <--- Can NOT remove.
  MakeArraySet(entry_block_, array_, c1, c1);
  MakeArraySet(entry_block_, array_, c2, c1);
  HInstruction* store1 = MakeArraySet(entry_block_, array_, c1, c1);
  HInstruction* store2 = MakeArraySet(entry_block_, array_, c1, c2);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue2) {
  CreateTestControlFlowGraph();

  // Test LSE handling same value stores on vector.
  // vdata = [0x1, 0x2, 0x3, 0x4, ...]
  // VecStore array[i...] = vdata;
  // VecStore array[j...] = vdata;  <--- MAY ALIAS.
  // VecStore array[i...] = vdata;  <--- Cannot Remove, even if it's same value.
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, j_);
  HInstruction* vstore = AddVecStore(entry_block_, array_, i_);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore));
}

TEST_F(LoadStoreEliminationTest, SameHeapValue3) {
  CreateTestControlFlowGraph();

  // VecStore array[i...] = vdata;
  // VecStore array[i+1...] = vdata;  <--- MAY alias due to partial overlap.
  // VecStore array[i...] = vdata;    <--- Cannot remove, even if it's same value.
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, i_add1_);
  HInstruction* vstore = AddVecStore(entry_block_, array_, i_);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore));
}

TEST_F(LoadStoreEliminationTest, OverlappingLoadStore) {
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);

  // Test LSE handling array LSE when there is vector store in between.
  // a[i] = 1;
  // .. = a[i];                <-- Remove.
  // a[i,i+1,i+2,i+3] = data;  <-- PARTIAL OVERLAP !
  // .. = a[i];                <-- Cannot remove.
  MakeArraySet(entry_block_, array_, i_, c1);
  HInstruction* load1 = MakeArrayGet(entry_block_, array_, i_, DataType::Type::kInt32);
  AddVecStore(entry_block_, array_, i_);
  HInstruction* load2 = MakeArrayGet(entry_block_, array_, i_, DataType::Type::kInt32);

  // Test LSE handling vector load/store partial overlap.
  // a[i,i+1,i+2,i+3] = data;
  // a[i+4,i+5,i+6,i+7] = data;
  // .. = a[i,i+1,i+2,i+3];
  // .. = a[i+4,i+5,i+6,i+7];
  // a[i+1,i+2,i+3,i+4] = data;  <-- PARTIAL OVERLAP !
  // .. = a[i,i+1,i+2,i+3];
  // .. = a[i+4,i+5,i+6,i+7];
  AddVecStore(entry_block_, array_, i_);
  AddVecStore(entry_block_, array_, i_add4_);
  HInstruction* vload1 = AddVecLoad(entry_block_, array_, i_);
  HInstruction* vload2 = AddVecLoad(entry_block_, array_, i_add4_);
  AddVecStore(entry_block_, array_, i_add1_);
  HInstruction* vload3 = AddVecLoad(entry_block_, array_, i_);
  HInstruction* vload4 = AddVecLoad(entry_block_, array_, i_add4_);

  // Test LSE handling vector LSE when there is array store in between.
  // a[i,i+1,i+2,i+3] = data;
  // a[i+1] = 1;                 <-- PARTIAL OVERLAP !
  // .. = a[i,i+1,i+2,i+3];
  AddVecStore(entry_block_, array_, i_);
  MakeArraySet(entry_block_, array_, i_, c1);
  HInstruction* vload5 = AddVecLoad(entry_block_, array_, i_);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_TRUE(IsRemoved(load1));
  ASSERT_FALSE(IsRemoved(load2));

  ASSERT_TRUE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vload3));
  ASSERT_FALSE(IsRemoved(vload4));

  ASSERT_FALSE(IsRemoved(vload5));
}
// function (int[] a, int j) {
// a[j] = 1;
// for (int i=0; i<128; i++) {
//    /* doesn't do any write */
// }
// a[j] = 1;
TEST_F(LoadStoreEliminationTest, StoreAfterLoopWithoutSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c1 = graph_->GetIntConstant(1);

  // a[j] = 1
  MakeArraySet(pre_header_, array_, j_, c1);

  // LOOP BODY:
  // .. = a[i,i+1,i+2,i+3];
  AddVecLoad(loop_, array_, phi_);

  // a[j] = 1;
  HInstruction* array_set = MakeArraySet(return_block_, array_, j_, c1);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_TRUE(IsRemoved(array_set));
}

// function (int[] a, int j) {
//   int[] b = new int[128];
//   a[j] = 0;
//   for (int phi=0; phi<128; phi++) {
//     a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
//     b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
//   }
//   a[j] = 0;
// }
TEST_F(LoadStoreEliminationTest, StoreAfterSIMDLoopWithSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // a[j] = 0;
  MakeArraySet(pre_header_, array_, j_, c0);

  // LOOP BODY:
  // a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
  // b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
  AddVecStore(loop_, array_, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_, phi_);
  AddVecStore(loop_, array_b, phi_, vload);

  // a[j] = 0;
  HInstruction* a_set = MakeArraySet(return_block_, array_, j_, c0);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(a_set));  // Cannot remove due to write side-effect in the loop.
}

// function (int[] a, int j) {
//   int[] b = new int[128];
//   a[j] = 0;
//   for (int phi=0; phi<128; phi++) {
//     a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
//     b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
//   }
//   x = a[j];
// }
TEST_F(LoadStoreEliminationTest, LoadAfterSIMDLoopWithSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // a[j] = 0;
  MakeArraySet(pre_header_, array_, j_, c0);

  // LOOP BODY:
  // a[phi,phi+1,phi+2,phi+3] = [1,1,1,1];
  // b[phi,phi+1,phi+2,phi+3] = a[phi,phi+1,phi+2,phi+3];
  AddVecStore(loop_, array_, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_, phi_);
  AddVecStore(loop_, array_b, phi_, vload);

  // x = a[j];
  HInstruction* load = MakeArrayGet(return_block_, array_, j_, DataType::Type::kInt32);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(load));  // Cannot remove due to write side-effect in the loop.
}

// Check that merging works correctly when there are VecStors in predecessors.
//
//                  vstore1: a[i,... i + 3] = [1,...1]
//                       /          \
//                      /            \
// vstore2: a[i,... i + 3] = [1,...1]  vstore3: a[i+1, ... i + 4] = [1, ... 1]
//                     \              /
//                      \            /
//                  vstore4: a[i,... i + 3] = [1,...1]
//
// Expected:
//   'vstore2' is removed.
//   'vstore3' is not removed.
//   'vstore4' is not removed. Such cases are not supported at the moment.
TEST_F(LoadStoreEliminationTest, MergePredecessorVecStores) {
  auto [upper, left, right, down] = CreateDiamondShapedCFG();

  // upper: a[i,... i + 3] = [1,...1]
  HInstruction* vstore1 = AddVecStore(upper, array_, i_);
  HInstruction* vdata = vstore1->InputAt(2);

  // left: a[i,... i + 3] = [1,...1]
  HInstruction* vstore2 = AddVecStore(left, array_, i_, vdata);

  // right: a[i+1, ... i + 4] = [1, ... 1]
  HInstruction* vstore3 = AddVecStore(right, array_, i_add1_, vdata);

  // down: a[i,... i + 3] = [1,...1]
  HInstruction* vstore4 = AddVecStore(down, array_, i_, vdata);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_TRUE(IsRemoved(vstore2));
  ASSERT_FALSE(IsRemoved(vstore3));
  ASSERT_FALSE(IsRemoved(vstore4));
}

// Check that merging works correctly when there are ArraySets in predecessors.
//
//          a[i] = 1
//        /          \
//       /            \
// store1: a[i] = 1  store2: a[i+1] = 1
//       \            /
//        \          /
//          store3: a[i] = 1
//
// Expected:
//   'store1' is removed.
//   'store2' is not removed.
//   'store3' is removed.
TEST_F(LoadStoreEliminationTest, MergePredecessorStores) {
  auto [upper, left, right, down] = CreateDiamondShapedCFG();

  HInstruction* c1 = graph_->GetIntConstant(1);

  // upper: a[i] = 1
  MakeArraySet(upper, array_, i_, c1);

  // left: a[i] = 1
  HInstruction* store1 = MakeArraySet(left, array_, i_, c1);

  // right: a[i+1] = 1
  HInstruction* store2 = MakeArraySet(right, array_, i_add1_, c1);

  // down: a[i] = 1
  HInstruction* store3 = MakeArraySet(down, array_, i_, c1);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
  ASSERT_TRUE(IsRemoved(store3));
}

// Check that redundant VStore/VLoad are removed from a SIMD loop.
//
//  LOOP BODY
//     vstore1: a[i,... i + 3] = [1,...1]
//     vload:   x = a[i,... i + 3]
//     vstore2: b[i,... i + 3] = x
//     vstore3: a[i,... i + 3] = [1,...1]
//
// Return 'a' from the method to make it escape.
//
// Expected:
//   'vstore1' is not removed.
//   'vload' is removed.
//   'vstore2' is removed because 'b' does not escape.
//   'vstore3' is removed.
TEST_F(LoadStoreEliminationTest, RedundantVStoreVLoadInLoop) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  ASSERT_TRUE(return_block_->GetLastInstruction()->IsReturnVoid());
  HInstruction* ret = new (GetAllocator()) HReturn(array_a);
  return_block_->ReplaceAndRemoveInstructionWith(return_block_->GetLastInstruction(), ret);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    a[i,... i + 3] = [1,...1]
  //    x = a[i,... i + 3]
  //    b[i,... i + 3] = x
  //    a[i,... i + 3] = [1,...1]
  HInstruction* vstore1 = AddVecStore(loop_, array_a, phi_);
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vstore2 = AddVecStore(loop_, array_b, phi_, vload);
  HInstruction* vstore3 = AddVecStore(loop_, array_a, phi_, vstore1->InputAt(2));

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_TRUE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(vstore2));
  ASSERT_TRUE(IsRemoved(vstore3));
}

// Loop writes invalidate only possibly aliased heap locations.
TEST_F(LoadStoreEliminationTest, StoreAfterLoopWithSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c128 = graph_->GetIntConstant(128);

  // array[0] = 2;
  // loop:
  //   b[i] = array[i]
  // array[0] = 2
  HInstruction* store1 = MakeArraySet(entry_block_, array_, c0, c2);

  HInstruction* array_b = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_b, pre_header_->GetLastInstruction());
  array_b->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  HInstruction* load = MakeArrayGet(loop_, array_, phi_, DataType::Type::kInt32);
  HInstruction* store2 = MakeArraySet(loop_, array_b, phi_, load);

  HInstruction* store3 = MakeArraySet(return_block_, array_, c0, c2);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(store1));
  ASSERT_TRUE(IsRemoved(store2));
  ASSERT_TRUE(IsRemoved(store3));
}

// Loop writes invalidate only possibly aliased heap locations.
TEST_F(LoadStoreEliminationTest, StoreAfterLoopWithSideEffects2) {
  CreateTestControlFlowGraph();

  // Add another array parameter that may alias with `array_`.
  // Note: We're not adding it to the suspend check environment.
  HInstruction* array2 = MakeParam(DataType::Type::kInt32);

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c2 = graph_->GetIntConstant(2);

  // array[0] = 2;
  // loop:
  //   array2[i] = array[i]
  // array[0] = 2
  HInstruction* store1 = MakeArraySet(pre_header_, array_, c0, c2);

  HInstruction* load = MakeArrayGet(loop_, array_, phi_, DataType::Type::kInt32);
  HInstruction* store2 = MakeArraySet(loop_, array2, phi_, load);

  HInstruction* store3 = MakeArraySet(return_block_, array_, c0, c2);

  PerformLSE();

  ASSERT_FALSE(IsRemoved(store1));
  ASSERT_FALSE(IsRemoved(store2));
  ASSERT_FALSE(IsRemoved(store3));
}

// As it is not allowed to use defaults for VecLoads, check if there is a new created array
// a VecLoad used in a loop and after it is not replaced with a default.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueInLoopWithoutWriteSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  // array[0,... 3] = v
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(vstore));
}

// As it is not allowed to use defaults for VecLoads, check if there is a new created array
// a VecLoad is not replaced with a default.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValue) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // array[0,... 3] = v
  HInstruction* vload = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_FALSE(IsRemoved(vstore));
}

// As it is allowed to use defaults for ordinary loads, check if there is a new created array
// a load used in a loop and after it is replaced with a default.
TEST_F(LoadStoreEliminationTest, LoadDefaultValueInLoopWithoutWriteSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i]
  // array[0] = v
  HInstruction* load = MakeArrayGet(loop_, array_a, phi_, DataType::Type::kInt32);
  HInstruction* store = MakeArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is allowed to use defaults for ordinary loads, check if there is a new created array
// a load is replaced with a default.
TEST_F(LoadStoreEliminationTest, LoadDefaultValue) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0]
  // array[0] = v
  HInstruction* load = MakeArrayGet(pre_header_, array_a, c0, DataType::Type::kInt32);
  HInstruction* store = MakeArraySet(return_block_, array_, c0, load);

  PerformLSE();

  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is not allowed to use defaults for VecLoads but allowed for regular loads,
// check if there is a new created array, a VecLoad and a load used in a loop and after it,
// VecLoad is not replaced with a default but the load is.
TEST_F(LoadStoreEliminationTest, VLoadAndLoadDefaultValueInLoopWithoutWriteSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  //    v1 = a[i]
  // array[0,... 3] = v
  // array[0] = v1
  HInstruction* vload = AddVecLoad(loop_, array_a, phi_);
  HInstruction* load = MakeArrayGet(loop_, array_a, phi_, DataType::Type::kInt32);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload);
  HInstruction* store = MakeArraySet(return_block_, array_, c0, load);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(vstore));
  ASSERT_FALSE(IsRemoved(store));
}

// As it is not allowed to use defaults for VecLoads but allowed for regular loads,
// check if there is a new created array, a VecLoad and a load,
// VecLoad is not replaced with a default but the load is.
TEST_F(LoadStoreEliminationTest, VLoadAndLoadDefaultValue) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // v1 = a[0]
  // array[0,... 3] = v
  // array[0] = v1
  HInstruction* vload = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* load = MakeArrayGet(pre_header_, array_a, c0, DataType::Type::kInt32);
  HInstruction* vstore = AddVecStore(return_block_, array_, c0, vload);
  HInstruction* store = MakeArraySet(return_block_, array_, c0, load);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload));
  ASSERT_TRUE(IsRemoved(load));
  ASSERT_FALSE(IsRemoved(vstore));
  ASSERT_FALSE(IsRemoved(store));
}

// It is not allowed to use defaults for VecLoads. However it should not prevent from removing
// loads getting the same value.
// Check a load getting a known value is eliminated (a loop test case).
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueAndVLoadInLoopWithoutWriteSideEffects) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // LOOP BODY:
  //    v = a[i,... i + 3]
  //    v1 = a[i,... i + 3]
  // array[0,... 3] = v
  // array[128,... 131] = v1
  HInstruction* vload1 = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vload2 = AddVecLoad(loop_, array_a, phi_);
  HInstruction* vstore1 = AddVecStore(return_block_, array_, c0, vload1);
  HInstruction* vstore2 = AddVecStore(return_block_, array_, c128, vload2);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_FALSE(IsRemoved(vstore2));
}

// It is not allowed to use defaults for VecLoads. However it should not prevent from removing
// loads getting the same value.
// Check a load getting a known value is eliminated.
TEST_F(LoadStoreEliminationTest, VLoadDefaultValueAndVLoad) {
  CreateTestControlFlowGraph();

  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c128 = graph_->GetIntConstant(128);

  HInstruction* array_a = new (GetAllocator()) HNewArray(c0, c128, 0, 0);
  pre_header_->InsertInstructionBefore(array_a, pre_header_->GetLastInstruction());
  array_a->CopyEnvironmentFrom(suspend_check_->GetEnvironment());

  // v = a[0,... 3]
  // v1 = a[0,... 3]
  // array[0,... 3] = v
  // array[128,... 131] = v1
  HInstruction* vload1 = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vload2 = AddVecLoad(pre_header_, array_a, c0);
  HInstruction* vstore1 = AddVecStore(return_block_, array_, c0, vload1);
  HInstruction* vstore2 = AddVecStore(return_block_, array_, c128, vload2);

  // TODO: enable LSE for graphs with predicated SIMD.
  graph_->SetHasTraditionalSIMD(true);
  PerformLSE();

  ASSERT_FALSE(IsRemoved(vload1));
  ASSERT_TRUE(IsRemoved(vload2));
  ASSERT_FALSE(IsRemoved(vstore1));
  ASSERT_FALSE(IsRemoved(vstore2));
}

// Object o = new Obj();
// // Needed because otherwise we short-circuit LSA since GVN would get almost
// // everything other than this. Also since this isn't expected to be a very
// // common pattern it's not worth changing the LSA logic.
// o.foo = 3;
// return o.shadow$_klass_;
TEST_F(LoadStoreEliminationTest, DefaultShadowClass) {
  HBasicBlock* main = InitEntryMainExitGraph();

  HInstruction* suspend_check = MakeSuspendCheck(entry_block_);

  HInstruction* cls = MakeLoadClass(main);
  HInstruction* new_inst = MakeNewInstance(main, cls);
  HInstruction* const_fence = new (GetAllocator()) HConstructorFence(new_inst, 0, GetAllocator());
  main->AddInstruction(const_fence);
  HInstruction* set_field =
      MakeIFieldSet(main, new_inst, graph_->GetIntConstant(33), MemberOffset(32));
  HInstruction* get_field =
      MakeIFieldGet(main, new_inst, DataType::Type::kReference, mirror::Object::ClassOffset());
  HReturn* return_val = MakeReturn(main, get_field);

  PerformLSE();

  EXPECT_INS_REMOVED(new_inst);
  EXPECT_INS_REMOVED(const_fence);
  EXPECT_INS_REMOVED(get_field);
  EXPECT_INS_REMOVED(set_field);
  EXPECT_INS_RETAINED(cls);
  EXPECT_INS_EQ(cls, return_val->InputAt(0));
}

// Object o = new Obj();
// // Needed because otherwise we short-circuit LSA since GVN would get almost
// // everything other than this. Also since this isn't expected to be a very
// // common pattern (only a single java function, Object.identityHashCode,
// // ever reads this field) it's not worth changing the LSA logic.
// o.foo = 3;
// return o.shadow$_monitor_;
TEST_F(LoadStoreEliminationTest, DefaultShadowMonitor) {
  HBasicBlock* main = InitEntryMainExitGraph();

  HInstruction* suspend_check = MakeSuspendCheck(entry_block_);

  HInstruction* cls = MakeLoadClass(main);
  HInstruction* new_inst = MakeNewInstance(main, cls);
  HInstruction* const_fence = new (GetAllocator()) HConstructorFence(new_inst, 0, GetAllocator());
  main->AddInstruction(const_fence);
  HInstruction* set_field =
      MakeIFieldSet(main, new_inst, graph_->GetIntConstant(33), MemberOffset(32));
  HInstruction* get_field =
      MakeIFieldGet(main, new_inst, DataType::Type::kInt32, mirror::Object::MonitorOffset());
  HReturn* return_val = MakeReturn(main, get_field);

  PerformLSE();

  EXPECT_INS_REMOVED(new_inst);
  EXPECT_INS_REMOVED(const_fence);
  EXPECT_INS_REMOVED(get_field);
  EXPECT_INS_REMOVED(set_field);
  EXPECT_INS_RETAINED(cls);
  EXPECT_INS_EQ(graph_->GetIntConstant(0), return_val->InputAt(0));
}

// void DO_CAL() {
//   int i = 1;
//   int[] w = new int[80];
//   int t = 0;
//   while (i < 80) {
//     w[i] = PLEASE_INTERLEAVE(w[i - 1], 1)
//     t = PLEASE_SELECT(w[i], t);
//     i++;
//   }
//   return t;
// }
TEST_F(LoadStoreEliminationTest, ArrayLoopOverlap) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);
  auto [preheader, loop, body] = CreateWhileLoop(ret);

  HInstruction* zero_const = graph_->GetIntConstant(0);
  HInstruction* one_const = graph_->GetIntConstant(1);
  HInstruction* eighty_const = graph_->GetIntConstant(80);

  // preheader
  HInstruction* alloc_w = MakeNewArray(preheader, zero_const, eighty_const);

  // loop-start
  auto [i_phi, i_add] = MakeLinearLoopVar(loop, body, one_const, one_const);
  HPhi* t_phi = MakePhi(loop, {zero_const, /* placeholder */ zero_const});
  std::initializer_list<HInstruction*> common_env{alloc_w, i_phi, t_phi};
  HInstruction* suspend = MakeSuspendCheck(loop, common_env);
  HInstruction* i_cmp_top = MakeCondition(loop, kCondGE, i_phi, eighty_const);
  HIf* loop_if = MakeIf(loop, i_cmp_top);
  CHECK(loop_if->IfTrueSuccessor() == ret);

  // BODY
  HInstruction* last_i = MakeBinOp<HSub>(body, DataType::Type::kInt32, i_phi, one_const);
  HInstruction* last_get = MakeArrayGet(body, alloc_w, last_i, DataType::Type::kInt32);
  HInvoke* body_value =
      MakeInvokeStatic(body, DataType::Type::kInt32, { last_get, one_const }, common_env);
  HInstruction* body_set = MakeArraySet(body, alloc_w, i_phi, body_value, DataType::Type::kInt32);
  HInstruction* body_get = MakeArrayGet(body, alloc_w, i_phi, DataType::Type::kInt32);
  HInvoke* t_next = MakeInvokeStatic(body, DataType::Type::kInt32, { body_get, t_phi }, common_env);

  t_phi->ReplaceInput(t_next, 1u);  // Update back-edge input.

  // ret
  MakeReturn(ret, t_phi);

  PerformLSE();

  // TODO Technically this is optimizable. LSE just needs to add phis to keep
  // track of the last `N` values set where `N` is how many locations we can go
  // back into the array.
  if (IsRemoved(last_get)) {
    // If we were able to remove the previous read the entire array should be removable.
    EXPECT_INS_REMOVED(body_set);
    EXPECT_INS_REMOVED(alloc_w);
  } else {
    // This is the branch we actually take for now. If we rely on being able to
    // read the array we'd better remember to write to it as well.
    EXPECT_INS_RETAINED(body_set);
  }
  // The last 'get' should always be removable.
  EXPECT_INS_REMOVED(body_get);
}

// void DO_CAL2() {
//   int i = 1;
//   int[] w = new int[80];
//   int t = 0;
//   while (i < 80) {
//     w[i] = PLEASE_INTERLEAVE(w[i - 1], 1) // <-- removed
//     t = PLEASE_SELECT(w[i], t);
//     w[i] = PLEASE_INTERLEAVE(w[i - 1], 1) // <-- removed
//     t = PLEASE_SELECT(w[i], t);
//     w[i] = PLEASE_INTERLEAVE(w[i - 1], 1) // <-- kept
//     t = PLEASE_SELECT(w[i], t);
//     i++;
//   }
//   return t;
// }
TEST_F(LoadStoreEliminationTest, ArrayLoopOverlap2) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);
  auto [preheader, loop, body] = CreateWhileLoop(ret);

  HInstruction* zero_const = graph_->GetIntConstant(0);
  HInstruction* one_const = graph_->GetIntConstant(1);
  HInstruction* eighty_const = graph_->GetIntConstant(80);

  // preheader
  HInstruction* alloc_w = MakeNewArray(preheader, zero_const, eighty_const);

  // loop-start
  auto [i_phi, i_add] = MakeLinearLoopVar(loop, body, one_const, one_const);
  HPhi* t_phi = MakePhi(loop, {zero_const, /* placeholder */ zero_const});
  std::initializer_list<HInstruction*> common_env{alloc_w, i_phi, t_phi};
  HInstruction* suspend = MakeSuspendCheck(loop, common_env);
  HInstruction* i_cmp_top = MakeCondition(loop, kCondGE, i_phi, eighty_const);
  HIf* loop_if = MakeIf(loop, i_cmp_top);
  CHECK(loop_if->IfTrueSuccessor() == ret);

  // BODY
  HInstruction* last_i = MakeBinOp<HSub>(body, DataType::Type::kInt32, i_phi, one_const);
  auto make_instructions = [&](HInstruction* last_t_value) {
    HInstruction* last_get = MakeArrayGet(body, alloc_w, last_i, DataType::Type::kInt32);
    HInvoke* body_value =
        MakeInvokeStatic(body, DataType::Type::kInt32, { last_get, one_const }, common_env);
    HInstruction* body_set = MakeArraySet(body, alloc_w, i_phi, body_value, DataType::Type::kInt32);
    HInstruction* body_get = MakeArrayGet(body, alloc_w, i_phi, DataType::Type::kInt32);
    HInvoke* t_next =
        MakeInvokeStatic(body, DataType::Type::kInt32, { body_get, last_t_value }, common_env);
    return std::make_tuple(last_get, body_value, body_set, body_get, t_next);
  };
  auto [last_get_1, body_value_1, body_set_1, body_get_1, t_next_1] = make_instructions(t_phi);
  auto [last_get_2, body_value_2, body_set_2, body_get_2, t_next_2] = make_instructions(t_next_1);
  auto [last_get_3, body_value_3, body_set_3, body_get_3, t_next_3] = make_instructions(t_next_2);

  t_phi->ReplaceInput(t_next_3, 1u);  // Update back-edge input.

  // ret
  MakeReturn(ret, t_phi);

  PerformLSE();

  // TODO Technically this is optimizable. LSE just needs to add phis to keep
  // track of the last `N` values set where `N` is how many locations we can go
  // back into the array.
  if (IsRemoved(last_get_1)) {
    // If we were able to remove the previous read the entire array should be removable.
    EXPECT_INS_REMOVED(body_set_1);
    EXPECT_INS_REMOVED(body_set_2);
    EXPECT_INS_REMOVED(body_set_3);
    EXPECT_INS_REMOVED(last_get_1);
    EXPECT_INS_REMOVED(last_get_2);
    EXPECT_INS_REMOVED(alloc_w);
  } else {
    // This is the branch we actually take for now. If we rely on being able to
    // read the array we'd better remember to write to it as well.
    EXPECT_INS_RETAINED(body_set_3);
  }
  // The last 'get' should always be removable.
  EXPECT_INS_REMOVED(body_get_1);
  EXPECT_INS_REMOVED(body_get_2);
  EXPECT_INS_REMOVED(body_get_3);
  // shadowed writes should always be removed
  EXPECT_INS_REMOVED(body_set_1);
  EXPECT_INS_REMOVED(body_set_2);
}

TEST_F(LoadStoreEliminationTest, ArrayNonLoopPhi) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);

  HInstruction* param = MakeParam(DataType::Type::kBool);
  HInstruction* zero_const = graph_->GetIntConstant(0);
  HInstruction* one_const = graph_->GetIntConstant(1);
  HInstruction* two_const = graph_->GetIntConstant(2);

  auto [start, left, right] = CreateDiamondPattern(ret, param);

  // start
  HInstruction* alloc_w = MakeNewArray(start, zero_const, two_const);

  // left
  HInvoke* left_value =
      MakeInvokeStatic(left, DataType::Type::kInt32, { zero_const }, /*env=*/ { alloc_w });
  HInstruction* left_set_1 =
      MakeArraySet(left, alloc_w, zero_const, left_value, DataType::Type::kInt32);
  HInstruction* left_set_2 =
      MakeArraySet(left, alloc_w, one_const, zero_const, DataType::Type::kInt32);

  // right
  HInvoke* right_value =
      MakeInvokeStatic(right, DataType::Type::kInt32, { one_const }, /*env=*/ { alloc_w });
  HInstruction* right_set_1 =
      MakeArraySet(right, alloc_w, zero_const, right_value, DataType::Type::kInt32);
  HInstruction* right_set_2 =
      MakeArraySet(right, alloc_w, one_const, zero_const, DataType::Type::kInt32);

  // ret
  HInstruction* read_1 = MakeArrayGet(ret, alloc_w, zero_const, DataType::Type::kInt32);
  HInstruction* read_2 = MakeArrayGet(ret, alloc_w, one_const, DataType::Type::kInt32);
  HInstruction* add = MakeBinOp<HAdd>(ret, DataType::Type::kInt32, read_1, read_2);
  MakeReturn(ret, add);

  PerformLSE();

  EXPECT_INS_REMOVED(read_1);
  EXPECT_INS_REMOVED(read_2);
  EXPECT_INS_REMOVED(left_set_1);
  EXPECT_INS_REMOVED(left_set_2);
  EXPECT_INS_REMOVED(right_set_1);
  EXPECT_INS_REMOVED(right_set_2);
  EXPECT_INS_REMOVED(alloc_w);

  EXPECT_INS_RETAINED(left_value);
  EXPECT_INS_RETAINED(right_value);
}

TEST_F(LoadStoreEliminationTest, ArrayMergeDefault) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);

  HInstruction* param = MakeParam(DataType::Type::kBool);
  HInstruction* zero_const = graph_->GetIntConstant(0);
  HInstruction* one_const = graph_->GetIntConstant(1);
  HInstruction* two_const = graph_->GetIntConstant(2);

  auto [start, left, right] = CreateDiamondPattern(ret, param);

  // start
  HInstruction* alloc_w = MakeNewArray(start, zero_const, two_const);
  ArenaVector<HInstruction*> alloc_locals({}, GetAllocator()->Adapter(kArenaAllocInstruction));

  // left
  HInstruction* left_set_1 =
      MakeArraySet(left, alloc_w, zero_const, one_const, DataType::Type::kInt32);
  HInstruction* left_set_2 =
      MakeArraySet(left, alloc_w, zero_const, zero_const, DataType::Type::kInt32);

  // right
  HInstruction* right_set_1 =
      MakeArraySet(right, alloc_w, one_const, one_const, DataType::Type::kInt32);
  HInstruction* right_set_2 =
      MakeArraySet(right, alloc_w, one_const, zero_const, DataType::Type::kInt32);

  // ret
  HInstruction* read_1 = MakeArrayGet(ret, alloc_w, zero_const, DataType::Type::kInt32);
  HInstruction* read_2 = MakeArrayGet(ret, alloc_w, one_const, DataType::Type::kInt32);
  HInstruction* add = MakeBinOp<HAdd>(ret, DataType::Type::kInt32, read_1, read_2);
  MakeReturn(ret, add);

  PerformLSE();

  EXPECT_INS_REMOVED(read_1);
  EXPECT_INS_REMOVED(read_2);
  EXPECT_INS_REMOVED(left_set_1);
  EXPECT_INS_REMOVED(left_set_2);
  EXPECT_INS_REMOVED(right_set_1);
  EXPECT_INS_REMOVED(right_set_2);
  EXPECT_INS_REMOVED(alloc_w);
}

// Regression test for b/187487955.
// We previusly failed to consider aliasing between an array location
// with index `idx` defined in the loop (such as a loop Phi) and another
// array location with index `idx + constant`. This could have led to
// replacing the load with, for example, the default value 0.
TEST_F(LoadStoreEliminationTest, ArrayLoopAliasing1) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);
  auto [preheader, loop, body] = CreateWhileLoop(ret);
  loop->SwapSuccessors();  // Move the loop exit to the "else" successor.

  HInstruction* n = MakeParam(DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);

  // preheader
  HInstruction* cls = MakeLoadClass(preheader);
  HInstruction* array = MakeNewArray(preheader, cls, n);

  // loop
  auto [i_phi, i_add] = MakeLinearLoopVar(loop, body, c0, c1);
  HInstruction* loop_suspend_check = MakeSuspendCheck(loop);
  HInstruction* loop_cond = MakeCondition(loop, kCondLT, i_phi, n);
  HIf* loop_if = MakeIf(loop, loop_cond);
  CHECK(loop_if->IfTrueSuccessor() == body);

  // body
  HInstruction* body_set = MakeArraySet(body, array, i_phi, i_phi, DataType::Type::kInt32);

  // ret
  HInstruction* ret_sub = MakeBinOp<HSub>(ret, DataType::Type::kInt32, i_phi, c1);
  HInstruction* ret_get = MakeArrayGet(ret, array, ret_sub, DataType::Type::kInt32);
  MakeReturn(ret, ret_get);

  PerformLSE();

  EXPECT_INS_RETAINED(cls);
  EXPECT_INS_RETAINED(array);
  EXPECT_INS_RETAINED(body_set);
  EXPECT_INS_RETAINED(ret_get);
}

// Regression test for b/187487955.
// Similar to the `ArrayLoopAliasing1` test above but with additional load
// that marks a loop Phi placeholder as kept which used to trigger a DCHECK().
// There is also an LSE run-test for this but it relies on BCE eliminating
// BoundsCheck instructions and adds extra code in loop body to avoid
// loop unrolling. This gtest does not need to jump through those hoops
// as we do not unnecessarily run those optimization passes.
TEST_F(LoadStoreEliminationTest, ArrayLoopAliasing2) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);
  auto [preheader, loop, body] = CreateWhileLoop(ret);
  loop->SwapSuccessors();  // Move the loop exit to the "else" successor.

  HInstruction* n = MakeParam(DataType::Type::kInt32);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);

  // preheader
  HInstruction* cls = MakeLoadClass(preheader);
  HInstruction* array = MakeNewArray(preheader, cls, n);

  // loop
  auto [i_phi, i_add] = MakeLinearLoopVar(loop, body, c0, c1);
  HInstruction* loop_suspend_check = MakeSuspendCheck(loop);
  HInstruction* loop_cond = MakeCondition(loop, kCondLT, i_phi, n);
  HIf* loop_if = MakeIf(loop, loop_cond);
  CHECK(loop_if->IfTrueSuccessor() == body);

  // body
  HInstruction* body_set = MakeArraySet(body, array, i_phi, i_phi, DataType::Type::kInt32);

  // ret
  HInstruction* ret_sub = MakeBinOp<HSub>(ret, DataType::Type::kInt32, i_phi, c1);
  HInstruction* ret_get1 = MakeArrayGet(ret, array, ret_sub, DataType::Type::kInt32);
  HInstruction* ret_get2 = MakeArrayGet(ret, array, i_phi, DataType::Type::kInt32);
  HInstruction* ret_add = MakeBinOp<HAdd>(ret, DataType::Type::kInt32, ret_get1, ret_get2);
  MakeReturn(ret, ret_add);

  PerformLSE();

  EXPECT_INS_RETAINED(cls);
  EXPECT_INS_RETAINED(array);
  EXPECT_INS_RETAINED(body_set);
  EXPECT_INS_RETAINED(ret_get1);
  EXPECT_INS_RETAINED(ret_get2);
}

class TwoTypesConversionsTestGroup : public LoadStoreEliminationTestBase<
    CommonCompilerTestWithParam<std::tuple<DataType::Type, DataType::Type>>> {
 protected:
  DataType::Type FieldTypeForLoadType(DataType::Type load_type) {
    // `Uint8` is not a valid field type but it's a valid load type we can set for
    // a `HInstanceFieldGet` after constructing it.
    return (load_type == DataType::Type::kUint8) ? DataType::Type::kInt8 : load_type;
  }
};

TEST_P(TwoTypesConversionsTestGroup, StoreLoad) {
  auto [param_type, load_type] = GetParam();
  DataType::Type field_type = FieldTypeForLoadType(load_type);

  HBasicBlock* main = InitEntryMainExitGraph();
  HInstruction* param = MakeParam(param_type);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  HInstruction* write = MakeIFieldSet(main, object, param, field_type, MemberOffset(32));
  HInstanceFieldGet* read = MakeIFieldGet(main, object, field_type, MemberOffset(32));
  read->SetType(load_type);
  HInstruction* ret = MakeReturn(main, read);

  PerformLSE();

  EXPECT_INS_RETAINED(write);
  EXPECT_INS_REMOVED(read);

  HInstruction* ret_input = ret->InputAt(0);
  if (DataType::IsTypeConversionImplicit(param_type, load_type)) {
    ASSERT_EQ(param, ret_input) << ret_input->DebugName();
  } else {
    ASSERT_TRUE(ret_input->IsTypeConversion()) << ret_input->DebugName();
    ASSERT_EQ(load_type, ret_input->GetType());
    ASSERT_EQ(param, ret_input->InputAt(0)) << ret_input->InputAt(0)->DebugName();
  }
}

TEST_P(TwoTypesConversionsTestGroup, StoreLoadStoreLoad) {
  auto [load_type1, load_type2] = GetParam();
  DataType::Type field_type1 = FieldTypeForLoadType(load_type1);
  DataType::Type field_type2 = FieldTypeForLoadType(load_type2);

  HBasicBlock* main = InitEntryMainExitGraph();
  HInstruction* param = MakeParam(DataType::Type::kInt32);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  HInstruction* write1 = MakeIFieldSet(main, object, param, field_type1, MemberOffset(32));
  HInstanceFieldGet* read1 = MakeIFieldGet(main, object, field_type1, MemberOffset(32));
  read1->SetType(load_type1);
  HInstruction* write2 = MakeIFieldSet(main, object, read1, field_type2, MemberOffset(40));
  HInstanceFieldGet* read2 = MakeIFieldGet(main, object, field_type2, MemberOffset(40));
  read2->SetType(load_type2);
  HInstruction* ret = MakeReturn(main, read2);

  PerformLSE();

  EXPECT_INS_RETAINED(write1);
  EXPECT_INS_RETAINED(write2);
  EXPECT_INS_REMOVED(read1);
  EXPECT_INS_REMOVED(read2);

  // Note: Sometimes we create two type conversions when one is enough (Int32->Int16->Int8).
  // We currently rely on the instruction simplifier to remove the intermediate conversion.
  HInstruction* current = ret->InputAt(0);
  if (!DataType::IsTypeConversionImplicit(load_type1, load_type2)) {
    ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
    ASSERT_EQ(load_type2, current->GetType());
    current = current->InputAt(0);
  }
  if (!DataType::IsTypeConversionImplicit(DataType::Type::kInt32, load_type1)) {
    ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
    ASSERT_EQ(load_type1, current->GetType());
    current = current->InputAt(0);
  }
  ASSERT_EQ(param, current) << current->DebugName();
}

TEST_P(TwoTypesConversionsTestGroup, DefaultValueStores_LoadAfterLoop) {
  auto [default_load_type, load_type] = GetParam();
  DataType::Type default_field_type = FieldTypeForLoadType(default_load_type);
  DataType::Type field_type = FieldTypeForLoadType(load_type);

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop] = CreateDoWhileLoopWithInstructions(return_block);

  HInstruction* object = MakeParam(DataType::Type::kReference);
  HInstruction* cls = MakeLoadClass(pre_header);
  HInstruction* default_object = MakeNewInstance(pre_header, cls);
  HInstanceFieldGet* default_value =
      MakeIFieldGet(pre_header, default_object, default_field_type, MemberOffset(40));
  default_value->SetType(default_load_type);
  // Make the `default_object` escape to avoid write elimination (test only load elimination).
  HInstruction* invoke = MakeInvokeStatic(return_block, DataType::Type::kVoid, {default_object});

  HInstruction* write =
      MakeIFieldSet(return_block, object, default_value, field_type, MemberOffset(32));
  HInstanceFieldGet* read = MakeIFieldGet(return_block, object, field_type, MemberOffset(32));
  read->SetType(load_type);
  HInstruction* ret = MakeReturn(return_block, read);

  PerformLSE();

  EXPECT_INS_RETAINED(default_object);
  EXPECT_INS_REMOVED(default_value);
  EXPECT_INS_RETAINED(write);
  EXPECT_INS_REMOVED(read);

  HInstruction* ret_input = ret->InputAt(0);
  ASSERT_TRUE(ret_input->IsIntConstant()) << ret_input->DebugName();
  ASSERT_EQ(ret_input->AsIntConstant()->GetValue(), 0);
}

TEST_P(TwoTypesConversionsTestGroup, SingleValueStores_LoadAfterLoop) {
  auto [param_type, load_type] = GetParam();
  DataType::Type field_type = FieldTypeForLoadType(load_type);

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop_header, loop_body] = CreateForLoopWithInstructions(return_block);

  HInstruction* param = MakeParam(param_type);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  // Write the value in pre-header.
  HInstruction* write1 =
      MakeIFieldSet(pre_header, object, param, field_type, MemberOffset(32));

  // In the body, make a call to clobber all fields, then write the same value as in pre-header.
  MakeInvokeStatic(loop_body, DataType::Type::kVoid, {object});
  HInstruction* write2 =
      MakeIFieldSet(loop_body, object, param, field_type, MemberOffset(32));

  HInstanceFieldGet* read = MakeIFieldGet(return_block, object, field_type, MemberOffset(32));
  read->SetType(load_type);
  HInstruction* ret = MakeReturn(return_block, read);

  PerformLSE();

  EXPECT_INS_RETAINED(write1);
  EXPECT_INS_RETAINED(write2);
  EXPECT_INS_REMOVED(read);

  HInstruction* ret_input = ret->InputAt(0);
  if (DataType::IsTypeConversionImplicit(param_type, load_type)) {
    ASSERT_EQ(param, ret_input) << ret_input->DebugName();
  } else {
    ASSERT_TRUE(ret_input->IsTypeConversion()) << ret_input->DebugName();
    ASSERT_EQ(load_type, ret_input->GetType());
    ASSERT_EQ(param, ret_input->InputAt(0)) << ret_input->InputAt(0)->DebugName();
  }
}

TEST_P(TwoTypesConversionsTestGroup, StoreLoopLoad) {
  auto [param_type, load_type] = GetParam();
  DataType::Type field_type = FieldTypeForLoadType(load_type);

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop] = CreateDoWhileLoopWithInstructions(return_block);

  HInstruction* param = MakeParam(param_type);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  HInstruction* write = MakeIFieldSet(pre_header, object, param, field_type, MemberOffset(32));

  HInstanceFieldGet* read = MakeIFieldGet(return_block, object, field_type, MemberOffset(32));
  read->SetType(load_type);
  HInstruction* ret = MakeReturn(return_block, read);

  PerformLSE();

  EXPECT_INS_RETAINED(write);
  EXPECT_INS_REMOVED(read);

  HInstruction* ret_input = ret->InputAt(0);
  if (DataType::IsTypeConversionImplicit(param_type, load_type)) {
    ASSERT_EQ(param, ret_input) << ret_input->DebugName();
  } else {
    ASSERT_TRUE(ret_input->IsTypeConversion()) << ret_input->DebugName();
    ASSERT_EQ(load_type, ret_input->GetType());
    ASSERT_EQ(param, ret_input->InputAt(0)) << ret_input->InputAt(0)->DebugName();
  }
}

TEST_P(TwoTypesConversionsTestGroup, StoreLoopLoadStoreLoad) {
  auto [load_type1, load_type2] = GetParam();
  DataType::Type field_type1 = FieldTypeForLoadType(load_type1);
  DataType::Type field_type2 = FieldTypeForLoadType(load_type2);

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop] = CreateDoWhileLoopWithInstructions(return_block);
  HInstruction* param = MakeParam(DataType::Type::kInt32);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  HInstruction* write1 = MakeIFieldSet(pre_header, object, param, field_type1, MemberOffset(32));

  HInstanceFieldGet* read1 = MakeIFieldGet(return_block, object, field_type1, MemberOffset(32));
  read1->SetType(load_type1);
  HInstruction* write2 = MakeIFieldSet(return_block, object, read1, field_type2, MemberOffset(40));
  HInstanceFieldGet* read2 = MakeIFieldGet(return_block, object, field_type2, MemberOffset(40));
  read2->SetType(load_type2);
  HInstruction* ret = MakeReturn(return_block, read2);

  PerformLSE();

  EXPECT_INS_RETAINED(write1);
  EXPECT_INS_RETAINED(write2);
  EXPECT_INS_REMOVED(read1);
  EXPECT_INS_REMOVED(read2);

  // Note: If the `load_type2` is not larger than the `load_type1`, we avoid
  // the intermediate conversion and use `param` directly for the second load.
  DataType::Type read2_input_type = DataType::Size(load_type2) <= DataType::Size(load_type1)
      ? DataType::Type::kInt32
      : load_type1;
  HInstruction* current = ret->InputAt(0);
  if (!DataType::IsTypeConversionImplicit(read2_input_type, load_type2)) {
    ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
    ASSERT_EQ(load_type2, current->GetType());
    current = current->InputAt(0);
  }
  if (!DataType::IsTypeConversionImplicit(DataType::Type::kInt32, read2_input_type)) {
    ASSERT_EQ(read2_input_type, load_type1);
    ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
    ASSERT_EQ(load_type1, current->GetType()) << load_type2;
    current = current->InputAt(0);
  }
  ASSERT_EQ(param, current) << current->DebugName();
}

TEST_P(TwoTypesConversionsTestGroup, MergingConvertedValueStore) {
  auto [param_type, load_type] = GetParam();
  DataType::Type field_type = FieldTypeForLoadType(load_type);
  DataType::Type phi_field_type = DataType::Type::kInt32;  // "phi field" can hold the full value.
  CHECK(DataType::IsTypeConversionImplicit(param_type, phi_field_type));
  CHECK(DataType::IsTypeConversionImplicit(load_type, phi_field_type));

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop_header, loop_body] = CreateForLoopWithInstructions(return_block);

  HInstruction* param = MakeParam(param_type);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  // Initialize the "phi field".
  HInstruction* pre_header_write =
      MakeIFieldSet(pre_header, object, param, phi_field_type, MemberOffset(40));

  // In the body, we read the "phi field", store and load the value to a different field
  // to force type conversion, and store back to the "phi field".
  HInstanceFieldGet* phi_read = MakeIFieldGet(loop_body, object, phi_field_type, MemberOffset(40));
  HInstruction* conversion_write =
      MakeIFieldSet(loop_body, object, phi_read, field_type, MemberOffset(32));
  HInstanceFieldGet* conversion_read =
      MakeIFieldGet(loop_body, object, field_type, MemberOffset(32));
  conversion_read->SetType(load_type);
  HInstruction* phi_write =
      MakeIFieldSet(loop_body, object, conversion_read, phi_field_type, MemberOffset(40));

  HInstanceFieldGet* final_read =
      MakeIFieldGet(return_block, object, phi_field_type, MemberOffset(40));
  HInstruction* ret = MakeReturn(return_block, final_read);

  PerformLSE();

  EXPECT_INS_RETAINED(pre_header_write);
  EXPECT_INS_RETAINED(conversion_write);
  EXPECT_INS_REMOVED(phi_read);
  EXPECT_INS_REMOVED(conversion_read);
  EXPECT_INS_REMOVED(final_read);

  HInstruction* ret_input = ret->InputAt(0);
  if (DataType::IsTypeConversionImplicit(param_type, load_type)) {
    EXPECT_INS_REMOVED(phi_write) << "\n" << param_type << "/" << load_type;
    ASSERT_EQ(param, ret_input) << ret_input->DebugName();
  } else {
    EXPECT_INS_RETAINED(phi_write) << "\n" << param_type << "/" << load_type;
    ASSERT_TRUE(ret_input->IsPhi()) << ret_input->DebugName();
    HInstruction* pre_header_input = ret_input->InputAt(0);
    HInstruction* loop_body_input = ret_input->InputAt(1);
    ASSERT_EQ(param, pre_header_input) << pre_header_input->DebugName();
    ASSERT_TRUE(loop_body_input->IsTypeConversion());
    ASSERT_EQ(load_type, loop_body_input->GetType());
    ASSERT_EQ(ret_input, loop_body_input->InputAt(0));
  }
}

TEST_P(TwoTypesConversionsTestGroup, MergingTwiceConvertedValueStore) {
  auto [load_type1, load_type2] = GetParam();
  DataType::Type field_type1 = FieldTypeForLoadType(load_type1);
  DataType::Type field_type2 = FieldTypeForLoadType(load_type2);
  DataType::Type phi_field_type = DataType::Type::kInt32;  // "phi field" can hold the full value.
  CHECK(DataType::IsTypeConversionImplicit(load_type1, phi_field_type));
  CHECK(DataType::IsTypeConversionImplicit(load_type2, phi_field_type));

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop_header, loop_body] = CreateForLoopWithInstructions(return_block);

  HInstruction* param = MakeParam(DataType::Type::kInt32);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  // Initialize the "phi field".
  HInstruction* pre_header_write =
      MakeIFieldSet(pre_header, object, param, phi_field_type, MemberOffset(40));

  // In the body, we read the "phi field", store and load the value to a different field
  // to force type conversion - twice, and store back to the "phi field".
  HInstanceFieldGet* phi_read = MakeIFieldGet(loop_body, object, phi_field_type, MemberOffset(40));
  HInstruction* conversion_write1 =
      MakeIFieldSet(loop_body, object, phi_read, field_type1, MemberOffset(32));
  HInstanceFieldGet* conversion_read1 =
      MakeIFieldGet(loop_body, object, field_type1, MemberOffset(32));
  conversion_read1->SetType(load_type1);
  HInstruction* conversion_write2 =
      MakeIFieldSet(loop_body, object, conversion_read1, field_type2, MemberOffset(36));
  HInstanceFieldGet* conversion_read2 =
      MakeIFieldGet(loop_body, object, field_type2, MemberOffset(36));
  conversion_read2->SetType(load_type2);
  HInstruction* phi_write =
      MakeIFieldSet(loop_body, object, conversion_read2, phi_field_type, MemberOffset(40));

  HInstanceFieldGet* final_read =
      MakeIFieldGet(return_block, object, phi_field_type, MemberOffset(40));
  HInstruction* ret = MakeReturn(return_block, final_read);

  PerformLSE();

  EXPECT_INS_RETAINED(pre_header_write);
  EXPECT_INS_RETAINED(conversion_write1);
  EXPECT_INS_RETAINED(conversion_write2);
  EXPECT_INS_REMOVED(phi_read);
  EXPECT_INS_REMOVED(conversion_read1);
  EXPECT_INS_REMOVED(conversion_read2);
  EXPECT_INS_REMOVED(final_read);

  HInstruction* ret_input = ret->InputAt(0);
  if (load_type1 == DataType::Type::kInt32 && load_type2 == DataType::Type::kInt32) {
    EXPECT_INS_REMOVED(phi_write) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_EQ(param, ret_input) << ret_input->DebugName();
  } else {
    EXPECT_INS_RETAINED(phi_write) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_TRUE(ret_input->IsPhi()) << ret_input->DebugName();
    HInstruction* pre_header_input = ret_input->InputAt(0);
    HInstruction* loop_body_input = ret_input->InputAt(1);
    ASSERT_EQ(param, pre_header_input) << pre_header_input->DebugName();
    ASSERT_TRUE(loop_body_input->IsTypeConversion());
    HInstruction* current = loop_body_input;
    // Note: If the `load_type2` is not larger than the `load_type1`, we avoid
    // the intermediate conversion and use Phi directly for the second load.
    DataType::Type read2_input_type = DataType::Size(load_type2) <= DataType::Size(load_type1)
        ? DataType::Type::kInt32
        : load_type1;
    if (!DataType::IsTypeConversionImplicit(read2_input_type, load_type2)) {
      ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
      ASSERT_EQ(load_type2, current->GetType());
      current = current->InputAt(0);
    }
    if (!DataType::IsTypeConversionImplicit(DataType::Type::kInt32, read2_input_type)) {
      ASSERT_EQ(read2_input_type, load_type1);
      ASSERT_TRUE(current->IsTypeConversion()) << current->DebugName();
      ASSERT_EQ(load_type1, current->GetType()) << load_type2;
      current = current->InputAt(0);
    }
    ASSERT_EQ(current, ret_input);
  }
}

TEST_P(TwoTypesConversionsTestGroup, MergingConvertedValueStorePhiDeduplication) {
  auto [load_type1, load_type2] = GetParam();
  DataType::Type field_type1 = FieldTypeForLoadType(load_type1);
  DataType::Type field_type2 = FieldTypeForLoadType(load_type2);
  DataType::Type phi_field_type = DataType::Type::kInt32;  // "phi field" can hold the full value.
  CHECK(DataType::IsTypeConversionImplicit(load_type1, phi_field_type));
  CHECK(DataType::IsTypeConversionImplicit(load_type2, phi_field_type));
  DataType::Type param_type = DataType::Type::kInt32;

  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [pre_header, loop_header, loop_body] = CreateForLoopWithInstructions(return_block);

  HInstruction* param = MakeParam(param_type);
  HInstruction* object = MakeParam(DataType::Type::kReference);

  // Initialize the two "phi fields".
  HInstruction* pre_header_write1 =
      MakeIFieldSet(pre_header, object, param, phi_field_type, MemberOffset(40));
  HInstruction* pre_header_write2 =
      MakeIFieldSet(pre_header, object, param, phi_field_type, MemberOffset(60));

  // In the body, we read the "phi fields", store and load the values to different fields
  // to force type conversion, and store back to the "phi fields".
  HInstanceFieldGet* phi_read1 = MakeIFieldGet(loop_body, object, phi_field_type, MemberOffset(40));
  HInstanceFieldGet* phi_read2 = MakeIFieldGet(loop_body, object, phi_field_type, MemberOffset(60));
  HInstruction* conversion_write1 =
      MakeIFieldSet(loop_body, object, phi_read1, field_type1, MemberOffset(32));
  HInstruction* conversion_write2 =
      MakeIFieldSet(loop_body, object, phi_read2, field_type2, MemberOffset(52));
  HInstanceFieldGet* conversion_read1 =
      MakeIFieldGet(loop_body, object, field_type1, MemberOffset(32));
  conversion_read1->SetType(load_type1);
  HInstanceFieldGet* conversion_read2 =
      MakeIFieldGet(loop_body, object, field_type2, MemberOffset(52));
  conversion_read2->SetType(load_type2);
  HInstruction* phi_write1 =
      MakeIFieldSet(loop_body, object, conversion_read1, phi_field_type, MemberOffset(40));
  HInstruction* phi_write2 =
      MakeIFieldSet(loop_body, object, conversion_read2, phi_field_type, MemberOffset(60));

  HInstanceFieldGet* final_read1 =
      MakeIFieldGet(return_block, object, phi_field_type, MemberOffset(40));
  HInstanceFieldGet* final_read2 =
      MakeIFieldGet(return_block, object, phi_field_type, MemberOffset(60));
  HAdd* add = MakeBinOp<HAdd>(return_block, DataType::Type::kInt32, final_read1, final_read2);
  HInstruction* ret = MakeReturn(return_block, add);

  PerformLSE();

  EXPECT_INS_RETAINED(pre_header_write1);
  EXPECT_INS_RETAINED(pre_header_write2);
  EXPECT_INS_RETAINED(conversion_write1);
  EXPECT_INS_RETAINED(conversion_write2);
  EXPECT_INS_REMOVED(phi_read1);
  EXPECT_INS_REMOVED(phi_read2);
  EXPECT_INS_REMOVED(conversion_read1);
  EXPECT_INS_REMOVED(conversion_read2);
  EXPECT_INS_REMOVED(final_read1);
  EXPECT_INS_REMOVED(final_read2);

  HInstruction* ret_input = ret->InputAt(0);
  ASSERT_EQ(add, ret_input) << ret_input->DebugName();
  HInstruction* add_lhs = add->InputAt(0);
  HInstruction* add_rhs = add->InputAt(1);
  if (load_type1 == load_type2) {
    ASSERT_EQ(add_lhs, add_rhs);
  } else {
    ASSERT_NE(add_lhs, add_rhs);
  }
  if (DataType::IsTypeConversionImplicit(param_type, load_type1)) {
    EXPECT_INS_REMOVED(phi_write1) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_EQ(param, add_lhs) << ret_input->DebugName();
  } else {
    EXPECT_INS_RETAINED(phi_write1) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_TRUE(add_lhs->IsPhi()) << add_lhs->DebugName();
    HInstruction* pre_header_input = add_lhs->InputAt(0);
    HInstruction* loop_body_input = add_lhs->InputAt(1);
    ASSERT_EQ(param, pre_header_input) << pre_header_input->DebugName();
    ASSERT_TRUE(loop_body_input->IsTypeConversion());
    ASSERT_EQ(load_type1, loop_body_input->GetType());
    ASSERT_EQ(add_lhs, loop_body_input->InputAt(0));
  }
  if (DataType::IsTypeConversionImplicit(param_type, load_type2)) {
    EXPECT_INS_REMOVED(phi_write2) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_EQ(param, add_rhs) << ret_input->DebugName();
  } else {
    EXPECT_INS_RETAINED(phi_write2) << "\n" << load_type1 << "/" << load_type2;
    ASSERT_TRUE(add_rhs->IsPhi()) << add_rhs->DebugName();
    HInstruction* pre_header_input = add_rhs->InputAt(0);
    HInstruction* loop_body_input = add_rhs->InputAt(1);
    ASSERT_EQ(param, pre_header_input) << pre_header_input->DebugName();
    ASSERT_TRUE(loop_body_input->IsTypeConversion());
    ASSERT_EQ(load_type2, loop_body_input->GetType());
    ASSERT_EQ(add_rhs, loop_body_input->InputAt(0));
  }
}

auto Int32AndSmallerTypesGenerator() {
  return ::testing::Values(DataType::Type::kInt32,
                           DataType::Type::kInt16,
                           DataType::Type::kInt8,
                           DataType::Type::kUint16,
                           DataType::Type::kUint8);
}

INSTANTIATE_TEST_SUITE_P(LoadStoreEliminationTest,
                         TwoTypesConversionsTestGroup,
                         ::testing::Combine(Int32AndSmallerTypesGenerator(),
                                            Int32AndSmallerTypesGenerator()));

// // ENTRY
// obj = new Obj();
// // ALL should be kept
// switch (parameter_value) {
//   case 1:
//     // Case1
//     obj.field = 1;
//     call_func(obj);
//     break;
//   case 2:
//     // Case2
//     obj.field = 2;
//     call_func(obj);
//     // We don't know what obj.field is now we aren't able to eliminate the read below!
//     break;
//   default:
//     // Case3
//     // TODO This only happens because of limitations on our LSE which is unable
//     //      to materialize co-dependent loop and non-loop phis.
//     // Ideally we'd want to generate
//     // P1 = PHI[3, loop_val]
//     // while (test()) {
//     //   if (test2()) { goto; } else { goto; }
//     //   loop_val = [P1, 5]
//     // }
//     // Currently we aren't able to unfortunately.
//     obj.field = 3;
//     while (test()) {
//       if (test2()) { } else { obj.field = 5; }
//     }
//     break;
// }
// EXIT
// return obj.field
TEST_F(LoadStoreEliminationTest, PartialUnknownMerge) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "bswitch"},
                                                  {"bswitch", "case1"},
                                                  {"bswitch", "case2"},
                                                  {"bswitch", "case3"},
                                                  {"case1", "breturn"},
                                                  {"case2", "breturn"},
                                                  {"case3", "loop_pre_header"},
                                                  {"loop_pre_header", "loop_header"},
                                                  {"loop_header", "loop_body"},
                                                  {"loop_body", "loop_if_left"},
                                                  {"loop_body", "loop_if_right"},
                                                  {"loop_if_left", "loop_end"},
                                                  {"loop_if_right", "loop_end"},
                                                  {"loop_end", "loop_header"},
                                                  {"loop_header", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(bswitch);
  GET_BLOCK(exit);
  GET_BLOCK(breturn);
  GET_BLOCK(case1);
  GET_BLOCK(case2);
  GET_BLOCK(case3);

  GET_BLOCK(loop_pre_header);
  GET_BLOCK(loop_header);
  GET_BLOCK(loop_body);
  GET_BLOCK(loop_if_left);
  GET_BLOCK(loop_if_right);
  GET_BLOCK(loop_end);
#undef GET_BLOCK
  HInstruction* switch_val = MakeParam(DataType::Type::kInt32);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* c5 = graph_->GetIntConstant(5);

  HInstruction* cls = MakeLoadClass(entry);
  HInstruction* new_inst = MakeNewInstance(entry, cls);
  MakeGoto(entry);

  HInstruction* switch_inst = new (GetAllocator()) HPackedSwitch(0, 2, switch_val);
  bswitch->AddInstruction(switch_inst);

  HInstruction* write_c1 = MakeIFieldSet(case1, new_inst, c1, MemberOffset(32));
  HInstruction* call_c1 = MakeInvokeStatic(case1, DataType::Type::kVoid, { new_inst });
  MakeGoto(case1);

  HInstruction* write_c2 = MakeIFieldSet(case2, new_inst, c2, MemberOffset(32));
  HInstruction* call_c2 = MakeInvokeStatic(case2, DataType::Type::kVoid, { new_inst });
  MakeGoto(case2);

  HInstruction* write_c3 = MakeIFieldSet(case3, new_inst, c3, MemberOffset(32));
  MakeGoto(case3);

  MakeGoto(loop_pre_header);

  HInstruction* suspend_check_header = MakeSuspendCheck(loop_header);
  HInstruction* call_loop_header = MakeInvokeStatic(loop_header, DataType::Type::kBool, {});
  MakeIf(loop_header, call_loop_header);

  HInstruction* call_loop_body = MakeInvokeStatic(loop_body, DataType::Type::kBool, {});
  MakeIf(loop_body, call_loop_body);

  MakeGoto(loop_if_left);

  HInstruction* write_loop_right = MakeIFieldSet(loop_if_right, new_inst, c5, MemberOffset(32));
  MakeGoto(loop_if_right);

  MakeGoto(loop_end);

  HInstruction* read_bottom =
      MakeIFieldGet(breturn, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(breturn, read_bottom);

  MakeExit(exit);

  PerformLSE(blks);

  EXPECT_INS_RETAINED(read_bottom);
  EXPECT_INS_RETAINED(write_c1);
  EXPECT_INS_RETAINED(write_c2);
  EXPECT_INS_RETAINED(write_c3);
  EXPECT_INS_RETAINED(write_loop_right);
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   obj.field = 1;
//   call_func(obj);
//   // We don't know what obj.field is now we aren't able to eliminate the read below!
// } else {
//   // DO NOT ELIMINATE
//   obj.field = 2;
//   // RIGHT
// }
// EXIT
// return obj.field
// This test runs with partial LSE disabled.
TEST_F(LoadStoreEliminationTest, PartialLoadPreserved) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);

  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);

  auto [start, left, right] = CreateDiamondPattern(ret, bool_value);

  HInstruction* cls = MakeLoadClass(start);
  HInstruction* new_inst = MakeNewInstance(start, cls);

  HInstruction* write_left = MakeIFieldSet(left, new_inst, c1, MemberOffset(32));
  HInstruction* call_left = MakeInvokeStatic(left, DataType::Type::kVoid, { new_inst });

  HInstruction* write_right = MakeIFieldSet(right, new_inst, c2, MemberOffset(32));

  HInstruction* read_bottom =
      MakeIFieldGet(ret, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(ret, read_bottom);

  PerformLSE();

  EXPECT_INS_RETAINED(read_bottom) << *read_bottom;
  EXPECT_INS_RETAINED(write_right) << *write_right;
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   obj.field = 1;
//   call_func(obj);
//   // We don't know what obj.field is now we aren't able to eliminate the read below!
// } else {
//   // DO NOT ELIMINATE
//   if (param2) {
//     obj.field = 2;
//   } else {
//     obj.field = 3;
//   }
//   // RIGHT
// }
// EXIT
// return obj.field
// NB This test is for non-partial LSE flow. Normally the obj.field writes will be removed
TEST_F(LoadStoreEliminationTest, PartialLoadPreserved2) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* ret = InitEntryMainExitGraph(&vshs);

  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* bool_value_2 = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  auto [start, left, right_end] = CreateDiamondPattern(ret, bool_value);
  auto [right_start, right_first, right_second] = CreateDiamondPattern(right_end, bool_value_2);

  HInstruction* cls = MakeLoadClass(start);
  HInstruction* new_inst = MakeNewInstance(start, cls);

  HInstruction* write_left = MakeIFieldSet(left, new_inst, c1, MemberOffset(32));
  HInstruction* call_left = MakeInvokeStatic(left, DataType::Type::kVoid, { new_inst });

  HInstruction* write_right_first = MakeIFieldSet(right_first, new_inst, c2, MemberOffset(32));

  HInstruction* write_right_second = MakeIFieldSet(right_second, new_inst, c3, MemberOffset(32));

  HInstruction* read_bottom =
      MakeIFieldGet(ret, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(ret, read_bottom);

  PerformLSE();

  EXPECT_INS_RETAINED(read_bottom);
  EXPECT_INS_RETAINED(write_right_first);
  EXPECT_INS_RETAINED(write_right_second);
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   // DO NOT ELIMINATE
//   obj.field = 1;
//   while (true) {
//     bool esc = escape(obj);
//     if (esc) break;
//     // DO NOT ELIMINATE
//     obj.field = 3;
//   }
// } else {
//   // RIGHT
//   // DO NOT ELIMINATE
//   obj.field = 2;
// }
// // DO NOT ELIMINATE
// return obj.field;
// EXIT
TEST_F(LoadStoreEliminationTest, PartialLoadPreserved3) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "entry_post"},
                                                  {"entry_post", "right"},
                                                  {"right", "return_block"},
                                                  {"entry_post", "left_pre"},
                                                  {"left_pre", "left_loop"},
                                                  {"left_loop", "left_loop_post"},
                                                  {"left_loop_post", "left_loop"},
                                                  {"left_loop", "return_block"},
                                                  {"return_block", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(entry_post);
  GET_BLOCK(exit);
  GET_BLOCK(return_block);
  GET_BLOCK(left_pre);
  GET_BLOCK(left_loop);
  GET_BLOCK(left_loop_post);
  GET_BLOCK(right);
#undef GET_BLOCK
  // Left-loops first successor is the break.
  if (left_loop->GetSuccessors()[0] != return_block) {
    left_loop->SwapSuccessors();
  }
  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  HInstruction* cls = MakeLoadClass(entry);
  HInstruction* new_inst = MakeNewInstance(entry, cls);
  MakeGoto(entry);

  MakeIf(entry_post, bool_value);

  HInstruction* write_left_pre = MakeIFieldSet(left_pre, new_inst, c1, MemberOffset(32));
  MakeGoto(left_pre);

  HInstruction* suspend_left_loop = MakeSuspendCheck(left_loop);
  HInstruction* call_left_loop = MakeInvokeStatic(left_loop, DataType::Type::kBool, {new_inst});
  MakeIf(left_loop, call_left_loop);

  HInstruction* write_left_loop = MakeIFieldSet(left_loop_post, new_inst, c3, MemberOffset(32));
  MakeGoto(left_loop_post);

  HInstruction* write_right = MakeIFieldSet(right, new_inst, c2, MemberOffset(32));
  MakeGoto(right);

  HInstruction* read_return =
      MakeIFieldGet(return_block, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(return_block, read_return);

  MakeExit(exit);

  PerformLSE(blks);

  EXPECT_INS_RETAINED(write_left_pre) << *write_left_pre;
  EXPECT_INS_RETAINED(read_return) << *read_return;
  EXPECT_INS_RETAINED(write_right) << *write_right;
  EXPECT_INS_RETAINED(write_left_loop) << *write_left_loop;
  EXPECT_INS_RETAINED(call_left_loop) << *call_left_loop;
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   // ELIMINATE (not visible since always overridden by obj.field = 3)
//   obj.field = 1;
//   while (true) {
//     bool stop = should_stop();
//     // DO NOT ELIMINATE (visible by read at end)
//     obj.field = 3;
//     if (stop) break;
//   }
// } else {
//   // RIGHT
//   // DO NOT ELIMINATE
//   obj.field = 2;
//   escape(obj);
// }
// // DO NOT ELIMINATE
// return obj.field;
// EXIT
// Disabled due to b/205813546.
TEST_F(LoadStoreEliminationTest, DISABLED_PartialLoadPreserved4) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  CreateGraph(&vshs);
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "entry_post"},
                                                  {"entry_post", "right"},
                                                  {"right", "return_block"},
                                                  {"entry_post", "left_pre"},
                                                  {"left_pre", "left_loop"},
                                                  {"left_loop", "left_loop"},
                                                  {"left_loop", "return_block"},
                                                  {"return_block", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(entry_post);
  GET_BLOCK(exit);
  GET_BLOCK(return_block);
  GET_BLOCK(left_pre);
  GET_BLOCK(left_loop);
  GET_BLOCK(right);
#undef GET_BLOCK
  // Left-loops first successor is the break.
  if (left_loop->GetSuccessors()[0] != return_block) {
    left_loop->SwapSuccessors();
  }
  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  HInstruction* cls = MakeLoadClass(entry);
  HInstruction* new_inst = MakeNewInstance(entry, cls);
  MakeGoto(entry);

  MakeIf(entry_post, bool_value);

  HInstruction* write_left_pre = MakeIFieldSet(left_pre, new_inst, c1, MemberOffset(32));
  MakeGoto(left_pre);

  HInstruction* suspend_left_loop = MakeSuspendCheck(left_loop);
  HInstruction* call_left_loop = MakeInvokeStatic(left_loop, DataType::Type::kBool, {});
  HInstruction* write_left_loop = MakeIFieldSet(left_loop, new_inst, c3, MemberOffset(32));
  MakeIf(left_loop, call_left_loop);

  HInstruction* write_right = MakeIFieldSet(right, new_inst, c2, MemberOffset(32));
  HInstruction* call_right = MakeInvokeStatic(right, DataType::Type::kBool, {new_inst});
  MakeGoto(right);

  HInstruction* read_return =
      MakeIFieldGet(return_block, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(return_block, read_return);

  MakeExit(exit);

  PerformLSE(blks);

  EXPECT_INS_RETAINED(read_return);
  EXPECT_INS_RETAINED(write_right);
  EXPECT_INS_RETAINED(write_left_loop);
  EXPECT_INS_RETAINED(call_left_loop);
  EXPECT_INS_REMOVED(write_left_pre);
  EXPECT_INS_RETAINED(call_right);
}

// // ENTRY
// obj = new Obj();
// if (parameter_value) {
//   // LEFT
//   // DO NOT ELIMINATE
//   escape(obj);
//   obj.field = 1;
//   // obj has already escaped so can't use field = 1 for value
//   noescape();
// } else {
//   // RIGHT
//   // obj is needed for read since we don't know what the left value is
//   // DO NOT ELIMINATE
//   obj.field = 2;
//   noescape();
// }
// EXIT
// ELIMINATE
// return obj.field
TEST_F(LoadStoreEliminationTest, PartialLoadPreserved5) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* breturn = InitEntryMainExitGraph(&vshs);

  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);

  auto [start, left, right] = CreateDiamondPattern(breturn, bool_value);

  // start
  HInstruction* cls = MakeLoadClass(start);
  HInstruction* new_inst = MakeNewInstance(start, cls);

  HInstruction* call_left = MakeInvokeStatic(left, DataType::Type::kVoid, { new_inst });
  HInstruction* write_left = MakeIFieldSet(left, new_inst, c1, MemberOffset(32));
  HInstruction* call2_left = MakeInvokeStatic(left, DataType::Type::kVoid, {});

  HInstruction* write_right = MakeIFieldSet(right, new_inst, c2, MemberOffset(32));
  HInstruction* call_right = MakeInvokeStatic(right, DataType::Type::kVoid, {});

  HInstruction* read_bottom =
      MakeIFieldGet(breturn, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(breturn, read_bottom);

  PerformLSE();

  EXPECT_INS_RETAINED(read_bottom);
  EXPECT_INS_RETAINED(write_right);
  EXPECT_INS_RETAINED(write_left);
  EXPECT_INS_RETAINED(call_left);
  EXPECT_INS_RETAINED(call_right);
}

// // ENTRY
// obj = new Obj();
// DO NOT ELIMINATE. Kept by escape.
// obj.field = 3;
// noescape();
// if (parameter_value) {
//   // LEFT
//   // DO NOT ELIMINATE
//   escape(obj);
//   obj.field = 1;
// } else {
//   // RIGHT
//   // ELIMINATE
//   obj.field = 2;
// }
// EXIT
// ELIMINATE
// return obj.field
// Disabled due to b/205813546.
TEST_F(LoadStoreEliminationTest, DISABLED_PartialLoadPreserved6) {
  CreateGraph();
  AdjacencyListGraph blks(SetupFromAdjacencyList("entry",
                                                 "exit",
                                                 {{"entry", "left"},
                                                  {"entry", "right"},
                                                  {"left", "breturn"},
                                                  {"right", "breturn"},
                                                  {"breturn", "exit"}}));
#define GET_BLOCK(name) HBasicBlock* name = blks.Get(#name)
  GET_BLOCK(entry);
  GET_BLOCK(exit);
  GET_BLOCK(breturn);
  GET_BLOCK(left);
  GET_BLOCK(right);
#undef GET_BLOCK
  HInstruction* bool_value = MakeParam(DataType::Type::kBool);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);

  HInstruction* cls = MakeLoadClass(entry);
  HInstruction* new_inst = MakeNewInstance(entry, cls);
  HInstruction* write_entry = MakeIFieldSet(entry, new_inst, c3, MemberOffset(32));
  HInstruction* call_entry = MakeInvokeStatic(entry, DataType::Type::kVoid, {});
  MakeIf(entry, bool_value);

  HInstruction* call_left = MakeInvokeStatic(left, DataType::Type::kVoid, { new_inst });
  HInstruction* write_left = MakeIFieldSet(left, new_inst, c1, MemberOffset(32));
  MakeGoto(left);

  HInstruction* write_right = MakeIFieldSet(right, new_inst, c2, MemberOffset(32));
  MakeGoto(right);

  HInstruction* read_bottom =
      MakeIFieldGet(breturn, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(breturn, read_bottom);

  MakeExit(exit);

  PerformLSE(blks);

  EXPECT_INS_REMOVED(read_bottom);
  EXPECT_INS_REMOVED(write_right);
  EXPECT_INS_RETAINED(write_entry);
  EXPECT_INS_RETAINED(write_left);
  EXPECT_INS_RETAINED(call_left);
  EXPECT_INS_RETAINED(call_entry);
}
}  // namespace art
