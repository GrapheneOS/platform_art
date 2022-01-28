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

#ifndef ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
#define ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_

#include <memory>
#include <unordered_set>

#include "base/atomic.h"
#include "barrier.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/bitmap-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc_root.h"
#include "immune_spaces.h"
#include "offsets.h"

namespace art {

namespace mirror {
class DexCache;
}

namespace gc {

class Heap;

namespace space {
class BumpPointerSpace;
}  // namespace space

namespace collector {
class MarkCompact : public GarbageCollector {
 public:
  static constexpr size_t kAlignment = kObjectAlignment;

  explicit MarkCompact(Heap* heap);

  ~MarkCompact() {}

  void RunPhases() override REQUIRES(!Locks::mutator_lock_);

  // Updated before (or in) pre-compaction pause and is accessed only in the
  // pause or during concurrent compaction. The flag is reset after compaction
  // is completed and never accessed by mutators. Therefore, safe to update
  // without any memory ordering.
  bool IsCompacting(Thread* self) const {
    return compacting_ && self == thread_running_gc_;
  }

  GcType GetGcType() const override {
    return kGcTypeFull;
  }

  CollectorType GetCollectorType() const override {
    return kCollectorTypeCMC;
  }

  Barrier& GetBarrier() {
    return gc_barrier_;
  }

  mirror::Object* MarkObject(mirror::Object* obj) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void MarkHeapReference(mirror::HeapReference<mirror::Object>* obj,
                         bool do_atomic_update) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void VisitRoots(mirror::Object*** roots,
                  size_t count,
                  const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                  size_t count,
                  const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  bool IsNullOrMarkedHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                   bool do_atomic_update) override
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void RevokeAllThreadLocalBuffers() override;

  void DelayReferenceReferent(ObjPtr<mirror::Class> klass,
                              ObjPtr<mirror::Reference> reference) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  mirror::Object* IsMarked(mirror::Object* obj) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  // Perform GC-root updation and heap protection so that during the concurrent
  // compaction phase we can receive faults and compact the corresponding pages
  // on the fly. This is performed in a STW pause.
  void CompactionPause() REQUIRES(Locks::mutator_lock_, !Locks::heap_bitmap_lock_);

  mirror::Object* GetFromSpaceAddrFromBarrier(mirror::Object* old_ref) {
    CHECK(compacting_);
    if (live_words_bitmap_->HasAddress(old_ref)) {
      return GetFromSpaceAddr(old_ref);
    }
    return old_ref;
  }

 private:
  using ObjReference = mirror::ObjectReference</*kPoisonReferences*/ false, mirror::Object>;
  // Number of bits (live-words) covered by a single chunk-info (below)
  // entry/word.
  // TODO: Since popcount is performed usomg SIMD instructions, we should
  // consider using 128-bit in order to halve the chunk-info size.
  static constexpr uint32_t kBitsPerVectorWord = kBitsPerIntPtrT;
  static constexpr uint32_t kOffsetChunkSize = kBitsPerVectorWord * kAlignment;
  static_assert(kOffsetChunkSize < kPageSize);
  // Bitmap with bits corresponding to every live word set. For an object
  // which is 4 words in size will have the corresponding 4 bits set. This is
  // required for efficient computation of new-address (post-compaction) from
  // the given old-address (pre-compaction).
  template <size_t kAlignment>
  class LiveWordsBitmap : private accounting::MemoryRangeBitmap<kAlignment> {
    using Bitmap = accounting::Bitmap;
    using MemRangeBitmap = accounting::MemoryRangeBitmap<kAlignment>;

   public:
    static_assert(IsPowerOfTwo(kBitsPerVectorWord));
    static_assert(IsPowerOfTwo(Bitmap::kBitsPerBitmapWord));
    static_assert(kBitsPerVectorWord >= Bitmap::kBitsPerBitmapWord);
    static constexpr uint32_t kBitmapWordsPerVectorWord =
            kBitsPerVectorWord / Bitmap::kBitsPerBitmapWord;
    static_assert(IsPowerOfTwo(kBitmapWordsPerVectorWord));
    static LiveWordsBitmap* Create(uintptr_t begin, uintptr_t end);

