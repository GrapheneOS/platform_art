/*
 * Copyright 2022 The Android Open Source Project
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

#ifndef ART_RUNTIME_BASE_GC_VISITED_ARENA_POOL_H_
#define ART_RUNTIME_BASE_GC_VISITED_ARENA_POOL_H_

#include "base/casts.h"
#include "base/arena_allocator.h"
#include "base/locks.h"
#include "base/mem_map.h"

#include <set>

namespace art {

// GcVisitedArenaPool can be used for tracking allocations so that they can
// be visited during GC to update the GC-roots inside them.

// An Arena which tracks its allocations.
class TrackedArena final : public Arena {
 public:
  TrackedArena(uint8_t* start, size_t size);

  template <typename PageVisitor>
  void VisitRoots(PageVisitor& visitor) const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK_ALIGNED(Size(), kPageSize);
    DCHECK_ALIGNED(Begin(), kPageSize);
    int nr_pages = Size() / kPageSize;
    uint8_t* page_begin = Begin();
    for (int i = 0; i < nr_pages && first_obj_array_[i] != nullptr; i++, page_begin += kPageSize) {
      visitor(page_begin, first_obj_array_[i]);
    }
  }

  // Set 'obj_begin' in first_obj_array_ in every element for which it's the
  // first object.
  void SetFirstObject(uint8_t* obj_begin, uint8_t* obj_end);

  void Release() override;

 private:
  // first_obj_array_[i] is the object that overlaps with the ith page's
  // beginning, i.e. first_obj_array_[i] <= ith page_begin.
  std::unique_ptr<uint8_t*[]> first_obj_array_;
};

// An arena-pool wherein allocations can be tracked so that the GC can visit all
// the GC roots. All the arenas are allocated in one sufficiently large memory
// range to avoid multiple calls to mremapped/mprotected syscalls.
class GcVisitedArenaPool final : public ArenaPool {
 public:
  explicit GcVisitedArenaPool(bool low_4gb = false, const char* name = "LinearAlloc");
  virtual ~GcVisitedArenaPool();
  Arena* AllocArena(size_t size) override;
  void FreeArenaChain(Arena* first) override;
  size_t GetBytesAllocated() const override;
  void ReclaimMemory() override {}
  void LockReclaimMemory() override {}
  void TrimMaps() override {}

  template <typename PageVisitor>
  void VisitRoots(PageVisitor& visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
    std::lock_guard<std::mutex> lock(lock_);
    for (auto& arena : allocated_arenas_) {
      arena.VisitRoots(visitor);
    }
  }

 private:
  void FreeRangeLocked(uint8_t* range_begin, size_t range_size) REQUIRES(lock_);
  // Add a map to the pool of at least min_size
  void AddMap(size_t min_size) REQUIRES(lock_);

  class Chunk {
   public:
    Chunk(uint8_t* addr, size_t size) : addr_(addr), size_(size) {}
    uint8_t* addr_;
    size_t size_;
  };

  class LessByChunkAddr {
   public:
    bool operator()(const Chunk* a, const Chunk* b) const {
      return std::less<uint8_t*>{}(a->addr_, b->addr_);
    }
  };

  class LessByChunkSize {
   public:
    // Since two chunks could have the same size, use addr when that happens.
    bool operator()(const Chunk* a, const Chunk* b) const {
      return std::less<size_t>{}(a->size_, b->size_)
             || (std::equal_to<size_t>{}(a->size_, b->size_)
                 && std::less<uint8_t*>{}(a->addr_, b->addr_));
    }
  };

  class LessByArenaAddr {
   public:
    bool operator()(const TrackedArena& a, const TrackedArena& b) const {
      return std::less<uint8_t*>{}(a.Begin(), b.Begin());
    }
  };

  // Use a std::mutex here as Arenas are second-from-the-bottom when using MemMaps, and MemMap
  // itself uses std::mutex scoped to within an allocate/free only.
  mutable std::mutex lock_;
  std::vector<MemMap> maps_ GUARDED_BY(lock_);
  std::set<Chunk*, LessByChunkSize> best_fit_allocs_ GUARDED_BY(lock_);
  std::set<Chunk*, LessByChunkAddr> free_chunks_ GUARDED_BY(lock_);
  // Set of allocated arenas. It's required to be able to find the arena
  // corresponding to a given address.
  // TODO: We can manage without this set if we decide to have a large
  // 'first-object' array for the entire space, instead of per arena. Analyse
  // which approach is better.
  std::set<TrackedArena, LessByArenaAddr> allocated_arenas_ GUARDED_BY(lock_);
  // Number of bytes allocated so far.
  size_t bytes_allocated_ GUARDED_BY(lock_);
  const char* name_;
  const bool low_4gb_;

  DISALLOW_COPY_AND_ASSIGN(GcVisitedArenaPool);
};

}  // namespace art

#endif  // ART_RUNTIME_BASE_GC_VISITED_ARENA_POOL_H_
