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
  // TODO: optimize by using memset. Sometimes this function may get called with
  // large ranges.
  for (; bm_address < end_bm_address; bm_address++) {
    *bm_address |= mask;
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
                                                                       uint8_t* end,
                                                                       const size_t bytes,
                                                                       Visitor&& visitor) const {
  DCHECK(IsAligned<kAlignment>(end));
  // We have to use the unchecked version of BitIndexFromAddr() as 'end' could
  // be outside the range. Do explicit check here.
  DCHECK_LE(reinterpret_cast<uintptr_t>(end), MemRangeBitmap::CoverEnd());
  const uintptr_t end_bit_idx =
      MemRangeBitmap::BitIndexFromAddrUnchecked(reinterpret_cast<uintptr_t>(end));
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
uint32_t MarkCompact::LiveWordsBitmap<kAlignment>::FindNthLiveWordOffset(size_t chunk_idx,
                                                                         uint32_t n) const {
  DCHECK_LT(n, kBitsPerVectorWord);
  const size_t index = chunk_idx * kBitmapWordsPerVectorWord;
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
  if (kIsDebugBuild) {
    if (live_words_bitmap_->HasAddress(old_ref)
        && reinterpret_cast<uint8_t*>(old_ref) < black_allocations_begin_
        && !current_space_bitmap_->Test(old_ref)) {
      mirror::Object* from_ref = GetFromSpaceAddr(old_ref);
      std::ostringstream oss;
      heap_->DumpSpaces(oss);
      MemMap::DumpMaps(oss, /* terse= */ true);
      LOG(FATAL) << "Not marked in the bitmap ref=" << old_ref
                 << " from_ref=" << from_ref
                 << " offset=" << offset
                 << " obj=" << obj
                 << " obj-validity=" << IsValidObject(obj)
                 << " from-space=" << static_cast<void*>(from_space_begin_)
                 << " bitmap= " << current_space_bitmap_->DumpMemAround(old_ref)
                 << " from_ref "
                 << heap_->GetVerification()->DumpRAMAroundAddress(
                     reinterpret_cast<uintptr_t>(from_ref), 128)
                 << " obj "
                 << heap_->GetVerification()->DumpRAMAroundAddress(
                     reinterpret_cast<uintptr_t>(obj), 128)
                 << " old_ref " << heap_->GetVerification()->DumpRAMAroundAddress(
                     reinterpret_cast<uintptr_t>(old_ref), 128)
                 << " maps\n" << oss.str();
    }
  }
  mirror::Object* new_ref = PostCompactAddress(old_ref);
  if (new_ref != old_ref) {
    obj->SetFieldObjectWithoutWriteBarrier<
        /*kTransactionActive*/false, /*kCheckTransaction*/false, kVerifyNone, /*kIsVolatile*/false>(
            offset,
            new_ref);
  }
}

inline bool MarkCompact::VerifyRootSingleUpdate(void* root,
                                                mirror::Object* old_ref,
                                                const RootInfo& info) {
  void* stack_end = stack_end_;
  void* stack_addr = stack_addr_;
  if (!live_words_bitmap_->HasAddress(old_ref)) {
    return false;
  }
  if (stack_end == nullptr) {
    pthread_attr_t attr;
    size_t stack_size;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);
    stack_end = reinterpret_cast<char*>(stack_addr) + stack_size;
  }
  if (root < stack_addr || root > stack_end) {
    auto ret = updated_roots_.insert(root);
    DCHECK(ret.second) << "root=" << root
                       << " old_ref=" << old_ref
                       << " stack_addr=" << stack_addr
                       << " stack_end=" << stack_end;
  }
  DCHECK(reinterpret_cast<uint8_t*>(old_ref) >= black_allocations_begin_
         || live_words_bitmap_->Test(old_ref))
        << "ref=" << old_ref
        << " RootInfo=" << info;
  return true;
}

inline void MarkCompact::UpdateRoot(mirror::CompressedReference<mirror::Object>* root,
                                    const RootInfo& info) {
  DCHECK(!root->IsNull());
  mirror::Object* old_ref = root->AsMirrorPtr();
  if (VerifyRootSingleUpdate(root, old_ref, info)) {
    mirror::Object* new_ref = PostCompactAddress(old_ref);
    if (old_ref != new_ref) {
      root->Assign(new_ref);
    }
  }
}

