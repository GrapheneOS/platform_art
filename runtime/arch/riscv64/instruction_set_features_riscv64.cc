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

#include <fstream>
#include <sstream>

#include "android-base/strings.h"
#include "base/logging.h"

namespace art {

// Basic feature set is rv64gc, aka rv64imafdc.
constexpr uint32_t BasicFeatures() {
  return Riscv64InstructionSetFeatures::kExtGeneric | Riscv64InstructionSetFeatures::kExtCompressed;
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromVariant(
    const std::string& variant, std::string* error_msg ATTRIBUTE_UNUSED) {
  if (variant != "generic") {
    LOG(WARNING) << "Unexpected CPU variant for Riscv64 using defaults: " << variant;
  }
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures(BasicFeatures()));
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromBitmap(uint32_t bitmap) {
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures(bitmap));
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCppDefines() {
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures(BasicFeatures()));
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCpuInfo() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromHwcap() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromAssembly() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCpuFeatures() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

bool Riscv64InstructionSetFeatures::Equals(const InstructionSetFeatures* other) const {
  if (InstructionSet::kRiscv64 != other->GetInstructionSet()) {
    return false;
  }
  return bits_ == other->AsRiscv64InstructionSetFeatures()->bits_;
}

uint32_t Riscv64InstructionSetFeatures::AsBitmap() const { return bits_; }

std::string Riscv64InstructionSetFeatures::GetFeatureString() const {
  std::string result = "rv64";
  if (bits_ & kExtGeneric) {
    result += "g";
  }
  if (bits_ & kExtCompressed) {
    result += "c";
  }
  if (bits_ & kExtVector) {
    result += "v";
  }
  return result;
}

std::unique_ptr<const InstructionSetFeatures>
Riscv64InstructionSetFeatures::AddFeaturesFromSplitString(
    const std::vector<std::string>& features ATTRIBUTE_UNUSED,
    std::string* error_msg ATTRIBUTE_UNUSED) const {
  UNIMPLEMENTED(WARNING);
  return std::unique_ptr<const InstructionSetFeatures>(new Riscv64InstructionSetFeatures(bits_));
}

}  // namespace art
