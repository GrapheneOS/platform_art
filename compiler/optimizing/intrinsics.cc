/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "intrinsics.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/utils.h"
#include "class_linker.h"
#include "class_root-inl.h"
#include "code_generator.h"
#include "dex/invoke_type.h"
#include "driver/compiler_options.h"
#include "gc/space/image_space.h"
#include "image-inl.h"
#include "intrinsic_objects.h"
#include "intrinsics_list.h"
#include "nodes.h"
#include "obj_ptr-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
#include "well_known_classes-inl.h"

namespace art HIDDEN {

std::ostream& operator<<(std::ostream& os, const Intrinsics& intrinsic) {
  switch (intrinsic) {
    case Intrinsics::kNone:
      os << "None";
      break;
#define OPTIMIZING_INTRINSICS(Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
    case Intrinsics::k ## Name: \
      os << # Name; \
      break;
      ART_INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef OPTIMIZING_INTRINSICS
  }
  return os;
}

static const char kIntegerCacheDescriptor[] = "Ljava/lang/Integer$IntegerCache;";
static const char kIntegerDescriptor[] = "Ljava/lang/Integer;";
static const char kLowFieldName[] = "low";
static const char kHighFieldName[] = "high";
static const char kValueFieldName[] = "value";

static constexpr int32_t kIntegerCacheLow = -128;
static constexpr int32_t kIntegerCacheHigh = 127;
static constexpr int32_t kIntegerCacheLength = kIntegerCacheHigh - kIntegerCacheLow + 1;


static ObjPtr<mirror::ObjectArray<mirror::Object>> GetBootImageLiveObjects()
    REQUIRES_SHARED(Locks::mutator_lock_) {
  gc::Heap* heap = Runtime::Current()->GetHeap();
  const std::vector<gc::space::ImageSpace*>& boot_image_spaces = heap->GetBootImageSpaces();
  DCHECK(!boot_image_spaces.empty());
  const ImageHeader& main_header = boot_image_spaces[0]->GetImageHeader();
  ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects =
      ObjPtr<mirror::ObjectArray<mirror::Object>>::DownCast(
          main_header.GetImageRoot<kWithoutReadBarrier>(ImageHeader::kBootImageLiveObjects));
  DCHECK(boot_image_live_objects != nullptr);
  DCHECK(heap->ObjectIsInBootImageSpace(boot_image_live_objects));
  return boot_image_live_objects;
}

static ObjPtr<mirror::ObjectArray<mirror::Object>> GetIntegerCacheArray(
    ObjPtr<mirror::Class> cache_class) REQUIRES_SHARED(Locks::mutator_lock_) {
  ArtField* cache_field = WellKnownClasses::java_lang_Integer_IntegerCache_cache;
  return ObjPtr<mirror::ObjectArray<mirror::Object>>::DownCast(cache_field->GetObject(cache_class));
}

static int32_t GetIntegerCacheField(ObjPtr<mirror::Class> cache_class, const char* field_name)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ArtField* field = cache_class->FindDeclaredStaticField(field_name, "I");
  DCHECK(field != nullptr);
  return field->GetInt(cache_class);
}

bool IntrinsicVisitor::CheckIntegerCacheFields(ObjPtr<mirror::ObjectArray<mirror::Object>> cache) {
  ObjPtr<mirror::Class> cache_class = WellKnownClasses::java_lang_Integer_IntegerCache.Get();
  // Check that the range matches the boot image cache length.
  int32_t low = GetIntegerCacheField(cache_class, kLowFieldName);
  int32_t high = GetIntegerCacheField(cache_class, kHighFieldName);
  if (low != kIntegerCacheLow || high != kIntegerCacheHigh) {
    return false;
  }
  if (cache->GetLength() != high - low + 1) {
    return false;
  }

  // Check that the elements match the values we expect.
  ObjPtr<mirror::Class> integer_class = WellKnownClasses::java_lang_Integer.Get();
  DCHECK(integer_class->IsInitialized());
  ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
  DCHECK(value_field != nullptr);
  for (int32_t i = 0, len = cache->GetLength(); i != len; ++i) {
    ObjPtr<mirror::Object> current_object = cache->Get(i);
    if (value_field->GetInt(current_object) != low + i) {
      return false;
    }
  }
  return true;
}

