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

class CollectStartupDexCacheVisitor : public DexCacheVisitor {
 public:
  explicit CollectStartupDexCacheVisitor(VariableSizedHandleScope& handles) : handles_(handles) {}

  void Visit(ObjPtr<mirror::DexCache> dex_cache)
      REQUIRES_SHARED(Locks::dex_lock_, Locks::mutator_lock_) override {
    handles_.NewHandle(dex_cache);
  }

 private:
  VariableSizedHandleScope& handles_;
};

class UnlinkVisitor {
 public:
  UnlinkVisitor() {}

  void VisitRootIfNonNull(StackReference<mirror::Object>* ref)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!ref->IsNull()) {
      ref->AsMirrorPtr()->AsDexCache()->UnlinkStartupCaches();
    }
  }
};

void StartupCompletedTask::Run(Thread* self) {
  VLOG(startup) << "StartupCompletedTask running";
  Runtime* const runtime = Runtime::Current();
  if (!runtime->NotifyStartupCompleted()) {
    return;
  }

  // Maybe generate a runtime app image.
  {
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

  // Fetch the startup linear alloc before the checkpoint to play nice with
  // 1002-notify-startup test which resets the startup state.
  std::unique_ptr<LinearAlloc> startup_linear_alloc(runtime->ReleaseStartupLinearAlloc());
  {
    ScopedTrace trace("Releasing dex caches and app image spaces metadata");
    ScopedObjectAccess soa(Thread::Current());

    // Collect dex caches that were allocated with the startup linear alloc.
    VariableSizedHandleScope handles(soa.Self());
    {
      CollectStartupDexCacheVisitor visitor(handles);
      ReaderMutexLock mu(self, *Locks::dex_lock_);
      runtime->GetClassLinker()->VisitDexCaches(&visitor);
    }

    // Request empty checkpoints to make sure no threads are:
    // - accessing the image space metadata section when we madvise it
    // - accessing dex caches when we free them
    //
    // Use GC exclusion to prevent deadlocks that may happen if
    // multiple threads are attempting to run empty checkpoints at the same time.
    {
      // Avoid using ScopedGCCriticalSection since that does not allow thread suspension. This is
      // not allowed to prevent allocations, but it's still safe to suspend temporarily for the
      // checkpoint.
      gc::ScopedInterruptibleGCCriticalSection sigcs(self,
                                                     gc::kGcCauseRunEmptyCheckpoint,
                                                     gc::kCollectorTypeCriticalSection);
      // Do the unlinking of dex cache arrays in the GC critical section to
      // avoid GC not seeing these arrays. We do it before the checkpoint so
      // we know after the checkpoint, no thread is holding onto the array.
      UnlinkVisitor visitor;
      handles.VisitRoots(visitor);

      runtime->GetThreadList()->RunEmptyCheckpoint();
    }

    for (gc::space::ContinuousSpace* space : runtime->GetHeap()->GetContinuousSpaces()) {
      if (space->IsImageSpace()) {
        gc::space::ImageSpace* image_space = space->AsImageSpace();
        if (image_space->GetImageHeader().IsAppImage()) {
          image_space->ReleaseMetadata();
        }
      }
    }
  }

  {
    // Delete the thread pool used for app image loading since startup is assumed to be completed.
    ScopedTrace trace2("Delete thread pool");
    runtime->DeleteThreadPool();
  }

  if (startup_linear_alloc != nullptr) {
    // We know that after the checkpoint, there is no thread that can hold
    // the startup linear alloc, so it's safe to delete it now.
    ScopedTrace trace2("Delete startup linear alloc");
    ArenaPool* arena_pool = startup_linear_alloc->GetArenaPool();
    startup_linear_alloc.reset();
    arena_pool->TrimMaps();
  }
}

}  // namespace art
