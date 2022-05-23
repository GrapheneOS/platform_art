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

#include "base/gc_visited_arena_pool.h"

#include "base/arena_allocator-inl.h"
#include "base/utils.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace art {

#if defined(__LP64__)
// Use a size in multiples of 1GB as that can utilize the optimized mremap
// page-table move.
static constexpr size_t kLinearAllocPoolSize = 1 * GB;
static constexpr size_t kLow4GBLinearAllocPoolSize = 32 * MB;
#else
static constexpr size_t kLinearAllocPoolSize = 32 * MB;
#endif

TrackedArena::TrackedArena(uint8_t* start, size_t size) : Arena(), first_obj_array_(nullptr) {
  static_assert(ArenaAllocator::kArenaAlignment <= kPageSize,
                "Arena should not need stronger alignment than kPageSize.");
  DCHECK_ALIGNED(size, kPageSize);
  DCHECK_ALIGNED(start, kPageSize);
  memory_ = start;
  size_ = size;
  size_t arr_size = size / kPageSize;
  first_obj_array_.reset(new uint8_t*[arr_size]);
  std::fill_n(first_obj_array_.get(), arr_size, nullptr);
}

void TrackedArena::Release() {
  if (bytes_allocated_ > 0) {
    ZeroAndReleasePages(Begin(), Size());
    std::fill_n(first_obj_array_.get(), Size() / kPageSize, nullptr);
    bytes_allocated_ = 0;
  }
}

void TrackedArena::SetFirstObject(uint8_t* obj_begin, uint8_t* obj_end) {
  DCHECK_LE(static_cast<void*>(Begin()), static_cast<void*>(obj_end));
  DCHECK_LT(static_cast<void*>(obj_begin), static_cast<void*>(obj_end));
  size_t idx = static_cast<size_t>(obj_begin - Begin()) / kPageSize;
  size_t last_byte_idx = static_cast<size_t>(obj_end - 1 - Begin()) / kPageSize;
  // If the addr is at the beginning of a page, then we set it for that page too.
  if (IsAligned<kPageSize>(obj_begin)) {
    first_obj_array_[idx] = obj_begin;
  }
  while (idx < last_byte_idx) {
    first_obj_array_[++idx] = obj_begin;
  }
}

void GcVisitedArenaPool::AddMap(size_t min_size) {
  size_t size = std::max(min_size, kLinearAllocPoolSize);
#if defined(__LP64__)
  // This is true only when we are running a 64-bit dex2oat to compile a 32-bit image.
  if (low_4gb_) {
    size = std::max(min_size, kLow4GBLinearAllocPoolSize);
  }
#endif
  std::string err_msg;
  maps_.emplace_back(MemMap::MapAnonymous(name_,
                                          size,
                                          PROT_READ | PROT_WRITE,
                                          low_4gb_,
                                          &err_msg));
  MemMap& map = maps_.back();
  if (!map.IsValid()) {
    LOG(FATAL) << "Failed to allocate " << name_
               << ": " << err_msg;
    UNREACHABLE();
  }
  Chunk* chunk = new Chunk(map.Begin(), map.Size());
  best_fit_allocs_.insert(chunk);
  free_chunks_.insert(chunk);
}

GcVisitedArenaPool::GcVisitedArenaPool(bool low_4gb, const char* name)
  : bytes_allocated_(0), name_(name), low_4gb_(low_4gb) {
  std::lock_guard<std::mutex> lock(lock_);
  // It's extremely rare to have more than one map.
  maps_.reserve(1);
  AddMap(/*min_size=*/0);
}

GcVisitedArenaPool::~GcVisitedArenaPool() {
  for (Chunk* chunk : free_chunks_) {
    delete chunk;
  }
  // Must not delete chunks from best_fit_allocs_ as they are shared with
  // free_chunks_.
}

size_t GcVisitedArenaPool::GetBytesAllocated() const {
  std::lock_guard<std::mutex> lock(lock_);
  return bytes_allocated_;
}

Arena* GcVisitedArenaPool::AllocArena(size_t size) {
  // Return only page aligned sizes so that madvise can be leveraged.
  size = RoundUp(size, kPageSize);
  Chunk temp_chunk(nullptr, size);
  std::lock_guard<std::mutex> lock(lock_);
  auto best_fit_iter = best_fit_allocs_.lower_bound(&temp_chunk);
  if (UNLIKELY(best_fit_iter == best_fit_allocs_.end())) {
    AddMap(size);
    best_fit_iter = best_fit_allocs_.lower_bound(&temp_chunk);
    CHECK(best_fit_iter != best_fit_allocs_.end());
  }
  auto free_chunks_iter = free_chunks_.find(*best_fit_iter);
  DCHECK(free_chunks_iter != free_chunks_.end());
  Chunk* chunk = *best_fit_iter;
  DCHECK_EQ(chunk, *free_chunks_iter);
  // if the best-fit chunk < 2x the requested size, then give the whole chunk.
  if (chunk->size_ < 2 * size) {
    DCHECK_GE(chunk->size_, size);
    auto emplace_result = allocated_arenas_.emplace(chunk->addr_, chunk->size_);
    DCHECK(emplace_result.second);
    free_chunks_.erase(free_chunks_iter);
    best_fit_allocs_.erase(best_fit_iter);
    delete chunk;
    return const_cast<TrackedArena*>(&(*emplace_result.first));
  } else {
    auto emplace_result = allocated_arenas_.emplace(chunk->addr_, size);
    DCHECK(emplace_result.second);
    // Compute next iterators for faster insert later.
    auto next_best_fit_iter = best_fit_iter;
    next_best_fit_iter++;
    auto next_free_chunks_iter = free_chunks_iter;
    next_free_chunks_iter++;
    auto best_fit_nh = best_fit_allocs_.extract(best_fit_iter);
    auto free_chunks_nh = free_chunks_.extract(free_chunks_iter);
    best_fit_nh.value()->addr_ += size;
    best_fit_nh.value()->size_ -= size;
    DCHECK_EQ(free_chunks_nh.value()->addr_, chunk->addr_);
    best_fit_allocs_.insert(next_best_fit_iter, std::move(best_fit_nh));
    free_chunks_.insert(next_free_chunks_iter, std::move(free_chunks_nh));
    return const_cast<TrackedArena*>(&(*emplace_result.first));
  }
}

