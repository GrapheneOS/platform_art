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

#include <signal.h>

#include <map>
#include <memory>
#include <unordered_set>

#include "barrier.h"
#include "base/atomic.h"
#include "base/bit_vector.h"
#include "base/gc_visited_arena_pool.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/bitmap-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc_root.h"
#include "immune_spaces.h"
#include "offsets.h"

namespace art HIDDEN {

EXPORT bool KernelSupportsUffd();

namespace mirror {
class DexCache;
}  // namespace mirror

namespace gc {

class Heap;

namespace space {
class BumpPointerSpace;
}  // namespace space

namespace collector {
class MarkCompact;

// The actual young GC code is also implemented in MarkCompact class. However,
// using this class saves us from creating duplicate data-structures, which
// would have happened with two instances of MarkCompact.
class YoungMarkCompact final : public GarbageCollector {
 public:
  YoungMarkCompact(Heap* heap, MarkCompact* main);

  void RunPhases() override REQUIRES(!Locks::mutator_lock_);

  GcType GetGcType() const override { return kGcTypeSticky; }

  CollectorType GetCollectorType() const override { return kCollectorTypeCMC; }

  // None of the following methods are ever called as actual GC is performed by MarkCompact.

  mirror::Object* MarkObject([[maybe_unused]] mirror::Object* obj) override {
    UNIMPLEMENTED(FATAL);
    UNREACHABLE();
  }
  void MarkHeapReference([[maybe_unused]] mirror::HeapReference<mirror::Object>* obj,
                         [[maybe_unused]] bool do_atomic_update) override {
    UNIMPLEMENTED(FATAL);
  }
  void VisitRoots([[maybe_unused]] mirror::Object*** roots,
                  [[maybe_unused]] size_t count,
                  [[maybe_unused]] const RootInfo& info) override {
    UNIMPLEMENTED(FATAL);
  }
  void VisitRoots([[maybe_unused]] mirror::CompressedReference<mirror::Object>** roots,
                  [[maybe_unused]] size_t count,
                  [[maybe_unused]] const RootInfo& info) override {
    UNIMPLEMENTED(FATAL);
  }
  bool IsNullOrMarkedHeapReference([[maybe_unused]] mirror::HeapReference<mirror::Object>* obj,
                                   [[maybe_unused]] bool do_atomic_update) override {
    UNIMPLEMENTED(FATAL);
    UNREACHABLE();
  }
  void RevokeAllThreadLocalBuffers() override { UNIMPLEMENTED(FATAL); }

  void DelayReferenceReferent([[maybe_unused]] ObjPtr<mirror::Class> klass,
                              [[maybe_unused]] ObjPtr<mirror::Reference> reference) override {
    UNIMPLEMENTED(FATAL);
  }
  mirror::Object* IsMarked([[maybe_unused]] mirror::Object* obj) override {
    UNIMPLEMENTED(FATAL);
    UNREACHABLE();
  }
  void ProcessMarkStack() override { UNIMPLEMENTED(FATAL); }

 private:
  MarkCompact* const main_collector_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(YoungMarkCompact);
};

class MarkCompact final : public GarbageCollector {
 public:
  using SigbusCounterType = uint32_t;

  static constexpr size_t kAlignment = kObjectAlignment;
  static constexpr int kCopyMode = -1;
  // Fake file descriptor for fall back mode (when uffd isn't available)
  static constexpr int kFallbackMode = -3;
  static constexpr int kFdUnused = -2;

  // Bitmask for the compaction-done bit in the sigbus_in_progress_count_.
  static constexpr SigbusCounterType kSigbusCounterCompactionDoneMask =
      1u << (BitSizeOf<SigbusCounterType>() - 1);

  explicit MarkCompact(Heap* heap);

  ~MarkCompact() {}

  void RunPhases() override REQUIRES(!Locks::mutator_lock_, !lock_);

  void ClampGrowthLimit(size_t new_capacity) REQUIRES(Locks::heap_bitmap_lock_);
  // Updated before (or in) pre-compaction pause and is accessed only in the
  // pause or during concurrent compaction. The flag is reset in next GC cycle's
  // InitializePhase(). Therefore, it's safe to update without any memory ordering.
  bool IsCompacting() const { return compacting_; }

  // Called by SIGBUS handler. NO_THREAD_SAFETY_ANALYSIS for mutator-lock, which
  // is asserted in the function.
  bool SigbusHandler(siginfo_t* info) REQUIRES(!lock_) NO_THREAD_SAFETY_ANALYSIS;

