/*
 * Copyright 2021 The Android Open Source Project
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

#include <fcntl.h>
// Glibc v2.19 doesn't include these in fcntl.h so host builds will fail without.
#if !defined(FALLOC_FL_PUNCH_HOLE) || !defined(FALLOC_FL_KEEP_SIZE)
#include <linux/falloc.h>
#endif
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fstream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "android-base/file.h"
#include "android-base/parsebool.h"
#include "android-base/parseint.h"
#include "android-base/properties.h"
#include "android-base/strings.h"
#include "base/file_utils.h"
#include "base/memfd.h"
#include "base/quasi_atomic.h"
#include "base/systrace.h"
#include "base/utils.h"
#include "gc/accounting/mod_union_table-inl.h"
#include "gc/collector_type.h"
#include "gc/reference_processor.h"
#include "gc/space/bump_pointer_space.h"
#include "gc/space/space-inl.h"
#include "gc/task_processor.h"
#include "gc/verification-inl.h"
#include "jit/jit_code_cache.h"
#include "mark_compact-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "read_barrier_config.h"
#include "scoped_thread_state_change-inl.h"
#include "sigchain.h"
#include "thread_list.h"

#ifdef ART_TARGET_ANDROID
#include "android-modules-utils/sdk_level.h"
#include "com_android_art.h"
#include "com_android_art_flags.h"
#endif

// See aosp/2996596 for where these values came from.
#ifndef UFFDIO_COPY_MODE_MMAP_TRYLOCK
#define UFFDIO_COPY_MODE_MMAP_TRYLOCK (static_cast<uint64_t>(1) << 63)
#endif
#ifndef UFFDIO_ZEROPAGE_MODE_MMAP_TRYLOCK
#define UFFDIO_ZEROPAGE_MODE_MMAP_TRYLOCK (static_cast<uint64_t>(1) << 63)
#endif
#ifdef ART_TARGET_ANDROID
namespace {

using ::android::base::GetBoolProperty;
using ::android::base::ParseBool;
using ::android::base::ParseBoolResult;
using ::android::modules::sdklevel::IsAtLeastV;

}  // namespace
#endif

namespace art HIDDEN {

static bool HaveMremapDontunmap() {
  const size_t page_size = GetPageSizeSlow();
  void* old = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  CHECK_NE(old, MAP_FAILED);
  void* addr = mremap(old, page_size, page_size, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, nullptr);
  CHECK_EQ(munmap(old, page_size), 0);
  if (addr != MAP_FAILED) {
    CHECK_EQ(munmap(addr, page_size), 0);
    return true;
  } else {
    return false;
  }
}

static bool gUffdSupportsMmapTrylock = false;
// We require MREMAP_DONTUNMAP functionality of the mremap syscall, which was
// introduced in 5.13 kernel version. But it was backported to GKI kernels.
static bool gHaveMremapDontunmap = IsKernelVersionAtLeast(5, 13) || HaveMremapDontunmap();
// Bitmap of features supported by userfaultfd. This is obtained via uffd API ioctl.
static uint64_t gUffdFeatures = 0;
// Both, missing and minor faults on shmem are needed only for minor-fault mode.
static constexpr uint64_t kUffdFeaturesForMinorFault =
    UFFD_FEATURE_MISSING_SHMEM | UFFD_FEATURE_MINOR_SHMEM;
static constexpr uint64_t kUffdFeaturesForSigbus = UFFD_FEATURE_SIGBUS;
// A region which is more than kBlackDenseRegionThreshold percent live doesn't
// need to be compacted as it is too densely packed.
static constexpr uint kBlackDenseRegionThreshold = 95U;
// We consider SIGBUS feature necessary to enable this GC as it's superior than
// threading-based implementation for janks. We may want minor-fault in future
// to be available for making jit-code-cache updation concurrent, which uses shmem.
bool KernelSupportsUffd() {
#ifdef __linux__
  if (gHaveMremapDontunmap) {
    int fd = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
    // On non-android devices we may not have the kernel patches that restrict
    // userfaultfd to user mode. But that is not a security concern as we are
    // on host. Therefore, attempt one more time without UFFD_USER_MODE_ONLY.
    if (!kIsTargetAndroid && fd == -1 && errno == EINVAL) {
      fd = syscall(__NR_userfaultfd, O_CLOEXEC);
    }
    if (fd >= 0) {
      // We are only fetching the available features, which is returned by the
      // ioctl.
      struct uffdio_api api = {.api = UFFD_API, .features = 0, .ioctls = 0};
      CHECK_EQ(ioctl(fd, UFFDIO_API, &api), 0) << "ioctl_userfaultfd : API:" << strerror(errno);
      gUffdFeatures = api.features;
      // MMAP_TRYLOCK is available only in 5.10 and 5.15 GKI kernels. The higher
      // versions will have per-vma locks. The lower ones don't support
      // userfaultfd.
      if (kIsTargetAndroid && !IsKernelVersionAtLeast(5, 16)) {
        // Check if MMAP_TRYLOCK feature is supported
        const size_t page_size = GetPageSizeSlow();
        void* mem =
            mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        CHECK_NE(mem, MAP_FAILED) << " errno: " << errno;

        struct uffdio_zeropage uffd_zeropage;
        uffd_zeropage.mode = UFFDIO_ZEROPAGE_MODE_MMAP_TRYLOCK;
        uffd_zeropage.range.start = reinterpret_cast<uintptr_t>(mem);
        uffd_zeropage.range.len = page_size;
        uffd_zeropage.zeropage = 0;
        // The ioctl will definitely fail as mem is not registered with uffd.
        CHECK_EQ(ioctl(fd, UFFDIO_ZEROPAGE, &uffd_zeropage), -1);
        // uffd ioctls return EINVAL for several reasons. We make sure with
        // (proper alignment of 'mem' and 'len') that, before updating
        // uffd_zeropage.zeropage (with error), it fails with EINVAL only if
        // `trylock` isn't available.
        if (uffd_zeropage.zeropage == 0 && errno == EINVAL) {
          LOG(INFO) << "MMAP_TRYLOCK is not supported in uffd addr:" << mem
                    << " page-size:" << page_size;
        } else {
          gUffdSupportsMmapTrylock = true;
          LOG(INFO) << "MMAP_TRYLOCK is supported in uffd errno:" << errno << " addr:" << mem
                    << " size:" << page_size;
        }
        munmap(mem, page_size);
      }
      close(fd);
      // Minimum we need is sigbus feature for using userfaultfd.
      return (api.features & kUffdFeaturesForSigbus) == kUffdFeaturesForSigbus;
    }
  }
#endif
  return false;
}

// The other cases are defined as constexpr in runtime/read_barrier_config.h
#if !defined(ART_FORCE_USE_READ_BARRIER) && defined(ART_USE_READ_BARRIER)
// Returns collector type asked to be used on the cmdline.
static gc::CollectorType FetchCmdlineGcType() {
  std::string argv;
  gc::CollectorType gc_type = gc::CollectorType::kCollectorTypeNone;
  if (android::base::ReadFileToString("/proc/self/cmdline", &argv)) {
    auto pos = argv.rfind("-Xgc:");
    if (argv.substr(pos + 5, 3) == "CMC") {
      gc_type = gc::CollectorType::kCollectorTypeCMC;
    } else if (argv.substr(pos + 5, 2) == "CC") {
      gc_type = gc::CollectorType::kCollectorTypeCC;
    }
  }
  return gc_type;
}

#ifdef ART_TARGET_ANDROID
static int GetOverrideCacheInfoFd() {
  std::string args_str;
  if (!android::base::ReadFileToString("/proc/self/cmdline", &args_str)) {
    LOG(WARNING) << "Failed to load /proc/self/cmdline";
    return -1;
  }
  std::vector<std::string_view> args;
  Split(std::string_view(args_str), /*separator=*/'\0', &args);
  for (std::string_view arg : args) {
    if (android::base::ConsumePrefix(&arg, "--cache-info-fd=")) {  // This is a dex2oat flag.
      int fd;
      if (!android::base::ParseInt(std::string(arg), &fd)) {
        LOG(ERROR) << "Failed to parse --cache-info-fd (value: '" << arg << "')";
        return -1;
      }
      return fd;
    }
  }
  return -1;
}

static std::unordered_map<std::string, std::string> GetCachedProperties() {
  // For simplicity, we don't handle multiple calls because otherwise we would have to reset the fd.
  static bool called = false;
  CHECK(!called) << "GetCachedBoolProperty can be called only once";
  called = true;

  std::string cache_info_contents;
  int fd = GetOverrideCacheInfoFd();
  if (fd >= 0) {
    if (!android::base::ReadFdToString(fd, &cache_info_contents)) {
      PLOG(ERROR) << "Failed to read cache-info from fd " << fd;
      return {};
    }
  } else {
    std::string path = GetApexDataDalvikCacheDirectory(InstructionSet::kNone) + "/cache-info.xml";
    if (!android::base::ReadFileToString(path, &cache_info_contents)) {
      // If the file is not found, then we are in chroot or in a standalone runtime process (e.g.,
      // IncidentHelper), or odsign/odrefresh failed to generate and sign the cache info. There's
      // nothing we can do.
      if (errno != ENOENT) {
        PLOG(ERROR) << "Failed to read cache-info from the default path";
      }
      return {};
    }
  }

  std::optional<com::android::art::CacheInfo> cache_info =
      com::android::art::parse(cache_info_contents.c_str());
  if (!cache_info.has_value()) {
    // This should never happen.
    LOG(ERROR) << "Failed to parse cache-info";
    return {};
  }
  const com::android::art::KeyValuePairList* list = cache_info->getFirstSystemProperties();
  if (list == nullptr) {
    // This should never happen.
    LOG(ERROR) << "Missing system properties from cache-info";
    return {};
  }
  const std::vector<com::android::art::KeyValuePair>& properties = list->getItem();
  std::unordered_map<std::string, std::string> result;
  for (const com::android::art::KeyValuePair& pair : properties) {
    result[pair.getK()] = pair.getV();
  }
  return result;
}

static bool GetCachedBoolProperty(
    const std::unordered_map<std::string, std::string>& cached_properties,
    const std::string& key,
    bool default_value) {
  auto it = cached_properties.find(key);
  if (it == cached_properties.end()) {
    return default_value;
  }
  ParseBoolResult result = ParseBool(it->second);
  switch (result) {
    case ParseBoolResult::kTrue:
      return true;
    case ParseBoolResult::kFalse:
      return false;
    case ParseBoolResult::kError:
      return default_value;
  }
}

static bool SysPropSaysUffdGc() {
  // The phenotype flag can change at time time after boot, but it shouldn't take effect until a
  // reboot. Therefore, we read the phenotype flag from the cache info, which is generated on boot.
  std::unordered_map<std::string, std::string> cached_properties = GetCachedProperties();
  bool phenotype_enable = GetCachedBoolProperty(
      cached_properties, "persist.device_config.runtime_native_boot.enable_uffd_gc_2", false);
  bool phenotype_force_disable = GetCachedBoolProperty(
      cached_properties, "persist.device_config.runtime_native_boot.force_disable_uffd_gc", false);
  bool build_enable = GetBoolProperty("ro.dalvik.vm.enable_uffd_gc", false);
  bool is_at_most_u = !IsAtLeastV();
  return (phenotype_enable || build_enable || is_at_most_u) && !phenotype_force_disable;
}
#else
// Never called.
static bool SysPropSaysUffdGc() { return false; }
#endif

static bool ShouldUseUserfaultfd() {
  static_assert(kUseBakerReadBarrier || kUseTableLookupReadBarrier);
#ifdef __linux__
  // Use CMC/CC if that is being explicitly asked for on cmdline. Otherwise,
  // always use CC on host. On target, use CMC only if system properties says so
  // and the kernel supports it.
  gc::CollectorType gc_type = FetchCmdlineGcType();
  return gc_type == gc::CollectorType::kCollectorTypeCMC ||
         (gc_type == gc::CollectorType::kCollectorTypeNone &&
          kIsTargetAndroid &&
          SysPropSaysUffdGc() &&
          KernelSupportsUffd());
#else
  return false;
#endif
}

const bool gUseUserfaultfd = ShouldUseUserfaultfd();
const bool gUseReadBarrier = !gUseUserfaultfd;
#endif
#ifdef ART_TARGET_ANDROID
bool ShouldUseGenerationalGC() {
  if (gUseUserfaultfd && !com::android::art::flags::use_generational_cmc()) {
    return false;
  }
  // Generational GC feature doesn't need a reboot. Any process (like dex2oat)
  // can pick a different values than zygote and will be able to execute.
  return GetBoolProperty("persist.device_config.runtime_native_boot.use_generational_gc", true);
}
#else
bool ShouldUseGenerationalGC() { return true; }
#endif

namespace gc {
namespace collector {

// Turn off kCheckLocks when profiling the GC as it slows down the GC
// significantly.
static constexpr bool kCheckLocks = kDebugLocking;
static constexpr bool kVerifyRootsMarked = kIsDebugBuild;
// Verify that there are no missing card marks.
static constexpr bool kVerifyNoMissingCardMarks = kIsDebugBuild;
// Verify that all references in post-GC objects are valid.
static constexpr bool kVerifyPostGCObjects = kIsDebugBuild;
// Number of compaction buffers reserved for mutator threads in SIGBUS feature
// case. It's extremely unlikely that we will ever have more than these number
// of mutator threads trying to access the moving-space during one compaction
// phase.
static constexpr size_t kMutatorCompactionBufferCount = 2048;
// Minimum from-space chunk to be madvised (during concurrent compaction) in one go.
// Choose a reasonable size to avoid making too many batched ioctl and madvise calls.
static constexpr ssize_t kMinFromSpaceMadviseSize = 8 * MB;
// Concurrent compaction termination logic is different (and slightly more efficient) if the
// kernel has the fault-retry feature (allowing repeated faults on the same page), which was
// introduced in 5.7 (https://android-review.git.corp.google.com/c/kernel/common/+/1540088).
// This allows a single page fault to be handled, in turn, by each worker thread, only waking
// up the GC thread at the end.
static const bool gKernelHasFaultRetry = IsKernelVersionAtLeast(5, 7);

std::pair<bool, bool> MarkCompact::GetUffdAndMinorFault() {
  bool uffd_available;
  // In most cases the gUffdFeatures will already be initialized at boot time
  // when libart is loaded. On very old kernels we may get '0' from the kernel,
  // in which case we would be doing the syscalls each time this function is
  // called. But that's very unlikely case. There are no correctness issues as
  // the response from kernel never changes after boot.
  if (UNLIKELY(gUffdFeatures == 0)) {
    uffd_available = KernelSupportsUffd();
  } else {
    // We can have any uffd features only if uffd exists.
    uffd_available = true;
  }
  bool minor_fault_available =
      (gUffdFeatures & kUffdFeaturesForMinorFault) == kUffdFeaturesForMinorFault;
  return std::pair<bool, bool>(uffd_available, minor_fault_available);
}

bool MarkCompact::CreateUserfaultfd(bool post_fork) {
  if (post_fork || uffd_ == kFdUnused) {
    // Check if we have MREMAP_DONTUNMAP here for cases where
    // 'ART_USE_READ_BARRIER=false' is used. Additionally, this check ensures
    // that userfaultfd isn't used on old kernels, which cause random ioctl
    // failures.
    if (gHaveMremapDontunmap) {
      // Don't use O_NONBLOCK as we rely on read waiting on uffd_ if there isn't
      // any read event available. We don't use poll.
      uffd_ = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
      // On non-android devices we may not have the kernel patches that restrict
      // userfaultfd to user mode. But that is not a security concern as we are
      // on host. Therefore, attempt one more time without UFFD_USER_MODE_ONLY.
      if (!kIsTargetAndroid && UNLIKELY(uffd_ == -1 && errno == EINVAL)) {
        uffd_ = syscall(__NR_userfaultfd, O_CLOEXEC);
      }
      if (UNLIKELY(uffd_ == -1)) {
        uffd_ = kFallbackMode;
        LOG(WARNING) << "Userfaultfd isn't supported (reason: " << strerror(errno)
                     << ") and therefore falling back to stop-the-world compaction.";
      } else {
        DCHECK(IsValidFd(uffd_));
        // Initialize uffd with the features which are required and available.
        // Using private anonymous mapping in threading mode is the default,
        // for which we don't need to ask for any features. Note: this mode
        // is not used in production.
        struct uffdio_api api = {.api = UFFD_API, .features = 0, .ioctls = 0};
        // We should add SIGBUS feature only if we plan on using it as
        // requesting it here will mean threading mode will not work.
        CHECK_EQ(gUffdFeatures & kUffdFeaturesForSigbus, kUffdFeaturesForSigbus);
        api.features |= kUffdFeaturesForSigbus;
        CHECK_EQ(ioctl(uffd_, UFFDIO_API, &api), 0)
            << "ioctl_userfaultfd: API: " << strerror(errno);
      }
    } else {
      uffd_ = kFallbackMode;
    }
  }
  uffd_initialized_ = !post_fork || uffd_ == kFallbackMode;
  return IsValidFd(uffd_);
}

template <size_t kAlignment>
MarkCompact::LiveWordsBitmap<kAlignment>* MarkCompact::LiveWordsBitmap<kAlignment>::Create(
    uintptr_t begin, uintptr_t end) {
  return static_cast<LiveWordsBitmap<kAlignment>*>(
          MemRangeBitmap::Create("Concurrent Mark Compact live words bitmap", begin, end));
}

size_t MarkCompact::ComputeInfoMapSize() {
  size_t moving_space_size = bump_pointer_space_->Capacity();
  size_t chunk_info_vec_size = moving_space_size / kOffsetChunkSize;
  size_t nr_moving_pages = DivideByPageSize(moving_space_size);
  size_t nr_non_moving_pages = DivideByPageSize(heap_->GetNonMovingSpace()->Capacity());
  return chunk_info_vec_size * sizeof(uint32_t) + nr_non_moving_pages * sizeof(ObjReference) +
         nr_moving_pages * (sizeof(ObjReference) + sizeof(uint32_t) + sizeof(Atomic<uint32_t>));
}

size_t MarkCompact::InitializeInfoMap(uint8_t* p, size_t moving_space_sz) {
  size_t nr_moving_pages = DivideByPageSize(moving_space_sz);

  chunk_info_vec_ = reinterpret_cast<uint32_t*>(p);
  vector_length_ = moving_space_sz / kOffsetChunkSize;
  size_t total = vector_length_ * sizeof(uint32_t);

  first_objs_moving_space_ = reinterpret_cast<ObjReference*>(p + total);
  total += nr_moving_pages * sizeof(ObjReference);

  pre_compact_offset_moving_space_ = reinterpret_cast<uint32_t*>(p + total);
  total += nr_moving_pages * sizeof(uint32_t);

  moving_pages_status_ = reinterpret_cast<Atomic<uint32_t>*>(p + total);
  total += nr_moving_pages * sizeof(Atomic<uint32_t>);

  first_objs_non_moving_space_ = reinterpret_cast<ObjReference*>(p + total);
  total += DivideByPageSize(heap_->GetNonMovingSpace()->Capacity()) * sizeof(ObjReference);
  DCHECK_EQ(total, ComputeInfoMapSize());
  return total;
}

YoungMarkCompact::YoungMarkCompact(Heap* heap, MarkCompact* main)
    : GarbageCollector(heap, "young concurrent mark compact"), main_collector_(main) {
  // Initialize GC metrics.
  metrics::ArtMetrics* metrics = GetMetrics();
  gc_time_histogram_ = metrics->YoungGcCollectionTime();
  metrics_gc_count_ = metrics->YoungGcCount();
  metrics_gc_count_delta_ = metrics->YoungGcCountDelta();
  gc_throughput_histogram_ = metrics->YoungGcThroughput();
  gc_tracing_throughput_hist_ = metrics->YoungGcTracingThroughput();
  gc_throughput_avg_ = metrics->YoungGcThroughputAvg();
  gc_tracing_throughput_avg_ = metrics->YoungGcTracingThroughputAvg();
  gc_scanned_bytes_ = metrics->YoungGcScannedBytes();
  gc_scanned_bytes_delta_ = metrics->YoungGcScannedBytesDelta();
  gc_freed_bytes_ = metrics->YoungGcFreedBytes();
  gc_freed_bytes_delta_ = metrics->YoungGcFreedBytesDelta();
  gc_duration_ = metrics->YoungGcDuration();
  gc_duration_delta_ = metrics->YoungGcDurationDelta();
  gc_app_slow_path_during_gc_duration_delta_ = metrics->AppSlowPathDuringYoungGcDurationDelta();
  are_metrics_initialized_ = true;
}

void YoungMarkCompact::RunPhases() {
  DCHECK(!main_collector_->young_gen_);
  main_collector_->young_gen_ = true;
  main_collector_->RunPhases();
  main_collector_->young_gen_ = false;
}

MarkCompact::MarkCompact(Heap* heap)
    : GarbageCollector(heap, "concurrent mark compact"),
      gc_barrier_(0),
      lock_("mark compact lock", kGenericBottomLock),
      sigbus_in_progress_count_{kSigbusCounterCompactionDoneMask, kSigbusCounterCompactionDoneMask},
      mid_to_old_promo_bit_vec_(nullptr),
      bump_pointer_space_(heap->GetBumpPointerSpace()),
      post_compact_end_(nullptr),
      young_gen_(false),
      use_generational_(heap->GetUseGenerational()),
      compacting_(false),
      moving_space_bitmap_(bump_pointer_space_->GetMarkBitmap()),
      moving_space_begin_(bump_pointer_space_->Begin()),
      moving_space_end_(bump_pointer_space_->Limit()),
      black_dense_end_(moving_space_begin_),
      mid_gen_end_(moving_space_begin_),
      uffd_(kFdUnused),
      marking_done_(false),
      uffd_initialized_(false),
      clamp_info_map_status_(ClampInfoStatus::kClampInfoNotDone) {
  if (kIsDebugBuild) {
    updated_roots_.reset(new std::unordered_set<void*>());
  }
  if (gUffdFeatures == 0) {
    GetUffdAndMinorFault();
  }
  uint8_t* moving_space_begin = bump_pointer_space_->Begin();
  // TODO: Depending on how the bump-pointer space move is implemented. If we
  // switch between two virtual memories each time, then we will have to
  // initialize live_words_bitmap_ accordingly.
  live_words_bitmap_.reset(LiveWordsBitmap<kAlignment>::Create(
      reinterpret_cast<uintptr_t>(moving_space_begin),
      reinterpret_cast<uintptr_t>(bump_pointer_space_->Limit())));

  std::string err_msg;
  size_t moving_space_size = bump_pointer_space_->Capacity();
  {
    // Create one MemMap for all the data structures
    info_map_ = MemMap::MapAnonymous("Concurrent mark-compact chunk-info vector",
                                     ComputeInfoMapSize(),
                                     PROT_READ | PROT_WRITE,
                                     /*low_4gb=*/false,
                                     &err_msg);
    if (UNLIKELY(!info_map_.IsValid())) {
      LOG(FATAL) << "Failed to allocate concurrent mark-compact chunk-info vector: " << err_msg;
    } else {
      size_t total = InitializeInfoMap(info_map_.Begin(), moving_space_size);
      DCHECK_EQ(total, info_map_.Size());
    }
  }

  size_t moving_space_alignment = Heap::BestPageTableAlignment(moving_space_size);
  // The moving space is created at a fixed address, which is expected to be
  // PMD-size aligned.
  if (!IsAlignedParam(moving_space_begin, moving_space_alignment)) {
    LOG(WARNING) << "Bump pointer space is not aligned to " << PrettySize(moving_space_alignment)
                 << ". This can lead to longer stop-the-world pauses for compaction";
  }
  // NOTE: PROT_NONE is used here as these mappings are for address space reservation
  // only and will be used only after appropriately remapping them.
  from_space_map_ = MemMap::MapAnonymousAligned("Concurrent mark-compact from-space",
                                                moving_space_size,
                                                PROT_NONE,
                                                /*low_4gb=*/kObjPtrPoisoning,
                                                moving_space_alignment,
                                                &err_msg);
  if (UNLIKELY(!from_space_map_.IsValid())) {
    LOG(FATAL) << "Failed to allocate concurrent mark-compact from-space" << err_msg;
  } else {
    from_space_begin_ = from_space_map_.Begin();
  }

  compaction_buffers_map_ = MemMap::MapAnonymous("Concurrent mark-compact compaction buffers",
                                                 (1 + kMutatorCompactionBufferCount) * gPageSize,
                                                 PROT_READ | PROT_WRITE,
                                                 /*low_4gb=*/kObjPtrPoisoning,
                                                 &err_msg);
  if (UNLIKELY(!compaction_buffers_map_.IsValid())) {
    LOG(FATAL) << "Failed to allocate concurrent mark-compact compaction buffers" << err_msg;
  }
  // We also use the first page-sized buffer for the purpose of terminating concurrent compaction.
  conc_compaction_termination_page_ = compaction_buffers_map_.Begin();
  // Touch the page deliberately to avoid userfaults on it. We madvise it in
  // CompactionPhase() before using it to terminate concurrent compaction.
  ForceRead(conc_compaction_termination_page_);

  // In most of the cases, we don't expect more than one LinearAlloc space.
  linear_alloc_spaces_data_.reserve(1);

  // Initialize GC metrics.
  metrics::ArtMetrics* metrics = GetMetrics();
  gc_time_histogram_ = metrics->FullGcCollectionTime();
  metrics_gc_count_ = metrics->FullGcCount();
  metrics_gc_count_delta_ = metrics->FullGcCountDelta();
  gc_throughput_histogram_ = metrics->FullGcThroughput();
  gc_tracing_throughput_hist_ = metrics->FullGcTracingThroughput();
  gc_throughput_avg_ = metrics->FullGcThroughputAvg();
  gc_tracing_throughput_avg_ = metrics->FullGcTracingThroughputAvg();
  gc_scanned_bytes_ = metrics->FullGcScannedBytes();
  gc_scanned_bytes_delta_ = metrics->FullGcScannedBytesDelta();
  gc_freed_bytes_ = metrics->FullGcFreedBytes();
  gc_freed_bytes_delta_ = metrics->FullGcFreedBytesDelta();
  gc_duration_ = metrics->FullGcDuration();
  gc_duration_delta_ = metrics->FullGcDurationDelta();
  gc_app_slow_path_during_gc_duration_delta_ = metrics->AppSlowPathDuringFullGcDurationDelta();
  are_metrics_initialized_ = true;
}

void MarkCompact::ResetGenerationalState() {
  black_dense_end_ = mid_gen_end_ = moving_space_begin_;
  post_compact_end_ = nullptr;
  class_after_obj_map_.clear();
}

void MarkCompact::AddLinearAllocSpaceData(uint8_t* begin, size_t len) {
  DCHECK_ALIGNED_PARAM(begin, gPageSize);
  DCHECK_ALIGNED_PARAM(len, gPageSize);
  DCHECK_GE(len, Heap::GetPMDSize());
  size_t alignment = Heap::BestPageTableAlignment(len);
  std::string err_msg;
  MemMap shadow(MemMap::MapAnonymousAligned("linear-alloc shadow map",
                                            len,
                                            PROT_NONE,
                                            /*low_4gb=*/false,
                                            alignment,
                                            &err_msg));
  if (!shadow.IsValid()) {
    LOG(FATAL) << "Failed to allocate linear-alloc shadow map: " << err_msg;
    UNREACHABLE();
  }

  MemMap page_status_map(MemMap::MapAnonymous("linear-alloc page-status map",
                                              DivideByPageSize(len),
                                              PROT_READ | PROT_WRITE,
                                              /*low_4gb=*/false,
                                              &err_msg));
  if (!page_status_map.IsValid()) {
    LOG(FATAL) << "Failed to allocate linear-alloc page-status shadow map: " << err_msg;
    UNREACHABLE();
  }
  linear_alloc_spaces_data_.emplace_back(
      std::forward<MemMap>(shadow), std::forward<MemMap>(page_status_map), begin, begin + len);
}

void MarkCompact::ClampGrowthLimit(size_t new_capacity) {
  // From-space is the same size as moving-space in virtual memory.
  // However, if it's in >4GB address space then we don't need to do it
  // synchronously.
#if defined(__LP64__)
  constexpr bool kClampFromSpace = kObjPtrPoisoning;
#else
  constexpr bool kClampFromSpace = true;
#endif
  size_t old_capacity = bump_pointer_space_->Capacity();
  new_capacity = bump_pointer_space_->ClampGrowthLimit(new_capacity);
  if (new_capacity < old_capacity) {
    CHECK(from_space_map_.IsValid());
    if (kClampFromSpace) {
      from_space_map_.SetSize(new_capacity);
    }
    clamp_info_map_status_ = ClampInfoStatus::kClampInfoPending;
  }
  CHECK_EQ(moving_space_begin_, bump_pointer_space_->Begin());
}

void MarkCompact::MaybeClampGcStructures() {
  size_t moving_space_size = bump_pointer_space_->Capacity();
  DCHECK(thread_running_gc_ != nullptr);
  if (UNLIKELY(clamp_info_map_status_ == ClampInfoStatus::kClampInfoPending)) {
    CHECK(from_space_map_.IsValid());
    if (from_space_map_.Size() > moving_space_size) {
      from_space_map_.SetSize(moving_space_size);
    }
    // Bitmaps and other data structures
    live_words_bitmap_->SetBitmapSize(moving_space_size);
    size_t set_size = InitializeInfoMap(info_map_.Begin(), moving_space_size);
    CHECK_LT(set_size, info_map_.Size());
    info_map_.SetSize(set_size);

    clamp_info_map_status_ = ClampInfoStatus::kClampInfoFinished;
  }
}

void MarkCompact::PrepareForMarking(bool pre_marking) {
  static_assert(gc::accounting::CardTable::kCardDirty - 1 == gc::accounting::CardTable::kCardAged);
  static_assert(gc::accounting::CardTable::kCardAged - 1 == gc::accounting::CardTable::kCardAged2);
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  accounting::CardTable* const card_table = heap_->GetCardTable();
  // immune_spaces_ is emptied in InitializePhase() before marking starts. This
  // function is invoked twice during marking. We only need to populate immune_spaces_
  // once per GC cycle. And when it's done (below), all the immune spaces are
  // added to it. We can never have partially filled immune_spaces_.
  bool update_immune_spaces = immune_spaces_.IsEmpty();
  // Mark all of the spaces we never collect as immune.
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyNeverCollect ||
        space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect) {
      CHECK(space->IsZygoteSpace() || space->IsImageSpace());
      if (update_immune_spaces) {
        immune_spaces_.AddSpace(space);
      }
      accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
      if (table != nullptr) {
        table->ProcessCards();
      } else {
        // Keep cards aged if we don't have a mod-union table since we need
        // to scan them in future GCs. This case is for app images.
        card_table->ModifyCardsAtomic(
            space->Begin(),
            space->End(),
            [](uint8_t card) {
              return (card == gc::accounting::CardTable::kCardClean)
                  ? card
                  : gc::accounting::CardTable::kCardAged;
            },
            /* card modified visitor */ VoidFunctor());
      }
    } else if (pre_marking) {
      CHECK(!space->IsZygoteSpace());
      CHECK(!space->IsImageSpace());
      if (young_gen_) {
        uint8_t* space_age_end = space->Limit();
        // Age cards in old-gen as they contain old-to-young references.
        if (space == bump_pointer_space_) {
          DCHECK_ALIGNED_PARAM(old_gen_end_, gPageSize);
          moving_space_bitmap_->ClearRange(reinterpret_cast<mirror::Object*>(old_gen_end_),
                                           reinterpret_cast<mirror::Object*>(moving_space_end_));
          // Clear cards in [old_gen_end_, moving_space_end_) as they are not needed.
          card_table->ClearCardRange(old_gen_end_, space->Limit());
          space_age_end = old_gen_end_;
        }
        card_table->ModifyCardsAtomic(space->Begin(),
                                      space_age_end,
                                      AgeCardVisitor(),
                                      /*card modified visitor=*/VoidFunctor());
      } else {
        // The card-table corresponding to bump-pointer and non-moving space can
        // be cleared, because we are going to traverse all the reachable objects
        // in these spaces. This card-table will eventually be used to track
        // mutations while concurrent marking is going on.
        card_table->ClearCardRange(space->Begin(), space->Limit());
        if (space == bump_pointer_space_) {
          moving_space_bitmap_->Clear();
        }
      }
      if (space != bump_pointer_space_) {
        CHECK_EQ(space, heap_->GetNonMovingSpace());
        if (young_gen_) {
          space->AsContinuousMemMapAllocSpace()->BindLiveToMarkBitmap();
        }
        non_moving_space_ = space;
        non_moving_space_bitmap_ = space->GetMarkBitmap();
      }
    } else {
      if (young_gen_) {
        // It would be correct to retain existing aged cards and add dirty cards
        // to that set. However, that would unecessarily need us to re-scan
        // cards which haven't been dirtied since first-pass of marking.
        auto card_visitor = [](uint8_t card) {
          return (card > gc::accounting::CardTable::kCardAged2)
                     ? card - 1
                     : gc::accounting::CardTable::kCardClean;
        };
        card_table->ModifyCardsAtomic(
            space->Begin(), space->End(), card_visitor, /*card modified visitor=*/VoidFunctor());
      } else {
        card_table->ModifyCardsAtomic(space->Begin(),
                                      space->End(),
                                      AgeCardVisitor(),
                                      /*card modified visitor=*/VoidFunctor());
      }
    }
  }
  if (pre_marking && young_gen_) {
    for (const auto& space : GetHeap()->GetDiscontinuousSpaces()) {
      CHECK(space->IsLargeObjectSpace());
      space->AsLargeObjectSpace()->CopyLiveToMarked();
    }
  }
}

