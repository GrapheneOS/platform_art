/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_DEX_CACHE_H_
#define ART_RUNTIME_MIRROR_DEX_CACHE_H_

#include "array.h"
#include "base/array_ref.h"
#include "base/atomic_pair.h"
#include "base/bit_utils.h"
#include "base/locks.h"
#include "dex/dex_file.h"
#include "dex/dex_file_types.h"
#include "gc_root.h"  // Note: must not use -inl here to avoid circular dependency.
#include "linear_alloc.h"
#include "object.h"
#include "object_array.h"

namespace art {

namespace linker {
class ImageWriter;
}  // namespace linker

class ArtField;
class ArtMethod;
struct DexCacheOffsets;
class DexFile;
union JValue;
class ReflectiveValueVisitor;
class Thread;

namespace mirror {

class CallSite;
class Class;
class ClassLoader;
class DexCache;
class MethodType;
class String;

template <typename T> struct PACKED(8) DexCachePair {
  GcRoot<T> object;
  uint32_t index;
  // The array is initially [ {0,0}, {0,0}, {0,0} ... ]
  // We maintain the invariant that once a dex cache entry is populated,
  // the pointer is always non-0
  // Any given entry would thus be:
  // {non-0, non-0} OR {0,0}
  //
  // It's generally sufficiently enough then to check if the
  // lookup index matches the stored index (for a >0 lookup index)
  // because if it's true the pointer is also non-null.
  //
  // For the 0th entry which is a special case, the value is either
  // {0,0} (initial state) or {non-0, 0} which indicates
  // that a valid object is stored at that index for a dex section id of 0.
  //
  // As an optimization, we want to avoid branching on the object pointer since
  // it's always non-null if the id branch succeeds (except for the 0th id).
  // Set the initial state for the 0th entry to be {0,1} which is guaranteed to fail
  // the lookup id == stored id branch.
  DexCachePair(ObjPtr<T> object, uint32_t index);
  DexCachePair() : index(0) {}
  DexCachePair(const DexCachePair<T>&) = default;
  DexCachePair& operator=(const DexCachePair<T>&) = default;

  static void Initialize(std::atomic<DexCachePair<T>>* dex_cache);

  static uint32_t InvalidIndexForSlot(uint32_t slot) {
    // Since the cache size is a power of two, 0 will always map to slot 0.
    // Use 1 for slot 0 and 0 for all other slots.
    return (slot == 0) ? 1u : 0u;
  }

  T* GetObjectForIndex(uint32_t idx) REQUIRES_SHARED(Locks::mutator_lock_);
};

template <typename T> struct PACKED(2 * __SIZEOF_POINTER__) NativeDexCachePair {
  T* object;
  size_t index;
  // This is similar to DexCachePair except that we're storing a native pointer
  // instead of a GC root. See DexCachePair for the details.
  NativeDexCachePair(T* object, uint32_t index)
      : object(object),
        index(index) {}
  NativeDexCachePair() : object(nullptr), index(0u) { }
  NativeDexCachePair(const NativeDexCachePair<T>&) = default;
  NativeDexCachePair& operator=(const NativeDexCachePair<T>&) = default;

  static void Initialize(std::atomic<NativeDexCachePair<T>>* dex_cache);

  static uint32_t InvalidIndexForSlot(uint32_t slot) {
    // Since the cache size is a power of two, 0 will always map to slot 0.
    // Use 1 for slot 0 and 0 for all other slots.
    return (slot == 0) ? 1u : 0u;
  }

  T* GetObjectForIndex(uint32_t idx) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (idx != index) {
      return nullptr;
    }
    DCHECK(object != nullptr);
    return object;
  }
};

template <typename T, size_t size> class NativeDexCachePairArray {
 public:
  NativeDexCachePairArray() {}

  T* Get(uint32_t index) REQUIRES_SHARED(Locks::mutator_lock_) {
    auto pair = GetNativePair(entries_, SlotIndex(index));
    return pair.GetObjectForIndex(index);
  }

  void Set(uint32_t index, T* value) {
    NativeDexCachePair<T> pair(value, index);
    SetNativePair(entries_, SlotIndex(index), pair);
  }

  NativeDexCachePair<T> GetNativePair(uint32_t index) REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetNativePair(entries_, SlotIndex(index));
  }

  void SetNativePair(uint32_t index, NativeDexCachePair<T> value) {
    SetNativePair(entries_, SlotIndex(index), value);
  }

