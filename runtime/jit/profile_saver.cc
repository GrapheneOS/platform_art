/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "profile_saver.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <utility>

#include "android-base/file.h"
#include "android-base/strings.h"
#include "app_info.h"
#include "art_method-inl.h"
#include "base/compiler_filter.h"
#include "base/logging.h"  // For VLOG.
#include "base/pointer_size.h"
#include "base/safe_map.h"
#include "base/scoped_arena_containers.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "class_table-inl.h"
#include "dex/dex_file_loader.h"
#include "dex_reference_collection.h"
#include "gc/collector_type.h"
#include "gc/gc_cause.h"
#include "jit/jit.h"
#include "jit/profiling_info.h"
#include "oat/oat_file_manager.h"
#include "profile/profile_compilation_info.h"
#include "scoped_thread_state_change-inl.h"

namespace art HIDDEN {

using Hotness = ProfileCompilationInfo::MethodHotness;

ProfileSaver* ProfileSaver::instance_ = nullptr;
pthread_t ProfileSaver::profiler_pthread_ = 0U;

static_assert(ProfileCompilationInfo::kIndividualInlineCacheSize ==
              InlineCache::kIndividualCacheSize,
              "InlineCache and ProfileCompilationInfo do not agree on kIndividualCacheSize");

// At what priority to schedule the saver threads. 9 is the lowest foreground priority on device.
static constexpr int kProfileSaverPthreadPriority = 9;

// The valid window time for responding to profile delay signal.
static constexpr uint64_t kProfileDelaySignalValidWindowMs = 2000;

static void SetProfileSaverThreadPriority(pthread_t thread, int priority) {
#if defined(ART_TARGET_ANDROID)
  int result = setpriority(PRIO_PROCESS, pthread_gettid_np(thread), priority);
  if (result != 0) {
    LOG(ERROR) << "Failed to setpriority to :" << priority;
  }
#else
  UNUSED(thread);
  UNUSED(priority);
#endif
}

static int GetDefaultThreadPriority() {
#if defined(ART_TARGET_ANDROID)
  pthread_attr_t attr;
  sched_param param;
  pthread_attr_init(&attr);
  pthread_attr_getschedparam(&attr, &param);
  return param.sched_priority;
#else
  return 0;
#endif
}

ProfileSaver::ProfileSaver(const ProfileSaverOptions& options, jit::JitCodeCache* jit_code_cache)
    : jit_code_cache_(jit_code_cache),
      shutting_down_(false),
      last_time_ns_saver_woke_up_(0),
      jit_activity_notifications_(0),
      wait_lock_("ProfileSaver wait lock"),
      period_condition_("ProfileSaver period condition", wait_lock_),
      total_bytes_written_(0),
      total_number_of_writes_(0),
      total_number_of_code_cache_queries_(0),
      total_number_of_skipped_writes_(0),
      total_number_of_failed_writes_(0),
      total_ms_of_sleep_(0),
      total_ns_of_work_(0),
      total_number_of_hot_spikes_(0),
      total_number_of_wake_ups_(0),
      options_(options),
      notify_delay_time_(0) {
  DCHECK(options_.IsEnabled());
}

ProfileSaver::~ProfileSaver() {
  for (auto& it : profile_cache_) {
    delete it.second;
  }
}

void ProfileSaver::NotifyStartupCompleted() {
  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::profiler_lock_);
  if (instance_ == nullptr || instance_->shutting_down_) {
    return;
  }
  MutexLock mu2(self, instance_->wait_lock_);
  instance_->period_condition_.Signal(self);
}

void ProfileSaver::NotifyDelayProfileSaving() {
  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::profiler_lock_);
  if (instance_ == nullptr || instance_->shutting_down_) {
    return;
  }
  instance_->notify_delay_time_.store(MilliTime(), std::memory_order_relaxed);
  VLOG(profiler) << "Profile saving might be delayed";
}

void ProfileSaver::Run() {
  Thread* self = Thread::Current();

  // For thread annotalysis, the setup is more complicated than it should be. Run needs to start
  // under mutex, but should drop it.
  Locks::profiler_lock_->ExclusiveUnlock(self);

  // Fetch the resolved classes for the app images after waiting for Startup
  // completion notification.
  const uint64_t thread_start_time = NanoTime();

  // Wait for startup to complete with a timeout at StartupCompletedTask.
  // Note that we may be woken up by JIT notifications.
  // We need to wait for startup to complete to make sure we have
  // the resolved classes and methods.
  while (!Runtime::Current()->GetStartupCompleted() && !ShuttingDown(self)) {
    MutexLock mu(self, wait_lock_);
    // Make sure to sleep again until startup is completed.
    period_condition_.Wait(self);
  }

  // Mark collected classes/methods as startup.
  FetchAndCacheResolvedClassesAndMethods(/*startup=*/ true);

  bool is_min_first_save_set =
      options_.GetMinFirstSaveMs() != ProfileSaverOptions::kMinFirstSaveMsNotSet;
  bool force_first_save = is_min_first_save_set && IsFirstSave();

  // When we save without waiting for JIT notifications we use a simple
  // exponential back off policy bounded by max_wait_without_jit.
  uint32_t max_wait_without_jit = options_.GetMinSavePeriodMs() * 16;
  uint64_t cur_wait_without_jit = options_.GetMinSavePeriodMs();

  // Loop for the profiled methods.
  while (!ShuttingDown(self)) {
    // In case of force_first_save we need to count from the start of the thread.
    uint64_t sleep_start = force_first_save ? thread_start_time : NanoTime();
    uint64_t sleep_time = 0;
    {
      MutexLock mu(self, wait_lock_);
      if (options_.GetWaitForJitNotificationsToSave()) {
        period_condition_.Wait(self);
      } else {
        period_condition_.TimedWait(self, cur_wait_without_jit, 0);
        if (cur_wait_without_jit < max_wait_without_jit) {
          cur_wait_without_jit *= 2;
        }
      }
      sleep_time = NanoTime() - sleep_start;
    }
    // Check if the thread was woken up for shutdown.
    if (ShuttingDown(self)) {
      break;
    }
    total_number_of_wake_ups_++;
    // We might have been woken up by a huge number of notifications to guarantee saving.
    // If we didn't meet the minimum saving period go back to sleep (only if missed by
    // a reasonable margin).
    uint64_t min_save_period_ns = MsToNs(force_first_save ? options_.GetMinFirstSaveMs() :
                                                                  options_.GetMinSavePeriodMs());
    // When the delay signal is valid (the notification delay time is within
    // kProfileDelaySignalValidWindowMs), period_condition_.TimedWait is used to wait for
    // the remaining window time to delay the processing of the profile. When there are multiple
    // consecutive delays, the maximum sleep time does not exceed min_save_period_ns * 2.
    // When profiling the boot class path, the delay mechanism is always disabled to ensure the
    // profile collection is not impacted.
    do {
      uint64_t time_since_notify = MilliTime() - notify_delay_time_.load(std::memory_order_relaxed);
      bool should_delay = !options_.GetProfileBootClassPath() &&
                          (time_since_notify < kProfileDelaySignalValidWindowMs);

      if (min_save_period_ns * 0.9 <= sleep_time &&
          !(should_delay && min_save_period_ns * 2 > sleep_time)) {
        break;
      }
      uint64_t wait_time = should_delay ? kProfileDelaySignalValidWindowMs - time_since_notify
                                        : NsToMs(min_save_period_ns - sleep_time);
      {
        MutexLock mu(self, wait_lock_);
        period_condition_.TimedWait(self, wait_time, 0);
        sleep_time = NanoTime() - sleep_start;
      }
      // Check if the thread was woken up for shutdown.
      if (ShuttingDown(self)) {
        break;
      }
      total_number_of_wake_ups_++;
    } while (true);

    total_ms_of_sleep_ += NsToMs(NanoTime() - sleep_start);

    if (ShuttingDown(self)) {
      break;
    }

    uint16_t number_of_new_methods = 0;
    uint64_t start_work = NanoTime();
    bool profile_saved_to_disk = ProcessProfilingInfo(
        /*force_save=*/ false,
        &number_of_new_methods);

    // Reset the flag, so we can continue on the normal schedule.
    force_first_save = false;

    // Update the notification counter based on result. Note that there might be contention on this
    // but we don't care about to be 100% precise.
    if (!profile_saved_to_disk) {
      // If we didn't save to disk it may be because we didn't have enough new methods.
      // Set the jit activity notifications to number_of_new_methods so we can wake up earlier
      // if needed.
      jit_activity_notifications_ = number_of_new_methods;
    }
    total_ns_of_work_ += NanoTime() - start_work;
  }
}