void MarkCompact::MarkZygoteLargeObjects() {
  Thread* self = thread_running_gc_;
  DCHECK_EQ(self, Thread::Current());
  space::LargeObjectSpace* const los = heap_->GetLargeObjectsSpace();
  if (los != nullptr) {
    // Pick the current live bitmap (mark bitmap if swapped).
    accounting::LargeObjectBitmap* const live_bitmap = los->GetLiveBitmap();
    accounting::LargeObjectBitmap* const mark_bitmap = los->GetMarkBitmap();
    // Walk through all of the objects and explicitly mark the zygote ones so they don't get swept.
    std::pair<uint8_t*, uint8_t*> range = los->GetBeginEndAtomic();
    live_bitmap->VisitMarkedRange(reinterpret_cast<uintptr_t>(range.first),
                                  reinterpret_cast<uintptr_t>(range.second),
                                  [mark_bitmap, los, self](mirror::Object* obj)
                                      REQUIRES(Locks::heap_bitmap_lock_)
                                          REQUIRES_SHARED(Locks::mutator_lock_) {
                                            if (los->IsZygoteLargeObject(self, obj)) {
                                              mark_bitmap->Set(obj);
                                            }
                                          });
  }
}

void MarkCompact::InitializePhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  mark_stack_ = heap_->GetMarkStack();
  CHECK(mark_stack_->IsEmpty());
  immune_spaces_.Reset();
  moving_first_objs_count_ = 0;
  non_moving_first_objs_count_ = 0;
  black_page_count_ = 0;
  bytes_scanned_ = 0;
  freed_objects_ = 0;
  // The first buffer is used by gc-thread.
  compaction_buffer_counter_.store(1, std::memory_order_relaxed);
  black_allocations_begin_ = bump_pointer_space_->Limit();
  DCHECK_EQ(moving_space_begin_, bump_pointer_space_->Begin());
  from_space_slide_diff_ = from_space_begin_ - moving_space_begin_;
  moving_space_end_ = bump_pointer_space_->Limit();
  if (use_generational_ && !young_gen_) {
    class_after_obj_map_.clear();
  }
  // TODO: Would it suffice to read it once in the constructor, which is called
  // in zygote process?
  pointer_size_ = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
  for (size_t i = 0; i < vector_length_; i++) {
    DCHECK_EQ(chunk_info_vec_[i], 0u);
  }
  app_slow_path_start_time_ = 0;
}

class MarkCompact::ThreadFlipVisitor : public Closure {
 public:
  explicit ThreadFlipVisitor(MarkCompact* collector) : collector_(collector) {}

  void Run(Thread* thread) override REQUIRES_SHARED(Locks::mutator_lock_) {
    // Note: self is not necessarily equal to thread since thread may be suspended.
    Thread* self = Thread::Current();
    CHECK(thread == self || thread->GetState() != ThreadState::kRunnable)
        << thread->GetState() << " thread " << thread << " self " << self;
    thread->VisitRoots(collector_, kVisitRootFlagAllRoots);
    // Interpreter cache is thread-local so it needs to be swept either in a
    // flip, or a stop-the-world pause.
    CHECK(collector_->compacting_);
    thread->GetInterpreterCache()->Clear(thread);
    thread->AdjustTlab(collector_->black_objs_slide_diff_);
  }

 private:
  MarkCompact* const collector_;
};

class MarkCompact::FlipCallback : public Closure {
 public:
  explicit FlipCallback(MarkCompact* collector) : collector_(collector) {}

  void Run([[maybe_unused]] Thread* thread) override REQUIRES(Locks::mutator_lock_) {
    collector_->CompactionPause();
  }

 private:
  MarkCompact* const collector_;
};

void MarkCompact::RunPhases() {
  Thread* self = Thread::Current();
  thread_running_gc_ = self;
  Runtime* runtime = Runtime::Current();
  GetHeap()->PreGcVerification(this);
  InitializePhase();
  {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    MarkingPhase();
  }
  {
    // Marking pause
    ScopedPause pause(this);
    MarkingPause();
    if (kIsDebugBuild) {
      bump_pointer_space_->AssertAllThreadLocalBuffersAreRevoked();
    }
  }
  bool perform_compaction;
  {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    ReclaimPhase();
    perform_compaction = PrepareForCompaction();
  }
  if (perform_compaction) {
    // Compaction pause
    ThreadFlipVisitor visitor(this);
    FlipCallback callback(this);
    runtime->GetThreadList()->FlipThreadRoots(
        &visitor, &callback, this, GetHeap()->GetGcPauseListener());

    if (IsValidFd(uffd_)) {
      ReaderMutexLock mu(self, *Locks::mutator_lock_);
      CompactionPhase();
    }
  } else {
    if (use_generational_) {
      DCHECK_IMPLIES(post_compact_end_ != nullptr, post_compact_end_ == black_allocations_begin_);
    }
    post_compact_end_ = black_allocations_begin_;
  }
  FinishPhase(perform_compaction);
  GetHeap()->PostGcVerification(this);
  thread_running_gc_ = nullptr;
}

void MarkCompact::InitMovingSpaceFirstObjects(size_t vec_len, size_t to_space_page_idx) {
  uint32_t offset_in_chunk_word;
  uint32_t offset;
  mirror::Object* obj;
  const uintptr_t heap_begin = moving_space_bitmap_->HeapBegin();

  // Find the first live word.
  size_t chunk_idx = to_space_page_idx * (gPageSize / kOffsetChunkSize);
  DCHECK_LT(chunk_idx, vec_len);
  // Find the first live word in the space
  for (; chunk_info_vec_[chunk_idx] == 0; chunk_idx++) {
    if (chunk_idx >= vec_len) {
      // We don't have any live data on the moving-space.
      moving_first_objs_count_ = to_space_page_idx;
      return;
    }
  }
  DCHECK_LT(chunk_idx, vec_len);
  // Use live-words bitmap to find the first live word
  offset_in_chunk_word = live_words_bitmap_->FindNthLiveWordOffset(chunk_idx, /*n*/ 0);
  offset = chunk_idx * kBitsPerVectorWord + offset_in_chunk_word;
  DCHECK(live_words_bitmap_->Test(offset)) << "offset=" << offset
                                           << " chunk_idx=" << chunk_idx
                                           << " N=0"
                                           << " offset_in_word=" << offset_in_chunk_word
                                           << " word=" << std::hex
                                           << live_words_bitmap_->GetWord(chunk_idx);
  obj = moving_space_bitmap_->FindPrecedingObject(heap_begin + offset * kAlignment);
  // TODO: add a check to validate the object.

  pre_compact_offset_moving_space_[to_space_page_idx] = offset;
  first_objs_moving_space_[to_space_page_idx].Assign(obj);
  to_space_page_idx++;

  uint32_t page_live_bytes = 0;
  while (true) {
    for (; page_live_bytes <= gPageSize; chunk_idx++) {
      if (chunk_idx >= vec_len) {
        moving_first_objs_count_ = to_space_page_idx;
        return;
      }
      page_live_bytes += chunk_info_vec_[chunk_idx];
    }
    chunk_idx--;
    page_live_bytes -= gPageSize;
    DCHECK_LE(page_live_bytes, kOffsetChunkSize);
    DCHECK_LE(page_live_bytes, chunk_info_vec_[chunk_idx])
        << " chunk_idx=" << chunk_idx
        << " to_space_page_idx=" << to_space_page_idx
        << " vec_len=" << vec_len;
    DCHECK(IsAligned<kAlignment>(chunk_info_vec_[chunk_idx] - page_live_bytes));
    offset_in_chunk_word =
            live_words_bitmap_->FindNthLiveWordOffset(
                chunk_idx, (chunk_info_vec_[chunk_idx] - page_live_bytes) / kAlignment);
    offset = chunk_idx * kBitsPerVectorWord + offset_in_chunk_word;
    DCHECK(live_words_bitmap_->Test(offset))
        << "offset=" << offset
        << " chunk_idx=" << chunk_idx
        << " N=" << ((chunk_info_vec_[chunk_idx] - page_live_bytes) / kAlignment)
        << " offset_in_word=" << offset_in_chunk_word
        << " word=" << std::hex << live_words_bitmap_->GetWord(chunk_idx);
    // TODO: Can we optimize this for large objects? If we are continuing a
    // large object that spans multiple pages, then we may be able to do without
    // calling FindPrecedingObject().
    //
    // Find the object which encapsulates offset in it, which could be
    // starting at offset itself.
    obj = moving_space_bitmap_->FindPrecedingObject(heap_begin + offset * kAlignment);
    // TODO: add a check to validate the object.
    pre_compact_offset_moving_space_[to_space_page_idx] = offset;
    first_objs_moving_space_[to_space_page_idx].Assign(obj);
    to_space_page_idx++;
    chunk_idx++;
  }
}

size_t MarkCompact::InitNonMovingFirstObjects(uintptr_t begin,
                                              uintptr_t end,
                                              accounting::ContinuousSpaceBitmap* bitmap,
                                              ObjReference* first_objs_arr) {
  mirror::Object* prev_obj;
  size_t page_idx;
  {
    // Find first live object
    mirror::Object* obj = nullptr;
    bitmap->VisitMarkedRange</*kVisitOnce*/ true>(begin,
                                                  end,
                                                  [&obj] (mirror::Object* o) {
                                                    obj = o;
                                                  });
    if (obj == nullptr) {
      // There are no live objects in the space
      return 0;
    }
    page_idx = DivideByPageSize(reinterpret_cast<uintptr_t>(obj) - begin);
    first_objs_arr[page_idx++].Assign(obj);
    prev_obj = obj;
  }
  // TODO: check obj is valid
  uintptr_t prev_obj_end = reinterpret_cast<uintptr_t>(prev_obj)
                           + RoundUp(prev_obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
  // For every page find the object starting from which we need to call
  // VisitReferences. It could either be an object that started on some
  // preceding page, or some object starting within this page.
  begin = RoundDown(reinterpret_cast<uintptr_t>(prev_obj) + gPageSize, gPageSize);
  while (begin < end) {
    // Utilize, if any, large object that started in some preceding page, but
    // overlaps with this page as well.
    if (prev_obj != nullptr && prev_obj_end > begin) {
      DCHECK_LT(prev_obj, reinterpret_cast<mirror::Object*>(begin));
      first_objs_arr[page_idx].Assign(prev_obj);
    } else {
      prev_obj_end = 0;
      // It's sufficient to only search for previous object in the preceding page.
      // If no live object started in that page and some object had started in
      // the page preceding to that page, which was big enough to overlap with
      // the current page, then we wouldn't be in the else part.
      prev_obj = bitmap->FindPrecedingObject(begin, begin - gPageSize);
      if (prev_obj != nullptr) {
        prev_obj_end = reinterpret_cast<uintptr_t>(prev_obj)
                        + RoundUp(prev_obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
      }
      if (prev_obj_end > begin) {
        first_objs_arr[page_idx].Assign(prev_obj);
      } else {
        // Find the first live object in this page
        bitmap->VisitMarkedRange</*kVisitOnce*/ true>(
            begin, begin + gPageSize, [first_objs_arr, page_idx](mirror::Object* obj) {
              first_objs_arr[page_idx].Assign(obj);
            });
      }
      // An empty entry indicates that the page has no live objects and hence
      // can be skipped.
    }
    begin += gPageSize;
    page_idx++;
  }
  return page_idx;
}

// Generational CMC description
// ============================
//
// All allocations since last GC are considered to be in young generation.
// Unlike other ART GCs, we promote surviving objects to old generation after
// they survive two contiguous GCs. Objects that survive one GC are considered
// to be in mid generation. In the next young GC, marking is performed on both
// the young as well as mid gen objects. And then during compaction, the
// surviving mid-gen objects are compacted and then promoted to old-gen, while
// the surviving young gen objects are compacted and promoted to mid-gen.
//
// Some other important points worth explaining:
//
// 1. During marking-phase, 'mid_gen_end_' segregates young and mid generations.
// Before starting compaction, in PrepareForCompaction(), we set it to the
// corresponding post-compact addresses, aligned up to page-size. Therefore,
// some object's beginning portion maybe in mid-gen, while the rest is in young-gen.
// Aligning up is essential as mid_gen_end_ becomes old_gen_end_ at the end of
// GC cycle, and the latter has to be page-aligned as old-gen pages are
// processed differently (no compaction).
//
// 2. We need to maintain the mark-bitmap for the old-gen for subsequent GCs,
// when objects are promoted to old-gen from mid-gen, their mark bits are
// first collected in a BitVector and then later copied into mark-bitmap in
// FinishPhase(). We can't directly set the bits in mark-bitmap as the bitmap
// contains pre-compaction mark bits which are required during compaction.
//
// 3. Since we need to revisit mid-gen objects in the next GC cycle, we need to
// dirty the cards in old-gen containing references to them. We identify these
// references when visiting old-gen objects during compaction. However, native
// roots are skipped at that time (they are updated separately in linear-alloc
// space, where we don't know which object (dex-cache/class-loader/class) does
// a native root belong to. Therefore, native roots are covered during marking
// phase.

bool MarkCompact::PrepareForCompaction() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  size_t chunk_info_per_page = gPageSize / kOffsetChunkSize;
  size_t vector_len = (black_allocations_begin_ - moving_space_begin_) / kOffsetChunkSize;
  DCHECK_LE(vector_len, vector_length_);
  DCHECK_ALIGNED_PARAM(vector_length_, chunk_info_per_page);
  if (UNLIKELY(vector_len == 0)) {
    // Nothing to compact. Entire heap is empty.
    black_dense_end_ = mid_gen_end_ = moving_space_begin_;
    return false;
  }
  for (size_t i = 0; i < vector_len; i++) {
    DCHECK_LE(chunk_info_vec_[i], kOffsetChunkSize)
        << "i:" << i << " vector_length:" << vector_len << " vector_length_:" << vector_length_;
    DCHECK_EQ(chunk_info_vec_[i], live_words_bitmap_->LiveBytesInBitmapWord(i));
  }

  // TODO: We can do a lot of neat tricks with this offset vector to tune the
  // compaction as we wish. Originally, the compaction algorithm slides all
  // live objects towards the beginning of the heap. This is nice because it
  // keeps the spatial locality of objects intact.
  // However, sometimes it's desired to compact objects in certain portions
  // of the heap. For instance, it is expected that, over time,
  // objects towards the beginning of the heap are long lived and are always
  // densely packed. In this case, it makes sense to only update references in
  // there and not try to compact it.
  // Furthermore, we might have some large objects and may not want to move such
  // objects.
  // We can adjust, without too much effort, the values in the chunk_info_vec_ such
  // that the objects in the dense beginning area aren't moved. OTOH, large
  // objects, which could be anywhere in the heap, could also be kept from
  // moving by using a similar trick. The only issue is that by doing this we will
  // leave an unused hole in the middle of the heap which can't be used for
  // allocations until we do a *full* compaction.
  //
  // At this point every element in the chunk_info_vec_ contains the live-bytes
  // of the corresponding chunk. For old-to-new address computation we need
  // every element to reflect total live-bytes till the corresponding chunk.

  size_t black_dense_idx = 0;
  GcCause gc_cause = GetCurrentIteration()->GetGcCause();
  if (young_gen_) {
    DCHECK_ALIGNED_PARAM(old_gen_end_, gPageSize);
    DCHECK_GE(mid_gen_end_, old_gen_end_);
    DCHECK_GE(black_allocations_begin_, mid_gen_end_);
    // old-gen's boundary was decided at the end of previous GC-cycle.
    black_dense_idx = (old_gen_end_ - moving_space_begin_) / kOffsetChunkSize;
    if (black_dense_idx == vector_len) {
      // There is nothing live in young-gen.
      DCHECK_EQ(old_gen_end_, black_allocations_begin_);
      mid_gen_end_ = black_allocations_begin_;
      return false;
    }
    InitNonMovingFirstObjects(reinterpret_cast<uintptr_t>(moving_space_begin_),
                              reinterpret_cast<uintptr_t>(old_gen_end_),
                              moving_space_bitmap_,
                              first_objs_moving_space_);
  } else if (gc_cause != kGcCauseExplicit && gc_cause != kGcCauseCollectorTransition &&
             !GetCurrentIteration()->GetClearSoftReferences()) {
    uint64_t live_bytes = 0, total_bytes = 0;
    size_t aligned_vec_len = RoundUp(vector_len, chunk_info_per_page);
    size_t num_pages = aligned_vec_len / chunk_info_per_page;
    size_t threshold_passing_marker = 0;  // In number of pages
    std::vector<uint32_t> pages_live_bytes;
    pages_live_bytes.reserve(num_pages);
    // Identify the largest chunk towards the beginning of moving space which
    // passes the black-dense threshold.
    for (size_t i = 0; i < aligned_vec_len; i += chunk_info_per_page) {
      uint32_t page_live_bytes = 0;
      for (size_t j = 0; j < chunk_info_per_page; j++) {
        page_live_bytes += chunk_info_vec_[i + j];
        total_bytes += kOffsetChunkSize;
      }
      live_bytes += page_live_bytes;
      pages_live_bytes.push_back(page_live_bytes);
      if (live_bytes * 100U >= total_bytes * kBlackDenseRegionThreshold) {
        threshold_passing_marker = pages_live_bytes.size();
      }
    }
    DCHECK_EQ(pages_live_bytes.size(), num_pages);
    // Eliminate the pages at the end of the chunk which are lower than the threshold.
    if (threshold_passing_marker > 0) {
      auto iter = std::find_if(
          pages_live_bytes.rbegin() + (num_pages - threshold_passing_marker),
          pages_live_bytes.rend(),
          [](uint32_t bytes) { return bytes * 100U >= gPageSize * kBlackDenseRegionThreshold; });
      black_dense_idx = (pages_live_bytes.rend() - iter) * chunk_info_per_page;
    }
    black_dense_end_ = moving_space_begin_ + black_dense_idx * kOffsetChunkSize;
    DCHECK_ALIGNED_PARAM(black_dense_end_, gPageSize);

    // Adjust for class allocated after black_dense_end_ while its object(s)
    // are earlier. This is required as we update the references in the
    // black-dense region in-place. And if the class pointer of some first
    // object for a page, which started in some preceding page, is already
    // updated, then we will read wrong class data like ref-offset bitmap.
    for (auto iter = class_after_obj_map_.rbegin();
         iter != class_after_obj_map_.rend() &&
         reinterpret_cast<uint8_t*>(iter->first.AsMirrorPtr()) >= black_dense_end_;
         iter++) {
      black_dense_end_ =
          std::min(black_dense_end_, reinterpret_cast<uint8_t*>(iter->second.AsMirrorPtr()));
      black_dense_end_ = AlignDown(black_dense_end_, gPageSize);
    }
    black_dense_idx = (black_dense_end_ - moving_space_begin_) / kOffsetChunkSize;
    DCHECK_LE(black_dense_idx, vector_len);
    if (black_dense_idx == vector_len) {
      // There is nothing to compact. All the in-use pages are completely full.
      mid_gen_end_ = black_allocations_begin_;
      return false;
    }
    InitNonMovingFirstObjects(reinterpret_cast<uintptr_t>(moving_space_begin_),
                              reinterpret_cast<uintptr_t>(black_dense_end_),
                              moving_space_bitmap_,
                              first_objs_moving_space_);
  } else {
    black_dense_end_ = moving_space_begin_;
  }

  InitMovingSpaceFirstObjects(vector_len, black_dense_idx / chunk_info_per_page);
  non_moving_first_objs_count_ =
      InitNonMovingFirstObjects(reinterpret_cast<uintptr_t>(non_moving_space_->Begin()),
                                reinterpret_cast<uintptr_t>(non_moving_space_->End()),
                                non_moving_space_bitmap_,
                                first_objs_non_moving_space_);
  // Update the vector one past the heap usage as it is required for black
  // allocated objects' post-compact address computation.
  uint32_t total_bytes;
  if (vector_len < vector_length_) {
    vector_len++;
    total_bytes = 0;
  } else {
    // Fetch the value stored in the last element before it gets overwritten by
    // std::exclusive_scan().
    total_bytes = chunk_info_vec_[vector_len - 1];
  }
  std::exclusive_scan(chunk_info_vec_ + black_dense_idx,
                      chunk_info_vec_ + vector_len,
                      chunk_info_vec_ + black_dense_idx,
                      black_dense_idx * kOffsetChunkSize);
  total_bytes += chunk_info_vec_[vector_len - 1];
  post_compact_end_ = AlignUp(moving_space_begin_ + total_bytes, gPageSize);
  CHECK_EQ(post_compact_end_, moving_space_begin_ + moving_first_objs_count_ * gPageSize)
      << "moving_first_objs_count_:" << moving_first_objs_count_
      << " black_dense_idx:" << black_dense_idx << " vector_len:" << vector_len
      << " total_bytes:" << total_bytes
      << " black_dense_end:" << reinterpret_cast<void*>(black_dense_end_)
      << " chunk_info_per_page:" << chunk_info_per_page;
  black_objs_slide_diff_ = black_allocations_begin_ - post_compact_end_;
  // We shouldn't be consuming more space after compaction than pre-compaction.
  CHECK_GE(black_objs_slide_diff_, 0);
  for (size_t i = vector_len; i < vector_length_; i++) {
    DCHECK_EQ(chunk_info_vec_[i], 0u);
  }
  if (black_objs_slide_diff_ == 0) {
    // Regardless of the gc-type, there are no pages to be compacted. Ensure
    // that we don't shrink the mid-gen, which will become old-gen in
    // FinishPhase(), thereby possibly moving some objects back to young-gen,
    // which can cause memory corruption due to missing card marks.
    mid_gen_end_ = std::max(mid_gen_end_, black_dense_end_);
    mid_gen_end_ = std::min(mid_gen_end_, post_compact_end_);
    return false;
  }
  if (use_generational_) {
    // Current value of mid_gen_end_ represents end of 'pre-compacted' mid-gen,
    // which was done at the end of previous GC. Compute, 'post-compacted' end of
    // mid-gen, which will be consumed by old-gen at the end of this GC cycle.
    DCHECK_NE(mid_gen_end_, nullptr);
    mirror::Object* first_obj = nullptr;
    if (mid_gen_end_ < black_allocations_begin_) {
      ReaderMutexLock rmu(thread_running_gc_, *Locks::heap_bitmap_lock_);
      // Find the first live object in the young-gen.
      moving_space_bitmap_->VisitMarkedRange</*kVisitOnce=*/true>(
          reinterpret_cast<uintptr_t>(mid_gen_end_),
          reinterpret_cast<uintptr_t>(black_allocations_begin_),
          [&first_obj](mirror::Object* obj) { first_obj = obj; });
    }
    if (first_obj != nullptr) {
      mirror::Object* compacted_obj;
      if (reinterpret_cast<uint8_t*>(first_obj) >= old_gen_end_) {
        // post-compact address of the first live object in young-gen.
        compacted_obj = PostCompactOldObjAddr(first_obj);
        DCHECK_LT(reinterpret_cast<uint8_t*>(compacted_obj), post_compact_end_);
      } else {
        DCHECK(!young_gen_);
        compacted_obj = first_obj;
      }
      // It's important to page-align mid-gen boundary. However, that means
      // there could be an object overlapping that boundary. We will deal with
      // the consequences of that at different places. Aligning up is important
      // to ensure that we don't de-promote an object from old-gen back to
      // young-gen. Otherwise, we may skip dirtying card for such an object if
      // it contains native-roots to young-gen.
      mid_gen_end_ = AlignUp(reinterpret_cast<uint8_t*>(compacted_obj), gPageSize);
      // We need to ensure that for any object in old-gen, its class is also in
      // there (for the same reason as mentioned above in the black-dense case).
      // So adjust mid_gen_end_ accordingly, in the worst case all the way up
      // to post_compact_end_.
      auto iter = class_after_obj_map_.lower_bound(ObjReference::FromMirrorPtr(first_obj));
      for (; iter != class_after_obj_map_.end(); iter++) {
        // 'mid_gen_end_' is now post-compact, so need to compare with
        // post-compact addresses.
        compacted_obj =
            PostCompactAddress(iter->second.AsMirrorPtr(), old_gen_end_, moving_space_end_);
        // We cannot update the map with post-compact addresses yet as compaction-phase
        // expects pre-compacted addresses. So we will update in FinishPhase().
        if (reinterpret_cast<uint8_t*>(compacted_obj) < mid_gen_end_) {
          mirror::Object* klass = iter->first.AsMirrorPtr();
          DCHECK_LT(reinterpret_cast<uint8_t*>(klass), black_allocations_begin_);
          klass = PostCompactAddress(klass, old_gen_end_, moving_space_end_);
          // We only need to make sure that the class object doesn't move during
          // compaction, which can be ensured by just making its first word be
          // consumed in to the old-gen.
          mid_gen_end_ =
              std::max(mid_gen_end_, reinterpret_cast<uint8_t*>(klass) + kObjectAlignment);
          mid_gen_end_ = AlignUp(mid_gen_end_, gPageSize);
        }
      }
      CHECK_LE(mid_gen_end_, post_compact_end_);
    } else {
      // Young-gen is empty.
      mid_gen_end_ = post_compact_end_;
    }
    DCHECK_LE(mid_gen_end_, post_compact_end_);
    // We need this temporary bitmap only when running in generational mode.
    if (old_gen_end_ < mid_gen_end_) {
      mid_to_old_promo_bit_vec_.reset(
          new BitVector((mid_gen_end_ - old_gen_end_) / kObjectAlignment,
                        /*expandable=*/false,
                        Allocator::GetCallocAllocator()));
    }
  }
  // How do we handle compaction of heap portion used for allocations after the
  // marking-pause?
  // All allocations after the marking-pause are considered black (reachable)
  // for this GC cycle. However, they need not be allocated contiguously as
  // different mutators use TLABs. So we will compact the heap till the point
  // where allocations took place before the marking-pause. And everything after
  // that will be slid with TLAB holes, and then TLAB info in TLS will be
  // appropriately updated in the pre-compaction pause.
  // The chunk-info vector entries for the post marking-pause allocations will be
  // also updated in the pre-compaction pause.

  if (!uffd_initialized_) {
    CreateUserfaultfd(/*post_fork=*/false);
  }
  return true;
}

class MarkCompact::VerifyRootMarkedVisitor : public SingleRootVisitor {
 public:
  explicit VerifyRootMarkedVisitor(MarkCompact* collector) : collector_(collector) { }

  void VisitRoot(mirror::Object* root, const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_, Locks::heap_bitmap_lock_) {
    CHECK(collector_->IsMarked(root) != nullptr) << info.ToString();
  }

 private:
  MarkCompact* const collector_;
};

void MarkCompact::ReMarkRoots(Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  Locks::mutator_lock_->AssertExclusiveHeld(thread_running_gc_);
  MarkNonThreadRoots(runtime);
  MarkConcurrentRoots(
      static_cast<VisitRootFlags>(kVisitRootFlagNewRoots | kVisitRootFlagStopLoggingNewRoots |
                                  kVisitRootFlagClearRootLog),
      runtime);
  if (kVerifyRootsMarked) {
    TimingLogger::ScopedTiming t2("(Paused)VerifyRoots", GetTimings());
    VerifyRootMarkedVisitor visitor(this);
    runtime->VisitRoots(&visitor);
  }
}

void MarkCompact::MarkingPause() {
  TimingLogger::ScopedTiming t("(Paused)MarkingPause", GetTimings());
  Runtime* runtime = Runtime::Current();
  Locks::mutator_lock_->AssertExclusiveHeld(thread_running_gc_);
  {
    // Handle the dirty objects as we are a concurrent GC
    WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    {
      MutexLock mu2(thread_running_gc_, *Locks::runtime_shutdown_lock_);
      MutexLock mu3(thread_running_gc_, *Locks::thread_list_lock_);
      std::list<Thread*> thread_list = runtime->GetThreadList()->GetList();
      for (Thread* thread : thread_list) {
        thread->VisitRoots(this, static_cast<VisitRootFlags>(0));
        DCHECK_EQ(thread->GetThreadLocalGcBuffer(), nullptr);
        // Need to revoke all the thread-local allocation stacks since we will
        // swap the allocation stacks (below) and don't want anybody to allocate
        // into the live stack.
        thread->RevokeThreadLocalAllocationStack();
        bump_pointer_space_->RevokeThreadLocalBuffers(thread);
      }
    }
    ProcessMarkStack();
    // Fetch only the accumulated objects-allocated count as it is guaranteed to
    // be up-to-date after the TLAB revocation above.
    freed_objects_ += bump_pointer_space_->GetAccumulatedObjectsAllocated();
    // Capture 'end' of moving-space at this point. Every allocation beyond this
    // point will be considered as black.
    // Align-up to page boundary so that black allocations happen from next page
    // onwards. Also, it ensures that 'end' is aligned for card-table's
    // ClearCardRange().
    black_allocations_begin_ = bump_pointer_space_->AlignEnd(thread_running_gc_, gPageSize, heap_);
    DCHECK_ALIGNED_PARAM(black_allocations_begin_, gPageSize);

    // Re-mark root set. Doesn't include thread-roots as they are already marked
    // above.
    ReMarkRoots(runtime);
    // Scan dirty objects.
    RecursiveMarkDirtyObjects(/*paused*/ true, accounting::CardTable::kCardDirty);
    {
      TimingLogger::ScopedTiming t2("SwapStacks", GetTimings());
      heap_->SwapStacks();
      live_stack_freeze_size_ = heap_->GetLiveStack()->Size();
    }
  }
  // TODO: For PreSweepingGcVerification(), find correct strategy to visit/walk
  // objects in bump-pointer space when we have a mark-bitmap to indicate live
  // objects. At the same time we also need to be able to visit black allocations,
  // even though they are not marked in the bitmap. Without both of these we fail
  // pre-sweeping verification. As well as we leave windows open wherein a
  // VisitObjects/Walk on the space would either miss some objects or visit
  // unreachable ones. These windows are when we are switching from shared
  // mutator-lock to exclusive and vice-versa starting from here till compaction pause.
  // heap_->PreSweepingGcVerification(this);

  // Disallow new system weaks to prevent a race which occurs when someone adds
  // a new system weak before we sweep them. Since this new system weak may not
  // be marked, the GC may incorrectly sweep it. This also fixes a race where
  // interning may attempt to return a strong reference to a string that is
  // about to be swept.
  runtime->DisallowNewSystemWeaks();
  // Enable the reference processing slow path, needs to be done with mutators
  // paused since there is no lock in the GetReferent fast path.
  heap_->GetReferenceProcessor()->EnableSlowPath();
  marking_done_ = true;
}

void MarkCompact::SweepSystemWeaks(Thread* self, Runtime* runtime, const bool paused) {
  TimingLogger::ScopedTiming t(paused ? "(Paused)SweepSystemWeaks" : "SweepSystemWeaks",
                               GetTimings());
  ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
  runtime->SweepSystemWeaks(this);
}

void MarkCompact::ProcessReferences(Thread* self) {
  WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
  GetHeap()->GetReferenceProcessor()->ProcessReferences(self, GetTimings());
}

void MarkCompact::SweepArray(accounting::ObjectStack* obj_arr, bool swap_bitmaps) {
  TimingLogger::ScopedTiming t("SweepArray", GetTimings());
  std::vector<space::ContinuousSpace*> sweep_spaces;
  for (space::ContinuousSpace* space : heap_->GetContinuousSpaces()) {
    if (!space->IsAllocSpace() || space == bump_pointer_space_ ||
        immune_spaces_.ContainsSpace(space) || space->GetLiveBitmap() == nullptr) {
      continue;
    }
    sweep_spaces.push_back(space);
  }
  GarbageCollector::SweepArray(obj_arr, swap_bitmaps, &sweep_spaces);
}

void MarkCompact::Sweep(bool swap_bitmaps) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  if (young_gen_) {
    // Only sweep objects on the live stack.
    SweepArray(heap_->GetLiveStack(), /*swap_bitmaps=*/false);
  } else {
    // Ensure that nobody inserted objects in the live stack after we swapped the
    // stacks.
    CHECK_GE(live_stack_freeze_size_, GetHeap()->GetLiveStack()->Size());
    {
      TimingLogger::ScopedTiming t2("MarkAllocStackAsLive", GetTimings());
      // Mark everything allocated since the last GC as live so that we can sweep
      // concurrently, knowing that new allocations won't be marked as live.
      accounting::ObjectStack* live_stack = heap_->GetLiveStack();
      heap_->MarkAllocStackAsLive(live_stack);
      live_stack->Reset();
      DCHECK(mark_stack_->IsEmpty());
    }
    for (const auto& space : GetHeap()->GetContinuousSpaces()) {
      if (space->IsContinuousMemMapAllocSpace() && space != bump_pointer_space_ &&
          !immune_spaces_.ContainsSpace(space)) {
        space::ContinuousMemMapAllocSpace* alloc_space = space->AsContinuousMemMapAllocSpace();
        DCHECK(!alloc_space->IsZygoteSpace());
        TimingLogger::ScopedTiming split("SweepMallocSpace", GetTimings());
        RecordFree(alloc_space->Sweep(swap_bitmaps));
      }
    }
    SweepLargeObjects(swap_bitmaps);
  }
}

