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

#include "startup_completed_task.h"

#include "base/systrace.h"
#include "class_linker.h"
#include "gc/heap.h"
#include "gc/scoped_gc_critical_section.h"
#include "gc/space/image_space.h"
#include "gc/space/space-inl.h"
#include "handle_scope-inl.h"
#include "linear_alloc-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/object-inl.h"
#include "obj_ptr.h"
#include "runtime_image.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "thread_list.h"

namespace art {

class UnlinkStartupDexCacheVisitor : public DexCacheVisitor {
 public:
  UnlinkStartupDexCacheVisitor() {}

  void Visit(ObjPtr<mirror::DexCache> dex_cache)
      REQUIRES_SHARED(Locks::dex_lock_, Locks::mutator_lock_) override {
    dex_cache->UnlinkStartupCaches();
  }
};

void StartupCompletedTask::Run(Thread* self) {
  VLOG(startup) << "StartupCompletedTask running";
  Runtime* const runtime = Runtime::Current();
  if (!runtime->NotifyStartupCompleted()) {
    return;
  }

  // Maybe generate a runtime app image. If the runtime is debuggable, boot
  // classpath classes can be dynamically changed, so don't bother generating an
  // image.
  if (!runtime->IsJavaDebuggable()) {
    std::string compiler_filter;
    std::string compilation_reason;
    runtime->GetAppInfo()->GetPrimaryApkOptimizationStatus(&compiler_filter, &compilation_reason);
    CompilerFilter::Filter filter;
    if (CompilerFilter::ParseCompilerFilter(compiler_filter.c_str(), &filter) &&
        !CompilerFilter::IsAotCompilationEnabled(filter)) {
      std::string error_msg;
      if (!RuntimeImage::WriteImageToDisk(&error_msg)) {
        LOG(DEBUG) << "Could not write temporary image to disk " << error_msg;
      }
    }
  }

  {
    ScopedTrace trace("Releasing dex caches and app image spaces metadata");
    ScopedObjectAccess soa(Thread::Current());

    {
      UnlinkStartupDexCacheVisitor visitor;
      ReaderMutexLock mu(self, *Locks::dex_lock_);
      runtime->GetClassLinker()->VisitDexCaches(&visitor);
    }

    // Request a checkpoint to make sure no threads are:
    // - accessing the image space metadata section when we madvise it
    // - accessing dex caches when we free them
    static struct EmptyClosure : Closure {
      void Run(Thread* thread ATTRIBUTE_UNUSED) override {}
    } closure;

    runtime->GetThreadList()->RunCheckpoint(&closure);

    // Now delete dex cache arrays from both images and startup linear alloc in
    // a critical section. The critical section is to ensure there is no
    // possibility the GC can temporarily see those arrays.
    gc::ScopedGCCriticalSection sgcs(soa.Self(),
                                     gc::kGcCauseDeletingDexCacheArrays,
                                     gc::kCollectorTypeCriticalSection);
    for (gc::space::ContinuousSpace* space : runtime->GetHeap()->GetContinuousSpaces()) {
      if (space->IsImageSpace()) {
        gc::space::ImageSpace* image_space = space->AsImageSpace();
        if (image_space->GetImageHeader().IsAppImage()) {
          image_space->ReleaseMetadata();
        }
      }
    }

    std::unique_ptr<LinearAlloc> startup_linear_alloc(runtime->ReleaseStartupLinearAlloc());
    if (startup_linear_alloc != nullptr) {
      ScopedTrace trace2("Delete startup linear alloc");
      ArenaPool* arena_pool = startup_linear_alloc->GetArenaPool();
      startup_linear_alloc.reset();
      arena_pool->TrimMaps();
    }
  }

  // Delete the thread pool used for app image loading since startup is assumed to be completed.
  ScopedTrace trace2("Delete thread pool");
  runtime->DeleteThreadPool();
}

}  // namespace art
