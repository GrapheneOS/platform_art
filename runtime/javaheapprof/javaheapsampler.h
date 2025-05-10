/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_
#define ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_

#include <random>
#include "base/locks.h"
#include "base/mutex.h"
#include "mirror/object.h"

namespace art HIDDEN {

class HeapSampler {
 public:
  HeapSampler()
      : rng_(/*seed=*/std::minstd_rand::default_seed),
        rng_lock_("Heap Sampler RNG lock", art::LockLevel::kGenericBottomLock) {}

  void SetHeapID(uint32_t heap_id) {
    perfetto_heap_id_ = heap_id;
  }
  void EnableHeapSampler() {
    enabled_.store(true, std::memory_order_release);
  }
  void DisableHeapSampler() {
    enabled_.store(false, std::memory_order_release);
  }

  // Reports an allocation of the given size to perfetto.  This should be
  // called for all allocations. Sampling is done internally to reduce the
  // performance overhead based on the sampling interval.
  EXPORT void ReportAllocation(art::mirror::Object* obj, size_t allocation_size);

  // Report a tlab allocation. This will adjust the allocation size based on
  // the number of bytes allocated in the thread local buffer since the last
  // sample was reported.
  // The allocation_size should be the size of the object being allocated.
  // pre_tlab_size should be the TlabSize() before the allocation, and
  // post_tlab_size should be the TlabSize() after the allocation.
  void ReportTlabAllocation(art::mirror::Object* obj,
                            size_t allocation_size,
                            size_t pre_tlab_size,
                            size_t post_tlab_size);

  // Computes and records the next TLAB allocation size to use based on the
  // given target size. If profiling is enabled, the size is chosen randomly
  // based on the current sampling interval, otherwise the target is returned
  // directly.
  size_t NextTlabSize(size_t target) REQUIRES(!rng_lock_) {
    return IsEnabled() ? NextRandomTlabSize(target) : target;
  }

  // Computes and records the next TLAB allocation size to use based on the
  // given target size, assuming profiling is enabled. The size is chosen
  // randomly based on the current sampling interval.
  size_t NextRandomTlabSize(size_t target) REQUIRES(!rng_lock_);

  // Is heap sampler enabled?
  bool IsEnabled() { return enabled_.load(std::memory_order_acquire); }
  // Set the sampling interval.
  void SetSamplingInterval(size_t sampling_interval);
  // Return the sampling interval.
  size_t GetSamplingInterval();

 private:
  // The number of bytes remaining in the thread local buffer that we have not
  // sampled yet.
  static thread_local size_t tlab_unsampled_bytes_;

  std::atomic<bool> enabled_{false};
  // Default sampling interval is 4kb.
  std::atomic<size_t> p_sampling_interval_{4 * 1024};
  uint32_t perfetto_heap_id_ = 0;
  // std random number generator.
  std::minstd_rand rng_ GUARDED_BY(rng_lock_);  // Holds the state
  // Multiple threads can access the random number generator concurrently and
  // thus rng_lock_ is used for thread safety.
  art::Mutex rng_lock_;
};

}  // namespace art

#endif  // ART_RUNTIME_JAVAHEAPPROF_JAVAHEAPSAMPLER_H_
