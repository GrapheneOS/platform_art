/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef ART_RUNTIME_INSTRUMENTATION_INL_H_
#define ART_RUNTIME_INSTRUMENTATION_INL_H_

#include "instrumentation.h"

#include "art_method-inl.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "gc/heap.h"
#include "jit/jit.h"
#include "runtime.h"

namespace art HIDDEN {
namespace instrumentation {

inline bool Instrumentation::CanUseAotCode(const void* quick_code) {
  if (quick_code == nullptr) {
    return false;
  }
  Runtime* runtime = Runtime::Current();
  // For simplicity, we never use AOT code for debuggable.
  if (runtime->IsJavaDebuggable()) {
    return false;
  }

  if (runtime->IsNativeDebuggable()) {
    DCHECK(runtime->UseJitCompilation() && runtime->GetJit()->JitAtFirstUse());
    // If we are doing native debugging, ignore application's AOT code,
    // since we want to JIT it (at first use) with extra stackmaps for native
    // debugging. We keep however all AOT code from the boot image,
    // since the JIT-at-first-use is blocking and would result in non-negligible
    // startup performance impact.
    return runtime->GetHeap()->IsInBootImageOatFile(quick_code);
  }

  return true;
}

inline const void* Instrumentation::GetInitialEntrypoint(uint32_t method_access_flags,
                                                         const void* aot_code) {
  if (!ArtMethod::IsInvokable(method_access_flags)) {
    return GetQuickToInterpreterBridge();
  }

  // Special case if we need an initialization check.
  if (ArtMethod::NeedsClinitCheckBeforeCall(method_access_flags)) {
    // If we have code but the method needs a class initialization check before calling that code,
    // install the resolution stub that will perform the check. It will be replaced by the proper
    // entry point by `ClassLinker::FixupStaticTrampolines()` after initializing class.
    // Note: This mimics the logic in image_writer.cc that installs the resolution stub only
    // if we have compiled code or we can execute nterp, and the method needs a class
    // initialization check.
    return (aot_code != nullptr || ArtMethod::IsNative(method_access_flags))
        ? GetQuickResolutionStub()
        : GetQuickToInterpreterBridge();
  }

  // Use the provided AOT code if possible.
  if (CanUseAotCode(aot_code)) {
    return aot_code;
  }

  // Use default entrypoints.
  return ArtMethod::IsNative(method_access_flags) ? GetQuickGenericJniStub()
                                                  : GetQuickToInterpreterBridge();
}


inline bool Instrumentation::InitialEntrypointNeedsInstrumentationStubs() {
  return IsForcedInterpretOnly() || EntryExitStubsInstalled();
}

inline void Instrumentation::InitializeMethodsCode(ArtMethod* method,
                                                   const void* entrypoint,
                                                   PointerSize pointer_size) {
  if (kIsDebugBuild) {
    // Entrypoint should be uninitialized.
    CHECK(method->GetEntryPointFromQuickCompiledCodePtrSize(pointer_size) == nullptr)
        << method->PrettyMethod();
    // We initialize the entrypoint while loading the class, well before the class
    // is verified and Nterp entrypoint is allowed. We prefer to check for resolved
    // because a verified class may lose its "verified" status (by becoming erroneous)
    // but the resolved status is always kept (as "resolved erroneous" if needed).
    CHECK(!method->GetDeclaringClass()->IsResolved());
    CHECK_NE(entrypoint, interpreter::GetNterpEntryPoint()) << method->PrettyMethod();
    if (InitialEntrypointNeedsInstrumentationStubs()) {
      const void* expected =
          method->IsNative() ? GetQuickGenericJniStub() : GetQuickToInterpreterBridge();
      CHECK_EQ(entrypoint, expected) << method->PrettyMethod() << " " << method->IsNative();
    } else if (method->NeedsClinitCheckBeforeCall()) {
      if (method->IsNative()) {
        CHECK_EQ(entrypoint, GetQuickResolutionStub());
      } else {
        // We do not have the original `aot_code` to determine which entrypoint to expect.
        CHECK(entrypoint == GetQuickResolutionStub() ||
              entrypoint == GetQuickToInterpreterBridge());
      }
    } else {
      bool is_stub = (entrypoint == GetQuickToInterpreterBridge()) ||
                     (entrypoint == GetQuickGenericJniStub()) ||
                     (entrypoint == GetQuickResolutionStub());
      const void* aot_code = is_stub ? nullptr : entrypoint;
      const void* initial = GetInitialEntrypoint(method->GetAccessFlags(), aot_code);
      CHECK_EQ(initial, entrypoint)
          << method->PrettyMethod() << " 0x" << std::hex << method->GetAccessFlags();
    }
  }
  method->SetEntryPointFromQuickCompiledCodePtrSize(entrypoint, pointer_size);
}

}  // namespace instrumentation
}  // namespace art

#endif  // ART_RUNTIME_INSTRUMENTATION_INL_H_