 private:
  NativeDexCachePair<T> GetNativePair(std::atomic<NativeDexCachePair<T>>* pair_array, size_t idx) {
    auto* array = reinterpret_cast<std::atomic<AtomicPair<uintptr_t>>*>(pair_array);
    AtomicPair<uintptr_t> value = AtomicPairLoadAcquire(&array[idx]);
    return NativeDexCachePair<T>(reinterpret_cast<T*>(value.first), value.second);
  }

  void SetNativePair(std::atomic<NativeDexCachePair<T>>* pair_array,
                     size_t idx,
                     NativeDexCachePair<T> pair) {
    auto* array = reinterpret_cast<std::atomic<AtomicPair<uintptr_t>>*>(pair_array);
    AtomicPair<uintptr_t> v(reinterpret_cast<size_t>(pair.object), pair.index);
    AtomicPairStoreRelease(&array[idx], v);
  }

  uint32_t SlotIndex(uint32_t index) {
    return index % size;
  }

  std::atomic<NativeDexCachePair<T>> entries_[0];

  NativeDexCachePairArray(const NativeDexCachePairArray<T, size>&) = delete;
  NativeDexCachePairArray& operator=(const NativeDexCachePairArray<T, size>&) = delete;
};

template <typename T, size_t size> class DexCachePairArray {
 public:
  DexCachePairArray() {}

  T* Get(uint32_t index) REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetPair(index).GetObjectForIndex(index);
  }

  void Set(uint32_t index, T* value) {
    SetPair(index, DexCachePair<T>(value, index));
  }

  DexCachePair<T> GetPair(uint32_t index) {
    return entries_[SlotIndex(index)].load(std::memory_order_relaxed);
  }

  void SetPair(uint32_t index, DexCachePair<T> value) {
    entries_[SlotIndex(index)].store(value, std::memory_order_relaxed);
  }

  void Clear(uint32_t index) {
    uint32_t slot = SlotIndex(index);
    // This is racy but should only be called from the transactional interpreter.
    if (entries_[slot].load(std::memory_order_relaxed).index == index) {
      DexCachePair<T> cleared(nullptr, DexCachePair<T>::InvalidIndexForSlot(slot));
      entries_[slot].store(cleared, std::memory_order_relaxed);
    }
  }

 private:
  uint32_t SlotIndex(uint32_t index) {
    return index % size;
  }

  std::atomic<DexCachePair<T>> entries_[0];

  DexCachePairArray(const DexCachePairArray<T, size>&) = delete;
  DexCachePairArray& operator=(const DexCachePairArray<T, size>&) = delete;
};

// C++ mirror of java.lang.DexCache.
class MANAGED DexCache final : public Object {
 public:
  MIRROR_CLASS("Ljava/lang/DexCache;");

  // Size of java.lang.DexCache.class.
  static uint32_t ClassSize(PointerSize pointer_size);

