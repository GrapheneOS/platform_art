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

#ifndef ART_RUNTIME_MIRROR_CLASS_REFVISITOR_INL_H_
#define ART_RUNTIME_MIRROR_CLASS_REFVISITOR_INL_H_

#include "class-inl.h"

#include "art_field-inl.h"
#include "class_ext-inl.h"

namespace art HIDDEN {
namespace mirror {

// NO_THREAD_SAFETY_ANALYSIS for mutator_lock_ and heap_bitmap_lock_, as
// requirements for these vary depending on the visitor.
template <VerifyObjectFlags kVerifyFlags, typename Visitor>
inline void Class::VisitStaticFieldsReferences(const Visitor& visitor) NO_THREAD_SAFETY_ANALYSIS {
  DCHECK(!IsTemp<kVerifyFlags>());
  const size_t num_reference_fields = NumReferenceStaticFields();
  if (num_reference_fields > 0u) {
    // Presumably GC can happen when we are cross compiling, it should not cause performance
    // problems to do pointer size logic.
    MemberOffset field_offset = GetFirstReferenceStaticFieldOffset<kVerifyFlags>(
        Runtime::Current()->GetClassLinker()->GetImagePointerSize());
    for (size_t i = 0u; i < num_reference_fields; ++i) {
      DCHECK_NE(field_offset.Uint32Value(), ClassOffset().Uint32Value());
      visitor(this, field_offset, /*is_static=*/true);
      field_offset =
          MemberOffset(field_offset.Uint32Value() + sizeof(mirror::HeapReference<mirror::Object>));
    }
  }
}

template <bool kVisitNativeRoots,
          VerifyObjectFlags kVerifyFlags,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor>
inline void Class::VisitReferences(ObjPtr<Class> klass, const Visitor& visitor) {
  VisitInstanceFieldsReferences<kVerifyFlags>(klass.Ptr(), visitor);
  // Right after a class is allocated, but not yet loaded
  // (ClassStatus::kNotReady, see ClassLinker::LoadClass()), GC may find it
  // and scan it. IsTemp() may call Class::GetAccessFlags() but may
  // fail in the DCHECK in Class::GetAccessFlags() because the class
  // status is ClassStatus::kNotReady. To avoid it, rely on IsResolved()
  // only. This is fine because a temp class never goes into the
  // ClassStatus::kResolved state.
  if (IsResolved<kVerifyFlags>()) {
    // Temp classes don't ever populate imt/vtable or static fields and they are not even
    // allocated with the right size for those. Also, unresolved classes don't have fields
    // linked yet.
    VisitStaticFieldsReferences<kVerifyFlags>(visitor);
  }
  if (kVisitNativeRoots) {
    // Since this class is reachable, we must also visit the associated roots when we scan it.
    VisitNativeRoots<kReadBarrierOption>(
        visitor, Runtime::Current()->GetClassLinker()->GetImagePointerSize());
  }
}

template<ReadBarrierOption kReadBarrierOption, bool kVisitProxyMethod, class Visitor>
void Class::VisitNativeRoots(Visitor& visitor, PointerSize pointer_size) {
  VisitFields<kReadBarrierOption>([&](ArtField* field) REQUIRES_SHARED(art::Locks::mutator_lock_) {
    field->VisitRoots(visitor);
    if (kIsDebugBuild && !gUseUserfaultfd && IsResolved()) {
      CHECK_EQ(field->GetDeclaringClass<kReadBarrierOption>(), this)
          << GetStatus() << field->GetDeclaringClass()->PrettyClass() << " != " << PrettyClass();
    }
  });
  // The method array may be null when the class isn't resolved yet.
  if (GetMethodsPtr() != nullptr) {
    // Don't use VisitMethods because we don't want to hit the class-ext methods twice.
    for (ArtMethod& method : GetMethods(pointer_size)) {
      method.VisitRoots<kReadBarrierOption, kVisitProxyMethod>(visitor, pointer_size);
    }
  }
  ObjPtr<ClassExt> ext(GetExtData<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!ext.IsNull()) {
    ext->VisitNativeRoots<kReadBarrierOption, kVisitProxyMethod>(visitor, pointer_size);
  }
}

template<ReadBarrierOption kReadBarrierOption>
void Class::VisitObsoleteDexCaches(DexCacheVisitor& visitor) {
  ObjPtr<ClassExt> ext(GetExtData<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!ext.IsNull()) {
    ext->VisitDexCaches<kDefaultVerifyFlags, kReadBarrierOption>(visitor);
  }
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void Class::VisitObsoleteClass(Visitor& visitor) {
  ObjPtr<ClassExt> ext(GetExtData<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!ext.IsNull()) {
    ObjPtr<Class> klass = ext->GetObsoleteClass<kDefaultVerifyFlags, kReadBarrierOption>();
    visitor(klass);
  }
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void Class::VisitMethods(Visitor visitor, PointerSize pointer_size) {
  for (ArtMethod& method : GetMethods(pointer_size)) {
    visitor(&method);
  }
  ObjPtr<ClassExt> ext(GetExtData<kDefaultVerifyFlags, kReadBarrierOption>());
  if (!ext.IsNull()) {
    ext->VisitMethods<kReadBarrierOption, Visitor>(visitor, pointer_size);
  }
}

template<ReadBarrierOption kReadBarrierOption, class Visitor>
void Class::VisitFields(Visitor visitor) {
  for (ArtField& field : GetFieldsUnchecked()) {
    visitor(&field);
  }
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_CLASS_REFVISITOR_INL_H_
