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

#include <iostream>

#include "android-base/file.h"
#include "android-base/strings.h"
#include "base/file_utils.h"
#include "base/mem_map.h"
#include "dex/class_accessor-inl.h"
#include "dex/dex_file_verifier.h"
#include "dex/standard_dex_file.h"
#include "handle_scope-inl.h"
#include "interpreter/unstarted_runtime.h"
#include "jni/java_vm_ext.h"
#include "noop_compiler_callbacks.h"
#include "runtime.h"
#include "runtime_intrinsics.h"
#include "scoped_thread_state_change-inl.h"
#include "verifier/class_verifier.h"
#include "well_known_classes.h"

// Global variable to count how many DEX files passed DEX file verification and they were
// registered, since these are the cases for which we would be running the GC. In case of
// scheduling multiple fuzzer jobs, using the ‘-jobs’ flag, this is not shared among the threads.
int skipped_gc_iterations = 0;
// Global variable to call the GC once every maximum number of iterations.
// TODO: These values were obtained from local experimenting. They can be changed after
// further investigation.
static constexpr int kMaxSkipGCIterations = 100;
// Global variable to signal LSAN that we are not leaking memory.
uint8_t* allocated_signal_stack = nullptr;

namespace art {
// A class to be friends with ClassLinker and access the internal FindDexCacheDataLocked method.
class VerifyClassesFuzzerHelper {
 public:
  static const ClassLinker::DexCacheData* GetDexCacheData(Runtime* runtime, const DexFile* dex_file)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    ReaderMutexLock mu(self, *Locks::dex_lock_);
    ClassLinker* class_linker = runtime->GetClassLinker();
    const ClassLinker::DexCacheData* cached_data = class_linker->FindDexCacheDataLocked(*dex_file);
    return cached_data;
  }
};
}  // namespace art

std::string GetDexFileName(const std::string& jar_name) {
  // The jar files are located in the data directory within the directory of the fuzzer's binary.
  std::string executable_dir = android::base::GetExecutableDirectory();

  std::string result =
      android::base::StringPrintf("%s/data/%s.jar", executable_dir.c_str(), jar_name.c_str());

  return result;
}

std::vector<std::string> GetLibCoreDexFileNames() {
  std::vector<std::string> result;
  const std::vector<std::string> modules = {
      "core-oj",
      "core-libart",
      "okhttp",
      "bouncycastle",
      "apache-xml",
      "core-icu4j",
      "conscrypt",
  };
  result.reserve(modules.size());
  for (const std::string& module : modules) {
    result.push_back(GetDexFileName(module));
  }
  return result;
}

std::string GetClassPathOption(const char* option, const std::vector<std::string>& class_path) {
  return option + android::base::Join(class_path, ':');
}

jobject RegisterDexFileAndGetClassLoader(art::Runtime* runtime, art::StandardDexFile* dex_file)
    REQUIRES_SHARED(art::Locks::mutator_lock_) {
  art::Thread* self = art::Thread::Current();
  art::ClassLinker* class_linker = runtime->GetClassLinker();
  const std::vector<const art::DexFile*> dex_files = {dex_file};
  jobject class_loader = class_linker->CreatePathClassLoader(self, dex_files);
  art::ObjPtr<art::mirror::ClassLoader> cl = self->DecodeJObject(class_loader)->AsClassLoader();
  class_linker->RegisterDexFile(*dex_file, cl);
  return class_loader;
}

