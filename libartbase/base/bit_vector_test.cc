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

#include <memory>
#include <random>
#include <vector>

#include "allocator.h"
#include "base/stl_util.h"
#include "bit_vector-inl.h"
#include "gtest/gtest.h"
#include "transform_iterator.h"

namespace art {

template <typename StorageType, StorageType kWord0, StorageType kWord1>
void TestBitVectorViewSetBitAndClearBit() {
  static constexpr StorageType kStorage[2] = { kWord0, kWord1 };
  static constexpr size_t kSizeInBits = 2 * BitSizeOf<StorageType>();
  static constexpr BitVectorView<const StorageType> kBvv(kStorage, kSizeInBits);
  auto get_bit_from_params = [](size_t index) constexpr {
    StorageType word = (index < BitSizeOf<StorageType>()) ? kWord0 : kWord1;
    size_t shift = index % BitSizeOf<StorageType>();
    return (word & (static_cast<StorageType>(1u) << shift)) != 0u;
  };
  auto verify_is_bit_set = [get_bit_from_params]() constexpr {
    for (size_t index = 0; index != kSizeInBits; ++index) {
      // If the `CHECK_EQ()` fails, the `static_assert` evaluation fails at compile time.
      CHECK_EQ(get_bit_from_params(index), kBvv.IsBitSet(index)) << index;
    }
    return true;
  };
  static_assert(verify_is_bit_set());

  auto verify_size = []() constexpr {
    for (size_t size = 0; size != kSizeInBits; ++size) {
      // If the `CHECK_EQ()` fails, the `static_assert` evaluation fails at compile time.
      CHECK_EQ(size, BitVectorView(kStorage, size).SizeInBits());
      size_t words = RoundUp(size, BitSizeOf<StorageType>()) / BitSizeOf<StorageType>();
      CHECK_EQ(words, BitVectorView(kStorage, size).SizeInWords());
    }
    return true;
  };
  static_assert(verify_size());

  StorageType storage[2] = {0u, 0u};
  size_t size_in_bits = 2 * BitSizeOf<StorageType>();
  BitVectorView<StorageType> bvv(storage, size_in_bits);
  for (size_t index = 0; index != size_in_bits; ++index) {
    ASSERT_FALSE(bvv.IsBitSet(index));
  }
  // Set one bit at a time, then clear it.
  for (size_t bit_to_set = 0; bit_to_set != size_in_bits; ++bit_to_set) {
    bvv.SetBit(bit_to_set);
    for (size_t index = 0; index != size_in_bits; ++index) {
      ASSERT_EQ(index == bit_to_set, bvv.IsBitSet(index));
    }
    ASSERT_TRUE(bvv.IsAnyBitSet());
    bvv.ClearBit(bit_to_set);
    for (size_t index = 0; index != size_in_bits; ++index) {
      ASSERT_FALSE(bvv.IsBitSet(index));
    }
    ASSERT_FALSE(bvv.IsAnyBitSet());
  }
  // Set bits for `kWord0` and `kWord1`.
  for (size_t index = 0; index != size_in_bits; ++index) {
    if (get_bit_from_params(index)) {
      bvv.SetBit(index);
    }
  }
  ASSERT_EQ(kWord0, storage[0]);
  ASSERT_EQ(kWord1, storage[1]);
  // Clear all bits that are already clear.
  for (size_t index = 0; index != size_in_bits; ++index) {
    if (!get_bit_from_params(index)) {
      bvv.ClearBit(index);
    }
  }
  ASSERT_EQ(kWord0, storage[0]);
  ASSERT_EQ(kWord1, storage[1]);
  // Clear all bits that are set.
  for (size_t index = 0; index != size_in_bits; ++index) {
    if (get_bit_from_params(index)) {
      bvv.ClearBit(index);
    }
  }
  ASSERT_EQ(0u, storage[0]);
  ASSERT_EQ(0u, storage[1]);
}

TEST(BitVectorView, Uint32T) {
  TestBitVectorViewSetBitAndClearBit<uint32_t, 0x12345678u, 0x87654321u>();
}

TEST(BitVectorView, Uint64T) {
  TestBitVectorViewSetBitAndClearBit<uint64_t,
                                     UINT64_C(0x1234567890abcdef),
                                     UINT64_C(0xfedcba0987654321)>();
}

TEST(BitVectorView, SizeT) {
  // Note: The constants below are truncated on 32-bit architectures.
  TestBitVectorViewSetBitAndClearBit<size_t,
                                     static_cast<size_t>(UINT64_C(0xfedcba0987654321)),
                                     static_cast<size_t>(UINT64_C(0x1234567890abcdef))>();
}

TEST(BitVectorView, ConversionToConstStorage) {
  uint32_t storage[] = {1u, 2u, 3u};
  size_t size = 2 * BitSizeOf<uint32_t>() + MinimumBitsToStore(storage[2]);
  BitVectorView<uint32_t> bvv(storage, size);
  auto is_bit_set = [](BitVectorView<const uint32_t> cbvv, size_t index) {
    return cbvv.IsBitSet(index);
  };
  for (size_t index = 0; index != size; ++index) {
    ASSERT_EQ(bvv.IsBitSet(index), is_bit_set(bvv, index));
  }
}

TEST(BitVectorView, DefaultConstructor) {
  BitVectorView<> bvv;
  ASSERT_EQ(0u, bvv.SizeInBits());
  ASSERT_EQ(0u, bvv.SizeInWords());
}

TEST(BitVectorView, ClearAllBits) {
  uint32_t storage[] = {1u, 2u, 0xffffffffu};
  size_t size = 2 * BitSizeOf<uint32_t>() + 1u;
  BitVectorView<uint32_t> bvv(storage, size);  // Construction allowed with bogus trailing bits.
  ASSERT_EQ(1u, storage[0]);
  ASSERT_EQ(2u, storage[1]);
  ASSERT_EQ(0xffffffffu, storage[2]);
  bvv.ClearAllBits();
  ASSERT_EQ(0u, storage[0]);
  ASSERT_EQ(0u, storage[1]);
  ASSERT_EQ(0u, storage[2]);
}

TEST(BitVectorView, SetInitialBits) {
  uint32_t storage[] = {1u, 2u, 0xffffffffu};
  size_t size = 2 * BitSizeOf<uint32_t>() + 1u;
  BitVectorView<uint32_t> bvv(storage, size);  // Construction allowed with bogus trailing bits.
  ASSERT_EQ(1u, storage[0]);
  ASSERT_EQ(2u, storage[1]);
  ASSERT_EQ(0xffffffffu, storage[2]);
  bvv.SetInitialBits(40u);
  ASSERT_EQ(0xffffffffu, storage[0]);
  ASSERT_EQ(0xffu, storage[1]);
  ASSERT_EQ(0u, storage[2]);
  bvv.SetInitialBits(0u);
  ASSERT_EQ(0u, storage[0]);
  ASSERT_EQ(0u, storage[1]);
  ASSERT_EQ(0u, storage[2]);
  bvv.SetInitialBits(17u);
  ASSERT_EQ(0x1ffffu, storage[0]);
  ASSERT_EQ(0u, storage[1]);
  ASSERT_EQ(0u, storage[2]);
  bvv.SetInitialBits(64u);
  ASSERT_EQ(0xffffffffu, storage[0]);
  ASSERT_EQ(0xffffffffu, storage[1]);
  ASSERT_EQ(0u, storage[2]);
  bvv.SetInitialBits(65u);
  ASSERT_EQ(0xffffffffu, storage[0]);
  ASSERT_EQ(0xffffffffu, storage[1]);
  ASSERT_EQ(1u, storage[2]);
}

template <typename StorageType, StorageType kWord0, StorageType kWord1>
void TestBitVectorViewIndexes() {
  StorageType storage[] = {kWord0, kWord1};
  size_t size = 2u * BitSizeOf<StorageType>();
  BitVectorView bvv(storage, size);

  std::vector<size_t> indexes1;
  for (size_t index = 0; index != size; ++index) {
    if (bvv.IsBitSet(index)) {
      indexes1.push_back(index);
    }
  }

  std::vector<size_t> indexes2;
  for (size_t index : bvv.Indexes()) {
    indexes2.push_back(index);
  }
  ASSERT_EQ(indexes1, indexes2);

  std::vector<size_t> indexes3;
  for (auto it = bvv.Indexes().begin(); !it.Done(); ++it) {
    indexes3.push_back(*it);
  }
  ASSERT_EQ(indexes1, indexes3);

  StorageType empty_storage[] = {0u, 0u, 0u};
  BitVectorView empty(empty_storage, 3 * BitSizeOf<StorageType>() - 1u);
  for (size_t index : empty.Indexes()) {
    FAIL();
  }
  ASSERT_TRUE(empty.Indexes().begin().Done());
}

TEST(BitVectorView, IndexesUint32T) {
  TestBitVectorViewIndexes<uint32_t, 0x12345678u, 0x87654321u>();
}

TEST(BitVectorView, IndexesUint64T) {
  TestBitVectorViewIndexes<uint64_t,
                           UINT64_C(0x1234567890abcdef),
                           UINT64_C(0xfedcba0987654321)>();
}

TEST(BitVectorView, IndexesSizeT) {
  // Note: The constants below are truncated on 32-bit architectures.
  TestBitVectorViewIndexes<size_t,
                           static_cast<size_t>(UINT64_C(0xfedcba0987654321)),
                           static_cast<size_t>(UINT64_C(0x1234567890abcdef))>();
}

template <typename StorageType>
void TestBitVectorViewUnion() {
  // Truncated if the constants do not fit in `StorageType`.
  static constexpr StorageType kInitWord0 = static_cast<StorageType>(UINT64_C(0xfedcba0987654321));
  static constexpr StorageType kInitWord1 = static_cast<StorageType>(UINT64_C(0x1234567890abcdef));
  StorageType storage[] = { kInitWord0, kInitWord1 };
  size_t size = 2u * BitSizeOf<StorageType>();
  BitVectorView<StorageType> bvv(storage, size);

  StorageType equal_storage[] = { kInitWord0, kInitWord1 };
  BitVectorView<StorageType> equal_bvv(equal_storage, size);
  ASSERT_FALSE(bvv.Union(equal_bvv));
  ASSERT_EQ(kInitWord0, storage[0]);
  ASSERT_EQ(kInitWord1, storage[1]);

  StorageType mask = static_cast<StorageType>(UINT64_C(0x5555555555555555));
  StorageType subset_storage[] = { kInitWord0 & mask, kInitWord1 & mask };
  BitVectorView<StorageType> subset_bvv(subset_storage, size);
  ASSERT_FALSE(bvv.Union(subset_bvv));
  ASSERT_EQ(kInitWord0, storage[0]);
  ASSERT_EQ(kInitWord1, storage[1]);

  static constexpr StorageType kOtherWord0 = kInitWord1;
  static constexpr StorageType kOtherWord1 = kInitWord0;
  StorageType other_storage[] = { kOtherWord0, kOtherWord1 };
  BitVectorView<StorageType> other_bvv(other_storage, size);
  ASSERT_TRUE(bvv.Union(other_bvv));
  ASSERT_EQ(kInitWord0 | kOtherWord0, storage[0]);
  ASSERT_EQ(kInitWord1 | kOtherWord1, storage[1]);
}

TEST(BitVectorView, UnionUint32T) {
  TestBitVectorViewUnion<uint32_t>();
}

TEST(BitVectorView, UnionUint64T) {
  TestBitVectorViewUnion<uint64_t>();
}

TEST(BitVectorView, UnionSizeT) {
  // Note: The constants below are truncated on 32-bit architectures.
  TestBitVectorViewUnion<size_t>();
}

template <typename StorageType>
void TestBitVectorViewUnionIfNotIn() {
  // Truncated if the constants do not fit in `StorageType`.
  static constexpr StorageType kInitWord0 = static_cast<StorageType>(UINT64_C(0xfedcba0987654321));
  static constexpr StorageType kInitWord1 = static_cast<StorageType>(UINT64_C(0x1234567890abcdef));
  StorageType storage[] = { kInitWord0, kInitWord1 };
  size_t size = 2u * BitSizeOf<StorageType>();
  BitVectorView<StorageType> bvv(storage, size);
  StorageType equal_storage[] = { kInitWord0, kInitWord1 };
  BitVectorView<StorageType> equal_bvv(equal_storage, size);
  StorageType mask = static_cast<StorageType>(UINT64_C(0x5555555555555555));
  StorageType subset_storage[] = { kInitWord0 & mask, kInitWord1 & mask };
  BitVectorView<StorageType> subset_bvv(subset_storage, size);
  StorageType empty_storage[] = { 0u, 0u };
  BitVectorView<StorageType> empty_bvv(subset_storage, size);
  static constexpr StorageType kOtherWord0 = kInitWord1;
  static constexpr StorageType kOtherWord1 = kInitWord0;
  StorageType other_storage[] = { kOtherWord0, kOtherWord1 };
  BitVectorView<StorageType> other_bvv(other_storage, size);
  StorageType mask_storage[] = { mask, mask };
  BitVectorView<StorageType> mask_bvv(mask_storage, size);

  // Test cases where we add bits and the `not_in` is relevant.
  ASSERT_TRUE(bvv.UnionIfNotIn(other_bvv, mask_bvv));
  ASSERT_EQ(kInitWord0 | (kOtherWord0 & ~mask), storage[0]);
  ASSERT_EQ(kInitWord1 | (kOtherWord1 & ~mask), storage[1]);
  storage[0] = kInitWord0;  // Reset `bvv` storage.
  storage[1] = kInitWord1;
  ASSERT_TRUE(bvv.UnionIfNotIn(mask_bvv, other_bvv));
  ASSERT_EQ(kInitWord0 | (mask & ~kOtherWord0), storage[0]);
  ASSERT_EQ(kInitWord1 | (mask & ~kOtherWord1), storage[1]);
  storage[0] = kInitWord0;  // Reset `bvv` storage.
  storage[1] = kInitWord1;

  // Test cases where we add bits but the `not_in` is irrelevant because it's a subset of `bvv`.
  for (BitVectorView<StorageType> not_in : { equal_bvv, subset_bvv, empty_bvv }) {
    ASSERT_TRUE(bvv.UnionIfNotIn(other_bvv, not_in));
    ASSERT_EQ(kInitWord0 | kOtherWord0, storage[0]);
    ASSERT_EQ(kInitWord1 | kOtherWord1, storage[1]);
    storage[0] = kInitWord0;  // Reset `bvv` storage.
    storage[1] = kInitWord1;
    ASSERT_TRUE(bvv.UnionIfNotIn(mask_bvv, not_in));
    ASSERT_EQ(kInitWord0 | mask, storage[0]);
    ASSERT_EQ(kInitWord1 | mask, storage[1]);
    storage[0] = kInitWord0;  // Reset `bvv` storage.
    storage[1] = kInitWord1;
  }

  // Test various cases where we add no bits.
  for (BitVectorView<StorageType> union_with : { equal_bvv, subset_bvv, empty_bvv }) {
    for (BitVectorView<StorageType> not_in :
             { equal_bvv, subset_bvv, empty_bvv, other_bvv, mask_bvv }) {
      ASSERT_FALSE(bvv.UnionIfNotIn(union_with, not_in));
      ASSERT_EQ(kInitWord0, storage[0]);
      ASSERT_EQ(kInitWord1, storage[1]);
    }
  }
  ASSERT_FALSE(bvv.UnionIfNotIn(other_bvv, other_bvv));
  ASSERT_EQ(kInitWord0, storage[0]);
  ASSERT_EQ(kInitWord1, storage[1]);
  ASSERT_FALSE(bvv.UnionIfNotIn(mask_bvv, mask_bvv));
  ASSERT_EQ(kInitWord0, storage[0]);
  ASSERT_EQ(kInitWord1, storage[1]);
}

TEST(BitVectorView, UnionIfNotInUint32T) {
  TestBitVectorViewUnionIfNotIn<uint32_t>();
}

TEST(BitVectorView, UnionIfNotInUint64T) {
  TestBitVectorViewUnionIfNotIn<uint64_t>();
}

TEST(BitVectorView, UnionIfNotInSizeT) {
  // Note: The constants below are truncated on 32-bit architectures.
  TestBitVectorViewUnionIfNotIn<size_t>();
}

TEST(BitVector, Test) {
  const size_t kBits = 32;

  BitVector bv(kBits, false, Allocator::GetCallocAllocator());
  EXPECT_EQ(1U, bv.GetStorageSize());
  EXPECT_EQ(sizeof(uint32_t), bv.GetSizeOf());
  EXPECT_FALSE(bv.IsExpandable());

  EXPECT_EQ(0U, bv.NumSetBits());
  EXPECT_EQ(0U, bv.NumSetBits(1));
  EXPECT_EQ(0U, bv.NumSetBits(kBits));
  for (size_t i = 0; i < kBits; i++) {
    EXPECT_FALSE(bv.IsBitSet(i));
  }
  EXPECT_EQ(0U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0U, *bv.GetRawStorage());

  EXPECT_TRUE(bv.Indexes().begin().Done());
  EXPECT_TRUE(bv.Indexes().begin() == bv.Indexes().end());

  bv.SetBit(0);
  bv.SetBit(kBits - 1);
  EXPECT_EQ(2U, bv.NumSetBits());
  EXPECT_EQ(1U, bv.NumSetBits(1));
  EXPECT_EQ(2U, bv.NumSetBits(kBits));
  EXPECT_TRUE(bv.IsBitSet(0));
  for (size_t i = 1; i < kBits - 1; i++) {
    EXPECT_FALSE(bv.IsBitSet(i));
  }
  EXPECT_TRUE(bv.IsBitSet(kBits - 1));
  EXPECT_EQ(0x80000001U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0x80000001U, *bv.GetRawStorage());

  BitVectorIndexIterator<const uint32_t> iterator = bv.Indexes().begin();
  EXPECT_TRUE(iterator != bv.Indexes().end());
  EXPECT_EQ(0u, *iterator);
  ++iterator;
  EXPECT_TRUE(iterator != bv.Indexes().end());
  EXPECT_EQ(kBits - 1u, *iterator);
  ++iterator;
  EXPECT_TRUE(iterator == bv.Indexes().end());
}

struct MessyAllocator : public Allocator {
 public:
  MessyAllocator() : malloc_(Allocator::GetCallocAllocator()) {}
  ~MessyAllocator() {}

