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

#include "base/atomic.h"
#include "barrier.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc_root.h"
#include "immune_spaces.h"
#include "offsets.h"

namespace art {
namespace gc {

class Heap;

namespace space {
class BumpPointerSpace;
}  // namespace space

namespace collector {
class MarkCompact : public GarbageCollector {
 public:
  explicit MarkCompact(Heap* heap);

  ~MarkCompact() {}

  void RunPhases() override REQUIRES(!Locks::mutator_lock_);

  GcType GetGcType() const override {
    return kGcTypeFull;
  }

  CollectorType GetCollectorType() const override {
    return kCollectorTypeCMC;
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

 private:
  void InitializePhase();
  void FinishPhase() REQUIRES(!Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  void MarkingPhase() REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(!Locks::heap_bitmap_lock_);
  void CompactionPhase() REQUIRES_SHARED(Locks::mutator_lock_);

  void SweepSystemWeaks(Thread* self, const bool paused)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::heap_bitmap_lock_);
  // Identify immune spaces and reset card-table, mod-union-table, and mark
  // bitmaps.
  void BindAndResetBitmaps() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

  // Perform one last round of marking, identifying roots from dirty cards
  // during a stop-the-world (STW) pause
  void PausePhase() REQUIRES(Locks::mutator_lock_, !Locks::heap_bitmap_lock_);
  // Perform GC-root updation and heap protection so that during the concurrent
  // compaction phase we can receive faults and compact the corresponding pages
  // on the fly. This is performed in a STW pause.
  void PreCompactionPhase() REQUIRES(Locks::mutator_lock_);
  // Compute offset-vector and other data structures required during concurrent
  // compaction
  void PrepareForCompaction() REQUIRES_SHARED(Locks::mutator_lock_);
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
  void ProcessMarkStack() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void ExpandMarkStack() REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  void ScanObject(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);
  // Push objects to the mark-stack right after successfully marking objects.
  void PushOnMarkStack(mirror::Object* obj)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_);

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

  // For checkpoints
  std::unique_ptr<Barrier> gc_barrier_;
  // Required only when mark-stack is accessed in shared mode, which happens
  // when collecting thread-stack roots using checkpoint.
  Mutex mark_stack_lock_;
  accounting::ObjectStack* mark_stack_;
  Thread* thread_running_gc_;
  const space::BumpPointerSpace* bump_pointer_space_;
  // The main space bitmap
  accounting::ContinuousSpaceBitmap* current_space_bitmap_;
  accounting::ContinuousSpaceBitmap* non_moving_space_bitmap_;
  // Every object inside the immune spaces is assumed to be marked.
  ImmuneSpaces immune_spaces_;
  size_t live_stack_freeze_size_;

  class VerifyRootMarkedVisitor;
  class ScanObjectVisitor;
  class CheckpointMarkThreadRoots;
  template<size_t kBufferSize> class ThreadRootsVisitor;
  class CardModifiedVisitor;
  class RefFieldsVisitor;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MarkCompact);
};

}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_COMPACT_H_
