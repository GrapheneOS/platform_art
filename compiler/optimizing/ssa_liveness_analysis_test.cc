/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "ssa_liveness_analysis.h"

#include "arch/instruction_set.h"
#include "arch/instruction_set_features.h"
#include "base/arena_allocator.h"
#include "base/arena_containers.h"
#include "base/macros.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "optimizing_unit_test.h"

namespace art HIDDEN {

class SsaLivenessAnalysisTest : public OptimizingUnitTest {
 protected:
  void SetUp() override {
    OptimizingUnitTest::SetUp();
    graph_ = CreateGraph();
    compiler_options_ = CommonCompilerTest::CreateCompilerOptions(kRuntimeISA, "default");
    codegen_ = CodeGenerator::Create(graph_, *compiler_options_);
    CHECK(codegen_ != nullptr);
    // Create entry block.
    entry_ = new (GetAllocator()) HBasicBlock(graph_);
    graph_->AddBlock(entry_);
    graph_->SetEntryBlock(entry_);
  }

 protected:
  HBasicBlock* CreateSuccessor(HBasicBlock* block) {
    HGraph* graph = block->GetGraph();
    HBasicBlock* successor = new (GetAllocator()) HBasicBlock(graph);
    graph->AddBlock(successor);
    block->AddSuccessor(successor);
    return successor;
  }

  HGraph* graph_;
  std::unique_ptr<CompilerOptions> compiler_options_;
  std::unique_ptr<CodeGenerator> codegen_;
  HBasicBlock* entry_;
};

TEST_F(SsaLivenessAnalysisTest, TestReturnArg) {
  HInstruction* arg = MakeParam(DataType::Type::kInt32);

  HBasicBlock* block = CreateSuccessor(entry_);
  MakeReturn(block, arg);
  HBasicBlock* exit = AddExitBlock();
  block->AddSuccessor(exit);

  graph_->BuildDominatorTree();
  SsaLivenessAnalysis ssa_analysis(graph_, codegen_.get(), GetScopedAllocator());
  ssa_analysis.Analyze();

  std::ostringstream arg_dump;
  arg->GetLiveInterval()->Dump(arg_dump);
  EXPECT_STREQ(
      "ranges: { [4,12) }, uses: { 12 }, { } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
      arg_dump.str().c_str());
}

TEST_F(SsaLivenessAnalysisTest, TestAput) {
  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);
  HInstruction* value = MakeParam(DataType::Type::kInt32);
  HInstruction* extra_arg1 = MakeParam(DataType::Type::kInt32);
  HInstruction* extra_arg2 = MakeParam(DataType::Type::kReference);
  std::initializer_list<HInstruction*> args{array, index, value, extra_arg1, extra_arg2};

  HBasicBlock* block = CreateSuccessor(entry_);
  HInstruction* null_check = MakeNullCheck(block, array, /*env=*/ args);
  HInstruction* length = MakeArrayLength(block, array);
  HInstruction* bounds_check = MakeBoundsCheck(block, index, length, /*env=*/ args);
  MakeArraySet(block, array, index, value, DataType::Type::kInt32);
  HBasicBlock* exit = AddExitBlock();
  block->AddSuccessor(exit);

  graph_->BuildDominatorTree();
  SsaLivenessAnalysis ssa_analysis(graph_, codegen_.get(), GetScopedAllocator());
  ssa_analysis.Analyze();

  EXPECT_FALSE(graph_->IsDebuggable());
  EXPECT_EQ(36u, bounds_check->GetLifetimePosition());
  static const char* const expected[] = {
      "ranges: { [4,41) }, uses: { 29 33 41 }, { 29 37 } is_fixed: 0, is_split: 0 is_low: 0 "
          "is_high: 0",
      "ranges: { [8,41) }, uses: { 37 41 }, { } is_fixed: 0, is_split: 0 is_low: 0 "
          "is_high: 0",
      "ranges: { [12,41) }, uses: { 41 }, { } is_fixed: 0, is_split: 0 is_low: 0 "
          "is_high: 0",
      // Environment uses do not keep the non-reference argument alive.
      "ranges: { [16,20) }, uses: { }, { } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
      // Environment uses keep the reference argument alive.
      "ranges: { [20,37) }, uses: { }, { 29 37 } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
  };
  CHECK_EQ(arraysize(expected), args.size());
  size_t arg_index = 0u;
  for (HInstruction* arg : args) {
    std::ostringstream arg_dump;
    arg->GetLiveInterval()->Dump(arg_dump);
    EXPECT_STREQ(expected[arg_index], arg_dump.str().c_str()) << arg_index;
    ++arg_index;
  }
}

TEST_F(SsaLivenessAnalysisTest, TestDeoptimize) {
  HInstruction* array = MakeParam(DataType::Type::kReference);
  HInstruction* index = MakeParam(DataType::Type::kInt32);
  HInstruction* value = MakeParam(DataType::Type::kInt32);
  HInstruction* extra_arg1 = MakeParam(DataType::Type::kInt32);
  HInstruction* extra_arg2 = MakeParam(DataType::Type::kReference);
  std::initializer_list<HInstruction*> args{array, index, value, extra_arg1, extra_arg2};

  HBasicBlock* block = CreateSuccessor(entry_);
  HInstruction* null_check = MakeNullCheck(block, array, /*env=*/ args);
  HInstruction* length = MakeArrayLength(block, array);
  // Use HAboveOrEqual+HDeoptimize as the bounds check.
  HInstruction* ae = MakeCondition(block, kCondAE, index, length);
  HInstruction* deoptimize = new(GetAllocator()) HDeoptimize(
      GetAllocator(), ae, DeoptimizationKind::kBlockBCE, /* dex_pc= */ 0u);
  block->AddInstruction(deoptimize);
  ManuallyBuildEnvFor(deoptimize, /*env=*/ args);
  MakeArraySet(block, array, index, value, DataType::Type::kInt32);
  HBasicBlock* exit = AddExitBlock();
  block->AddSuccessor(exit);

  graph_->BuildDominatorTree();
  SsaLivenessAnalysis ssa_analysis(graph_, codegen_.get(), GetScopedAllocator());
  ssa_analysis.Analyze();

  EXPECT_FALSE(graph_->IsDebuggable());
  EXPECT_EQ(40u, deoptimize->GetLifetimePosition());
  static const char* const expected[] = {
      "ranges: { [4,45) }, uses: { 29 33 45 }, { 29 41 } is_fixed: 0, is_split: 0 is_low: 0 "
          "is_high: 0",
      "ranges: { [8,45) }, uses: { 37 45 }, { 41 } is_fixed: 0, is_split: 0 is_low: 0 "
          "is_high: 0",
      "ranges: { [12,45) }, uses: { 45 }, { 41 } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
      // Environment use in HDeoptimize keeps even the non-reference argument alive.
      "ranges: { [16,41) }, uses: { }, { 41 } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
      // Environment uses keep the reference argument alive.
      "ranges: { [20,41) }, uses: { }, { 29 41 } is_fixed: 0, is_split: 0 is_low: 0 is_high: 0",
  };
  CHECK_EQ(arraysize(expected), args.size());
  size_t arg_index = 0u;
  for (HInstruction* arg : args) {
    std::ostringstream arg_dump;
    arg->GetLiveInterval()->Dump(arg_dump);
    EXPECT_STREQ(expected[arg_index], arg_dump.str().c_str()) << arg_index;
    ++arg_index;
  }
}

}  // namespace art