void MarkCompact::SweepLargeObjects(bool swap_bitmaps) {
  space::LargeObjectSpace* los = heap_->GetLargeObjectsSpace();
  if (los != nullptr) {
    TimingLogger::ScopedTiming split(__FUNCTION__, GetTimings());
    RecordFreeLOS(los->Sweep(swap_bitmaps));
  }
}

void MarkCompact::ReclaimPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK(thread_running_gc_ == Thread::Current());
  Runtime* const runtime = Runtime::Current();
  // Process the references concurrently.
  ProcessReferences(thread_running_gc_);
  // TODO: Try to merge this system-weak sweeping with the one while updating
  // references during the compaction pause.
  SweepSystemWeaks(thread_running_gc_, runtime, /*paused*/ false);
  runtime->AllowNewSystemWeaks();
  // Clean up class loaders after system weaks are swept since that is how we know if class
  // unloading occurred.
  runtime->GetClassLinker()->CleanupClassLoaders();
  {
    WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    // Reclaim unmarked objects.
    Sweep(false);
    // Swap the live and mark bitmaps for each space which we modified space. This is an
    // optimization that enables us to not clear live bits inside of the sweep. Only swaps unbound
    // bitmaps.
    SwapBitmaps();
    // Unbind the live and mark bitmaps.
    GetHeap()->UnBindBitmaps();
  }
  // After sweeping and unbinding, we will need to use non-moving space'
  // live-bitmap, instead of mark-bitmap.
  non_moving_space_bitmap_ = non_moving_space_->GetLiveBitmap();
}

// We want to avoid checking for every reference if it's within the page or
// not. This can be done if we know where in the page the holder object lies.
// If it doesn't overlap either boundaries then we can skip the checks.
//
// If kDirtyOldToMid = true, then check if the object contains any references
// into young-gen, which will be mid-gen after this GC. This is required
// as we mark and compact mid-gen again in next GC-cycle, and hence cards
// need to be dirtied. Note that even black-allocations (the next young-gen)
// will also have to be checked because the pages are being compacted and hence
// the card corresponding to the compacted page needs to be dirtied.
template <bool kCheckBegin, bool kCheckEnd, bool kDirtyOldToMid>
class MarkCompact::RefsUpdateVisitor {
 public:
  RefsUpdateVisitor(MarkCompact* collector,
                    mirror::Object* obj,
                    uint8_t* begin,
                    uint8_t* end,
                    accounting::CardTable* card_table = nullptr,
                    mirror::Object* card_obj = nullptr)
      : RefsUpdateVisitor(collector, obj, begin, end, false) {
    DCHECK(!kCheckBegin || begin != nullptr);
    DCHECK(!kCheckEnd || end != nullptr);
    // We can skip checking each reference for objects whose cards are already dirty.
    if (kDirtyOldToMid && card_obj != nullptr) {
      dirty_card_ = card_table->IsDirty(card_obj);
    }
  }

  RefsUpdateVisitor(
      MarkCompact* collector, mirror::Object* obj, uint8_t* begin, uint8_t* end, bool dirty_card)
      : collector_(collector),
        moving_space_begin_(collector->black_dense_end_),
        moving_space_end_(collector->moving_space_end_),
        young_gen_begin_(collector->mid_gen_end_),
        obj_(obj),
        begin_(begin),
        end_(end),
        dirty_card_(dirty_card) {}

  bool ShouldDirtyCard() const { return dirty_card_; }

  void operator()([[maybe_unused]] mirror::Object* old,
                  MemberOffset offset,
                  [[maybe_unused]] bool is_static) const ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES_SHARED(Locks::heap_bitmap_lock_) {
    bool update = true;
    if (kCheckBegin || kCheckEnd) {
      uint8_t* ref = reinterpret_cast<uint8_t*>(obj_) + offset.Int32Value();
      update = (!kCheckBegin || ref >= begin_) && (!kCheckEnd || ref < end_);
    }
    if (update) {
      mirror::Object* new_ref =
          collector_->UpdateRef(obj_, offset, moving_space_begin_, moving_space_end_);
      CheckShouldDirtyCard(new_ref);
    }
  }

  // For object arrays we don't need to check boundaries here as it's done in
  // VisitReferenes().
  // TODO: Optimize reference updating using SIMD instructions. Object arrays
  // are perfect as all references are tightly packed.
  void operator()([[maybe_unused]] mirror::Object* old,
                  MemberOffset offset,
                  [[maybe_unused]] bool is_static,
                  [[maybe_unused]] bool is_obj_array) const ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES_SHARED(Locks::heap_bitmap_lock_) {
    mirror::Object* new_ref =
        collector_->UpdateRef(obj_, offset, moving_space_begin_, moving_space_end_);
    CheckShouldDirtyCard(new_ref);
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mirror::Object* new_ref = collector_->UpdateRoot(root, moving_space_begin_, moving_space_end_);
    CheckShouldDirtyCard(new_ref);
  }

 private:
  inline void CheckShouldDirtyCard(mirror::Object* ref) const {
    if (kDirtyOldToMid && !dirty_card_) {
      // moving_space_end_ is young-gen's end.
      dirty_card_ = reinterpret_cast<uint8_t*>(ref) >= young_gen_begin_ &&
                    reinterpret_cast<uint8_t*>(ref) < moving_space_end_;
    }
  }

  MarkCompact* const collector_;
  uint8_t* const moving_space_begin_;
  uint8_t* const moving_space_end_;
  uint8_t* const young_gen_begin_;
  mirror::Object* const obj_;
  uint8_t* const begin_;
  uint8_t* const end_;
  mutable bool dirty_card_;
};

inline void MarkCompact::SetBitForMidToOldPromotion(uint8_t* obj) {
  DCHECK(use_generational_);
  DCHECK_GE(obj, old_gen_end_);
  DCHECK_LT(obj, mid_gen_end_);
  // This doesn't need to be atomic as every thread only sets bits in the
  // bit_vector words corresponding to the page it is compacting.
  mid_to_old_promo_bit_vec_->SetBit((obj - old_gen_end_) / kObjectAlignment);
}

bool MarkCompact::IsValidObject(mirror::Object* obj) const {
  mirror::Class* klass = obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
  if (!heap_->GetVerification()->IsValidHeapObjectAddress(klass)) {
    return false;
  }
  return heap_->GetVerification()->IsValidClassUnchecked<kWithFromSpaceBarrier>(
          obj->GetClass<kVerifyNone, kWithFromSpaceBarrier>());
}

template <typename Callback>
void MarkCompact::VerifyObject(mirror::Object* ref, Callback& callback) const {
  if (kIsDebugBuild) {
    mirror::Class* klass = ref->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    mirror::Class* pre_compact_klass = ref->GetClass<kVerifyNone, kWithoutReadBarrier>();
    mirror::Class* klass_klass = klass->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    mirror::Class* klass_klass_klass = klass_klass->GetClass<kVerifyNone, kWithFromSpaceBarrier>();
    if (HasAddress(pre_compact_klass) &&
        reinterpret_cast<uint8_t*>(pre_compact_klass) < black_allocations_begin_) {
      CHECK(moving_space_bitmap_->Test(pre_compact_klass))
          << "ref=" << ref
          << " post_compact_end=" << static_cast<void*>(post_compact_end_)
          << " pre_compact_klass=" << pre_compact_klass
          << " black_allocations_begin=" << static_cast<void*>(black_allocations_begin_);
      if (!young_gen_) {
        CHECK(live_words_bitmap_->Test(pre_compact_klass));
      }
    }
    if (!IsValidObject(ref)) {
      std::ostringstream oss;
      oss << "Invalid object: "
          << "ref=" << ref
          << " klass=" << klass
          << " klass_klass=" << klass_klass
          << " klass_klass_klass=" << klass_klass_klass
          << " pre_compact_klass=" << pre_compact_klass
          << " from_space_begin=" << static_cast<void*>(from_space_begin_)
          << " pre_compact_begin=" << static_cast<void*>(bump_pointer_space_->Begin())
          << " post_compact_end=" << static_cast<void*>(post_compact_end_)
          << " black_allocations_begin=" << static_cast<void*>(black_allocations_begin_);

      // Call callback before dumping larger data like RAM and space dumps.
      callback(oss);

      oss << " \nobject="
          << heap_->GetVerification()->DumpRAMAroundAddress(reinterpret_cast<uintptr_t>(ref), 128)
          << " \nklass(from)="
          << heap_->GetVerification()->DumpRAMAroundAddress(reinterpret_cast<uintptr_t>(klass), 128)
          << "spaces:\n";
      heap_->DumpSpaces(oss);
      LOG(FATAL) << oss.str();
    }
  }
}

