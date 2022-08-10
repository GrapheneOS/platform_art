/*
 * Copyright 2021 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_INL_H_
#define ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_INL_H_

#include "mark_compact.h"

namespace art {
namespace gc {
namespace collector {

template <size_t kAlignment>
uintptr_t MarkCompact::LiveWordsBitmap<kAlignment>::SetLiveWords(uintptr_t begin, size_t size) {
  const uintptr_t begin_bit_idx = MemRangeBitmap::BitIndexFromAddr(begin);
  DCHECK(!Bitmap::TestBit(begin_bit_idx));
  const uintptr_t end_bit_idx = MemRangeBitmap::BitIndexFromAddr(begin + size);
  uintptr_t* bm_address = Bitmap::Begin() + Bitmap::BitIndexToWordIndex(begin_bit_idx);
  uintptr_t* const end_bm_address = Bitmap::Begin() + Bitmap::BitIndexToWordIndex(end_bit_idx);
  uintptr_t mask = Bitmap::BitIndexToMask(begin_bit_idx);
  // Bits that needs to be set in the first word, if it's not also the last word
  mask = ~(mask - 1);
  // loop over all the words, except the last one.
  while (bm_address < end_bm_address) {
    *bm_address |= mask;
    bm_address++;
    // This needs to be set only once as we are setting all bits in the
    // subsequent iterations. Hopefully, the compiler will optimize it.
    mask = ~0;
  }
  // Take care of the last word. If we had only one word, then mask != ~0.
  const uintptr_t end_mask = Bitmap::BitIndexToMask(end_bit_idx);
  *bm_address |= mask & (end_mask - 1);
  return begin_bit_idx;
}

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_INL_H_
