/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_NOOP_COMPILER_CALLBACKS_H_
#define ART_RUNTIME_NOOP_COMPILER_CALLBACKS_H_

#include "base/macros.h"
#include "class_linker.h"
#include "compiler_callbacks.h"

namespace art HIDDEN {

// Used for tests and some tools that pretend to be a compiler (say, oatdump).
class NoopCompilerCallbacks final : public CompilerCallbacks {
 public:
  NoopCompilerCallbacks() : CompilerCallbacks(CompilerCallbacks::CallbackMode::kCompileApp) {}
  ~NoopCompilerCallbacks() {}

  ClassLinker* CreateAotClassLinker(InternTable* intern_table) override {
    return new PermissiveClassLinker(intern_table);
  }

  void AddUncompilableMethod([[maybe_unused]] MethodReference ref) override {}
  void AddUncompilableClass([[maybe_unused]] ClassReference ref) override {}
  bool IsUncompilableMethod([[maybe_unused]] MethodReference ref) override { return false; }
  void ClassRejected([[maybe_unused]] ClassReference ref) override {}

  verifier::VerifierDeps* GetVerifierDeps() const override { return nullptr; }

 private:
  // When we supply compiler callbacks, we need an appropriate `ClassLinker` that can
  // handle `SdkChecker`-related calls that are unimplemented in the base `ClassLinker`.
  class PermissiveClassLinker : public ClassLinker {
   public:
    explicit PermissiveClassLinker(InternTable* intern_table)
        : ClassLinker(intern_table, /*fast_class_not_found_exceptions=*/ false) {}

    bool DenyAccessBasedOnPublicSdk([[maybe_unused]] ArtMethod* art_method) const override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      return false;
    }
    bool DenyAccessBasedOnPublicSdk([[maybe_unused]] ArtField* art_field) const override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      return false;
    }
    bool DenyAccessBasedOnPublicSdk(
        [[maybe_unused]] std::string_view type_descriptor) const override {
      return false;
    }
    void SetEnablePublicSdkChecks([[maybe_unused]] bool enabled) override {}

    // Transaction-related virtual functions should not be called on `PermissiveClassLinker`.

    bool TransactionWriteConstraint([[maybe_unused]] Thread* self,
                                    [[maybe_unused]] ObjPtr<mirror::Object> obj) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    bool TransactionWriteValueConstraint([[maybe_unused]] Thread* self,
                                         [[maybe_unused]] ObjPtr<mirror::Object> value) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    bool TransactionAllocationConstraint([[maybe_unused]] Thread* self,
                                         [[maybe_unused]] ObjPtr<mirror::Class> klass) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteFieldBoolean([[maybe_unused]] mirror::Object* obj,
                                 [[maybe_unused]] MemberOffset field_offset,
                                 [[maybe_unused]] uint8_t value,
                                 [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteFieldByte([[maybe_unused]] mirror::Object* obj,
                              [[maybe_unused]] MemberOffset field_offset,
                              [[maybe_unused]] int8_t value,
                              [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteFieldChar([[maybe_unused]] mirror::Object* obj,
                              [[maybe_unused]] MemberOffset field_offset,
                              [[maybe_unused]] uint16_t value,
                              [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteFieldShort([[maybe_unused]] mirror::Object* obj,
                               [[maybe_unused]] MemberOffset field_offset,
                               [[maybe_unused]] int16_t value,
                               [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteField32([[maybe_unused]] mirror::Object* obj,
                            [[maybe_unused]] MemberOffset field_offset,
                            [[maybe_unused]] uint32_t value,
                            [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteField64([[maybe_unused]] mirror::Object* obj,
                            [[maybe_unused]] MemberOffset field_offset,
                            [[maybe_unused]] uint64_t value,
                            [[maybe_unused]] bool is_volatile) override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteFieldReference([[maybe_unused]] mirror::Object* obj,
                                   [[maybe_unused]] MemberOffset field_offset,
                                   [[maybe_unused]] ObjPtr<mirror::Object> value,
                                   [[maybe_unused]] bool is_volatile) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWriteArray([[maybe_unused]] mirror::Array* array,
                          [[maybe_unused]] size_t index,
                          [[maybe_unused]] uint64_t value) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordStrongStringInsertion([[maybe_unused]] ObjPtr<mirror::String> s) override
        REQUIRES(Locks::intern_table_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWeakStringInsertion([[maybe_unused]] ObjPtr<mirror::String> s) override
        REQUIRES(Locks::intern_table_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordStrongStringRemoval([[maybe_unused]] ObjPtr<mirror::String> s) override
        REQUIRES(Locks::intern_table_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordWeakStringRemoval([[maybe_unused]] ObjPtr<mirror::String> s) override
        REQUIRES(Locks::intern_table_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordResolveString([[maybe_unused]] ObjPtr<mirror::DexCache> dex_cache,
                             [[maybe_unused]] dex::StringIndex string_idx) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void RecordResolveMethodType([[maybe_unused]] ObjPtr<mirror::DexCache> dex_cache,
                                 [[maybe_unused]] dex::ProtoIndex proto_idx) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void ThrowTransactionAbortError([[maybe_unused]] Thread* self) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void AbortTransactionF([[maybe_unused]] Thread* self,
                           [[maybe_unused]] const char* fmt, ...) override
        __attribute__((__format__(__printf__, 3, 4)))
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void AbortTransactionV([[maybe_unused]] Thread* self,
                           [[maybe_unused]] const char* fmt,
                           [[maybe_unused]] va_list args) override
        REQUIRES_SHARED(Locks::mutator_lock_) {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    bool IsTransactionAborted() const override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }

    void VisitTransactionRoots([[maybe_unused]] RootVisitor* visitor) override {
      // Nothing to do for `PermissiveClassLinker`, only `AotClassLinker` handles transactions.
    }

    const void* GetTransactionalInterpreter() override {
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    }
  };

  DISALLOW_COPY_AND_ASSIGN(NoopCompilerCallbacks);
};

}  // namespace art

#endif  // ART_RUNTIME_NOOP_COMPILER_CALLBACKS_H_
