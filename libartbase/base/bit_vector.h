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

#ifndef ART_LIBARTBASE_BASE_BIT_VECTOR_H_
#define ART_LIBARTBASE_BASE_BIT_VECTOR_H_

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <limits>

#include "bit_utils.h"
#include "globals.h"
#include "logging.h"

namespace art {

class Allocator;

// A bit vector view encapsulating externally-provided fixed-size storage for bits.
//
// The size in bits does not need to specify whole number of storage words but the view
// is intended to work only on the specified number of bits. Single-bit functions
// `SetBit()`, `ClearBit()` and `IsBitSet()` verify the passed index with `DCHECK()`
// and do not care about trailing bits in the last storage word, if any. Multi-bit
// functions require that the trailing bits are cleared on entry, except for functions
// `ClearAllBits()` and `SetInitialBits()` that are used for storage initialization
// and clear the trailing bits, if any.
template <typename StorageType = size_t>
class BitVectorView {
 public:
  using WordType = StorageType;
  static_assert(std::numeric_limits<WordType>::is_integer);
  static_assert(!std::numeric_limits<WordType>::is_signed);
  static constexpr size_t kWordBits = BitSizeOf<WordType>();
  static_assert(IsPowerOfTwo(kWordBits));

  static constexpr size_t BitsToWords(size_t bits) {
    return (bits + /* round up */ (kWordBits - 1)) / kWordBits;
  }

  // Construct an empty `BitVectorView`.
  constexpr BitVectorView()
      : storage_(nullptr), size_in_bits_(0u) {}

  // Construct a `BitVectorView` referencing the provided backing storage.
  constexpr BitVectorView(WordType* storage, size_t size_in_bits)
      : storage_(storage), size_in_bits_(size_in_bits) {}

  // The `BitVectorView<>` can be copied and passed to functions by value.
  // The new copy shall reference the same underlying data, similarly to `std::string_view`.
  BitVectorView(const BitVectorView& src) = default;

  // Implicit conversion to a view with constant storage.
  template <typename ST,
            typename = std::enable_if_t<std::is_const_v<StorageType> &&
                                        std::is_same_v<ST, std::remove_const_t<StorageType>>>>
  BitVectorView(const BitVectorView<ST>& src)
      : storage_(src.storage_), size_in_bits_(src.size_in_bits_) {}

  // Get the size of the bit vector view in bits.
  constexpr size_t SizeInBits() const {
    return size_in_bits_;
  }

  // Get the size of the bit vector view in storage words.
  constexpr size_t SizeInWords() const {
    return BitsToWords(SizeInBits());
  }

  // Mark the specified bit as "set".
  void SetBit(size_t index) {
    DCHECK_LT(index, size_in_bits_);
    storage_[WordIndex(index)] |= BitMask(index);
  }

  // Mark the specified bit as "clear".
  void ClearBit(size_t index) {
    DCHECK_LT(index, size_in_bits_);
    storage_[WordIndex(index)] &= ~BitMask(index);
  }

  // Determine whether or not the specified bit is set.
  constexpr bool IsBitSet(size_t index) const {
    DCHECK_LT(index, size_in_bits_);
    return (storage_[WordIndex(index)] & BitMask(index)) != 0u;
  }

  // Mark all bits as "clear".
  void ClearAllBits();

  // Mark specified number of initial bits as "set" and clear all bits after that.
  void SetInitialBits(uint32_t num_bits);

  // Return true if there are any bits set, false otherwise.
  bool IsAnyBitSet() const {
    DCheckTrailingBitsClear();
    return std::any_of(storage_, storage_ + SizeInWords(), [](WordType w) { return w != 0u; });
  }

  // Union with another bit vector view of the same size.
  bool Union(BitVectorView<const StorageType> union_with);

  // Union with the bits in `union_with` but not in `not_in`. All views must have the same size.
  bool UnionIfNotIn(BitVectorView<const StorageType> union_with,
                    BitVectorView<const StorageType> not_in);

  // `BitVectorView` wrapper class for iteration across indexes of set bits.
  class IndexContainerImpl;
  using IndexContainer = BitVectorView<const StorageType>::IndexContainerImpl;

  IndexContainer Indexes() const;

 private:
  static constexpr size_t WordIndex(size_t index) {
    return index >> WhichPowerOf2(kWordBits);
  }

  static constexpr WordType BitMask(size_t index) {
    return static_cast<WordType>(1) << (index % kWordBits);
  }

