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

#ifndef ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_H_
#define ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_H_

#include <stdint.h>

#include <iosfwd>
#include <limits>
#include <string>

#include <android-base/logging.h>

#include "base/bit_utils.h"
#include "base/locks.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/mutex.h"
#include "gc_root.h"
#include "indirect_reference_table.h"
#include "obj_ptr.h"
#include "offsets.h"
#include "read_barrier_option.h"

namespace art {

class RootInfo;

namespace mirror {
class Object;
}  // namespace mirror

namespace jni {

// Maintain a table of local references.  Used for local JNI references.
// TODO: Rewrite the implementation, so that valid local references are effectively
// `CompressedReference<Object>*`, so that it can be decoded very quickly.
//
// The table contains object references, where the strong (local/global) references are part of the
// GC root set (but not the weak global references). When an object is added we return an
// IndirectRef that is not a valid pointer but can be used to find the original value in O(1) time.
// Conversions to and from indirect references are performed on upcalls and downcalls, so they need
// to be very fast.
//
// To be efficient for JNI local variable storage, we need to provide operations that allow us to
// operate on segments of the table, where segments are pushed and popped as if on a stack. For
// example, deletion of an entry should only succeed if it appears in the current segment, and we
// want to be able to strip off the current segment quickly when a method returns. Additions to the
// table must be made in the current segment even if space is available in an earlier area.
//
// A new segment is created when we call into native code from interpreted code, or when we handle
// the JNI PushLocalFrame function.
//
// The GC must be able to scan the entire table quickly.
//
// In summary, these must be very fast:
//  - adding or removing a segment
//  - adding references to a new segment
//  - converting an indirect reference back to an Object
// These can be a little slower, but must still be pretty quick:
//  - adding references to a "mature" segment
//  - removing individual references
//  - scanning the entire table straight through
//
// If there's more than one segment, we don't guarantee that the table will fill completely before
// we fail due to lack of space. We do ensure that the current segment will pack tightly, which
// should satisfy JNI requirements (e.g. EnsureLocalCapacity).

// Indirect reference definition.  This must be interchangeable with JNI's jobject, and it's
// convenient to let null be null, so we use void*.
//
// We need a (potentially) large table index and a 2-bit reference type (global, local, weak
// global). We also reserve some bits to be used to detect stale indirect references: we put a
// serial number in the extra bits, and keep a copy of the serial number in the table. This requires
// more memory and additional memory accesses on add/get, but is moving-GC safe. It will catch
// additional problems, e.g.: create iref1 for obj, delete iref1, create iref2 for same obj,
// lookup iref1. A pattern based on object bits will miss this.

// Table definition.
//
// For the global reference table, the expected common operations are adding a new entry and
// removing a recently-added entry (usually the most-recently-added entry).  For JNI local
// references, the common operations are adding a new entry and removing an entire table segment.
//
// If we delete entries from the middle of the list, we will be left with "holes".  We track the
// number of holes so that, when adding new elements, we can quickly decide to do a trivial append
// or go slot-hunting.
//
// When the top-most entry is removed, any holes immediately below it are also removed. Thus,
// deletion of an entry may reduce "top_index" by more than one.
//
// To get the desired behavior for JNI locals, we need to know the bottom and top of the current
// "segment". The top is managed internally, and the bottom is passed in as a function argument.
// When we call a native method or push a local frame, the current top index gets pushed on, and
// serves as the new bottom. When we pop a frame off, the value from the stack becomes the new top
// index, and the value stored in the previous frame becomes the new bottom.
//
// Holes are being locally cached for the segment. Otherwise we'd have to pass bottom index and
// number of holes, which restricts us to 16 bits for the top index. The value is cached within the
// table. To avoid code in generated JNI transitions, which implicitly form segments, the code for
// adding and removing references needs to detect the change of a segment. Helper fields are used
// for this detection.
//
// Common alternative implementation: make IndirectRef a pointer to the actual reference slot.
// Instead of getting a table and doing a lookup, the lookup can be done instantly. Operations like
// determining the type and deleting the reference are more expensive because the table must be
// hunted for (i.e. you have to do a pointer comparison to see which table it's in), you can't move
// the table when expanding it (so realloc() is out), and tricks like serial number checking to
// detect stale references aren't possible (though we may be able to get similar benefits with other
// approaches).
//
// TODO: consider a "lastDeleteIndex" for quick hole-filling when an add immediately follows a
// delete; must invalidate after segment pop might be worth only using it for JNI globals.
//
// TODO: may want completely different add/remove algorithms for global and local refs to improve
// performance.  A large circular buffer might reduce the amortized cost of adding global
// references.

// The state of the current segment. We only store the index. Splitting it for index and hole
// count restricts the range too much.
struct LRTSegmentState {
  uint32_t top_index;
};

// Use as initial value for "cookie", and when table has only one segment.
static constexpr LRTSegmentState kLRTFirstSegment = { 0 };

// We associate a few bits of serial number with each reference, for error checking.
static constexpr unsigned int kLRTSerialBits = 3;
static constexpr uint32_t kLRTMaxSerial = ((1 << kLRTSerialBits) - 1);

class LrtEntry {
 public:
  void Add(ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_);