// Checks if the profile file is empty.
// Return true if the size of the profile file is 0 or if there were errors when
// trying to open the file.
static bool IsProfileEmpty(const std::string& location) {
  if (location.empty()) {
    return true;
  }

  struct stat stat_buffer;
  if (stat(location.c_str(), &stat_buffer) != 0) {
    if (VLOG_IS_ON(profiler)) {
      PLOG(WARNING) << "Failed to stat profile location for IsFirstUse: " << location;
    }
    return true;
  }

  VLOG(profiler) << "Profile " << location << " size=" << stat_buffer.st_size;
  return stat_buffer.st_size == 0;
}

bool ProfileSaver::IsFirstSave() {
  Thread* self = Thread::Current();
  SafeMap<std::string, std::pair<std::string, AppInfo::CodeType>> tracked_locations;
  {
    // Make a copy so that we don't hold the lock while doing I/O.
    MutexLock mu(self, *Locks::profiler_lock_);
    tracked_locations = tracked_profiles_;
  }

  for (const auto& it : tracked_locations) {
    if (ShuttingDown(self)) {
      return false;
    }
    const std::string& cur_profile = it.first;
    const std::string& ref_profile = it.second.first;

    // Check if any profile is non empty. If so, then this is not the first save.
    if (!IsProfileEmpty(cur_profile) || !IsProfileEmpty(ref_profile)) {
      return false;
    }
  }

  // All locations are empty. Assume this is the first use.
  VLOG(profiler) << "All profile locations are empty. This is considered to be first save";
  return true;
}

void ProfileSaver::NotifyJitActivity() {
  MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
  if (instance_ == nullptr || instance_->shutting_down_) {
    return;
  }
  instance_->NotifyJitActivityInternal();
}

void ProfileSaver::WakeUpSaver() {
  jit_activity_notifications_ = 0;
  last_time_ns_saver_woke_up_ = NanoTime();
  period_condition_.Signal(Thread::Current());
}

void ProfileSaver::NotifyJitActivityInternal() {
  // Unlikely to overflow but if it happens,
  // we would have waken up the saver long before that.
  jit_activity_notifications_++;
  // Note that we are not as precise as we could be here but we don't want to wake the saver
  // every time we see a hot method.
  if (jit_activity_notifications_ > options_.GetMinNotificationBeforeWake()) {
    MutexLock wait_mutex(Thread::Current(), wait_lock_);
    if ((NanoTime() - last_time_ns_saver_woke_up_) > MsToNs(options_.GetMinSavePeriodMs())) {
      WakeUpSaver();
    } else if (jit_activity_notifications_ > options_.GetMaxNotificationBeforeWake()) {
      // Make sure to wake up the saver if we see a spike in the number of notifications.
      // This is a precaution to avoid losing a big number of methods in case
      // this is a spike with no jit after.
      total_number_of_hot_spikes_++;
      WakeUpSaver();
    }
  }
}

class ProfileSaver::ScopedDefaultPriority {
 public:
  explicit ScopedDefaultPriority(pthread_t thread) : thread_(thread) {
    SetProfileSaverThreadPriority(thread_, GetDefaultThreadPriority());
  }

  ~ScopedDefaultPriority() {
    SetProfileSaverThreadPriority(thread_, kProfileSaverPthreadPriority);
  }

 private:
  const pthread_t thread_;
};

class ProfileSaver::GetClassesAndMethodsHelper {
 public:
  GetClassesAndMethodsHelper(bool startup,
                             const ProfileSaverOptions& options,
                             const ProfileCompilationInfo::ProfileSampleAnnotation& annotation)
      REQUIRES_SHARED(Locks::mutator_lock_)
      : startup_(startup),
        profile_boot_class_path_(options.GetProfileBootClassPath()),
        extra_flags_(GetExtraMethodHotnessFlags(options)),
        annotation_(annotation),
        arena_stack_(Runtime::Current()->GetArenaPool()),
        allocator_(&arena_stack_),
        class_loaders_(std::nullopt),
        dex_file_records_map_(allocator_.Adapter(kArenaAllocProfile)),
        number_of_hot_methods_(0u),
        number_of_sampled_methods_(0u) {
    std::fill_n(max_primitive_array_dimensions_.data(), max_primitive_array_dimensions_.size(), 0u);
  }