  GcType GetGcType() const override { return kGcTypePartial; }

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

  mirror::Object* GetFromSpaceAddrFromBarrier(mirror::Object* old_ref) {
    CHECK(compacting_);
    if (HasAddress(old_ref)) {
      return GetFromSpaceAddr(old_ref);
    }
    return old_ref;
  }
  // Called from Heap::PostForkChildAction() for non-zygote processes and from
  // PrepareForCompaction() for zygote processes. Returns true if uffd was
  // created or was already done.
  bool CreateUserfaultfd(bool post_fork);

  // Returns a pair indicating if userfaultfd itself is available (first) and if
  // so then whether its minor-fault feature is available or not (second).
  static std::pair<bool, bool> GetUffdAndMinorFault();

  // Add linear-alloc space data when a new space is added to
  // GcVisitedArenaPool, which mostly happens only once.
  void AddLinearAllocSpaceData(uint8_t* begin, size_t len);

  // Called by Heap::PreZygoteFork() to reset generational heap pointers and
  // other data structures as the moving space gets completely evicted into new
  // zygote-space.
  void ResetGenerationalState();

  // In copy-mode of userfaultfd, we don't need to reach a 'processed' state as
  // it's given that processing thread also copies the page, thereby mapping it.
  // The order is important as we may treat them as integers. Also
  // 'kUnprocessed' should be set to 0 as we rely on madvise(dontneed) to return
  // us zero'ed pages, which implicitly makes page-status initialized to 'kUnprocessed'.
  enum class PageState : uint8_t {
    kUnprocessed = 0,           // Not processed yet.
    kProcessing = 1,            // Being processed by GC thread and will not be mapped
    kProcessed = 2,             // Processed but not mapped
    kProcessingAndMapping = 3,  // Being processed by GC or mutator and will be mapped
    kMutatorProcessing = 4,     // Being processed by mutator thread
    kProcessedAndMapping = 5,   // Processed and will be mapped
    kProcessedAndMapped = 6     // Processed and mapped. For SIGBUS.
  };

  // Different heap clamping states.
  enum class ClampInfoStatus : uint8_t {
    kClampInfoNotDone,
    kClampInfoPending,
    kClampInfoFinished
  };

  friend void YoungMarkCompact::RunPhases();

 private:
  using ObjReference = mirror::CompressedReference<mirror::Object>;
  static constexpr uint32_t kPageStateMask = (1 << BitSizeOf<uint8_t>()) - 1;
  // Number of bits (live-words) covered by a single chunk-info (below)
  // entry/word.
  // TODO: Since popcount is performed usomg SIMD instructions, we should
  // consider using 128-bit in order to halve the chunk-info size.
  static constexpr uint32_t kBitsPerVectorWord = kBitsPerIntPtrT;
  static constexpr uint32_t kOffsetChunkSize = kBitsPerVectorWord * kAlignment;
  static_assert(kOffsetChunkSize < kMinPageSize);
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
    using MemRangeBitmap::SetBitmapSize;
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

  static bool HasAddress(mirror::Object* obj, uint8_t* begin, uint8_t* end) {
    uint8_t* ptr = reinterpret_cast<uint8_t*>(obj);
    return ptr >= begin && ptr < end;
  }

  bool HasAddress(mirror::Object* obj) const {
    return HasAddress(obj, moving_space_begin_, moving_space_end_);
  }
  // For a given object address in pre-compact space, return the corresponding
  // address in the from-space, where heap pages are relocated in the compaction
  // pause.
  mirror::Object* GetFromSpaceAddr(mirror::Object* obj) const {
    DCHECK(HasAddress(obj)) << " obj=" << obj;
    return reinterpret_cast<mirror::Object*>(reinterpret_cast<uintptr_t>(obj)
                                             + from_space_slide_diff_);
  }

  inline bool IsOnAllocStack(mirror::Object* ref)
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Verifies that that given object reference refers to a valid object.
  // Otherwise fataly dumps logs, including those from callback.
  template <typename Callback>
  void VerifyObject(mirror::Object* ref, Callback& callback) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Check if the obj is within heap and has a klass which is likely to be valid
  // mirror::Class.
  bool IsValidObject(mirror::Object* obj) const REQUIRES_SHARED(Locks::mutator_lock_);
  void InitializePhase();
  void FinishPhase(bool performed_compaction)
      REQUIRES(!Locks::mutator_lock_, !Locks::heap_bitmap_lock_, !lock_);
  void MarkingPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);
  void CompactionPhase() REQUIRES_SHARED(Locks::mutator_lock_);