extern "C" int LLVMFuzzerInitialize([[maybe_unused]] int* argc, [[maybe_unused]] char*** argv) {
  // Set logging to error and above to avoid warnings about unexpected checksums.
  android::base::SetMinimumLogSeverity(android::base::ERROR);

  // Create runtime.
  art::RuntimeOptions options;
  {
    static art::NoopCompilerCallbacks callbacks;
    options.push_back(std::make_pair("compilercallbacks", &callbacks));
  }

  std::string boot_class_path_string =
      GetClassPathOption("-Xbootclasspath:", GetLibCoreDexFileNames());
  options.push_back(std::make_pair(boot_class_path_string, nullptr));

  // Instruction set.
  options.push_back(
      std::make_pair("imageinstructionset",
                     reinterpret_cast<const void*>(GetInstructionSetString(art::kRuntimeISA))));

  if (!art::Runtime::Create(options, false)) {
    LOG(FATAL) << "We should always be able to create the runtime";
    UNREACHABLE();
  }

  art::interpreter::UnstartedRuntime::Initialize();
  art::Runtime::Current()->GetClassLinker()->RunEarlyRootClinits(art::Thread::Current());
  art::InitializeIntrinsics();
  art::Runtime::Current()->RunRootClinits(art::Thread::Current());

  // Check for heap corruption before running the fuzzer.
  art::Runtime::Current()->GetHeap()->VerifyHeap();

  // Runtime::Create acquired the mutator_lock_ that is normally given away when we
  // Runtime::Start, give it away now with `TransitionFromSuspendedToRunnable` until we figure out
  // how to start a Runtime.
  art::Thread::Current()->TransitionFromRunnableToSuspended(art::ThreadState::kNative);

  // Query the current stack and add it to the global variable. Otherwise LSAN complains about a
  // non-existing leak.
  stack_t ss;
  if (sigaltstack(nullptr, &ss) == -1) {
    PLOG(FATAL) << "sigaltstack failed";
  }
  allocated_signal_stack = reinterpret_cast<uint8_t*>(ss.ss_sp);


  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Do not verify the checksum as we only care about the DEX file contents,
  // and know that the checksum would probably be erroneous (i.e. random).
  constexpr bool kVerify = false;

  auto container = std::make_shared<art::MemoryDexFileContainer>(data, size);
  art::StandardDexFile dex_file(data,
                                /*location=*/"fuzz.dex",
                                /*location_checksum=*/0,
                                /*oat_dex_file=*/nullptr,
                                container);
  std::string error_msg;
  const bool verify_result =
      art::dex::Verify(&dex_file, dex_file.GetLocation().c_str(), kVerify, &error_msg);

  if (!verify_result) {
    // DEX file couldn't be verified, don't save it in the corpus.
    return -1;
  }

  art::Runtime* runtime = art::Runtime::Current();
  CHECK(runtime != nullptr);

  art::ScopedObjectAccess soa(art::Thread::Current());
  art::ClassLinker* class_linker = runtime->GetClassLinker();
  jobject class_loader = RegisterDexFileAndGetClassLoader(runtime, &dex_file);

  // Scope for the handles
  {
    art::StackHandleScope<4> scope(soa.Self());
    art::Handle<art::mirror::ClassLoader> h_loader =
        scope.NewHandle(soa.Decode<art::mirror::ClassLoader>(class_loader));
    art::MutableHandle<art::mirror::Class> h_klass(scope.NewHandle<art::mirror::Class>(nullptr));
    art::MutableHandle<art::mirror::DexCache> h_dex_cache(
        scope.NewHandle<art::mirror::DexCache>(nullptr));
    art::MutableHandle<art::mirror::ClassLoader> h_dex_cache_class_loader =
        scope.NewHandle(h_loader.Get());

    for (art::ClassAccessor accessor : dex_file.GetClasses()) {
      h_klass.Assign(
          class_linker->FindClass(soa.Self(), dex_file, accessor.GetClassIdx(), h_loader));
      // Ignore classes that couldn't be loaded since we are looking for crashes during
      // class/method verification.
      if (h_klass == nullptr || h_klass->IsErroneous()) {
        soa.Self()->ClearException();
        continue;
      }
      h_dex_cache.Assign(h_klass->GetDexCache());

      // The class loader from the class's dex cache is different from the dex file's class loader
      // for boot image classes e.g. java.util.AbstractCollection.
      h_dex_cache_class_loader.Assign(h_klass->GetDexCache()->GetClassLoader());
      art::verifier::ClassVerifier::VerifyClass(soa.Self(),
                                                /* verifier_deps= */ nullptr,
                                                h_dex_cache->GetDexFile(),
                                                h_klass,
                                                h_dex_cache,
                                                h_dex_cache_class_loader,
                                                *h_klass->GetClassDef(),
                                                runtime->GetCompilerCallbacks(),
                                                art::verifier::HardFailLogMode::kLogWarning,
                                                /* api_level= */ 0,
                                                &error_msg);
    }
  }

  skipped_gc_iterations++;

  // Delete weak root to the DexCache before removing a DEX file from the cache. This is usually
  // handled by the GC, but since we are not calling it every iteration, we need to delete them
  // manually.
  const art::ClassLinker::DexCacheData* dex_cache_data =
      art::VerifyClassesFuzzerHelper::GetDexCacheData(runtime, &dex_file);
  soa.Env()->GetVm()->DeleteWeakGlobalRef(soa.Self(), dex_cache_data->weak_root);

  class_linker->RemoveDexFromCaches(dex_file);

  // Delete global ref and unload class loader to free RAM.
  soa.Env()->GetVm()->DeleteGlobalRef(soa.Self(), class_loader);

  if (skipped_gc_iterations == kMaxSkipGCIterations) {
    runtime->GetHeap()->CollectGarbage(/* clear_soft_references */ true);
    skipped_gc_iterations = 0;
  }

  return 0;
}