static bool CheckIntegerCache(ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Since we have a cache in the boot image, both java.lang.Integer and
  // java.lang.Integer$IntegerCache must be initialized in the boot image.
  ObjPtr<mirror::Class> cache_class = WellKnownClasses::java_lang_Integer_IntegerCache.Get();
  DCHECK(cache_class->IsInitialized());
  ObjPtr<mirror::Class> integer_class = WellKnownClasses::java_lang_Integer.Get();
  DCHECK(integer_class->IsInitialized());

  ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_cache = GetIntegerCacheArray(cache_class);
  if (!IntrinsicVisitor::CheckIntegerCacheFields(boot_image_cache)) {
    return false;
  }

  // Check that the elements match the boot image intrinsic objects and check their values as well.
  ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
  DCHECK(value_field != nullptr);
  for (int32_t i = 0, len = boot_image_cache->GetLength(); i != len; ++i) {
    ObjPtr<mirror::Object> boot_image_object =
        IntrinsicObjects::GetIntegerValueOfObject(boot_image_live_objects, i);
    DCHECK(Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(boot_image_object));
    // No need for read barrier for comparison with a boot image object.
    ObjPtr<mirror::Object> current_object =
        boot_image_cache->GetWithoutChecks<kVerifyNone, kWithoutReadBarrier>(i);
    if (boot_image_object != current_object) {
      return false;  // Messed up IntegerCache.cache[i]
    }
    if (value_field->GetInt(boot_image_object) != kIntegerCacheLow + i) {
      return false;  // Messed up IntegerCache.cache[i].value.
    }
  }

  return true;
}

static bool CanReferenceBootImageObjects(HInvoke* invoke, const CompilerOptions& compiler_options) {
  // Piggyback on the method load kind to determine whether we can use PC-relative addressing
  // for AOT. This should cover both the testing config (non-PIC boot image) and codegens that
  // reject PC-relative load kinds and fall back to the runtime call.
  if (compiler_options.IsAotCompiler() &&
      !invoke->AsInvokeStaticOrDirect()->HasPcRelativeMethodLoadKind()) {
    return false;
  }
  if (!compiler_options.IsBootImage() &&
      Runtime::Current()->GetHeap()->GetBootImageSpaces().empty()) {
    return false;  // Running without boot image, cannot use required boot image objects.
  }
  return true;
}

void IntrinsicVisitor::ComputeIntegerValueOfLocations(HInvoke* invoke,
                                                      CodeGenerator* codegen,
                                                      Location return_location,
                                                      Location first_argument_location) {
  // The intrinsic will call if it needs to allocate a j.l.Integer.
  LocationSummary::CallKind call_kind = LocationSummary::kCallOnMainOnly;
  const CompilerOptions& compiler_options = codegen->GetCompilerOptions();
  if (!CanReferenceBootImageObjects(invoke, compiler_options)) {
    return;
  }
  HInstruction* const input = invoke->InputAt(0);
  if (compiler_options.IsBootImage()) {
    if (!compiler_options.IsImageClass(kIntegerCacheDescriptor) ||
        !compiler_options.IsImageClass(kIntegerDescriptor)) {
      return;
    }
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Class> cache_class = WellKnownClasses::java_lang_Integer_IntegerCache.Get();
    DCHECK(cache_class->IsInitialized());
    ObjPtr<mirror::Class> integer_class = WellKnownClasses::java_lang_Integer.Get();
    DCHECK(integer_class->IsInitialized());
    int32_t low = kIntegerCacheLow;
    int32_t high = kIntegerCacheHigh;
    if (kIsDebugBuild) {
      CHECK_EQ(low, GetIntegerCacheField(cache_class, kLowFieldName));
      CHECK_EQ(high, GetIntegerCacheField(cache_class, kHighFieldName));
      ObjPtr<mirror::ObjectArray<mirror::Object>> current_cache = GetIntegerCacheArray(cache_class);
      CHECK(current_cache != nullptr);
      CHECK_EQ(current_cache->GetLength(), high - low + 1);
      ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
      CHECK(value_field != nullptr);
      for (int32_t i = 0, len = current_cache->GetLength(); i != len; ++i) {
        ObjPtr<mirror::Object> current_object = current_cache->GetWithoutChecks(i);
        CHECK(current_object != nullptr);
        CHECK_EQ(value_field->GetInt(current_object), low + i);
      }
    }
    if (input->IsIntConstant()) {
      int32_t value = input->AsIntConstant()->GetValue();
      if (static_cast<uint32_t>(value) - static_cast<uint32_t>(low) <
          static_cast<uint32_t>(high - low + 1)) {
        // No call, we shall use direct pointer to the Integer object.
        call_kind = LocationSummary::kNoCall;
      }
    }
  } else {
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects = GetBootImageLiveObjects();
    DCHECK_IMPLIES(compiler_options.IsAotCompiler(), CheckIntegerCache(boot_image_live_objects));

    if (input->IsIntConstant()) {
      if (kIsDebugBuild) {
        // Check the `value` from the lowest cached Integer.
        ObjPtr<mirror::Object> low_integer =
            IntrinsicObjects::GetIntegerValueOfObject(boot_image_live_objects, 0u);
        ObjPtr<mirror::Class> integer_class =
            low_integer->GetClass<kVerifyNone, kWithoutReadBarrier>();
        ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
        DCHECK(value_field != nullptr);
        DCHECK_EQ(kIntegerCacheLow, value_field->GetInt(low_integer));
      }
      int32_t value = input->AsIntConstant()->GetValue();
      if (static_cast<uint32_t>(value) - static_cast<uint32_t>(kIntegerCacheLow) <
          static_cast<uint32_t>(kIntegerCacheLength)) {
          // No call, we shall use direct pointer to the Integer object.
          call_kind = LocationSummary::kNoCall;
      }
    }
  }

  ArenaAllocator* allocator = codegen->GetGraph()->GetAllocator();
  LocationSummary* locations = new (allocator) LocationSummary(invoke, call_kind, kIntrinsified);
  if (call_kind == LocationSummary::kCallOnMainOnly) {
    locations->SetInAt(0, Location::RegisterOrConstant(input));
    locations->AddTemp(first_argument_location);
    locations->SetOut(return_location);
  } else {
    locations->SetInAt(0, Location::ConstantLocation(input));
    locations->SetOut(Location::RequiresRegister());
  }
}

