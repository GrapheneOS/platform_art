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

#include "odrefresh/odrefresh.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/properties.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android/log.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/macros.h"
#include "base/os.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "com_android_apex.h"
#include "com_android_art.h"
#include "dex/art_dex_file_loader.h"
#include "dexoptanalyzer.h"
#include "exec_utils.h"
#include "log/log.h"
#include "odr_artifacts.h"
#include "odr_compilation_log.h"
#include "odr_config.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "palette/palette.h"
#include "palette/palette_types.h"

namespace art {
namespace odrefresh {

namespace apex = com::android::apex;
namespace art_apex = com::android::art;

namespace {

// Name of cache info file in the ART Apex artifact cache.
static constexpr const char* kCacheInfoFile = "cache-info.xml";

static void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  if (isatty(fileno(stderr))) {
    std::cerr << error << std::endl;
  } else {
    LOG(ERROR) << error;
  }
}

static void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN static void ArgumentError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
  UsageError("Try '--help' for more information.");
  exit(EX_USAGE);
}

NO_RETURN static void UsageHelp(const char* argv0) {
  std::string name(android::base::Basename(argv0));
  UsageError("Usage: %s ACTION", name.c_str());
  UsageError("On-device refresh tool for boot class path extensions and system server");
  UsageError("following an update of the ART APEX.");
  UsageError("");
  UsageError("Valid ACTION choices are:");
  UsageError("");
  UsageError(
      "--check          Check compilation artifacts are up-to-date based on metadata (fast).");
  UsageError("--compile        Compile boot class path extensions and system_server jars");
  UsageError("                 when necessary.");
  UsageError("--force-compile  Unconditionally compile the boot class path extensions and");
  UsageError("                 system_server jars.");
  UsageError("--verify         Verify artifacts are up-to-date with dexoptanalyzer (slow).");
  UsageError("--help           Display this help information.");
  exit(EX_USAGE);
}

static std::string Concatenate(std::initializer_list<std::string_view> args) {
  std::stringstream ss;
  for (auto arg : args) {
    ss << arg;
  }
  return ss.str();
}

static std::string GetEnvironmentVariableOrDie(const char* name) {
  const char* value = getenv(name);
  LOG_ALWAYS_FATAL_IF(value == nullptr, "%s is not defined.", name);
  return value;
}

static std::string QuotePath(std::string_view path) {
  return Concatenate({"'", path, "'"});
}

static void EraseFiles(const std::vector<std::unique_ptr<File>>& files) {
  for (auto& file : files) {
    file->Erase(/*unlink=*/true);
  }
}

// Moves `files` to the directory `output_directory_path`.
//
// If any of the files cannot be moved, then all copies of the files are removed from both
// the original location and the output location.
//
// Returns true if all files are moved, false otherwise.
static bool MoveOrEraseFiles(const std::vector<std::unique_ptr<File>>& files,
                             std::string_view output_directory_path) {
  std::vector<std::unique_ptr<File>> output_files;
  for (auto& file : files) {
    const std::string file_basename(android::base::Basename(file->GetPath()));
    const std::string output_file_path = Concatenate({output_directory_path, "/", file_basename});
    const std::string input_file_path = file->GetPath();

    output_files.emplace_back(OS::CreateEmptyFileWriteOnly(output_file_path.c_str()));
    if (output_files.back() == nullptr) {
      PLOG(ERROR) << "Failed to open " << QuotePath(output_file_path);
      output_files.pop_back();
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    static constexpr mode_t kFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (fchmod(output_files.back()->Fd(), kFileMode) != 0) {
      PLOG(ERROR) << "Could not set file mode on " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    const size_t file_bytes = file->GetLength();
    if (!output_files.back()->Copy(file.get(), /*offset=*/0, file_bytes)) {
      PLOG(ERROR) << "Failed to copy " << QuotePath(file->GetPath())
                  << " to " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    if (!file->Erase(/*unlink=*/true)) {
      PLOG(ERROR) << "Failed to erase " << QuotePath(file->GetPath());
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    if (output_files.back()->FlushCloseOrErase() != 0) {
      PLOG(ERROR) << "Failed to flush and close file " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }
  }
  return true;
}

}  // namespace

bool ParseZygoteKind(const char* input, ZygoteKind* zygote_kind) {
  std::string_view z(input);
  if (z == "zygote32") {
    *zygote_kind = ZygoteKind::kZygote32;
    return true;
  } else if (z == "zygote32_64") {
    *zygote_kind = ZygoteKind::kZygote32_64;
    return true;
  } else if (z == "zygote64_32") {
    *zygote_kind = ZygoteKind::kZygote64_32;
    return true;
  } else if (z == "zygote64") {
    *zygote_kind = ZygoteKind::kZygote64;
    return true;
  }
  return false;
}

class OnDeviceRefresh final {
 private:
  // Maximum execution time for odrefresh from start to end.
  static constexpr time_t kMaximumExecutionSeconds = 300;

  // Maximum execution time for any child process spawned.
  static constexpr time_t kMaxChildProcessSeconds = 90;

  // Configuration to use.
  const OdrConfig& config_;

  // Path to cache information file that is used to speed up artifact checking.
  const std::string cache_info_filename_;

  // List of boot extension components that should be compiled.
  std::vector<std::string> boot_extension_compilable_jars_;

  // List of system_server components that should be compiled.
  std::vector<std::string> systemserver_compilable_jars_;

  // List of all boot classpath components. Used as the dependencies for compiling the
  // system_server.
  std::vector<std::string> boot_classpath_jars_;

  const time_t start_time_;

 public:
  explicit OnDeviceRefresh(const OdrConfig& config)
      : config_{config},
        cache_info_filename_{Concatenate({kOdrefreshArtifactDirectory, "/", kCacheInfoFile})},
        start_time_{time(nullptr)} {
    for (const std::string& jar : android::base::Split(config_.GetDex2oatBootClasspath(), ":")) {
      // Boot class path extensions are those not in the ART APEX. Updatable APEXes should not
      // have DEX files in the DEX2OATBOOTCLASSPATH. At the time of writing i18n is a non-updatable
      // APEX and so does appear in the DEX2OATBOOTCLASSPATH.
      if (!LocationIsOnArtModule(jar)) {
        boot_extension_compilable_jars_.emplace_back(jar);
      }
    }

    systemserver_compilable_jars_ = android::base::Split(config_.GetSystemServerClasspath(), ":");
    boot_classpath_jars_ = android::base::Split(config_.GetBootClasspath(), ":");
  }

  time_t GetExecutionTimeUsed() const { return time(nullptr) - start_time_; }

  time_t GetExecutionTimeRemaining() const {
    return kMaximumExecutionSeconds - GetExecutionTimeUsed();
  }

  time_t GetSubprocessTimeout() const {
    return std::max(GetExecutionTimeRemaining(), kMaxChildProcessSeconds);
  }

  // Gets the `ApexInfo` for active APEXes.
  std::optional<std::vector<apex::ApexInfo>> GetApexInfoList() const {
    std::optional<apex::ApexInfoList> info_list =
        apex::readApexInfoList(config_.GetApexInfoListFile().c_str());
    if (!info_list.has_value()) {
      return std::nullopt;
    }

    std::vector<apex::ApexInfo> filtered_info_list;
    std::copy_if(info_list->getApexInfo().begin(),
                 info_list->getApexInfo().end(),
                 std::back_inserter(filtered_info_list),
                 [](const apex::ApexInfo& info) { return info.getIsActive(); });
    return filtered_info_list;
  }

  // Gets the `ApexInfo` associated with the currently active ART APEX.
  static std::optional<apex::ApexInfo> GetArtApexInfo(
      const std::vector<apex::ApexInfo>& info_list) {
    auto it = std::find_if(info_list.begin(), info_list.end(), [](const apex::ApexInfo& info) {
      return info.getModuleName() == "com.android.art";
    });
    return it != info_list.end() ? std::make_optional(*it) : std::nullopt;
  }

  // Reads the ART APEX cache information (if any) found in `kOdrefreshArtifactDirectory`.
  std::optional<art_apex::CacheInfo> ReadCacheInfo() {
    return art_apex::read(cache_info_filename_.c_str());
  }

  // Write ART APEX cache information to `kOdrefreshArtifactDirectory`.
  void WriteCacheInfo() const {
    if (OS::FileExists(cache_info_filename_.c_str())) {
      if (unlink(cache_info_filename_.c_str()) != 0) {
        PLOG(ERROR) << "Failed to unlink() file " << QuotePath(cache_info_filename_);
      }
    }

    const std::string dir_name = android::base::Dirname(cache_info_filename_);
    if (!EnsureDirectoryExists(dir_name)) {
      LOG(ERROR) << "Could not create directory: " << QuotePath(dir_name);
      return;
    }

    std::optional<std::vector<apex::ApexInfo>> apex_info_list = GetApexInfoList();
    if (!apex_info_list.has_value()) {
      LOG(ERROR) << "Could not update " << QuotePath(cache_info_filename_) << " : no APEX info";
      return;
    }

    std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list.value());
    if (!art_apex_info.has_value()) {
      LOG(ERROR) << "Could not update " << QuotePath(cache_info_filename_) << " : no ART APEX info";
      return;
    }

    art_apex::ModuleInfo art_module_info = GenerateModuleInfo(art_apex_info.value());
    std::vector<art_apex::ModuleInfo> module_info_list =
        GenerateModuleInfoList(apex_info_list.value());

    std::optional<std::vector<art_apex::Component>> bcp_components =
        GenerateBootClasspathComponents();
    if (!bcp_components.has_value()) {
      LOG(ERROR) << "No boot classpath components.";
      return;
    }

    std::optional<std::vector<art_apex::Component>> bcp_compilable_components =
        GenerateBootExtensionCompilableComponents();
    if (!bcp_compilable_components.has_value()) {
      LOG(ERROR) << "No boot classpath extension compilable components.";
      return;
    }

    std::optional<std::vector<art_apex::Component>> system_server_components =
        GenerateSystemServerComponents();
    if (!system_server_components.has_value()) {
      LOG(ERROR) << "No system_server extension components.";
      return;
    }

    std::ofstream out(cache_info_filename_.c_str());
    art_apex::CacheInfo info({art_module_info},
                             {art_apex::ModuleInfoList(module_info_list)},
                             {art_apex::Classpath(bcp_components.value())},
                             {art_apex::Classpath(bcp_compilable_components.value())},
                             {art_apex::Classpath(system_server_components.value())});

    art_apex::write(out, info);
  }

  // Returns cache provenance information based on the current APEX version and filesystem
  // information.
  static art_apex::ModuleInfo GenerateModuleInfo(const apex::ApexInfo& apex_info) {
    // The lastUpdateMillis is an addition to ApexInfoList.xsd to support samegrade installs.
    int64_t last_update_millis =
        apex_info.hasLastUpdateMillis() ? apex_info.getLastUpdateMillis() : 0;
    return art_apex::ModuleInfo {apex_info.getModuleName(),
                                 apex_info.getVersionCode(),
                                 apex_info.getVersionName(),
                                 last_update_millis};
  }

  // Returns cache provenance information for all APEXes.
  static std::vector<art_apex::ModuleInfo> GenerateModuleInfoList(
      const std::vector<apex::ApexInfo>& apex_info_list) {
    std::vector<art_apex::ModuleInfo> module_info_list;
    std::transform(apex_info_list.begin(),
                   apex_info_list.end(),
                   std::back_inserter(module_info_list),
                   GenerateModuleInfo);
    return module_info_list;
  }

  bool CheckComponents(const std::vector<art_apex::Component>& expected_components,
                       const std::vector<art_apex::Component>& actual_components,
                       std::string* error_msg) const {
    if (expected_components.size() != actual_components.size()) {
      return false;
    }

    for (size_t i = 0; i < expected_components.size(); ++i) {
      const art_apex::Component& expected = expected_components[i];
      const art_apex::Component& actual = actual_components[i];

      if (expected.getFile() != actual.getFile()) {
        *error_msg = android::base::StringPrintf("Component %zu file differs ('%s' != '%s')",
                                                 i,
                                                 expected.getFile().c_str(),
                                                 actual.getFile().c_str());
        return false;
      }
      if (expected.getSize() != actual.getSize()) {
        *error_msg =
            android::base::StringPrintf("Component %zu size differs (%" PRIu64 " != %" PRIu64 ")",
                                        i,
                                        expected.getSize(),
                                        actual.getSize());
        return false;
      }
      if (expected.getChecksums() != actual.getChecksums()) {
        *error_msg = android::base::StringPrintf("Component %zu checksums differ ('%s' != '%s')",
                                                 i,
                                                 expected.getChecksums().c_str(),
                                                 actual.getChecksums().c_str());
        return false;
      }
    }

    return true;
  }

  std::vector<art_apex::Component> GenerateComponents(const std::vector<std::string>& jars) const {
    std::vector<art_apex::Component> components;

    ArtDexFileLoader loader;
    for (const std::string& path : jars) {
      struct stat sb;
      if (stat(path.c_str(), &sb) == -1) {
        PLOG(ERROR) << "Failed to get component: " << QuotePath(path);
        return {};
      }

      std::vector<uint32_t> checksums;
      std::vector<std::string> dex_locations;
      std::string error_msg;
      if (!loader.GetMultiDexChecksums(path.c_str(), &checksums, &dex_locations, &error_msg)) {
        LOG(ERROR) << "Failed to get components: " << error_msg;
        return {};
      }

      std::ostringstream oss;
      for (size_t i = 0; i < checksums.size(); ++i) {
        if (i != 0) {
          oss << ';';
        }
        oss << android::base::StringPrintf("%08x", checksums[i]);
      }
      const std::string checksum = oss.str();

      components.emplace_back(
          art_apex::Component{path, static_cast<uint64_t>(sb.st_size), checksum});
    }

    return components;
  }

  std::vector<art_apex::Component> GenerateBootClasspathComponents() const {
    return GenerateComponents(boot_classpath_jars_);
  }

  std::vector<art_apex::Component> GenerateBootExtensionCompilableComponents() const {
    return GenerateComponents(boot_extension_compilable_jars_);
  }

  std::vector<art_apex::Component> GenerateSystemServerComponents() const {
    return GenerateComponents(systemserver_compilable_jars_);
  }

  // Checks whether a group of artifacts exists. Returns true if all are present, false otherwise.
  static bool ArtifactsExist(const OdrArtifacts& artifacts,
                             bool check_art_file,
                             /*out*/ std::string* error_msg) {
    std::vector<const char*> paths{artifacts.OatPath().c_str(), artifacts.VdexPath().c_str()};
    if (check_art_file) {
      paths.push_back(artifacts.ImagePath().c_str());
    }
    for (const char* path : paths) {
      if (!OS::FileExists(path)) {
        if (errno == EACCES) {
          PLOG(ERROR) << "Failed to stat() " << path;
        }
        *error_msg = "Missing file: " + QuotePath(path);
        return false;
      }
    }
    return true;
  }

  // Checks whether all boot extension artifacts are present. Returns true if all are present, false
  // otherwise.
  WARN_UNUSED bool BootExtensionArtifactsExist(bool on_system,
                                               const InstructionSet isa,
                                               /*out*/ std::string* error_msg) const {
    const std::string apexdata_image_location = GetBootImageExtensionImagePath(on_system, isa);
    const OdrArtifacts artifacts = OdrArtifacts::ForBootImageExtension(apexdata_image_location);
    return ArtifactsExist(artifacts, /*check_art_file=*/true, error_msg);
  }

  // Checks whether all system_server artifacts are present. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  WARN_UNUSED bool SystemServerArtifactsExist(bool on_system,
                                              /*out*/ std::string* error_msg) const {
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      // Temporarily skip checking APEX jar artifacts on system to prevent compilation on the first
      // boot. Currently, APEX jar artifacts can never be found on system because we don't preopt
      // them.
      // TODO(b/194150908): Preopt APEX jars for system server and put the artifacts on /system.
      if (on_system && StartsWith(jar_path, "/apex")) {
        continue;
      }
      const std::string image_location = GetSystemServerImagePath(on_system, jar_path);
      const OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
      // .art files are optional and are not generated for all jars by the build system.
      const bool check_art_file = !on_system;
      if (!ArtifactsExist(artifacts, check_art_file, error_msg)) {
        return false;
      }
    }
    return true;
  }

  WARN_UNUSED bool CheckBootExtensionArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const InstructionSet isa,
      const apex::ApexInfo& art_apex_info,
      const std::optional<art_apex::CacheInfo>& cache_info,
      /*out*/ bool* cleanup_required) {
    std::string error_msg;

    if (art_apex_info.getIsFactory()) {
      LOG(INFO) << "Factory ART APEX mounted.";

      // ART is not updated, so we can use the artifacts on /system. Check if they exist.
      if (BootExtensionArtifactsExist(/*on_system=*/true, isa, &error_msg)) {
        // We don't need the artifacts on /data since we can use those on /system.
        *cleanup_required = true;
        return true;
      }

      LOG(INFO) << "Incomplete boot extension artifacts on /system. " << error_msg;
      LOG(INFO) << "Checking cache.";
    }

    if (!cache_info.has_value()) {
      // If the cache info file does not exist, it means on-device compilation has not been done
      // before.
      PLOG(INFO) << "No prior cache-info file: " << QuotePath(cache_info_filename_);
      metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
      *cleanup_required = true;
      return false;
    }

    // Check whether the current cache ART module info differs from the current ART module info.
    const art_apex::ModuleInfo* cached_art_info = cache_info->getFirstArtModuleInfo();

    if (cached_art_info == nullptr) {
      LOG(INFO) << "Missing ART APEX info from cache-info.";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    if (cached_art_info->getVersionCode() != art_apex_info.getVersionCode()) {
      LOG(INFO) << "ART APEX version code mismatch (" << cached_art_info->getVersionCode()
                << " != " << art_apex_info.getVersionCode() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    if (cached_art_info->getVersionName() != art_apex_info.getVersionName()) {
      LOG(INFO) << "ART APEX version name mismatch (" << cached_art_info->getVersionName()
                << " != " << art_apex_info.getVersionName() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    // Check lastUpdateMillis for samegrade installs. If `cached_art_info` is missing
    // lastUpdateMillis then it is not current with the schema used by this binary so treat it as a
    // samegrade update. Otherwise check whether the lastUpdateMillis changed.
    if (!cached_art_info->hasLastUpdateMillis() ||
        cached_art_info->getLastUpdateMillis() != art_apex_info.getLastUpdateMillis()) {
      LOG(INFO) << "ART APEX last update time mismatch (" << cached_art_info->getLastUpdateMillis()
                << " != " << art_apex_info.getLastUpdateMillis() << ").";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    // Check boot class components.
    //
    // This checks the size and checksums of odrefresh compilable files on the DEX2OATBOOTCLASSPATH
    // (the Odrefresh constructor determines which files are compilable). If the number of files
    // there changes, or their size or checksums change then compilation will be triggered.
    //
    // The boot class components may change unexpectedly, for example an OTA could update
    // framework.jar.
    const std::vector<art_apex::Component> expected_bcp_compilable_components =
        GenerateBootExtensionCompilableComponents();
    if (expected_bcp_compilable_components.size() != 0 &&
        (!cache_info->hasDex2oatBootClasspath() ||
         !cache_info->getFirstDex2oatBootClasspath()->hasComponent())) {
      LOG(INFO) << "Missing Dex2oatBootClasspath components.";
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      *cleanup_required = true;
      return false;
    }

    const std::vector<art_apex::Component>& bcp_compilable_components =
        cache_info->getFirstDex2oatBootClasspath()->getComponent();
    if (!CheckComponents(
            expected_bcp_compilable_components, bcp_compilable_components, &error_msg)) {
      LOG(INFO) << "Dex2OatClasspath components mismatch: " << error_msg;
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      *cleanup_required = true;
      return false;
    }

    // Cache info looks good, check all compilation artifacts exist.
    if (!BootExtensionArtifactsExist(/*on_system=*/false, isa, &error_msg)) {
      LOG(INFO) << "Incomplete boot extension artifacts. " << error_msg;
      metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
      *cleanup_required = true;
      return false;
    }

    *cleanup_required = false;
    return true;
  }

  WARN_UNUSED bool CheckSystemServerArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const std::vector<apex::ApexInfo>& apex_info_list,
      const std::optional<art_apex::CacheInfo>& cache_info,
      /*out*/ bool* cleanup_required) {
    std::string error_msg;

    if (std::all_of(apex_info_list.begin(),
                    apex_info_list.end(),
                    [](const apex::ApexInfo& apex_info) { return apex_info.getIsFactory(); })) {
      LOG(INFO) << "Factory APEXes mounted.";

      // APEXes are not updated, so we can use the artifacts on /system. Check if they exist.
      if (SystemServerArtifactsExist(/*on_system=*/true, &error_msg)) {
        // We don't need the artifacts on /data since we can use those on /system.
        *cleanup_required = true;
        return true;
      }

      LOG(INFO) << "Incomplete system server artifacts on /system. " << error_msg;
      LOG(INFO) << "Checking cache.";
    }

    if (!cache_info.has_value()) {
      // If the cache info file does not exist, it means on-device compilation has not been done
      // before.
      PLOG(INFO) << "No prior cache-info file: " << QuotePath(cache_info_filename_);
      metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
      *cleanup_required = true;
      return false;
    }

    // Check whether the current cached module info differs from the current module info.
    const art_apex::ModuleInfoList* cached_module_info_list = cache_info->getFirstModuleInfoList();

    if (cached_module_info_list == nullptr) {
      LOG(INFO) << "Missing APEX info list from cache-info.";
      metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
      *cleanup_required = true;
      return false;
    }

    std::unordered_map<std::string, const art_apex::ModuleInfo*> cached_module_info_map;
    for (const art_apex::ModuleInfo& module_info : cached_module_info_list->getModuleInfo()) {
      if (!module_info.hasName()) {
        LOG(INFO) << "Unexpected module info from cache-info. Missing module name.";
        metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
        *cleanup_required = true;
        return false;
      }
      cached_module_info_map[module_info.getName()] = &module_info;
    }

    for (const apex::ApexInfo& current_apex_info : apex_info_list) {
      auto it = cached_module_info_map.find(current_apex_info.getModuleName());
      if (it == cached_module_info_map.end()) {
        LOG(INFO) << "Missing APEX info from cache-info (" << current_apex_info.getModuleName()
                  << ").";
        metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
        *cleanup_required = true;
        return false;
      }

      const art_apex::ModuleInfo* cached_module_info = it->second;

      if (cached_module_info->getVersionCode() != current_apex_info.getVersionCode()) {
        LOG(INFO) << "APEX (" << current_apex_info.getModuleName() << ") version code mismatch ("
                  << cached_module_info->getVersionCode()
                  << " != " << current_apex_info.getVersionCode() << ").";
        metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
        *cleanup_required = true;
        return false;
      }

      if (cached_module_info->getVersionName() != current_apex_info.getVersionName()) {
        LOG(INFO) << "APEX (" << current_apex_info.getModuleName() << ") version name mismatch ("
                  << cached_module_info->getVersionName()
                  << " != " << current_apex_info.getVersionName() << ").";
        metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
        *cleanup_required = true;
        return false;
      }

      if (!cached_module_info->hasLastUpdateMillis() ||
          cached_module_info->getLastUpdateMillis() != current_apex_info.getLastUpdateMillis()) {
        LOG(INFO) << "APEX (" << current_apex_info.getModuleName()
                  << ") last update time mismatch (" << cached_module_info->getLastUpdateMillis()
                  << " != " << current_apex_info.getLastUpdateMillis() << ").";
        metrics.SetTrigger(OdrMetrics::Trigger::kApexVersionMismatch);
        *cleanup_required = true;
        return false;
      }
    }

    // Check system server components.
    //
    // This checks the size and checksums of odrefresh compilable files on the
    // SYSTEMSERVERCLASSPATH (the Odrefresh constructor determines which files are compilable). If
    // the number of files there changes, or their size or checksums change then compilation will be
    // triggered.
    //
    // The system_server components may change unexpectedly, for example an OTA could update
    // services.jar.
    const std::vector<art_apex::Component> expected_system_server_components =
        GenerateSystemServerComponents();
    if (expected_system_server_components.size() != 0 &&
        (!cache_info->hasSystemServerClasspath() ||
         !cache_info->getFirstSystemServerClasspath()->hasComponent())) {
      LOG(INFO) << "Missing SystemServerClasspath components.";
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      *cleanup_required = true;
      return false;
    }

    const std::vector<art_apex::Component>& system_server_components =
        cache_info->getFirstSystemServerClasspath()->getComponent();
    if (!CheckComponents(expected_system_server_components, system_server_components, &error_msg)) {
      LOG(INFO) << "SystemServerClasspath components mismatch: " << error_msg;
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      *cleanup_required = true;
      return false;
    }

    const std::vector<art_apex::Component> expected_bcp_components =
        GenerateBootClasspathComponents();
    if (expected_bcp_components.size() != 0 &&
        (!cache_info->hasBootClasspath() || !cache_info->getFirstBootClasspath()->hasComponent())) {
      LOG(INFO) << "Missing BootClasspath components.";
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      *cleanup_required = true;
      return false;
    }

    const std::vector<art_apex::Component>& bcp_components =
        cache_info->getFirstBootClasspath()->getComponent();
    if (!CheckComponents(expected_bcp_components, bcp_components, &error_msg)) {
      LOG(INFO) << "BootClasspath components mismatch: " << error_msg;
      metrics.SetTrigger(OdrMetrics::Trigger::kDexFilesChanged);
      // Boot classpath components can be dependencies of system_server components, so system_server
      // components need to be recompiled if boot classpath components are changed.
      *cleanup_required = true;
      return false;
    }

    if (!SystemServerArtifactsExist(/*on_system=*/false, &error_msg)) {
      LOG(INFO) << "Incomplete system_server artifacts. " << error_msg;
      // No clean-up is required here: we have boot extension artifacts. The method
      // `SystemServerArtifactsExistOnData()` checks in compilation order so it is possible some of
      // the artifacts are here. We likely ran out of space compiling the system_server artifacts.
      // Any artifacts present are usable.
      metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
      *cleanup_required = false;
      return false;
    }

    *cleanup_required = false;
    return true;
  }

  // Returns the exit code, a list of ISAs that boot extensions should be compiled for, and a
  // boolean indicating whether the system server should be compiled.
  WARN_UNUSED ExitCode
  CheckArtifactsAreUpToDate(OdrMetrics& metrics,
                            /*out*/ std::vector<InstructionSet>* compile_boot_extensions,
                            /*out*/ bool* compile_system_server) {
    metrics.SetStage(OdrMetrics::Stage::kCheck);

    // Clean-up helper used to simplify clean-ups and handling failures there.
    auto cleanup_and_compile_all = [&, this]() {
      *compile_boot_extensions = config_.GetBootExtensionIsas();
      *compile_system_server = true;
      return RemoveArtifactsDirectory() ? ExitCode::kCompilationRequired : ExitCode::kCleanupFailed;
    };

    std::optional<std::vector<apex::ApexInfo>> apex_info_list = GetApexInfoList();
    if (!apex_info_list.has_value()) {
      // This should never happen, further up-to-date checks are not possible if it does.
      LOG(ERROR) << "Could not get APEX info.";
      metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
      return cleanup_and_compile_all();
    }

    std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list.value());
    if (!art_apex_info.has_value()) {
      // This should never happen, further up-to-date checks are not possible if it does.
      LOG(ERROR) << "Could not get ART APEX info.";
      metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
      return cleanup_and_compile_all();
    }

    // Record ART APEX version for metrics reporting.
    metrics.SetArtApexVersion(art_apex_info->getVersionCode());

    // Record ART APEX last update milliseconds (used in compilation log).
    metrics.SetArtApexLastUpdateMillis(art_apex_info->getLastUpdateMillis());

    std::optional<art_apex::CacheInfo> cache_info = ReadCacheInfo();
    if (!cache_info.has_value() && OS::FileExists(cache_info_filename_.c_str())) {
      // This should not happen unless odrefresh is updated to a new version that is not
      // compatible with an old cache-info file. Further up-to-date checks are not possible if it
      // does.
      PLOG(ERROR) << "Failed to parse cache-info file: " << QuotePath(cache_info_filename_);
      metrics.SetTrigger(OdrMetrics::Trigger::kUnknown);
      return cleanup_and_compile_all();
    }

    InstructionSet system_server_isa = config_.GetSystemServerIsa();
    bool cleanup_required;

    for (const InstructionSet isa : config_.GetBootExtensionIsas()) {
      cleanup_required = false;
      if (!CheckBootExtensionArtifactsAreUpToDate(
              metrics, isa, art_apex_info.value(), cache_info, &cleanup_required)) {
        compile_boot_extensions->push_back(isa);
        // system_server artifacts are invalid without valid boot extension artifacts.
        if (isa == system_server_isa) {
          *compile_system_server = true;
          if (!RemoveSystemServerArtifactsFromData()) {
            return ExitCode::kCleanupFailed;
          }
        }
      }
      if (cleanup_required) {
        if (!RemoveBootExtensionArtifactsFromData(isa)) {
          return ExitCode::kCleanupFailed;
        }
      }
    }

    cleanup_required = false;
    if (!*compile_system_server &&
        !CheckSystemServerArtifactsAreUpToDate(
            metrics, apex_info_list.value(), cache_info, &cleanup_required)) {
      *compile_system_server = true;
    }
    if (cleanup_required) {
      if (!RemoveSystemServerArtifactsFromData()) {
        return ExitCode::kCleanupFailed;
      }
    }

    return (!compile_boot_extensions->empty() || *compile_system_server)
               ? ExitCode::kCompilationRequired
               : ExitCode::kOkay;
  }

  static void AddDex2OatCommonOptions(/*inout*/ std::vector<std::string>* args) {
    args->emplace_back("--android-root=out/empty");
    args->emplace_back("--abort-on-hard-verifier-error");
    args->emplace_back("--no-abort-on-soft-verifier-error");
    args->emplace_back("--compilation-reason=boot");
    args->emplace_back("--image-format=lz4");
    args->emplace_back("--force-determinism");
    args->emplace_back("--resolve-startup-const-strings=true");
  }

  static void AddDex2OatConcurrencyArguments(/*inout*/ std::vector<std::string>* args) {
    static constexpr std::pair<const char*, const char*> kPropertyArgPairs[] = {
        std::make_pair("dalvik.vm.boot-dex2oat-cpu-set", "--cpu-set="),
        std::make_pair("dalvik.vm.boot-dex2oat-threads", "-j"),
    };
    for (auto property_arg_pair : kPropertyArgPairs) {
      auto [property, arg] = property_arg_pair;
      std::string value = android::base::GetProperty(property, {});
      if (!value.empty()) {
        args->push_back(arg + value);
      }
    }
  }

  static void AddDex2OatDebugInfo(/*inout*/ std::vector<std::string>* args) {
    args->emplace_back("--generate-mini-debug-info");
    args->emplace_back("--strip");
  }

  static void AddDex2OatInstructionSet(/*inout*/ std::vector<std::string>* args,
                                       InstructionSet isa) {
    const char* isa_str = GetInstructionSetString(isa);
    args->emplace_back(Concatenate({"--instruction-set=", isa_str}));
  }

  static void AddDex2OatProfileAndCompilerFilter(
      /*inout*/ std::vector<std::string>* args,
      /*inout*/ std::vector<std::unique_ptr<File>>* output_files,
      const std::string& profile_path) {
    std::unique_ptr<File> profile_file(OS::OpenFileForReading(profile_path.c_str()));
    if (profile_file && profile_file->IsOpened()) {
      args->emplace_back(android::base::StringPrintf("--profile-file-fd=%d", profile_file->Fd()));
      args->emplace_back("--compiler-filter=speed-profile");
      output_files->push_back(std::move(profile_file));
    } else {
      args->emplace_back("--compiler-filter=speed");
    }
  }

  static bool AddBootClasspathFds(/*inout*/ std::vector<std::string>& args,
                                  /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
                                  const std::vector<std::string>& bcp_jars) {
    auto bcp_fds = std::vector<std::string>();
    for (const std::string& jar : bcp_jars) {
      std::unique_ptr<File> jar_file(OS::OpenFileForReading(jar.c_str()));
      if (!jar_file || !jar_file->IsValid()) {
        LOG(ERROR) << "Failed to open a BCP jar " << jar;
        return false;
      }
      bcp_fds.push_back(std::to_string(jar_file->Fd()));
      output_files.push_back(std::move(jar_file));
    }
    args.emplace_back("--runtime-arg");
    args.emplace_back(Concatenate({"-Xbootclasspathfds:", android::base::Join(bcp_fds, ':')}));
    return true;
  }

  static void AddCompiledBootClasspathFdsIfAny(
      /*inout*/ std::vector<std::string>& args,
      /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
      const std::vector<std::string>& bcp_jars,
      const InstructionSet isa) {
    std::vector<std::string> bcp_image_fds;
    std::vector<std::string> bcp_oat_fds;
    std::vector<std::string> bcp_vdex_fds;
    std::vector<std::unique_ptr<File>> opened_files;
    bool added_any = false;
    for (const std::string& jar : bcp_jars) {
      std::string image_path = GetApexDataBootImage(jar);
      image_path = image_path.empty() ? "" : GetSystemImageFilename(image_path.c_str(), isa);
      std::unique_ptr<File> image_file(OS::OpenFileForReading(image_path.c_str()));
      if (image_file && image_file->IsValid()) {
        bcp_image_fds.push_back(std::to_string(image_file->Fd()));
        opened_files.push_back(std::move(image_file));
        added_any = true;
      } else {
        bcp_image_fds.push_back("-1");
      }

      std::string oat_path = ReplaceFileExtension(image_path, "oat");
      std::unique_ptr<File> oat_file(OS::OpenFileForReading(oat_path.c_str()));
      if (oat_file && oat_file->IsValid()) {
        bcp_oat_fds.push_back(std::to_string(oat_file->Fd()));
        opened_files.push_back(std::move(oat_file));
        added_any = true;
      } else {
        bcp_oat_fds.push_back("-1");
      }

      std::string vdex_path = ReplaceFileExtension(image_path, "vdex");
      std::unique_ptr<File> vdex_file(OS::OpenFileForReading(vdex_path.c_str()));
      if (vdex_file && vdex_file->IsValid()) {
        bcp_vdex_fds.push_back(std::to_string(vdex_file->Fd()));
        opened_files.push_back(std::move(vdex_file));
        added_any = true;
      } else {
        bcp_vdex_fds.push_back("-1");
      }
    }
    // Add same amount of FDs as BCP JARs, or none.
    if (added_any) {
      std::move(opened_files.begin(), opened_files.end(), std::back_inserter(output_files));

      args.emplace_back("--runtime-arg");
      args.emplace_back(Concatenate({"-Xbootclasspathimagefds:",
        android::base::Join(bcp_image_fds, ':')}));
      args.emplace_back("--runtime-arg");
      args.emplace_back(Concatenate({"-Xbootclasspathoatfds:",
        android::base::Join(bcp_oat_fds, ':')}));
      args.emplace_back("--runtime-arg");
      args.emplace_back(Concatenate({"-Xbootclasspathvdexfds:",
        android::base::Join(bcp_vdex_fds, ':')}));
    }
  }

  WARN_UNUSED bool VerifySystemServerArtifactsAreUpToDate(bool on_system) const {
    std::vector<std::string> classloader_context;
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      std::vector<std::string> args;
      args.emplace_back(config_.GetDexOptAnalyzer());
      args.emplace_back("--dex-file=" + jar_path);

      const std::string image_location = GetSystemServerImagePath(on_system, jar_path);

      // odrefresh produces app-image files, but these are not guaranteed for those pre-installed
      // on /system.
      if (!on_system && !OS::FileExists(image_location.c_str(), true)) {
        LOG(INFO) << "Missing image file: " << QuotePath(image_location);
        return false;
      }

      // Generate set of artifacts that are output by compilation.
      OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
      if (!on_system) {
        CHECK_EQ(artifacts.OatPath(),
                 GetApexDataOdexFilename(jar_path, config_.GetSystemServerIsa()));
        CHECK_EQ(artifacts.ImagePath(),
                 GetApexDataDalvikCacheFilename(jar_path, config_.GetSystemServerIsa(), "art"));
        CHECK_EQ(artifacts.OatPath(),
                 GetApexDataDalvikCacheFilename(jar_path, config_.GetSystemServerIsa(), "odex"));
        CHECK_EQ(artifacts.VdexPath(),
                 GetApexDataDalvikCacheFilename(jar_path, config_.GetSystemServerIsa(), "vdex"));
      }

      // Associate inputs and outputs with dexoptanalyzer arguments.
      std::pair<const std::string, const char*> location_args[] = {
          std::make_pair(artifacts.OatPath(), "--oat-fd="),
          std::make_pair(artifacts.VdexPath(), "--vdex-fd="),
          std::make_pair(jar_path, "--zip-fd=")
      };

      // Open file descriptors for dexoptanalyzer file inputs and add to the command-line.
      std::vector<std::unique_ptr<File>> files;
      for (const auto& location_arg : location_args) {
        auto& [location, arg] = location_arg;
        std::unique_ptr<File> file(OS::OpenFileForReading(location.c_str()));
        if (file == nullptr) {
          PLOG(ERROR) << "Failed to open \"" << location << "\"";
          return false;
        }
        args.emplace_back(android::base::StringPrintf("%s%d", arg, file->Fd()));
        files.emplace_back(file.release());
      }

      const std::string basename(android::base::Basename(jar_path));
      const std::string root = GetAndroidRoot();
      const std::string profile_file = Concatenate({root, "/framework/", basename, ".prof"});
      if (OS::FileExists(profile_file.c_str())) {
        args.emplace_back("--compiler-filter=speed-profile");
      } else {
        args.emplace_back("--compiler-filter=speed");
      }

      args.emplace_back(
          Concatenate({"--image=", GetBootImage(), ":", GetBootImageExtensionImage(on_system)}));
      args.emplace_back(
          Concatenate({"--isa=", GetInstructionSetString(config_.GetSystemServerIsa())}));
      args.emplace_back("--runtime-arg");
      args.emplace_back(Concatenate({"-Xbootclasspath:", config_.GetBootClasspath()}));
      args.emplace_back(Concatenate(
          {"--class-loader-context=PCL[", android::base::Join(classloader_context, ':'), "]"}));

      classloader_context.emplace_back(jar_path);

      LOG(INFO) << "Checking " << jar_path << ": " << android::base::Join(args, ' ');
      std::string error_msg;
      bool timed_out = false;
      const time_t timeout = GetSubprocessTimeout();
      const int dexoptanalyzer_result = ExecAndReturnCode(args, timeout, &timed_out, &error_msg);
      if (dexoptanalyzer_result == -1) {
        LOG(ERROR) << "Unexpected exit from dexoptanalyzer: " << error_msg;
        if (timed_out) {
          // TODO(oth): record metric for timeout.
        }
        return false;
      }
      LOG(INFO) << "dexoptanalyzer returned " << dexoptanalyzer_result;

      bool unexpected_result = true;
      switch (static_cast<dexoptanalyzer::ReturnCode>(dexoptanalyzer_result)) {
        case art::dexoptanalyzer::ReturnCode::kNoDexOptNeeded:
          unexpected_result = false;
          break;

        // Recompile needed
        case art::dexoptanalyzer::ReturnCode::kDex2OatFromScratch:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOat:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForBootImageOdex:
        case art::dexoptanalyzer::ReturnCode::kDex2OatForFilterOdex:
          return false;

        // Unexpected issues (note no default-case here to catch missing enum values, but the
        // return code from dexoptanalyzer may also be outside expected values, such as a
        // process crash.
        case art::dexoptanalyzer::ReturnCode::kFlattenClassLoaderContextSuccess:
        case art::dexoptanalyzer::ReturnCode::kErrorInvalidArguments:
        case art::dexoptanalyzer::ReturnCode::kErrorCannotCreateRuntime:
        case art::dexoptanalyzer::ReturnCode::kErrorUnknownDexOptNeeded:
          break;
      }

      if (unexpected_result) {
        LOG(ERROR) << "Unexpected result from dexoptanalyzer: " << dexoptanalyzer_result;
        return false;
      }
    }
    return true;
  }

  WARN_UNUSED bool RemoveSystemServerArtifactsFromData() const {
    if (config_.GetDryRun()) {
      LOG(INFO) << "Removal of system_server artifacts on /data skipped (dry-run).";
      return true;
    }

    bool success = true;
    for (const std::string& jar_path : systemserver_compilable_jars_) {
      const std::string image_location = GetSystemServerImagePath(/*on_system=*/false, jar_path);
      const OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
      LOG(INFO) << "Removing system_server artifacts on /data for " << QuotePath(jar_path);
      success &= RemoveArtifacts(artifacts);
    }
    return success;
  }

  // Verify the validity of system server artifacts on both /system and /data.
  // This method has the side-effect of removing system server artifacts on /data, if there are
  // valid artifacts on /system, or if the artifacts on /data are not valid.
  // Returns true if valid artifacts are found.
  WARN_UNUSED bool VerifySystemServerArtifactsAreUpToDate() const {
    bool system_ok = VerifySystemServerArtifactsAreUpToDate(/*on_system=*/true);
    LOG(INFO) << "system_server artifacts on /system are " << (system_ok ? "ok" : "stale");
    bool data_ok = VerifySystemServerArtifactsAreUpToDate(/*on_system=*/false);
    LOG(INFO) << "system_server artifacts on /data are " << (data_ok ? "ok" : "stale");
    return system_ok || data_ok;
  }

  // Check the validity of boot class path extension artifacts.
  //
  // Returns true if artifacts exist and are valid according to dexoptanalyzer.
  WARN_UNUSED bool VerifyBootExtensionArtifactsAreUpToDate(const InstructionSet isa,
                                                           bool on_system) const {
    const std::string dex_file = boot_extension_compilable_jars_.front();
    const std::string image_location = GetBootImageExtensionImage(on_system);

    std::vector<std::string> args;
    args.emplace_back(config_.GetDexOptAnalyzer());
    args.emplace_back("--validate-bcp");
    args.emplace_back(Concatenate({"--image=", GetBootImage(), ":", image_location}));
    args.emplace_back(Concatenate({"--isa=", GetInstructionSetString(isa)}));
    args.emplace_back("--runtime-arg");
    args.emplace_back(Concatenate({"-Xbootclasspath:", config_.GetDex2oatBootClasspath()}));

    LOG(INFO) << "Checking " << dex_file << ": " << android::base::Join(args, ' ');

    std::string error_msg;
    bool timed_out = false;
    const time_t timeout = GetSubprocessTimeout();
    const int dexoptanalyzer_result = ExecAndReturnCode(args, timeout, &timed_out, &error_msg);
    if (dexoptanalyzer_result == -1) {
      LOG(ERROR) << "Unexpected exit from dexoptanalyzer: " << error_msg;
      if (timed_out) {
        // TODO(oth): record metric for timeout.
      }
      return false;
    }
    auto rc = static_cast<dexoptanalyzer::ReturnCode>(dexoptanalyzer_result);
    if (rc == dexoptanalyzer::ReturnCode::kNoDexOptNeeded) {
      return true;
    }
    return false;
  }

  // Remove boot extension artifacts from /data.
  WARN_UNUSED bool RemoveBootExtensionArtifactsFromData(InstructionSet isa) const {
    if (config_.GetDryRun()) {
      LOG(INFO) << "Removal of bcp extension artifacts on /data skipped (dry-run).";
      return true;
    }

    const std::string apexdata_image_location =
        GetBootImageExtensionImagePath(/*on_system=*/false, isa);
    LOG(INFO) << "Removing boot class path artifacts on /data for "
              << QuotePath(apexdata_image_location);
    return RemoveArtifacts(OdrArtifacts::ForBootImageExtension(apexdata_image_location));
  }

  // Verify whether boot extension artifacts for `isa` are valid on system partition or in apexdata.
  // This method has the side-effect of removing boot classpath extension artifacts on /data,
  // if there are valid artifacts on /system, or if the artifacts on /data are not valid.
  // Returns true if valid boot externsion artifacts are valid.
  WARN_UNUSED bool VerifyBootExtensionArtifactsAreUpToDate(InstructionSet isa) const {
    bool system_ok = VerifyBootExtensionArtifactsAreUpToDate(isa, /*on_system=*/true);
    LOG(INFO) << "Boot extension artifacts on /system are " << (system_ok ? "ok" : "stale");
    bool data_ok = VerifyBootExtensionArtifactsAreUpToDate(isa, /*on_system=*/false);
    LOG(INFO) << "Boot extension artifacts on /data are " << (data_ok ? "ok" : "stale");
    return system_ok || data_ok;
  }

  // Verify all artifacts are up-to-date.
  //
  // This method checks artifacts can be loaded by the runtime.
  //
  // Returns ExitCode::kOkay if artifacts are up-to-date, ExitCode::kCompilationRequired otherwise.
  //
  // NB This is the main function used by the --verify command-line option.
  WARN_UNUSED ExitCode VerifyArtifactsAreUpToDate() {
    ExitCode exit_code = ExitCode::kOkay;
    for (const InstructionSet isa : config_.GetBootExtensionIsas()) {
      if (!VerifyBootExtensionArtifactsAreUpToDate(isa)) {
        // system_server artifacts are invalid without valid boot extension artifacts.
        if (!RemoveSystemServerArtifactsFromData() || !RemoveBootExtensionArtifactsFromData(isa)) {
          return ExitCode::kCleanupFailed;
        }
        exit_code = ExitCode::kCompilationRequired;
      }
    }
    if (!VerifySystemServerArtifactsAreUpToDate()) {
      if (!RemoveSystemServerArtifactsFromData()) {
        return ExitCode::kCleanupFailed;
      }
      exit_code = ExitCode::kCompilationRequired;
    }
    return exit_code;
  }

  WARN_UNUSED bool RemoveArtifactsDirectory() const {
    if (config_.GetDryRun()) {
      LOG(INFO) << "Directory " << QuotePath(kOdrefreshArtifactDirectory)
                << " and contents would be removed (dry-run).";
      return true;
    }
    return RemoveDirectory(kOdrefreshArtifactDirectory);
  }

  WARN_UNUSED bool RemoveArtifacts(const OdrArtifacts& artifacts) const {
    bool success = true;
    for (const auto& location :
         {artifacts.ImagePath(), artifacts.OatPath(), artifacts.VdexPath()}) {
      if (config_.GetDryRun()) {
        LOG(INFO) << "Removing " << QuotePath(location) << " (dry-run).";
        continue;
      }

      if (OS::FileExists(location.c_str()) && unlink(location.c_str()) != 0) {
        PLOG(ERROR) << "Failed to remove: " << QuotePath(location);
        success = false;
      }
    }
    return success;
  }

  static std::string GetBootImage() {
    // Typically "/apex/com.android.art/javalib/boot.art".
    return GetArtRoot() + "/javalib/boot.art";
  }

  std::string GetBootImageExtensionImage(bool on_system) const {
    CHECK(!boot_extension_compilable_jars_.empty());
    const std::string leading_jar = boot_extension_compilable_jars_[0];
    if (on_system) {
      const std::string jar_name = android::base::Basename(leading_jar);
      const std::string image_name = ReplaceFileExtension(jar_name, "art");
      // Typically "/system/framework/boot-framework.art".
      return Concatenate({GetAndroidRoot(), "/framework/boot-", image_name});
    } else {
      // Typically "/data/misc/apexdata/com.android.art/dalvik-cache/boot-framework.art".
      return GetApexDataBootImage(leading_jar);
    }
  }

  std::string GetBootImageExtensionImagePath(bool on_system, const InstructionSet isa) const {
    // Typically "/data/misc/apexdata/com.android.art/dalvik-cache/<isa>/boot-framework.art".
    return GetSystemImageFilename(GetBootImageExtensionImage(on_system).c_str(), isa);
  }

  std::string GetSystemServerImagePath(bool on_system, const std::string& jar_path) const {
    if (on_system) {
      // TODO(b/194150908): Define a path for "preopted" APEX artifacts.
      const std::string jar_name = android::base::Basename(jar_path);
      const std::string image_name = ReplaceFileExtension(jar_name, "art");
      const char* isa_str = GetInstructionSetString(config_.GetSystemServerIsa());
      // Typically "/system/framework/oat/<isa>/services.art".
      return Concatenate({GetAndroidRoot(), "/framework/oat/", isa_str, "/", image_name});
    } else {
      // Typically
      // "/data/misc/apexdata/.../dalvik-cache/<isa>/system@framework@services.jar@classes.art".
      const std::string image = GetApexDataImage(jar_path.c_str());
      return GetSystemImageFilename(image.c_str(), config_.GetSystemServerIsa());
    }
  }

  std::string GetStagingLocation(const std::string& staging_dir, const std::string& path) const {
    return Concatenate({staging_dir, "/", android::base::Basename(path)});
  }

  std::string JoinFilesAsFDs(const std::vector<std::unique_ptr<File>>& files, char delimiter) const {
      std::stringstream output;
      bool is_first = true;
      for (const auto& f : files) {
        if (is_first) {
          is_first = false;
        } else {
          output << delimiter;
        }
        output << std::to_string(f->Fd());
      }
      return output.str();
  }

  WARN_UNUSED bool CompileBootExtensionArtifacts(const InstructionSet isa,
                                                 const std::string& staging_dir,
                                                 OdrMetrics& metrics,
                                                 uint32_t* dex2oat_invocation_count,
                                                 std::string* error_msg) const {
    ScopedOdrCompilationTimer compilation_timer(metrics);
    std::vector<std::string> args;
    args.push_back(config_.GetDex2Oat());

    AddDex2OatCommonOptions(&args);
    AddDex2OatConcurrencyArguments(&args);
    AddDex2OatDebugInfo(&args);
    AddDex2OatInstructionSet(&args, isa);

    std::vector<std::unique_ptr<File>> readonly_files_raii;
    const std::string boot_profile_file(GetAndroidRoot() + "/etc/boot-image.prof");
    AddDex2OatProfileAndCompilerFilter(&args, &readonly_files_raii, boot_profile_file);

    // Compile as a single image for fewer files and slightly less memory overhead.
    args.emplace_back("--single-image");

    // Set boot-image and expectation of compiling boot classpath extensions.
    args.emplace_back("--boot-image=" + GetBootImage());

    const std::string dirty_image_objects_file(GetAndroidRoot() + "/etc/dirty-image-objects");
    if (OS::FileExists(dirty_image_objects_file.c_str())) {
      std::unique_ptr<File> file(OS::OpenFileForReading(dirty_image_objects_file.c_str()));
      args.emplace_back(android::base::StringPrintf("--dirty-image-objects-fd=%d", file->Fd()));
      readonly_files_raii.push_back(std::move(file));
    } else {
      LOG(WARNING) << "Missing dirty objects file : " << QuotePath(dirty_image_objects_file);
    }

    // Add boot extensions to compile.
    for (const std::string& component : boot_extension_compilable_jars_) {
      args.emplace_back("--dex-file=" + component);
      std::unique_ptr<File> file(OS::OpenFileForReading(component.c_str()));
      args.emplace_back(android::base::StringPrintf("--dex-fd=%d", file->Fd()));
      readonly_files_raii.push_back(std::move(file));
    }

    args.emplace_back("--runtime-arg");
    args.emplace_back(Concatenate({"-Xbootclasspath:", config_.GetDex2oatBootClasspath()}));
    auto bcp_jars = android::base::Split(config_.GetDex2oatBootClasspath(), ":");
    if (!AddBootClasspathFds(args, readonly_files_raii, bcp_jars)) {
      return false;
    }

    const std::string image_location = GetBootImageExtensionImagePath(/*on_system=*/false, isa);
    const OdrArtifacts artifacts = OdrArtifacts::ForBootImageExtension(image_location);
    CHECK_EQ(GetApexDataOatFilename(boot_extension_compilable_jars_.front().c_str(), isa),
             artifacts.OatPath());

    args.emplace_back("--oat-location=" + artifacts.OatPath());
    const std::pair<const std::string, const char*> location_kind_pairs[] = {
        std::make_pair(artifacts.ImagePath(), "image"),
        std::make_pair(artifacts.OatPath(), "oat"),
        std::make_pair(artifacts.VdexPath(), "output-vdex")
    };

    std::vector<std::unique_ptr<File>> staging_files;
    for (const auto& location_kind_pair : location_kind_pairs) {
      auto& [location, kind] = location_kind_pair;
      const std::string staging_location = GetStagingLocation(staging_dir, location);
      std::unique_ptr<File> staging_file(OS::CreateEmptyFile(staging_location.c_str()));
      if (staging_file == nullptr) {
        PLOG(ERROR) << "Failed to create " << kind << " file: " << staging_location;
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        EraseFiles(staging_files);
        return false;
      }

      if (fchmod(staging_file->Fd(), S_IRUSR | S_IWUSR) != 0) {
        PLOG(ERROR) << "Could not set file mode on " << QuotePath(staging_location);
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        EraseFiles(staging_files);
        return false;
      }

      args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, staging_file->Fd()));
      staging_files.emplace_back(std::move(staging_file));
    }

