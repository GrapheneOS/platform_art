/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_INL_H_
#define ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_INL_H_

#include "register_allocator.h"

#include "base/bit_utils.h"
#include "data_type.h"
#include "ssa_liveness_analysis.h"

namespace art HIDDEN {

inline uint32_t RegisterAllocator::GetSingleRegisterMask(LiveInterval* interval,
                                                         RegisterType register_type) {
  DCHECK(interval->HasRegister());
  DCHECK_EQ(register_type == RegisterType::kFpRegister,
            DataType::IsFloatingPointType(interval->GetType()));
  DCHECK_LE(static_cast<size_t>(interval->GetRegister()), BitSizeOf<uint32_t>());
  return 1u << interval->GetRegister();
}

inline uint32_t RegisterAllocator::GetBlockedRegistersMask(
    LiveInterval* interval,
    ArrayRef<HInstruction* const> instructions_from_positions,
    size_t number_of_registers,
    uint32_t registers_blocked_for_call) {
  DCHECK(!interval->HasRegister());
  DCHECK(interval->IsFixed());
  DCHECK_EQ(interval->GetType(), DataType::Type::kVoid);
  DCHECK(interval->GetFirstRange() != nullptr);
  size_t start = interval->GetFirstRange()->GetStart();
  bool blocked_for_call =
      instructions_from_positions[start / kLivenessPositionsPerInstruction] != nullptr;
  return blocked_for_call ? registers_blocked_for_call : MaxInt<uint32_t>(number_of_registers);
}

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_INL_H_