  void SweepSystemWeaks(Thread* self, Runtime* runtime, const bool paused)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);
  // Update the reference at 'offset' in 'obj' with post-compact address, and
  // return the new address. [begin, end) is a range in which compaction is
  // happening. So post-compact address needs to be computed only for
  // pre-compact references in this range.
  ALWAYS_INLINE mirror::Object* UpdateRef(mirror::Object* obj,
                                          MemberOffset offset,
                                          uint8_t* begin,
                                          uint8_t* end) REQUIRES_SHARED(Locks::mutator_lock_);

  // Verify that the gc-root is updated only once. Returns false if the update
  // shouldn't be done.
  ALWAYS_INLINE bool VerifyRootSingleUpdate(void* root,
                                            mirror::Object* old_ref,
                                            const RootInfo& info)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update the given root with post-compact address and return the new address. [begin, end)
  // is a range in which compaction is happening. So post-compact address needs to be computed
  // only for pre-compact references in this range.
  ALWAYS_INLINE mirror::Object* UpdateRoot(mirror::CompressedReference<mirror::Object>* root,
                                           uint8_t* begin,
                                           uint8_t* end,
                                           const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  ALWAYS_INLINE mirror::Object* UpdateRoot(mirror::Object** root,
                                           uint8_t* begin,
                                           uint8_t* end,
                                           const RootInfo& info = RootInfo(RootType::kRootUnknown))
      REQUIRES_SHARED(Locks::mutator_lock_);
  // If the given pre-compact address (old_ref) is in [begin, end) range of moving-space,
  // then the function returns the computed post-compact address. Otherwise, 'old_ref' is
  // returned.
  ALWAYS_INLINE mirror::Object* PostCompactAddress(mirror::Object* old_ref,
                                                   uint8_t* begin,
                                                   uint8_t* end) const
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
  // Clears (for alloc spaces in the beginning of marking phase) or ages the
  // card table. Also, identifies immune spaces and mark bitmap.
  void PrepareForMarking(bool pre_marking) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Perform one last round of marking, identifying roots from dirty cards
  // during a stop-the-world (STW) pause.
  void MarkingPause() REQUIRES(Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  // Perform stop-the-world pause prior to concurrent compaction.
  // Updates GC-roots and protects heap so that during the concurrent
  // compaction phase we can receive faults and compact the corresponding pages
  // on the fly.
  void CompactionPause() REQUIRES(Locks::mutator_lock_);
  // Compute offsets (in chunk_info_vec_) and other data structures required
  // during concurrent compaction. Also determines a black-dense region at the
  // beginning of the moving space which is not compacted. Returns false if
  // performing compaction isn't required.
  bool PrepareForCompaction() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);

  // Copy gPageSize live bytes starting from 'offset' (within the moving space),
  // which must be within 'obj', into the gPageSize sized memory pointed by 'addr'.
  // Then update the references within the copied objects. The boundary objects are
  // partially updated such that only the references that lie in the page are updated.
  // This is necessary to avoid cascading userfaults.
  template <bool kSetupForGenerational>
  void CompactPage(mirror::Object* obj,
                   uint32_t offset,
                   uint8_t* addr,
                   uint8_t* to_space_addr,
                   bool needs_memset_zero) REQUIRES_SHARED(Locks::mutator_lock_);
  // Compact the bump-pointer space. Pass page that should be used as buffer for
  // userfaultfd.
  template <int kMode>
  void CompactMovingSpace(uint8_t* page) REQUIRES_SHARED(Locks::mutator_lock_);

  // Compact the given page as per func and change its state. Also map/copy the
  // page, if required. Returns true if the page was compacted, else false.
  template <int kMode, typename CompactionFn>
  ALWAYS_INLINE bool DoPageCompactionWithStateChange(size_t page_idx,
                                                     uint8_t* to_space_page,
                                                     uint8_t* page,
                                                     bool map_immediately,
                                                     CompactionFn func)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Update all the objects in the given non-moving page. 'first' object
  // could have started in some preceding page.
  template <bool kSetupForGenerational>
  void UpdateNonMovingPage(mirror::Object* first,
                           uint8_t* page,
                           ptrdiff_t from_space_diff,
                           accounting::ContinuousSpaceBitmap* bitmap)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Update all the references in the non-moving space.
  void UpdateNonMovingSpace() REQUIRES_SHARED(Locks::mutator_lock_);

  // For all the pages in non-moving space, find the first object that overlaps
  // with the pages' start address, and store in first_objs_non_moving_space_ array.
  size_t InitNonMovingFirstObjects(uintptr_t begin,
                                   uintptr_t end,
                                   accounting::ContinuousSpaceBitmap* bitmap,
                                   ObjReference* first_objs_arr)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // In addition to the first-objects for every post-compact moving space page,
  // also find offsets within those objects from where the contents should be
  // copied to the page. The offsets are relative to the moving-space's
  // beginning. Store the computed first-object and offset in first_objs_moving_space_
  // and pre_compact_offset_moving_space_ respectively.
  void InitMovingSpaceFirstObjects(size_t vec_len, size_t to_space_page_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Gather the info related to black allocations from bump-pointer space to
  // enable concurrent sliding of these pages.
  void UpdateMovingSpaceBlackAllocations() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);
  // Update first-object info from allocation-stack for non-moving space black
  // allocations.
  void UpdateNonMovingSpaceBlackAllocations() REQUIRES(Locks::mutator_lock_, Locks::heap_bitmap_lock_);

  // Slides (retain the empty holes, which are usually part of some in-use TLAB)
  // black page in the moving space. 'first_obj' is the object that overlaps with
  // the first byte of the page being slid. pre_compact_page is the pre-compact
  // address of the page being slid. 'dest' is the gPageSize sized memory where
  // the contents would be copied.
  void SlideBlackPage(mirror::Object* first_obj,
                      mirror::Object* next_page_first_obj,
                      uint32_t first_chunk_size,
                      uint8_t* const pre_compact_page,
                      uint8_t* dest,
                      bool needs_memset_zero) REQUIRES_SHARED(Locks::mutator_lock_);

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
  // Also updates freed_objects_ counter.
  void UpdateLivenessInfo(mirror::Object* obj, size_t obj_size)
      REQUIRES_SHARED(Locks::mutator_lock_);

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

  // Perform all kernel operations required for concurrent compaction. Includes
  // mremap to move pre-compact pages to from-space, followed by userfaultfd
  // registration on the moving space and linear-alloc.
  void KernelPreparation();
  // Called by KernelPreparation() for every memory range being prepared for
  // userfaultfd registration.
  void KernelPrepareRangeForUffd(uint8_t* to_addr, uint8_t* from_addr, size_t map_size);

  void RegisterUffd(void* addr, size_t size);
  void UnregisterUffd(uint8_t* start, size_t len);

  // Called by SIGBUS handler to compact and copy/map the fault page in moving space.
  void ConcurrentlyProcessMovingPage(uint8_t* fault_page,
                                     uint8_t* buf,
                                     size_t nr_moving_space_used_pages,
                                     bool tolerate_enoent) REQUIRES_SHARED(Locks::mutator_lock_);
  // Called by SIGBUS handler to process and copy/map the fault page in linear-alloc.
  void ConcurrentlyProcessLinearAllocPage(uint8_t* fault_page, bool tolerate_enoent)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Process concurrently all the pages in linear-alloc. Called by gc-thread.
  void ProcessLinearAlloc() REQUIRES_SHARED(Locks::mutator_lock_);

  // Does the following:
  // 1. Checks the status of to-space pages in [cur_page_idx,
  //    last_checked_reclaim_page_idx_) range to see whether the corresponding
  //    from-space pages can be reused.
  // 2. Taking into consideration classes which are allocated after their
  //    objects (in address order), computes the page (in from-space) from which
  //    actual reclamation can be done.
  // 3. Map the pages in [cur_page_idx, end_idx_for_mapping) range.
  // 4. Madvise the pages in [page from (2), last_reclaimed_page_)
  bool FreeFromSpacePages(size_t cur_page_idx, int mode, size_t end_idx_for_mapping)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Maps moving space pages in [start_idx, arr_len) range. It fetches the page
  // address containing the compacted content from moving_pages_status_ array.
  // 'from_fault' is true when called from userfault (sigbus handler).
  // 'return_on_contention' is set to true by gc-thread while it is compacting
  // pages. In the end it calls the function with `return_on_contention=false`
  // to ensure all pages are mapped. Returns number of pages that are mapped.
  size_t MapMovingSpacePages(size_t start_idx,
                             size_t arr_len,
                             bool from_fault,
                             bool return_on_contention,
                             bool tolerate_enoent) REQUIRES_SHARED(Locks::mutator_lock_);

  bool IsValidFd(int fd) const { return fd >= 0; }

  PageState GetPageStateFromWord(uint32_t page_word) {
    return static_cast<PageState>(static_cast<uint8_t>(page_word));
  }

  PageState GetMovingPageState(size_t idx) {
    return GetPageStateFromWord(moving_pages_status_[idx].load(std::memory_order_acquire));
  }

  // Add/update <class, obj> pair if class > obj and obj is the lowest address
  // object of class.
  ALWAYS_INLINE void UpdateClassAfterObjectMap(mirror::Object* obj)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void MarkZygoteLargeObjects() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Map zero-pages in the given range. 'tolerate_eexist' and 'tolerate_enoent'
  // help us decide if we should expect EEXIST or ENOENT back from the ioctl
  // respectively. It may return after mapping fewer pages than requested.
  // found to be contended, then we delay the operations based on thread's
  // Returns number of bytes (multiple of page-size) now known to be mapped.
  size_t ZeropageIoctl(void* addr, size_t length, bool tolerate_eexist, bool tolerate_enoent);
  // Map 'buffer' to 'dst', both being 'length' bytes using at most one ioctl
  // call. 'return_on_contention' indicates that the function should return
  // as soon as mmap_lock contention is detected. Like ZeropageIoctl(), this
  // function also uses thread's priority to decide how long we delay before
  // forcing the ioctl operation. If ioctl returns EEXIST, then also function
  // returns. Returns number of bytes (multiple of page-size) mapped.
  size_t CopyIoctl(
      void* dst, void* buffer, size_t length, bool return_on_contention, bool tolerate_enoent);

  // Called after updating linear-alloc page(s) to map the page. It first
  // updates the state of the pages to kProcessedAndMapping and after ioctl to
  // kProcessedAndMapped. Returns true if at least the first page is now mapped.
  // If 'free_pages' is true then also frees shadow pages. If 'single_ioctl'
  // is true, then stops after first ioctl.
  bool MapUpdatedLinearAllocPages(uint8_t* start_page,
                                  uint8_t* start_shadow_page,
                                  Atomic<PageState>* state,
                                  size_t length,
                                  bool free_pages,
                                  bool single_ioctl,
                                  bool tolerate_enoent);
  // Called for clamping of 'info_map_' and other GC data structures, which are
  // small and/or in >4GB address space. There is no real benefit of clamping
  // them synchronously during app forking. It clamps only if clamp_info_map_status_
  // is set to kClampInfoPending, which is done by ClampGrowthLimit().
  void MaybeClampGcStructures() REQUIRES(Locks::heap_bitmap_lock_);

  size_t ComputeInfoMapSize();
  // Initialize all the info-map related fields of this GC. Returns total size
  // of all the structures in info-map.
  size_t InitializeInfoMap(uint8_t* p, size_t moving_space_sz);
  // Update class-table classes in compaction pause if we are running in debuggable
  // mode. Only visit class-table in image spaces if 'immune_class_table_only'
  // is true.
  void UpdateClassTableClasses(Runtime* runtime, bool immune_class_table_only)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void SweepArray(accounting::ObjectStack* obj_arr, bool swap_bitmaps)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  // Set bit corresponding to 'obj' in 'mid_to_old_promo_bit_vec_' bit-vector.
  // 'obj' is the post-compacted object in mid-gen, which will get promoted to
  // old-gen and hence 'mid_to_old_promo_bit_vec_' is copied into mark-bitmap at
  // the end of GC for next GC cycle.
  void SetBitForMidToOldPromotion(uint8_t* obj);
  // Scan old-gen for young GCs by looking for cards that are at least 'aged' in
  // the card-table corresponding to moving and non-moving spaces.
  void ScanOldGenObjects() REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_);

