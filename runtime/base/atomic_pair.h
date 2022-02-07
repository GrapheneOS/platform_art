/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ART_RUNTIME_BASE_ATOMIC_PAIR_H_
#define ART_RUNTIME_BASE_ATOMIC_PAIR_H_

#include "base/macros.h"

#include <type_traits>

namespace art {

// std::pair<> is not trivially copyable and as such it is unsuitable for atomic operations.
template <typename IntType>
struct PACKED(2 * sizeof(IntType)) AtomicPair {
  static_assert(std::is_integral_v<IntType>);

  constexpr AtomicPair() : first(0), second(0) { }
  AtomicPair(IntType f, IntType s) : first(f), second(s) { }
  AtomicPair(const AtomicPair&) = default;
  AtomicPair& operator=(const AtomicPair&) = default;

  IntType first;
  IntType second;
};

template <typename IntType>
ALWAYS_INLINE static inline AtomicPair<IntType> AtomicPairLoadAcquire(
    std::atomic<AtomicPair<IntType>>* target) {
  static_assert(std::atomic<AtomicPair<IntType>>::is_always_lock_free);
  return target->load(std::memory_order_acquire);
}

template <typename IntType>
ALWAYS_INLINE static inline void AtomicPairStoreRelease(
    std::atomic<AtomicPair<IntType>>* target, AtomicPair<IntType> value) {
  static_assert(std::atomic<AtomicPair<IntType>>::is_always_lock_free);
  target->store(value, std::memory_order_release);
}

// llvm does not implement 16-byte atomic operations on x86-64.
#if defined(__x86_64__)
ALWAYS_INLINE static inline AtomicPair<uint64_t> AtomicPairLoadAcquire(
    std::atomic<AtomicPair<uint64_t>>* target) {
  uint64_t first, second;
  __asm__ __volatile__(
      "lock cmpxchg16b (%2)"
      : "=&a"(first), "=&d"(second)
      : "r"(target), "a"(0), "d"(0), "b"(0), "c"(0)
      : "cc");
  return {first, second};
}

ALWAYS_INLINE static inline void AtomicPairStoreRelease(
    std::atomic<AtomicPair<uint64_t>>* target, AtomicPair<uint64_t> value) {
  uint64_t first, second;
  __asm__ __volatile__ (
      "movq (%2), %%rax\n\t"
      "movq 8(%2), %%rdx\n\t"
      "1:\n\t"
      "lock cmpxchg16b (%2)\n\t"
      "jnz 1b"
      : "=&a"(first), "=&d"(second)
      : "r"(target), "b"(value.first), "c"(value.second)
      : "cc");
}
#endif  // defined(__x86_64__)

}  // namespace art

#endif  // ART_RUNTIME_BASE_ATOMIC_PAIR_H_