    const std::string install_location = android::base::Dirname(image_location);
    if (!EnsureDirectoryExists(install_location)) {
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      return false;
    }

    if (config_.UseCompilationOs()) {
      std::vector<std::string> prefix_args = {
        "/apex/com.android.compos/bin/pvm_exec",
        "--cid=" + config_.GetCompilationOsAddress(),
        "--in-fd=" + JoinFilesAsFDs(readonly_files_raii, ','),
        "--out-fd=" + JoinFilesAsFDs(staging_files, ','),
        "--",
      };
      args.insert(args.begin(), prefix_args.begin(), prefix_args.end());
    }

    const time_t timeout = GetSubprocessTimeout();
    const std::string cmd_line = android::base::Join(args, ' ');
    LOG(INFO) << "Compiling boot extensions (" << isa << "): " << cmd_line
              << " [timeout " << timeout << "s]";
    if (config_.GetDryRun()) {
      LOG(INFO) << "Compilation skipped (dry-run).";
      return true;
    }

    bool timed_out = false;
    int dex2oat_exit_code = ExecAndReturnCode(args, timeout, &timed_out, error_msg);
    if (dex2oat_exit_code != 0) {
      if (timed_out) {
        metrics.SetStatus(OdrMetrics::Status::kTimeLimitExceeded);
      } else {
        metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
      }
      EraseFiles(staging_files);
      return false;
    }

