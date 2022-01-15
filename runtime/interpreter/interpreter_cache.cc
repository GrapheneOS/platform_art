/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "interpreter_cache.h"
#include "thread-inl.h"

namespace art {

std::array<std::atomic<InterpreterCache::Entry>,
           InterpreterCache::kSharedSize> InterpreterCache::shared_array_;

InterpreterCache::InterpreterCache() {
  // We can not use the ClearThreadLocal method since the constructor will not
  // be called from the owning thread.
  thread_local_array_.fill(Entry{});
}

void InterpreterCache::ClearThreadLocal(Thread* owning_thread) {
  // Must be called from the owning thread or when the owning thread is suspended.
  DCHECK(owning_thread->GetInterpreterCache() == this);
  DCHECK(owning_thread == Thread::Current() || owning_thread->IsSuspended());

  thread_local_array_.fill(Entry{});
}

void InterpreterCache::ClearShared() {
  // Can be called from any thread since the writes are atomic.
  // The static shared cache isn't bound to specific thread in the first place.

  for (std::atomic<Entry>& entry : shared_array_) {
    AtomicPairStoreRelease(&entry, Entry{});
  }
}

}  // namespace art
