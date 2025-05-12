/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "aot_class_linker.h"

#include "base/stl_util.h"
#include "class_status.h"
#include "compiler_callbacks.h"
#include "dex/class_reference.h"
#include "gc/heap.h"
#include "handle_scope-inl.h"
#include "interpreter/interpreter_switch_impl.h"
#include "mirror/class-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "transaction.h"
#include "verifier/verifier_enums.h"

namespace art HIDDEN {

AotClassLinker::AotClassLinker(InternTable* intern_table)
    : ClassLinker(intern_table, /*fast_class_not_found_exceptions=*/ false),
      preinitialization_transactions_() {}

AotClassLinker::~AotClassLinker() {}

bool AotClassLinker::CanAllocClass() {
  // AllocClass doesn't work under transaction, so we abort.
  if (IsActiveTransaction()) {
    AbortTransactionF(Thread::Current(), "Can't resolve type within transaction.");
    return false;
  }
  return ClassLinker::CanAllocClass();
}

// Wrap the original InitializeClass with creation of transaction when in strict mode.
bool AotClassLinker::InitializeClass(Thread* self,
                                     Handle<mirror::Class> klass,
                                     bool can_init_statics,
                                     bool can_init_parents) {
  bool strict_mode = IsActiveStrictTransactionMode();

  DCHECK(klass != nullptr);
  if (klass->IsInitialized() || klass->IsInitializing()) {
    return ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);
  }

  // When compiling a boot image extension, do not initialize a class defined
  // in a dex file belonging to the boot image we're compiling against.
  // However, we must allow the initialization of TransactionAbortError,
  // VerifyError, etc. outside of a transaction.
  if (!strict_mode &&
      Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(klass->GetDexCache())) {
    if (IsActiveTransaction()) {
      AbortTransactionF(self,
                        "Can't initialize %s because it is defined in a boot image dex file.",
                        klass->PrettyTypeOf().c_str());
      return false;
    }
    CHECK(klass->IsThrowableClass()) << klass->PrettyDescriptor();
  }

  // When in strict_mode, don't initialize a class if it belongs to boot but not initialized.
  if (strict_mode && klass->IsBootStrapClassLoaded()) {
    AbortTransactionF(self,
                      "Can't resolve %s because it is an uninitialized boot class.",
                      klass->PrettyTypeOf().c_str());
    return false;
  }

  // Don't initialize klass if it's superclass is not initialized, because superclass might abort
  // the transaction and rolled back after klass's change is commited.
  if (strict_mode && !klass->IsInterface() && klass->HasSuperClass()) {
    if (klass->GetSuperClass()->GetStatus() == ClassStatus::kInitializing) {
      AbortTransactionF(self,
                        "Can't resolve %s because it's superclass is not initialized.",
                        klass->PrettyTypeOf().c_str());
      return false;
    }
  }

  if (strict_mode) {
    EnterTransactionMode(/*strict=*/ true, klass.Get());
  }
  bool success = ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);

  if (strict_mode) {
    if (success) {
      // Exit Transaction if success.
      ExitTransactionMode();
    } else {
      // If not successfully initialized, don't rollback immediately, leave the cleanup to compiler
      // driver which needs abort message and exception.
      DCHECK(self->IsExceptionPending());
    }
  }
  return success;
}

verifier::FailureKind AotClassLinker::PerformClassVerification(
    Thread* self,
    verifier::VerifierDeps* verifier_deps,
    Handle<mirror::Class> klass,
    verifier::HardFailLogMode log_level,
    std::string* error_msg) {
  Runtime* const runtime = Runtime::Current();
  CompilerCallbacks* callbacks = runtime->GetCompilerCallbacks();
  ClassStatus old_status = callbacks->GetPreviousClassState(
      ClassReference(&klass->GetDexFile(), klass->GetDexClassDefIndex()));
  // Was it verified? Report no failure.
  if (old_status >= ClassStatus::kVerified) {
    return verifier::FailureKind::kNoFailure;
  }
  if (old_status >= ClassStatus::kVerifiedNeedsAccessChecks) {
    return verifier::FailureKind::kAccessChecksFailure;
  }
  // Does it need to be verified at runtime? Report soft failure.
  if (old_status >= ClassStatus::kRetryVerificationAtRuntime) {
    // Error messages from here are only reported through -verbose:class. It is not worth it to
    // create a message.
    return verifier::FailureKind::kSoftFailure;
  }
  // Do the actual work.
  return ClassLinker::PerformClassVerification(self, verifier_deps, klass, log_level, error_msg);
}

static const std::vector<const DexFile*>* gAppImageDexFiles = nullptr;

