/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "common_transaction_test.h"

#include "aot_class_linker.h"
#include "runtime.h"

namespace art HIDDEN {

class CommonTransactionTestCompilerCallbacks : public CompilerCallbacks {
 public:
  CommonTransactionTestCompilerCallbacks()
      : CompilerCallbacks(CompilerCallbacks::CallbackMode::kCompileApp) {}

  ClassLinker* CreateAotClassLinker(InternTable* intern_table) override {
    return new AotClassLinker(intern_table);
  }

  void AddUncompilableMethod([[maybe_unused]] MethodReference ref) override {}
  void AddUncompilableClass([[maybe_unused]] ClassReference ref) override {}
  void ClassRejected([[maybe_unused]] ClassReference ref) override {}
  bool IsUncompilableMethod([[maybe_unused]] MethodReference ref) override { return false; }

  verifier::VerifierDeps* GetVerifierDeps() const override { return nullptr; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CommonTransactionTestCompilerCallbacks);
};

CompilerCallbacks* CommonTransactionTestImpl::CreateCompilerCallbacks() {
  return new CommonTransactionTestCompilerCallbacks();
}

void CommonTransactionTestImpl::EnterTransactionMode() {
  CHECK(!Runtime::Current()->IsActiveTransaction());
  AotClassLinker* class_linker = down_cast<AotClassLinker*>(Runtime::Current()->GetClassLinker());
  class_linker->EnterTransactionMode(/*strict=*/ false, /*root=*/ nullptr);
}

void CommonTransactionTestImpl::ExitTransactionMode() {
  AotClassLinker* class_linker = down_cast<AotClassLinker*>(Runtime::Current()->GetClassLinker());
  class_linker->ExitTransactionMode();
  CHECK(!Runtime::Current()->IsActiveTransaction());
}

void CommonTransactionTestImpl::RollbackAndExitTransactionMode() {
  AotClassLinker* class_linker = down_cast<AotClassLinker*>(Runtime::Current()->GetClassLinker());
  class_linker->RollbackAndExitTransactionMode();
  CHECK(!Runtime::Current()->IsActiveTransaction());
}

bool CommonTransactionTestImpl::IsTransactionAborted() {
  if (!Runtime::Current()->IsActiveTransaction()) {
    return false;
  }
  AotClassLinker* class_linker = down_cast<AotClassLinker*>(Runtime::Current()->GetClassLinker());
  return class_linker->IsTransactionAborted();
}

}  // namespace art