  GcRoot<mirror::Object>* GetReference() {
    DCHECK_LE(serial_, kLRTMaxSerial);
    return &reference_;
  }

  const GcRoot<mirror::Object>* GetReference() const {
    DCHECK_LE(serial_, kLRTMaxSerial);
    return &reference_;
  }

  uint32_t GetSerial() const {
    return serial_;
  }

  void SetReference(ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  uint32_t serial_;  // Incremented for each reuse; checked against reference.
  GcRoot<mirror::Object> reference_;
};
static_assert(sizeof(LrtEntry) == 2 * sizeof(uint32_t), "Unexpected sizeof(LrtEntry)");
static_assert(IsPowerOfTwo(sizeof(LrtEntry)), "Unexpected sizeof(LrtEntry)");

// We initially allocate local reference tables with a very small number of entries, packing
// multiple tables into a single page. If we need to expand one, we allocate them in units of
// pages.
// TODO: We should allocate all LRT tables as nonmovable Java objects, That in turn works better
// if we break up each table into 2 parallel arrays, one for the Java reference, and one for the
// serial number. The current scheme page-aligns regions containing LRT tables, and so allows them
// to be identified and page-protected in the future.
constexpr size_t kInitialLrtBytes = 512;  // Number of bytes in an initial local table.
constexpr size_t kSmallLrtEntries = kInitialLrtBytes / sizeof(LrtEntry);
static_assert(kPageSize % kInitialLrtBytes == 0);
static_assert(kInitialLrtBytes % sizeof(LrtEntry) == 0);
static_assert(kInitialLrtBytes % sizeof(void *) == 0);

// A minimal stopgap allocator for initial small local LRT tables.
class SmallLrtAllocator {
 public:
  SmallLrtAllocator();

  // Allocate a LRT table for kSmallLrtEntries.
  LrtEntry* Allocate(std::string* error_msg) REQUIRES(!lock_);

  void Deallocate(LrtEntry* unneeded) REQUIRES(!lock_);

 private:
  // A free list of kInitialLrtBytes chunks linked through the first word.
  LrtEntry* small_lrt_freelist_;

  // Repository of MemMaps used for small LRT tables.
  std::vector<MemMap> shared_lrt_maps_;

  Mutex lock_;  // Level kGenericBottomLock; acquired before mem_map_lock_, which is a C++ mutex.
};

class LocalReferenceTable {
 public:
  // Constructs an uninitialized indirect reference table. Use `Initialize()` to initialize it.
  LocalReferenceTable();

  // Initialize the indirect reference table.
  //
  // Max_count is the minimum initial capacity (resizable).
  // A value of 1 indicates an implementation-convenient small size.
  bool Initialize(size_t max_count, std::string* error_msg);

  ~LocalReferenceTable();

  /*
   * Checks whether construction of the LocalReferenceTable succeeded.
   *
   * This object must only be used if IsValid() returns true. It is safe to
   * call IsValid from multiple threads without locking or other explicit
   * synchronization.
   */
  bool IsValid() const;

  // Add a new entry. "obj" must be a valid non-null object reference. This function will
  // return null if an error happened (with an appropriate error message set).
  IndirectRef Add(LRTSegmentState previous_state,
                  ObjPtr<mirror::Object> obj,
                  std::string* error_msg)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Given an IndirectRef in the table, return the Object it refers to.
  //
  // This function may abort under error conditions.
  template<ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<mirror::Object> Get(IndirectRef iref) const REQUIRES_SHARED(Locks::mutator_lock_)
      ALWAYS_INLINE;

  // Updates an existing indirect reference to point to a new object.
  void Update(IndirectRef iref, ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_);

  // Remove an existing entry.
  //
  // If the entry is not between the current top index and the bottom index
  // specified by the cookie, we don't remove anything.  This is the behavior
  // required by JNI's DeleteLocalRef function.
  //
  // Returns "false" if nothing was removed.
  bool Remove(LRTSegmentState previous_state, IndirectRef iref);

  void AssertEmpty() REQUIRES_SHARED(Locks::mutator_lock_);

  void Dump(std::ostream& os) const
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::alloc_tracker_lock_);

  IndirectRefKind GetKind() const {
    return kLocal;
  }

  // Return the #of entries in the entire table.  This includes holes, and
  // so may be larger than the actual number of "live" entries.
  size_t Capacity() const {
    return segment_state_.top_index;
  }

  // Return the number of non-null entries in the table. Only reliable for a
  // single segment table.
  int32_t NEntriesForGlobal() {
    return segment_state_.top_index - current_num_holes_;
  }

  // Ensure that at least free_capacity elements are available, or return false.
  // Caller ensures free_capacity > 0.
  bool EnsureFreeCapacity(size_t free_capacity, std::string* error_msg)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // See implementation of EnsureFreeCapacity. We'll only state here how much is trivially free,
  // without recovering holes. Thus this is a conservative estimate.
  size_t FreeCapacity() const;