inline void MarkCompact::UpdateRoot(mirror::Object** root, const RootInfo& info) {
  mirror::Object* old_ref = *root;
  if (VerifyRootSingleUpdate(root, old_ref, info)) {
    mirror::Object* new_ref = PostCompactAddress(old_ref);
    if (old_ref != new_ref) {
      *root = new_ref;
    }
  }
}

template <size_t kAlignment>
inline size_t MarkCompact::LiveWordsBitmap<kAlignment>::CountLiveWordsUpto(size_t bit_idx) const {
  const size_t word_offset = Bitmap::BitIndexToWordIndex(bit_idx);
  uintptr_t word;
  size_t ret = 0;
  // This is needed only if we decide to make chunks 128-bit but still
  // choose to use 64-bit word for bitmap. Ideally we should use 128-bit
  // SIMD instructions to compute popcount.
  if (kBitmapWordsPerVectorWord > 1) {
    for (size_t i = RoundDown(word_offset, kBitmapWordsPerVectorWord); i < word_offset; i++) {
      word = Bitmap::Begin()[i];
      ret += POPCOUNT(word);
    }
  }
  word = Bitmap::Begin()[word_offset];
  const uintptr_t mask = Bitmap::BitIndexToMask(bit_idx);
  DCHECK_NE(word & mask, 0u)
        << " word_offset:" << word_offset
        << " bit_idx:" << bit_idx
        << " bit_idx_in_word:" << (bit_idx % Bitmap::kBitsPerBitmapWord)
        << std::hex << " word: 0x" << word
        << " mask: 0x" << mask << std::dec;
  ret += POPCOUNT(word & (mask - 1));
  return ret;
}

inline mirror::Object* MarkCompact::PostCompactBlackObjAddr(mirror::Object* old_ref) const {
  return reinterpret_cast<mirror::Object*>(reinterpret_cast<uint8_t*>(old_ref)
                                           - black_objs_slide_diff_);
}

inline mirror::Object* MarkCompact::PostCompactOldObjAddr(mirror::Object* old_ref) const {
  const uintptr_t begin = live_words_bitmap_->Begin();
  const uintptr_t addr_offset = reinterpret_cast<uintptr_t>(old_ref) - begin;
  const size_t vec_idx = addr_offset / kOffsetChunkSize;
  const size_t live_bytes_in_bitmap_word =
      live_words_bitmap_->CountLiveWordsUpto(addr_offset / kAlignment) * kAlignment;
  return reinterpret_cast<mirror::Object*>(begin
                                           + chunk_info_vec_[vec_idx]
                                           + live_bytes_in_bitmap_word);
}

inline mirror::Object* MarkCompact::PostCompactAddressUnchecked(mirror::Object* old_ref) const {
  if (reinterpret_cast<uint8_t*>(old_ref) >= black_allocations_begin_) {
    return PostCompactBlackObjAddr(old_ref);
  }
  if (kIsDebugBuild) {
    mirror::Object* from_ref = GetFromSpaceAddr(old_ref);
    DCHECK(live_words_bitmap_->Test(old_ref))
         << "ref=" << old_ref;
    if (!current_space_bitmap_->Test(old_ref)) {
      std::ostringstream oss;
      Runtime::Current()->GetHeap()->DumpSpaces(oss);
      MemMap::DumpMaps(oss, /* terse= */ true);
      LOG(FATAL) << "ref=" << old_ref
                 << " from_ref=" << from_ref
                 << " from-space=" << static_cast<void*>(from_space_begin_)
                 << " bitmap= " << current_space_bitmap_->DumpMemAround(old_ref)
                 << heap_->GetVerification()->DumpRAMAroundAddress(
                         reinterpret_cast<uintptr_t>(from_ref), 128)
                 << " maps\n" << oss.str();
    }
  }
  return PostCompactOldObjAddr(old_ref);
}

inline mirror::Object* MarkCompact::PostCompactAddress(mirror::Object* old_ref) const {
  // TODO: To further speedup the check, maybe we should consider caching heap
  // start/end in this object.
  if (LIKELY(live_words_bitmap_->HasAddress(old_ref))) {
    return PostCompactAddressUnchecked(old_ref);
  }
  return old_ref;
}

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_INL_H_