  constexpr void DCheckTrailingBitsClear() const {
    DCHECK_IMPLIES(SizeInBits() % kWordBits != 0u,
                   (storage_[WordIndex(SizeInBits())] & ~(BitMask(SizeInBits()) - 1u)) == 0u);
  }

  WordType* storage_;
  size_t size_in_bits_;

  template <typename ST> friend class BitVectorIndexIterator;
  template <typename ST> friend class BitVectorView;
};

/**
 * @brief Convenient iterator across the indexes of the bits in `BitVector` or `BitVectorView<>`.
 *
 * @details BitVectorIndexIterator is a Forward iterator (C++11: 24.2.5) from the lowest
 * to the highest index of the BitVector's set bits. Instances can be retrieved
 * only through `BitVector{,View}::Indexes()` which return an index container wrapper
 * object with begin() and end() suitable for range-based loops:
 *   for (uint32_t idx : bit_vector.Indexes()) {
 *     // Use idx.
 *   }
 */
template <typename StorageType>
class BitVectorIndexIterator {
  static_assert(std::is_const_v<StorageType>);

 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = void;
  using reference = void;

  bool operator==(const BitVectorIndexIterator& other) const;
  bool operator!=(const BitVectorIndexIterator& other) const;

  size_t operator*() const;

  BitVectorIndexIterator& operator++();
  BitVectorIndexIterator operator++(int);

  // Helper function to check for end without comparing with bit_vector.Indexes().end().
  bool Done() const {
    return bit_index_ == bit_vector_view_.SizeInBits();
  }

 private:
  struct begin_tag { };
  struct end_tag { };

  BitVectorIndexIterator(BitVectorView<StorageType> bit_vector_view, begin_tag);
  BitVectorIndexIterator(BitVectorView<StorageType> bit_vector_view, end_tag);

  size_t FindIndex(size_t start_index) const;

  static constexpr size_t kWordBits = BitVectorView<StorageType>::kWordBits;

  const BitVectorView<StorageType> bit_vector_view_;
  size_t bit_index_;  // Current index (size in bits).

  template <typename ST>
  friend class BitVectorView;
};

/*
 * Expanding bitmap. Bits are numbered starting from zero. All operations on a BitVector are
 * unsynchronized. New BitVectors are not necessarily zeroed out. If the used allocator doesn't do
 * clear the vector (e.g. ScopedArenaAllocator), the responsibility of clearing it relies on the
 * caller (e.g. ArenaBitVector).
 */
class BitVector {
 public:
  static constexpr uint32_t kWordBytes = sizeof(uint32_t);
  static constexpr uint32_t kWordBits = kWordBytes * 8;

  using IndexContainer = BitVectorView<uint32_t>::IndexContainer;

  // MoveConstructible but not MoveAssignable, CopyConstructible or CopyAssignable.

  BitVector(const BitVector& other) = delete;
  BitVector& operator=(const BitVector& other) = delete;

  BitVector(BitVector&& other) noexcept
      : storage_(other.storage_),
        storage_size_(other.storage_size_),
        allocator_(other.allocator_),
        expandable_(other.expandable_) {
    other.storage_ = nullptr;
    other.storage_size_ = 0u;
  }

  BitVector(uint32_t start_bits,
            bool expandable,
            Allocator* allocator);

  BitVector(bool expandable,
            Allocator* allocator,
            uint32_t storage_size,
            uint32_t* storage);

  BitVector(const BitVector& src,
            bool expandable,
            Allocator* allocator);

  virtual ~BitVector();

  // The number of words necessary to encode bits.
  static constexpr uint32_t BitsToWords(uint32_t bits) {
    return RoundUp(bits, kWordBits) / kWordBits;
  }

  // Mark the specified bit as "set".
  void SetBit(uint32_t idx) {
    /*
     * TUNING: this could have pathologically bad growth/expand behavior.  Make sure we're
     * not using it badly or change resize mechanism.
     */
    if (idx >= storage_size_ * kWordBits) {
      EnsureSize(idx);
    }
    AsView().SetBit(idx);
  }

  // Mark the specified bit as "clear".
  void ClearBit(uint32_t idx) {
    // If the index is over the size, we don't have to do anything, it is cleared.
    if (idx < storage_size_ * kWordBits) {
      // Otherwise, go ahead and clear it.
      AsView().ClearBit(idx);
    }
  }

  // Determine whether or not the specified bit is set.
  bool IsBitSet(uint32_t idx) const {
    // If the index is over the size, whether it is expandable or not, this bit does not exist:
    // thus it is not set.
    return (idx < (storage_size_ * kWordBits)) && AsView().IsBitSet(idx);
  }