  // Verify that cards corresponding to objects containing references to
  // young-gen are dirty.
  void VerifyNoMissingCardMarks() REQUIRES(Locks::heap_bitmap_lock_, Locks::mutator_lock_);
  // Verify that post-GC objects (all objects except the ones allocated after
  // marking pause) are valid with valid references in them. Bitmap corresponding
  // to [moving_space_begin_, mark_bitmap_clear_end) was retained. This is used in
  // case compaction is skipped.
  void VerifyPostGCObjects(bool performed_compaction, uint8_t* mark_bitmap_clear_end)
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_);

  // For checkpoints
  Barrier gc_barrier_;
  // Required only when mark-stack is accessed in shared mode, which happens
  // when collecting thread-stack roots using checkpoint. Otherwise, we use it
  // to synchronize on updated_roots_ in debug-builds.
  Mutex lock_;
  // Counters to synchronize mutator threads and gc-thread at the end of
  // compaction. Counter 0 represents the number of mutators still working on
  // moving space pages which started before gc-thread finished compacting pages,
  // whereas the counter 1 represents those which started afterwards but
  // before unregistering the space from uffd. Once counter 1 reaches 0, the
  // gc-thread madvises spaces and data structures like page-status array.
  // Both the counters are set to 0 before compaction begins. They are or'ed
  // with kSigbusCounterCompactionDoneMask one-by-one by gc-thread after
  // compaction to communicate the status to future mutators.
  std::atomic<SigbusCounterType> sigbus_in_progress_count_[2];
  MemMap from_space_map_;
  // Any array of live-bytes in logical chunks of kOffsetChunkSize size
  // in the 'to-be-compacted' space.
  MemMap info_map_;
  // Set of page-sized buffers used for compaction. The first page is used by
  // the GC thread. Subdequent pages are used by mutator threads in case of
  // SIGBUS feature, and by uffd-worker threads otherwise. In the latter case
  // the first page is also used for termination of concurrent compaction by
  // making worker threads terminate the userfaultfd read loop.
  MemMap compaction_buffers_map_;