  void* Alloc(size_t s) override {
    void* res = malloc_->Alloc(s);
    memset(res, 0xfe, s);
    return res;
  }

  void Free(void* v) override {
    malloc_->Free(v);
  }

 private:
  Allocator* malloc_;
};

TEST(BitVector, MessyAllocator) {
  MessyAllocator alloc;
  BitVector bv(32, false, &alloc);
  bv.ClearAllBits();
  EXPECT_EQ(bv.NumSetBits(), 0u);
  EXPECT_EQ(bv.GetHighestBitSet(), -1);
}

TEST(BitVector, NoopAllocator) {
  const uint32_t kWords = 2;

  uint32_t bits[kWords];
  memset(bits, 0, sizeof(bits));

  BitVector bv(false, Allocator::GetNoopAllocator(), kWords, bits);
  EXPECT_EQ(kWords, bv.GetStorageSize());
  EXPECT_EQ(kWords * sizeof(uint32_t), bv.GetSizeOf());
  EXPECT_EQ(bits, bv.GetRawStorage());
  EXPECT_EQ(0U, bv.NumSetBits());

  bv.SetBit(8);
  EXPECT_EQ(1U, bv.NumSetBits());
  EXPECT_EQ(0x00000100U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0x00000000U, bv.GetRawStorageWord(1));
  EXPECT_EQ(1U, bv.NumSetBits());

  bv.SetBit(16);
  EXPECT_EQ(2U, bv.NumSetBits());
  EXPECT_EQ(0x00010100U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0x00000000U, bv.GetRawStorageWord(1));
  EXPECT_EQ(2U, bv.NumSetBits());

  bv.SetBit(32);
  EXPECT_EQ(3U, bv.NumSetBits());
  EXPECT_EQ(0x00010100U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0x00000001U, bv.GetRawStorageWord(1));
  EXPECT_EQ(3U, bv.NumSetBits());

  bv.SetBit(48);
  EXPECT_EQ(4U, bv.NumSetBits());
  EXPECT_EQ(0x00010100U, bv.GetRawStorageWord(0));
  EXPECT_EQ(0x00010001U, bv.GetRawStorageWord(1));
  EXPECT_EQ(4U, bv.NumSetBits());

  EXPECT_EQ(0U, bv.NumSetBits(1));

  EXPECT_EQ(0U, bv.NumSetBits(8));
  EXPECT_EQ(1U, bv.NumSetBits(9));
  EXPECT_EQ(1U, bv.NumSetBits(10));

  EXPECT_EQ(1U, bv.NumSetBits(16));
  EXPECT_EQ(2U, bv.NumSetBits(17));
  EXPECT_EQ(2U, bv.NumSetBits(18));

  EXPECT_EQ(2U, bv.NumSetBits(32));
  EXPECT_EQ(3U, bv.NumSetBits(33));
  EXPECT_EQ(3U, bv.NumSetBits(34));

  EXPECT_EQ(3U, bv.NumSetBits(48));
  EXPECT_EQ(4U, bv.NumSetBits(49));
  EXPECT_EQ(4U, bv.NumSetBits(50));

  EXPECT_EQ(4U, bv.NumSetBits(64));
}

TEST(BitVector, SetInitialBits) {
  const uint32_t kWords = 2;

  uint32_t bits[kWords];
  memset(bits, 0, sizeof(bits));

  BitVector bv(false, Allocator::GetNoopAllocator(), kWords, bits);
  bv.SetInitialBits(0u);
  EXPECT_EQ(0u, bv.NumSetBits());
  bv.SetInitialBits(1u);
  EXPECT_EQ(1u, bv.NumSetBits());
  bv.SetInitialBits(32u);
  EXPECT_EQ(32u, bv.NumSetBits());
  bv.SetInitialBits(63u);
  EXPECT_EQ(63u, bv.NumSetBits());
  bv.SetInitialBits(64u);
  EXPECT_EQ(64u, bv.NumSetBits());
}

TEST(BitVector, UnionIfNotIn) {
  {
    BitVector first(2, true, Allocator::GetCallocAllocator());
    BitVector second(5, true, Allocator::GetCallocAllocator());
    BitVector third(5, true, Allocator::GetCallocAllocator());

    second.SetBit(64);
    third.SetBit(64);
    bool changed = first.UnionIfNotIn(&second, &third);
    EXPECT_EQ(0u, first.NumSetBits());
    EXPECT_FALSE(changed);
  }

  {
    BitVector first(2, true, Allocator::GetCallocAllocator());
    BitVector second(5, true, Allocator::GetCallocAllocator());
    BitVector third(5, true, Allocator::GetCallocAllocator());

    second.SetBit(64);
    bool changed = first.UnionIfNotIn(&second, &third);
    EXPECT_EQ(1u, first.NumSetBits());
    EXPECT_TRUE(changed);
    EXPECT_TRUE(first.IsBitSet(64));
  }
}

TEST(BitVector, Subset) {
  {
    BitVector first(2, true, Allocator::GetCallocAllocator());
    BitVector second(5, true, Allocator::GetCallocAllocator());

    EXPECT_TRUE(first.IsSubsetOf(&second));
    second.SetBit(4);
    EXPECT_TRUE(first.IsSubsetOf(&second));
  }

  {
    BitVector first(5, true, Allocator::GetCallocAllocator());
    BitVector second(5, true, Allocator::GetCallocAllocator());

    first.SetBit(5);
    EXPECT_FALSE(first.IsSubsetOf(&second));
    second.SetBit(4);
    EXPECT_FALSE(first.IsSubsetOf(&second));
  }

  {
    BitVector first(5, true, Allocator::GetCallocAllocator());
    BitVector second(5, true, Allocator::GetCallocAllocator());

    first.SetBit(16);
    first.SetBit(32);
    first.SetBit(48);
    second.SetBit(16);
    second.SetBit(32);
    second.SetBit(48);

    EXPECT_TRUE(first.IsSubsetOf(&second));
    second.SetBit(8);
    EXPECT_TRUE(first.IsSubsetOf(&second));
    second.SetBit(40);
    EXPECT_TRUE(first.IsSubsetOf(&second));
    second.SetBit(52);
    EXPECT_TRUE(first.IsSubsetOf(&second));

    first.SetBit(9);
    EXPECT_FALSE(first.IsSubsetOf(&second));
  }
}

TEST(BitVector, CopyTo) {
  {
    // Test copying an empty BitVector. Padding should fill `buf` with zeroes.
    BitVector bv(0, true, Allocator::GetCallocAllocator());
    uint32_t buf;

    bv.CopyTo(&buf, sizeof(buf));
    EXPECT_EQ(0u, bv.GetSizeOf());
    EXPECT_EQ(0u, buf);
  }

  {
    // Test copying when `bv.storage_` and `buf` are of equal lengths.
    BitVector bv(0, true, Allocator::GetCallocAllocator());
    uint32_t buf;

    bv.SetBit(0);
    bv.SetBit(17);
    bv.SetBit(26);
    EXPECT_EQ(sizeof(buf), bv.GetSizeOf());

    bv.CopyTo(&buf, sizeof(buf));
    EXPECT_EQ(0x04020001u, buf);
  }

  {
    // Test copying when the `bv.storage_` is longer than `buf`. As long as
    // `buf` is long enough to hold all set bits, copying should succeed.
    BitVector bv(0, true, Allocator::GetCallocAllocator());
    uint8_t buf[5];

    bv.SetBit(18);
    bv.SetBit(39);
    EXPECT_LT(sizeof(buf), bv.GetSizeOf());

    bv.CopyTo(buf, sizeof(buf));
    EXPECT_EQ(0x00u, buf[0]);
    EXPECT_EQ(0x00u, buf[1]);
    EXPECT_EQ(0x04u, buf[2]);
    EXPECT_EQ(0x00u, buf[3]);
    EXPECT_EQ(0x80u, buf[4]);
  }

  {
    // Test zero padding when `bv.storage_` is shorter than `buf`.
    BitVector bv(0, true, Allocator::GetCallocAllocator());
    uint32_t buf[2];

    bv.SetBit(18);
    bv.SetBit(31);
    EXPECT_GT(sizeof(buf), bv.GetSizeOf());

    bv.CopyTo(buf, sizeof(buf));
    EXPECT_EQ(0x80040000U, buf[0]);
    EXPECT_EQ(0x00000000U, buf[1]);
  }
}

TEST(BitVector, TransformIterator) {
  BitVector bv(16, false, Allocator::GetCallocAllocator());
  bv.SetBit(4);
  bv.SetBit(8);

  auto indexs = bv.Indexes();
  for (int32_t negative :
       MakeTransformRange(indexs, [](uint32_t idx) { return -1 * static_cast<int32_t>(idx); })) {
    EXPECT_TRUE(negative == -4 || negative == -8);
  }
}

class SingleAllocator : public Allocator {
 public:
  SingleAllocator() : alloc_count_(0), free_count_(0) {}
  ~SingleAllocator() {
    EXPECT_EQ(alloc_count_, 1u);
    EXPECT_EQ(free_count_, 1u);
  }