    if (!MoveOrEraseFiles(staging_files, install_location)) {
      metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
      return false;
    }

    *dex2oat_invocation_count = *dex2oat_invocation_count + 1;
    ReportNextBootAnimationProgress(*dex2oat_invocation_count);

    return true;
  }

  WARN_UNUSED bool CompileSystemServerArtifacts(const std::string& staging_dir,
                                                OdrMetrics& metrics,
                                                uint32_t* dex2oat_invocation_count,
                                                std::string* error_msg) const {
    ScopedOdrCompilationTimer compilation_timer(metrics);
    std::vector<std::string> classloader_context;

    const std::string dex2oat = config_.GetDex2Oat();
    const InstructionSet isa = config_.GetSystemServerIsa();
    for (const std::string& jar : systemserver_compilable_jars_) {
      std::vector<std::unique_ptr<File>> readonly_files_raii;
      std::vector<std::string> args;
      args.emplace_back(dex2oat);
      args.emplace_back("--dex-file=" + jar);

      std::unique_ptr<File> dex_file(OS::OpenFileForReading(jar.c_str()));
      args.emplace_back(android::base::StringPrintf("--dex-fd=%d", dex_file->Fd()));
      readonly_files_raii.push_back(std::move(dex_file));

      AddDex2OatCommonOptions(&args);
      AddDex2OatConcurrencyArguments(&args);
      AddDex2OatDebugInfo(&args);
      AddDex2OatInstructionSet(&args, isa);
      const std::string jar_name(android::base::Basename(jar));
      const std::string profile = Concatenate({GetAndroidRoot(), "/framework/", jar_name, ".prof"});
      std::string compiler_filter =
          android::base::GetProperty("dalvik.vm.systemservercompilerfilter", "speed");
      if (compiler_filter == "speed-profile") {
        AddDex2OatProfileAndCompilerFilter(&args, &readonly_files_raii, profile);
      } else {
        args.emplace_back("--compiler-filter=" + compiler_filter);
      }

      const std::string image_location = GetSystemServerImagePath(/*on_system=*/false, jar);
      const std::string install_location = android::base::Dirname(image_location);
      if (classloader_context.empty()) {
        // All images are in the same directory, we only need to check on the first iteration.
        if (!EnsureDirectoryExists(install_location)) {
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          return false;
        }
      }

      OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
      CHECK_EQ(artifacts.OatPath(), GetApexDataOdexFilename(jar.c_str(), isa));

      const std::pair<const std::string, const char*> location_kind_pairs[] = {
          std::make_pair(artifacts.ImagePath(), "app-image"),
          std::make_pair(artifacts.OatPath(), "oat"),
          std::make_pair(artifacts.VdexPath(), "output-vdex")
      };

      std::vector<std::unique_ptr<File>> staging_files;
      for (const auto& location_kind_pair : location_kind_pairs) {
        auto& [location, kind] = location_kind_pair;
        const std::string staging_location = GetStagingLocation(staging_dir, location);
        std::unique_ptr<File> staging_file(OS::CreateEmptyFile(staging_location.c_str()));
        if (staging_file == nullptr) {
          PLOG(ERROR) << "Failed to create " << kind << " file: " << staging_location;
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          EraseFiles(staging_files);
          return false;
        }
        args.emplace_back(android::base::StringPrintf("--%s-fd=%d", kind, staging_file->Fd()));
        staging_files.emplace_back(std::move(staging_file));
      }
      args.emplace_back("--oat-location=" + artifacts.OatPath());

      if (!config_.GetUpdatableBcpPackagesFile().empty()) {
        const std::string& bcp_packages = config_.GetUpdatableBcpPackagesFile();
        if (!OS::FileExists(bcp_packages.c_str())) {
          *error_msg = "Cannot compile system_server JARs: missing " + QuotePath(bcp_packages);
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          EraseFiles(staging_files);
          return false;
        }
        std::unique_ptr<File> file(OS::OpenFileForReading(bcp_packages.c_str()));
        args.emplace_back(
            android::base::StringPrintf("--updatable-bcp-packages-fd=%d", file->Fd()));
        readonly_files_raii.push_back(std::move(file));
      }

      args.emplace_back("--runtime-arg");
      args.emplace_back(Concatenate({"-Xbootclasspath:", config_.GetBootClasspath()}));
      auto bcp_jars = android::base::Split(config_.GetBootClasspath(), ":");
      if (!AddBootClasspathFds(args, readonly_files_raii, bcp_jars)) {
        return false;
      }
      AddCompiledBootClasspathFdsIfAny(args, readonly_files_raii, bcp_jars, isa);

      const std::string context_path = android::base::Join(classloader_context, ':');
      args.emplace_back(Concatenate({"--class-loader-context=PCL[", context_path, "]"}));
      if (!classloader_context.empty()) {
        std::vector<int> fds;
        for (const std::string& path : classloader_context) {
          std::unique_ptr<File> file(OS::OpenFileForReading(path.c_str()));
          if (!file->IsValid()) {
            PLOG(ERROR) << "Failed to open classloader context " << path;
            metrics.SetStatus(OdrMetrics::Status::kIoError);
            return false;
          }
          fds.emplace_back(file->Fd());
          readonly_files_raii.emplace_back(std::move(file));
        }
        const std::string context_fds = android::base::Join(fds, ':');
        args.emplace_back(Concatenate({"--class-loader-context-fds=", context_fds}));
      }
      const std::string extension_image = GetBootImageExtensionImage(/*on_system=*/false);
      args.emplace_back(Concatenate({"--boot-image=", GetBootImage(), ":", extension_image}));

      if (config_.UseCompilationOs()) {
        std::vector<std::string> prefix_args = {
          "/apex/com.android.compos/bin/pvm_exec",
          "--cid=" + config_.GetCompilationOsAddress(),
          "--in-fd=" + JoinFilesAsFDs(readonly_files_raii, ','),
          "--out-fd=" + JoinFilesAsFDs(staging_files, ','),
          "--",
        };
        args.insert(args.begin(), prefix_args.begin(), prefix_args.end());
      }

      const time_t timeout = GetSubprocessTimeout();
      const std::string cmd_line = android::base::Join(args, ' ');
      LOG(INFO) << "Compiling " << jar << ": " << cmd_line << " [timeout " << timeout << "s]";
      if (config_.GetDryRun()) {
        LOG(INFO) << "Compilation skipped (dry-run).";
        return true;
      }

      bool timed_out = false;
      int dex2oat_exit_code = ExecAndReturnCode(args, timeout, &timed_out, error_msg);
      if (dex2oat_exit_code != 0) {
        if (timed_out) {
          metrics.SetStatus(OdrMetrics::Status::kTimeLimitExceeded);
        } else {
          metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
        }
        EraseFiles(staging_files);
        return false;
      }

      if (!MoveOrEraseFiles(staging_files, install_location)) {
        metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
        return false;
      }

      *dex2oat_invocation_count = *dex2oat_invocation_count + 1;
      ReportNextBootAnimationProgress(*dex2oat_invocation_count);
      classloader_context.emplace_back(jar);
    }

    return true;
  }

  void ReportNextBootAnimationProgress(uint32_t current_compilation) const {
    uint32_t number_of_compilations =
        config_.GetBootExtensionIsas().size() + systemserver_compilable_jars_.size();
    // We arbitrarily show progress until 90%, expecting that our compilations
    // take a large chunk of boot time.
    uint32_t value = (90 * current_compilation) / number_of_compilations;
    android::base::SetProperty("service.bootanim.progress", std::to_string(value));
  }

  WARN_UNUSED bool CheckCompilationSpace() const {
    // Check the available storage space against an arbitrary threshold because dex2oat does not
    // report when it runs out of storage space and we do not want to completely fill
    // the users data partition.
    //
    // We do not have a good way of pre-computing the required space for a compilation step, but
    // typically observe 16MB as the largest size of an AOT artifact. Since there are three
    // AOT artifacts per compilation step - an image file, executable file, and a verification
    // data file - the threshold is three times 16MB.
    static constexpr uint64_t kMinimumSpaceForCompilation = 3 * 16 * 1024 * 1024;

    uint64_t bytes_available;
    const std::string& art_apex_data_path = GetArtApexData();
    if (!GetFreeSpace(art_apex_data_path, &bytes_available)) {
      return false;
    }

    if (bytes_available < kMinimumSpaceForCompilation) {
      LOG(WARNING) << "Low space for " << QuotePath(art_apex_data_path) << " (" << bytes_available
                   << " bytes)";
      return false;
    }

    return true;
  }

  WARN_UNUSED ExitCode Compile(OdrMetrics& metrics,
                               const std::vector<InstructionSet>& compile_boot_extensions,
                               bool compile_system_server) const {
    const char* staging_dir = nullptr;
    metrics.SetStage(OdrMetrics::Stage::kPreparation);

    // Create staging area and assign label for generating compilation artifacts.
    if (PaletteCreateOdrefreshStagingDirectory(&staging_dir) != PALETTE_STATUS_OK) {
      metrics.SetStatus(OdrMetrics::Status::kStagingFailed);
      return ExitCode::kCleanupFailed;
    }

    // Emit cache info before compiling. This can be used to throttle compilation attempts later.
    WriteCacheInfo();

    std::string error_msg;

    uint32_t dex2oat_invocation_count = 0;
    ReportNextBootAnimationProgress(dex2oat_invocation_count);

    const auto& bcp_instruction_sets = config_.GetBootExtensionIsas();
    DCHECK(!bcp_instruction_sets.empty() && bcp_instruction_sets.size() <= 2);
    for (const InstructionSet isa : compile_boot_extensions) {
      auto stage = (isa == bcp_instruction_sets.front()) ?
                       OdrMetrics::Stage::kPrimaryBootClasspath :
                       OdrMetrics::Stage::kSecondaryBootClasspath;
      metrics.SetStage(stage);
      if (!CheckCompilationSpace()) {
        metrics.SetStatus(OdrMetrics::Status::kNoSpace);
        // Return kOkay so odsign will keep and sign whatever we have been able to compile.
        return ExitCode::kOkay;
      }

      if (!CompileBootExtensionArtifacts(
              isa, staging_dir, metrics, &dex2oat_invocation_count, &error_msg)) {
        LOG(ERROR) << "Compilation of BCP failed: " << error_msg;
        if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
          return ExitCode::kCleanupFailed;
        }
        return ExitCode::kCompilationFailed;
      }
    }

    if (compile_system_server) {
      metrics.SetStage(OdrMetrics::Stage::kSystemServerClasspath);

      if (!CheckCompilationSpace()) {
        metrics.SetStatus(OdrMetrics::Status::kNoSpace);
        // Return kOkay so odsign will keep and sign whatever we have been able to compile.
        return ExitCode::kOkay;
      }

      if (!CompileSystemServerArtifacts(
              staging_dir, metrics, &dex2oat_invocation_count, &error_msg)) {
        LOG(ERROR) << "Compilation of system_server failed: " << error_msg;
        if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
          return ExitCode::kCleanupFailed;
        }
        return ExitCode::kCompilationFailed;
      }
    }

    metrics.SetStage(OdrMetrics::Stage::kComplete);
    return ExitCode::kCompilationSuccess;
  }

  static bool ArgumentMatches(std::string_view argument,
                              std::string_view prefix,
                              std::string* value) {
    if (StartsWith(argument, prefix)) {
      *value = std::string(argument.substr(prefix.size()));
      return true;
    }
    return false;
  }

  static bool ArgumentEquals(std::string_view argument, std::string_view expected) {
    return argument == expected;
  }

  static bool InitializeCommonConfig(std::string_view argument, OdrConfig* config) {
    static constexpr std::string_view kDryRunArgument{"--dry-run"};
    if (ArgumentEquals(argument, kDryRunArgument)) {
      config->SetDryRun();
      return true;
    }
    return false;
  }

  static int InitializeHostConfig(int argc, const char** argv, OdrConfig* config) {
    __android_log_set_logger(__android_log_stderr_logger);

    std::string current_binary;
    if (argv[0][0] == '/') {
      current_binary = argv[0];
    } else {
      std::vector<char> buf(PATH_MAX);
      if (getcwd(buf.data(), buf.size()) == nullptr) {
        PLOG(FATAL) << "Failed getwd()";
      }
      current_binary = Concatenate({buf.data(), "/", argv[0]});
    }
    config->SetArtBinDir(android::base::Dirname(current_binary));

    int n = 1;
    for (; n < argc - 1; ++n) {
      const char* arg = argv[n];
      std::string value;
      if (ArgumentMatches(arg, "--android-root=", &value)) {
        setenv("ANDROID_ROOT", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--android-art-root=", &value)) {
        setenv("ANDROID_ART_ROOT", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--apex-info-list=", &value)) {
        config->SetApexInfoListFile(value);
      } else if (ArgumentMatches(arg, "--art-apex-data=", &value)) {
        setenv("ART_APEX_DATA", value.c_str(), 1);
      } else if (ArgumentMatches(arg, "--dex2oat-bootclasspath=", &value)) {
        config->SetDex2oatBootclasspath(value);
      } else if (ArgumentMatches(arg, "--isa=", &value)) {
        config->SetIsa(GetInstructionSetFromString(value.c_str()));
      } else if (ArgumentMatches(arg, "--system-server-classpath=", &value)) {
        config->SetSystemServerClasspath(arg);
      } else if (ArgumentMatches(arg, "--updatable-bcp-packages-file=", &value)) {
        config->SetUpdatableBcpPackagesFile(value);
      } else if (ArgumentMatches(arg, "--zygote-arch=", &value)) {
        ZygoteKind zygote_kind;
        if (!ParseZygoteKind(value.c_str(), &zygote_kind)) {
          ArgumentError("Unrecognized zygote kind: '%s'", value.c_str());
        }
        config->SetZygoteKind(zygote_kind);
      } else if (!InitializeCommonConfig(arg, config)) {
        UsageError("Unrecognized argument: '%s'", arg);
      }
    }
    return n;
  }

  static int InitializeTargetConfig(int argc, const char** argv, OdrConfig* config) {
    config->SetApexInfoListFile("/apex/apex-info-list.xml");
    config->SetArtBinDir(GetArtBinDir());
    config->SetBootClasspath(GetEnvironmentVariableOrDie("BOOTCLASSPATH"));
    config->SetDex2oatBootclasspath(GetEnvironmentVariableOrDie("DEX2OATBOOTCLASSPATH"));
    config->SetSystemServerClasspath(GetEnvironmentVariableOrDie("SYSTEMSERVERCLASSPATH"));
    config->SetIsa(kRuntimeISA);

    const std::string zygote = android::base::GetProperty("ro.zygote", {});
    ZygoteKind zygote_kind;
    if (!ParseZygoteKind(zygote.c_str(), &zygote_kind)) {
      LOG(FATAL) << "Unknown zygote: " << QuotePath(zygote);
    }
    config->SetZygoteKind(zygote_kind);

    const std::string updatable_packages =
        android::base::GetProperty("dalvik.vm.dex2oat-updatable-bcp-packages-file", {});
    config->SetUpdatableBcpPackagesFile(updatable_packages);

    int n = 1;
    for (; n < argc - 1; ++n) {
      const char* arg = argv[n];
      std::string value;
      if (ArgumentMatches(arg, "--use-compilation-os=", &value)) {
        config->SetCompilationOsAddress(value);
      } else if (!InitializeCommonConfig(arg, config)) {
        UsageError("Unrecognized argument: '%s'", arg);
      }
    }
    return n;
  }

  static int InitializeConfig(int argc, const char** argv, OdrConfig* config) {
    if (kIsTargetBuild) {
      return InitializeTargetConfig(argc, argv, config);
    } else {
      return InitializeHostConfig(argc, argv, config);
    }
  }

  static int main(int argc, const char** argv) {
    OdrConfig config(argv[0]);
    int n = InitializeConfig(argc, argv, &config);
    argv += n;
    argc -= n;
    if (argc != 1) {
      UsageError("Expected 1 argument, but have %d.", argc);
    }

    OdrMetrics metrics(kOdrefreshArtifactDirectory);
    OnDeviceRefresh odr(config);
    for (int i = 0; i < argc; ++i) {
      std::string_view action(argv[i]);
      std::vector<InstructionSet> compile_boot_extensions;
      bool compile_system_server;
      if (action == "--check") {
        // Fast determination of whether artifacts are up to date.
        return odr.CheckArtifactsAreUpToDate(
            metrics, &compile_boot_extensions, &compile_system_server);
      } else if (action == "--compile") {
        const ExitCode exit_code = odr.CheckArtifactsAreUpToDate(
            metrics, &compile_boot_extensions, &compile_system_server);
        if (exit_code != ExitCode::kCompilationRequired) {
          return exit_code;
        }
        OdrCompilationLog compilation_log;
        if (!compilation_log.ShouldAttemptCompile(metrics.GetArtApexVersion(),
                                                  metrics.GetArtApexLastUpdateMillis(),
                                                  metrics.GetTrigger())) {
          return ExitCode::kOkay;
        }
        ExitCode compile_result =
            odr.Compile(metrics, compile_boot_extensions, compile_system_server);
        compilation_log.Log(metrics.GetArtApexVersion(),
                            metrics.GetArtApexLastUpdateMillis(),
                            metrics.GetTrigger(),
                            compile_result);
        return compile_result;
      } else if (action == "--force-compile") {
        // Clean-up existing files.
        if (!odr.RemoveArtifactsDirectory()) {
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          return ExitCode::kCleanupFailed;
        }
        return odr.Compile(metrics,
                           /*compile_boot_extensions=*/config.GetBootExtensionIsas(),
                           /*compile_system_server=*/true);
      } else if (action == "--verify") {
        // Slow determination of whether artifacts are up to date. These are too slow for checking
        // during boot (b/181689036).
        return odr.VerifyArtifactsAreUpToDate();
      } else if (action == "--help") {
        UsageHelp(argv[0]);
      } else {
        UsageError("Unknown argument: ", argv[i]);
      }
    }
    return ExitCode::kOkay;
  }

  DISALLOW_COPY_AND_ASSIGN(OnDeviceRefresh);
};

}  // namespace odrefresh
}  // namespace art

int main(int argc, const char** argv) {
  // odrefresh is launched by `init` which sets the umask of forked processed to
  // 077 (S_IRWXG | S_IRWXO). This blocks the ability to make files and directories readable
  // by others and prevents system_server from loading generated artifacts.
  umask(S_IWGRP | S_IWOTH);
  return art::odrefresh::OnDeviceRefresh::main(argc, argv);
}