void GcVisitedArenaPool::FreeRangeLocked(uint8_t* range_begin, size_t range_size) {
  Chunk temp_chunk(range_begin, range_size);
  bool merge_with_next = false;
  bool merge_with_prev = false;
  auto next_iter = free_chunks_.lower_bound(&temp_chunk);
  auto iter_for_extract = free_chunks_.end();
  // Can we merge with the previous chunk?
  if (next_iter != free_chunks_.begin()) {
    auto prev_iter = next_iter;
    prev_iter--;
    merge_with_prev = (*prev_iter)->addr_ + (*prev_iter)->size_ == range_begin;
    if (merge_with_prev) {
      range_begin = (*prev_iter)->addr_;
      range_size += (*prev_iter)->size_;
      // Hold on to the iterator for faster extract later
      iter_for_extract = prev_iter;
    }
  }
  // Can we merge with the next chunk?
  if (next_iter != free_chunks_.end()) {
    merge_with_next = range_begin + range_size == (*next_iter)->addr_;
    if (merge_with_next) {
      range_size += (*next_iter)->size_;
      if (merge_with_prev) {
        auto iter = next_iter;
        next_iter++;
        // Keep only one of the two chunks to be expanded.
        Chunk* chunk = *iter;
        size_t erase_res = best_fit_allocs_.erase(chunk);
        DCHECK_EQ(erase_res, 1u);
        free_chunks_.erase(iter);
        delete chunk;
      } else {
        iter_for_extract = next_iter;
        next_iter++;
      }
    }
  }

  // Extract-insert avoids 2/4 destroys and 2/2 creations
  // as compared to erase-insert, so use that when merging.
  if (merge_with_prev || merge_with_next) {
    auto free_chunks_nh = free_chunks_.extract(iter_for_extract);
    auto best_fit_allocs_nh = best_fit_allocs_.extract(*iter_for_extract);

    free_chunks_nh.value()->addr_ = range_begin;
    DCHECK_EQ(best_fit_allocs_nh.value()->addr_, range_begin);
    free_chunks_nh.value()->size_ = range_size;
    DCHECK_EQ(best_fit_allocs_nh.value()->size_, range_size);

    free_chunks_.insert(next_iter, std::move(free_chunks_nh));
    // Since the chunk's size has expanded, the hint won't be useful
    // for best-fit set.
    best_fit_allocs_.insert(std::move(best_fit_allocs_nh));
  } else {
    DCHECK(iter_for_extract == free_chunks_.end());
    Chunk* chunk = new Chunk(range_begin, range_size);
    free_chunks_.insert(next_iter, chunk);
    best_fit_allocs_.insert(chunk);
  }
}

void GcVisitedArenaPool::FreeArenaChain(Arena* first) {
  if (kRunningOnMemoryTool) {
    for (Arena* arena = first; arena != nullptr; arena = arena->Next()) {
      MEMORY_TOOL_MAKE_UNDEFINED(arena->Begin(), arena->GetBytesAllocated());
    }
  }

  // TODO: Handle the case when arena_allocator::kArenaAllocatorPreciseTracking
  // is true. See MemMapArenaPool::FreeArenaChain() for example.
  CHECK(!arena_allocator::kArenaAllocatorPreciseTracking);

  // madvise the arenas before acquiring lock for scalability
  for (Arena* temp = first; temp != nullptr; temp = temp->Next()) {
    temp->Release();
  }

  std::lock_guard<std::mutex> lock(lock_);
  while (first != nullptr) {
    FreeRangeLocked(first->Begin(), first->Size());
    // In other implementations of ArenaPool this is calculated when asked for,
    // thanks to the list of free arenas that is kept around. But in this case,
    // we release the freed arena back to the pool and therefore need to
    // calculate here.
    bytes_allocated_ += first->GetBytesAllocated();
    TrackedArena* temp = down_cast<TrackedArena*>(first);
    first = first->Next();
    size_t erase_count = allocated_arenas_.erase(*temp);
    DCHECK_EQ(erase_count, 1u);
  }
}

}  // namespace art