    // Return offset (within the indexed chunk-info) of the nth live word.
    uint32_t FindNthLiveWordOffset(size_t chunk_idx, uint32_t n) const;
    // Sets all bits in the bitmap corresponding to the given range. Also
    // returns the bit-index of the first word.
    ALWAYS_INLINE uintptr_t SetLiveWords(uintptr_t begin, size_t size);
    // Count number of live words upto the given bit-index. This is to be used
    // to compute the post-compact address of an old reference.
    ALWAYS_INLINE size_t CountLiveWordsUpto(size_t bit_idx) const;
    // Call 'visitor' for every stride of contiguous marked bits in the live-words
    // bitmap, starting from begin_bit_idx. Only visit 'bytes' live bytes or
    // until 'end', whichever comes first.
    // Visitor is called with index of the first marked bit in the stride,
    // stride size and whether it's the last stride in the given range or not.
    template <typename Visitor>
    ALWAYS_INLINE void VisitLiveStrides(uintptr_t begin_bit_idx,
                                        uint8_t* end,
                                        const size_t bytes,
                                        Visitor&& visitor) const
        REQUIRES_SHARED(Locks::mutator_lock_);
    // Count the number of live bytes in the given vector entry.
    size_t LiveBytesInBitmapWord(size_t chunk_idx) const;
    void ClearBitmap() { Bitmap::Clear(); }
    ALWAYS_INLINE uintptr_t Begin() const { return MemRangeBitmap::CoverBegin(); }
    ALWAYS_INLINE bool HasAddress(mirror::Object* obj) const {
      return MemRangeBitmap::HasAddress(reinterpret_cast<uintptr_t>(obj));
    }
    ALWAYS_INLINE bool Test(uintptr_t bit_index) const {
      return Bitmap::TestBit(bit_index);
    }
    ALWAYS_INLINE bool Test(mirror::Object* obj) const {
      return MemRangeBitmap::Test(reinterpret_cast<uintptr_t>(obj));
    }
    ALWAYS_INLINE uintptr_t GetWord(size_t index) const {
      static_assert(kBitmapWordsPerVectorWord == 1);
      return Bitmap::Begin()[index * kBitmapWordsPerVectorWord];
    }
  };

  // For a given object address in pre-compact space, return the corresponding
  // address in the from-space, where heap pages are relocated in the compaction
  // pause.
  mirror::Object* GetFromSpaceAddr(mirror::Object* obj) const {
    DCHECK(live_words_bitmap_->HasAddress(obj)) << " obj=" << obj;
    return reinterpret_cast<mirror::Object*>(reinterpret_cast<uintptr_t>(obj)
                                             + from_space_slide_diff_);
  }

  // Verifies that that given object reference refers to a valid object.
  // Otherwise fataly dumps logs, including those from callback.
  template <typename Callback>
  void VerifyObject(mirror::Object* ref, Callback& callback) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Check if the obj is within heap and has a klass which is likely to be valid
  // mirror::Class.
  bool IsValidObject(mirror::Object* obj) const REQUIRES_SHARED(Locks::mutator_lock_);
  void InitializePhase();
  void FinishPhase() REQUIRES(!Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  void MarkingPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);
  void CompactionPhase() REQUIRES_SHARED(Locks::mutator_lock_);