inline IntrinsicVisitor::IntegerValueOfInfo::IntegerValueOfInfo()
    : value_offset(0),
      low(0),
      length(0u),
      value_boot_image_reference(kInvalidReference) {}

IntrinsicVisitor::IntegerValueOfInfo IntrinsicVisitor::ComputeIntegerValueOfInfo(
    HInvoke* invoke, const CompilerOptions& compiler_options) {
  // Note that we could cache all of the data looked up here. but there's no good
  // location for it. We don't want to add it to WellKnownClasses, to avoid creating global
  // jni values. Adding it as state to the compiler singleton seems like wrong
  // separation of concerns.
  // The need for this data should be pretty rare though.

  // Note that at this point we can no longer abort the code generation. Therefore,
  // we need to provide data that shall not lead to a crash even if the fields were
  // modified through reflection since ComputeIntegerValueOfLocations() when JITting.

  ScopedObjectAccess soa(Thread::Current());
  IntegerValueOfInfo info;
  info.low = kIntegerCacheLow;
  info.length = kIntegerCacheLength;
  if (compiler_options.IsBootImage()) {
    ObjPtr<mirror::Class> integer_class = invoke->GetResolvedMethod()->GetDeclaringClass();
    DCHECK(integer_class->DescriptorEquals(kIntegerDescriptor));
    ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
    DCHECK(value_field != nullptr);
    info.value_offset = value_field->GetOffset().Uint32Value();
    ObjPtr<mirror::Class> cache_class = WellKnownClasses::java_lang_Integer_IntegerCache.Get();
    DCHECK_EQ(info.low, GetIntegerCacheField(cache_class, kLowFieldName));
    DCHECK_EQ(kIntegerCacheHigh, GetIntegerCacheField(cache_class, kHighFieldName));

    if (invoke->InputAt(0)->IsIntConstant()) {
      int32_t input_value = invoke->InputAt(0)->AsIntConstant()->GetValue();
      uint32_t index = static_cast<uint32_t>(input_value) - static_cast<uint32_t>(info.low);
      if (index < static_cast<uint32_t>(info.length)) {
        info.value_boot_image_reference = IntrinsicObjects::EncodePatch(
            IntrinsicObjects::PatchType::kIntegerValueOfObject, index);
      } else {
        // Not in the cache.
        info.value_boot_image_reference = IntegerValueOfInfo::kInvalidReference;
      }
    } else {
      info.array_data_boot_image_reference =
          IntrinsicObjects::EncodePatch(IntrinsicObjects::PatchType::kIntegerValueOfArray);
    }
  } else {
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects = GetBootImageLiveObjects();
    ObjPtr<mirror::Object> low_integer =
        IntrinsicObjects::GetIntegerValueOfObject(boot_image_live_objects, 0u);
    ObjPtr<mirror::Class> integer_class = low_integer->GetClass<kVerifyNone, kWithoutReadBarrier>();
    ArtField* value_field = integer_class->FindDeclaredInstanceField(kValueFieldName, "I");
    DCHECK(value_field != nullptr);
    info.value_offset = value_field->GetOffset().Uint32Value();

    if (invoke->InputAt(0)->IsIntConstant()) {
      int32_t input_value = invoke->InputAt(0)->AsIntConstant()->GetValue();
      uint32_t index = static_cast<uint32_t>(input_value) - static_cast<uint32_t>(info.low);
      if (index < static_cast<uint32_t>(info.length)) {
        ObjPtr<mirror::Object> integer =
            IntrinsicObjects::GetIntegerValueOfObject(boot_image_live_objects, index);
        info.value_boot_image_reference = CodeGenerator::GetBootImageOffset(integer);
      } else {
        // Not in the cache.
        info.value_boot_image_reference = IntegerValueOfInfo::kInvalidReference;
      }
    } else {
      info.array_data_boot_image_reference =
          CodeGenerator::GetBootImageOffset(boot_image_live_objects) +
          IntrinsicObjects::GetIntegerValueOfArrayDataOffset(boot_image_live_objects).Uint32Value();
    }
  }

  return info;
}

