/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_FAST_COMPILER_H_
#define ART_COMPILER_OPTIMIZING_FAST_COMPILER_H_

#include <memory>
#include <vector>

#include "arch/instruction_set.h"
#include "base/array_ref.h"
#include "base/macros.h"
#include "base/scoped_arena_containers.h"
#include "driver/compiler_options.h"
#include "handle_scope.h"

namespace art HIDDEN {

class ArenaAllocator;
class ArtMethod;
class DexCompilationUnit;
class VariableSizedHandleScope;

namespace mirror {
class Object;
}

/**
 * A lightweight, one-pass compiler. Goes over each dex instruction and emits
 * native code for it.
 */
class FastCompiler {
 public:
  static std::unique_ptr<FastCompiler> Compile(
      ArtMethod* method,
      [[maybe_unused]] ArenaAllocator* allocator,
      [[maybe_unused]] ArenaStack* arena_stack,
      [[maybe_unused]] VariableSizedHandleScope* handles,
      [[maybe_unused]] const CompilerOptions& compiler_options,
      [[maybe_unused]] const DexCompilationUnit& dex_compilation_unit) {
    if (method == nullptr) {
      return nullptr;
    }
    switch (compiler_options.GetInstructionSet()) {
#ifdef ART_ENABLE_CODEGEN_arm64
      case InstructionSet::kArm64:
        return CompileARM64(method,
                            allocator,
                            arena_stack,
                            handles,
                            compiler_options,
                            dex_compilation_unit);
#endif
      default:
        return nullptr;
    }
  }

  virtual ArrayRef<const uint8_t> GetCode() const = 0;
  virtual ScopedArenaVector<uint8_t> BuildStackMaps() const = 0;
  virtual ArrayRef<const uint8_t> GetCfiData() const = 0;
  virtual int32_t GetFrameSize() const = 0;
  virtual uint32_t GetNumberOfJitRoots() const = 0;
  virtual void EmitJitRoots(uint8_t* code,
                            const uint8_t* roots_data,
                            /*out*/std::vector<Handle<mirror::Object>>* roots)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;

  virtual ~FastCompiler() = default;

 private:
#ifdef ART_ENABLE_CODEGEN_arm64
  static std::unique_ptr<FastCompiler> CompileARM64(ArtMethod* method,
                                                    ArenaAllocator* allocator,
                                                    ArenaStack* arena_stack,
                                                    VariableSizedHandleScope* handles,
                                                    const CompilerOptions& compiler_options,
                                                    const DexCompilationUnit& dex_compilation_unit);
#endif
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_FAST_COMPILER_H_