template <bool kSetupForGenerational>
void MarkCompact::CompactPage(mirror::Object* obj,
                              uint32_t offset,
                              uint8_t* addr,
                              uint8_t* to_space_addr,
                              bool needs_memset_zero) {
  DCHECK_ALIGNED_PARAM(to_space_addr, gPageSize);
  DCHECK(moving_space_bitmap_->Test(obj)
         && live_words_bitmap_->Test(obj));
  DCHECK(live_words_bitmap_->Test(offset)) << "obj=" << obj
                                           << " offset=" << offset
                                           << " addr=" << static_cast<void*>(addr)
                                           << " black_allocs_begin="
                                           << static_cast<void*>(black_allocations_begin_)
                                           << " post_compact_addr="
                                           << static_cast<void*>(post_compact_end_);
  accounting::CardTable* card_table = heap_->GetCardTable();
  uint8_t* const start_addr = addr;
  // We need to find the cards in the mid-gen (which is going to be consumed
  // into old-gen after this GC) for dirty cards (dirtied after marking-pause and
  // until compaction pause) and dirty the corresponding post-compact cards. We
  // could have found reference fields while updating them in RefsUpdateVisitor.
  // But it will not catch native-roots and hence we need to directly look at the
  // pre-compact card-table.
  // NOTE: we may get some false-positives if the same address in post-compact
  // heap is already allocated as TLAB and has been having write-barrers be
  // called. But that is not harmful.
  size_t cards_per_page = gPageSize >> accounting::CardTable::kCardShift;
  size_t dest_cards = 0;
  DCHECK(IsAligned<accounting::CardTable::kCardSize>(gPageSize));
  static_assert(sizeof(dest_cards) * kBitsPerByte >=
                kMaxPageSize / accounting::CardTable::kCardSize);
  // How many distinct live-strides do we have.
  size_t stride_count = 0;
  uint8_t* last_stride = addr;
  uint32_t last_stride_begin = 0;
  auto verify_obj_callback = [&](std::ostream& os) {
    os << " stride_count=" << stride_count << " last_stride=" << static_cast<void*>(last_stride)
       << " offset=" << offset << " start_addr=" << static_cast<void*>(start_addr);
  };
  live_words_bitmap_->VisitLiveStrides(
      offset,
      black_allocations_begin_,
      gPageSize,
      [&](uint32_t stride_begin, size_t stride_size, [[maybe_unused]] bool is_last)
          REQUIRES_SHARED(Locks::mutator_lock_) {
            size_t stride_in_bytes = stride_size * kAlignment;
            size_t stride_begin_bytes = stride_begin * kAlignment;
            DCHECK_LE(stride_in_bytes, gPageSize);
            last_stride_begin = stride_begin;
            DCHECK(IsAligned<kAlignment>(addr));
            memcpy(addr, from_space_begin_ + stride_begin_bytes, stride_in_bytes);
            if (kIsDebugBuild) {
              uint8_t* space_begin = bump_pointer_space_->Begin();
              // We can interpret the first word of the stride as an
              // obj only from second stride onwards, as the first
              // stride's first-object may have started on previous
              // page. The only exception is the first page of the
              // moving space.
              if (stride_count > 0 || stride_begin * kAlignment < gPageSize) {
                mirror::Object* o =
                    reinterpret_cast<mirror::Object*>(space_begin + stride_begin * kAlignment);
                CHECK(live_words_bitmap_->Test(o)) << "ref=" << o;
                CHECK(moving_space_bitmap_->Test(o))
                    << "ref=" << o << " bitmap: " << moving_space_bitmap_->DumpMemAround(o);
                VerifyObject(reinterpret_cast<mirror::Object*>(addr), verify_obj_callback);
              }
            }
            last_stride = addr;
            stride_count++;
            if (kSetupForGenerational) {
              // Card idx within the gPageSize sized destination page.
              size_t dest_card_idx = (addr - start_addr) >> accounting::CardTable::kCardShift;
              DCHECK_LT(dest_card_idx, cards_per_page);
              // Bytes remaining to fill in the current dest card.
              size_t dest_bytes_remaining = accounting::CardTable::kCardSize -
                                            (addr - start_addr) % accounting::CardTable::kCardSize;
              // Update 'addr' for next stride before starting to modify
              // 'stride_in_bytes' in the loops below.
              addr += stride_in_bytes;
              // Unconsumed bytes in the current src card.
              size_t src_card_bytes = accounting::CardTable::kCardSize -
                                      stride_begin_bytes % accounting::CardTable::kCardSize;
              src_card_bytes = std::min(src_card_bytes, stride_in_bytes);
              uint8_t* end_card = card_table->CardFromAddr(
                  moving_space_begin_ + stride_begin_bytes + stride_in_bytes - 1);
              for (uint8_t* card =
                       card_table->CardFromAddr(moving_space_begin_ + stride_begin_bytes);
                   card <= end_card;
                   card++) {
                if (*card == accounting::CardTable::kCardDirty) {
                  // If the current src card will contribute to the next dest
                  // card as well, then dirty the next one too.
                  size_t val = dest_bytes_remaining < src_card_bytes ? 3 : 1;
                  dest_cards |= val << dest_card_idx;
                }
                // Adjust destination card and its remaining bytes for next iteration.
                if (dest_bytes_remaining <= src_card_bytes) {
                  dest_bytes_remaining =
                      accounting::CardTable::kCardSize - (src_card_bytes - dest_bytes_remaining);
                  dest_card_idx++;
                } else {
                  dest_bytes_remaining -= src_card_bytes;
                }
                DCHECK_LE(dest_card_idx, cards_per_page);
                stride_in_bytes -= src_card_bytes;
                src_card_bytes = std::min(accounting::CardTable::kCardSize, stride_in_bytes);
              }
            } else {
              addr += stride_in_bytes;
            }
          });
  DCHECK_LT(last_stride, start_addr + gPageSize);
  DCHECK_GT(stride_count, 0u);
  size_t obj_size = 0;
  uint32_t offset_within_obj =
      offset * kAlignment - (reinterpret_cast<uint8_t*>(obj) - moving_space_begin_);
  // First object
  if (offset_within_obj > 0) {
    bool should_dirty_card;
    mirror::Object* to_ref = reinterpret_cast<mirror::Object*>(start_addr - offset_within_obj);
    mirror::Object* from_obj = GetFromSpaceAddr(obj);
    mirror::Object* post_compact_obj = nullptr;
    if (kSetupForGenerational) {
      post_compact_obj = PostCompactAddress(obj, black_dense_end_, moving_space_end_);
    }
    if (stride_count > 1) {
      RefsUpdateVisitor</*kCheckBegin*/ true, /*kCheckEnd*/ false, kSetupForGenerational> visitor(
          this, to_ref, start_addr, nullptr, card_table, post_compact_obj);
      obj_size =
          from_obj->VisitRefsForCompaction</*kFetchObjSize*/ true, /*kVisitNativeRoots*/ false>(
              visitor, MemberOffset(offset_within_obj), MemberOffset(-1));
      should_dirty_card = visitor.ShouldDirtyCard();
    } else {
      RefsUpdateVisitor</*kCheckBegin*/ true, /*kCheckEnd*/ true, kSetupForGenerational> visitor(
          this, to_ref, start_addr, start_addr + gPageSize, card_table, post_compact_obj);
      obj_size =
          from_obj->VisitRefsForCompaction</*kFetchObjSize*/ true, /*kVisitNativeRoots*/ false>(
              visitor,
              MemberOffset(offset_within_obj),
              MemberOffset(offset_within_obj + gPageSize));
      should_dirty_card = visitor.ShouldDirtyCard();
    }
    if (kSetupForGenerational && should_dirty_card) {
      card_table->MarkCard(post_compact_obj);
    }
    obj_size = RoundUp(obj_size, kAlignment);
    DCHECK_GT(obj_size, offset_within_obj)
        << "obj:" << obj
        << " class:" << from_obj->GetClass<kDefaultVerifyFlags, kWithFromSpaceBarrier>()
        << " to_addr:" << to_ref
        << " black-allocation-begin:" << reinterpret_cast<void*>(black_allocations_begin_)
        << " post-compact-end:" << reinterpret_cast<void*>(post_compact_end_)
        << " offset:" << offset * kAlignment << " class-after-obj-iter:"
        << (class_after_obj_iter_ != class_after_obj_map_.rend()
                ? class_after_obj_iter_->first.AsMirrorPtr()
                : nullptr)
        << " last-reclaimed-page:" << reinterpret_cast<void*>(last_reclaimed_page_)
        << " last-checked-reclaim-page-idx:" << last_checked_reclaim_page_idx_
        << " offset-of-last-idx:"
        << pre_compact_offset_moving_space_[last_checked_reclaim_page_idx_] * kAlignment
        << " first-obj-of-last-idx:"
        << first_objs_moving_space_[last_checked_reclaim_page_idx_].AsMirrorPtr();

    obj_size -= offset_within_obj;
    // If there is only one stride, then adjust last_stride_begin to the
    // end of the first object.
    if (stride_count == 1) {
      last_stride_begin += obj_size / kAlignment;
    }
  }

  // Except for the last page being compacted, the pages will have addr ==
  // start_addr + gPageSize.
  uint8_t* const end_addr = addr;
  addr = start_addr;
  size_t bytes_done = obj_size;
  // All strides except the last one can be updated without any boundary
  // checks.
  DCHECK_LE(addr, last_stride);
  size_t bytes_to_visit = last_stride - addr;
  DCHECK_LE(bytes_to_visit, gPageSize);
  while (bytes_to_visit > bytes_done) {
    mirror::Object* ref = reinterpret_cast<mirror::Object*>(addr + bytes_done);
    VerifyObject(ref, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/ false, /*kCheckEnd*/ false, kSetupForGenerational> visitor(
        this,
        ref,
        nullptr,
        nullptr,
        dest_cards & (1 << (bytes_done >> accounting::CardTable::kCardShift)));
    obj_size = ref->VisitRefsForCompaction(visitor, MemberOffset(0), MemberOffset(-1));
    if (kSetupForGenerational) {
      SetBitForMidToOldPromotion(to_space_addr + bytes_done);
      if (visitor.ShouldDirtyCard()) {
        card_table->MarkCard(reinterpret_cast<mirror::Object*>(to_space_addr + bytes_done));
      }
    }
    obj_size = RoundUp(obj_size, kAlignment);
    bytes_done += obj_size;
  }
  // Last stride may have multiple objects in it and we don't know where the
  // last object which crosses the page boundary starts, therefore check
  // page-end in all of these objects. Also, we need to call
  // VisitRefsForCompaction() with from-space object as we fetch object size,
  // which in case of klass requires 'class_size_'.
  uint8_t* from_addr = from_space_begin_ + last_stride_begin * kAlignment;
  bytes_to_visit = end_addr - addr;
  DCHECK_LE(bytes_to_visit, gPageSize);
  while (bytes_to_visit > bytes_done) {
    mirror::Object* ref = reinterpret_cast<mirror::Object*>(addr + bytes_done);
    obj = reinterpret_cast<mirror::Object*>(from_addr);
    VerifyObject(ref, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/ false, /*kCheckEnd*/ true, kSetupForGenerational> visitor(
        this,
        ref,
        nullptr,
        start_addr + gPageSize,
        dest_cards & (1 << (bytes_done >> accounting::CardTable::kCardShift)));
    obj_size = obj->VisitRefsForCompaction(visitor,
                                           MemberOffset(0),
                                           MemberOffset(end_addr - (addr + bytes_done)));
    if (kSetupForGenerational) {
      SetBitForMidToOldPromotion(to_space_addr + bytes_done);
      if (visitor.ShouldDirtyCard()) {
        card_table->MarkCard(reinterpret_cast<mirror::Object*>(to_space_addr + bytes_done));
      }
    }
    obj_size = RoundUp(obj_size, kAlignment);
    DCHECK_GT(obj_size, 0u)
        << "from_addr:" << obj
        << " from-space-class:" << obj->GetClass<kDefaultVerifyFlags, kWithFromSpaceBarrier>()
        << " to_addr:" << ref
        << " black-allocation-begin:" << reinterpret_cast<void*>(black_allocations_begin_)
        << " post-compact-end:" << reinterpret_cast<void*>(post_compact_end_)
        << " offset:" << offset * kAlignment << " bytes_done:" << bytes_done
        << " class-after-obj-iter:"
        << (class_after_obj_iter_ != class_after_obj_map_.rend() ?
                class_after_obj_iter_->first.AsMirrorPtr() :
                nullptr)
        << " last-reclaimed-page:" << reinterpret_cast<void*>(last_reclaimed_page_)
        << " last-checked-reclaim-page-idx:" << last_checked_reclaim_page_idx_
        << " offset-of-last-idx:"
        << pre_compact_offset_moving_space_[last_checked_reclaim_page_idx_] * kAlignment
        << " first-obj-of-last-idx:"
        << first_objs_moving_space_[last_checked_reclaim_page_idx_].AsMirrorPtr();

    from_addr += obj_size;
    bytes_done += obj_size;
  }
  // The last page that we compact may have some bytes left untouched in the
  // end, we should zero them as the kernel copies at page granularity.
  if (needs_memset_zero && UNLIKELY(bytes_done < gPageSize)) {
    std::memset(addr + bytes_done, 0x0, gPageSize - bytes_done);
  }
}

// We store the starting point (pre_compact_page - first_obj) and first-chunk's
// size. If more TLAB(s) started in this page, then those chunks are identified
// using mark bitmap. All this info is prepared in UpdateMovingSpaceBlackAllocations().
// If we find a set bit in the bitmap, then we copy the remaining page and then
// use the bitmap to visit each object for updating references.
void MarkCompact::SlideBlackPage(mirror::Object* first_obj,
                                 mirror::Object* next_page_first_obj,
                                 uint32_t first_chunk_size,
                                 uint8_t* const pre_compact_page,
                                 uint8_t* dest,
                                 bool needs_memset_zero) {
  DCHECK(IsAlignedParam(pre_compact_page, gPageSize));
  size_t bytes_copied;
  uint8_t* src_addr = reinterpret_cast<uint8_t*>(GetFromSpaceAddr(first_obj));
  uint8_t* pre_compact_addr = reinterpret_cast<uint8_t*>(first_obj);
  uint8_t* const pre_compact_page_end = pre_compact_page + gPageSize;
  uint8_t* const dest_page_end = dest + gPageSize;

  auto verify_obj_callback = [&] (std::ostream& os) {
                               os << " first_obj=" << first_obj
                                  << " next_page_first_obj=" << next_page_first_obj
                                  << " first_chunk_sie=" << first_chunk_size
                                  << " dest=" << static_cast<void*>(dest)
                                  << " pre_compact_page="
                                  << static_cast<void* const>(pre_compact_page);
                             };
  // We have empty portion at the beginning of the page. Zero it.
  if (pre_compact_addr > pre_compact_page) {
    bytes_copied = pre_compact_addr - pre_compact_page;
    DCHECK_LT(bytes_copied, gPageSize);
    if (needs_memset_zero) {
      std::memset(dest, 0x0, bytes_copied);
    }
    dest += bytes_copied;
  } else {
    bytes_copied = 0;
    size_t offset = pre_compact_page - pre_compact_addr;
    pre_compact_addr = pre_compact_page;
    src_addr += offset;
    DCHECK(IsAlignedParam(src_addr, gPageSize));
  }
  // Copy the first chunk of live words
  std::memcpy(dest, src_addr, first_chunk_size);
  // Update references in the first chunk. Use object size to find next object.
  {
    size_t bytes_to_visit = first_chunk_size;
    size_t obj_size;
    // The first object started in some previous page. So we need to check the
    // beginning.
    DCHECK_LE(reinterpret_cast<uint8_t*>(first_obj), pre_compact_addr);
    size_t offset = pre_compact_addr - reinterpret_cast<uint8_t*>(first_obj);
    if (bytes_copied == 0 && offset > 0) {
      mirror::Object* to_obj = reinterpret_cast<mirror::Object*>(dest - offset);
      mirror::Object* from_obj = reinterpret_cast<mirror::Object*>(src_addr - offset);
      // If the next page's first-obj is in this page or nullptr, then we don't
      // need to check end boundary
      if (next_page_first_obj == nullptr
          || (first_obj != next_page_first_obj
              && reinterpret_cast<uint8_t*>(next_page_first_obj) <= pre_compact_page_end)) {
        RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/false> visitor(this,
                                                                           to_obj,
                                                                           dest,
                                                                           nullptr);
        obj_size = from_obj->VisitRefsForCompaction<
                /*kFetchObjSize*/true, /*kVisitNativeRoots*/false>(visitor,
                                                                   MemberOffset(offset),
                                                                   MemberOffset(-1));
      } else {
        RefsUpdateVisitor</*kCheckBegin*/true, /*kCheckEnd*/true> visitor(this,
                                                                          to_obj,
                                                                          dest,
                                                                          dest_page_end);
        obj_size = from_obj->VisitRefsForCompaction<
                /*kFetchObjSize*/true, /*kVisitNativeRoots*/false>(visitor,
                                                                   MemberOffset(offset),
                                                                   MemberOffset(offset
                                                                                + gPageSize));
        if (first_obj == next_page_first_obj) {
          // First object is the only object on this page. So there's nothing else left to do.
          return;
        }
      }
      obj_size = RoundUp(obj_size, kAlignment);
      obj_size -= offset;
      dest += obj_size;
      bytes_to_visit -= obj_size;
    }
    bytes_copied += first_chunk_size;
    // If the last object in this page is next_page_first_obj, then we need to check end boundary
    bool check_last_obj = false;
    if (next_page_first_obj != nullptr
        && reinterpret_cast<uint8_t*>(next_page_first_obj) < pre_compact_page_end
        && bytes_copied == gPageSize) {
      size_t diff = pre_compact_page_end - reinterpret_cast<uint8_t*>(next_page_first_obj);
      DCHECK_LE(diff, gPageSize);
      DCHECK_LE(diff, bytes_to_visit);
      bytes_to_visit -= diff;
      check_last_obj = true;
    }
    while (bytes_to_visit > 0) {
      mirror::Object* dest_obj = reinterpret_cast<mirror::Object*>(dest);
      VerifyObject(dest_obj, verify_obj_callback);
      RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false> visitor(this,
                                                                          dest_obj,
                                                                          nullptr,
                                                                          nullptr);
      obj_size = dest_obj->VisitRefsForCompaction(visitor, MemberOffset(0), MemberOffset(-1));
      obj_size = RoundUp(obj_size, kAlignment);
      bytes_to_visit -= obj_size;
      dest += obj_size;
    }
    DCHECK_EQ(bytes_to_visit, 0u);
    if (check_last_obj) {
      mirror::Object* dest_obj = reinterpret_cast<mirror::Object*>(dest);
      VerifyObject(dest_obj, verify_obj_callback);
      RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/true> visitor(this,
                                                                         dest_obj,
                                                                         nullptr,
                                                                         dest_page_end);
      mirror::Object* obj = GetFromSpaceAddr(next_page_first_obj);
      obj->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                          MemberOffset(0),
                                                          MemberOffset(dest_page_end - dest));
      return;
    }
  }

  // Probably a TLAB finished on this page and/or a new TLAB started as well.
  if (bytes_copied < gPageSize) {
    src_addr += first_chunk_size;
    pre_compact_addr += first_chunk_size;
    // Use mark-bitmap to identify where objects are. First call
    // VisitMarkedRange for only the first marked bit. If found, zero all bytes
    // until that object and then call memcpy on the rest of the page.
    // Then call VisitMarkedRange for all marked bits *after* the one found in
    // this invocation. This time to visit references.
    uintptr_t start_visit = reinterpret_cast<uintptr_t>(pre_compact_addr);
    uintptr_t page_end = reinterpret_cast<uintptr_t>(pre_compact_page_end);
    mirror::Object* found_obj = nullptr;
    moving_space_bitmap_->VisitMarkedRange</*kVisitOnce*/true>(start_visit,
                                                                page_end,
                                                                [&found_obj](mirror::Object* obj) {
                                                                  found_obj = obj;
                                                                });
    size_t remaining_bytes = gPageSize - bytes_copied;
    if (found_obj == nullptr) {
      if (needs_memset_zero) {
        // No more black objects in this page. Zero the remaining bytes and return.
        std::memset(dest, 0x0, remaining_bytes);
      }
      return;
    }
    // Copy everything in this page, which includes any zeroed regions
    // in-between.
    std::memcpy(dest, src_addr, remaining_bytes);
    DCHECK_LT(reinterpret_cast<uintptr_t>(found_obj), page_end);
    moving_space_bitmap_->VisitMarkedRange(
            reinterpret_cast<uintptr_t>(found_obj) + mirror::kObjectHeaderSize,
            page_end,
            [&found_obj, pre_compact_addr, dest, this, verify_obj_callback] (mirror::Object* obj)
            REQUIRES_SHARED(Locks::mutator_lock_) {
              ptrdiff_t diff = reinterpret_cast<uint8_t*>(found_obj) - pre_compact_addr;
              mirror::Object* ref = reinterpret_cast<mirror::Object*>(dest + diff);
              VerifyObject(ref, verify_obj_callback);
              RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false>
                      visitor(this, ref, nullptr, nullptr);
              ref->VisitRefsForCompaction</*kFetchObjSize*/false>(visitor,
                                                                  MemberOffset(0),
                                                                  MemberOffset(-1));
              // Remember for next round.
              found_obj = obj;
            });
    // found_obj may have been updated in VisitMarkedRange. Visit the last found
    // object.
    DCHECK_GT(reinterpret_cast<uint8_t*>(found_obj), pre_compact_addr);
    DCHECK_LT(reinterpret_cast<uintptr_t>(found_obj), page_end);
    ptrdiff_t diff = reinterpret_cast<uint8_t*>(found_obj) - pre_compact_addr;
    mirror::Object* dest_obj = reinterpret_cast<mirror::Object*>(dest + diff);
    VerifyObject(dest_obj, verify_obj_callback);
    RefsUpdateVisitor</*kCheckBegin*/ false, /*kCheckEnd*/ true> visitor(
        this, dest_obj, nullptr, dest_page_end);
    // Last object could overlap with next page. And if it happens to be a
    // class, then we may access something (like static-fields' offsets) which
    // is on the next page. Therefore, use from-space's reference.
    mirror::Object* obj = GetFromSpaceAddr(found_obj);
    obj->VisitRefsForCompaction</*kFetchObjSize*/ false>(
        visitor, MemberOffset(0), MemberOffset(page_end - reinterpret_cast<uintptr_t>(found_obj)));
  }
}

template <uint32_t kYieldMax = 5, uint64_t kSleepUs = 10>
static void BackOff(uint32_t i) {
  // TODO: Consider adding x86 PAUSE and/or ARM YIELD here.
  if (i <= kYieldMax) {
    sched_yield();
  } else {
    // nanosleep is not in the async-signal-safe list, but bionic implements it
    // with a pure system call, so it should be fine.
    NanoSleep(kSleepUs * 1000 * (i - kYieldMax));
  }
}

size_t MarkCompact::ZeropageIoctl(void* addr,
                                  size_t length,
                                  bool tolerate_eexist,
                                  bool tolerate_enoent) {
  int32_t backoff_count = -1;
  int32_t max_backoff = 10;  // max native priority.
  struct uffdio_zeropage uffd_zeropage;
  DCHECK(IsAlignedParam(addr, gPageSize));
  uffd_zeropage.range.start = reinterpret_cast<uintptr_t>(addr);
  uffd_zeropage.range.len = length;
  uffd_zeropage.mode = gUffdSupportsMmapTrylock ? UFFDIO_ZEROPAGE_MODE_MMAP_TRYLOCK : 0;
  while (true) {
    uffd_zeropage.zeropage = 0;
    int ret = ioctl(uffd_, UFFDIO_ZEROPAGE, &uffd_zeropage);
    if (ret == 0) {
      DCHECK_EQ(uffd_zeropage.zeropage, static_cast<ssize_t>(length));
      return length;
    } else if (errno == EAGAIN) {
      if (uffd_zeropage.zeropage > 0) {
        // Contention was observed after acquiring mmap_lock. But the first page
        // is already done, which is what we care about.
        DCHECK(IsAlignedParam(uffd_zeropage.zeropage, gPageSize));
        DCHECK_GE(uffd_zeropage.zeropage, static_cast<ssize_t>(gPageSize));
        return uffd_zeropage.zeropage;
      } else if (uffd_zeropage.zeropage < 0) {
        // mmap_read_trylock() failed due to contention. Back-off and retry.
        DCHECK_EQ(uffd_zeropage.zeropage, -EAGAIN);
        if (backoff_count == -1) {
          int prio = Thread::Current()->GetNativePriority();
          DCHECK(prio > 0 && prio <= 10) << prio;
          max_backoff -= prio;
          backoff_count = 0;
        }
        if (backoff_count < max_backoff) {
          // Using 3 to align 'normal' priority threads with sleep.
          BackOff</*kYieldMax=*/3, /*kSleepUs=*/1000>(backoff_count++);
        } else {
          uffd_zeropage.mode = 0;
        }
      }
    } else if (tolerate_eexist && errno == EEXIST) {
      // Ioctl returns the number of bytes it mapped. The page on which EEXIST occurred
      // wouldn't be included in it.
      return uffd_zeropage.zeropage > 0 ? uffd_zeropage.zeropage + gPageSize : gPageSize;
    } else {
      CHECK(tolerate_enoent && errno == ENOENT)
          << "ioctl_userfaultfd: zeropage failed: " << strerror(errno) << ". addr:" << addr;
      return 0;
    }
  }
}

size_t MarkCompact::CopyIoctl(
    void* dst, void* buffer, size_t length, bool return_on_contention, bool tolerate_enoent) {
  int32_t backoff_count = -1;
  int32_t max_backoff = 10;  // max native priority.
  struct uffdio_copy uffd_copy;
  uffd_copy.mode = gUffdSupportsMmapTrylock ? UFFDIO_COPY_MODE_MMAP_TRYLOCK : 0;
  uffd_copy.src = reinterpret_cast<uintptr_t>(buffer);
  uffd_copy.dst = reinterpret_cast<uintptr_t>(dst);
  uffd_copy.len = length;
  uffd_copy.copy = 0;
  while (true) {
    int ret = ioctl(uffd_, UFFDIO_COPY, &uffd_copy);
    if (ret == 0) {
      DCHECK_EQ(uffd_copy.copy, static_cast<ssize_t>(length));
      break;
    } else if (errno == EAGAIN) {
      // Contention observed.
      DCHECK_NE(uffd_copy.copy, 0);
      if (uffd_copy.copy > 0) {
        // Contention was observed after acquiring mmap_lock.
        DCHECK(IsAlignedParam(uffd_copy.copy, gPageSize));
        DCHECK_GE(uffd_copy.copy, static_cast<ssize_t>(gPageSize));
        break;
      } else {
        // mmap_read_trylock() failed due to contention.
        DCHECK_EQ(uffd_copy.copy, -EAGAIN);
        uffd_copy.copy = 0;
        if (return_on_contention) {
          break;
        }
      }
      if (backoff_count == -1) {
        int prio = Thread::Current()->GetNativePriority();
        DCHECK(prio > 0 && prio <= 10) << prio;
        max_backoff -= prio;
        backoff_count = 0;
      }
      if (backoff_count < max_backoff) {
        // Using 3 to align 'normal' priority threads with sleep.
        BackOff</*kYieldMax=*/3, /*kSleepUs=*/1000>(backoff_count++);
      } else {
        uffd_copy.mode = 0;
      }
    } else if (errno == EEXIST) {
      DCHECK_NE(uffd_copy.copy, 0);
      if (uffd_copy.copy < 0) {
        uffd_copy.copy = 0;
      }
      // Ioctl returns the number of bytes it mapped. The page on which EEXIST occurred
      // wouldn't be included in it.
      uffd_copy.copy += gPageSize;
      break;
    } else {
      CHECK(tolerate_enoent && errno == ENOENT)
          << "ioctl_userfaultfd: copy failed: " << strerror(errno) << ". src:" << buffer
          << " dst:" << dst;
      return uffd_copy.copy > 0 ? uffd_copy.copy : 0;
    }
  }
  return uffd_copy.copy;
}

template <int kMode, typename CompactionFn>
bool MarkCompact::DoPageCompactionWithStateChange(size_t page_idx,
                                                  uint8_t* to_space_page,
                                                  uint8_t* page,
                                                  bool map_immediately,
                                                  CompactionFn func) {
  uint32_t expected_state = static_cast<uint8_t>(PageState::kUnprocessed);
  uint32_t desired_state = static_cast<uint8_t>(map_immediately ? PageState::kProcessingAndMapping :
                                                                  PageState::kProcessing);
  // In the concurrent case (kMode != kFallbackMode) we need to ensure that the update
  // to moving_spaces_status_[page_idx] is released before the contents of the page are
  // made accessible to other threads.
  //
  // We need acquire ordering here to ensure that when the CAS fails, another thread
  // has completed processing the page, which is guaranteed by the release below.
  if (kMode == kFallbackMode || moving_pages_status_[page_idx].compare_exchange_strong(
                                    expected_state, desired_state, std::memory_order_acquire)) {
    func();
    if (kMode == kCopyMode) {
      if (map_immediately) {
        CopyIoctl(to_space_page,
                  page,
                  gPageSize,
                  /*return_on_contention=*/false,
                  /*tolerate_enoent=*/false);
        // Store is sufficient as no other thread could modify the status at this
        // point. Relaxed order is sufficient as the ioctl will act as a fence.
        moving_pages_status_[page_idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapped),
                                             std::memory_order_relaxed);
      } else {
        // Add the src page's index in the status word.
        DCHECK(from_space_map_.HasAddress(page));
        DCHECK_LE(static_cast<size_t>(page - from_space_begin_),
                  std::numeric_limits<uint32_t>::max());
        uint32_t store_val = page - from_space_begin_;
        DCHECK_EQ(store_val & kPageStateMask, 0u);
        store_val |= static_cast<uint8_t>(PageState::kProcessed);
        // Store is sufficient as no other thread would modify the status at this point.
        moving_pages_status_[page_idx].store(store_val, std::memory_order_release);
      }
    }
    return true;
  } else {
    // Only GC thread could have set the state to Processed.
    DCHECK_NE(expected_state, static_cast<uint8_t>(PageState::kProcessed));
    return false;
  }
}

bool MarkCompact::FreeFromSpacePages(size_t cur_page_idx, int mode, size_t end_idx_for_mapping) {
  // Thanks to sliding compaction, bump-pointer allocations, and reverse
  // compaction (see CompactMovingSpace) the logic here is pretty simple: find
  // the to-space page up to which compaction has finished, all the from-space
  // pages corresponding to this onwards can be freed. There are some corner
  // cases to be taken care of, which are described below.
  size_t idx = last_checked_reclaim_page_idx_;
  // Find the to-space page up to which the corresponding from-space pages can be
  // freed.
  for (; idx > cur_page_idx; idx--) {
    PageState state = GetMovingPageState(idx - 1);
    if (state == PageState::kMutatorProcessing) {
      // Some mutator is working on the page.
      break;
    }
    DCHECK(state >= PageState::kProcessed ||
           (state == PageState::kUnprocessed &&
            (mode == kFallbackMode || idx > moving_first_objs_count_)));
  }
  DCHECK_LE(idx, last_checked_reclaim_page_idx_);
  if (idx == last_checked_reclaim_page_idx_) {
    // Nothing to do.
    return false;
  }

  uint8_t* reclaim_begin;
  uint8_t* idx_addr;
  // Calculate the first from-space page to be freed using 'idx'. If the
  // first-object of the idx'th to-space page started before the corresponding
  // from-space page, which is almost always the case in the compaction portion
  // of the moving-space, then it indicates that the subsequent pages that are
  // yet to be compacted will need the from-space pages. Therefore, find the page
  // (from the already compacted pages) whose first-object is different from
  // ours. All the from-space pages starting from that one are safe to be
  // removed. Please note that this iteration is not expected to be long in
  // normal cases as objects are smaller than page size.
  if (idx >= moving_first_objs_count_) {
    // black-allocated portion of the moving-space
    idx_addr = black_allocations_begin_ + (idx - moving_first_objs_count_) * gPageSize;
    reclaim_begin = idx_addr;
    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    if (first_obj != nullptr && reinterpret_cast<uint8_t*>(first_obj) < reclaim_begin) {
      size_t idx_len = moving_first_objs_count_ + black_page_count_;
      for (size_t i = idx + 1; i < idx_len; i++) {
        mirror::Object* obj = first_objs_moving_space_[i].AsMirrorPtr();
        // A null first-object indicates that the corresponding to-space page is
        // not used yet. So we can compute its from-space page and use that.
        if (obj != first_obj) {
          reclaim_begin = obj != nullptr
                          ? AlignUp(reinterpret_cast<uint8_t*>(obj), gPageSize)
                          : (black_allocations_begin_ + (i - moving_first_objs_count_) * gPageSize);
          break;
        }
      }
    }
  } else {
    DCHECK_GE(pre_compact_offset_moving_space_[idx], 0u);
    idx_addr = moving_space_begin_ + idx * gPageSize;
    if (idx_addr >= black_dense_end_) {
      idx_addr = moving_space_begin_ + pre_compact_offset_moving_space_[idx] * kAlignment;
    }
    reclaim_begin = idx_addr;
    DCHECK_LE(reclaim_begin, black_allocations_begin_);
    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    if (first_obj != nullptr) {
      if (reinterpret_cast<uint8_t*>(first_obj) < reclaim_begin) {
        DCHECK_LT(idx, moving_first_objs_count_);
        mirror::Object* obj = first_obj;
        for (size_t i = idx + 1; i < moving_first_objs_count_; i++) {
          obj = first_objs_moving_space_[i].AsMirrorPtr();
          if (obj == nullptr) {
            reclaim_begin = moving_space_begin_ + i * gPageSize;
            break;
          } else if (first_obj != obj) {
            DCHECK_LT(first_obj, obj);
            DCHECK_LT(reclaim_begin, reinterpret_cast<uint8_t*>(obj));
            reclaim_begin = reinterpret_cast<uint8_t*>(obj);
            break;
          }
        }
        if (obj == first_obj) {
          reclaim_begin = black_allocations_begin_;
        }
      }
    }
    reclaim_begin = AlignUp(reclaim_begin, gPageSize);
  }

  DCHECK_NE(reclaim_begin, nullptr);
  DCHECK_ALIGNED_PARAM(reclaim_begin, gPageSize);
  DCHECK_ALIGNED_PARAM(last_reclaimed_page_, gPageSize);
  // Check if the 'class_after_obj_map_' map allows pages to be freed.
  for (; class_after_obj_iter_ != class_after_obj_map_.rend(); class_after_obj_iter_++) {
    mirror::Object* klass = class_after_obj_iter_->first.AsMirrorPtr();
    mirror::Class* from_klass = static_cast<mirror::Class*>(GetFromSpaceAddr(klass));
    // Check with class' end to ensure that, if required, the entire class survives.
    uint8_t* klass_end = reinterpret_cast<uint8_t*>(klass) + from_klass->SizeOf<kVerifyNone>();
    DCHECK_LE(klass_end, last_reclaimed_page_);
    if (reinterpret_cast<uint8_t*>(klass_end) >= reclaim_begin) {
      // Found a class which is in the reclaim range.
      if (reinterpret_cast<uint8_t*>(class_after_obj_iter_->second.AsMirrorPtr()) < idx_addr) {
        // Its lowest-address object is not compacted yet. Reclaim starting from
        // the end of this class.
        reclaim_begin = AlignUp(klass_end, gPageSize);
      } else {
        // Continue consuming pairs wherein the lowest address object has already
        // been compacted.
        continue;
      }
    }
    // All the remaining class (and thereby corresponding object) addresses are
    // lower than the reclaim range.
    break;
  }
  bool all_mapped = mode == kFallbackMode;
  ssize_t size = last_reclaimed_page_ - reclaim_begin;
  if (size > kMinFromSpaceMadviseSize) {
    // Map all the pages in the range.
    if (mode == kCopyMode && cur_page_idx < end_idx_for_mapping) {
      if (MapMovingSpacePages(cur_page_idx,
                              end_idx_for_mapping,
                              /*from_ioctl=*/false,
                              /*return_on_contention=*/true,
                              /*tolerate_enoent=*/false) == end_idx_for_mapping - cur_page_idx) {
        all_mapped = true;
      }
    } else {
      // This for the black-allocations pages so that madvise is not missed.
      all_mapped = true;
    }
    // If not all pages are mapped, then take it as a hint that mmap_lock is
    // contended and hence don't madvise as that also needs the same lock.
    if (all_mapped) {
      // Retain a few pages for subsequent compactions.
      const ssize_t gBufferPages = 4 * gPageSize;
      DCHECK_LT(gBufferPages, kMinFromSpaceMadviseSize);
      size -= gBufferPages;
      uint8_t* addr = last_reclaimed_page_ - size;
      CHECK_EQ(madvise(addr + from_space_slide_diff_, size, MADV_DONTNEED), 0)
          << "madvise of from-space failed: " << strerror(errno);
      last_reclaimed_page_ = addr;
      cur_reclaimable_page_ = addr;
    }
  }
  last_reclaimable_page_ = std::min(reclaim_begin, last_reclaimable_page_);
  last_checked_reclaim_page_idx_ = idx;
  return all_mapped;
}

template <int kMode>
void MarkCompact::CompactMovingSpace(uint8_t* page) {
  // For every page we have a starting object, which may have started in some
  // preceding page, and an offset within that object from where we must start
  // copying.
  // Consult the live-words bitmap to copy all contiguously live words at a
  // time. These words may constitute multiple objects. To avoid the need for
  // consulting mark-bitmap to find where does the next live object start, we
  // use the object-size returned by VisitRefsForCompaction.
  //
  // We do the compaction in reverse direction so that the pages containing
  // TLAB and latest allocations are processed first.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  size_t page_status_arr_len = moving_first_objs_count_ + black_page_count_;
  size_t idx = page_status_arr_len;
  size_t black_dense_end_idx = (black_dense_end_ - moving_space_begin_) / gPageSize;
  uint8_t* to_space_end = moving_space_begin_ + page_status_arr_len * gPageSize;
  uint8_t* pre_compact_page = black_allocations_begin_ + (black_page_count_ * gPageSize);

  DCHECK(IsAlignedParam(pre_compact_page, gPageSize));

  // These variables are maintained by FreeFromSpacePages().
  last_reclaimed_page_ = pre_compact_page;
  last_reclaimable_page_ = last_reclaimed_page_;
  cur_reclaimable_page_ = last_reclaimed_page_;
  last_checked_reclaim_page_idx_ = idx;
  class_after_obj_iter_ = class_after_obj_map_.rbegin();
  // Allocated-black pages
  mirror::Object* next_page_first_obj = nullptr;
  while (idx > moving_first_objs_count_) {
    idx--;
    pre_compact_page -= gPageSize;
    to_space_end -= gPageSize;
    if (kMode == kFallbackMode) {
      page = to_space_end;
    }
    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    uint32_t first_chunk_size = black_alloc_pages_first_chunk_size_[idx];
    if (first_obj != nullptr) {
      DoPageCompactionWithStateChange<kMode>(idx,
                                             to_space_end,
                                             page,
                                             /*map_immediately=*/true,
                                             [&]() REQUIRES_SHARED(Locks::mutator_lock_) {
                                               SlideBlackPage(first_obj,
                                                              next_page_first_obj,
                                                              first_chunk_size,
                                                              pre_compact_page,
                                                              page,
                                                              kMode == kCopyMode);
                                             });
      // We are sliding here, so no point attempting to madvise for every
      // page. Wait for enough pages to be done.
      if (idx % DivideByPageSize(kMinFromSpaceMadviseSize) == 0) {
        FreeFromSpacePages(idx, kMode, /*end_idx_for_mapping=*/0);
      }
    }
    next_page_first_obj = first_obj;
  }
  DCHECK_EQ(pre_compact_page, black_allocations_begin_);
  // Reserved page to be used if we can't find any reclaimable page for processing.
  uint8_t* reserve_page = page;
  size_t end_idx_for_mapping = idx;
  while (idx > black_dense_end_idx) {
    idx--;
    to_space_end -= gPageSize;
    if (kMode == kFallbackMode) {
      page = to_space_end;
    } else {
      DCHECK_EQ(kMode, kCopyMode);
      if (cur_reclaimable_page_ > last_reclaimable_page_) {
        cur_reclaimable_page_ -= gPageSize;
        page = cur_reclaimable_page_ + from_space_slide_diff_;
      } else {
        page = reserve_page;
      }
    }
    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    bool success = DoPageCompactionWithStateChange<kMode>(
        idx,
        to_space_end,
        page,
        /*map_immediately=*/page == reserve_page,
        [&]() REQUIRES_SHARED(Locks::mutator_lock_) {
          if (use_generational_ && to_space_end < mid_gen_end_) {
            CompactPage</*kSetupForGenerational=*/true>(first_obj,
                                                        pre_compact_offset_moving_space_[idx],
                                                        page,
                                                        to_space_end,
                                                        kMode == kCopyMode);
          } else {
            CompactPage</*kSetupForGenerational=*/false>(first_obj,
                                                         pre_compact_offset_moving_space_[idx],
                                                         page,
                                                         to_space_end,
                                                         kMode == kCopyMode);
          }
        });
    if (kMode == kCopyMode && (!success || page == reserve_page) && end_idx_for_mapping - idx > 1) {
      // map the pages in the following address as they can't be mapped with the
      // pages yet-to-be-compacted as their src-side pages won't be contiguous.
      MapMovingSpacePages(idx + 1,
                          end_idx_for_mapping,
                          /*from_fault=*/false,
                          /*return_on_contention=*/true,
                          /*tolerate_enoent=*/false);
    }
    if (FreeFromSpacePages(idx, kMode, end_idx_for_mapping)) {
      end_idx_for_mapping = idx;
    }
  }
  while (idx > 0) {
    idx--;
    to_space_end -= gPageSize;
    mirror::Object* first_obj = first_objs_moving_space_[idx].AsMirrorPtr();
    if (first_obj != nullptr) {
      DoPageCompactionWithStateChange<kMode>(
          idx,
          to_space_end,
          to_space_end + from_space_slide_diff_,
          /*map_immediately=*/false,
          [&]() REQUIRES_SHARED(Locks::mutator_lock_) {
            if (use_generational_) {
              UpdateNonMovingPage</*kSetupForGenerational=*/true>(
                  first_obj, to_space_end, from_space_slide_diff_, moving_space_bitmap_);
            } else {
              UpdateNonMovingPage</*kSetupForGenerational=*/false>(
                  first_obj, to_space_end, from_space_slide_diff_, moving_space_bitmap_);
            }
            if (kMode == kFallbackMode) {
              memcpy(to_space_end, to_space_end + from_space_slide_diff_, gPageSize);
            }
          });
    } else {
      // The page has no reachable object on it. Just declare it mapped.
      // Mutators shouldn't step on this page, which is asserted in sigbus
      // handler.
      DCHECK_EQ(moving_pages_status_[idx].load(std::memory_order_relaxed),
                static_cast<uint8_t>(PageState::kUnprocessed));
      moving_pages_status_[idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapped),
                                      std::memory_order_release);
    }
    if (FreeFromSpacePages(idx, kMode, end_idx_for_mapping)) {
      end_idx_for_mapping = idx;
    }
  }
  // map one last time to finish anything left.
  if (kMode == kCopyMode && end_idx_for_mapping > 0) {
    MapMovingSpacePages(idx,
                        end_idx_for_mapping,
                        /*from_fault=*/false,
                        /*return_on_contention=*/false,
                        /*tolerate_enoent=*/false);
  }
  DCHECK_EQ(to_space_end, bump_pointer_space_->Begin());
}

