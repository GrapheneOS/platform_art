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

#include "mirror/object-inl.h"

namespace art {
namespace gc {
namespace collector {

template <size_t kAlignment>
inline uintptr_t MarkCompact::LiveWordsBitmap<kAlignment>::SetLiveWords(uintptr_t begin,
                                                                        size_t size) {
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

template <size_t kAlignment> template <typename Visitor>
inline void MarkCompact::LiveWordsBitmap<kAlignment>::VisitLiveStrides(uintptr_t begin_bit_idx,
                                                                       const size_t bytes,
                                                                       Visitor&& visitor) const {
  // TODO: we may require passing end addr/end_bit_offset to the function.
  const uintptr_t end_bit_idx = MemRangeBitmap::BitIndexFromAddr(CoverEnd());
  DCHECK_LT(begin_bit_idx, end_bit_idx);
  uintptr_t begin_word_idx = Bitmap::BitIndexToWordIndex(begin_bit_idx);
  const uintptr_t end_word_idx = Bitmap::BitIndexToWordIndex(end_bit_idx);
  DCHECK(Bitmap::TestBit(begin_bit_idx));
  size_t stride_size = 0;
  size_t idx_in_word = 0;
  size_t num_heap_words = bytes / kAlignment;
  uintptr_t live_stride_start_idx;
  uintptr_t word = Bitmap::Begin()[begin_word_idx];

  // Setup the first word.
  word &= ~(Bitmap::BitIndexToMask(begin_bit_idx) - 1);
  begin_bit_idx = RoundDown(begin_bit_idx, Bitmap::kBitsPerBitmapWord);

  do {
    if (UNLIKELY(begin_word_idx == end_word_idx)) {
      word &= Bitmap::BitIndexToMask(end_bit_idx) - 1;
    }
    if (~word == 0) {
      // All bits in the word are marked.
      if (stride_size == 0) {
        live_stride_start_idx = begin_bit_idx;
      }
      stride_size += Bitmap::kBitsPerBitmapWord;
      if (num_heap_words <= stride_size) {
        break;
      }
    } else {
      while (word != 0) {
        // discard 0s
        size_t shift = CTZ(word);
        idx_in_word += shift;
        word >>= shift;
        if (stride_size > 0) {
          if (shift > 0) {
            if (num_heap_words <= stride_size) {
              break;
            }
            visitor(live_stride_start_idx, stride_size, /*is_last*/ false);
            num_heap_words -= stride_size;
            live_stride_start_idx = begin_bit_idx + idx_in_word;
            stride_size = 0;
          }
        } else {
          live_stride_start_idx = begin_bit_idx + idx_in_word;
        }
        // consume 1s
        shift = CTZ(~word);
        DCHECK_NE(shift, 0u);
        word >>= shift;
        idx_in_word += shift;
        stride_size += shift;
      }
      // If the whole word == 0 or the higher bits are 0s, then we exit out of
      // the above loop without completely consuming the word, so call visitor,
      // if needed.
      if (idx_in_word < Bitmap::kBitsPerBitmapWord && stride_size > 0) {
        if (num_heap_words <= stride_size) {
          break;
        }
        visitor(live_stride_start_idx, stride_size, /*is_last*/ false);
        num_heap_words -= stride_size;
        stride_size = 0;
      }
      idx_in_word = 0;
    }
    begin_bit_idx += Bitmap::kBitsPerBitmapWord;
    begin_word_idx++;
    if (UNLIKELY(begin_word_idx > end_word_idx)) {
      num_heap_words = std::min(stride_size, num_heap_words);
      break;
    }
    word = Bitmap::Begin()[begin_word_idx];
  } while (true);

  if (stride_size > 0) {
    visitor(live_stride_start_idx, num_heap_words, /*is_last*/ true);
  }
}

template <size_t kAlignment>
inline
uint32_t MarkCompact::LiveWordsBitmap<kAlignment>::FindNthLiveWordOffset(size_t offset_vec_idx,
                                                                         uint32_t n) const {
  DCHECK_LT(n, kBitsPerVectorWord);
  const size_t index = offset_vec_idx * kBitmapWordsPerVectorWord;
  for (uint32_t i = 0; i < kBitmapWordsPerVectorWord; i++) {
    uintptr_t word = Bitmap::Begin()[index + i];
    if (~word == 0) {
      if (n < Bitmap::kBitsPerBitmapWord) {
        return i * Bitmap::kBitsPerBitmapWord + n;
      }
      n -= Bitmap::kBitsPerBitmapWord;
    } else {
      uint32_t j = 0;
      while (word != 0) {
        // count contiguous 0s
        uint32_t shift = CTZ(word);
        word >>= shift;
        j += shift;
        // count contiguous 1s
        shift = CTZ(~word);
        DCHECK_NE(shift, 0u);
        if (shift > n) {
          return i * Bitmap::kBitsPerBitmapWord + j + n;
        }
        n -= shift;
        word >>= shift;
        j += shift;
      }
    }
  }
  UNREACHABLE();
}

inline void MarkCompact::UpdateRef(mirror::Object* obj, MemberOffset offset) {
  mirror::Object* old_ref = obj->GetFieldObject<
      mirror::Object, kVerifyNone, kWithoutReadBarrier, /*kIsVolatile*/false>(offset);
  mirror::Object* new_ref = PostCompactAddress(old_ref);
  if (new_ref != old_ref) {
    obj->SetFieldObjectWithoutWriteBarrier<
        /*kTransactionActive*/false, /*kCheckTransaction*/false, kVerifyNone, /*kIsVolatile*/false>(
            offset,
            new_ref);
  }
}

inline void MarkCompact::UpdateRoot(mirror::CompressedReference<mirror::Object>* root) {
  DCHECK(!root->IsNull());
  mirror::Object* old_ref = root->AsMirrorPtr();
  mirror::Object* new_ref = PostCompactAddress(old_ref);
  if (old_ref != new_ref) {
    root->Assign(new_ref);
  }
}

template <size_t kAlignment>
inline size_t MarkCompact::LiveWordsBitmap<kAlignment>::CountLiveWordsUpto(size_t bit_idx) const {
  const size_t word_offset = Bitmap::BitIndexToWordIndex(bit_idx);
  uintptr_t word;
  size_t ret = 0;
  // This is needed only if we decide to make offset_vector chunks 128-bit but
  // still choose to use 64-bit word for bitmap. Ideally we should use 128-bit
  // SIMD instructions to compute popcount.
  if (kBitmapWordsPerVectorWord > 1) {
    for (size_t i = RoundDown(word_offset, kBitmapWordsPerVectorWord); i < word_offset; i++) {
      word = Bitmap::Begin()[i];
      ret += POPCOUNT(word);
    }
  }
  word = Bitmap::Begin()[word_offset];
  const uintptr_t mask = Bitmap::BitIndexToMask(bit_idx);
  DCHECK_NE(word & mask, 0u);
  ret += POPCOUNT(word & (mask - 1));
  return ret;
}

inline mirror::Object* MarkCompact::PostCompactAddress(mirror::Object* old_ref) const {
  // TODO: To further speedup the check, maybe we should consider caching heap
  // start/end in this object.
  if (LIKELY(live_words_bitmap_->HasAddress(old_ref))) {
    const uintptr_t begin = live_words_bitmap_->Begin();
    const uintptr_t addr_offset = reinterpret_cast<uintptr_t>(old_ref) - begin;
    const size_t vec_idx = addr_offset / kOffsetChunkSize;
    const size_t live_bytes_in_bitmap_word =
        live_words_bitmap_->CountLiveWordsUpto(addr_offset / kAlignment) * kAlignment;
    return reinterpret_cast<mirror::Object*>(begin
                                             + offset_vector_[vec_idx]
                                             + live_bytes_in_bitmap_word);
  } else {
    return old_ref;
  }
}

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_INL_H_