  class LessByArenaAddr {
   public:
    bool operator()(const TrackedArena* a, const TrackedArena* b) const {
      return std::less<uint8_t*>{}(a->Begin(), b->Begin());
    }
  };

  // Map of arenas allocated in LinearAlloc arena-pool and last non-zero page,
  // captured during compaction pause for concurrent updates.
  std::map<const TrackedArena*, uint8_t*, LessByArenaAddr> linear_alloc_arenas_;
  // Set of PageStatus arrays, one per arena-pool space. It's extremely rare to
  // have more than one, but this is to be ready for the worst case.
  class LinearAllocSpaceData {
   public:
    LinearAllocSpaceData(MemMap&& shadow, MemMap&& page_status_map, uint8_t* begin, uint8_t* end)
        : shadow_(std::move(shadow)),
          page_status_map_(std::move(page_status_map)),
          begin_(begin),
          end_(end) {}

    MemMap shadow_;
    MemMap page_status_map_;
    uint8_t* begin_;
    uint8_t* end_;
  };
  std::vector<LinearAllocSpaceData> linear_alloc_spaces_data_;

  class LessByObjReference {
   public:
    bool operator()(const ObjReference& a, const ObjReference& b) const {
      return std::less<mirror::Object*>{}(a.AsMirrorPtr(), b.AsMirrorPtr());
    }
  };
  using ClassAfterObjectMap = std::map<ObjReference, ObjReference, LessByObjReference>;
  // map of <K, V> such that the class K (in moving space) is after its
  // objects, and its object V is the lowest object (in moving space).
  ClassAfterObjectMap class_after_obj_map_;
  // Since the compaction is done in reverse, we use a reverse iterator. It is maintained
  // either at the pair whose class is lower than the first page to be freed, or at the
  // pair whose object is not yet compacted.
  ClassAfterObjectMap::const_reverse_iterator class_after_obj_iter_;
  // Every object inside the immune spaces is assumed to be marked.
  ImmuneSpaces immune_spaces_;
  // Bit-vector to store bits for objects which are promoted from mid-gen to
  // old-gen during compaction. Later in FinishPhase() it's copied into
  // mark-bitmap of moving-space.
  std::unique_ptr<BitVector> mid_to_old_promo_bit_vec_;