  // Mark all bits as "clear".
  void ClearAllBits();

  // Mark specified number of bits as "set". Cannot set all bits like ClearAll since there might
  // be unused bits - setting those to one will confuse the iterator.
  void SetInitialBits(uint32_t num_bits);

  void Copy(const BitVector* src);

  // Intersect with another bit vector.
  void Intersect(const BitVector* src2);

  // Union with another bit vector.
  bool Union(const BitVector* src);

  // Set bits of union_with that are not in not_in.
  bool UnionIfNotIn(const BitVector* union_with, const BitVector* not_in);

  void Subtract(const BitVector* src);

  // Are we equal to another bit vector?  Note: expandability attributes must also match.
  bool Equal(const BitVector* src) const;

  /**
   * @brief Are all the bits set the same?
   * @details expandability and size can differ as long as the same bits are set.
   */
  bool SameBitsSet(const BitVector *src) const;

  bool IsSubsetOf(const BitVector *other) const;

  // Count the number of bits that are set.
  uint32_t NumSetBits() const;

  // Count the number of bits that are set in range [0, end).
  uint32_t NumSetBits(uint32_t end) const;

  IndexContainer Indexes() const;

  uint32_t GetStorageSize() const {
    return storage_size_;
  }

  bool IsExpandable() const {
    return expandable_;
  }

  uint32_t GetRawStorageWord(size_t idx) const {
    return storage_[idx];
  }

  uint32_t* GetRawStorage() {
    return storage_;
  }

  const uint32_t* GetRawStorage() const {
    return storage_;
  }

  size_t GetSizeOf() const {
    return storage_size_ * kWordBytes;
  }

  size_t GetBitSizeOf() const {
    return storage_size_ * kWordBits;
  }

  /**
   * @return the highest bit set, -1 if none are set
   */
  int GetHighestBitSet() const;

  /**
   * @return true if there are any bits set, false otherwise.
   */
  bool IsAnyBitSet() const {
    return AsView().IsAnyBitSet();
  }

  // Minimum number of bits required to store this vector, 0 if none are set.
  size_t GetNumberOfBits() const {
    return GetHighestBitSet() + 1;
  }

  // Is bit set in storage. (No range check.)
  static bool IsBitSet(const uint32_t* storage, uint32_t idx) {
    return (storage[WordIndex(idx)] & BitMask(idx)) != 0;
  }

  // Number of bits set in range [0, end) in storage. (No range check.)
  static uint32_t NumSetBits(const uint32_t* storage, uint32_t end);

  // Fill given memory region with the contents of the vector and zero padding.
  void CopyTo(void* dst, size_t len) const {
    DCHECK_LE(static_cast<size_t>(GetHighestBitSet() + 1), len * kBitsPerByte);
    size_t vec_len = GetSizeOf();
    if (vec_len < len) {
      void* dst_padding = reinterpret_cast<uint8_t*>(dst) + vec_len;
      memcpy(dst, storage_, vec_len);
      memset(dst_padding, 0, len - vec_len);
    } else {
      memcpy(dst, storage_, len);
    }
  }

  void Dump(std::ostream& os, const char* prefix) const;

  Allocator* GetAllocator() const;

 private:
  /**
   * @brief Dump the bitvector into buffer in a 00101..01 format.
   * @param buffer the ostringstream used to dump the bitvector into.
   */
  void DumpHelper(const char* prefix, std::ostringstream& buffer) const;

  BitVectorView<uint32_t> AsView() {
    return {storage_, storage_size_ * kWordBits};
  }

  BitVectorView<const uint32_t> AsView() const {
    return {storage_, storage_size_ * kWordBits};
  }

  // Ensure there is space for a bit at idx.
  void EnsureSize(uint32_t idx);

  // The index of the word within storage.
  static constexpr uint32_t WordIndex(uint32_t idx) {
    return idx >> 5;
  }

  // A bit mask to extract the bit for the given index.
  static constexpr uint32_t BitMask(uint32_t idx) {
    return 1 << (idx & 0x1f);
  }

  uint32_t*  storage_;            // The storage for the bit vector.
  uint32_t   storage_size_;       // Current size, in 32-bit words.
  Allocator* const allocator_;    // Allocator if expandable.
  const bool expandable_;         // Should the bitmap expand if too small?
};

}  // namespace art

#endif  // ART_LIBARTBASE_BASE_BIT_VECTOR_H_