void AotClassLinker::SetAppImageDexFiles(const std::vector<const DexFile*>* app_image_dex_files) {
  gAppImageDexFiles = app_image_dex_files;
}

bool AotClassLinker::CanReferenceInBootImageExtensionOrAppImage(
    ObjPtr<mirror::Class> klass, gc::Heap* heap) {
  // Do not allow referencing a class or instance of a class defined in a dex file
  // belonging to the boot image we're compiling against but not itself in the boot image;
  // or a class referencing such classes as component type, superclass or interface.
  // Allowing this could yield duplicate class objects from multiple images.

  if (heap->ObjectIsInBootImageSpace(klass)) {
    return true;  // Already included in the boot image we're compiling against.
  }

  // Treat arrays and primitive types specially because they do not have a DexCache that we
  // can use to check whether the dex file belongs to the boot image we're compiling against.
  DCHECK(!klass->IsPrimitive());  // Primitive classes must be in the primary boot image.
  if (klass->IsArrayClass()) {
    DCHECK(heap->ObjectIsInBootImageSpace(klass->GetIfTable()));  // IfTable is OK.
    // Arrays of all dimensions are tied to the dex file of the non-array component type.
    do {
      klass = klass->GetComponentType();
    } while (klass->IsArrayClass());
    if (klass->IsPrimitive()) {
      return false;
    }
    // Do not allow arrays of erroneous classes (the array class is not itself erroneous).
    if (klass->IsErroneous()) {
      return false;
    }
  }

  auto can_reference_dex_cache = [&](ObjPtr<mirror::DexCache> dex_cache)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    // We cannot reference a boot image dex cache for classes
    // that were not themselves in the boot image.
    if (heap->ObjectIsInBootImageSpace(dex_cache)) {
      return false;
    }
    // App image compilation can pull in dex files from parent or library class loaders.
    // Classes from such dex files cannot be included or referenced in the current app image
    // to avoid conflicts with classes in the parent or library class loader's app image.
    if (gAppImageDexFiles != nullptr &&
        !ContainsElement(*gAppImageDexFiles, dex_cache->GetDexFile())) {
      return false;
    }
    return true;
  };

  // Check the class itself.
  if (!can_reference_dex_cache(klass->GetDexCache())) {
    return false;
  }

  // Check superclasses.
  ObjPtr<mirror::Class> superclass = klass->GetSuperClass();
  while (!heap->ObjectIsInBootImageSpace(superclass)) {
    DCHECK(superclass != nullptr);  // Cannot skip Object which is in the primary boot image.
    if (!can_reference_dex_cache(superclass->GetDexCache())) {
      return false;
    }
    superclass = superclass->GetSuperClass();
  }

  // Check IfTable. This includes direct and indirect interfaces.
  ObjPtr<mirror::IfTable> if_table = klass->GetIfTable();
  for (size_t i = 0, num_interfaces = klass->GetIfTableCount(); i < num_interfaces; ++i) {
    ObjPtr<mirror::Class> interface = if_table->GetInterface(i);
    DCHECK(interface != nullptr);
    if (!heap->ObjectIsInBootImageSpace(interface) &&
        !can_reference_dex_cache(interface->GetDexCache())) {
      return false;
    }
  }

  if (kIsDebugBuild) {
    // All virtual methods must come from classes we have already checked above.
    PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
    ObjPtr<mirror::Class> k = klass;
    while (!heap->ObjectIsInBootImageSpace(k)) {
      for (auto& m : k->GetMethods(pointer_size)) {
        if (m.IsVirtual()) {
          ObjPtr<mirror::Class> declaring_class = m.GetDeclaringClass();
          CHECK(heap->ObjectIsInBootImageSpace(declaring_class) ||
                can_reference_dex_cache(declaring_class->GetDexCache()));
        }
      }
      k = k->GetSuperClass();
    }
  }

  return true;
}

void AotClassLinker::SetSdkChecker(std::unique_ptr<SdkChecker>&& sdk_checker) {
  sdk_checker_ = std::move(sdk_checker);
}

const SdkChecker* AotClassLinker::GetSdkChecker() const {
  return sdk_checker_.get();
}

bool AotClassLinker::DenyAccessBasedOnPublicSdk(ArtMethod* art_method) const {
  return sdk_checker_ != nullptr && sdk_checker_->ShouldDenyAccess(art_method);
}
bool AotClassLinker::DenyAccessBasedOnPublicSdk(ArtField* art_field) const {
  return sdk_checker_ != nullptr && sdk_checker_->ShouldDenyAccess(art_field);
}
bool AotClassLinker::DenyAccessBasedOnPublicSdk(std::string_view type_descriptor) const {
  return sdk_checker_ != nullptr && sdk_checker_->ShouldDenyAccess(type_descriptor);
}