  // List of objects found to have native gc-roots into young-gen during
  // marking. Cards corresponding to these objects are dirtied at the end of GC.
  // These have to be captured during marking phase as we don't update
  // native-roots during compaction.
  std::vector<mirror::Object*> dirty_cards_later_vec_;
  space::ContinuousSpace* non_moving_space_;
  space::BumpPointerSpace* const bump_pointer_space_;
  Thread* thread_running_gc_;
  // Length of 'chunk_info_vec_' vector (defined below).
  size_t vector_length_;
  size_t live_stack_freeze_size_;
  size_t non_moving_first_objs_count_;
  // Length of first_objs_moving_space_ and pre_compact_offset_moving_space_
  // arrays. Also the number of pages which are to be compacted.
  size_t moving_first_objs_count_;
  // Number of pages containing black-allocated objects, indicating number of
  // pages to be slid.
  size_t black_page_count_;
  // Used by FreeFromSpacePages() for maintaining markers in the moving space for
  // how far the pages have been reclaimed (madvised) and checked.
  //
  // Pages from this index to the end of to-space have been checked (via page_status)
  // and their corresponding from-space pages are reclaimable.
  size_t last_checked_reclaim_page_idx_;
  // All from-space pages in [last_reclaimed_page_, from_space->End()) are
  // reclaimed (madvised). Pages in [from-space page corresponding to
  // last_checked_reclaim_page_idx_, last_reclaimed_page_) are not reclaimed as
  // they may contain classes required for class hierarchy traversal for
  // visiting references during compaction.
  uint8_t* last_reclaimed_page_;
  // All the pages in [last_reclaimable_page_, last_reclaimed_page_) in
  // from-space are available to store compacted contents for batching until the
  // next time madvise is called.
  uint8_t* last_reclaimable_page_;
  // [cur_reclaimable_page_, last_reclaimed_page_) have been used to store
  // compacted contents for batching.
  uint8_t* cur_reclaimable_page_;