  ~GetClassesAndMethodsHelper() REQUIRES_SHARED(Locks::mutator_lock_) {
    // The `class_loaders_` member destructor needs the mutator lock.
    // We need to destroy arena-allocated dex file records.
    for (const auto& entry : dex_file_records_map_) {
      delete entry.second;
    }
  }

  void CollectClasses(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);
  void UpdateProfile(const std::set<std::string>& locations, ProfileCompilationInfo* profile_info);

  size_t GetNumberOfHotMethods() const {
    return number_of_hot_methods_;
  }

  size_t GetNumberOfSampledMethods() const {
    return number_of_sampled_methods_;
  }

 private:
  class CollectInternalVisitor {
   public:
    explicit CollectInternalVisitor(GetClassesAndMethodsHelper* helper)
        : helper_(helper) {}

    void VisitRootIfNonNull(StackReference<mirror::Object>* ref)
        REQUIRES_SHARED(Locks::mutator_lock_) {
      if (!ref->IsNull()) {
        helper_->CollectInternal</*kBootClassLoader=*/ false>(ref->AsMirrorPtr()->AsClassLoader());
      }
    }

   private:
    GetClassesAndMethodsHelper* helper_;
  };

  struct ClassRecord {
    dex::TypeIndex type_index;
    uint16_t array_dimension;
    LengthPrefixedArray<ArtMethod>* methods;
  };

  struct DexFileRecords : public DeletableArenaObject<kArenaAllocProfile> {
    explicit DexFileRecords(ScopedArenaAllocator* allocator)
        : class_records(allocator->Adapter(kArenaAllocProfile)),
          copied_methods(allocator->Adapter(kArenaAllocProfile)) {
      class_records.reserve(kInitialClassRecordsReservation);
    }

    static constexpr size_t kInitialClassRecordsReservation = 512;

    ScopedArenaVector<ClassRecord> class_records;
    ScopedArenaVector<ArtMethod*> copied_methods;
  };

  using DexFileRecordsMap = ScopedArenaHashMap<const DexFile*, DexFileRecords*>;

  ALWAYS_INLINE static bool ShouldCollectClasses(bool startup) {
    // We only record classes for the startup case. This may change in the future.
    return startup;
  }

  // Collect classes and methods from one class loader.
  template <bool kBootClassLoader>
  void CollectInternal(ObjPtr<mirror::ClassLoader> class_loader) NO_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_);

  const bool startup_;
  const bool profile_boot_class_path_;
  const uint32_t extra_flags_;
  const ProfileCompilationInfo::ProfileSampleAnnotation annotation_;
  ArenaStack arena_stack_;
  ScopedArenaAllocator allocator_;
  std::optional<VariableSizedHandleScope> class_loaders_;
  DexFileRecordsMap dex_file_records_map_;

  static_assert(Primitive::kPrimLast == Primitive::kPrimVoid);  // There are no arrays of void.
  std::array<uint8_t, static_cast<size_t>(Primitive::kPrimLast)> max_primitive_array_dimensions_;

  size_t number_of_hot_methods_;
  size_t number_of_sampled_methods_;
};

template <bool kBootClassLoader>
void ProfileSaver::GetClassesAndMethodsHelper::CollectInternal(
    ObjPtr<mirror::ClassLoader> class_loader) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  DCHECK_EQ(kBootClassLoader, class_loader == nullptr);

  // If the class loader has not loaded any classes, it may have a null table.
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  ClassTable* const table =
      class_linker->ClassTableForClassLoader(kBootClassLoader ? nullptr : class_loader);
  if (table == nullptr) {
    return;
  }

  // Move members to local variables to allow the compiler to optimize this properly.
  const bool startup = startup_;
  table->Visit([&](ObjPtr<mirror::Class> klass) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (kBootClassLoader ? (!klass->IsBootStrapClassLoaded())
                         : (klass->GetClassLoader() != class_loader)) {
      // To avoid processing a class more than once, we process each class only
      // when we encounter it in the defining class loader's class table.
      // This class has a different defining class loader, skip it.
      return true;
    }

    uint16_t dim = 0u;
    ObjPtr<mirror::Class> k = klass;
    if (klass->IsArrayClass()) {
      DCHECK_EQ(klass->NumMethods(), 0u);  // No methods to collect.
      if (!ShouldCollectClasses(startup)) {
        return true;
      }
      do {
        DCHECK(k->IsResolved());  // Array classes are always resolved.
        ++dim;
        // At the time of array class creation, the element type is already either
        // resolved or erroneous unresoved and either shall remain an invariant.
        // Similarly, the access flag indicating a proxy class is an invariant.
        // Read barrier is unnecessary for reading a chain of constant references
        // in order to read primitive fields to check such invariants, or to read
        // other constant primitive fields (dex file, primitive type) below.
        k = k->GetComponentType<kDefaultVerifyFlags, kWithoutReadBarrier>();
      } while (k->IsArrayClass());

      DCHECK(kBootClassLoader || !k->IsPrimitive());
      if (kBootClassLoader && UNLIKELY(k->IsPrimitive())) {
        size_t index = enum_cast<size_t>(k->GetPrimitiveType());
        DCHECK_LT(index, max_primitive_array_dimensions_.size());
        if (dim > max_primitive_array_dimensions_[index]) {
          // Enforce an upper limit of 255 for primitive array dimensions.
          max_primitive_array_dimensions_[index] =
              std::min<size_t>(dim, std::numeric_limits<uint8_t>::max());
        }
        return true;
      }

      DCHECK(klass->GetMethodsPtr() == nullptr);
    } else {
      // Non-array class. There is no need to collect primitive types.
      DCHECK(kBootClassLoader || !k->IsPrimitive());
      if (kBootClassLoader && UNLIKELY(klass->IsPrimitive())) {
        DCHECK(profile_boot_class_path_);
        DCHECK_EQ(klass->NumMethods(), 0u);  // No methods to collect.
        return true;
      }
    }

    if (!k->IsResolved() || k->IsProxyClass()) {
      return true;
    }

    const DexFile& dex_file = k->GetDexFile();
    dex::TypeIndex type_index = k->GetDexTypeIndex();
    LengthPrefixedArray<ArtMethod>* methods = klass->GetMethodsPtr();
    DexFileRecords* dex_file_records;
    auto it = dex_file_records_map_.find(&dex_file);
    if (it != dex_file_records_map_.end()) {
      dex_file_records = it->second;
    } else {
      dex_file_records = new (&allocator_) DexFileRecords(&allocator_);
      dex_file_records_map_.insert(std::make_pair(&dex_file, dex_file_records));
    }
    dex_file_records->class_records.push_back(ClassRecord{type_index, dim, methods});
    return true;
  });
}

