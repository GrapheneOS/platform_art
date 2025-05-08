/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "instruction_simplifier_riscv64.h"

#include <gtest/gtest.h>

#include "base/globals.h"
#include "optimizing/nodes.h"
#include "optimizing_unit_test.h"

namespace art HIDDEN {
namespace riscv64 {

class InstructionSimplifierRiscv64Test : public OptimizingUnitTest {};

TEST_F(InstructionSimplifierRiscv64Test, SimplifyShiftAdd) {
  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();
  graph_->BuildDominatorTree();

  HInstruction* param0 = MakeParam(DataType::Type::kInt64);
  HInstruction* param1 = MakeParam(DataType::Type::kInt64);
  HInstruction* c0 = graph_->GetIntConstant(0);
  HInstruction* c1 = graph_->GetIntConstant(1);
  HInstruction* c2 = graph_->GetIntConstant(2);
  HInstruction* c3 = graph_->GetIntConstant(3);
  HInstruction* c4 = graph_->GetIntConstant(4);

  HInstruction* shl0 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c0);
  HInstruction* add_shl0 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl0);
  HInstruction* shl1 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c1);
  HInstruction* add_shl1 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl1);
  HInstruction* shl2 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c2);
  HInstruction* add_shl2 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl2);
  HInstruction* shl3 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c3);
  HInstruction* add_shl3 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl3);
  HInstruction* shl4 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c4);
  HInstruction* add_shl4 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl4);

  InstructionSimplifierRiscv64 simplifier(graph_, /*stats=*/ nullptr);
  simplifier.Run();

  EXPECT_FALSE(add_shl0->GetBlock() == nullptr);
  EXPECT_TRUE(add_shl1->GetBlock() == nullptr);
  EXPECT_TRUE(add_shl2->GetBlock() == nullptr);
  EXPECT_TRUE(add_shl3->GetBlock() == nullptr);
  EXPECT_FALSE(add_shl4->GetBlock() == nullptr);
}

TEST_F(InstructionSimplifierRiscv64Test, SimplifyShiftAddReusedShift) {
  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();
  graph_->BuildDominatorTree();

  HInstruction* param0 = MakeParam(DataType::Type::kInt64);
  HInstruction* param1 = MakeParam(DataType::Type::kInt64);
  HInstruction* param2 = MakeParam(DataType::Type::kInt64);
  HInstruction* param3 = MakeParam(DataType::Type::kInt64);
  HInstruction* c1 = graph_->GetIntConstant(1);

  HInstruction* shl1 = MakeBinOp<HShl>(block, DataType::Type::kInt64, param0, c1);
  HInstruction* add1 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param1, shl1);
  HInstruction* add2 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param2, shl1);
  HInstruction* add3 = MakeBinOp<HAdd>(block, DataType::Type::kInt64, param3, shl1);

  InstructionSimplifierRiscv64 simplifier(graph_, /*stats=*/ nullptr);
  simplifier.Run();

  EXPECT_TRUE(shl1->GetBlock() == nullptr);
  EXPECT_TRUE(add1->GetBlock() == nullptr);
  EXPECT_TRUE(add2->GetBlock() == nullptr);
  EXPECT_TRUE(add3->GetBlock() == nullptr);
}

}  // namespace riscv64
}  // namespace art
