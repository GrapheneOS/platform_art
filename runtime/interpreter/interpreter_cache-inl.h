/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ART_RUNTIME_INTERPRETER_INTERPRETER_CACHE_INL_H_
#define ART_RUNTIME_INTERPRETER_INTERPRETER_CACHE_INL_H_

#include "interpreter_cache.h"
#include "thread.h"

namespace art {

template<bool kSkipThreadLocal>
inline bool InterpreterCache::Get(Thread* self, const void* dex_instr, /* out */ size_t* value) {
  DCHECK(self->GetInterpreterCache() == this) << "Must be called from owning thread";
  size_t key = reinterpret_cast<size_t>(dex_instr);
  Entry& local_entry = thread_local_array_[IndexOf<kThreadLocalSize>(key)];
  if (kSkipThreadLocal) {
    DCHECK_NE(local_entry.first, key) << "Expected cache miss";
  } else {
    if (LIKELY(local_entry.first == key)) {
      *value = local_entry.second;
      return true;
    }
  }
  Entry shared_entry = AtomicPairLoadAcquire(&shared_array_[IndexOf<kSharedSize>(key)]);
  if (LIKELY(shared_entry.first == key)) {
    // For simplicity, only update the cache if weak ref accesses are enabled. If
    // they are disabled, this means the GC is processing the cache, and is
    // reading it concurrently.
    if (self->GetWeakRefAccessEnabled()) {
      local_entry = shared_entry;  // Copy to local array to make future lookup faster.
    }
    *value = shared_entry.second;
    return true;
  }
  return false;
}

inline void InterpreterCache::Set(Thread* self, const void* dex_instr, size_t value) {
  DCHECK(self->GetInterpreterCache() == this) << "Must be called from owning thread";

  // For simplicity, only update the cache if weak ref accesses are enabled. If
  // they are disabled, this means the GC is processing the cache, and is
  // reading it concurrently.
  if (self->GetWeakRefAccessEnabled()) {
    size_t key = reinterpret_cast<size_t>(dex_instr);
    thread_local_array_[IndexOf<kThreadLocalSize>(key)] = {key, value};
    AtomicPairStoreRelease(&shared_array_[IndexOf<kSharedSize>(key)], {key, value});
  }
}

}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_INTERPRETER_CACHE_INL_H_