void ProfileSaver::GetClassesAndMethodsHelper::CollectClasses(Thread* self) {
  ScopedTrace trace(__PRETTY_FUNCTION__);

  // Collect class loaders into a `VariableSizedHandleScope` to prevent contention
  // problems on the class_linker_classes_lock. Hold those class loaders in
  // a member variable to keep them alive and prevent unloading their classes,
  // so that methods referenced in collected `DexFileRecords` remain valid.
  class_loaders_.emplace(self);
  Runtime::Current()->GetClassLinker()->GetClassLoaders(self, &class_loaders_.value());

  // Collect classes and their method array pointers.
  if (profile_boot_class_path_) {
    // Collect classes from the boot class loader since visit classloaders doesn't visit it.
    CollectInternal</*kBootClassLoader=*/ true>(/*class_loader=*/ nullptr);
  }
  {
    CollectInternalVisitor visitor(this);
    class_loaders_->VisitRoots(visitor);
  }

  // Attribute copied methods to defining dex files while holding the mutator lock.
  for (const auto& entry : dex_file_records_map_) {
    const DexFile* dex_file = entry.first;
    DexFileRecords* dex_file_records = entry.second;

    for (const ClassRecord& class_record : dex_file_records->class_records) {
      LengthPrefixedArray<ArtMethod>* methods = class_record.methods;
      if (methods == nullptr) {
        continue;
      }
      const size_t methods_size = methods->size();
      for (size_t index = methods_size; index != 0u; --index) {
        // Note: Using `ArtMethod` array with implicit `kRuntimePointerSize`.
        ArtMethod& method = methods->At(index - 1);
        if (!method.IsCopied()) {
          break;
        }
        CHECK(!method.IsNative()) << dex_file->PrettyType(class_record.type_index);
        if (method.IsInvokable()) {
          const DexFile* method_dex_file = method.GetDexFile();
          DexFileRecords* method_dex_file_records = dex_file_records;
          if (method_dex_file != dex_file) {
            auto it = dex_file_records_map_.find(method_dex_file);
            if (it == dex_file_records_map_.end()) {
              // We have not seen any class in the dex file that defines the interface with this
              // copied method. This can happen if the interface is in the boot class path and
              // we are not profiling boot class path; or when we first visit classes for the
              // interface's defining class loader before it has any resolved classes and then
              // the interface is resolved and an implementing class is defined in a child class
              // loader before we visit that child class loader's classes.
              continue;
            }
            method_dex_file_records = it->second;
          }
          method_dex_file_records->copied_methods.push_back(&method);
        }
      }
    }
  }
}

void ProfileSaver::GetClassesAndMethodsHelper::UpdateProfile(const std::set<std::string>& locations,
                                                             ProfileCompilationInfo* profile_info) {
  // Move members to local variables to allow the compiler to optimize this properly.
  const bool startup = startup_;
  const uint32_t base_flags =
      (startup ? Hotness::kFlagStartup : Hotness::kFlagPostStartup) | extra_flags_;

  // Collect the number of hot and sampled methods.
  size_t number_of_hot_methods = 0u;
  size_t number_of_sampled_methods = 0u;

  uint16_t initial_value = Runtime::Current()->GetJITOptions()->GetWarmupThreshold();
  auto get_method_flags = [&](ArtMethod& method) {
    // Mark methods as hot if they are marked as such (warm for the runtime
    // means hot for the profile).
    if (method.PreviouslyWarm()) {
      ++number_of_hot_methods;
      return enum_cast<ProfileCompilationInfo::MethodHotness::Flag>(base_flags | Hotness::kFlagHot);
    } else if (method.CounterHasChanged(initial_value)) {
      ++number_of_sampled_methods;
      return enum_cast<ProfileCompilationInfo::MethodHotness::Flag>(base_flags);
    } else {
      return enum_cast<ProfileCompilationInfo::MethodHotness::Flag>(0u);
    }
  };

  // Use a single string for array descriptors to avoid too many reallocations.
  std::string array_class_descriptor;

  // Process classes and methods.
  for (const auto& entry : dex_file_records_map_) {
    const DexFile* dex_file = entry.first;
    const DexFileRecords* dex_file_records = entry.second;

    // Check if this is a profiled dex file.
    const std::string base_location = DexFileLoader::GetBaseLocation(dex_file->GetLocation());
    if (locations.find(base_location) == locations.end()) {
      continue;
    }

    // Get the profile index.
    ProfileCompilationInfo::ProfileIndexType profile_index =
        profile_info->FindOrAddDexFile(*dex_file, annotation_);
    if (profile_index == ProfileCompilationInfo::MaxProfileIndex()) {
      // Error adding dex file to the `profile_info`.
      continue;
    }

    for (const ClassRecord& class_record : dex_file_records->class_records) {
      if (class_record.array_dimension != 0u) {
        DCHECK(ShouldCollectClasses(startup));
        DCHECK(class_record.methods == nullptr);  // No methods to process.
        array_class_descriptor.assign(class_record.array_dimension, '[');
        array_class_descriptor += dex_file->GetTypeDescriptorView(class_record.type_index);
        dex::TypeIndex type_index =
            profile_info->FindOrCreateTypeIndex(*dex_file, array_class_descriptor);
        if (type_index.IsValid()) {
          profile_info->AddClass(profile_index, type_index);
        }
      } else {
        // Non-array class.
        if (ShouldCollectClasses(startup)) {
          profile_info->AddClass(profile_index, class_record.type_index);
        }
        LengthPrefixedArray<ArtMethod>* methods = class_record.methods;
        if (methods != nullptr) {
          for (size_t index = 0, size = methods->size(); index != size; ++index) {
            // Note: Using `ArtMethod` array with implicit `kRuntimePointerSize`.
            ArtMethod& method = methods->At(index);
            if (method.IsCopied()) {
              break;
            }
            // We do not record native methods. Once we AOT-compile the app,
            // all native methods shall have their JNI stubs compiled.
            if (method.IsInvokable() && !method.IsNative()) {
              ProfileCompilationInfo::MethodHotness::Flag flags = get_method_flags(method);
              if (flags != 0u) {
                profile_info->AddMethod(profile_index, method.GetDexMethodIndex(), flags);
              }
            }
          }
        }
      }
    }

    for (ArtMethod* method : dex_file_records->copied_methods) {
      DCHECK(method->IsCopied());
      DCHECK(method->IsInvokable());
      DCHECK(!method->IsNative());
      ProfileCompilationInfo::MethodHotness::Flag flags = get_method_flags(*method);
      if (flags != 0u) {
        profile_info->AddMethod(profile_index, method->GetDexMethodIndex(), flags);
      }
    }
  }

  if (profile_boot_class_path_) {
    // Attribute primitive arrays to the first dex file in the boot class path (should
    // be core-oj). We collect primitive array types to know the needed dimensions.
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    DCHECK(!class_linker->GetBootClassPath().empty());
    const DexFile* dex_file = class_linker->GetBootClassPath().front();
    ProfileCompilationInfo::ProfileIndexType profile_index =
        profile_info->FindOrAddDexFile(*dex_file, annotation_);
    if (profile_index != ProfileCompilationInfo::MaxProfileIndex()) {
      for (size_t i = 0; i != max_primitive_array_dimensions_.size(); ++i) {
        size_t max_dim = max_primitive_array_dimensions_[i];
        // Insert descriptors for all dimensions up to `max_dim`.
        for (size_t dim = 1; dim <= max_dim; ++dim) {
          array_class_descriptor.assign(dim, '[');
          array_class_descriptor += Primitive::Descriptor(enum_cast<Primitive::Type>(i));
          dex::TypeIndex type_index =
              profile_info->FindOrCreateTypeIndex(*dex_file, array_class_descriptor);
          if (type_index.IsValid()) {
            profile_info->AddClass(profile_index, type_index);
          }
        }
      }
    } else {
      // Error adding dex file to the `profile_info`.
    }
  } else {
    DCHECK(std::all_of(max_primitive_array_dimensions_.begin(),
                       max_primitive_array_dimensions_.end(),
                       [](uint8_t dim) { return dim == 0u; }));
  }

  // Store the number of hot and sampled methods.
  number_of_hot_methods_ = number_of_hot_methods;
  number_of_sampled_methods_ = number_of_sampled_methods;
}

