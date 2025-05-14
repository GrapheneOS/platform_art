/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "calloc_arena_pool.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <numeric>

#include <android-base/logging.h>
#include "arena_allocator-inl.h"
#include "mman.h"

namespace art {

class CallocArena final : public Arena {
 public:
  explicit CallocArena(size_t size = arena_allocator::kArenaDefaultSize);
  virtual ~CallocArena();
 private:
  static constexpr size_t RequiredOverallocation() {
    return (alignof(std::max_align_t) < ArenaAllocator::kArenaAlignment)
        ? ArenaAllocator::kArenaAlignment - alignof(std::max_align_t)
        : 0u;
  }

  uint8_t* unaligned_memory_;
};

CallocArena::CallocArena(size_t size) {
  // We need to guarantee kArenaAlignment aligned allocation for the new arena.
  // TODO: Use std::aligned_alloc() when it becomes available with C++17.
  constexpr size_t overallocation = RequiredOverallocation();
  unaligned_memory_ = reinterpret_cast<uint8_t*>(calloc(1, size + overallocation));
  CHECK(unaligned_memory_ != nullptr);  // Abort on OOM.
  DCHECK_ALIGNED(unaligned_memory_, alignof(std::max_align_t));
  if (overallocation == 0u) {
    memory_ = unaligned_memory_;
  } else {
    memory_ = AlignUp(unaligned_memory_, ArenaAllocator::kArenaAlignment);
    if (kRunningOnMemoryTool) {
      size_t head = memory_ - unaligned_memory_;
      size_t tail = overallocation - head;
      MEMORY_TOOL_MAKE_NOACCESS(unaligned_memory_, head);
      MEMORY_TOOL_MAKE_NOACCESS(memory_ + size, tail);
    }
  }
  DCHECK_ALIGNED(memory_, ArenaAllocator::kArenaAlignment);
  size_ = size;
}

CallocArena::~CallocArena() {
  constexpr size_t overallocation = RequiredOverallocation();
  if (overallocation != 0u && kRunningOnMemoryTool) {
    size_t head = memory_ - unaligned_memory_;
    size_t tail = overallocation - head;
    MEMORY_TOOL_MAKE_UNDEFINED(unaligned_memory_, head);
    MEMORY_TOOL_MAKE_UNDEFINED(memory_ + size_, tail);
  }
  free(reinterpret_cast<void*>(unaligned_memory_));
}

void Arena::Reset() {
  if (bytes_allocated_ > 0) {
    memset(Begin(), 0, bytes_allocated_);
    bytes_allocated_ = 0;
  }
}

CallocArenaPool::CallocArenaPool() : free_arenas_(nullptr) {
}

CallocArenaPool::~CallocArenaPool() {
  ReclaimMemory();
}

void CallocArenaPool::ReclaimMemory() {
  while (free_arenas_ != nullptr) {
    Arena* arena = free_arenas_;
    free_arenas_ = free_arenas_->next_;
    delete arena;
  }
}

void CallocArenaPool::LockReclaimMemory() {
  std::lock_guard<std::mutex> lock(lock_);
  ReclaimMemory();
}

Arena* CallocArenaPool::AllocArena(size_t size) {
  Arena* ret = nullptr;
  {
    std::lock_guard<std::mutex> lock(lock_);
    // We used to check only the first free arena but we're now checking two.
    //
    // FIXME: This is a workaround for `oatdump` running out of memory because of an allocation
    // pattern where we would allocate a large arena (more than the default size) and then a
    // normal one (default size) and then return them to the pool together, with the normal one
    // passed as `first` to `FreeArenaChain()`, thus becoming the first in the `free_arenas_`
    // list. Since we checked only the first arena, doing this repeatedly would never reuse the
    // existing freed larger arenas and they would just accumulate in the free arena list until
    // running out of memory. This workaround allows reusing the second arena in the list, thus
    // fixing the problem for this specific allocation pattern. Similar allocation patterns
    // with three or more arenas can still result in out of memory issues.
    if (free_arenas_ != nullptr && LIKELY(free_arenas_->Size() >= size)) {
      ret = free_arenas_;
      free_arenas_ = free_arenas_->next_;
    } else if (free_arenas_ != nullptr &&
               free_arenas_->next_ != nullptr &&
               free_arenas_->next_->Size() >= size) {
      ret = free_arenas_->next_;
      free_arenas_->next_ = free_arenas_->next_->next_;
    }
  }
  if (ret == nullptr) {
    ret = new CallocArena(size);
  }
  ret->Reset();
  return ret;
}

void CallocArenaPool::TrimMaps() {
  // Nop, because there is no way to do madvise here.
}

size_t CallocArenaPool::GetBytesAllocated() const {
  size_t total = 0;
  std::lock_guard<std::mutex> lock(lock_);
  for (Arena* arena = free_arenas_; arena != nullptr; arena = arena->next_) {
    total += arena->GetBytesAllocated();
  }
  return total;
}

void CallocArenaPool::FreeArenaChain(Arena* first) {
  if (kRunningOnMemoryTool) {
    for (Arena* arena = first; arena != nullptr; arena = arena->next_) {
      MEMORY_TOOL_MAKE_UNDEFINED(arena->memory_, arena->bytes_allocated_);
    }
  }

  if (arena_allocator::kArenaAllocatorPreciseTracking) {
    // Do not reuse arenas when tracking.
    while (first != nullptr) {
      Arena* next = first->next_;
      delete first;
      first = next;
    }
    return;
  }

  if (first != nullptr) {
    Arena* last = first;
    while (last->next_ != nullptr) {
      last = last->next_;
    }
    std::lock_guard<std::mutex> lock(lock_);
    last->next_ = free_arenas_;
    free_arenas_ = first;
  }
}

}  // namespace art
