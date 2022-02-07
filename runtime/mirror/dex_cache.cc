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

#include "dex_cache-inl.h"

#include "art_method-inl.h"
#include "class_linker.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/heap.h"
#include "linear_alloc.h"
#include "oat_file.h"
#include "object-inl.h"
#include "object.h"
#include "object_array-inl.h"
#include "reflective_value_visitor.h"
#include "runtime.h"
#include "runtime_globals.h"
#include "string.h"
#include "thread.h"
#include "write_barrier.h"

namespace art {
namespace mirror {

void DexCache::Initialize(const DexFile* dex_file, ObjPtr<ClassLoader> class_loader) {
  DCHECK(GetDexFile() == nullptr);
  DCHECK(GetStrings() == nullptr);
  DCHECK(GetResolvedTypes() == nullptr);
  DCHECK(GetResolvedMethods() == nullptr);
  DCHECK(GetResolvedFields() == nullptr);
  DCHECK(GetResolvedMethodTypes() == nullptr);
  DCHECK(GetResolvedCallSites() == nullptr);

  ScopedAssertNoThreadSuspension sants(__FUNCTION__);

  SetDexFile(dex_file);
  SetClassLoader(class_loader);
}

void DexCache::VisitReflectiveTargets(ReflectiveValueVisitor* visitor) {
  bool wrote = false;
  FieldDexCacheType* fields = GetResolvedFields();
  size_t num_fields = NumResolvedFields();
  // Check both the data pointer and count since the array might be initialized
  // concurrently on other thread, and we might observe just one of the values.
  for (size_t i = 0; fields != nullptr && i < num_fields; i++) {
    auto pair(GetNativePair(fields, i));
    if (pair.index == FieldDexCachePair::InvalidIndexForSlot(i)) {
      continue;
    }
    ArtField* new_val = visitor->VisitField(
        pair.object, DexCacheSourceInfo(kSourceDexCacheResolvedField, pair.index, this));
    if (UNLIKELY(new_val != pair.object)) {
      if (new_val == nullptr) {
        pair = FieldDexCachePair(nullptr, FieldDexCachePair::InvalidIndexForSlot(i));
      } else {
        pair.object = new_val;
      }
      SetNativePair(fields, i, pair);
      wrote = true;
    }
  }
  MethodDexCacheType* methods = GetResolvedMethods();
  size_t num_methods = NumResolvedMethods();
  // Check both the data pointer and count since the array might be initialized
  // concurrently on other thread, and we might observe just one of the values.
  for (size_t i = 0; methods != nullptr && i < num_methods; i++) {
    auto pair(GetNativePair(methods, i));
    if (pair.index == MethodDexCachePair::InvalidIndexForSlot(i)) {
      continue;
    }
    ArtMethod* new_val = visitor->VisitMethod(
        pair.object, DexCacheSourceInfo(kSourceDexCacheResolvedMethod, pair.index, this));
    if (UNLIKELY(new_val != pair.object)) {
      if (new_val == nullptr) {
        pair = MethodDexCachePair(nullptr, MethodDexCachePair::InvalidIndexForSlot(i));
      } else {
        pair.object = new_val;
      }
      SetNativePair(methods, i, pair);
      wrote = true;
    }
  }
  if (wrote) {
    WriteBarrier::ForEveryFieldWrite(this);
  }
}

void DexCache::ResetNativeArrays() {
  SetStrings(nullptr);
  SetResolvedTypes(nullptr);
  SetResolvedMethods(nullptr);
  SetResolvedFields(nullptr);
  SetResolvedMethodTypes(nullptr);
  SetResolvedCallSites(nullptr);
  SetField32<false>(NumStringsOffset(), 0);
  SetField32<false>(NumResolvedTypesOffset(), 0);
  SetField32<false>(NumResolvedMethodsOffset(), 0);
  SetField32<false>(NumResolvedFieldsOffset(), 0);
  SetField32<false>(NumResolvedMethodTypesOffset(), 0);
  SetField32<false>(NumResolvedCallSitesOffset(), 0);
}

void DexCache::SetLocation(ObjPtr<mirror::String> location) {
  SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, location_), location);
}

void DexCache::SetClassLoader(ObjPtr<ClassLoader> class_loader) {
  SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, class_loader_), class_loader);
}

ObjPtr<ClassLoader> DexCache::GetClassLoader() {
  return GetFieldObject<ClassLoader>(OFFSET_OF_OBJECT_MEMBER(DexCache, class_loader_));
}

}  // namespace mirror
}  // namespace art