void ProfileSaver::FetchAndCacheResolvedClassesAndMethods(bool startup) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  const uint64_t start_time = NanoTime();

  // Resolve any new registered locations.
  ResolveTrackedLocations();

  Thread* const self = Thread::Current();
  pthread_t profiler_pthread;
  {
    MutexLock mu(self, *Locks::profiler_lock_);
    profiler_pthread = profiler_pthread_;
  }

  size_t number_of_hot_methods = 0u;
  size_t number_of_sampled_methods = 0u;
  {
    // Restore profile saver thread priority while holding the mutator lock. This helps
    // prevent priority inversions blocking the GC for long periods of time.
    // Only restore default priority if we are the profile saver thread. Other threads
    // that call this are threads calling Stop and the signal catcher (for SIGUSR1).
    std::optional<ScopedDefaultPriority> sdp = std::nullopt;
    if (pthread_self() == profiler_pthread) {
      sdp.emplace(profiler_pthread);
    }

    ScopedObjectAccess soa(self);
    GetClassesAndMethodsHelper helper(startup, options_, GetProfileSampleAnnotation());
    helper.CollectClasses(self);

    // Release the mutator lock. We shall need to re-acquire the lock for a moment to
    // destroy the `VariableSizedHandleScope` inside the `helper` which shall be
    // conveniently handled by destroying `sts`, then `helper` and then `soa`.
    ScopedThreadSuspension sts(self, ThreadState::kNative);
    // Get back to the previous thread priority. We shall not increase the priority
    // for the short time we need to re-acquire mutator lock for `helper` destructor.
    sdp.reset();

    MutexLock mu(self, *Locks::profiler_lock_);
    for (const auto& it : tracked_dex_base_locations_) {
      const std::string& filename = it.first;
      auto info_it = profile_cache_.find(filename);
      if (info_it == profile_cache_.end()) {
        info_it = profile_cache_.Put(
            filename,
            new ProfileCompilationInfo(
                Runtime::Current()->GetArenaPool(), options_.GetProfileBootClassPath()));
      }
      ProfileCompilationInfo* cached_info = info_it->second;

      const std::set<std::string>& locations = it.second;
      VLOG(profiler) << "Locations for " << it.first << " " << android::base::Join(locations, ':');
      helper.UpdateProfile(locations, cached_info);

      // Update statistics. Note that a method shall be counted for each
      // tracked location that covers the dex file where it is defined.
      number_of_hot_methods += helper.GetNumberOfHotMethods();
      number_of_sampled_methods += helper.GetNumberOfSampledMethods();
    }
  }
  VLOG(profiler) << "Profile saver recorded " << number_of_hot_methods
                 << " hot methods and " << number_of_sampled_methods
                 << " sampled methods in " << PrettyDuration(NanoTime() - start_time);
}