  void VisitRoots(RootVisitor* visitor, const RootInfo& root_info)
      REQUIRES_SHARED(Locks::mutator_lock_);

  LRTSegmentState GetSegmentState() const {
    return segment_state_;
  }

  void SetSegmentState(LRTSegmentState new_state);

  static Offset SegmentStateOffset(size_t pointer_size ATTRIBUTE_UNUSED) {
    // Note: Currently segment_state_ is at offset 0. We're testing the expected value in
    //       jni_internal_test to make sure it stays correct. It is not OFFSETOF_MEMBER, as that
    //       is not pointer-size-safe.
    return Offset(0);
  }

  // Release pages past the end of the table that may have previously held references.
  void Trim() REQUIRES_SHARED(Locks::mutator_lock_);

  // Determine what kind of indirect reference this is. Opposite of EncodeIndirectRefKind.
  ALWAYS_INLINE static inline IndirectRefKind GetIndirectRefKind(IndirectRef iref) {
    return DecodeIndirectRefKind(reinterpret_cast<uintptr_t>(iref));
  }

  /* Reference validation for CheckJNI. */
  bool IsValidReference(IndirectRef, /*out*/std::string* error_msg) const
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  static constexpr uint32_t kShiftedSerialMask = (1u << kLRTSerialBits) - 1;

  static constexpr size_t kKindBits = MinimumBitsToStore(
      static_cast<uint32_t>(IndirectRefKind::kLastKind));
  static constexpr uint32_t kKindMask = (1u << kKindBits) - 1;

  static constexpr uintptr_t EncodeIndex(uint32_t table_index) {
    static_assert(sizeof(IndirectRef) == sizeof(uintptr_t), "Unexpected IndirectRef size");
    DCHECK_LE(MinimumBitsToStore(table_index), BitSizeOf<uintptr_t>() - kLRTSerialBits - kKindBits);
    return (static_cast<uintptr_t>(table_index) << kKindBits << kLRTSerialBits);
  }
  static constexpr uint32_t DecodeIndex(uintptr_t uref) {
    return static_cast<uint32_t>((uref >> kKindBits) >> kLRTSerialBits);
  }

  static constexpr uintptr_t EncodeIndirectRefKind(IndirectRefKind kind) {
    return static_cast<uintptr_t>(kind);
  }
  static constexpr IndirectRefKind DecodeIndirectRefKind(uintptr_t uref) {
    return static_cast<IndirectRefKind>(uref & kKindMask);
  }

  static constexpr uintptr_t EncodeSerial(uint32_t serial) {
    DCHECK_LE(MinimumBitsToStore(serial), kLRTSerialBits);
    return serial << kKindBits;
  }
  static constexpr uint32_t DecodeSerial(uintptr_t uref) {
    return static_cast<uint32_t>(uref >> kKindBits) & kShiftedSerialMask;
  }

  constexpr uintptr_t EncodeIndirectRef(uint32_t table_index, uint32_t serial) const {
    DCHECK_LT(table_index, max_entries_);
    return EncodeIndex(table_index) | EncodeSerial(serial) | EncodeIndirectRefKind(kLocal);
  }

  static void ConstexprChecks();

  // Extract the table index from an indirect reference.
  ALWAYS_INLINE static uint32_t ExtractIndex(IndirectRef iref) {
    return DecodeIndex(reinterpret_cast<uintptr_t>(iref));
  }

  IndirectRef ToIndirectRef(uint32_t table_index) const {
    DCHECK_LT(table_index, max_entries_);
    uint32_t serial = table_[table_index].GetSerial();
    return reinterpret_cast<IndirectRef>(EncodeIndirectRef(table_index, serial));
  }

  // Resize the backing table to be at least new_size elements long. Currently
  // must be larger than the current size. After return max_entries_ >= new_size.
  bool Resize(size_t new_size, std::string* error_msg);

  void RecoverHoles(LRTSegmentState from);

  // Abort if check_jni is not enabled. Otherwise, just log as an error.
  static void AbortIfNoCheckJNI(const std::string& msg);

  /* extra debugging checks */
  bool CheckEntry(const char*, IndirectRef, uint32_t) const;

  /// semi-public - read/write by jni down calls.
  LRTSegmentState segment_state_;

  // Mem map where we store the indirect refs. If it's invalid, and table_ is non-null, then
  // table_ is valid, but was allocated via `SmallLrtAllocator`;
  MemMap table_mem_map_;
  // bottom of the stack. Do not directly access the object references
  // in this as they are roots. Use Get() that has a read barrier.
  LrtEntry* table_;

  // max #of entries allowed (modulo resizing).
  size_t max_entries_;

  // Some values to retain old behavior with holes. Description of the algorithm is in the .cc
  // file.
  // TODO: Consider other data structures for compact tables, e.g., free lists.
  size_t current_num_holes_;  // Number of holes in the current / top segment.
  LRTSegmentState last_known_previous_state_;
};

}  // namespace jni
}  // namespace art

#endif  // ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_H_