void AotClassLinker::SetEnablePublicSdkChecks(bool enabled) {
  if (sdk_checker_ != nullptr) {
    sdk_checker_->SetEnabled(enabled);
  }
}

// Transaction support.

bool AotClassLinker::IsActiveTransaction() const {
  bool result = Runtime::Current()->IsActiveTransaction();
  DCHECK_EQ(result, !preinitialization_transactions_.empty() && !GetTransaction()->IsRollingBack());
  return result;
}

void AotClassLinker::EnterTransactionMode(bool strict, mirror::Class* root) {
  Runtime* runtime = Runtime::Current();
  ArenaPool* arena_pool = nullptr;
  ArenaStack* arena_stack = nullptr;
  if (preinitialization_transactions_.empty()) {  // Top-level transaction?
    // Make initialized classes visibly initialized now. If that happened during the transaction
    // and then the transaction was aborted, we would roll back the status update but not the
    // ClassLinker's bookkeeping structures, so these classes would never be visibly initialized.
    {
      Thread* self = Thread::Current();
      StackHandleScope<1> hs(self);
      HandleWrapper<mirror::Class> h(hs.NewHandleWrapper(&root));
      ScopedThreadSuspension sts(self, ThreadState::kNative);
      MakeInitializedClassesVisiblyInitialized(Thread::Current(), /*wait=*/ true);
    }
    // Pass the runtime `ArenaPool` to the transaction.
    arena_pool = runtime->GetArenaPool();
  } else {
    // Pass the `ArenaStack` from previous transaction to the new one.
    arena_stack = preinitialization_transactions_.front().GetArenaStack();
  }
  preinitialization_transactions_.emplace_front(strict, root, arena_stack, arena_pool);
  runtime->SetActiveTransaction();
}

void AotClassLinker::ExitTransactionMode() {
  DCHECK(IsActiveTransaction());
  preinitialization_transactions_.pop_front();
  if (preinitialization_transactions_.empty()) {
    Runtime::Current()->ClearActiveTransaction();
  } else {
    DCHECK(IsActiveTransaction());  // Trigger the DCHECK() in `IsActiveTransaction()`.
  }
}

void AotClassLinker::RollbackAllTransactions() {
  // If transaction is aborted, all transactions will be kept in the list.
  // Rollback and exit all of them.
  while (IsActiveTransaction()) {
    RollbackAndExitTransactionMode();
  }
}

void AotClassLinker::RollbackAndExitTransactionMode() {
  DCHECK(IsActiveTransaction());
  Runtime::Current()->ClearActiveTransaction();
  preinitialization_transactions_.front().Rollback();
  preinitialization_transactions_.pop_front();
  if (!preinitialization_transactions_.empty()) {
    Runtime::Current()->SetActiveTransaction();
  }
}

const Transaction* AotClassLinker::GetTransaction() const {
  DCHECK(!preinitialization_transactions_.empty());
  return &preinitialization_transactions_.front();
}

Transaction* AotClassLinker::GetTransaction() {
  DCHECK(!preinitialization_transactions_.empty());
  return &preinitialization_transactions_.front();
}

bool AotClassLinker::IsActiveStrictTransactionMode() const {
  return IsActiveTransaction() && GetTransaction()->IsStrict();
}

bool AotClassLinker::TransactionWriteConstraint(Thread* self, ObjPtr<mirror::Object> obj) {
  DCHECK(IsActiveTransaction());
  if (GetTransaction()->WriteConstraint(obj)) {
    Runtime* runtime = Runtime::Current();
    DCHECK(runtime->GetHeap()->ObjectIsInBootImageSpace(obj) || obj->IsClass());
    const char* extra = runtime->GetHeap()->ObjectIsInBootImageSpace(obj) ? "boot image " : "";
    AbortTransactionF(
        self, "Can't set fields of %s%s", extra, obj->PrettyTypeOf().c_str());
    return true;
  }
  return false;
}

bool AotClassLinker::TransactionWriteValueConstraint(Thread* self, ObjPtr<mirror::Object> value) {
  DCHECK(IsActiveTransaction());
  if (GetTransaction()->WriteValueConstraint(value)) {
    DCHECK(value != nullptr);
    const char* description = value->IsClass() ? "class" : "instance of";
    ObjPtr<mirror::Class> klass = value->IsClass() ? value->AsClass() : value->GetClass();
    AbortTransactionF(
        self, "Can't store reference to %s %s", description, klass->PrettyDescriptor().c_str());
    return true;
  }
  return false;
}