bool ProfileSaver::ProcessProfilingInfo(bool force_save, /*out*/uint16_t* number_of_new_methods) {
  ScopedTrace trace(__PRETTY_FUNCTION__);

  // Resolve any new registered locations.
  ResolveTrackedLocations();

  std::vector<std::pair<std::string, std::set<std::string>>> tracked_locations;
  SafeMap<std::string, AppInfo::CodeType> profile_to_code_type;
  {
    // Make a copy so that we don't hold the lock while doing I/O.
    MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
    tracked_locations.assign(tracked_dex_base_locations_.begin(),
                             tracked_dex_base_locations_.end());
    for (const auto& [key, value] : tracked_profiles_) {
      profile_to_code_type.Put(key, value.second);
    }
  }

  // Put "primary.prof" at the end. `artd` relies on the fact that "primary.prof" is the last one to
  // write when it waits for a profile save to be done.
  std::sort(tracked_locations.begin(),
            tracked_locations.end(),
            [&](const auto& pair1, const auto& pair2) {
              return profile_to_code_type.Get(pair1.first) != AppInfo::CodeType::kPrimaryApk &&
                     profile_to_code_type.Get(pair2.first) == AppInfo::CodeType::kPrimaryApk;
            });

  bool profile_file_saved = false;
  if (number_of_new_methods != nullptr) {
    *number_of_new_methods = 0;
  }

  FetchAndCacheResolvedClassesAndMethods(/*startup=*/ false);

  for (const auto& it : tracked_locations) {
    if (!force_save && ShuttingDown(Thread::Current())) {
      // The ProfileSaver is in shutdown mode, meaning a stop request was made and
      // we need to exit cleanly (by waiting for the saver thread to finish). Unless
      // we have a request for a forced save, do not do any processing so that we
      // speed up the exit.
      return true;
    }
    const std::string& filename = it.first;
    const std::set<std::string>& locations = it.second;
    VLOG(profiler) << "Tracked filename " << filename << " locations "
                   << android::base::Join(locations, ":");

    std::vector<ProfileMethodInfo> profile_methods;
    {
      ScopedObjectAccess soa(Thread::Current());
      jit_code_cache_->GetProfiledMethods(
          locations, profile_methods, options_.GetInlineCacheThreshold());
      total_number_of_code_cache_queries_++;
    }
    {
      ProfileCompilationInfo info(Runtime::Current()->GetArenaPool(),
                                  /*for_boot_image=*/options_.GetProfileBootClassPath());
      // Load the existing profile before saving.
      // If the file is updated between `Load` and `Save`, the update will be lost. This is
      // acceptable. The main reason is that the lost entries will eventually come back if the user
      // keeps using the same methods, or they won't be needed if the user doesn't use the same
      // methods again.
      if (!info.Load(filename, /*clear_if_invalid=*/true)) {
        LOG(WARNING) << "Could not forcefully load profile " << filename;
        continue;
      }

      uint64_t last_save_number_of_methods = info.GetNumberOfMethods();
      uint64_t last_save_number_of_classes = info.GetNumberOfResolvedClasses();
      VLOG(profiler) << "last_save_number_of_methods=" << last_save_number_of_methods
                     << " last_save_number_of_classes=" << last_save_number_of_classes
                     << " number of profiled methods=" << profile_methods.size();

      // Try to add the method data. Note this may fail is the profile loaded from disk contains
      // outdated data (e.g. the previous profiled dex files might have been updated).
      // If this happens we clear the profile data and for the save to ensure the file is cleared.
      if (!info.AddMethods(
              profile_methods,
              AnnotateSampleFlags(Hotness::kFlagHot | Hotness::kFlagPostStartup),
              GetProfileSampleAnnotation())) {
        LOG(WARNING) << "Could not add methods to the existing profiler. "
            << "Clearing the profile data.";
        info.ClearData();
        force_save = true;
      }

      {
        MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
        auto profile_cache_it = profile_cache_.find(filename);
        if (profile_cache_it != profile_cache_.end()) {
          if (!info.MergeWith(*(profile_cache_it->second))) {
            LOG(WARNING) << "Could not merge the profile. Clearing the profile data.";
            info.ClearData();
            force_save = true;
          }
        } else if (VLOG_IS_ON(profiler)) {
          LOG(INFO) << "Failed to find cached profile for " << filename;
          for (auto&& pair : profile_cache_) {
            LOG(INFO) << "Cached profile " << pair.first;
          }
        }

        int64_t delta_number_of_methods =
            info.GetNumberOfMethods() - last_save_number_of_methods;
        int64_t delta_number_of_classes =
            info.GetNumberOfResolvedClasses() - last_save_number_of_classes;

        // Always write on a forced save. `artd` relies on the fact that profiles are always
        // written when it waits for a forced profile save to be done.
        if (!force_save &&
            delta_number_of_methods < options_.GetMinMethodsToSave() &&
            delta_number_of_classes < options_.GetMinClassesToSave()) {
          VLOG(profiler) << "Not enough information to save to: " << filename
                        << " Number of methods: " << delta_number_of_methods
                        << " Number of classes: " << delta_number_of_classes;
          total_number_of_skipped_writes_++;
          continue;
        }

        if (number_of_new_methods != nullptr) {
          *number_of_new_methods =
              std::max(static_cast<uint16_t>(delta_number_of_methods),
                      *number_of_new_methods);
        }
        uint64_t bytes_written;
        // Force the save. In case the profile data is corrupted or the profile
        // has the wrong version this will "fix" the file to the correct format.
        if (info.Save(filename, &bytes_written, force_save)) {
          // We managed to save the profile. Clear the cache stored during startup.
          if (profile_cache_it != profile_cache_.end()) {
            ProfileCompilationInfo *cached_info = profile_cache_it->second;
            profile_cache_.erase(profile_cache_it);
            delete cached_info;
          }
          if (bytes_written > 0) {
            total_number_of_writes_++;
            total_bytes_written_ += bytes_written;
            profile_file_saved = true;
          } else {
            // At this point we could still have avoided the write.
            // We load and merge the data from the file lazily at its first ever
            // save attempt. So, whatever we are trying to save could already be
            // in the file.
            total_number_of_skipped_writes_++;
          }
        } else {
          LOG(WARNING) << "Could not save profiling info to " << filename;
          total_number_of_failed_writes_++;
        }
      }
    }
  }

  // Trim the maps to madvise the pages used for profile info.
  // It is unlikely we will need them again in the near feature.
  Runtime::Current()->GetArenaPool()->TrimMaps();

  return profile_file_saved;
}

void* ProfileSaver::RunProfileSaverThread(void* arg) {
  Runtime* runtime = Runtime::Current();

  bool attached = runtime->AttachCurrentThread("Profile Saver",
                                               /*as_daemon=*/true,
                                               runtime->GetSystemThreadGroup(),
                                               /*create_peer=*/true);
  if (!attached) {
    CHECK(runtime->IsShuttingDown(Thread::Current()));
    return nullptr;
  }

  {
    Locks::profiler_lock_->ExclusiveLock(Thread::Current());
    CHECK_EQ(reinterpret_cast<ProfileSaver*>(arg), instance_);
    instance_->Run();
  }

  runtime->DetachCurrentThread();
  VLOG(profiler) << "Profile saver shutdown";
  return nullptr;
}

static bool ShouldProfileLocation(const std::string& location, bool profile_aot_code) {
  if (profile_aot_code) {
    // If we have to profile all the code, irrespective of its compilation state, return true
    // right away.
    return true;
  }

  OatFileManager& oat_manager = Runtime::Current()->GetOatFileManager();
  const OatFile* oat_file = oat_manager.FindOpenedOatFileFromDexLocation(location);
  if (oat_file == nullptr) {
    // This can happen if we fallback to run code directly from the APK.
    // Profile it with the hope that the background dexopt will get us back into
    // a good state.
    VLOG(profiler) << "Asked to profile a location without an oat file:" << location;
    return true;
  }
  CompilerFilter::Filter filter = oat_file->GetCompilerFilter();
  if ((filter == CompilerFilter::kSpeed) || (filter == CompilerFilter::kEverything)) {
    VLOG(profiler)
        << "Skip profiling oat file because it's already speed|everything compiled: "
        << location << " oat location: " << oat_file->GetLocation();
    return false;
  }
  return true;
}

