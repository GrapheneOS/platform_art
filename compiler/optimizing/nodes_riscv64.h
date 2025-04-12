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

#ifndef ART_COMPILER_OPTIMIZING_NODES_RISCV64_H_
#define ART_COMPILER_OPTIMIZING_NODES_RISCV64_H_

namespace art HIDDEN {

class HRiscv64ShiftAdd final : public HBinaryOperation {
 public:
  HRiscv64ShiftAdd(HInstruction* left,
                   HInstruction* right,
                   uint32_t distance,
                   uint32_t dex_pc = kNoDexPc)
      : HBinaryOperation(
            kRiscv64ShiftAdd, DataType::Type::kInt64, left, right, SideEffects::None(), dex_pc) {
    DCHECK_GE(distance, 1u);
    DCHECK_LE(distance, 3u);

    SetPackedField<DistanceField>(distance);
  }

  uint32_t GetDistance() const { return GetPackedField<DistanceField>(); }

  bool InstructionDataEquals(const HInstruction* other) const override {
    return GetPackedFields() == other->AsRiscv64ShiftAdd()->GetPackedFields();
  }

  HConstant* Evaluate(HLongConstant* x, HLongConstant* y) const override {
    const int64_t value = y->GetValue() + (x->GetValue() << GetDistance());
    return GetBlock()->GetGraph()->GetLongConstant(value);
  }

  DECLARE_INSTRUCTION(Riscv64ShiftAdd);

 protected:
  DEFAULT_COPY_CONSTRUCTOR(Riscv64ShiftAdd);

 private:
  static constexpr size_t kFieldDistance = kNumberOfGenericPackedBits;
  static constexpr size_t kFieldDistanceSize = MinimumBitsToStore(3u);
  static constexpr size_t kNumberOfRiscv64ShiftAddPackedBits =
      kFieldDistance + kFieldDistanceSize;
  static_assert(kNumberOfRiscv64ShiftAddPackedBits <= kMaxNumberOfPackedBits,
                "Too many packed fields.");
  using DistanceField = BitField<uint32_t, kFieldDistance, kFieldDistanceSize>;
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_NODES_RISCV64_H_