  void SweepSystemWeaks(Thread* self, Runtime* runtime, const bool paused)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);
  // Update the reference at given offset in the given object with post-compact
  // address.
  ALWAYS_INLINE void UpdateRef(mirror::Object* obj, MemberOffset offset)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Verify that the gc-root is updated only once. Returns false if the update
  // shouldn't be done.
  ALWAYS_INLINE bool VerifyRootSingleUpdate(void* root,
                                            mirror::Object* old_ref,
                                            const RootInfo& info)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update the given root with post-compact address.
  ALWAYS_INLINE void UpdateRoot(mirror::CompressedReference<mirror::Object>* root,
                                const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE void UpdateRoot(mirror::Object** root,
                                const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Given the pre-compact address, the function returns the post-compact
  // address of the given object.
  ALWAYS_INLINE mirror::Object* PostCompactAddress(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute post-compact address of an object in moving space. This function
  // assumes that old_ref is in moving space.
  ALWAYS_INLINE mirror::Object* PostCompactAddressUnchecked(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute the new address for an object which was allocated prior to starting
  // this GC cycle.
  ALWAYS_INLINE mirror::Object* PostCompactOldObjAddr(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compute the new address for an object which was black allocated during this
  // GC cycle.
  ALWAYS_INLINE mirror::Object* PostCompactBlackObjAddr(mirror::Object* old_ref) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Identify immune spaces and reset card-table, mod-union-table, and mark
  // bitmaps.
  void BindAndResetBitmaps() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Perform one last round of marking, identifying roots from dirty cards
  // during a stop-the-world (STW) pause.
  void MarkingPause() REQUIRES(Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  // Perform stop-the-world pause prior to concurrent compaction.
  // Updates GC-roots and protects heap so that during the concurrent
  // compaction phase we can receive faults and compact the corresponding pages
  // on the fly.
  void PreCompactionPhase() REQUIRES(Locks::mutator_lock_);
  // Compute offsets (in chunk_info_vec_) and other data structures required
  // during concurrent compaction.
  void PrepareForCompaction() REQUIRES_SHARED(Locks::mutator_lock_);

  // Copy kPageSize live bytes starting from 'offset' (within the moving space),
  // which must be within 'obj', into the kPageSize sized memory pointed by 'addr'.
  // Then update the references within the copied objects. The boundary objects are
  // partially updated such that only the references that lie in the page are updated.
  // This is necessary to avoid cascading userfaults.
  void CompactPage(mirror::Object* obj, uint32_t offset, uint8_t* addr)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Compact the bump-pointer space.
  void CompactMovingSpace() REQUIRES_SHARED(Locks::mutator_lock_);
  // Update all the objects in the given non-moving space page. 'first' object
  // could have started in some preceding page.
  void UpdateNonMovingPage(mirror::Object* first, uint8_t* page)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update all the references in the non-moving space.
  void UpdateNonMovingSpace() REQUIRES_SHARED(Locks::mutator_lock_);

  // For all the pages in non-moving space, find the first object that overlaps
  // with the pages' start address, and store in first_objs_non_moving_space_ array.
  void InitNonMovingSpaceFirstObjects() REQUIRES_SHARED(Locks::mutator_lock_);
  // In addition to the first-objects for every post-compact moving space page,
  // also find offsets within those objects from where the contents should be
  // copied to the page. The offsets are relative to the moving-space's
  // beginning. Store the computed first-object and offset in first_objs_moving_space_
  // and pre_compact_offset_moving_space_ respectively.
  void InitMovingSpaceFirstObjects(const size_t vec_len) REQUIRES_SHARED(Locks::mutator_lock_);

  // Gather the info related to black allocations from bump-pointer space to
  // enable concurrent sliding of these pages.
  void UpdateMovingSpaceBlackAllocations() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Update first-object info from allocation-stack for non-moving space black
  // allocations.
  void UpdateNonMovingSpaceBlackAllocations() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  // Slides (retain the empty holes, which are usually part of some in-use TLAB)
  // black page in the moving space. 'first_obj' is the object that overlaps with
  // the first byte of the page being slid. pre_compact_page is the pre-compact
  // address of the page being slid. 'page_idx' is used to fetch the first
  // allocated chunk's size and next page's first_obj. 'dest' is the kPageSize
  // sized memory where the contents would be copied.
  void SlideBlackPage(mirror::Object* first_obj,
                      const size_t page_idx,
                      uint8_t* const pre_compact_page,
                      uint8_t* dest)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Perform reference-processing and the likes before sweeping the non-movable
  // spaces.
  void ReclaimPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);

  // Mark GC-roots (except from immune spaces and thread-stacks) during a STW pause.
  void ReMarkRoots(Runtime* runtime) REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Concurrently mark GC-roots, except from immune spaces.
  void MarkRoots(VisitRootFlags flags) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Collect thread stack roots via a checkpoint.
  void MarkRootsCheckpoint(Thread* self, Runtime* runtime) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Second round of concurrent marking. Mark all gray objects that got dirtied
  // since the first round.
  void PreCleanCards() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  void MarkNonThreadRoots(Runtime* runtime) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void MarkConcurrentRoots(VisitRootFlags flags, Runtime* runtime)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  // Traverse through the reachable objects and mark them.
  void MarkReachableObjects() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Scan (only) immune spaces looking for references into the garbage collected
  // spaces.
  void UpdateAndMarkModUnion() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Scan mod-union and card tables, covering all the spaces, to identify dirty objects.
  // These are in 'minimum age' cards, which is 'kCardAged' in case of concurrent (second round)
  // marking and kCardDirty during the STW pause.
  void ScanDirtyObjects(bool paused, uint8_t minimum_age) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Recursively mark dirty objects. Invoked both concurrently as well in a STW
  // pause in PausePhase().
  void RecursiveMarkDirtyObjects(bool paused, uint8_t minimum_age)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Go through all the objects in the mark-stack until it's empty.
  void ProcessMarkStack() override REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void ExpandMarkStack() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Scan object for references. If kUpdateLivewords is true then set bits in
  // the live-words bitmap and add size to chunk-info.
  template <bool kUpdateLiveWords>
  void ScanObject(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Push objects to the mark-stack right after successfully marking objects.
  void PushOnMarkStack(mirror::Object* obj)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Update the live-words bitmap as well as add the object size to the
  // chunk-info vector. Both are required for computation of post-compact addresses.
  void UpdateLivenessInfo(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_);

  void ProcessReferences(Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);

  void MarkObjectNonNull(mirror::Object* obj,
                         mirror::Object* holder = nullptr,
                         MemberOffset offset = MemberOffset(0))
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  void MarkObject(mirror::Object* obj, mirror::Object* holder, MemberOffset offset)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  template <bool kParallel>
  bool MarkObjectNonNullNoPush(mirror::Object* obj,
                               mirror::Object* holder = nullptr,
                               MemberOffset offset = MemberOffset(0))
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void Sweep(bool swap_bitmaps) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void SweepLargeObjects(bool swap_bitmaps) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Store all the dex-cache objects visited during marking phase.
  // This is required during compaction phase to ensure that we don't miss any
  // of them from visiting (to update references). Somehow, iterating over
  // class-tables to fetch these misses some of them, leading to memory
  // corruption.
  // TODO: once we implement concurrent compaction of classes and dex-caches,
  // which will visit all of them, we should remove this.
  void RememberDexCaches(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_);
  // For checkpoints
  Barrier gc_barrier_;
  // Every object inside the immune spaces is assumed to be marked.
  ImmuneSpaces immune_spaces_;
  // Required only when mark-stack is accessed in shared mode, which happens
  // when collecting thread-stack roots using checkpoint.
  Mutex mark_stack_lock_;
  accounting::ObjectStack* mark_stack_;
  // Special bitmap wherein all the bits corresponding to an object are set.
  // TODO: make LiveWordsBitmap encapsulated in this class rather than a
  // pointer. We tend to access its members in performance-sensitive
  // code-path. Also, use a single MemMap for all the GC's data structures,
  // which we will clear in the end. This would help in limiting the number of
  // VMAs that get created in the kernel.
  std::unique_ptr<LiveWordsBitmap<kAlignment>> live_words_bitmap_;
  // Track GC-roots updated so far in a GC-cycle. This is to confirm that no
  // GC-root is updated twice.
  // TODO: Must be replaced with an efficient mechanism eventually. Or ensure
  // that double updation doesn't happen in the first place.
  std::unordered_set<void*> updated_roots_;
  // Set of dex-caches visited during marking. See comment above
  // RememberDexCaches() for the explanation.
  std::unordered_set<uint32_t> dex_caches_;
  MemMap from_space_map_;
  // Any array of live-bytes in logical chunks of kOffsetChunkSize size
  // in the 'to-be-compacted' space.
  MemMap info_map_;
  // The main space bitmap
  accounting::ContinuousSpaceBitmap* moving_space_bitmap_;
  accounting::ContinuousSpaceBitmap* non_moving_space_bitmap_;
  space::ContinuousSpace* non_moving_space_;
  space::BumpPointerSpace* const bump_pointer_space_;
  Thread* thread_running_gc_;
  size_t vector_length_;
  size_t live_stack_freeze_size_;

  // For every page in the to-space (post-compact heap) we need to know the
  // first object from which we must compact and/or update references. This is
  // for both non-moving and moving space. Additionally, for the moving-space,
  // we also need the offset within the object from where we need to start
  // copying.
  // chunk_info_vec_ holds live bytes for chunks during marking phase. After
  // marking we perform an exclusive scan to compute offset for every chunk.
  uint32_t* chunk_info_vec_;
  // Array holding offset within first-object from where contents should be
  // copied for a given post-compact page.
  // For black-alloc pages, the array stores first-chunk-size.
  union {
    uint32_t* pre_compact_offset_moving_space_;
    uint32_t* black_alloc_pages_first_chunk_size_;
  };
  // first_objs_moving_space_[i] is the address of the first object for the ith page
  ObjReference* first_objs_moving_space_;
  ObjReference* first_objs_non_moving_space_;
  size_t non_moving_first_objs_count_;
  // Number of first-objects in the moving space.
  size_t moving_first_objs_count_;
  // Number of pages consumed by black objects.
  size_t black_page_count_;

  uint8_t* from_space_begin_;
  // moving-space's end pointer at the marking pause. All allocations beyond
  // this will be considered black in the current GC cycle. Aligned up to page
  // size.
  uint8_t* black_allocations_begin_;
  // End of compacted space. Use for computing post-compact addr of black
  // allocated objects. Aligned up to page size.
  uint8_t* post_compact_end_;
  // Cache (black_allocations_begin_ - post_compact_end_) for post-compact
  // address computations.
  ptrdiff_t black_objs_slide_diff_;
  // Cache (from_space_begin_ - bump_pointer_space_->Begin()) so that we can
  // compute from-space address of a given pre-comapct addr efficiently.
  ptrdiff_t from_space_slide_diff_;

  // TODO: Remove once an efficient mechanism to deal with double root updation
  // is incorporated.
  void* stack_addr_;
  void* stack_end_;
  // Set to true when compacting starts.
  bool compacting_;

  class VerifyRootMarkedVisitor;
  class ScanObjectVisitor;
  class CheckpointMarkThreadRoots;
  template<size_t kBufferSize> class ThreadRootsVisitor;
  class CardModifiedVisitor;
  class RefFieldsVisitor;
  template <bool kCheckBegin, bool kCheckEnd> class RefsUpdateVisitor;
  class NativeRootsUpdateVisitor;
  class ImmuneSpaceUpdateObjVisitor;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MarkCompact);
};

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