void ProfileSaver::Start(const ProfileSaverOptions& options,
                         const std::string& output_filename,
                         jit::JitCodeCache* jit_code_cache,
                         const std::vector<std::string>& code_paths,
                         const std::string& ref_profile_filename,
                         AppInfo::CodeType code_type) {
  Runtime* const runtime = Runtime::Current();
  DCHECK(options.IsEnabled());
  DCHECK(runtime->GetJit() != nullptr);
  DCHECK(!output_filename.empty());
  DCHECK(jit_code_cache != nullptr);

  std::vector<std::string> code_paths_to_profile;
  for (const std::string& location : code_paths) {
    if (ShouldProfileLocation(location, options.GetProfileAOTCode()))  {
      VLOG(profiler) << "Code path to profile " << location;
      code_paths_to_profile.push_back(location);
    }
  }

  MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
  // Support getting profile samples for the boot class path. This will be used to generate the boot
  // image profile. The intention is to use this code to generate to boot image but not use it in
  // production. b/37966211
  if (options.GetProfileBootClassPath()) {
    std::set<std::string> code_paths_keys;
    for (const std::string& location : code_paths) {
      // Use the profile base key for checking file uniqueness (as it is constructed solely based
      // on the location and ignores other metadata like origin package).
      code_paths_keys.insert(ProfileCompilationInfo::GetProfileDexFileBaseKey(location));
    }
    for (const DexFile* dex_file : runtime->GetClassLinker()->GetBootClassPath()) {
      // Don't check ShouldProfileLocation since the boot class path may be speed compiled.
      const std::string& location = dex_file->GetLocation();
      const std::string key = ProfileCompilationInfo::GetProfileDexFileBaseKey(location);
      VLOG(profiler) << "Registering boot dex file " << location;
      if (code_paths_keys.find(key) != code_paths_keys.end()) {
        LOG(WARNING) << "Boot class path location key conflicts with code path " << location;
      } else if (instance_ == nullptr) {
        // Only add the boot class path once since Start may be called multiple times for secondary
        // dexes.
        // We still do the collision check above. This handles any secondary dexes that conflict
        // with the boot class path dex files.
        code_paths_to_profile.push_back(location);
      }
    }
  }
  if (code_paths_to_profile.empty()) {
    VLOG(profiler) << "No code paths should be profiled.";
    return;
  }

  if (instance_ != nullptr) {
    // If we already have an instance, make sure it uses the same jit_code_cache.
    // This may be called multiple times via Runtime::registerAppInfo (e.g. for
    // apps which share the same runtime).
    DCHECK_EQ(instance_->jit_code_cache_, jit_code_cache);
    // Add the code_paths to the tracked locations.
    instance_->AddTrackedLocations(
        output_filename, code_paths_to_profile, ref_profile_filename, code_type);
    return;
  }

  VLOG(profiler) << "Starting profile saver using output file: " << output_filename
      << ". Tracking: " << android::base::Join(code_paths_to_profile, ':')
      << ". With reference profile: " << ref_profile_filename;

  instance_ = new ProfileSaver(options, jit_code_cache);
  instance_->AddTrackedLocations(
      output_filename, code_paths_to_profile, ref_profile_filename, code_type);

  // Create a new thread which does the saving.
  CHECK_PTHREAD_CALL(
      pthread_create,
      (&profiler_pthread_, nullptr, &RunProfileSaverThread, reinterpret_cast<void*>(instance_)),
      "Profile saver thread");

  SetProfileSaverThreadPriority(profiler_pthread_, kProfileSaverPthreadPriority);
}

void ProfileSaver::Stop(bool dump_info) {
  ProfileSaver* profile_saver = nullptr;
  pthread_t profiler_pthread = 0U;

  {
    MutexLock profiler_mutex(Thread::Current(), *Locks::profiler_lock_);
    VLOG(profiler) << "Stopping profile saver thread";
    profile_saver = instance_;
    profiler_pthread = profiler_pthread_;
    if (instance_ == nullptr) {
      DCHECK(false) << "Tried to stop a profile saver which was not started";
      return;
    }
    if (instance_->shutting_down_) {
      DCHECK(false) << "Tried to stop the profile saver twice";
      return;
    }
    instance_->shutting_down_ = true;
  }

  {
    // Wake up the saver thread if it is sleeping to allow for a clean exit.
    MutexLock wait_mutex(Thread::Current(), profile_saver->wait_lock_);
    profile_saver->period_condition_.Signal(Thread::Current());
  }

  // Force save everything before destroying the thread since we want profiler_pthread_ to remain
  // valid.
  profile_saver->ProcessProfilingInfo(/*force_ save=*/ true, /*number_of_new_methods=*/ nullptr);

  // Wait for the saver thread to stop.
  CHECK_PTHREAD_CALL(pthread_join, (profiler_pthread, nullptr), "profile saver thread shutdown");

  {
    MutexLock profiler_mutex(Thread::Current(), *Locks::profiler_lock_);
    if (dump_info) {
      instance_->DumpInfo(LOG_STREAM(INFO));
    }
    instance_ = nullptr;
    profiler_pthread_ = 0U;
  }
  delete profile_saver;
}

bool ProfileSaver::ShuttingDown(Thread* self) {
  MutexLock mu(self, *Locks::profiler_lock_);
  return shutting_down_;
}

bool ProfileSaver::IsStarted() {
  MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
  return instance_ != nullptr;
}