  // Mark bits for non-moving space
  accounting::ContinuousSpaceBitmap* non_moving_space_bitmap_;
  // Array of moving-space's pages' compaction status, which is stored in the
  // least-significant byte. kProcessed entries also contain the from-space
  // offset of the page which contains the compacted contents of the ith
  // to-space page.
  Atomic<uint32_t>* moving_pages_status_;
  // For pages before black allocations, pre_compact_offset_moving_space_[i]
  // holds offset within the space from where the objects need to be copied in
  // the ith post-compact page.
  // Otherwise, black_alloc_pages_first_chunk_size_[i] holds the size of first
  // non-empty chunk in the ith black-allocations page.
  union {
    uint32_t* pre_compact_offset_moving_space_;
    uint32_t* black_alloc_pages_first_chunk_size_;
  };
  // first_objs_moving_space_[i] is the pre-compact address of the object which
  // would overlap with the starting boundary of the ith post-compact page.
  ObjReference* first_objs_moving_space_;
  // First object for every page. It could be greater than the page's start
  // address, or null if the page is empty.
  ObjReference* first_objs_non_moving_space_;

  // Cache (from_space_begin_ - bump_pointer_space_->Begin()) so that we can
  // compute from-space address of a given pre-comapct address efficiently.
  ptrdiff_t from_space_slide_diff_;
  uint8_t* from_space_begin_;

  // The moving space markers are ordered as follows:
  // [moving_space_begin_, black_dense_end_, mid_gen_end_, post_compact_end_, moving_space_end_)

  // End of compacted space. Used for computing post-compact address of black
  // allocated objects. Aligned up to page size.
  uint8_t* post_compact_end_;

  // BEGIN HOT FIELDS: accessed per object

  accounting::ObjectStack* mark_stack_;
  uint64_t bytes_scanned_;
  // Number of objects freed during this GC in moving space. It is decremented
  // every time an object is discovered. And total-object count is added to it
  // in MarkingPause(). It reaches the correct count only once the marking phase
  // is completed.
  int32_t freed_objects_;
  // Set to true when doing young gen collection.
  bool young_gen_;
  const bool use_generational_;
  // True while compacting.
  bool compacting_;
  // Mark bits for main space
  accounting::ContinuousSpaceBitmap* const moving_space_bitmap_;
  // Cached values of moving-space range to optimize checking if reference
  // belongs to moving-space or not. May get updated if and when heap is clamped.
  uint8_t* const moving_space_begin_;
  uint8_t* moving_space_end_;
  // In generational-mode, we maintain 3 generations: young, mid, and old.
  // Mid generation is collected during young collections. This means objects
  // need to survive two GCs before they get promoted to old-gen. This helps
  // in avoiding pre-mature promotion of objects which are allocated just
  // prior to a young collection but are short-lived.

  // Set to moving_space_begin_ if compacting the entire moving space.
  // Otherwise, set to a page-aligned address such that [moving_space_begin_,
  // black_dense_end_) is considered to be densely populated with reachable
  // objects and hence is not compacted. In generational mode, old-gen is
  // treated just like black-dense region.
  union {
    uint8_t* black_dense_end_;
    uint8_t* old_gen_end_;
  };
  // Prior to compaction, 'mid_gen_end_' represents end of 'pre-compacted'
  // mid-gen. During compaction, it represents 'post-compacted' end of mid-gen.
  // This is done in PrepareForCompaction(). At the end of GC, in FinishPhase(),
  // mid-gen gets consumed/promoted to old-gen, and young-gen becomes mid-gen,
  // in preparation for the next GC cycle.
  uint8_t* mid_gen_end_;

