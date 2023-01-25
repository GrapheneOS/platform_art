/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "instruction_set_features_riscv64.h"

#include <gtest/gtest.h>

namespace art {

TEST(Riscv64InstructionSetFeaturesTest, Riscv64FeaturesFromDefaultVariant) {
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> riscv64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kRiscv64, "generic", &error_msg));
  ASSERT_TRUE(riscv64_features.get() != nullptr) << error_msg;

  EXPECT_EQ(riscv64_features->GetInstructionSet(), InstructionSet::kRiscv64);

  EXPECT_TRUE(riscv64_features->Equals(riscv64_features.get()));

  uint32_t expected_extensions =
      Riscv64InstructionSetFeatures::kExtGeneric | Riscv64InstructionSetFeatures::kExtCompressed;
  EXPECT_EQ(riscv64_features->AsBitmap(), expected_extensions);  // rv64gc, aka rv64imafdc
}

}  // namespace art
