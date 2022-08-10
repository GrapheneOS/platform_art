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

#ifndef ART_RUNTIME_MIRROR_OBJECT_REFVISITOR_INL_H_
#define ART_RUNTIME_MIRROR_OBJECT_REFVISITOR_INL_H_

#include "object-inl.h"

#include "class-refvisitor-inl.h"
#include "class_loader-inl.h"
#include "dex_cache-inl.h"

namespace art {
namespace mirror {

template <bool kVisitNativeRoots,
          VerifyObjectFlags kVerifyFlags,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor,
          typename JavaLangRefVisitor>
inline void Object::VisitReferences(const Visitor& visitor,
                                    const JavaLangRefVisitor& ref_visitor) {
  visitor(this, ClassOffset(), /* is_static= */ false);
  ObjPtr<Class> klass = GetClass<kVerifyFlags, kReadBarrierOption>();
  const uint32_t class_flags = klass->GetClassFlags<kVerifyNone>();
  if (LIKELY(class_flags == kClassFlagNormal)) {
    DCHECK((!klass->IsVariableSize<kVerifyFlags>()));
    VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
    DCHECK((!klass->IsClassClass<kVerifyFlags>()));
    DCHECK(!klass->IsStringClass<kVerifyFlags>());
    DCHECK(!klass->IsClassLoaderClass<kVerifyFlags>());
    DCHECK((!klass->IsArrayClass<kVerifyFlags>()));
  } else {
    if ((class_flags & kClassFlagNoReferenceFields) == 0) {
      DCHECK(!klass->IsStringClass<kVerifyFlags>());
      if (class_flags == kClassFlagClass) {
        DCHECK((klass->IsClassClass<kVerifyFlags>()));
        ObjPtr<Class> as_klass = AsClass<kVerifyNone>();
        as_klass->VisitReferences<kVisitNativeRoots, kVerifyFlags, kReadBarrierOption>(klass,
                                                                                       visitor);
      } else if (class_flags == kClassFlagObjectArray) {
        DCHECK((klass->IsObjectArrayClass<kVerifyFlags>()));
        AsObjectArray<mirror::Object, kVerifyNone>()->VisitReferences(visitor);
      } else if ((class_flags & kClassFlagReference) != 0) {
        VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
        ref_visitor(klass, AsReference<kVerifyFlags, kReadBarrierOption>());
      } else if (class_flags == kClassFlagDexCache) {
        ObjPtr<mirror::DexCache> const dex_cache = AsDexCache<kVerifyFlags, kReadBarrierOption>();
        dex_cache->VisitReferences<kVisitNativeRoots,
                                   kVerifyFlags,
                                   kReadBarrierOption>(klass, visitor);
      } else {
        ObjPtr<mirror::ClassLoader> const class_loader =
            AsClassLoader<kVerifyFlags, kReadBarrierOption>();
        class_loader->VisitReferences<kVisitNativeRoots,
                                      kVerifyFlags,
                                      kReadBarrierOption>(klass, visitor);
      }
    } else if (kIsDebugBuild) {
      CHECK((!klass->IsClassClass<kVerifyFlags>()));
      CHECK((!klass->IsObjectArrayClass<kVerifyFlags>()));
      // String still has instance fields for reflection purposes but these don't exist in
      // actual string instances.
      if (!klass->IsStringClass<kVerifyFlags>()) {
        size_t total_reference_instance_fields = 0;
        ObjPtr<Class> super_class = klass;
        do {
          total_reference_instance_fields +=
              super_class->NumReferenceInstanceFields<kVerifyFlags>();
          super_class = super_class->GetSuperClass<kVerifyFlags, kReadBarrierOption>();
        } while (super_class != nullptr);
        // The only reference field should be the object's class. This field is handled at the
        // beginning of the function.
        CHECK_EQ(total_reference_instance_fields, 1u);
      }
    }
  }
}

// Could be called with from-space address of the object as we access klass and
// length (in case of arrays/strings) and we don't want to cause cascading faults.
template <bool kFetchObjSize,
          bool kVisitNativeRoots,
          VerifyObjectFlags kVerifyFlags,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor>
inline size_t Object::VisitRefsForCompaction(const Visitor& visitor,
                                             MemberOffset begin,
                                             MemberOffset end) {
  constexpr VerifyObjectFlags kSizeOfFlags = RemoveThisFlags(kVerifyFlags);
  size_t size;
  // We want to continue using pre-compact klass to avoid cascading faults.
  ObjPtr<Class> klass = GetClass<kVerifyFlags, kReadBarrierOption>();
  visitor(this, ClassOffset(), /* is_static= */ false);
  const uint32_t class_flags = klass->GetClassFlags<kVerifyNone>();
  if (LIKELY(class_flags == kClassFlagNormal)) {
    DCHECK((!klass->IsVariableSize<kVerifyFlags>()));
    VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
    size = kFetchObjSize ? klass->GetObjectSize<kSizeOfFlags>() : 0;
    DCHECK((!klass->IsClassClass<kVerifyFlags>()));
    DCHECK(!klass->IsStringClass<kVerifyFlags>());
    DCHECK(!klass->IsClassLoaderClass<kVerifyFlags>());
    DCHECK((!klass->IsArrayClass<kVerifyFlags>()));
  } else {
    if ((class_flags & kClassFlagNoReferenceFields) == 0) {
      DCHECK(!klass->IsStringClass<kVerifyFlags>());
      if (class_flags == kClassFlagClass) {
        DCHECK((klass->IsClassClass<kVerifyFlags>()));
        ObjPtr<Class> as_klass = AsClass<kVerifyNone>();
        as_klass->VisitReferences<kVisitNativeRoots, kVerifyFlags, kReadBarrierOption>(klass,
                                                                                       visitor);
        return kFetchObjSize ? as_klass->SizeOf<kSizeOfFlags>() : 0;
      } else if (class_flags == kClassFlagObjectArray) {
        DCHECK((klass->IsObjectArrayClass<kVerifyFlags>()));
        ObjPtr<ObjectArray<mirror::Object>> obj_arr = AsObjectArray<mirror::Object, kVerifyNone>();
        obj_arr->VisitReferences(visitor, begin, end);
        return kFetchObjSize ? obj_arr->SizeOf<kSizeOfFlags>() : 0;
      } else if ((class_flags & kClassFlagReference) != 0) {
        VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
        // Visit referent also as this is about updating the reference only.
        // There is no reference processing happening here.
        visitor(this, mirror::Reference::ReferentOffset(), /* is_static= */ false);
        if ((class_flags & kClassFlagFinalizerReference) != 0) {
          visitor(this, mirror::FinalizerReference::ZombieOffset(), /* is_static= */ false);
        }
      } else if (class_flags == kClassFlagDexCache) {
        ObjPtr<mirror::DexCache> const dex_cache = AsDexCache<kVerifyFlags, kReadBarrierOption>();
        dex_cache->VisitReferences<kVisitNativeRoots,
                                   kVerifyFlags,
                                   kReadBarrierOption>(klass, visitor);
      } else {
        ObjPtr<mirror::ClassLoader> const class_loader =
            AsClassLoader<kVerifyFlags, kReadBarrierOption>();
        class_loader->VisitReferences<kVisitNativeRoots,
                                      kVerifyFlags,
                                      kReadBarrierOption>(klass, visitor);
      }
      size = kFetchObjSize ? klass->GetObjectSize<kSizeOfFlags>() : 0;
    } else {
      DCHECK((!klass->IsClassClass<kVerifyFlags>()));
      DCHECK((!klass->IsObjectArrayClass<kVerifyFlags>()));
      if (class_flags == kClassFlagString) {
        size = kFetchObjSize ? AsString<kSizeOfFlags>()->template SizeOf<kSizeOfFlags>() : 0;
      } else if (klass->IsArrayClass<kVerifyFlags>()) {
        // TODO: We can optimize this by implementing a SizeOf() version which takes
        // component-size-shift as an argument, thereby avoiding multiple loads of
        // component_type.
        size = kFetchObjSize ? AsArray<kSizeOfFlags>()->template SizeOf<kSizeOfFlags>() : 0;
      } else {
        DCHECK_NE(class_flags & kClassFlagNormal, 0u);
        // Only possibility left is of a normal klass instance with no references.
        size = kFetchObjSize ? klass->GetObjectSize<kSizeOfFlags>() : 0;
      }

      if (kIsDebugBuild) {
        // String still has instance fields for reflection purposes but these don't exist in
        // actual string instances.
        if (!klass->IsStringClass<kVerifyFlags>()) {
          size_t total_reference_instance_fields = 0;
          ObjPtr<Class> super_class = klass;
          do {
            total_reference_instance_fields +=
                super_class->NumReferenceInstanceFields<kVerifyFlags>();
            super_class = super_class->GetSuperClass<kVerifyFlags, kReadBarrierOption>();
          } while (super_class != nullptr);
          // The only reference field should be the object's class. This field is handled at the
          // beginning of the function.
          CHECK_EQ(total_reference_instance_fields, 1u);
        }
      }
    }
  }
  return size;
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_OBJECT_REFVISITOR_INL_H_