MemberOffset IntrinsicVisitor::GetReferenceDisableIntrinsicOffset() {
  ScopedObjectAccess soa(Thread::Current());
  // The "disableIntrinsic" is the first static field.
  ArtField* field = GetClassRoot<mirror::Reference>()->GetStaticField(0);
  DCHECK_STREQ(field->GetName(), "disableIntrinsic");
  return field->GetOffset();
}

MemberOffset IntrinsicVisitor::GetReferenceSlowPathEnabledOffset() {
  ScopedObjectAccess soa(Thread::Current());
  // The "slowPathEnabled" is the second static field.
  ArtField* field = GetClassRoot<mirror::Reference>()->GetStaticField(1);
  DCHECK_STREQ(field->GetName(), "slowPathEnabled");
  return field->GetOffset();
}

void IntrinsicVisitor::CreateReferenceGetReferentLocations(HInvoke* invoke,
                                                           CodeGenerator* codegen) {
  if (!CanReferenceBootImageObjects(invoke, codegen->GetCompilerOptions())) {
    return;
  }

  ArenaAllocator* allocator = codegen->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicVisitor::CreateReferenceRefersToLocations(HInvoke* invoke, CodeGenerator* codegen) {
  if (codegen->EmitNonBakerReadBarrier()) {
    // Unimplemented for non-Baker read barrier.
    return;
  }

  ArenaAllocator* allocator = invoke->GetBlock()->GetGraph()->GetAllocator();
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicVisitor::AssertNonMovableStringClass() {
  if (kIsDebugBuild) {
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Class> string_class = GetClassRoot<mirror::String>();
    CHECK(!art::Runtime::Current()->GetHeap()->IsMovableObject(string_class));
  }
}

void InsertFpToIntegralIntrinsic(HInvokeStaticOrDirect* invoke, size_t input_index) {
  DCHECK_EQ(invoke->GetCodePtrLocation(), CodePtrLocation::kCallCriticalNative);
  DCHECK(!invoke->GetBlock()->GetGraph()->IsDebuggable())
      << "Unexpected direct @CriticalNative call in a debuggable graph!";
  DCHECK_LT(input_index, invoke->GetNumberOfArguments());
  HInstruction* input = invoke->InputAt(input_index);
  DataType::Type input_type = input->GetType();
  DCHECK(DataType::IsFloatingPointType(input_type));
  bool is_double = (input_type == DataType::Type::kFloat64);
  DataType::Type converted_type = is_double ? DataType::Type::kInt64 : DataType::Type::kInt32;
  ArtMethod* resolved_method = is_double
      ? WellKnownClasses::java_lang_Double_doubleToRawLongBits
      : WellKnownClasses::java_lang_Float_floatToRawIntBits;
  DCHECK(resolved_method != nullptr);
  DCHECK(resolved_method->IsIntrinsic());
  MethodReference target_method(nullptr, 0);
  {
    ScopedObjectAccess soa(Thread::Current());
    target_method =
        MethodReference(resolved_method->GetDexFile(), resolved_method->GetDexMethodIndex());
  }
  // Use arbitrary dispatch info that does not require the method argument.
  HInvokeStaticOrDirect::DispatchInfo dispatch_info = {
      MethodLoadKind::kBssEntry,
      CodePtrLocation::kCallArtMethod,
      /*method_load_data=*/ 0u
  };
  HBasicBlock* block = invoke->GetBlock();
  ArenaAllocator* allocator = block->GetGraph()->GetAllocator();
  HInvokeStaticOrDirect* new_input = new (allocator) HInvokeStaticOrDirect(
      allocator,
      /*number_of_arguments=*/ 1u,
      converted_type,
      invoke->GetDexPc(),
      /*method_reference=*/ MethodReference(nullptr, dex::kDexNoIndex),
      resolved_method,
      dispatch_info,
      kStatic,
      target_method,
      HInvokeStaticOrDirect::ClinitCheckRequirement::kNone,
      /*enable_intrinsic_opt=*/ true);
  // The intrinsic has no side effects and does not need the environment.
  new_input->SetSideEffects(SideEffects::None());
  IntrinsicOptimizations opt(new_input);
  opt.SetDoesNotNeedEnvironment();
  new_input->SetRawInputAt(0u, input);
  block->InsertInstructionBefore(new_input, invoke);
  invoke->ReplaceInput(new_input, input_index);
}

}  // namespace art