size_t MarkCompact::MapMovingSpacePages(size_t start_idx,
                                        size_t arr_len,
                                        bool from_fault,
                                        bool return_on_contention,
                                        bool tolerate_enoent) {
  DCHECK_LT(start_idx, arr_len);
  size_t arr_idx = start_idx;
  bool wait_for_unmapped = false;
  while (arr_idx < arr_len) {
    size_t map_count = 0;
    uint32_t cur_state = moving_pages_status_[arr_idx].load(std::memory_order_acquire);
    // Find a contiguous range that can be mapped with single ioctl.
    for (uint32_t i = arr_idx, from_page = cur_state & ~kPageStateMask; i < arr_len;
         i++, map_count++, from_page += gPageSize) {
      uint32_t s = moving_pages_status_[i].load(std::memory_order_acquire);
      uint32_t cur_from_page = s & ~kPageStateMask;
      if (GetPageStateFromWord(s) != PageState::kProcessed || cur_from_page != from_page) {
        break;
      }
    }

    if (map_count == 0) {
      if (from_fault) {
        bool mapped = GetPageStateFromWord(cur_state) == PageState::kProcessedAndMapped;
        return mapped ? 1 : 0;
      }
      // Skip the pages that this thread cannot map.
      for (; arr_idx < arr_len; arr_idx++) {
        PageState s = GetMovingPageState(arr_idx);
        if (s == PageState::kProcessed) {
          break;
        } else if (s != PageState::kProcessedAndMapped) {
          wait_for_unmapped = true;
        }
      }
    } else {
      uint32_t from_space_offset = cur_state & ~kPageStateMask;
      uint8_t* to_space_start = moving_space_begin_ + arr_idx * gPageSize;
      uint8_t* from_space_start = from_space_begin_ + from_space_offset;
      DCHECK_ALIGNED_PARAM(to_space_start, gPageSize);
      DCHECK_ALIGNED_PARAM(from_space_start, gPageSize);
      size_t mapped_len = CopyIoctl(to_space_start,
                                    from_space_start,
                                    map_count * gPageSize,
                                    return_on_contention,
                                    tolerate_enoent);
      for (size_t l = 0; l < mapped_len; l += gPageSize, arr_idx++) {
        // Store is sufficient as anyone storing is doing it with the same value.
        moving_pages_status_[arr_idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapped),
                                            std::memory_order_release);
      }
      if (from_fault) {
        return DivideByPageSize(mapped_len);
      }
      // We can return from COPY ioctl with a smaller length also if a page
      // was found to be already mapped. But that doesn't count as contention.
      if (return_on_contention && DivideByPageSize(mapped_len) < map_count && errno != EEXIST) {
        return arr_idx - start_idx;
      }
    }
  }
  if (wait_for_unmapped) {
    for (size_t i = start_idx; i < arr_len; i++) {
      PageState s = GetMovingPageState(i);
      DCHECK_GT(s, PageState::kProcessed);
      uint32_t backoff_count = 0;
      while (s != PageState::kProcessedAndMapped) {
        BackOff(backoff_count++);
        s = GetMovingPageState(i);
      }
    }
  }
  return arr_len - start_idx;
}

template <bool kSetupForGenerational>
void MarkCompact::UpdateNonMovingPage(mirror::Object* first,
                                      uint8_t* page,
                                      ptrdiff_t from_space_diff,
                                      accounting::ContinuousSpaceBitmap* bitmap) {
  DCHECK_LT(reinterpret_cast<uint8_t*>(first), page + gPageSize);
  accounting::CardTable* card_table = heap_->GetCardTable();
  mirror::Object* curr_obj = first;
  uint8_t* from_page = page + from_space_diff;
  uint8_t* from_page_end = from_page + gPageSize;
  uint8_t* scan_begin =
      std::max(reinterpret_cast<uint8_t*>(first) + mirror::kObjectHeaderSize, page);
  // For every object found in the page, visit the previous object. This ensures
  // that we can visit without checking page-end boundary.
  // Call VisitRefsForCompaction with from-space read-barrier as the klass object and
  // super-class loads require it.
  // TODO: Set kVisitNativeRoots to false once we implement concurrent
  // compaction
  auto obj_visitor = [&](mirror::Object* next_obj) {
    if (curr_obj != nullptr) {
      mirror::Object* from_obj =
          reinterpret_cast<mirror::Object*>(reinterpret_cast<uint8_t*>(curr_obj) + from_space_diff);
      bool should_dirty_card;
      if (reinterpret_cast<uint8_t*>(curr_obj) < page) {
        RefsUpdateVisitor</*kCheckBegin*/ true, /*kCheckEnd*/ false, kSetupForGenerational> visitor(
            this, from_obj, from_page, from_page_end, card_table, curr_obj);
        MemberOffset begin_offset(page - reinterpret_cast<uint8_t*>(curr_obj));
        // Native roots shouldn't be visited as they are done when this
        // object's beginning was visited in the preceding page.
        from_obj->VisitRefsForCompaction</*kFetchObjSize*/ false, /*kVisitNativeRoots*/ false>(
            visitor, begin_offset, MemberOffset(-1));
        should_dirty_card = visitor.ShouldDirtyCard();
      } else {
        RefsUpdateVisitor</*kCheckBegin*/ false, /*kCheckEnd*/ false, kSetupForGenerational>
            visitor(this, from_obj, from_page, from_page_end, card_table, curr_obj);
        from_obj->VisitRefsForCompaction</*kFetchObjSize*/ false>(
            visitor, MemberOffset(0), MemberOffset(-1));
        should_dirty_card = visitor.ShouldDirtyCard();
      }
      if (kSetupForGenerational && should_dirty_card) {
        card_table->MarkCard(curr_obj);
      }
    }
    curr_obj = next_obj;
  };

  if (young_gen_) {
    DCHECK(bitmap->Test(first));
    // If the first-obj is covered by the same card which also covers the first
    // word of the page, then it's important to set curr_obj to nullptr to avoid
    // updating the references twice.
    if (card_table->IsClean(first) ||
        card_table->CardFromAddr(first) == card_table->CardFromAddr(scan_begin)) {
      curr_obj = nullptr;
    }
    // We cannot acquire heap-bitmap-lock here as this function is called from
    // SIGBUS handler. But it's safe as the bitmap passed to Scan function
    // can't get modified until this GC cycle is finished.
    FakeMutexLock mu(*Locks::heap_bitmap_lock_);
    card_table->Scan</*kClearCard=*/false>(
        bitmap, scan_begin, page + gPageSize, obj_visitor, accounting::CardTable::kCardAged2);
  } else {
    bitmap->VisitMarkedRange(reinterpret_cast<uintptr_t>(scan_begin),
                             reinterpret_cast<uintptr_t>(page + gPageSize),
                             obj_visitor);
  }

  if (curr_obj != nullptr) {
    bool should_dirty_card;
    mirror::Object* from_obj =
        reinterpret_cast<mirror::Object*>(reinterpret_cast<uint8_t*>(curr_obj) + from_space_diff);
    MemberOffset end_offset(page + gPageSize - reinterpret_cast<uint8_t*>(curr_obj));
    if (reinterpret_cast<uint8_t*>(curr_obj) < page) {
      RefsUpdateVisitor</*kCheckBegin*/ true, /*kCheckEnd*/ true, kSetupForGenerational> visitor(
          this, from_obj, from_page, from_page_end, card_table, curr_obj);
      from_obj->VisitRefsForCompaction</*kFetchObjSize*/ false, /*kVisitNativeRoots*/ false>(
          visitor, MemberOffset(page - reinterpret_cast<uint8_t*>(curr_obj)), end_offset);
      should_dirty_card = visitor.ShouldDirtyCard();
    } else {
      RefsUpdateVisitor</*kCheckBegin*/ false, /*kCheckEnd*/ true, kSetupForGenerational> visitor(
          this, from_obj, from_page, from_page_end, card_table, curr_obj);
      from_obj->VisitRefsForCompaction</*kFetchObjSize*/ false>(
          visitor, MemberOffset(0), end_offset);
      should_dirty_card = visitor.ShouldDirtyCard();
    }
    if (kSetupForGenerational && should_dirty_card) {
      card_table->MarkCard(curr_obj);
    }
  }
}

void MarkCompact::UpdateNonMovingSpace() {
  TimingLogger::ScopedTiming t("(Paused)UpdateNonMovingSpace", GetTimings());
  // Iterating in reverse ensures that the class pointer in objects which span
  // across more than one page gets updated in the end. This is necessary for
  // VisitRefsForCompaction() to work correctly.
  // TODO: If and when we make non-moving space update concurrent, implement a
  // mechanism to remember class pointers for such objects off-heap and pass it
  // to VisitRefsForCompaction().
  uint8_t* page = non_moving_space_->Begin() + non_moving_first_objs_count_ * gPageSize;
  for (ssize_t i = non_moving_first_objs_count_ - 1; i >= 0; i--) {
    mirror::Object* obj = first_objs_non_moving_space_[i].AsMirrorPtr();
    page -= gPageSize;
    // null means there are no objects on the page to update references.
    if (obj != nullptr) {
      if (use_generational_) {
        UpdateNonMovingPage</*kSetupForGenerational=*/true>(
            obj, page, /*from_space_diff=*/0, non_moving_space_bitmap_);
      } else {
        UpdateNonMovingPage</*kSetupForGenerational=*/false>(
            obj, page, /*from_space_diff=*/0, non_moving_space_bitmap_);
      }
    }
  }
}

void MarkCompact::UpdateMovingSpaceBlackAllocations() {
  // For sliding black pages, we need the first-object, which overlaps with the
  // first byte of the page. Additionally, we compute the size of first chunk of
  // black objects. This will suffice for most black pages. Unlike, compaction
  // pages, here we don't need to pre-compute the offset within first-obj from
  // where sliding has to start. That can be calculated using the pre-compact
  // address of the page. Therefore, to save space, we store the first chunk's
  // size in black_alloc_pages_first_chunk_size_ array.
  // For the pages which may have holes after the first chunk, which could happen
  // if a new TLAB starts in the middle of the page, we mark the objects in
  // the mark-bitmap. So, if the first-chunk size is smaller than gPageSize,
  // then we use the mark-bitmap for the remainder of the page.
  uint8_t* const begin = bump_pointer_space_->Begin();
  uint8_t* black_allocs = black_allocations_begin_;
  DCHECK_LE(begin, black_allocs);
  size_t consumed_blocks_count = 0;
  size_t first_block_size;
  // Needed only for debug at the end of the function. Hopefully compiler will
  // eliminate it otherwise.
  size_t num_blocks = 0;
  // Get the list of all blocks allocated in the bump-pointer space.
  std::vector<size_t>* block_sizes = bump_pointer_space_->GetBlockSizes(thread_running_gc_,
                                                                        &first_block_size);
  DCHECK_LE(first_block_size, (size_t)(black_allocs - begin));
  if (block_sizes != nullptr) {
    size_t black_page_idx = moving_first_objs_count_;
    uint8_t* block_end = begin + first_block_size;
    uint32_t remaining_chunk_size = 0;
    uint32_t first_chunk_size = 0;
    mirror::Object* first_obj = nullptr;
    num_blocks = block_sizes->size();
    for (size_t block_size : *block_sizes) {
      block_end += block_size;
      // Skip the blocks that are prior to the black allocations. These will be
      // merged with the main-block later.
      if (black_allocs >= block_end) {
        consumed_blocks_count++;
        continue;
      }
      mirror::Object* obj = reinterpret_cast<mirror::Object*>(black_allocs);
      bool set_mark_bit = remaining_chunk_size > 0;
      // We don't know how many objects are allocated in the current block. When we hit
      // a null assume it's the end. This works as every block is expected to
      // have objects allocated linearly using bump-pointer.
      // BumpPointerSpace::Walk() also works similarly.
      while (black_allocs < block_end
             && obj->GetClass<kDefaultVerifyFlags, kWithoutReadBarrier>() != nullptr) {
        // Try to keep instructions which access class instance together to
        // avoid reloading the pointer from object.
        size_t obj_size = obj->SizeOf();
        bytes_scanned_ += obj_size;
        obj_size = RoundUp(obj_size, kAlignment);
        UpdateClassAfterObjectMap(obj);
        if (first_obj == nullptr) {
          first_obj = obj;
        }
        // We only need the mark-bitmap in the pages wherein a new TLAB starts in
        // the middle of the page.
        if (set_mark_bit) {
          moving_space_bitmap_->Set(obj);
        }
        // Handle objects which cross page boundary, including objects larger
        // than page size.
        if (remaining_chunk_size + obj_size >= gPageSize) {
          set_mark_bit = false;
          first_chunk_size += gPageSize - remaining_chunk_size;
          remaining_chunk_size += obj_size;
          // We should not store first-object and remaining_chunk_size if there were
          // unused bytes before this TLAB, in which case we must have already
          // stored the values (below).
          if (black_alloc_pages_first_chunk_size_[black_page_idx] == 0) {
            black_alloc_pages_first_chunk_size_[black_page_idx] = first_chunk_size;
            first_objs_moving_space_[black_page_idx].Assign(first_obj);
          }
          black_page_idx++;
          remaining_chunk_size -= gPageSize;
          // Consume an object larger than page size.
          while (remaining_chunk_size >= gPageSize) {
            black_alloc_pages_first_chunk_size_[black_page_idx] = gPageSize;
            first_objs_moving_space_[black_page_idx].Assign(obj);
            black_page_idx++;
            remaining_chunk_size -= gPageSize;
          }
          first_obj = remaining_chunk_size > 0 ? obj : nullptr;
          first_chunk_size = remaining_chunk_size;
        } else {
          DCHECK_LE(first_chunk_size, remaining_chunk_size);
          first_chunk_size += obj_size;
          remaining_chunk_size += obj_size;
        }
        black_allocs += obj_size;
        obj = reinterpret_cast<mirror::Object*>(black_allocs);
      }
      DCHECK_LE(black_allocs, block_end);
      DCHECK_LT(remaining_chunk_size, gPageSize);
      // consume the unallocated portion of the block
      if (black_allocs < block_end) {
        // first-chunk of the current page ends here. Store it.
        if (first_chunk_size > 0 && black_alloc_pages_first_chunk_size_[black_page_idx] == 0) {
          black_alloc_pages_first_chunk_size_[black_page_idx] = first_chunk_size;
          first_objs_moving_space_[black_page_idx].Assign(first_obj);
        }
        first_chunk_size = 0;
        first_obj = nullptr;
        size_t page_remaining = gPageSize - remaining_chunk_size;
        size_t block_remaining = block_end - black_allocs;
        if (page_remaining <= block_remaining) {
          block_remaining -= page_remaining;
          // current page and the subsequent empty pages in the block
          black_page_idx += 1 + DivideByPageSize(block_remaining);
          remaining_chunk_size = ModuloPageSize(block_remaining);
        } else {
          remaining_chunk_size += block_remaining;
        }
        black_allocs = block_end;
      }
    }
    if (black_page_idx < DivideByPageSize(bump_pointer_space_->Size())) {
      // Store the leftover first-chunk, if any, and update page index.
      if (black_alloc_pages_first_chunk_size_[black_page_idx] > 0) {
        black_page_idx++;
      } else if (first_chunk_size > 0) {
        black_alloc_pages_first_chunk_size_[black_page_idx] = first_chunk_size;
        first_objs_moving_space_[black_page_idx].Assign(first_obj);
        black_page_idx++;
      }
    }
    black_page_count_ = black_page_idx - moving_first_objs_count_;
    delete block_sizes;
  }
  // Update bump-pointer space by consuming all the pre-black blocks into the
  // main one.
  bump_pointer_space_->SetBlockSizes(thread_running_gc_,
                                     post_compact_end_ - begin,
                                     consumed_blocks_count);
  if (kIsDebugBuild) {
    size_t moving_space_size = bump_pointer_space_->Size();
    size_t los_size = 0;
    if (heap_->GetLargeObjectsSpace()) {
      los_size = heap_->GetLargeObjectsSpace()->GetBytesAllocated();
    }
    // The moving-space size is already updated to post-compact size in SetBlockSizes above.
    // Also, bytes-allocated has already been adjusted with large-object space' freed-bytes
    // in Sweep(), but not with moving-space freed-bytes.
    CHECK_GE(heap_->GetBytesAllocated() - black_objs_slide_diff_, moving_space_size + los_size)
        << " moving-space size:" << moving_space_size
        << " moving-space bytes-freed:" << black_objs_slide_diff_
        << " large-object-space size:" << los_size
        << " large-object-space bytes-freed:" << GetCurrentIteration()->GetFreedLargeObjectBytes()
        << " num-tlabs-merged:" << consumed_blocks_count
        << " main-block-size:" << (post_compact_end_ - begin)
        << " total-tlabs-moving-space:" << num_blocks;
  }
}

void MarkCompact::UpdateNonMovingSpaceBlackAllocations() {
  accounting::ObjectStack* stack = heap_->GetAllocationStack();
  const StackReference<mirror::Object>* limit = stack->End();
  uint8_t* const space_begin = non_moving_space_->Begin();
  size_t num_pages = DivideByPageSize(non_moving_space_->Capacity());
  for (StackReference<mirror::Object>* it = stack->Begin(); it != limit; ++it) {
    mirror::Object* obj = it->AsMirrorPtr();
    if (obj != nullptr && non_moving_space_bitmap_->HasAddress(obj)) {
      non_moving_space_bitmap_->Set(obj);
      if (!use_generational_) {
        // Clear so that we don't try to set the bit again in the next GC-cycle.
        it->Clear();
      }
      size_t idx = DivideByPageSize(reinterpret_cast<uint8_t*>(obj) - space_begin);
      uint8_t* page_begin = AlignDown(reinterpret_cast<uint8_t*>(obj), gPageSize);
      mirror::Object* first_obj = first_objs_non_moving_space_[idx].AsMirrorPtr();
      if (first_obj == nullptr
          || (obj < first_obj && reinterpret_cast<uint8_t*>(first_obj) > page_begin)) {
        first_objs_non_moving_space_[idx].Assign(obj);
      }
      if (++idx == num_pages) {
        continue;
      }
      mirror::Object* next_page_first_obj = first_objs_non_moving_space_[idx].AsMirrorPtr();
      uint8_t* next_page_begin = page_begin + gPageSize;
      if (next_page_first_obj == nullptr
          || reinterpret_cast<uint8_t*>(next_page_first_obj) > next_page_begin) {
        size_t obj_size = RoundUp(obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
        uint8_t* obj_end = reinterpret_cast<uint8_t*>(obj) + obj_size;
        while (next_page_begin < obj_end) {
          first_objs_non_moving_space_[idx++].Assign(obj);
          next_page_begin += gPageSize;
        }
      }
      // update first_objs count in case we went past non_moving_first_objs_count_
      non_moving_first_objs_count_ = std::max(non_moving_first_objs_count_, idx);
    }
  }
}

class MarkCompact::ImmuneSpaceUpdateObjVisitor {
 public:
  explicit ImmuneSpaceUpdateObjVisitor(MarkCompact* collector) : collector_(collector) {}

  void operator()(mirror::Object* obj) const ALWAYS_INLINE REQUIRES(Locks::mutator_lock_) {
    RefsUpdateVisitor</*kCheckBegin*/false, /*kCheckEnd*/false> visitor(collector_,
                                                                        obj,
                                                                        /*begin_*/nullptr,
                                                                        /*end_*/nullptr);
    obj->VisitRefsForCompaction</*kFetchObjSize*/ false>(
        visitor, MemberOffset(0), MemberOffset(-1));
  }

  static void Callback(mirror::Object* obj, void* arg) REQUIRES(Locks::mutator_lock_) {
    reinterpret_cast<ImmuneSpaceUpdateObjVisitor*>(arg)->operator()(obj);
  }

 private:
  MarkCompact* const collector_;
};

class MarkCompact::ClassLoaderRootsUpdater : public ClassLoaderVisitor {
 public:
  explicit ClassLoaderRootsUpdater(MarkCompact* collector)
      : collector_(collector),
        moving_space_begin_(collector->black_dense_end_),
        moving_space_end_(collector->moving_space_end_) {}

  void Visit(ObjPtr<mirror::ClassLoader> class_loader) override
      REQUIRES_SHARED(Locks::classlinker_classes_lock_, Locks::mutator_lock_) {
    ClassTable* const class_table = class_loader->GetClassTable();
    if (class_table != nullptr) {
      // Classes are updated concurrently.
      class_table->VisitRoots(*this, /*skip_classes=*/true);
    }
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    collector_->UpdateRoot(
        root, moving_space_begin_, moving_space_end_, RootInfo(RootType::kRootVMInternal));
  }

 private:
  MarkCompact* collector_;
  uint8_t* const moving_space_begin_;
  uint8_t* const moving_space_end_;
};

class MarkCompact::LinearAllocPageUpdater {
 public:
  explicit LinearAllocPageUpdater(MarkCompact* collector)
      : collector_(collector),
        moving_space_begin_(collector->black_dense_end_),
        moving_space_end_(collector->moving_space_end_),
        last_page_touched_(false) {}

  // Update a page in multi-object arena.
  void MultiObjectArena(uint8_t* page_begin, uint8_t* first_obj)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(first_obj != nullptr);
    DCHECK_ALIGNED_PARAM(page_begin, gPageSize);
    uint8_t* page_end = page_begin + gPageSize;
    uint32_t obj_size;
    for (uint8_t* byte = first_obj; byte < page_end;) {
      TrackingHeader* header = reinterpret_cast<TrackingHeader*>(byte);
      obj_size = header->GetSize();
      if (UNLIKELY(obj_size == 0)) {
        // No more objects in this page to visit.
        last_page_touched_ = byte >= page_begin;
        return;
      }
      uint8_t* obj = byte + sizeof(TrackingHeader);
      uint8_t* obj_end = byte + obj_size;
      if (header->Is16Aligned()) {
        obj = AlignUp(obj, 16);
      }
      uint8_t* begin_boundary = std::max(obj, page_begin);
      uint8_t* end_boundary = std::min(obj_end, page_end);
      if (begin_boundary < end_boundary) {
        VisitObject(header->GetKind(), obj, begin_boundary, end_boundary);
      }
      if (ArenaAllocator::IsRunningOnMemoryTool()) {
        obj_size += ArenaAllocator::kMemoryToolRedZoneBytes;
      }
      byte += RoundUp(obj_size, LinearAlloc::kAlignment);
    }
    last_page_touched_ = true;
  }

  // This version is only used for cases where the entire page is filled with
  // GC-roots. For example, class-table and intern-table.
  void SingleObjectArena(uint8_t* page_begin, size_t page_size)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    static_assert(sizeof(uint32_t) == sizeof(GcRoot<mirror::Object>));
    DCHECK_ALIGNED(page_begin, kAlignment);
    // Least significant bits are used by class-table.
    static constexpr uint32_t kMask = kObjectAlignment - 1;
    size_t num_roots = page_size / sizeof(GcRoot<mirror::Object>);
    uint32_t* root_ptr = reinterpret_cast<uint32_t*>(page_begin);
    for (size_t i = 0; i < num_roots; root_ptr++, i++) {
      uint32_t word = *root_ptr;
      if (word != 0) {
        uint32_t lsbs = word & kMask;
        word &= ~kMask;
        VisitRootIfNonNull(reinterpret_cast<mirror::CompressedReference<mirror::Object>*>(&word));
        *root_ptr = word | lsbs;
        last_page_touched_ = true;
      }
    }
  }

  bool WasLastPageTouched() const { return last_page_touched_; }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      ALWAYS_INLINE REQUIRES_SHARED(Locks::mutator_lock_) {
    mirror::Object* old_ref = root->AsMirrorPtr();
    DCHECK_NE(old_ref, nullptr);
    if (MarkCompact::HasAddress(old_ref, moving_space_begin_, moving_space_end_)) {
      mirror::Object* new_ref = old_ref;
      if (reinterpret_cast<uint8_t*>(old_ref) >= collector_->black_allocations_begin_) {
        new_ref = collector_->PostCompactBlackObjAddr(old_ref);
      } else if (collector_->live_words_bitmap_->Test(old_ref)) {
        DCHECK(collector_->moving_space_bitmap_->Test(old_ref))
            << "ref:" << old_ref << " root:" << root;
        new_ref = collector_->PostCompactOldObjAddr(old_ref);
      }
      if (old_ref != new_ref) {
        root->Assign(new_ref);
      }
    }
  }

 private:
  void VisitObject(LinearAllocKind kind,
                   void* obj,
                   uint8_t* start_boundary,
                   uint8_t* end_boundary) const ALWAYS_INLINE
      REQUIRES_SHARED(Locks::mutator_lock_) {
    switch (kind) {
      case LinearAllocKind::kNoGCRoots:
        break;
      case LinearAllocKind::kGCRootArray:
        {
          GcRoot<mirror::Object>* root = reinterpret_cast<GcRoot<mirror::Object>*>(start_boundary);
          GcRoot<mirror::Object>* last = reinterpret_cast<GcRoot<mirror::Object>*>(end_boundary);
          for (; root < last; root++) {
            VisitRootIfNonNull(root->AddressWithoutBarrier());
          }
        }
        break;
      case LinearAllocKind::kArtMethodArray:
        {
          LengthPrefixedArray<ArtMethod>* array = static_cast<LengthPrefixedArray<ArtMethod>*>(obj);
          // Old methods are clobbered in debug builds. Check size to confirm if the array
          // has any GC roots to visit. See ClassLinker::LinkMethodsHelper::ClobberOldMethods()
          if (array->size() > 0) {
            if (collector_->pointer_size_ == PointerSize::k64) {
              ArtMethod::VisitArrayRoots<PointerSize::k64>(
                  *this, start_boundary, end_boundary, array);
            } else {
              DCHECK_EQ(collector_->pointer_size_, PointerSize::k32);
              ArtMethod::VisitArrayRoots<PointerSize::k32>(
                  *this, start_boundary, end_boundary, array);
            }
          }
        }
        break;
      case LinearAllocKind::kArtMethod:
        ArtMethod::VisitRoots(*this, start_boundary, end_boundary, static_cast<ArtMethod*>(obj));
        break;
      case LinearAllocKind::kArtFieldArray:
        ArtField::VisitArrayRoots(*this,
                                  start_boundary,
                                  end_boundary,
                                  static_cast<LengthPrefixedArray<ArtField>*>(obj));
        break;
      case LinearAllocKind::kDexCacheArray:
        {
          mirror::DexCachePair<mirror::Object>* first =
              reinterpret_cast<mirror::DexCachePair<mirror::Object>*>(start_boundary);
          mirror::DexCachePair<mirror::Object>* last =
              reinterpret_cast<mirror::DexCachePair<mirror::Object>*>(end_boundary);
          mirror::DexCache::VisitDexCachePairRoots(*this, first, last);
      }
    }
  }

  MarkCompact* const collector_;
  // Cache to speed up checking if GC-root is in moving space or not.
  uint8_t* const moving_space_begin_;
  uint8_t* const moving_space_end_;
  // Whether the last page was touched or not.
  bool last_page_touched_ = false;
};

void MarkCompact::UpdateClassTableClasses(Runtime* runtime, bool immune_class_table_only) {
  // If the process is debuggable then redefinition is allowed, which may mean
  // pre-zygote-fork class-tables may have pointer to class in moving-space.
  // So visit classes from class-sets that are not in linear-alloc arena-pool.
  if (UNLIKELY(runtime->IsJavaDebuggableAtInit())) {
    ClassLinker* linker = runtime->GetClassLinker();
    ClassLoaderRootsUpdater updater(this);
    GcVisitedArenaPool* pool = static_cast<GcVisitedArenaPool*>(runtime->GetLinearAllocArenaPool());
    auto cond = [this, pool, immune_class_table_only](ClassTable::ClassSet& set) -> bool {
      if (!set.empty()) {
        return immune_class_table_only ?
               immune_spaces_.ContainsObject(reinterpret_cast<mirror::Object*>(&*set.begin())) :
               !pool->Contains(reinterpret_cast<void*>(&*set.begin()));
      }
      return false;
    };
    linker->VisitClassTables([cond, &updater](ClassTable* table)
                                 REQUIRES_SHARED(Locks::mutator_lock_) {
                               table->VisitClassesIfConditionMet(cond, updater);
                             });
    ReaderMutexLock rmu(thread_running_gc_, *Locks::classlinker_classes_lock_);
    linker->GetBootClassTable()->VisitClassesIfConditionMet(cond, updater);
  }
}