bool AotClassLinker::TransactionReadConstraint(Thread* self, ObjPtr<mirror::Object> obj) {
  DCHECK(obj->IsClass());
  if (GetTransaction()->ReadConstraint(obj)) {
    AbortTransactionF(self,
                      "Can't read static fields of %s since it does not belong to clinit's class.",
                      obj->PrettyTypeOf().c_str());
    return true;
  }
  return false;
}

bool AotClassLinker::TransactionAllocationConstraint(Thread* self, ObjPtr<mirror::Class> klass) {
  DCHECK(IsActiveTransaction());
  if (klass->IsFinalizable()) {
    AbortTransactionF(self,
                      "Allocating finalizable object in transaction: %s",
                      klass->PrettyDescriptor().c_str());
    return true;
  }
  return false;
}

void AotClassLinker::RecordWriteFieldBoolean(mirror::Object* obj,
                                             MemberOffset field_offset,
                                             uint8_t value,
                                             bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteFieldBoolean(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteFieldByte(mirror::Object* obj,
                                          MemberOffset field_offset,
                                          int8_t value,
                                          bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteFieldByte(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteFieldChar(mirror::Object* obj,
                                          MemberOffset field_offset,
                                          uint16_t value,
                                          bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteFieldChar(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteFieldShort(mirror::Object* obj,
                                           MemberOffset field_offset,
                                           int16_t value,
                                           bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteFieldShort(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteField32(mirror::Object* obj,
                                        MemberOffset field_offset,
                                        uint32_t value,
                                        bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteField32(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteField64(mirror::Object* obj,
                                        MemberOffset field_offset,
                                        uint64_t value,
                                        bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteField64(obj, field_offset, value, is_volatile);
}

void AotClassLinker::RecordWriteFieldReference(mirror::Object* obj,
                                               MemberOffset field_offset,
                                               ObjPtr<mirror::Object> value,
                                               bool is_volatile) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteFieldReference(obj, field_offset, value.Ptr(), is_volatile);
}

void AotClassLinker::RecordWriteArray(mirror::Array* array, size_t index, uint64_t value) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWriteArray(array, index, value);
}

void AotClassLinker::RecordStrongStringInsertion(ObjPtr<mirror::String> s) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordStrongStringInsertion(s);
}

void AotClassLinker::RecordWeakStringInsertion(ObjPtr<mirror::String> s) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWeakStringInsertion(s);
}

void AotClassLinker::RecordStrongStringRemoval(ObjPtr<mirror::String> s) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordStrongStringRemoval(s);
}

void AotClassLinker::RecordWeakStringRemoval(ObjPtr<mirror::String> s) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordWeakStringRemoval(s);
}

void AotClassLinker::RecordResolveString(ObjPtr<mirror::DexCache> dex_cache,
                                         dex::StringIndex string_idx) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordResolveString(dex_cache, string_idx);
}

void AotClassLinker::RecordResolveMethodType(ObjPtr<mirror::DexCache> dex_cache,
                                             dex::ProtoIndex proto_idx) {
  DCHECK(IsActiveTransaction());
  GetTransaction()->RecordResolveMethodType(dex_cache, proto_idx);
}

void AotClassLinker::ThrowTransactionAbortError(Thread* self) {
  DCHECK(IsActiveTransaction());
  // Passing nullptr means we rethrow an exception with the earlier transaction abort message.
  GetTransaction()->ThrowAbortError(self, nullptr);
}

void AotClassLinker::AbortTransactionF(Thread* self, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  AbortTransactionV(self, fmt, args);
  va_end(args);
}

void AotClassLinker::AbortTransactionV(Thread* self, const char* fmt, va_list args) {
  CHECK(IsActiveTransaction());
  // Constructs abort message.
  std::string abort_message;
  android::base::StringAppendV(&abort_message, fmt, args);
  // Throws an exception so we can abort the transaction and rollback every change.
  //
  // Throwing an exception may cause its class initialization. If we mark the transaction
  // aborted before that, we may warn with a false alarm. Throwing the exception before
  // marking the transaction aborted avoids that.
  // But now the transaction can be nested, and abort the transaction will relax the constraints
  // for constructing stack trace.
  GetTransaction()->Abort(abort_message);
  GetTransaction()->ThrowAbortError(self, &abort_message);
}

bool AotClassLinker::IsTransactionAborted() const {
  DCHECK(IsActiveTransaction());
  return GetTransaction()->IsAborted();
}

void AotClassLinker::VisitTransactionRoots(RootVisitor* visitor) {
  for (Transaction& transaction : preinitialization_transactions_) {
    transaction.VisitRoots(visitor);
  }
}

const void* AotClassLinker::GetTransactionalInterpreter() {
  return reinterpret_cast<const void*>(
      &interpreter::ExecuteSwitchImplCpp</*transaction_active=*/ true>);
}

}  // namespace art