  void* Alloc(size_t s) override {
    EXPECT_LT(s, 1024ull);
    EXPECT_EQ(alloc_count_, free_count_);
    ++alloc_count_;
    return bytes_.begin();
  }

  void Free(void*) override {
    ++free_count_;
  }

  uint32_t AllocCount() const {
    return alloc_count_;
  }
  uint32_t FreeCount() const {
    return free_count_;
  }

 private:
  std::array<uint8_t, 1024> bytes_;
  uint32_t alloc_count_;
  uint32_t free_count_;
};

TEST(BitVector, MovementFree) {
  SingleAllocator alloc;
  {
    BitVector bv(16, false, &alloc);
    bv.SetBit(13);
    EXPECT_EQ(alloc.FreeCount(), 0u);
    EXPECT_EQ(alloc.AllocCount(), 1u);
    ASSERT_TRUE(bv.GetRawStorage() != nullptr);
    EXPECT_TRUE(bv.IsBitSet(13));
    {
      BitVector bv2(std::move(bv));
      // NOLINTNEXTLINE - checking underlying storage has been freed
      ASSERT_TRUE(bv.GetRawStorage() == nullptr);
      EXPECT_TRUE(bv2.IsBitSet(13));
      EXPECT_EQ(alloc.FreeCount(), 0u);
      EXPECT_EQ(alloc.AllocCount(), 1u);
    }
    EXPECT_EQ(alloc.FreeCount(), 1u);
    EXPECT_EQ(alloc.AllocCount(), 1u);
  }
  EXPECT_EQ(alloc.FreeCount(), 1u);
  EXPECT_EQ(alloc.AllocCount(), 1u);
}

}  // namespace art