void MarkCompact::CompactionPause() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  Runtime* runtime = Runtime::Current();
  if (kIsDebugBuild) {
    DCHECK_EQ(thread_running_gc_, Thread::Current());
    // TODO(Simulator): Test that this should not operate on the simulated stack when the simulator
    // supports mark compact.
    stack_low_addr_ = thread_running_gc_->GetStackEnd<kNativeStackType>();
    stack_high_addr_ = reinterpret_cast<char*>(stack_low_addr_)
                       + thread_running_gc_->GetUsableStackSize<kNativeStackType>();
  }
  {
    TimingLogger::ScopedTiming t2("(Paused)UpdateCompactionDataStructures", GetTimings());
    ReaderMutexLock rmu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    // Refresh data-structures to catch-up on allocations that may have
    // happened since marking-phase pause.
    // There could be several TLABs that got allocated since marking pause. We
    // don't want to compact them and instead update the TLAB info in TLS and
    // let mutators continue to use the TLABs.
    // We need to set all the bits in live-words bitmap corresponding to allocated
    // objects. Also, we need to find the objects that are overlapping with
    // page-begin boundaries. Unlike objects allocated before
    // black_allocations_begin_, which can be identified via mark-bitmap, we can get
    // this info only via walking the space past black_allocations_begin_, which
    // involves fetching object size.
    // TODO: We can reduce the time spent on this in a pause by performing one
    // round of this concurrently prior to the pause.
    UpdateMovingSpaceBlackAllocations();
    // Iterate over the allocation_stack_, for every object in the non-moving
    // space:
    // 1. Mark the object in live bitmap
    // 2. Erase the object from allocation stack
    // 3. In the corresponding page, if the first-object vector needs updating
    // then do so.
    UpdateNonMovingSpaceBlackAllocations();
    // This store is visible to mutator (or uffd worker threads) as the mutator
    // lock's unlock guarantees that.
    compacting_ = true;
    // Start updating roots and system weaks now.
    heap_->GetReferenceProcessor()->UpdateRoots(this);
  }
  {
    // TODO: Immune space updation has to happen either before or after
    // remapping pre-compact pages to from-space. And depending on when it's
    // done, we have to invoke VisitRefsForCompaction() with or without
    // read-barrier.
    TimingLogger::ScopedTiming t2("(Paused)UpdateImmuneSpaces", GetTimings());
    accounting::CardTable* const card_table = heap_->GetCardTable();
    for (auto& space : immune_spaces_.GetSpaces()) {
      DCHECK(space->IsImageSpace() || space->IsZygoteSpace());
      accounting::ContinuousSpaceBitmap* live_bitmap = space->GetLiveBitmap();
      accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
      // Having zygote-space indicates that the first zygote fork has taken
      // place and that the classes/dex-caches in immune-spaces may have allocations
      // (ArtMethod/ArtField arrays, dex-cache array, etc.) in the
      // non-userfaultfd visited private-anonymous mappings. Visit them here.
      ImmuneSpaceUpdateObjVisitor visitor(this);
      if (table != nullptr) {
        table->ProcessCards();
        table->VisitObjects(ImmuneSpaceUpdateObjVisitor::Callback, &visitor);
      } else {
        WriterMutexLock wmu(thread_running_gc_, *Locks::heap_bitmap_lock_);
        card_table->Scan<false>(
            live_bitmap,
            space->Begin(),
            space->Limit(),
            visitor,
            accounting::CardTable::kCardDirty - 1);
      }
    }
  }

  {
    TimingLogger::ScopedTiming t2("(Paused)UpdateRoots", GetTimings());
    runtime->VisitConcurrentRoots(this, kVisitRootFlagAllRoots);
    runtime->VisitNonThreadRoots(this);
    {
      ClassLinker* linker = runtime->GetClassLinker();
      ClassLoaderRootsUpdater updater(this);
      ReaderMutexLock rmu(thread_running_gc_, *Locks::classlinker_classes_lock_);
      linker->VisitClassLoaders(&updater);
      linker->GetBootClassTable()->VisitRoots(updater, /*skip_classes=*/true);
    }
    SweepSystemWeaks(thread_running_gc_, runtime, /*paused=*/true);

    bool has_zygote_space = heap_->HasZygoteSpace();
    GcVisitedArenaPool* arena_pool =
        static_cast<GcVisitedArenaPool*>(runtime->GetLinearAllocArenaPool());
    // Update immune/pre-zygote class-tables in case class redefinition took
    // place. pre-zygote class-tables that are not in immune spaces are updated
    // below if we are in fallback-mode or if there is no zygote space. So in
    // that case only visit class-tables that are there in immune-spaces.
    UpdateClassTableClasses(runtime, uffd_ == kFallbackMode || !has_zygote_space);

    // Acquire arena-pool's lock, which should be released after the pool is
    // userfaultfd registered. This is to ensure that no new arenas are
    // allocated and used in between. Since they will not be captured in
    // linear_alloc_arenas_ below, we will miss updating their pages. The same
    // reason also applies to new allocations within the existing arena which
    // may change last_byte.
    // Since we are in a STW pause, this shouldn't happen anyways, but holding
    // the lock confirms it.
    // TODO (b/305779657): Replace with ExclusiveTryLock() and assert that it
    // doesn't fail once it is available for ReaderWriterMutex.
    WriterMutexLock pool_wmu(thread_running_gc_, arena_pool->GetLock());

    // TODO: Find out why it's not sufficient to visit native roots of immune
    // spaces, and why all the pre-zygote fork arenas have to be linearly updated.
    // Is it possible that some native root starts getting pointed to by some object
    // in moving space after fork? Or are we missing a write-barrier somewhere
    // when a native root is updated?
    auto arena_visitor = [this](uint8_t* page_begin, uint8_t* first_obj, size_t page_size)
                             REQUIRES_SHARED(Locks::mutator_lock_) {
                           LinearAllocPageUpdater updater(this);
                           if (first_obj != nullptr) {
                             updater.MultiObjectArena(page_begin, first_obj);
                           } else {
                             updater.SingleObjectArena(page_begin, page_size);
                           }
                         };
    if (uffd_ == kFallbackMode || (!has_zygote_space && runtime->IsZygote())) {
      // Besides fallback-mode, visit linear-alloc space in the pause for zygote
      // processes prior to first fork (that's when zygote space gets created).
      if (kIsDebugBuild && IsValidFd(uffd_)) {
        // All arenas allocated so far are expected to be pre-zygote fork.
        arena_pool->ForEachAllocatedArena(
            [](const TrackedArena& arena)
                REQUIRES_SHARED(Locks::mutator_lock_) { CHECK(arena.IsPreZygoteForkArena()); });
      }
      arena_pool->VisitRoots(arena_visitor);
    } else {
      // Inform the arena-pool that compaction is going on. So the TrackedArena
      // objects corresponding to the arenas that are freed shouldn't be deleted
      // immediately. We will do that in FinishPhase(). This is to avoid ABA
      // problem.
      arena_pool->DeferArenaFreeing();
      arena_pool->ForEachAllocatedArena(
          [this, arena_visitor, has_zygote_space](const TrackedArena& arena)
              REQUIRES_SHARED(Locks::mutator_lock_) {
            // The pre-zygote fork arenas are not visited concurrently in the
            // zygote children processes. The native roots of the dirty objects
            // are visited during immune space visit below.
            if (!arena.IsPreZygoteForkArena()) {
              uint8_t* last_byte = arena.GetLastUsedByte();
              auto ret = linear_alloc_arenas_.insert({&arena, last_byte});
              CHECK(ret.second);
            } else if (!arena.IsSingleObjectArena() || !has_zygote_space) {
              // Pre-zygote class-table and intern-table don't need to be updated.
              // TODO: Explore the possibility of using /proc/self/pagemap to
              // fetch which pages in these arenas are private-dirty and then only
              // visit those pages. To optimize it further, we can keep all
              // pre-zygote arenas in a single memory range so that just one read
              // from pagemap is sufficient.
              arena.VisitRoots(arena_visitor);
            }
          });
    }
    // Release order wrt to mutator threads' SIGBUS handler load.
    sigbus_in_progress_count_[0].store(0, std::memory_order_relaxed);
    sigbus_in_progress_count_[1].store(0, std::memory_order_release);
    app_slow_path_start_time_ = MilliTime();
    KernelPreparation();
  }

  UpdateNonMovingSpace();
  // fallback mode
  if (uffd_ == kFallbackMode) {
    CompactMovingSpace<kFallbackMode>(nullptr);

    int32_t freed_bytes = black_objs_slide_diff_;
    bump_pointer_space_->RecordFree(freed_objects_, freed_bytes);
    RecordFree(ObjectBytePair(freed_objects_, freed_bytes));
  } else {
    DCHECK_EQ(compaction_buffer_counter_.load(std::memory_order_relaxed), 1);
  }
  stack_low_addr_ = nullptr;
}

void MarkCompact::KernelPrepareRangeForUffd(uint8_t* to_addr, uint8_t* from_addr, size_t map_size) {
  int mremap_flags = MREMAP_MAYMOVE | MREMAP_FIXED;
  if (gHaveMremapDontunmap) {
    mremap_flags |= MREMAP_DONTUNMAP;
  }

  void* ret = mremap(to_addr, map_size, map_size, mremap_flags, from_addr);
  CHECK_EQ(ret, static_cast<void*>(from_addr))
      << "mremap to move pages failed: " << strerror(errno)
      << ". space-addr=" << reinterpret_cast<void*>(to_addr) << " size=" << PrettySize(map_size);

  if (!gHaveMremapDontunmap) {
    // Without MREMAP_DONTUNMAP the source mapping is unmapped by mremap. So mmap
    // the moving space again.
    int mmap_flags = MAP_FIXED;
    // Use MAP_FIXED_NOREPLACE so that if someone else reserves 'to_addr'
    // mapping in meantime, which can happen when MREMAP_DONTUNMAP isn't
    // available, to avoid unmapping someone else' mapping and then causing
    // crashes elsewhere.
    mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
    ret = mmap(to_addr, map_size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    CHECK_EQ(ret, static_cast<void*>(to_addr))
        << "mmap for moving space failed: " << strerror(errno);
  }
}

void MarkCompact::KernelPreparation() {
  TimingLogger::ScopedTiming t("(Paused)KernelPreparation", GetTimings());
  uint8_t* moving_space_begin = bump_pointer_space_->Begin();
  size_t moving_space_size = bump_pointer_space_->Capacity();
  size_t moving_space_register_sz = (moving_first_objs_count_ + black_page_count_) * gPageSize;
  DCHECK_LE(moving_space_register_sz, moving_space_size);

  KernelPrepareRangeForUffd(moving_space_begin, from_space_begin_, moving_space_size);

  if (IsValidFd(uffd_)) {
    if (moving_space_register_sz > 0) {
      // mremap clears 'anon_vma' field of anonymous mappings. If we
      // uffd-register only the used portion of the space, then the vma gets
      // split (between used and unused portions) and as soon as pages are
      // mapped to the vmas, they get different `anon_vma` assigned, which
      // ensures that the two vmas cannot merge after we uffd-unregister the
      // used portion. OTOH, registering the entire space avoids the split, but
      // unnecessarily causes userfaults on allocations.
      // By faulting-in a page we force the kernel to allocate 'anon_vma' *before*
      // the vma-split in uffd-register. This ensures that when we unregister
      // the used portion after compaction, the two split vmas merge. This is
      // necessary for the mremap of the next GC cycle to not fail due to having
      // more than one vma in the source range.
      //
      // Fault in address aligned to PMD size so that in case THP is enabled,
      // we don't mistakenly fault a page in beginning portion that will be
      // registered with uffd. If the alignment takes us beyond the space, then
      // fault the first page and madvise it.
      size_t pmd_size = Heap::GetPMDSize();
      uint8_t* fault_in_addr = AlignUp(moving_space_begin + moving_space_register_sz, pmd_size);
      if (bump_pointer_space_->Contains(reinterpret_cast<mirror::Object*>(fault_in_addr))) {
        *const_cast<volatile uint8_t*>(fault_in_addr) = 0;
      } else {
        DCHECK_ALIGNED_PARAM(moving_space_begin, gPageSize);
        *const_cast<volatile uint8_t*>(moving_space_begin) = 0;
        madvise(moving_space_begin, pmd_size, MADV_DONTNEED);
      }
      // Register the moving space with userfaultfd.
      RegisterUffd(moving_space_begin, moving_space_register_sz);
      // madvise ensures that if any page gets mapped (only possible if some
      // thread is reading the page(s) without trying to make sense as we hold
      // mutator-lock exclusively) between mremap and uffd-registration, then
      // it gets zapped so that the map is empty and ready for userfaults. If
      // we could mremap after uffd-registration (like in case of linear-alloc
      // space below) then we wouldn't need it. But since we don't register the
      // entire space, we can't do that.
      madvise(moving_space_begin, moving_space_register_sz, MADV_DONTNEED);
    }
    // Prepare linear-alloc for concurrent compaction.
    for (auto& data : linear_alloc_spaces_data_) {
      DCHECK_EQ(static_cast<ssize_t>(data.shadow_.Size()), data.end_ - data.begin_);
      // There could be threads running in suspended mode when the compaction
      // pause is being executed. In order to make the userfaultfd setup atomic,
      // the registration has to be done *before* moving the pages to shadow map.
      RegisterUffd(data.begin_, data.shadow_.Size());
      KernelPrepareRangeForUffd(data.begin_, data.shadow_.Begin(), data.shadow_.Size());
    }
  }
}

bool MarkCompact::SigbusHandler(siginfo_t* info) {
  class ScopedInProgressCount {
   public:
    explicit ScopedInProgressCount(MarkCompact* collector) : collector_(collector) {
      // Increment the count only if compaction is not done yet.
      for (idx_ = 0; idx_ < 2; idx_++) {
        SigbusCounterType prev =
            collector_->sigbus_in_progress_count_[idx_].load(std::memory_order_relaxed);
        while ((prev & kSigbusCounterCompactionDoneMask) == 0) {
          if (collector_->sigbus_in_progress_count_[idx_].compare_exchange_strong(
                  prev, prev + 1, std::memory_order_acquire)) {
            DCHECK_LT(prev, kSigbusCounterCompactionDoneMask - 1);
            return;
          }
        }
      }
    }

    bool TolerateEnoent() const { return idx_ == 1; }

    bool IsCompactionDone() const { return idx_ == 2; }

    ~ScopedInProgressCount() {
      if (idx_ < 2) {
        collector_->sigbus_in_progress_count_[idx_].fetch_sub(1, std::memory_order_release);
      }
    }

   private:
    MarkCompact* const collector_;
    uint8_t idx_;
  };

  if (info->si_code != BUS_ADRERR) {
    // Userfaultfd raises SIGBUS with BUS_ADRERR. All other causes can't be
    // handled here.
    return false;
  }

  ScopedInProgressCount spc(this);
  uint8_t* fault_page = AlignDown(reinterpret_cast<uint8_t*>(info->si_addr), gPageSize);
  if (!spc.IsCompactionDone()) {
    if (HasAddress(reinterpret_cast<mirror::Object*>(fault_page))) {
      Thread* self = Thread::Current();
      Locks::mutator_lock_->AssertSharedHeld(self);
      size_t nr_moving_space_used_pages = moving_first_objs_count_ + black_page_count_;
      ConcurrentlyProcessMovingPage(fault_page,
                                    self->GetThreadLocalGcBuffer(),
                                    nr_moving_space_used_pages,
                                    spc.TolerateEnoent());
      return true;
    } else {
      // Find the linear-alloc space containing fault-addr
      for (auto& data : linear_alloc_spaces_data_) {
        if (data.begin_ <= fault_page && data.end_ > fault_page) {
          ConcurrentlyProcessLinearAllocPage(fault_page, spc.TolerateEnoent());
          return true;
        }
      }
      // Fault address doesn't belong to either moving-space or linear-alloc.
      return false;
    }
  } else {
    // We may spuriously get SIGBUS fault, which was initiated before the
    // compaction was finished, but ends up here. In that case, if the fault
    // address is valid then consider it handled.
    return HasAddress(reinterpret_cast<mirror::Object*>(fault_page)) ||
           linear_alloc_spaces_data_.end() !=
               std::find_if(linear_alloc_spaces_data_.begin(),
                            linear_alloc_spaces_data_.end(),
                            [fault_page](const LinearAllocSpaceData& data) {
                              return data.begin_ <= fault_page && data.end_ > fault_page;
                            });
  }
}

void MarkCompact::ConcurrentlyProcessMovingPage(uint8_t* fault_page,
                                                uint8_t* buf,
                                                size_t nr_moving_space_used_pages,
                                                bool tolerate_enoent) {
  Thread* self = Thread::Current();
  uint8_t* unused_space_begin = moving_space_begin_ + nr_moving_space_used_pages * gPageSize;
  DCHECK(IsAlignedParam(unused_space_begin, gPageSize));
  if (fault_page >= unused_space_begin) {
    // There is a race which allows more than one thread to install a
    // zero-page. But we can tolerate that. So absorb the EEXIST returned by
    // the ioctl and move on.
    ZeropageIoctl(fault_page, gPageSize, /*tolerate_eexist=*/true, tolerate_enoent);
    return;
  }
  size_t page_idx = DivideByPageSize(fault_page - moving_space_begin_);
  DCHECK_LT(page_idx, moving_first_objs_count_ + black_page_count_);
  mirror::Object* first_obj = first_objs_moving_space_[page_idx].AsMirrorPtr();
  if (first_obj == nullptr) {
    DCHECK_GT(fault_page, post_compact_end_);
    // Install zero-page in the entire remaining tlab to avoid multiple ioctl invocations.
    uint8_t* end = AlignDown(self->GetTlabEnd(), gPageSize);
    if (fault_page < self->GetTlabStart() || fault_page >= end) {
      end = fault_page + gPageSize;
    }
    size_t end_idx = page_idx + DivideByPageSize(end - fault_page);
    size_t length = 0;
    for (size_t idx = page_idx; idx < end_idx; idx++, length += gPageSize) {
      uint32_t cur_state = moving_pages_status_[idx].load(std::memory_order_acquire);
      if (cur_state != static_cast<uint8_t>(PageState::kUnprocessed)) {
        DCHECK_EQ(cur_state, static_cast<uint8_t>(PageState::kProcessedAndMapped));
        break;
      }
    }
    if (length > 0) {
      length = ZeropageIoctl(fault_page, length, /*tolerate_eexist=*/true, tolerate_enoent);
      for (size_t len = 0, idx = page_idx; len < length; idx++, len += gPageSize) {
        moving_pages_status_[idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapped),
                                        std::memory_order_release);
      }
    }
    return;
  }

  uint32_t raw_state = moving_pages_status_[page_idx].load(std::memory_order_acquire);
  uint32_t backoff_count = 0;
  PageState state;
  while (true) {
    state = GetPageStateFromWord(raw_state);
    if (state == PageState::kProcessing || state == PageState::kMutatorProcessing ||
        state == PageState::kProcessingAndMapping || state == PageState::kProcessedAndMapping) {
      // Wait for the page to be mapped (by gc-thread or some mutator) before returning.
      // The wait is not expected to be long as the read state indicates that the other
      // thread is actively working on the page.
      BackOff(backoff_count++);
      raw_state = moving_pages_status_[page_idx].load(std::memory_order_acquire);
    } else if (state == PageState::kProcessedAndMapped) {
      // Nothing to do.
      break;
    } else {
      // Acquire order to ensure we don't start writing to a page, which could
      // be shared, before the CAS is successful.
      if (state == PageState::kUnprocessed &&
          moving_pages_status_[page_idx].compare_exchange_strong(
              raw_state,
              static_cast<uint8_t>(PageState::kMutatorProcessing),
              std::memory_order_acquire)) {
        if (fault_page < black_dense_end_) {
          if (use_generational_) {
            UpdateNonMovingPage</*kSetupForGenerational=*/true>(
                first_obj, fault_page, from_space_slide_diff_, moving_space_bitmap_);
          } else {
            UpdateNonMovingPage</*kSetupForGenerational=*/false>(
                first_obj, fault_page, from_space_slide_diff_, moving_space_bitmap_);
          }
          buf = fault_page + from_space_slide_diff_;
        } else {
          if (UNLIKELY(buf == nullptr)) {
            uint16_t idx = compaction_buffer_counter_.fetch_add(1, std::memory_order_relaxed);
            // The buffer-map is one page bigger as the first buffer is used by GC-thread.
            CHECK_LE(idx, kMutatorCompactionBufferCount);
            buf = compaction_buffers_map_.Begin() + idx * gPageSize;
            DCHECK(compaction_buffers_map_.HasAddress(buf));
            self->SetThreadLocalGcBuffer(buf);
          }

          if (fault_page < post_compact_end_) {
            // The page has to be compacted.
            if (use_generational_ && fault_page < mid_gen_end_) {
              CompactPage</*kSetupGenerational=*/true>(first_obj,
                                                       pre_compact_offset_moving_space_[page_idx],
                                                       buf,
                                                       fault_page,
                                                       /*needs_memset_zero=*/true);
            } else {
              CompactPage</*kSetupGenerational=*/false>(first_obj,
                                                        pre_compact_offset_moving_space_[page_idx],
                                                        buf,
                                                        fault_page,
                                                        /*needs_memset_zero=*/true);
            }
          } else {
            DCHECK_NE(first_obj, nullptr);
            DCHECK_GT(pre_compact_offset_moving_space_[page_idx], 0u);
            uint8_t* pre_compact_page = black_allocations_begin_ + (fault_page - post_compact_end_);
            uint32_t first_chunk_size = black_alloc_pages_first_chunk_size_[page_idx];
            mirror::Object* next_page_first_obj = nullptr;
            if (page_idx + 1 < moving_first_objs_count_ + black_page_count_) {
              next_page_first_obj = first_objs_moving_space_[page_idx + 1].AsMirrorPtr();
            }
            DCHECK(IsAlignedParam(pre_compact_page, gPageSize));
            SlideBlackPage(first_obj,
                           next_page_first_obj,
                           first_chunk_size,
                           pre_compact_page,
                           buf,
                           /*needs_memset_zero=*/true);
          }
        }
        // Nobody else would simultaneously modify this page's state so an
        // atomic store is sufficient. Use 'release' order to guarantee that
        // loads/stores to the page are finished before this store. Since the
        // mutator used its own buffer for the processing, there is no reason to
        // put its index in the status of the page. Also, the mutator is going
        // to immediately map the page, so that info is not needed.
        moving_pages_status_[page_idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapping),
                                             std::memory_order_release);
        CopyIoctl(fault_page, buf, gPageSize, /*return_on_contention=*/false, tolerate_enoent);
        // Store is sufficient as no other thread modifies the status at this stage.
        moving_pages_status_[page_idx].store(static_cast<uint8_t>(PageState::kProcessedAndMapped),
                                             std::memory_order_release);
        break;
      }
      state = GetPageStateFromWord(raw_state);
      if (state == PageState::kProcessed) {
        size_t arr_len = moving_first_objs_count_ + black_page_count_;
        // The page is processed but not mapped. We should map it. The release
        // order used in MapMovingSpacePages will ensure that the increment to
        // moving_compaction_in_progress is done first.
        if (MapMovingSpacePages(page_idx,
                                arr_len,
                                /*from_fault=*/true,
                                /*return_on_contention=*/false,
                                tolerate_enoent) > 0) {
          break;
        }
        raw_state = moving_pages_status_[page_idx].load(std::memory_order_acquire);
      }
    }
  }
}

bool MarkCompact::MapUpdatedLinearAllocPages(uint8_t* start_page,
                                             uint8_t* start_shadow_page,
                                             Atomic<PageState>* state,
                                             size_t length,
                                             bool free_pages,
                                             bool single_ioctl,
                                             bool tolerate_enoent) {
  DCHECK_ALIGNED_PARAM(length, gPageSize);
  Atomic<PageState>* madv_state = state;
  size_t madv_len = length;
  uint8_t* madv_start = start_shadow_page;
  bool check_state_for_madv = false;
  uint8_t* end_page = start_page + length;
  while (start_page < end_page) {
    size_t map_len = 0;
    // Find a contiguous range of pages that we can map in single ioctl.
    for (Atomic<PageState>* cur_state = state;
         map_len < length && cur_state->load(std::memory_order_acquire) == PageState::kProcessed;
         map_len += gPageSize, cur_state++) {
      // No body.
    }

    if (map_len == 0) {
      if (single_ioctl) {
        return state->load(std::memory_order_relaxed) == PageState::kProcessedAndMapped;
      }
      // Skip all the pages that this thread can't map.
      while (length > 0) {
        PageState s = state->load(std::memory_order_relaxed);
        if (s == PageState::kProcessed) {
          break;
        }
        // If we find any page which is being processed or mapped (only possible by a mutator(s))
        // then we need to re-check the page-state and, if needed, wait for the state to change
        // to 'mapped', before the shadow pages are reclaimed.
        check_state_for_madv |= s > PageState::kUnprocessed && s < PageState::kProcessedAndMapped;
        state++;
        length -= gPageSize;
        start_shadow_page += gPageSize;
        start_page += gPageSize;
      }
    } else {
      map_len = CopyIoctl(start_page,
                          start_shadow_page,
                          map_len,
                          /*return_on_contention=*/false,
                          tolerate_enoent);
      DCHECK_NE(map_len, 0u);
      // Declare that the pages are ready to be accessed. Store is sufficient
      // as any thread will be storing the same value.
      for (size_t l = 0; l < map_len; l += gPageSize, state++) {
        PageState s = state->load(std::memory_order_relaxed);
        DCHECK(s == PageState::kProcessed || s == PageState::kProcessedAndMapped) << "state:" << s;
        state->store(PageState::kProcessedAndMapped, std::memory_order_release);
      }
      if (single_ioctl) {
        break;
      }
      start_page += map_len;
      start_shadow_page += map_len;
      length -= map_len;
      // state is already updated above.
    }
  }
  if (free_pages) {
    if (check_state_for_madv) {
      // Wait until all the pages are mapped before releasing them. This is needed to be
      // checked only if some mutators were found to be concurrently mapping pages earlier.
      for (size_t l = 0; l < madv_len; l += gPageSize, madv_state++) {
        uint32_t backoff_count = 0;
        PageState s = madv_state->load(std::memory_order_relaxed);
        while (s > PageState::kUnprocessed && s < PageState::kProcessedAndMapped) {
          BackOff(backoff_count++);
          s = madv_state->load(std::memory_order_relaxed);
        }
      }
    }
    ZeroAndReleaseMemory(madv_start, madv_len);
  }
  return true;
}

void MarkCompact::ConcurrentlyProcessLinearAllocPage(uint8_t* fault_page, bool tolerate_enoent) {
  auto arena_iter = linear_alloc_arenas_.end();
  {
    TrackedArena temp_arena(fault_page);
    arena_iter = linear_alloc_arenas_.upper_bound(&temp_arena);
    arena_iter = arena_iter != linear_alloc_arenas_.begin() ? std::prev(arena_iter)
                                                            : linear_alloc_arenas_.end();
  }
  // Unlike ProcessLinearAlloc(), we don't need to hold arena-pool's lock here
  // because a thread trying to access the page and as a result causing this
  // userfault confirms that nobody can delete the corresponding arena and
  // release its pages.
  // NOTE: We may have some memory range be recycled several times during a
  // compaction cycle, thereby potentially causing userfault on the same page
  // several times. That's not a problem as all of them (except for possibly the
  // first one) would require us mapping a zero-page, which we do without updating
  // the 'state_arr'.
  if (arena_iter == linear_alloc_arenas_.end() ||
      arena_iter->first->IsWaitingForDeletion() ||
      arena_iter->second <= fault_page) {
    // Fault page isn't in any of the arenas that existed before we started
    // compaction. So map zeropage and return.
    ZeropageIoctl(fault_page, gPageSize, /*tolerate_eexist=*/true, tolerate_enoent);
  } else {
    // Find the linear-alloc space containing fault-page
    LinearAllocSpaceData* space_data = nullptr;
    for (auto& data : linear_alloc_spaces_data_) {
      if (data.begin_ <= fault_page && fault_page < data.end_) {
        space_data = &data;
        break;
      }
    }
    DCHECK_NE(space_data, nullptr);
    ptrdiff_t diff = space_data->shadow_.Begin() - space_data->begin_;
    size_t page_idx = DivideByPageSize(fault_page - space_data->begin_);
    Atomic<PageState>* state_arr =
        reinterpret_cast<Atomic<PageState>*>(space_data->page_status_map_.Begin());
    PageState state = state_arr[page_idx].load(std::memory_order_acquire);
    uint32_t backoff_count = 0;
    while (true) {
      switch (state) {
        case PageState::kUnprocessed: {
          // Acquire order to ensure we don't start writing to shadow map, which is
          // shared, before the CAS is successful.
          if (state_arr[page_idx].compare_exchange_strong(
                  state, PageState::kProcessing, std::memory_order_acquire)) {
            LinearAllocPageUpdater updater(this);
            uint8_t* first_obj = arena_iter->first->GetFirstObject(fault_page);
            // null first_obj indicates that it's a page from arena for
            // intern-table/class-table. So first object isn't required.
            if (first_obj != nullptr) {
              updater.MultiObjectArena(fault_page + diff, first_obj + diff);
            } else {
              updater.SingleObjectArena(fault_page + diff, gPageSize);
            }
            if (updater.WasLastPageTouched()) {
              state_arr[page_idx].store(PageState::kProcessed, std::memory_order_release);
              state = PageState::kProcessed;
              continue;
            } else {
              // If the page wasn't touched, then it means it is empty and
              // is most likely not present on the shadow-side. Furthermore,
              // since the shadow is also userfaultfd registered doing copy
              // ioctl fails as the copy-from-user in the kernel will cause
              // userfault. Instead, just map a zeropage, which is not only
              // correct but also efficient as it avoids unnecessary memcpy
              // in the kernel.
              if (ZeropageIoctl(fault_page,
                                gPageSize,
                                /*tolerate_eexist=*/false,
                                tolerate_enoent)) {
                state_arr[page_idx].store(PageState::kProcessedAndMapped,
                                          std::memory_order_release);
              }
              return;
            }
          }
        }
          continue;
        case PageState::kProcessed:
          // Map as many pages as possible in a single ioctl, without spending
          // time freeing pages.
          if (MapUpdatedLinearAllocPages(fault_page,
                                         fault_page + diff,
                                         state_arr + page_idx,
                                         space_data->end_ - fault_page,
                                         /*free_pages=*/false,
                                         /*single_ioctl=*/true,
                                         tolerate_enoent)) {
            return;
          }
          // fault_page was not mapped by this thread (some other thread claimed
          // it). Wait for it to be mapped before returning.
          FALLTHROUGH_INTENDED;
        case PageState::kProcessing:
        case PageState::kProcessingAndMapping:
        case PageState::kProcessedAndMapping:
          // Wait for the page to be mapped before returning.
          BackOff(backoff_count++);
          state = state_arr[page_idx].load(std::memory_order_acquire);
          continue;
        case PageState::kMutatorProcessing:
          LOG(FATAL) << "Unreachable";
          UNREACHABLE();
        case PageState::kProcessedAndMapped:
          // Somebody else took care of the page.
          return;
      }
      break;
    }
  }
}

