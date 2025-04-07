/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "licm.h"

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

namespace art HIDDEN {

/**
 * Fixture class for the LICM tests.
 */
class LICMTest : public OptimizingUnitTest {
 public:
  LICMTest()
      : loop_preheader_(nullptr),
        loop_header_(nullptr),
        loop_body_(nullptr),
        parameter_(nullptr),
        int_constant_(nullptr),
        float_constant_(nullptr) {
    graph_ = CreateGraph();
  }

  ~LICMTest() { }

  // Builds a singly-nested loop structure in CFG. Tests can further populate
  // the basic blocks with instructions to set up interesting scenarios.
  void BuildLoop() {
    HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
    std::tie(loop_preheader_, loop_header_, loop_body_) = CreateWhileLoop(return_block);
    loop_header_->SwapSuccessors();  // Move the loop exit to the "else" successor.

    // Provide boiler-plate instructions.
    parameter_ = MakeParam(DataType::Type::kReference);
    int_constant_ = graph_->GetIntConstant(42);
    float_constant_ = graph_->GetFloatConstant(42.0f);
    MakeIf(loop_header_, parameter_);
  }

  // Performs LICM optimizations (after proper set up).
  void PerformLICM() {
    graph_->BuildDominatorTree();
    LICM(graph_, nullptr).Run();
  }

  // Specific basic blocks.
  HBasicBlock* loop_preheader_;
  HBasicBlock* loop_header_;
  HBasicBlock* loop_body_;

  HInstruction* parameter_;  // "this"
  HInstruction* int_constant_;
  HInstruction* float_constant_;
};

//
// The actual LICM tests.
//

TEST_F(LICMTest, FieldHoisting) {
  BuildLoop();

  // Populate the loop with instructions: set/get field with different types.
  HInstruction* get_field =
      MakeIFieldGet(loop_body_, parameter_, DataType::Type::kInt64, MemberOffset(10));
  HInstruction* set_field =
     MakeIFieldSet(loop_body_, parameter_, int_constant_, DataType::Type::kInt32, MemberOffset(20));

  EXPECT_EQ(get_field->GetBlock(), loop_body_);
  EXPECT_EQ(set_field->GetBlock(), loop_body_);
  PerformLICM();
  EXPECT_EQ(get_field->GetBlock(), loop_preheader_);
  EXPECT_EQ(set_field->GetBlock(), loop_body_);
}

TEST_F(LICMTest, NoFieldHoisting) {
  BuildLoop();

  // Populate the loop with instructions: set/get field with same types.
  ScopedNullHandle<mirror::DexCache> dex_cache;
  HInstruction* get_field =
      MakeIFieldGet(loop_body_, parameter_, DataType::Type::kInt64, MemberOffset(10));
  HInstruction* set_field = MakeIFieldSet(loop_body_, parameter_, get_field, MemberOffset(10));

  EXPECT_EQ(get_field->GetBlock(), loop_body_);
  EXPECT_EQ(set_field->GetBlock(), loop_body_);
  PerformLICM();
  EXPECT_EQ(get_field->GetBlock(), loop_body_);
  EXPECT_EQ(set_field->GetBlock(), loop_body_);
}

TEST_F(LICMTest, ArrayHoisting) {
  BuildLoop();

  // Populate the loop with instructions: set/get array with different types.
  HInstruction* get_array =
      MakeArrayGet(loop_body_, parameter_, int_constant_, DataType::Type::kInt32);
  HInstruction* set_array = MakeArraySet(
      loop_body_, parameter_, int_constant_, float_constant_, DataType::Type::kFloat32);

  EXPECT_EQ(get_array->GetBlock(), loop_body_);
  EXPECT_EQ(set_array->GetBlock(), loop_body_);
  PerformLICM();
  EXPECT_EQ(get_array->GetBlock(), loop_preheader_);
  EXPECT_EQ(set_array->GetBlock(), loop_body_);
}

TEST_F(LICMTest, NoArrayHoisting) {
  BuildLoop();

  // Populate the loop with instructions: set/get array with same types.
  HInstruction* get_array =
      MakeArrayGet(loop_body_, parameter_, int_constant_, DataType::Type::kFloat32);
  HInstruction* set_array =
      MakeArraySet(loop_body_, parameter_, get_array, float_constant_, DataType::Type::kFloat32);

  EXPECT_EQ(get_array->GetBlock(), loop_body_);
  EXPECT_EQ(set_array->GetBlock(), loop_body_);
  PerformLICM();
  EXPECT_EQ(get_array->GetBlock(), loop_body_);
  EXPECT_EQ(set_array->GetBlock(), loop_body_);
}

}  // namespace art
