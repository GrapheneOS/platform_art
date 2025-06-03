/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "android-base/logging.h"
#include "base/macros.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "loop_optimization.h"
#include "optimizing/data_type.h"
#include "optimizing/nodes.h"
#include "optimizing_unit_test.h"

namespace art HIDDEN {

// Base class for loop optimization tests.
class LoopOptimizationTestBase : public OptimizingUnitTest {
 protected:
  void SetUp() override {
    OptimizingUnitTest::SetUp();

    BuildGraph();
    iva_  = new (GetAllocator()) HInductionVarAnalysis(graph_);
    if (compiler_options_ == nullptr) {
      compiler_options_ = CommonCompilerTest::CreateCompilerOptions(kRuntimeISA, "default");
      DCHECK(compiler_options_ != nullptr);
    }
    codegen_ = CodeGenerator::Create(graph_, *compiler_options_);
    DCHECK(codegen_.get() != nullptr);
    loop_opt_ = new (GetAllocator()) HLoopOptimization(
        graph_, *codegen_.get(), iva_, /* stats= */ nullptr);
  }

  void TearDown() override {
    codegen_.reset();
    compiler_options_.reset();
    graph_ = nullptr;
    ResetPoolAndAllocator();
    OptimizingUnitTest::TearDown();
  }

  virtual void BuildGraph() = 0;

  // Run loop optimization and optionally check the graph.
  void PerformAnalysis(bool run_checker) {
    graph_->BuildDominatorTree();

    // Check the graph is valid before loop optimization.
    std::ostringstream oss;
    if (run_checker) {
      ASSERT_TRUE(CheckGraph(oss)) << oss.str();
    }

    iva_->Run();
    loop_opt_->Run();

    // Check the graph is valid after loop optimization.
    if (run_checker) {
      ASSERT_TRUE(CheckGraph(oss)) << oss.str();
    }
  }

  // General building fields.
  std::unique_ptr<CompilerOptions> compiler_options_;
  std::unique_ptr<CodeGenerator> codegen_;
  HInductionVarAnalysis* iva_;
  HLoopOptimization* loop_opt_;

  HBasicBlock* return_block_;

  HInstruction* parameter_;
};

/**
 * Fixture class for the loop optimization tests. These unit tests mostly focus
 * on constructing the loop hierarchy. Checker tests are also used to test
 * specific optimizations.
 */
class LoopOptimizationTest : public LoopOptimizationTestBase {
 protected:
  virtual ~LoopOptimizationTest() {}

  /** Constructs bare minimum graph. */
  void BuildGraph() override {
    return_block_ = InitEntryMainExitGraph();
    graph_->SetNumberOfVRegs(1);
    parameter_ = MakeParam(DataType::Type::kInt32);
  }

  /** Adds a loop nest at given position before successor. */
  HBasicBlock* AddLoop(HBasicBlock* position, HBasicBlock* successor) {
    HBasicBlock* header = AddNewBlock();
    HBasicBlock* body = AddNewBlock();
    // Control flow.
    position->ReplaceSuccessor(successor, header);
    header->AddSuccessor(body);
    header->AddSuccessor(successor);
    MakeIf(header, parameter_);
    body->AddSuccessor(header);
    MakeGoto(body);
    return header;
  }

  /** Constructs string representation of computed loop hierarchy. */
  std::string LoopStructure() {
    return LoopStructureRecurse(loop_opt_->top_loop_);
  }

  // Helper method
  std::string LoopStructureRecurse(HLoopOptimization::LoopNode* node) {
    std::string s;
    for ( ; node != nullptr; node = node->next) {
      s.append("[");
      s.append(LoopStructureRecurse(node->inner));
      s.append("]");
    }
    return s;
  }
};

#ifdef ART_ENABLE_CODEGEN_arm64
// Unit tests for predicated vectorization.
class PredicatedSimdLoopOptimizationTest : public LoopOptimizationTestBase {
 protected:
  void SetUp() override {
    // Predicated SIMD is only supported by SVE on Arm64.
    compiler_options_ = CommonCompilerTest::CreateCompilerOptions(InstructionSet::kArm64,
                                                                  "default",
                                                                  "sve");
    LoopOptimizationTestBase::SetUp();
  }

  virtual ~PredicatedSimdLoopOptimizationTest() {}