static void AddTrackedLocationsToMap(const std::string& output_filename,
                                     const std::vector<std::string>& code_paths,
                                     SafeMap<std::string, std::set<std::string>>* map) {
  std::vector<std::string> code_paths_and_filenames;
  // The dex locations are sometimes set to the filename instead of the full path.
  // So make sure we have both "locations" when tracking what needs to be profiled.
  //   - apps + system server have filenames
  //   - boot classpath elements have full paths

  // TODO(calin, ngeoffray, vmarko) This is an workaround for using filanames as
  // dex locations - needed to prebuilt with a partial boot image
  // (commit: c4a924d8c74241057d957d360bf31cd5cd0e4f9c).
  // We should find a better way which allows us to do the tracking based on full paths.
  for (const std::string& path : code_paths) {
    size_t last_sep_index = path.find_last_of('/');
    if (path.empty() || last_sep_index == path.size() - 1) {
      // Should not happen, but anyone can register code paths so better be prepared and ignore
      // such locations.
      continue;
    }
    std::string filename = last_sep_index == std::string::npos
        ? path
        : path.substr(last_sep_index + 1);

    code_paths_and_filenames.push_back(path);
    code_paths_and_filenames.push_back(filename);
  }

  auto it = map->FindOrAdd(output_filename);
  it->second.insert(code_paths_and_filenames.begin(), code_paths_and_filenames.end());
}

void ProfileSaver::AddTrackedLocations(const std::string& output_filename,
                                       const std::vector<std::string>& code_paths,
                                       const std::string& ref_profile_filename,
                                       AppInfo::CodeType code_type) {
  // Register the output profile and its reference profile.
  auto it = tracked_profiles_.find(output_filename);
  if (it == tracked_profiles_.end()) {
    tracked_profiles_.Put(output_filename, std::make_pair(ref_profile_filename, code_type));
  }

  // Add the code paths to the list of tracked location.
  AddTrackedLocationsToMap(output_filename, code_paths, &tracked_dex_base_locations_);
  // The code paths may contain symlinks which could fool the profiler.
  // If the dex file is compiled with an absolute location but loaded with symlink
  // the profiler could skip the dex due to location mismatch.
  // To avoid this, we add the code paths to the temporary cache of 'to_be_resolved'
  // locations. When the profiler thread executes we will resolve the paths to their
  // real paths.
  // Note that we delay taking the realpath to avoid spending more time than needed
  // when registering location (as it is done during app launch).
  AddTrackedLocationsToMap(output_filename,
                           code_paths,
                           &tracked_dex_base_locations_to_be_resolved_);
}

void ProfileSaver::DumpInstanceInfo(std::ostream& os) {
  MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
  if (instance_ != nullptr) {
    instance_->DumpInfo(os);
  }
}

void ProfileSaver::DumpInfo(std::ostream& os) {
  os << "ProfileSaver total_bytes_written=" << total_bytes_written_ << '\n'
     << "ProfileSaver total_number_of_writes=" << total_number_of_writes_ << '\n'
     << "ProfileSaver total_number_of_code_cache_queries="
     << total_number_of_code_cache_queries_ << '\n'
     << "ProfileSaver total_number_of_skipped_writes=" << total_number_of_skipped_writes_ << '\n'
     << "ProfileSaver total_number_of_failed_writes=" << total_number_of_failed_writes_ << '\n'
     << "ProfileSaver total_ms_of_sleep=" << total_ms_of_sleep_ << '\n'
     << "ProfileSaver total_ms_of_work=" << NsToMs(total_ns_of_work_) << '\n'
     << "ProfileSaver total_number_of_hot_spikes=" << total_number_of_hot_spikes_ << '\n'
     << "ProfileSaver total_number_of_wake_ups=" << total_number_of_wake_ups_ << '\n';
}


void ProfileSaver::ForceProcessProfiles() {
  ProfileSaver* saver = nullptr;
  {
    MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
    saver = instance_;
  }
  // TODO(calin): this is not actually thread safe as the instance_ may have been deleted,
  // but we only use this in testing when we now this won't happen.
  // Refactor the way we handle the instance so that we don't end up in this situation.
  if (saver != nullptr) {
    saver->ProcessProfilingInfo(/*force_save=*/ true, /*number_of_new_methods=*/ nullptr);
  }
}

void ProfileSaver::ResolveTrackedLocations() {
  SafeMap<std::string, std::set<std::string>> locations_to_be_resolved;
  {
    // Make a copy so that we don't hold the lock while doing I/O.
    MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
    locations_to_be_resolved = tracked_dex_base_locations_to_be_resolved_;
    tracked_dex_base_locations_to_be_resolved_.clear();
  }

  // Resolve the locations.
  SafeMap<std::string, std::vector<std::string>> resolved_locations_map;
  for (const auto& it : locations_to_be_resolved) {
    const std::string& filename = it.first;
    const std::set<std::string>& locations = it.second;
    auto resolved_locations_it = resolved_locations_map.Put(filename, std::vector<std::string>());
    resolved_locations_it->second.reserve(locations.size());

    for (const auto& location : locations) {
      UniqueCPtr<const char[]> location_real(realpath(location.c_str(), nullptr));
      // Note that it's ok if we cannot get the real path.
      if (location_real != nullptr) {
        resolved_locations_it->second.emplace_back(location_real.get());
      }
    }
  }

  // Add the resolved locations to the tracked collection.
  MutexLock mu(Thread::Current(), *Locks::profiler_lock_);
  for (const auto& it : resolved_locations_map) {
    AddTrackedLocationsToMap(it.first, it.second, &tracked_dex_base_locations_);
  }
}

ProfileCompilationInfo::ProfileSampleAnnotation ProfileSaver::GetProfileSampleAnnotation() {
  // Ideally, this would be cached in the ProfileSaver class, when we start the thread.
  // However the profile is initialized before the process package name is set and fixing this
  // would require unnecessary complex synchronizations.
  std::string package_name = Runtime::Current()->GetProcessPackageName();
  if (package_name.empty()) {
    package_name = "unknown";
  }
  // We only use annotation for the boot image profiles. Regular apps do not use the extra
  // metadata and as such there is no need to pay the cost (storage and computational)
  // that comes with the annotations.
  return options_.GetProfileBootClassPath()
      ? ProfileCompilationInfo::ProfileSampleAnnotation(package_name)
      : ProfileCompilationInfo::ProfileSampleAnnotation::kNone;
}

uint32_t ProfileSaver::GetExtraMethodHotnessFlags(const ProfileSaverOptions& options) {
  // We only add the extra flags for the boot image profile because individual apps do not use
  // this information.
  if (options.GetProfileBootClassPath()) {
    return Is64BitInstructionSet(Runtime::Current()->GetInstructionSet())
        ? Hotness::kFlag64bit
        : Hotness::kFlag32bit;
  } else {
    return 0u;
  }
}

Hotness::Flag ProfileSaver::AnnotateSampleFlags(uint32_t flags) {
  uint32_t extra_flags = GetExtraMethodHotnessFlags(options_);
  return static_cast<Hotness::Flag>(flags | extra_flags);
}

}   // namespace art