  // Size of type dex cache. Needs to be a power of 2 for entrypoint assumptions to hold.
  static constexpr size_t kDexCacheTypeCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheTypeCacheSize),
                "Type dex cache size is not a power of 2.");

  // Size of string dex cache. Needs to be a power of 2 for entrypoint assumptions to hold.
  static constexpr size_t kDexCacheStringCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheStringCacheSize),
                "String dex cache size is not a power of 2.");

  // Size of field dex cache. Needs to be a power of 2 for entrypoint assumptions to hold.
  static constexpr size_t kDexCacheFieldCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheFieldCacheSize),
                "Field dex cache size is not a power of 2.");

  // Size of method dex cache. Needs to be a power of 2 for entrypoint assumptions to hold.
  static constexpr size_t kDexCacheMethodCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheMethodCacheSize),
                "Method dex cache size is not a power of 2.");

  // Size of method type dex cache. Needs to be a power of 2 for entrypoint assumptions
  // to hold.
  static constexpr size_t kDexCacheMethodTypeCacheSize = 1024;
  static_assert(IsPowerOfTwo(kDexCacheMethodTypeCacheSize),
                "MethodType dex cache size is not a power of 2.");

  // Size of an instance of java.lang.DexCache not including referenced values.
  static constexpr uint32_t InstanceSize() {
    return sizeof(DexCache);
  }

  // Visit gc-roots in DexCachePair array in [pairs_begin, pairs_end) range.
  template <typename Visitor>
  static void VisitDexCachePairRoots(Visitor& visitor,
                                     DexCachePair<Object>* pairs_begin,
                                     DexCachePair<Object>* pairs_end)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void Initialize(const DexFile* dex_file, ObjPtr<ClassLoader> class_loader)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::dex_lock_);

  // Zero all array references.
  // WARNING: This does not free the memory since it is in LinearAlloc.
  void ResetNativeArrays() REQUIRES_SHARED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  ObjPtr<String> GetLocation() REQUIRES_SHARED(Locks::mutator_lock_);

  String* GetResolvedString(dex::StringIndex string_idx) ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_);

  void SetResolvedString(dex::StringIndex string_idx, ObjPtr<mirror::String> resolved) ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Clear a string for a string_idx, used to undo string intern transactions to make sure
  // the string isn't kept live.
  void ClearString(dex::StringIndex string_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  Class* GetResolvedType(dex::TypeIndex type_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  void SetResolvedType(dex::TypeIndex type_idx, ObjPtr<Class> resolved)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void ClearResolvedType(dex::TypeIndex type_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE ArtMethod* GetResolvedMethod(uint32_t method_idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE void SetResolvedMethod(uint32_t method_idx, ArtMethod* resolved)
      REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE ArtField* GetResolvedField(uint32_t idx)
      REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE void SetResolvedField(uint32_t idx, ArtField* field)
      REQUIRES_SHARED(Locks::mutator_lock_);

  MethodType* GetResolvedMethodType(dex::ProtoIndex proto_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  void SetResolvedMethodType(dex::ProtoIndex proto_idx, MethodType* resolved)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Clear a method type for proto_idx, used to undo method type resolution
  // in aborted transactions to make sure the method type isn't kept live.
  void ClearMethodType(dex::ProtoIndex proto_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  CallSite* GetResolvedCallSite(uint32_t call_site_idx) REQUIRES_SHARED(Locks::mutator_lock_);

  // Attempts to bind |call_site_idx| to the call site |resolved|. The
  // caller must use the return value in place of |resolved|. This is
  // because multiple threads can invoke the bootstrap method each
  // producing a call site, but the method handle invocation on the
  // call site must be on a common agreed value.
  ObjPtr<CallSite> SetResolvedCallSite(uint32_t call_site_idx, ObjPtr<CallSite> resolved)
      REQUIRES_SHARED(Locks::mutator_lock_) WARN_UNUSED;

  const DexFile* GetDexFile() ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetFieldPtr<const DexFile*>(OFFSET_OF_OBJECT_MEMBER(DexCache, dex_file_));
  }

  void SetDexFile(const DexFile* dex_file) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetFieldPtr<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, dex_file_), dex_file);
  }

  void SetLocation(ObjPtr<String> location) REQUIRES_SHARED(Locks::mutator_lock_);

  void VisitReflectiveTargets(ReflectiveValueVisitor* visitor) REQUIRES(Locks::mutator_lock_);

  void SetClassLoader(ObjPtr<ClassLoader> class_loader) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<ClassLoader> GetClassLoader() REQUIRES_SHARED(Locks::mutator_lock_);

  template <VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
            ReadBarrierOption kReadBarrierOption = kWithReadBarrier,
            typename Visitor>
  void VisitNativeRoots(const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  // NOLINTBEGIN(bugprone-macro-parentheses)
#define DEFINE_PAIR_ARRAY(name, pair_kind, getter_setter, type, size, ids, alloc_kind) \
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags> \
  pair_kind ##Array<type, size>* Get ##getter_setter() \
      ALWAYS_INLINE \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    return GetFieldPtr<pair_kind ##Array<type, size>*, kVerifyFlags>(getter_setter ##Offset()); \
  } \
  void Set ##getter_setter(pair_kind ##Array<type, size>* value) \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    SetFieldPtr<false>(getter_setter ##Offset(), value); \
  } \
  static constexpr MemberOffset getter_setter ##Offset() { \
    return OFFSET_OF_OBJECT_MEMBER(DexCache, name); \
  } \
  pair_kind ##Array<type, size>* Allocate ##getter_setter() \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    return reinterpret_cast<pair_kind ##Array<type, size>*>( \
        AllocArray<std::atomic<pair_kind<type>>, size>( \
            getter_setter ##Offset(), GetDexFile()->ids(), alloc_kind)); \
  } \
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags> \
  size_t Num ##getter_setter() REQUIRES_SHARED(Locks::mutator_lock_) { \
    return Get ##getter_setter() == nullptr ? 0u : std::min<size_t>(GetDexFile()->ids(), size); \
  } \