void MarkCompact::ProcessLinearAlloc() {
  GcVisitedArenaPool* arena_pool =
      static_cast<GcVisitedArenaPool*>(Runtime::Current()->GetLinearAllocArenaPool());
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  uint8_t* unmapped_range_start = nullptr;
  uint8_t* unmapped_range_end = nullptr;
  // Pointer to the linear-alloc space containing the current arena in the loop
  // below. Also helps in ensuring that two arenas, which are contiguous in
  // address space but are from different linear-alloc spaces, are not coalesced
  // into one range for mapping purpose.
  LinearAllocSpaceData* space_data = nullptr;
  Atomic<PageState>* state_arr = nullptr;
  ptrdiff_t diff = 0;

  auto map_pages = [&]() {
    DCHECK_NE(diff, 0);
    DCHECK_NE(space_data, nullptr);
    DCHECK_GE(unmapped_range_start, space_data->begin_);
    DCHECK_LT(unmapped_range_start, space_data->end_);
    DCHECK_GT(unmapped_range_end, space_data->begin_);
    DCHECK_LE(unmapped_range_end, space_data->end_);
    DCHECK_LT(unmapped_range_start, unmapped_range_end);
    DCHECK_ALIGNED_PARAM(unmapped_range_end - unmapped_range_start, gPageSize);
    size_t page_idx = DivideByPageSize(unmapped_range_start - space_data->begin_);
    MapUpdatedLinearAllocPages(unmapped_range_start,
                               unmapped_range_start + diff,
                               state_arr + page_idx,
                               unmapped_range_end - unmapped_range_start,
                               /*free_pages=*/true,
                               /*single_ioctl=*/false,
                               /*tolerate_enoent=*/false);
  };
  for (auto& pair : linear_alloc_arenas_) {
    const TrackedArena* arena = pair.first;
    size_t arena_size = arena->Size();
    uint8_t* arena_begin = arena->Begin();
    // linear_alloc_arenas_ is sorted on arena-begin. So we will get all arenas
    // in that order.
    DCHECK_LE(unmapped_range_end, arena_begin);
    DCHECK(space_data == nullptr || arena_begin > space_data->begin_)
        << "space-begin:" << static_cast<void*>(space_data->begin_)
        << " arena-begin:" << static_cast<void*>(arena_begin);
    if (space_data == nullptr || space_data->end_ <= arena_begin) {
      // Map the processed arenas as we are switching to another space.
      if (space_data != nullptr && unmapped_range_end != nullptr) {
        map_pages();
        unmapped_range_end = nullptr;
      }
      // Find the linear-alloc space containing the arena
      LinearAllocSpaceData* curr_space_data = space_data;
      for (auto& data : linear_alloc_spaces_data_) {
        if (data.begin_ <= arena_begin && arena_begin < data.end_) {
          // Since arenas are sorted, the next space should be higher in address
          // order than the current one.
          DCHECK(space_data == nullptr || data.begin_ >= space_data->end_);
          diff = data.shadow_.Begin() - data.begin_;
          state_arr = reinterpret_cast<Atomic<PageState>*>(data.page_status_map_.Begin());
          space_data = &data;
          break;
        }
      }
      CHECK_NE(space_data, curr_space_data)
          << "Couldn't find space for arena-begin:" << static_cast<void*>(arena_begin);
    }
    // Map the processed arenas if we found a hole within the current space.
    if (unmapped_range_end != nullptr && unmapped_range_end < arena_begin) {
      map_pages();
      unmapped_range_end = nullptr;
    }
    if (unmapped_range_end == nullptr) {
      unmapped_range_start = unmapped_range_end = arena_begin;
    }
    DCHECK_NE(unmapped_range_start, nullptr);
    // It's ok to include all arenas in the unmapped range. Since the
    // corresponding state bytes will be kUnprocessed, we will skip calling
    // ioctl and madvise on arenas which are waiting to be deleted.
    unmapped_range_end += arena_size;
    {
      // Acquire arena-pool's lock (in shared-mode) so that the arena being updated
      // does not get deleted at the same time. If this critical section is too
      // long and impacts mutator response time, then we get rid of this lock by
      // holding onto memory ranges of all deleted (since compaction pause)
      // arenas until completion finishes.
      ReaderMutexLock rmu(thread_running_gc_, arena_pool->GetLock());
      // If any arenas were freed since compaction pause then skip them from
      // visiting.
      if (arena->IsWaitingForDeletion()) {
        continue;
      }
      uint8_t* last_byte = pair.second;
      DCHECK_ALIGNED_PARAM(last_byte, gPageSize);
      auto visitor = [space_data, last_byte, diff, this, state_arr](
                         uint8_t* page_begin,
                         uint8_t* first_obj,
                         size_t page_size) REQUIRES_SHARED(Locks::mutator_lock_) {
        // No need to process pages past last_byte as they already have updated
        // gc-roots, if any.
        if (page_begin >= last_byte) {
          return;
        }
        LinearAllocPageUpdater updater(this);
        size_t page_idx = DivideByPageSize(page_begin - space_data->begin_);
        DCHECK_LT(page_idx, space_data->page_status_map_.Size());
        PageState expected_state = PageState::kUnprocessed;
        // Acquire order to ensure that we don't start accessing the shadow page,
        // which is shared with other threads, prior to CAS. Also, for same
        // reason, we used 'release' order for changing the state to 'processed'.
        if (state_arr[page_idx].compare_exchange_strong(
                expected_state, PageState::kProcessing, std::memory_order_acquire)) {
          // null first_obj indicates that it's a page from arena for
          // intern-table/class-table. So first object isn't required.
          if (first_obj != nullptr) {
            updater.MultiObjectArena(page_begin + diff, first_obj + diff);
          } else {
            DCHECK_EQ(page_size, gPageSize);
            updater.SingleObjectArena(page_begin + diff, page_size);
          }
          expected_state = PageState::kProcessing;
          // Store is sufficient as no other thread could be modifying it. Use
          // release order to ensure that the writes to shadow page are
          // committed to memory before.
          if (updater.WasLastPageTouched()) {
            state_arr[page_idx].store(PageState::kProcessed, std::memory_order_release);
          } else {
            // See comment in ConcurrentlyProcessLinearAllocPage() with same situation.
            ZeropageIoctl(
                page_begin, gPageSize, /*tolerate_eexist=*/false, /*tolerate_enoent=*/false);
            // Ioctl will act as release fence.
            state_arr[page_idx].store(PageState::kProcessedAndMapped, std::memory_order_release);
          }
        }
      };

      arena->VisitRoots(visitor);
    }
  }
  if (unmapped_range_end > unmapped_range_start) {
    // Map remaining pages.
    map_pages();
  }
}

void MarkCompact::RegisterUffd(void* addr, size_t size) {
  DCHECK(IsValidFd(uffd_));
  struct uffdio_register uffd_register;
  uffd_register.range.start = reinterpret_cast<uintptr_t>(addr);
  uffd_register.range.len = size;
  uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
  CHECK_EQ(ioctl(uffd_, UFFDIO_REGISTER, &uffd_register), 0)
      << "ioctl_userfaultfd: register failed: " << strerror(errno)
      << ". start:" << static_cast<void*>(addr) << " len:" << PrettySize(size);
}

// TODO: sometime we may want to tolerate certain error conditions (like ENOMEM
// when we unregister the unused portion of the moving-space). Implement support
// for that.
void MarkCompact::UnregisterUffd(uint8_t* start, size_t len) {
  DCHECK(IsValidFd(uffd_));
  struct uffdio_range range;
  range.start = reinterpret_cast<uintptr_t>(start);
  range.len = len;
  CHECK_EQ(ioctl(uffd_, UFFDIO_UNREGISTER, &range), 0)
      << "ioctl_userfaultfd: unregister failed: " << strerror(errno)
      << ". addr:" << static_cast<void*>(start) << " len:" << PrettySize(len);
}

void MarkCompact::CompactionPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  {
    int32_t freed_bytes = black_objs_slide_diff_;
    bump_pointer_space_->RecordFree(freed_objects_, freed_bytes);
    RecordFree(ObjectBytePair(freed_objects_, freed_bytes));
  }

  CompactMovingSpace<kCopyMode>(compaction_buffers_map_.Begin());

  ProcessLinearAlloc();

  auto wait_for_compaction_counter = [this](size_t idx) {
    SigbusCounterType count = sigbus_in_progress_count_[idx].fetch_or(
        kSigbusCounterCompactionDoneMask, std::memory_order_acq_rel);
    // Wait for SIGBUS handlers already in play.
    for (uint32_t i = 0; count > 0; i++) {
      BackOff(i);
      count = sigbus_in_progress_count_[idx].load(std::memory_order_acquire);
      count &= ~kSigbusCounterCompactionDoneMask;
    }
  };
  // Set compaction-done bit in the first counter to indicate that gc-thread
  // is done compacting and mutators should stop incrementing this counter.
  // Mutator should tolerate ENOENT after this. This helps avoid priority
  // inversion in case mutators need to map zero-pages after compaction is
  // finished but before gc-thread manages to unregister the spaces.
  wait_for_compaction_counter(0);

  // Unregister moving-space
  size_t moving_space_size = bump_pointer_space_->Capacity();
  size_t used_size = (moving_first_objs_count_ + black_page_count_) * gPageSize;
  if (used_size > 0) {
    UnregisterUffd(bump_pointer_space_->Begin(), used_size);
  }
  // Unregister linear-alloc spaces
  for (auto& data : linear_alloc_spaces_data_) {
    DCHECK_EQ(data.end_ - data.begin_, static_cast<ssize_t>(data.shadow_.Size()));
    UnregisterUffd(data.begin_, data.shadow_.Size());
  }
  GetCurrentIteration()->SetAppSlowPathDurationMs(MilliTime() - app_slow_path_start_time_);

  // Set compaction-done bit in the second counter to indicate that gc-thread
  // is done unregistering the spaces and therefore mutators, if in SIGBUS,
  // should return without attempting to map the faulted page. When the mutator
  // will access the address again, it will succeed. Once this counter is 0,
  // the gc-thread can safely initialize/madvise the data structures.
  wait_for_compaction_counter(1);

  // Release all of the memory taken by moving-space's from-map
  from_space_map_.MadviseDontNeedAndZero();
  // mprotect(PROT_NONE) all maps except to-space in debug-mode to catch any unexpected accesses.
  DCHECK_EQ(mprotect(from_space_begin_, moving_space_size, PROT_NONE), 0)
      << "mprotect(PROT_NONE) for from-space failed: " << strerror(errno);

  // madvise linear-allocs's page-status array. Note that we don't need to
  // madvise the shado-map as the pages from it were reclaimed in
  // ProcessLinearAlloc() after arenas were mapped.
  for (auto& data : linear_alloc_spaces_data_) {
    data.page_status_map_.MadviseDontNeedAndZero();
  }
}

template <size_t kBufferSize>
class MarkCompact::ThreadRootsVisitor : public RootVisitor {
 public:
  explicit ThreadRootsVisitor(MarkCompact* mark_compact, Thread* const self)
        : mark_compact_(mark_compact), self_(self) {}

  ~ThreadRootsVisitor() {
    Flush();
  }

  void VisitRoots(mirror::Object*** roots,
                  size_t count,
                  [[maybe_unused]] const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_) {
    for (size_t i = 0; i < count; i++) {
      mirror::Object* obj = *roots[i];
      if (mark_compact_->MarkObjectNonNullNoPush</*kParallel*/true>(obj)) {
        Push(obj);
      }
    }
  }

  void VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                  size_t count,
                  [[maybe_unused]] const RootInfo& info) override
      REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_) {
    for (size_t i = 0; i < count; i++) {
      mirror::Object* obj = roots[i]->AsMirrorPtr();
      if (mark_compact_->MarkObjectNonNullNoPush</*kParallel*/true>(obj)) {
        Push(obj);
      }
    }
  }

 private:
  void Flush() REQUIRES_SHARED(Locks::mutator_lock_)
               REQUIRES(Locks::heap_bitmap_lock_) {
    StackReference<mirror::Object>* start;
    StackReference<mirror::Object>* end;
    {
      MutexLock mu(self_, mark_compact_->lock_);
      // Loop here because even after expanding once it may not be sufficient to
      // accommodate all references. It's almost impossible, but there is no harm
      // in implementing it this way.
      while (!mark_compact_->mark_stack_->BumpBack(idx_, &start, &end)) {
        mark_compact_->ExpandMarkStack();
      }
    }
    while (idx_ > 0) {
      *start++ = roots_[--idx_];
    }
    DCHECK_EQ(start, end);
  }

  void Push(mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_)
                                 REQUIRES(Locks::heap_bitmap_lock_) {
    if (UNLIKELY(idx_ >= kBufferSize)) {
      Flush();
    }
    roots_[idx_++].Assign(obj);
  }

  StackReference<mirror::Object> roots_[kBufferSize];
  size_t idx_ = 0;
  MarkCompact* const mark_compact_;
  Thread* const self_;
};

class MarkCompact::CheckpointMarkThreadRoots : public Closure {
 public:
  explicit CheckpointMarkThreadRoots(MarkCompact* mark_compact) : mark_compact_(mark_compact) {}

  void Run(Thread* thread) override NO_THREAD_SAFETY_ANALYSIS {
    ScopedTrace trace("Marking thread roots");
    // Note: self is not necessarily equal to thread since thread may be
    // suspended.
    Thread* const self = Thread::Current();
    CHECK(thread == self
          || thread->IsSuspended()
          || thread->GetState() == ThreadState::kWaitingPerformingGc)
        << thread->GetState() << " thread " << thread << " self " << self;
    {
      ThreadRootsVisitor</*kBufferSize*/ 20> visitor(mark_compact_, self);
      thread->VisitRoots(&visitor, kVisitRootFlagAllRoots);
    }
    // Clear page-buffer to prepare for compaction phase.
    thread->SetThreadLocalGcBuffer(nullptr);

    // If thread is a running mutator, then act on behalf of the garbage
    // collector. See the code in ThreadList::RunCheckpoint.
    mark_compact_->GetBarrier().Pass(self);
  }

 private:
  MarkCompact* const mark_compact_;
};

void MarkCompact::MarkRootsCheckpoint(Thread* self, Runtime* runtime) {
  // We revote TLABs later during paused round of marking.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  CheckpointMarkThreadRoots check_point(this);
  ThreadList* thread_list = runtime->GetThreadList();
  gc_barrier_.Init(self, 0);
  // Request the check point is run on all threads returning a count of the threads that must
  // run through the barrier including self.
  size_t barrier_count = thread_list->RunCheckpoint(&check_point);
  // Release locks then wait for all mutator threads to pass the barrier.
  // If there are no threads to wait which implys that all the checkpoint functions are finished,
  // then no need to release locks.
  if (barrier_count == 0) {
    return;
  }
  Locks::heap_bitmap_lock_->ExclusiveUnlock(self);
  Locks::mutator_lock_->SharedUnlock(self);
  {
    ScopedThreadStateChange tsc(self, ThreadState::kWaitingForCheckPointsToRun);
    gc_barrier_.Increment(self, barrier_count);
  }
  Locks::mutator_lock_->SharedLock(self);
  Locks::heap_bitmap_lock_->ExclusiveLock(self);
  ProcessMarkStack();
}

void MarkCompact::MarkNonThreadRoots(Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  runtime->VisitNonThreadRoots(this);
  ProcessMarkStack();
}

void MarkCompact::MarkConcurrentRoots(VisitRootFlags flags, Runtime* runtime) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  runtime->VisitConcurrentRoots(this, flags);
  ProcessMarkStack();
}

void MarkCompact::RevokeAllThreadLocalBuffers() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  bump_pointer_space_->RevokeAllThreadLocalBuffers();
}

class MarkCompact::ScanObjectVisitor {
 public:
  explicit ScanObjectVisitor(MarkCompact* const mark_compact) ALWAYS_INLINE
      : mark_compact_(mark_compact) {}

  void operator()(ObjPtr<mirror::Object> obj) const
      ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    mark_compact_->ScanObject</*kUpdateLiveWords*/ false>(obj.Ptr());
  }

 private:
  MarkCompact* const mark_compact_;
};

void MarkCompact::UpdateAndMarkModUnion() {
  accounting::CardTable* const card_table = heap_->GetCardTable();
  for (const auto& space : immune_spaces_.GetSpaces()) {
    const char* name = space->IsZygoteSpace()
        ? "UpdateAndMarkZygoteModUnionTable"
        : "UpdateAndMarkImageModUnionTable";
    DCHECK(space->IsZygoteSpace() || space->IsImageSpace()) << *space;
    TimingLogger::ScopedTiming t(name, GetTimings());
    accounting::ModUnionTable* table = heap_->FindModUnionTableFromSpace(space);
    if (table != nullptr) {
      // UpdateAndMarkReferences() doesn't visit Reference-type objects. But
      // that's fine because these objects are immutable enough (referent can
      // only be cleared) and hence the only referents they can have are intra-space.
      table->UpdateAndMarkReferences(this);
    } else {
      // No mod-union table, scan all dirty/aged cards in the corresponding
      // card-table. This can only occur for app images.
      card_table->Scan</*kClearCard*/ false>(space->GetMarkBitmap(),
                                             space->Begin(),
                                             space->End(),
                                             ScanObjectVisitor(this),
                                             gc::accounting::CardTable::kCardAged);
    }
  }
}

void MarkCompact::ScanOldGenObjects() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  accounting::CardTable* const card_table = heap_->GetCardTable();
  // Moving space
  card_table->Scan</*kClearCard=*/false>(moving_space_bitmap_,
                                         moving_space_begin_,
                                         old_gen_end_,
                                         ScanObjectVisitor(this),
                                         gc::accounting::CardTable::kCardAged2);
  ProcessMarkStack();
  // Non-moving space
  card_table->Scan</*kClearCard=*/false>(non_moving_space_bitmap_,
                                         non_moving_space_->Begin(),
                                         non_moving_space_->End(),
                                         ScanObjectVisitor(this),
                                         gc::accounting::CardTable::kCardAged2);
  ProcessMarkStack();
}

void MarkCompact::MarkReachableObjects() {
  UpdateAndMarkModUnion();
  // Recursively mark all the non-image bits set in the mark bitmap.
  ProcessMarkStack();
  if (young_gen_) {
    // For the object overlapping on the old-gen boundary, we need to visit it
    // to make sure that we don't miss the references in the mid-gen area, and
    // also update the corresponding liveness info.
    if (old_gen_end_ > moving_space_begin_) {
      uintptr_t old_gen_end = reinterpret_cast<uintptr_t>(old_gen_end_);
      mirror::Object* obj = moving_space_bitmap_->FindPrecedingObject(old_gen_end - kAlignment);
      if (obj != nullptr) {
        size_t obj_size = obj->SizeOf<kDefaultVerifyFlags>();
        if (reinterpret_cast<uintptr_t>(obj) + RoundUp(obj_size, kAlignment) > old_gen_end) {
          ScanObject</*kUpdateLiveWords=*/true>(obj);
        }
      }
    }
    ScanOldGenObjects();
  }
}

void MarkCompact::ScanDirtyObjects(bool paused, uint8_t minimum_age) {
  accounting::CardTable* card_table = heap_->GetCardTable();
  for (const auto& space : heap_->GetContinuousSpaces()) {
    const char* name = nullptr;
    switch (space->GetGcRetentionPolicy()) {
    case space::kGcRetentionPolicyNeverCollect:
      name = paused ? "(Paused)ScanGrayImmuneSpaceObjects" : "ScanGrayImmuneSpaceObjects";
      break;
    case space::kGcRetentionPolicyFullCollect:
      name = paused ? "(Paused)ScanGrayZygoteSpaceObjects" : "ScanGrayZygoteSpaceObjects";
      break;
    case space::kGcRetentionPolicyAlwaysCollect:
      DCHECK(space == bump_pointer_space_ || space == non_moving_space_);
      name = paused ? "(Paused)ScanGrayAllocSpaceObjects" : "ScanGrayAllocSpaceObjects";
      break;
    }
    TimingLogger::ScopedTiming t(name, GetTimings());
    if (paused && use_generational_ &&
        space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect) {
      DCHECK_EQ(minimum_age, accounting::CardTable::kCardDirty);
      auto mod_visitor = [](uint8_t* card, uint8_t cur_val) {
        DCHECK_EQ(cur_val, accounting::CardTable::kCardDirty);
        *card = accounting::CardTable::kCardAged;
      };

      card_table->Scan</*kClearCard=*/false>(space->GetMarkBitmap(),
                                             space->Begin(),
                                             space->End(),
                                             ScanObjectVisitor(this),
                                             mod_visitor,
                                             minimum_age);
    } else {
      card_table->Scan</*kClearCard=*/false>(space->GetMarkBitmap(),
                                             space->Begin(),
                                             space->End(),
                                             ScanObjectVisitor(this),
                                             minimum_age);
    }
    ProcessMarkStack();
  }
}

void MarkCompact::RecursiveMarkDirtyObjects(bool paused, uint8_t minimum_age) {
  ScanDirtyObjects(paused, minimum_age);
  CHECK(mark_stack_->IsEmpty());
}

void MarkCompact::MarkRoots(VisitRootFlags flags) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  Runtime* runtime = Runtime::Current();
  // Make sure that the checkpoint which collects the stack roots is the first
  // one capturning GC-roots. As this one is supposed to find the address
  // everything allocated after that (during this marking phase) will be
  // considered 'marked'.
  MarkRootsCheckpoint(thread_running_gc_, runtime);
  MarkNonThreadRoots(runtime);
  MarkConcurrentRoots(flags, runtime);
}

void MarkCompact::PreCleanCards() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  CHECK(!Locks::mutator_lock_->IsExclusiveHeld(thread_running_gc_));
  // Age the card-table before thread stack scanning checkpoint in MarkRoots()
  // as it ensures that there are no in-progress write barriers which started
  // prior to aging the card-table.
  PrepareForMarking(/*pre_marking=*/false);
  MarkRoots(static_cast<VisitRootFlags>(kVisitRootFlagClearRootLog | kVisitRootFlagNewRoots));
  RecursiveMarkDirtyObjects(/*paused*/ false, accounting::CardTable::kCardDirty - 1);
}

// In a concurrent marking algorithm, if we are not using a write/read barrier, as
// in this case, then we need a stop-the-world (STW) round in the end to mark
// objects which were written into concurrently while concurrent marking was
// performed.
// In order to minimize the pause time, we could take one of the two approaches:
// 1. Keep repeating concurrent marking of dirty cards until the time spent goes
// below a threshold.
// 2. Do two rounds concurrently and then attempt a paused one. If we figure
// that it's taking too long, then resume mutators and retry.
//
// Given the non-trivial fixed overhead of running a round (card table and root
// scan), it might be better to go with approach 2.
void MarkCompact::MarkingPhase() {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
  MaybeClampGcStructures();
  PrepareForMarking(/*pre_marking=*/true);
  MarkZygoteLargeObjects();
  MarkRoots(
        static_cast<VisitRootFlags>(kVisitRootFlagAllRoots | kVisitRootFlagStartLoggingNewRoots));
  MarkReachableObjects();
  // Pre-clean dirtied cards to reduce pauses.
  PreCleanCards();

  // Setup reference processing and forward soft references once before enabling
  // slow path (in MarkingPause)
  ReferenceProcessor* rp = GetHeap()->GetReferenceProcessor();
  bool clear_soft_references = GetCurrentIteration()->GetClearSoftReferences();
  rp->Setup(thread_running_gc_, this, /*concurrent=*/ true, clear_soft_references);
  if (!clear_soft_references) {
    // Forward as many SoftReferences as possible before inhibiting reference access.
    rp->ForwardSoftReferences(GetTimings());
  }
}

class MarkCompact::RefFieldsVisitor {
 public:
  ALWAYS_INLINE RefFieldsVisitor(MarkCompact* const mark_compact)
      : mark_compact_(mark_compact),
        young_gen_begin_(mark_compact->mid_gen_end_),
        young_gen_end_(mark_compact->moving_space_end_),
        dirty_card_(false),
        // Ideally we should only check for objects outside young-gen. However,
        // the boundary of young-gen can change later in PrepareForCompaction()
        // as we need the mid-gen-end to be page-aligned. Since most of the
        // objects don't have native-roots, it's not too costly to check all
        // objects being visited during marking.
        check_native_roots_to_young_gen_(mark_compact->use_generational_) {}

  bool ShouldDirtyCard() const { return dirty_card_; }

  ALWAYS_INLINE void operator()(mirror::Object* obj,
                                MemberOffset offset,
                                [[maybe_unused]] bool is_static) const
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (kCheckLocks) {
      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
    }
    mirror::Object* ref = obj->GetFieldObject<mirror::Object>(offset);
    mark_compact_->MarkObject(ref, obj, offset);
  }

  void operator()(ObjPtr<mirror::Class> klass, ObjPtr<mirror::Reference> ref) const ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    mark_compact_->DelayReferenceReferent(klass, ref);
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const ALWAYS_INLINE
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (kCheckLocks) {
      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
    }
    mirror::Object* ref = root->AsMirrorPtr();
    mark_compact_->MarkObject(ref);
    if (check_native_roots_to_young_gen_) {
      dirty_card_ |= reinterpret_cast<uint8_t*>(ref) >= young_gen_begin_ &&
                     reinterpret_cast<uint8_t*>(ref) < young_gen_end_;
    }
  }

 private:
  MarkCompact* const mark_compact_;
  uint8_t* const young_gen_begin_;
  uint8_t* const young_gen_end_;
  mutable bool dirty_card_;
  const bool check_native_roots_to_young_gen_;
};

template <size_t kAlignment>
size_t MarkCompact::LiveWordsBitmap<kAlignment>::LiveBytesInBitmapWord(size_t chunk_idx) const {
  const size_t index = chunk_idx * kBitmapWordsPerVectorWord;
  size_t words = 0;
  for (uint32_t i = 0; i < kBitmapWordsPerVectorWord; i++) {
    words += POPCOUNT(Bitmap::Begin()[index + i]);
  }
  return words * kAlignment;
}

void MarkCompact::UpdateLivenessInfo(mirror::Object* obj, size_t obj_size) {
  DCHECK(obj != nullptr);
  DCHECK_EQ(obj_size, obj->SizeOf<kDefaultVerifyFlags>());
  uintptr_t obj_begin = reinterpret_cast<uintptr_t>(obj);
  UpdateClassAfterObjectMap(obj);
  size_t size = RoundUp(obj_size, kAlignment);
  uintptr_t bit_index = live_words_bitmap_->SetLiveWords(obj_begin, size);
  size_t chunk_idx =
      (obj_begin - reinterpret_cast<uintptr_t>(moving_space_begin_)) / kOffsetChunkSize;
  // Compute the bit-index within the chunk-info vector word.
  bit_index %= kBitsPerVectorWord;
  size_t first_chunk_portion = std::min(size, (kBitsPerVectorWord - bit_index) * kAlignment);
  chunk_info_vec_[chunk_idx] += first_chunk_portion;
  DCHECK_LE(chunk_info_vec_[chunk_idx], kOffsetChunkSize)
      << "first_chunk_portion:" << first_chunk_portion
      << " obj-size:" << RoundUp(obj_size, kAlignment);
  chunk_idx++;
  DCHECK_LE(first_chunk_portion, size);
  for (size -= first_chunk_portion; size > kOffsetChunkSize; size -= kOffsetChunkSize) {
    DCHECK_EQ(chunk_info_vec_[chunk_idx], 0u);
    chunk_info_vec_[chunk_idx++] = kOffsetChunkSize;
  }
  chunk_info_vec_[chunk_idx] += size;
  DCHECK_LE(chunk_info_vec_[chunk_idx], kOffsetChunkSize)
      << "size:" << size << " obj-size:" << RoundUp(obj_size, kAlignment);
}

