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

#ifndef ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_INL_H_
#define ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_INL_H_

#include "loop_information.h"

#include "base/array_ref.h"
#include "base/stl_util.h"
#include "base/transform_iterator.h"
#include "nodes.h"

namespace art HIDDEN {

inline auto HLoopInformation::GetBlocks() const {
  ArrayRef<HBasicBlock* const> blocks(GetHeader()->GetGraph()->GetBlocks());
  return MakeTransformRange(block_mask_.Indexes(),
                            [blocks](uint32_t index) {
                              DCHECK(blocks[index] != nullptr);
                              return blocks[index];
                            });
}

inline auto HLoopInformation::GetBlocksPostOrder() const {
  return Filter(GetHeader()->GetGraph()->GetPostOrder(),
                [this](HBasicBlock* block) { return block_mask_.IsBitSet(block->GetBlockId()); });
}

inline auto HLoopInformation::GetBlocksReversePostOrder() const {
  return Filter(GetHeader()->GetGraph()->GetReversePostOrder(),
                [this](HBasicBlock* block) { return block_mask_.IsBitSet(block->GetBlockId()); });
}

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_INL_H_