  // BEGIN HOT FIELDS: accessed per reference update

  // Special bitmap wherein all the bits corresponding to an object are set.
  // TODO: make LiveWordsBitmap encapsulated in this class rather than a
  // pointer. We tend to access its members in performance-sensitive
  // code-path. Also, use a single MemMap for all the GC's data structures,
  // which we will clear in the end. This would help in limiting the number of
  // VMAs that get created in the kernel.
  std::unique_ptr<LiveWordsBitmap<kAlignment>> live_words_bitmap_;
  // For every page in the to-space (post-compact heap) we need to know the
  // first object from which we must compact and/or update references. This is
  // for both non-moving and moving space. Additionally, for the moving-space,
  // we also need the offset within the object from where we need to start
  // copying.
  // chunk_info_vec_ holds live bytes for chunks during marking phase. After
  // marking we perform an exclusive scan to compute offset for every chunk.
  uint32_t* chunk_info_vec_;
  // moving-space's end pointer at the marking pause. All allocations beyond
  // this will be considered black in the current GC cycle. Aligned up to page
  // size.
  uint8_t* black_allocations_begin_;
  // Cache (black_allocations_begin_ - post_compact_end_) for post-compact
  // address computations.
  ptrdiff_t black_objs_slide_diff_;

  // END HOT FIELDS: accessed per reference update
  // END HOT FIELDS: accessed per object

  uint8_t* conc_compaction_termination_page_;
  PointerSize pointer_size_;
  // Userfault file descriptor, accessed only by the GC itself.
  // kFallbackMode value indicates that we are in the fallback mode.
  int uffd_;
  // When using SIGBUS feature, this counter is used by mutators to claim a page
  // out of compaction buffers to be used for the entire compaction cycle.
  std::atomic<uint16_t> compaction_buffer_counter_;
  // Set to true in MarkingPause() to indicate when allocation_stack_ should be
  // checked in IsMarked() for black allocations.
  bool marking_done_;
  // Flag indicating whether one-time uffd initialization has been done. It will
  // be false on the first GC for non-zygote processes, and always for zygote.
  // Its purpose is to minimize the userfaultfd overhead to the minimal in
  // Heap::PostForkChildAction() as it's invoked in app startup path. With
  // this, we register the compaction-termination page on the first GC.
  bool uffd_initialized_;
  // Clamping statue of `info_map_`. Initialized with 'NotDone'. Once heap is
  // clamped but info_map_ is delayed, we set it to 'Pending'. Once 'info_map_'
  // is also clamped, then we set it to 'Finished'.
  ClampInfoStatus clamp_info_map_status_;

  // Track GC-roots updated so far in a GC-cycle. This is to confirm that no
  // GC-root is updated twice.
  // TODO: Must be replaced with an efficient mechanism eventually. Or ensure
  // that double updation doesn't happen in the first place.
  std::unique_ptr<std::unordered_set<void*>> updated_roots_ GUARDED_BY(lock_);
  // TODO: Remove once an efficient mechanism to deal with double root updation
  // is incorporated.
  void* stack_high_addr_;
  void* stack_low_addr_;
  // Following values for logging purposes
  void* prev_post_compact_end_;
  void* prev_black_dense_end_;
  void* prev_black_allocations_begin_;
  bool prev_gc_young_;
  bool prev_gc_performed_compaction_;
  // Timestamp when the read-barrier is enabled
  uint64_t app_slow_path_start_time_;

  class FlipCallback;
  class ThreadFlipVisitor;
  class VerifyRootMarkedVisitor;
  class ScanObjectVisitor;
  class CheckpointMarkThreadRoots;
  template <size_t kBufferSize>
  class ThreadRootsVisitor;
  class RefFieldsVisitor;
  template <bool kCheckBegin, bool kCheckEnd, bool kDirtyOldToMid = false>
  class RefsUpdateVisitor;
  class ArenaPoolPageUpdater;
  class ClassLoaderRootsUpdater;
  class LinearAllocPageUpdater;
  class ImmuneSpaceUpdateObjVisitor;
  template <typename Visitor>
  class VisitReferencesVisitor;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MarkCompact);
};

std::ostream& operator<<(std::ostream& os, MarkCompact::PageState value);
std::ostream& operator<<(std::ostream& os, MarkCompact::ClampInfoStatus value);

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
