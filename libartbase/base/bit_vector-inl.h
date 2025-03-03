/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_BIT_VECTOR_INL_H_
#define ART_LIBARTBASE_BASE_BIT_VECTOR_INL_H_

#include "bit_vector.h"

#include <android-base/logging.h>
#include <cstring>

#include "bit_utils.h"

namespace art {

template <typename StorageType>
inline void BitVectorView<StorageType>::ClearAllBits() {
  // Note: We do not `DCheckTrailingBitsClear()` here as this may be the initial call
  // to clear the storage and the trailing bits may not be clear after allocation.
  memset(storage_, 0, SizeInWords() * sizeof(WordType));
}

template <typename StorageType>
inline void BitVectorView<StorageType>::SetInitialBits(uint32_t num_bits) {
  // Note: We do not `DCheckTrailingBitsClear()` here as this may be the initial call
  // to clear the storage and the trailing bits may not be clear after allocation.
  DCHECK_LE(num_bits, SizeInBits());
  size_t words = WordIndex(num_bits);
  // Set initial full words.
  std::fill_n(storage_, words, std::numeric_limits<WordType>::max());
  if (num_bits % kWordBits != 0) {
    // Set all bits below the first clear bit in the boundary storage word.
    storage_[words] = BitMask(num_bits) - static_cast<StorageType>(1u);
    ++words;
  }
  // Set clear words if any.
  std::fill_n(storage_ + words, SizeInWords() - words, static_cast<StorageType>(0));
}

template <typename StorageType>
inline bool BitVectorView<StorageType>::Union(BitVectorView<const StorageType> union_with) {
  DCHECK_EQ(SizeInBits(), union_with.SizeInBits());
  DCheckTrailingBitsClear();
  union_with.DCheckTrailingBitsClear();
  StorageType added_bits = 0u;
  for (size_t i = 0, size = SizeInWords(); i != size; ++i) {
    StorageType word = storage_[i];
    StorageType union_with_word = union_with.storage_[i];
    storage_[i] = union_with_word | word;
    added_bits |= union_with_word & ~word;
  }
  return added_bits != 0u;
}

template <typename StorageType>
inline bool BitVectorView<StorageType>::UnionIfNotIn(BitVectorView<const StorageType> union_with,
                                                     BitVectorView<const StorageType> not_in) {
  DCHECK_EQ(SizeInBits(), union_with.SizeInBits());
  DCHECK_EQ(SizeInBits(), not_in.SizeInBits());
  DCheckTrailingBitsClear();
  union_with.DCheckTrailingBitsClear();
  not_in.DCheckTrailingBitsClear();
  StorageType added_bits = 0u;
  for (size_t i = 0, size = SizeInWords(); i != size; ++i) {
    StorageType word = storage_[i];
    StorageType union_with_word = union_with.storage_[i] & ~not_in.storage_[i];
    storage_[i] = union_with_word | word;
    added_bits |= union_with_word & ~word;
  }
  return added_bits != 0u;
}

template <typename StorageType>
inline bool BitVectorIndexIterator<StorageType>::operator==(
    const BitVectorIndexIterator<StorageType>& other) const {
  DCHECK_EQ(bit_vector_view_.storage_, other.bit_vector_view_.storage_);
  DCHECK_EQ(bit_vector_view_.size_in_bits_, other.bit_vector_view_.size_in_bits_);
  return bit_index_ == other.bit_index_;
}

template <typename StorageType>
inline bool BitVectorIndexIterator<StorageType>::operator!=(
    const BitVectorIndexIterator<StorageType>& other) const {
  return !(*this == other);
}

template <typename StorageType>
inline size_t BitVectorIndexIterator<StorageType>::operator*() const {
  DCHECK_LT(bit_index_, bit_vector_view_.size_in_bits_);
  return bit_index_;
}

template <typename StorageType>
inline BitVectorIndexIterator<StorageType>& BitVectorIndexIterator<StorageType>::operator++() {
  DCHECK_LT(bit_index_, bit_vector_view_.size_in_bits_);
  bit_index_ = FindIndex(bit_index_ + 1u);
  return *this;
}

template <typename StorageType>
inline BitVectorIndexIterator<StorageType> BitVectorIndexIterator<StorageType>::operator++(int) {
  BitVectorIndexIterator result(*this);
  ++*this;
  return result;
}

template <typename StorageType>
inline size_t BitVectorIndexIterator<StorageType>::FindIndex(size_t start_index) const {
  DCHECK_LE(start_index, bit_vector_view_.size_in_bits_);
  bit_vector_view_.DCheckTrailingBitsClear();
  if (UNLIKELY(start_index == bit_vector_view_.size_in_bits_)) {
    return start_index;
  }
  size_t word_index = start_index / kWordBits;
  DCHECK_LT(word_index, bit_vector_view_.SizeInWords());
  std::remove_const_t<StorageType> word = bit_vector_view_.storage_[word_index];
  // Mask out any bits in the first word we've already considered.
  word &= std::numeric_limits<StorageType>::max() << (start_index % kWordBits);
  if (word == 0u) {
    size_t size_in_words = bit_vector_view_.SizeInWords();
    do {
      ++word_index;
      if (UNLIKELY(word_index == size_in_words)) {
        return bit_vector_view_.size_in_bits_;
      }
      word = bit_vector_view_.storage_[word_index];
    } while (word == 0u);
  }
  return word_index * kWordBits + CTZ(word);
}

template <typename StorageType>
inline BitVectorIndexIterator<StorageType>::BitVectorIndexIterator(
    BitVectorView<StorageType> bit_vector_view, begin_tag)
    : bit_vector_view_(bit_vector_view),
      bit_index_(FindIndex(0u)) { }

template <typename StorageType>
inline BitVectorIndexIterator<StorageType>::BitVectorIndexIterator(
    BitVectorView<StorageType> bit_vector_view, end_tag)
    : bit_vector_view_(bit_vector_view),
      bit_index_(bit_vector_view_.size_in_bits_) { }

template <typename StorageType>
class BitVectorView<StorageType>::IndexContainerImpl {
  static_assert(std::is_const_v<StorageType>);

 public:
  explicit IndexContainerImpl(BitVectorView<StorageType> bit_vector_view)
      : bit_vector_view_(bit_vector_view) { }

  BitVectorIndexIterator<StorageType> begin() const {
    return {bit_vector_view_, typename BitVectorIndexIterator<StorageType>::begin_tag()};
  }

  BitVectorIndexIterator<StorageType> end() const {
    return {bit_vector_view_, typename BitVectorIndexIterator<StorageType>::end_tag()};
  }

 private:
  BitVectorView<StorageType> bit_vector_view_;
};

template <typename StorageType>
BitVectorView<StorageType>::IndexContainer BitVectorView<StorageType>::Indexes() const {
  return BitVectorView<StorageType>::IndexContainer(*this);
}

inline void BitVector::ClearAllBits() {
  AsView().ClearAllBits();
}

inline bool BitVector::Equal(const BitVector* src) const {
  return (storage_size_ == src->GetStorageSize()) &&
    (expandable_ == src->IsExpandable()) &&
    (memcmp(storage_, src->GetRawStorage(), storage_size_ * sizeof(uint32_t)) == 0);
}

inline BitVector::IndexContainer BitVector::Indexes() const {
  return AsView().Indexes();
}

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_BIT_VECTOR_INL_H_