  // Constructs a graph with a diamond loop which should be vectorizable with predicated
  // vectorization. This graph includes a basic loop induction (consisting of Phi, Add, If and
  // SuspendCheck instructions) to control the loop as well as an if comparison (consisting of
  // Parameter, GreaterThanOrEqual and If instructions) to control the diamond loop.
  //
  //                       entry
  //                         |
  //                      preheader
  //                         |
  //  return <------------ header <----------------+
  //     |                   |                     |
  //   exit             diamond_top                |
  //                       /   \                   |
  //            diamond_true  diamond_false        |
  //                       \   /                   |
  //                     back_edge                 |
  //                         |                     |
  //                         +---------------------+
  void BuildGraph() override {
    return_block_ = InitEntryMainExitGraphWithReturnVoid();
    HBasicBlock* back_edge;
    std::tie(std::ignore, header_, back_edge) = CreateWhileLoop(return_block_);
    std::tie(diamond_top_, diamond_true_, std::ignore) = CreateDiamondPattern(back_edge);

    parameter_ = MakeParam(DataType::Type::kInt32);
    std::tie(phi_, std::ignore) = MakeLinearLoopVar(header_, back_edge, 0, 1);
    MakeSuspendCheck(header_);
    HInstruction* trip = MakeCondition(header_,
                                       kCondGE,
                                       phi_,
                                       graph_->GetIntConstant(kArm64DefaultSVEVectorLength));
    MakeIf(header_, trip);
    diamond_hif_ = MakeIf(diamond_top_, parameter_);
  }

  // Add an ArraySet to the loop which will be vectorized, thus setting the type of vector
  // instructions in the graph to the given vector_type. This needs to be called to ensure the loop
  // is not simplified by SimplifyInduction or SimplifyBlocks before vectorization.
  void AddArraySetToLoop(DataType::Type vector_type) {
    // Ensure the data type is a java type so it can be stored in a TypeField. The actual type does
    // not matter as long as the size is the same so it can still be vectorized.
    DataType::Type new_type = DataType::SignedIntegralTypeFromSize(DataType::Size(vector_type));

    // Add an array set to prevent the loop from being optimized away before vectorization.
    // Note: This uses an integer parameter and not an array reference to avoid the difficulties in
    // allocating an array. The instruction is still treated as a valid ArraySet by loop
    // optimization.
    diamond_true_->AddInstruction(new (GetAllocator()) HArraySet(parameter_,
                                                                 phi_,
                                                                 graph_->GetIntConstant(1),
                                                                 new_type,
                                                                 /* dex_pc= */ 0));
  }

  // Replace the input of diamond_hif_ with a new condition of the given types.
  void ReplaceIfCondition(DataType::Type l_type,
                          DataType::Type r_type,
                          HBasicBlock* condition_block,
                          IfCondition cond) {
    AddArraySetToLoop(l_type);
    HInstruction* l_param = MakeParam(l_type);
    HInstruction* r_param = MakeParam(r_type);
    HCondition* condition = MakeCondition(condition_block, cond, l_param, r_param);
    diamond_hif_->ReplaceInput(condition, 0);
  }

  // Is loop optimization able to vectorize predicated code?
  bool IsPredicatedVectorizationSupported() {
    // Mirror the check guarding TryVectorizePredicated in TryOptimizeInnerLoopFinite.
    return kForceTryPredicatedSIMD && loop_opt_->IsInPredicatedVectorizationMode();
  }

  HBasicBlock* header_;
  HBasicBlock* diamond_top_;
  HBasicBlock* diamond_true_;