#define DEFINE_ARRAY(name, getter_setter, type, ids, alloc_kind) \
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags> \
  type* Get ##getter_setter() \
      ALWAYS_INLINE \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    return GetFieldPtr<type*, kVerifyFlags>(getter_setter ##Offset()); \
  } \
  void Set ##getter_setter(type* value) \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    SetFieldPtr<false>(getter_setter ##Offset(), value); \
  } \
  static constexpr MemberOffset getter_setter ##Offset() { \
    return OFFSET_OF_OBJECT_MEMBER(DexCache, name); \
  } \
  type* Allocate ##getter_setter() \
      REQUIRES_SHARED(Locks::mutator_lock_) { \
    return reinterpret_cast<type*>(AllocArray<type, std::numeric_limits<size_t>::max()>( \
        getter_setter ##Offset(), GetDexFile()->ids(), alloc_kind)); \
  } \
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags> \
  size_t Num ##getter_setter() REQUIRES_SHARED(Locks::mutator_lock_) { \
    return GetDexFile()->ids(); \
  } \

  DEFINE_ARRAY(resolved_call_sites_,
               ResolvedCallSites,
               GcRoot<CallSite>,
               NumCallSiteIds,
               LinearAllocKind::kGCRootArray)

  DEFINE_PAIR_ARRAY(resolved_fields_,
                    NativeDexCachePair,
                    ResolvedFields,
                    ArtField,
                    kDexCacheFieldCacheSize,
                    NumFieldIds,
                    LinearAllocKind::kNoGCRoots)

  DEFINE_PAIR_ARRAY(resolved_method_types_,
                    DexCachePair,
                    ResolvedMethodTypes,
                    mirror::MethodType,
                    kDexCacheMethodTypeCacheSize,
                    NumProtoIds,
                    LinearAllocKind::kDexCacheArray);

  DEFINE_PAIR_ARRAY(resolved_methods_,
                    NativeDexCachePair,
                    ResolvedMethods,
                    ArtMethod,
                    kDexCacheMethodCacheSize,
                    NumMethodIds,
                    LinearAllocKind::kNoGCRoots)

  DEFINE_PAIR_ARRAY(resolved_types_,
                    DexCachePair,
                    ResolvedTypes,
                    mirror::Class,
                    kDexCacheTypeCacheSize,
                    NumTypeIds,
                    LinearAllocKind::kDexCacheArray);

  DEFINE_PAIR_ARRAY(strings_,
                    DexCachePair,
                    Strings,
                    mirror::String,
                    kDexCacheStringCacheSize,
                    NumStringIds,
                    LinearAllocKind::kDexCacheArray);

// NOLINTEND(bugprone-macro-parentheses)

 private:
  // Allocate new array in linear alloc and save it in the given fields.
  template<typename T, size_t kMaxCacheSize>
  T* AllocArray(MemberOffset obj_offset, size_t num, LinearAllocKind kind)
     REQUIRES_SHARED(Locks::mutator_lock_);

  // Visit instance fields of the dex cache as well as its associated arrays.
  template <bool kVisitNativeRoots,
            VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
            ReadBarrierOption kReadBarrierOption = kWithReadBarrier,
            typename Visitor>
  void VisitReferences(ObjPtr<Class> klass, const Visitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_);

  HeapReference<ClassLoader> class_loader_;
  HeapReference<String> location_;

  uint64_t dex_file_;                // const DexFile*

  uint64_t resolved_call_sites_;     // Array of call sites
  uint64_t resolved_fields_;         // NativeDexCacheArray holding ArtField's
  uint64_t resolved_method_types_;   // DexCacheArray holding mirror::MethodType's
  uint64_t resolved_methods_;        // NativeDexCacheArray holding ArtMethod's
  uint64_t resolved_types_;          // DexCacheArray holding mirror::Class's
  uint64_t strings_;                 // DexCacheArray holding mirror::String's

  friend struct art::DexCacheOffsets;  // for verifying offset information
  friend class linker::ImageWriter;
  friend class Object;  // For VisitReferences
  DISALLOW_IMPLICIT_CONSTRUCTORS(DexCache);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_DEX_CACHE_H_