template <bool kUpdateLiveWords>
void MarkCompact::ScanObject(mirror::Object* obj) {
  mirror::Class* klass = obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
  // TODO(lokeshgidra): Remove the following condition once b/373609505 is fixed.
  if (UNLIKELY(klass == nullptr)) {
    // It was seen in ConcurrentCopying GC that after a small wait when we reload
    // the class pointer, it turns out to be a valid class object. So as a workaround,
    // we can continue execution and log an error that this happened.
    for (size_t i = 0; i < 1000; i++) {
      // Wait for 1ms at a time. Don't wait for more than 1 second in total.
      usleep(1000);
      klass = obj->GetClass<kVerifyNone, kWithoutReadBarrier>();
      if (klass != nullptr) {
        break;
      }
    }
    if (klass == nullptr) {
      // It must be heap corruption.
      LOG(FATAL_WITHOUT_ABORT) << "klass pointer for obj: " << obj << " found to be null."
                               << " black_dense_end: " << static_cast<void*>(black_dense_end_)
                               << " mid_gen_end: " << static_cast<void*>(mid_gen_end_)
                               << " prev_post_compact_end: " << prev_post_compact_end_
                               << " prev_black_allocations_begin: " << prev_black_allocations_begin_
                               << " prev_black_dense_end: " << prev_black_dense_end_
                               << " prev_gc_young: " << prev_gc_young_
                               << " prev_gc_performed_compaction: "
                               << prev_gc_performed_compaction_;
      heap_->GetVerification()->LogHeapCorruption(
          obj, mirror::Object::ClassOffset(), klass, /*fatal=*/true);
    }
  }
  // The size of `obj` is used both here (to update `bytes_scanned_`) and in
  // `UpdateLivenessInfo`. As fetching this value can be expensive, do it once
  // here and pass that information to `UpdateLivenessInfo`.
  size_t obj_size = obj->SizeOf<kDefaultVerifyFlags>();
  bytes_scanned_ += obj_size;

  RefFieldsVisitor visitor(this);
  DCHECK(IsMarked(obj)) << "Scanning marked object " << obj << "\n" << heap_->DumpSpaces();
  if (kUpdateLiveWords && HasAddress(obj)) {
    UpdateLivenessInfo(obj, obj_size);
    freed_objects_--;
  }
  obj->VisitReferences(visitor, visitor);
  // old-gen cards for objects containing references to mid-gen needs to be kept
  // dirty for re-scan in the next GC cycle. We take care of that majorly during
  // compaction-phase as that enables us to implicitly take care of
  // black-allocated objects as well. Unfortunately, since we don't visit
  // native-roots during compaction, that has to be captured during marking.
  //
  // Note that we can't dirty the cards right away because then we will wrongly
  // age them during re-scan of this marking-phase, and thereby may loose them
  // by the end of the GC cycle.
  if (visitor.ShouldDirtyCard()) {
    dirty_cards_later_vec_.push_back(obj);
  }
}

// Scan anything that's on the mark stack.
void MarkCompact::ProcessMarkStack() {
  // TODO: eventually get rid of this as we now call this function quite a few times.
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  // TODO: try prefetch like in CMS
  while (!mark_stack_->IsEmpty()) {
    mirror::Object* obj = mark_stack_->PopBack();
    DCHECK(obj != nullptr);
    ScanObject</*kUpdateLiveWords*/ true>(obj);
  }
}

void MarkCompact::ExpandMarkStack() {
  const size_t new_size = mark_stack_->Capacity() * 2;
  std::vector<StackReference<mirror::Object>> temp(mark_stack_->Begin(),
                                                   mark_stack_->End());
  mark_stack_->Resize(new_size);
  for (auto& ref : temp) {
    mark_stack_->PushBack(ref.AsMirrorPtr());
  }
  DCHECK(!mark_stack_->IsFull());
}

inline void MarkCompact::PushOnMarkStack(mirror::Object* obj) {
  if (UNLIKELY(mark_stack_->IsFull())) {
    ExpandMarkStack();
  }
  mark_stack_->PushBack(obj);
}

inline void MarkCompact::MarkObjectNonNull(mirror::Object* obj,
                                           mirror::Object* holder,
                                           MemberOffset offset) {
  DCHECK(obj != nullptr);
  if (MarkObjectNonNullNoPush</*kParallel*/false>(obj, holder, offset)) {
    PushOnMarkStack(obj);
  }
}

template <bool kParallel>
inline bool MarkCompact::MarkObjectNonNullNoPush(mirror::Object* obj,
                                                 mirror::Object* holder,
                                                 MemberOffset offset) {
  // We expect most of the referenes to be in bump-pointer space, so try that
  // first to keep the cost of this function minimal.
  if (LIKELY(HasAddress(obj))) {
    // If obj is in old-gen (during young-gc) then we shouldn't add it to
    // mark-stack to limit marking to young generation.
    if (young_gen_ && reinterpret_cast<uint8_t*>(obj) < old_gen_end_) {
      DCHECK(moving_space_bitmap_->Test(obj));
      return false;
    }
    return kParallel ? !moving_space_bitmap_->AtomicTestAndSet(obj)
                     : !moving_space_bitmap_->Set(obj);
  } else if (non_moving_space_bitmap_->HasAddress(obj)) {
    return kParallel ? !non_moving_space_bitmap_->AtomicTestAndSet(obj)
                     : !non_moving_space_bitmap_->Set(obj);
  } else if (immune_spaces_.ContainsObject(obj)) {
    DCHECK(IsMarked(obj) != nullptr);
    return false;
  } else {
    // Must be a large-object space, otherwise it's a case of heap corruption.
    if (!IsAlignedParam(obj, space::LargeObjectSpace::ObjectAlignment())) {
      // Objects in large-object space are aligned to the large-object alignment.
      // So if we have an object which doesn't belong to any space and is not
      // page-aligned as well, then it's memory corruption.
      // TODO: implement protect/unprotect in bump-pointer space.
      heap_->GetVerification()->LogHeapCorruption(holder, offset, obj, /*fatal*/ true);
    }
    DCHECK_NE(heap_->GetLargeObjectsSpace(), nullptr)
        << "ref=" << obj
        << " doesn't belong to any of the spaces and large object space doesn't exist";
    accounting::LargeObjectBitmap* los_bitmap = heap_->GetLargeObjectsSpace()->GetMarkBitmap();
    DCHECK(los_bitmap->HasAddress(obj));
    if (kParallel) {
      los_bitmap->AtomicTestAndSet(obj);
    } else {
      los_bitmap->Set(obj);
    }
    // We only have primitive arrays in large object space. So there is no
    // reason to push into mark-stack.
    DCHECK(obj->IsString() || (obj->IsArrayInstance() && !obj->IsObjectArray()));
    return false;
  }
}

inline void MarkCompact::MarkObject(mirror::Object* obj,
                                    mirror::Object* holder,
                                    MemberOffset offset) {
  if (obj != nullptr) {
    MarkObjectNonNull(obj, holder, offset);
  }
}

mirror::Object* MarkCompact::MarkObject(mirror::Object* obj) {
  MarkObject(obj, nullptr, MemberOffset(0));
  return obj;
}

void MarkCompact::MarkHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                    [[maybe_unused]] bool do_atomic_update) {
  MarkObject(obj->AsMirrorPtr(), nullptr, MemberOffset(0));
}

void MarkCompact::VisitRoots(mirror::Object*** roots,
                             size_t count,
                             const RootInfo& info) {
  if (compacting_) {
    uint8_t* moving_space_begin = black_dense_end_;
    uint8_t* moving_space_end = moving_space_end_;
    for (size_t i = 0; i < count; ++i) {
      UpdateRoot(roots[i], moving_space_begin, moving_space_end, info);
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      MarkObjectNonNull(*roots[i]);
    }
  }
}

void MarkCompact::VisitRoots(mirror::CompressedReference<mirror::Object>** roots,
                             size_t count,
                             const RootInfo& info) {
  // TODO: do we need to check if the root is null or not?
  if (compacting_) {
    uint8_t* moving_space_begin = black_dense_end_;
    uint8_t* moving_space_end = moving_space_end_;
    for (size_t i = 0; i < count; ++i) {
      UpdateRoot(roots[i], moving_space_begin, moving_space_end, info);
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      MarkObjectNonNull(roots[i]->AsMirrorPtr());
    }
  }
}

mirror::Object* MarkCompact::IsMarked(mirror::Object* obj) {
  if (HasAddress(obj)) {
    const bool is_black = reinterpret_cast<uint8_t*>(obj) >= black_allocations_begin_;
    if (compacting_) {
      if (is_black) {
        return PostCompactBlackObjAddr(obj);
      } else if (moving_space_bitmap_->Test(obj)) {
        if (reinterpret_cast<uint8_t*>(obj) < black_dense_end_) {
          return obj;
        } else {
          return PostCompactOldObjAddr(obj);
        }
      } else {
        return nullptr;
      }
    }
    return (is_black || moving_space_bitmap_->Test(obj)) ? obj : nullptr;
  } else if (non_moving_space_bitmap_->HasAddress(obj)) {
    if (non_moving_space_bitmap_->Test(obj)) {
      return obj;
    }
  } else if (immune_spaces_.ContainsObject(obj)) {
    return obj;
  } else {
    DCHECK(heap_->GetLargeObjectsSpace())
        << "ref=" << obj
        << " doesn't belong to any of the spaces and large object space doesn't exist";
    accounting::LargeObjectBitmap* los_bitmap = heap_->GetLargeObjectsSpace()->GetMarkBitmap();
    if (los_bitmap->HasAddress(obj)) {
      DCHECK(IsAlignedParam(obj, space::LargeObjectSpace::ObjectAlignment()));
      if (los_bitmap->Test(obj)) {
        return obj;
      }
    } else {
      // The given obj is not in any of the known spaces, so return null. This could
      // happen for instance in interpreter caches wherein a concurrent updation
      // to the cache could result in obj being a non-reference. This is
      // tolerable because SweepInterpreterCaches only updates if the given
      // object has moved, which can't be the case for the non-reference.
      return nullptr;
    }
  }
  return marking_done_ && IsOnAllocStack(obj) ? obj : nullptr;
}

bool MarkCompact::IsNullOrMarkedHeapReference(mirror::HeapReference<mirror::Object>* obj,
                                              [[maybe_unused]] bool do_atomic_update) {
  mirror::Object* ref = obj->AsMirrorPtr();
  if (ref == nullptr) {
    return true;
  }
  return IsMarked(ref);
}

// Process the 'referent' field in a java.lang.ref.Reference. If the referent
// has not yet been marked, put it on the appropriate list in the heap for later
// processing.
void MarkCompact::DelayReferenceReferent(ObjPtr<mirror::Class> klass,
                                         ObjPtr<mirror::Reference> ref) {
  heap_->GetReferenceProcessor()->DelayReferenceReferent(klass, ref, this);
}

template <typename Visitor>
class MarkCompact::VisitReferencesVisitor {
 public:
  explicit VisitReferencesVisitor(Visitor visitor) : visitor_(visitor) {}

  ALWAYS_INLINE void operator()(mirror::Object* obj,
                                MemberOffset offset,
                                [[maybe_unused]] bool is_static) const
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    visitor_(obj->GetFieldObject<mirror::Object>(offset));
  }

  ALWAYS_INLINE void operator()([[maybe_unused]] ObjPtr<mirror::Class> klass,
                                ObjPtr<mirror::Reference> ref) const
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    visitor_(ref.Ptr());
  }

  void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root) const
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    if (!root->IsNull()) {
      VisitRoot(root);
    }
  }

  void VisitRoot(mirror::CompressedReference<mirror::Object>* root) const
      REQUIRES(Locks::heap_bitmap_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
    visitor_(root->AsMirrorPtr());
  }

 private:
  Visitor visitor_;
};

void MarkCompact::VerifyNoMissingCardMarks() {
  if (kVerifyNoMissingCardMarks) {
    accounting::CardTable* card_table = heap_->GetCardTable();
    auto obj_visitor = [&](mirror::Object* obj) REQUIRES_SHARED(Locks::mutator_lock_) {
      bool found = false;
      VisitReferencesVisitor visitor(
          [begin = old_gen_end_, end = moving_space_end_, &found](mirror::Object* ref) {
            found |= ref >= reinterpret_cast<mirror::Object*>(begin) &&
                     ref < reinterpret_cast<mirror::Object*>(end);
          });
      obj->VisitReferences</*kVisitNativeRoots=*/true>(visitor, visitor);
      if (found) {
        size_t obj_size = RoundUp(obj->SizeOf<kDefaultVerifyFlags>(), kAlignment);
        if (!card_table->IsDirty(obj) &&
            reinterpret_cast<uint8_t*>(obj) + obj_size <= old_gen_end_) {
          std::ostringstream oss;
          obj->DumpReferences</*kDumpNativeRoots=*/true>(oss, /*dump_type_of=*/true);
          LOG(FATAL_WITHOUT_ABORT)
              << "Object " << obj << " (" << obj->PrettyTypeOf()
              << ") has references to mid-gen/young-gen:"
              << "\n obj-size = " << obj_size
              << "\n old-gen-end = " << static_cast<void*>(old_gen_end_)
              << "\n mid-gen-end = " << static_cast<void*>(mid_gen_end_) << "\n references =\n"
              << oss.str();
          heap_->GetVerification()->LogHeapCorruption(
              /*holder=*/nullptr, MemberOffset(0), obj, /*fatal=*/true);
        }
      }
    };
    moving_space_bitmap_->VisitMarkedRange(reinterpret_cast<uintptr_t>(moving_space_begin_),
                                           reinterpret_cast<uintptr_t>(old_gen_end_),
                                           obj_visitor);
  }
}

void MarkCompact::VerifyPostGCObjects(bool performed_compaction, uint8_t* mark_bitmap_clear_end) {
  if (kVerifyPostGCObjects) {
    mirror::Object* last_visited_obj = nullptr;
    auto obj_visitor =
        [&](mirror::Object* obj, bool verify_bitmap = false) REQUIRES_SHARED(Locks::mutator_lock_) {
          std::vector<mirror::Object*> invalid_refs;
          if (verify_bitmap && !moving_space_bitmap_->Test(obj)) {
            LOG(FATAL) << "Obj " << obj << " (" << obj->PrettyTypeOf()
                       << ") doesn't have mark-bit set"
                       << "\n prev-black-dense-end = " << static_cast<void*>(prev_black_dense_end_)
                       << "\n old-gen-end = " << static_cast<void*>(old_gen_end_)
                       << "\n mid-gen-end = " << static_cast<void*>(mid_gen_end_);
          }
          VisitReferencesVisitor visitor(
              [verification = heap_->GetVerification(), &invalid_refs](mirror::Object* ref)
                  REQUIRES_SHARED(Locks::mutator_lock_) {
                    if (ref != nullptr && !verification->IsValidObject(ref)) {
                      invalid_refs.push_back(ref);
                    }
                  });
          obj->VisitReferences</*kVisitNativeRoots=*/true>(visitor, visitor);
          if (!invalid_refs.empty()) {
            std::ostringstream oss;
            for (mirror::Object* ref : invalid_refs) {
              oss << ref << " ";
            }
            LOG(FATAL_WITHOUT_ABORT)
                << "Object " << obj << " (" << obj->PrettyTypeOf() << ") has invalid references:\n"
                << oss.str() << "\ncard = " << static_cast<int>(heap_->GetCardTable()->GetCard(obj))
                << "\n prev-black-dense-end = " << static_cast<void*>(prev_black_dense_end_)
                << "\n old-gen-end = " << static_cast<void*>(old_gen_end_)
                << "\n mid-gen-end = " << static_cast<void*>(mid_gen_end_)
                << "\n black-allocations-begin = " << static_cast<void*>(black_allocations_begin_);

            // Calling PrettyTypeOf() on a stale reference mostly results in segfault.
            oss.str("");
            obj->DumpReferences</*kDumpNativeRoots=*/true>(oss, /*dump_type_of=*/false);
            LOG(FATAL_WITHOUT_ABORT) << "\n references =\n" << oss.str();

            heap_->GetVerification()->LogHeapCorruption(
                /*holder=*/nullptr, MemberOffset(0), obj, /*fatal=*/true);
          }
          last_visited_obj = obj;
        };
    non_moving_space_bitmap_->VisitAllMarked(obj_visitor);
    last_visited_obj = nullptr;
    // We should verify all objects that have survived, which means old and mid-gen
    // Objects that were promoted to old-gen and mid-gen in this GC cycle are tightly
    // packed, except if compaction was not performed. So we use object size to walk
    // the heap and also verify that the mark-bit is set in the tightly packed portion.
    moving_space_bitmap_->VisitMarkedRange(
        reinterpret_cast<uintptr_t>(moving_space_begin_),
        reinterpret_cast<uintptr_t>(performed_compaction ? prev_black_dense_end_
                                                         : mark_bitmap_clear_end),
        obj_visitor);
    if (performed_compaction) {
      mirror::Object* obj = last_visited_obj;
      if (obj == nullptr || AlignUp(reinterpret_cast<uint8_t*>(obj) + obj->SizeOf(), kAlignment) <
                                prev_black_dense_end_) {
        obj = reinterpret_cast<mirror::Object*>(prev_black_dense_end_);
      }
      while (reinterpret_cast<uint8_t*>(obj) < mid_gen_end_ && obj->GetClass() != nullptr) {
        // Objects in mid-gen will not have their corresponding mark-bits set.
        obj_visitor(obj, reinterpret_cast<void*>(obj) < black_dense_end_);
        uintptr_t next = reinterpret_cast<uintptr_t>(obj) + obj->SizeOf();
        obj = reinterpret_cast<mirror::Object*>(RoundUp(next, kAlignment));
      }
    }
  }
}

void MarkCompact::FinishPhase(bool performed_compaction) {
  TimingLogger::ScopedTiming t(__FUNCTION__, GetTimings());
  GetCurrentIteration()->SetScannedBytes(bytes_scanned_);
  bool is_zygote = Runtime::Current()->IsZygote();
  compacting_ = false;
  marking_done_ = false;
  uint8_t* mark_bitmap_clear_end = black_dense_end_;
  LOG(DEBUG) << "ART-GC black_dense_end:" << static_cast<void*>(black_dense_end_)
             << " mid_gen_end:" << static_cast<void*>(mid_gen_end_)
             << " post_compact_end:" << static_cast<void*>(post_compact_end_)
             << " black_allocations_begin:" << static_cast<void*>(black_allocations_begin_)
             << " young:" << young_gen_ << " performed_compaction:" << performed_compaction;

  // Retain values of some fields for logging in next GC cycle, in case there is
  // a memory corruption detected.
  prev_black_allocations_begin_ = static_cast<void*>(black_allocations_begin_);
  prev_black_dense_end_ = static_cast<void*>(black_dense_end_);
  prev_post_compact_end_ = static_cast<void*>(post_compact_end_);
  prev_gc_young_ = young_gen_;
  prev_gc_performed_compaction_ = performed_compaction;

  // Whether compaction is performend or not, we always set post_compact_end_
  // before reaching here.
  CHECK_NE(post_compact_end_, nullptr);
  if (use_generational_) {
    {
      ReaderMutexLock mu(thread_running_gc_, *Locks::mutator_lock_);
      // We need to retain and update class-after-object map for old-gen as
      // that won't be created in next young-gc.
      // Jump to the first class which is getting promoted to old-gen. Since
      // it is not compacted, references into old-gen don't need to be udated.
      // All pairs in mid-gen will be updated with post-compact addresses and
      // retained, as mid-gen is getting consumed into old-gen now. All pairs
      // after mid-gen will be erased as they are not required in next GC cycle.
      auto iter = class_after_obj_map_.lower_bound(
          ObjReference::FromMirrorPtr(reinterpret_cast<mirror::Object*>(old_gen_end_)));
      while (iter != class_after_obj_map_.end()) {
        mirror::Object* klass = iter->first.AsMirrorPtr();
        mirror::Object* obj = iter->second.AsMirrorPtr();
        DCHECK_GT(klass, obj);
        // Black allocations begin after marking-pause. Therefore, we cannot
        // have a situation wherein class is allocated after the pause while its
        // object is before.
        if (reinterpret_cast<uint8_t*>(klass) >= black_allocations_begin_) {
          for (auto it = iter; it != class_after_obj_map_.end(); it++) {
            DCHECK_GE(reinterpret_cast<uint8_t*>(it->second.AsMirrorPtr()),
                      black_allocations_begin_);
          }
          class_after_obj_map_.erase(iter, class_after_obj_map_.end());
          break;
        }

        DCHECK(moving_space_bitmap_->Test(klass));
        DCHECK(moving_space_bitmap_->Test(obj));
        // As 'mid_gen_end_' is where our old-gen will end now, compute compacted
        // addresses of <class, object> for comparisons and updating in the map.
        mirror::Object* compacted_klass = klass;
        mirror::Object* compacted_obj = obj;
        if (performed_compaction) {
          compacted_klass = PostCompactAddress(klass, old_gen_end_, moving_space_end_);
          compacted_obj = PostCompactAddress(obj, old_gen_end_, moving_space_end_);
          DCHECK_GT(compacted_klass, compacted_obj);
        }
        if (reinterpret_cast<uint8_t*>(compacted_obj) >= mid_gen_end_) {
          iter = class_after_obj_map_.erase(iter);
          continue;
        } else if (mid_to_old_promo_bit_vec_.get() != nullptr) {
          if (reinterpret_cast<uint8_t*>(compacted_klass) >= old_gen_end_) {
            DCHECK(mid_to_old_promo_bit_vec_->IsBitSet(
                (reinterpret_cast<uint8_t*>(compacted_obj) - old_gen_end_) / kAlignment));
          }
          if (reinterpret_cast<uint8_t*>(compacted_klass) < mid_gen_end_) {
            DCHECK(mid_to_old_promo_bit_vec_->IsBitSet(
                (reinterpret_cast<uint8_t*>(compacted_klass) - old_gen_end_) / kAlignment));
          }
        }
        if (performed_compaction) {
          auto nh = class_after_obj_map_.extract(iter++);
          nh.key() = ObjReference::FromMirrorPtr(compacted_klass);
          nh.mapped() = ObjReference::FromMirrorPtr(compacted_obj);
          auto success = class_after_obj_map_.insert(iter, std::move(nh));
          CHECK_EQ(success->first.AsMirrorPtr(), compacted_klass);
        } else {
          iter++;
        }
      }

      // Dirty the cards for objects captured from native-roots during marking-phase.
      accounting::CardTable* card_table = heap_->GetCardTable();
      for (auto obj : dirty_cards_later_vec_) {
        // Only moving and non-moving spaces are relevant as the remaining
        // spaces are all immune-spaces which anyways use card-table.
        if (HasAddress(obj)) {
          // Objects in young-gen that refer to other young-gen objects don't
          // need to be tracked.
          // The vector contains pre-compact object references whereas
          // 'mid_gen_end_' is post-compact boundary. So compare against
          // post-compact object reference.
          mirror::Object* compacted_obj =
              performed_compaction ? PostCompactAddress(obj, black_dense_end_, moving_space_end_)
                                   : obj;
          if (reinterpret_cast<uint8_t*>(compacted_obj) < mid_gen_end_) {
            card_table->MarkCard(compacted_obj);
          }
        } else if (non_moving_space_->HasAddress(obj)) {
          card_table->MarkCard(obj);
        }
      }
    }
    dirty_cards_later_vec_.clear();

    // Copy mid-gen bitmap into moving-space's mark-bitmap
    if (mid_to_old_promo_bit_vec_.get() != nullptr) {
      DCHECK_EQ(mid_to_old_promo_bit_vec_->GetBitSizeOf(),
                (mid_gen_end_ - old_gen_end_) / kObjectAlignment);
      uint32_t* bitmap_begin = reinterpret_cast<uint32_t*>(moving_space_bitmap_->Begin());
      DCHECK(IsAligned<kObjectAlignment * BitVector::kWordBits>(gPageSize));
      size_t index = (old_gen_end_ - moving_space_begin_) / kObjectAlignment / BitVector::kWordBits;
      mid_to_old_promo_bit_vec_->CopyTo(&bitmap_begin[index],
                                        mid_to_old_promo_bit_vec_->GetSizeOf());
      mid_to_old_promo_bit_vec_.reset(nullptr);
    } else if (!performed_compaction) {
      // We typically only retain the mark-bitmap for the old-generation as the
      // objects following it are expected to be contiguous. However, when
      // compaction is not performed, we may have decided to tolerate few holes
      // here and there. So we have to retain the bitmap for the entire
      // 'compacted' portion of the heap, which is up to mid-gen-end.
      DCHECK_LE(old_gen_end_, post_compact_end_);
      mark_bitmap_clear_end = post_compact_end_;
    }
    // Promote all mid-gen objects to old-gen and young-gen objects to mid-gen
    // for next GC cycle.
    old_gen_end_ = mid_gen_end_;
    mid_gen_end_ = post_compact_end_;
    post_compact_end_ = nullptr;

    // Verify (in debug builds) after updating mark-bitmap if class-after-object
    // map is correct or not.
    for (auto iter : class_after_obj_map_) {
      DCHECK(moving_space_bitmap_->Test(iter.second.AsMirrorPtr()));
      mirror::Object* klass = iter.first.AsMirrorPtr();
      DCHECK_IMPLIES(!moving_space_bitmap_->Test(klass),
                     reinterpret_cast<uint8_t*>(klass) >= old_gen_end_);
    }
  } else {
    class_after_obj_map_.clear();
    if (!performed_compaction) {
      DCHECK_LE(old_gen_end_, post_compact_end_);
      mark_bitmap_clear_end = post_compact_end_;
    }
  }
  // Black-dense region, which requires bitmap for object-walk, could be larger
  // than old-gen. Therefore, until next GC retain the bitmap for entire
  // black-dense region. At the beginning of next cycle, we clear [old_gen_end_,
  // moving_space_end_).
  mark_bitmap_clear_end = std::max(black_dense_end_, mark_bitmap_clear_end);
  DCHECK_ALIGNED_PARAM(mark_bitmap_clear_end, gPageSize);
  if (moving_space_begin_ == mark_bitmap_clear_end) {
    moving_space_bitmap_->Clear();
  } else {
    DCHECK_LT(moving_space_begin_, mark_bitmap_clear_end);
    DCHECK_LE(mark_bitmap_clear_end, moving_space_end_);
    moving_space_bitmap_->ClearRange(reinterpret_cast<mirror::Object*>(mark_bitmap_clear_end),
                                     reinterpret_cast<mirror::Object*>(moving_space_end_));
  }
  bump_pointer_space_->SetBlackDenseRegionSize(mark_bitmap_clear_end - moving_space_begin_);

  if (UNLIKELY(is_zygote && IsValidFd(uffd_))) {
    // This unregisters all ranges as a side-effect.
    close(uffd_);
    uffd_ = kFdUnused;
    uffd_initialized_ = false;
  }
  CHECK(mark_stack_->IsEmpty());  // Ensure that the mark stack is empty.
  mark_stack_->Reset();
  ZeroAndReleaseMemory(compaction_buffers_map_.Begin(), compaction_buffers_map_.Size());
  info_map_.MadviseDontNeedAndZero();
  live_words_bitmap_->ClearBitmap();
  DCHECK_EQ(thread_running_gc_, Thread::Current());
  if (kIsDebugBuild) {
    MutexLock mu(thread_running_gc_, lock_);
    if (updated_roots_.get() != nullptr) {
      updated_roots_->clear();
    }
  }
  linear_alloc_arenas_.clear();
  {
    ReaderMutexLock mu(thread_running_gc_, *Locks::mutator_lock_);
    WriterMutexLock mu2(thread_running_gc_, *Locks::heap_bitmap_lock_);
    heap_->ClearMarkedObjects();
    if (use_generational_) {
      if (performed_compaction) {
        // Clear the bits set temporarily for black allocations in non-moving
        // space in UpdateNonMovingSpaceBlackAllocations(), which is called when
        // we perform compaction, so that objects are considered for GC in next cycle.
        accounting::ObjectStack* stack = heap_->GetAllocationStack();
        const StackReference<mirror::Object>* limit = stack->End();
        for (StackReference<mirror::Object>* it = stack->Begin(); it != limit; ++it) {
          mirror::Object* obj = it->AsMirrorPtr();
          if (obj != nullptr && non_moving_space_bitmap_->HasAddress(obj)) {
            non_moving_space_bitmap_->Clear(obj);
          }
        }
      } else {
        // Since we didn't perform compaction, we need to identify old objects
        // referring to the mid-gen.
        auto obj_visitor = [this, card_table = heap_->GetCardTable()](mirror::Object* obj) {
          bool found = false;
          VisitReferencesVisitor visitor(
              [begin = old_gen_end_, end = mid_gen_end_, &found](mirror::Object* ref) {
                found |= ref >= reinterpret_cast<mirror::Object*>(begin) &&
                         ref < reinterpret_cast<mirror::Object*>(end);
              });
          uint8_t* card = card_table->CardFromAddr(obj);
          if (*card == accounting::CardTable::kCardDirty) {
            return;
          }
          // Native-roots are captured during marking and the corresponding cards are already
          // dirtied above.
          obj->VisitReferences</*kVisitNativeRoots=*/false>(visitor, visitor);
          if (found) {
            *card = accounting::CardTable::kCardDirty;
          }
        };
        moving_space_bitmap_->VisitMarkedRange(reinterpret_cast<uintptr_t>(moving_space_begin_),
                                               reinterpret_cast<uintptr_t>(old_gen_end_),
                                               obj_visitor);
        non_moving_space_bitmap_->VisitAllMarked(obj_visitor);
      }
    }
  }
  GcVisitedArenaPool* arena_pool =
      static_cast<GcVisitedArenaPool*>(Runtime::Current()->GetLinearAllocArenaPool());
  arena_pool->DeleteUnusedArenas();

  if (kVerifyNoMissingCardMarks && use_generational_) {
    // This must be done in a pause as otherwise verification between mutation
    // and card-dirtying by a mutator will spuriosely fail.
    ScopedPause pause(this);
    WriterMutexLock mu(thread_running_gc_, *Locks::heap_bitmap_lock_);
    VerifyNoMissingCardMarks();
  }
  if (kVerifyPostGCObjects && use_generational_) {
    ReaderMutexLock mu(thread_running_gc_, *Locks::mutator_lock_);
    WriterMutexLock mu2(thread_running_gc_, *Locks::heap_bitmap_lock_);
    VerifyPostGCObjects(performed_compaction, mark_bitmap_clear_end);
  }
}

}  // namespace collector
}  // namespace gc
}  // namespace art