  HPhi* phi_;
  HIf* diamond_hif_;
};

#endif  // ART_ENABLE_CODEGEN_arm64

//
// The actual tests.
//

// Loop structure tests can't run the graph checker because they don't create valid graphs.

TEST_F(LoopOptimizationTest, NoLoops) {
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("", LoopStructure());
}

TEST_F(LoopOptimizationTest, SingleLoop) {
  AddLoop(entry_block_, return_block_);
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("[]", LoopStructure());
}

TEST_F(LoopOptimizationTest, LoopNest10) {
  HBasicBlock* b = entry_block_;
  HBasicBlock* s = return_block_;
  for (int i = 0; i < 10; i++) {
    s = AddLoop(b, s);
    b = s->GetSuccessors()[0];
  }
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("[[[[[[[[[[]]]]]]]]]]", LoopStructure());
}

TEST_F(LoopOptimizationTest, LoopSequence10) {
  HBasicBlock* b = entry_block_;
  HBasicBlock* s = return_block_;
  for (int i = 0; i < 10; i++) {
    b = AddLoop(b, s);
    s = b->GetSuccessors()[1];
  }
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("[][][][][][][][][][]", LoopStructure());
}

TEST_F(LoopOptimizationTest, LoopSequenceOfNests) {
  HBasicBlock* b = entry_block_;
  HBasicBlock* s = return_block_;
  for (int i = 0; i < 10; i++) {
    b = AddLoop(b, s);
    s = b->GetSuccessors()[1];
    HBasicBlock* bi = b->GetSuccessors()[0];
    HBasicBlock* si = b;
    for (int j = 0; j < i; j++) {
      si = AddLoop(bi, si);
      bi = si->GetSuccessors()[0];
    }
  }
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("[]"
            "[[]]"
            "[[[]]]"
            "[[[[]]]]"
            "[[[[[]]]]]"
            "[[[[[[]]]]]]"
            "[[[[[[[]]]]]]]"
            "[[[[[[[[]]]]]]]]"
            "[[[[[[[[[]]]]]]]]]"
            "[[[[[[[[[[]]]]]]]]]]",
            LoopStructure());
}

TEST_F(LoopOptimizationTest, LoopNestWithSequence) {
  HBasicBlock* b = entry_block_;
  HBasicBlock* s = return_block_;
  for (int i = 0; i < 10; i++) {
    s = AddLoop(b, s);
    b = s->GetSuccessors()[0];
  }
  b = s;
  s = b->GetSuccessors()[1];
  for (int i = 0; i < 9; i++) {
    b = AddLoop(b, s);
    s = b->GetSuccessors()[1];
  }
  PerformAnalysis(/*run_checker=*/ false);
  EXPECT_EQ("[[[[[[[[[[][][][][][][][][][]]]]]]]]]]", LoopStructure());
}

// Check that SimplifyLoop() doesn't invalidate data flow when ordering loop headers'
// predecessors.
//
// This is a test for nodes.cc functionality - HGraph::SimplifyLoop.
TEST_F(LoopOptimizationTest, SimplifyLoopReoderPredecessors) {
  // Can't use AddLoop as we want special order for blocks predecessors.
  HBasicBlock* header = AddNewBlock();
  HBasicBlock* body = AddNewBlock();

  // Control flow: make a loop back edge first in the list of predecessors.
  entry_block_->RemoveSuccessor(return_block_);
  body->AddSuccessor(header);
  entry_block_->AddSuccessor(header);
  header->AddSuccessor(body);
  header->AddSuccessor(return_block_);
  DCHECK(header->GetSuccessors()[1] == return_block_);

  // Data flow.
  MakeIf(header, parameter_);
  MakeGoto(body);

  HPhi* phi = new (GetAllocator()) HPhi(GetAllocator(), 0, 0, DataType::Type::kInt32);
  header->AddPhi(phi);
  HInstruction* add = MakeBinOp<HAdd>(body, DataType::Type::kInt32, phi, parameter_);

  phi->AddInput(add);
  phi->AddInput(parameter_);

  graph_->ClearLoopInformation();
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  // BuildDominatorTree inserts a block beetween loop header and entry block.
  EXPECT_EQ(header->GetPredecessors()[0]->GetSinglePredecessor(), entry_block_);

  // Check that after optimizations in BuildDominatorTree()/SimplifyCFG() phi inputs
  // are still mapped correctly to the block predecessors.
  for (size_t i = 0, e = phi->InputCount(); i < e; i++) {
    HInstruction* input = phi->InputAt(i);
    EXPECT_TRUE(input->GetBlock()->Dominates(header->GetPredecessors()[i]));
  }
}

// Test that SimplifyLoop() processes the multiple-preheaders loops correctly.
//
// This is a test for nodes.cc functionality - HGraph::SimplifyLoop.
TEST_F(LoopOptimizationTest, SimplifyLoopSinglePreheader) {
  HBasicBlock* header = AddLoop(entry_block_, return_block_);

  header->InsertInstructionBefore(
      new (GetAllocator()) HSuspendCheck(), header->GetLastInstruction());

  // Insert an if construct before the loop so it will have two preheaders.
  HBasicBlock* if_block = AddNewBlock();
  HBasicBlock* preheader0 = AddNewBlock();
  HBasicBlock* preheader1 = AddNewBlock();

  // Fix successors/predecessors.
  entry_block_->ReplaceSuccessor(header, if_block);
  if_block->AddSuccessor(preheader0);
  if_block->AddSuccessor(preheader1);
  preheader0->AddSuccessor(header);
  preheader1->AddSuccessor(header);

  MakeIf(if_block, parameter_);
  MakeGoto(preheader0);
  MakeGoto(preheader1);

  HBasicBlock* body = header->GetSuccessors()[0];
  DCHECK(body != return_block_);

  // Add some data flow.
  HIntConstant* const_0 = graph_->GetIntConstant(0);
  HIntConstant* const_1 = graph_->GetIntConstant(1);
  HIntConstant* const_2 = graph_->GetIntConstant(2);

  HAdd* preheader0_add = MakeBinOp<HAdd>(preheader0, DataType::Type::kInt32, parameter_, const_0);
  HAdd* preheader1_add = MakeBinOp<HAdd>(preheader1, DataType::Type::kInt32, parameter_, const_1);

  HPhi* header_phi = new (GetAllocator()) HPhi(GetAllocator(), 0, 0, DataType::Type::kInt32);
  header->AddPhi(header_phi);

  HAdd* body_add = MakeBinOp<HAdd>(body, DataType::Type::kInt32, parameter_, const_2);

  DCHECK(header->GetPredecessors()[0] == body);
  DCHECK(header->GetPredecessors()[1] == preheader0);
  DCHECK(header->GetPredecessors()[2] == preheader1);

  header_phi->AddInput(body_add);
  header_phi->AddInput(preheader0_add);
  header_phi->AddInput(preheader1_add);

  graph_->ClearLoopInformation();
  graph_->ClearDominanceInformation();
  graph_->BuildDominatorTree();

  EXPECT_EQ(header->GetPredecessors().size(), 2u);
  EXPECT_EQ(header->GetPredecessors()[1], body);

  HBasicBlock* new_preheader = header->GetLoopInformation()->GetPreHeader();
  EXPECT_EQ(preheader0->GetSingleSuccessor(), new_preheader);
  EXPECT_EQ(preheader1->GetSingleSuccessor(), new_preheader);

  EXPECT_EQ(new_preheader->GetPhis().CountSize(), 1u);
  HPhi* new_preheader_phi = new_preheader->GetFirstPhi()->AsPhi();
  EXPECT_EQ(new_preheader_phi->InputCount(), 2u);
  EXPECT_EQ(new_preheader_phi->InputAt(0), preheader0_add);
  EXPECT_EQ(new_preheader_phi->InputAt(1), preheader1_add);

  EXPECT_EQ(header_phi->InputCount(), 2u);
  EXPECT_EQ(header_phi->InputAt(0), new_preheader_phi);
  EXPECT_EQ(header_phi->InputAt(1), body_add);
}

#ifdef ART_ENABLE_CODEGEN_arm64
#define FOR_EACH_CONDITION_INSTRUCTION(M, CondType) \
  M(EQ, CondType)                                   \
  M(NE, CondType)                                   \
  M(LT, CondType)                                   \
  M(LE, CondType)                                   \
  M(GT, CondType)                                   \
  M(GE, CondType)                                   \
  M(B, CondType)                                    \
  M(BE, CondType)                                   \
  M(A, CondType)                                    \
  M(AE, CondType)

// Define tests ensuring that all types of conditions can be handled in predicated vectorization
// for diamond loops.
#define DEFINE_CONDITION_TESTS(Name, CondType)                                                  \
TEST_F(PredicatedSimdLoopOptimizationTest, VectorizeCondition##Name##CondType) {                \
  if (!IsPredicatedVectorizationSupported()) {                                                  \
    GTEST_SKIP() << "Predicated SIMD is not enabled.";                                          \
  }                                                                                             \
  ReplaceIfCondition(DataType::Type::k##CondType,                                               \
                     DataType::Type::k##CondType,                                               \
                     diamond_top_,                                                              \
                     kCond##Name);                                                              \
  PerformAnalysis(/*run_checker=*/ true);                                                       \
  EXPECT_TRUE(graph_->HasPredicatedSIMD());                                                     \
}
FOR_EACH_CONDITION_INSTRUCTION(DEFINE_CONDITION_TESTS, Uint8)
FOR_EACH_CONDITION_INSTRUCTION(DEFINE_CONDITION_TESTS, Int8)
FOR_EACH_CONDITION_INSTRUCTION(DEFINE_CONDITION_TESTS, Uint16)
FOR_EACH_CONDITION_INSTRUCTION(DEFINE_CONDITION_TESTS, Int16)
FOR_EACH_CONDITION_INSTRUCTION(DEFINE_CONDITION_TESTS, Int32)
#undef DEFINE_CONDITION_TESTS
#undef FOR_EACH_CONDITION_INSTRUCTION
#endif  // ART_ENABLE_CODEGEN_arm64

}  // namespace art
