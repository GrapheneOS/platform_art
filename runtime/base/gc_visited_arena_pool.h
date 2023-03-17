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
  // Used for searching in maps. Only arena's starting address is relevant.
  explicit TrackedArena(uint8_t* addr) : pre_zygote_fork_(false) { memory_ = addr; }
  TrackedArena(uint8_t* start, size_t size, bool pre_zygote_fork);

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

  // Return the page addr of the first page with first_obj set to nullptr.
  uint8_t* GetLastUsedByte() const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK_ALIGNED(Begin(), kPageSize);
    DCHECK_ALIGNED(End(), kPageSize);
    // Jump past bytes-allocated for arenas which are not currently being used
    // by arena-allocator. This helps in reducing loop iterations below.
    uint8_t* last_byte = AlignUp(Begin() + GetBytesAllocated(), kPageSize);
    DCHECK_LE(last_byte, End());
    for (size_t i = (last_byte - Begin()) / kPageSize;
         last_byte < End() && first_obj_array_[i] != nullptr;
         last_byte += kPageSize, i++) {
      // No body.
    }
    return last_byte;
  }

  uint8_t* GetFirstObject(uint8_t* addr) const REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK_LE(Begin(), addr);
    DCHECK_GT(End(), addr);
    return first_obj_array_[(addr - Begin()) / kPageSize];
  }

  // Set 'obj_begin' in first_obj_array_ in every element for which it's the
  // first object.
  void SetFirstObject(uint8_t* obj_begin, uint8_t* obj_end);

  void Release() override;
  bool IsPreZygoteForkArena() const { return pre_zygote_fork_; }

 private:
  // first_obj_array_[i] is the object that overlaps with the ith page's
  // beginning, i.e. first_obj_array_[i] <= ith page_begin.
  std::unique_ptr<uint8_t*[]> first_obj_array_;
  const bool pre_zygote_fork_;
};

// An arena-pool wherein allocations can be tracked so that the GC can visit all
// the GC roots. All the arenas are allocated in one sufficiently large memory
// range to avoid multiple calls to mremapped/mprotected syscalls.
class GcVisitedArenaPool final : public ArenaPool {
 public:
#if defined(__LP64__)
  // Use a size in multiples of 1GB as that can utilize the optimized mremap
  // page-table move.
  static constexpr size_t kLinearAllocPoolSize = 1 * GB;
  static constexpr size_t kLow4GBLinearAllocPoolSize = 32 * MB;
#else
  static constexpr size_t kLinearAllocPoolSize = 32 * MB;
#endif

  explicit GcVisitedArenaPool(bool low_4gb = false,
                              bool is_zygote = false,
                              const char* name = "LinearAlloc");
  virtual ~GcVisitedArenaPool();
  Arena* AllocArena(size_t size) override;
  void FreeArenaChain(Arena* first) override;
  size_t GetBytesAllocated() const override;
  void ReclaimMemory() override {}
  void LockReclaimMemory() override {}
  void TrimMaps() override {}

  bool Contains(void* ptr) {
    std::lock_guard<std::mutex> lock(lock_);
    for (auto& map : maps_) {
      if (map.HasAddress(ptr)) {
        return true;
      }
    }
    return false;
  }

  template <typename PageVisitor>
  void VisitRoots(PageVisitor& visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
    std::lock_guard<std::mutex> lock(lock_);
    for (auto& arena : allocated_arenas_) {
      arena.VisitRoots(visitor);
    }
  }

  template <typename Callback>
  void ForEachAllocatedArena(Callback cb) REQUIRES_SHARED(Locks::mutator_lock_) {
    std::lock_guard<std::mutex> lock(lock_);
    for (auto& arena : allocated_arenas_) {
      cb(arena);
    }
  }

  // Called in Heap::PreZygoteFork(). All allocations after this are done in
  // arena-pool which is visited by userfaultfd.
  void SetupPostZygoteMode() {
    std::lock_guard<std::mutex> lock(lock_);
    DCHECK(pre_zygote_fork_);
    pre_zygote_fork_ = false;
  }

  // For userfaultfd GC to be able to acquire the lock to avoid concurrent
  // release of arenas when it is visiting them.
  std::mutex& GetLock() { return lock_; }

  // Find the given arena in allocated_arenas_. The function is called with
  // lock_ acquired.
  bool FindAllocatedArena(const TrackedArena* arena) const NO_THREAD_SAFETY_ANALYSIS {
    for (auto& allocated_arena : allocated_arenas_) {
      if (arena == &allocated_arena) {
        return true;
      }
    }
    return false;
  }

  void ClearArenasFreed() {
    std::lock_guard<std::mutex> lock(lock_);
    arenas_freed_ = false;
  }

  // The function is called with lock_ acquired.
  bool AreArenasFreed() const NO_THREAD_SAFETY_ANALYSIS { return arenas_freed_; }

 private:
  void FreeRangeLocked(uint8_t* range_begin, size_t range_size) REQUIRES(lock_);
  // Add a map (to be visited by userfaultfd) to the pool of at least min_size
  // and return its address.
  uint8_t* AddMap(size_t min_size) REQUIRES(lock_);
  // Add a private anonymous map prior to zygote fork to the pool and return its
  // address.
  uint8_t* AddPreZygoteForkMap(size_t size) REQUIRES(lock_);

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
      return a->size_ < b->size_ ||
             (a->size_ == b->size_ && std::less<uint8_t*>{}(a->addr_, b->addr_));
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
  // TODO: consider using HashSet, which is more memory efficient.
  std::set<TrackedArena, LessByArenaAddr> allocated_arenas_ GUARDED_BY(lock_);
  // Number of bytes allocated so far.
  size_t bytes_allocated_ GUARDED_BY(lock_);
  const char* name_;
  // Flag to indicate that some arenas have been freed. This flag is used as an
  // optimization by GC to know if it needs to find if the arena being visited
  // has been freed or not. The flag is cleared in the compaction pause and read
  // when linear-alloc space is concurrently visited updated to update GC roots.
  bool arenas_freed_ GUARDED_BY(lock_);
  const bool low_4gb_;
  // Set to true in zygote process so that all linear-alloc allocations are in
  // private-anonymous mappings and not on userfaultfd visited pages. At
  // first zygote fork, it's set to false, after which all allocations are done
  // in userfaultfd visited space.
  bool pre_zygote_fork_ GUARDED_BY(lock_);

  DISALLOW_COPY_AND_ASSIGN(GcVisitedArenaPool);
};

}  // namespace art

#endif  // ART_RUNTIME_BASE_GC_VISITED_ARENA_POOL_H_
