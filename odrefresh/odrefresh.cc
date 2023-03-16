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

#include "odrefresh.h"

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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/parsebool.h"
#include "android-base/parseint.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "android-base/scopeguard.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android-modules-utils/sdk_level.h"
#include "android/log.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/macros.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "com_android_apex.h"
#include "com_android_art.h"
#include "dex/art_dex_file_loader.h"
#include "dexoptanalyzer.h"
#include "exec_utils.h"
#include "fmt/format.h"
#include "gc/collector/mark_compact.h"
#include "log/log.h"
#include "odr_artifacts.h"
#include "odr_common.h"
#include "odr_compilation_log.h"
#include "odr_config.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"
#include "palette/palette.h"
#include "palette/palette_types.h"
#include "read_barrier_config.h"

namespace art {
namespace odrefresh {

namespace {

namespace apex = com::android::apex;
namespace art_apex = com::android::art;

using ::android::base::ParseBool;
using ::android::base::ParseBoolResult;
using ::android::base::Result;
using ::android::modules::sdklevel::IsAtLeastU;

using ::fmt::literals::operator""_format;  // NOLINT

// Name of cache info file in the ART Apex artifact cache.
constexpr const char* kCacheInfoFile = "cache-info.xml";

// Maximum execution time for odrefresh from start to end.
constexpr time_t kMaximumExecutionSeconds = 480;

// Maximum execution time for any child process spawned.
constexpr time_t kMaxChildProcessSeconds = 120;

constexpr mode_t kFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

constexpr const char* kFirstBootImageBasename = "boot.art";
constexpr const char* kMinimalBootImageBasename = "boot_minimal.art";

void EraseFiles(const std::vector<std::unique_ptr<File>>& files) {
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
bool MoveOrEraseFiles(const std::vector<std::unique_ptr<File>>& files,
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

    if (fchmod(output_files.back()->Fd(), kFileMode) != 0) {
      PLOG(ERROR) << "Could not set file mode on " << QuotePath(output_file_path);
      EraseFiles(output_files);
      EraseFiles(files);
      return false;
    }

    const size_t file_bytes = file->GetLength();
    if (!output_files.back()->Copy(file.get(), /*offset=*/0, file_bytes)) {
      PLOG(ERROR) << "Failed to copy " << QuotePath(file->GetPath()) << " to "
                  << QuotePath(output_file_path);
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

// Gets the `ApexInfo` associated with the currently active ART APEX.
std::optional<apex::ApexInfo> GetArtApexInfo(const std::vector<apex::ApexInfo>& info_list) {
  auto it = std::find_if(info_list.begin(), info_list.end(), [](const apex::ApexInfo& info) {
    return info.getModuleName() == "com.android.art";
  });
  return it != info_list.end() ? std::make_optional(*it) : std::nullopt;
}

// Returns cache provenance information based on the current APEX version and filesystem
// information.
art_apex::ModuleInfo GenerateModuleInfo(const apex::ApexInfo& apex_info) {
  // The lastUpdateMillis is an addition to ApexInfoList.xsd to support samegrade installs.
  int64_t last_update_millis =
      apex_info.hasLastUpdateMillis() ? apex_info.getLastUpdateMillis() : 0;
  return art_apex::ModuleInfo{apex_info.getModuleName(),
                              apex_info.getVersionCode(),
                              apex_info.getVersionName(),
                              last_update_millis};
}

// Returns cache provenance information for all APEXes.
std::vector<art_apex::ModuleInfo> GenerateModuleInfoList(
    const std::vector<apex::ApexInfo>& apex_info_list) {
  std::vector<art_apex::ModuleInfo> module_info_list;
  std::transform(apex_info_list.begin(),
                 apex_info_list.end(),
                 std::back_inserter(module_info_list),
                 GenerateModuleInfo);
  return module_info_list;
}

// Returns a rewritten path based on environment variables for interesting paths.
std::string RewriteParentDirectoryIfNeeded(const std::string& path) {
  if (StartsWith(path, "/system/")) {
    return Concatenate({GetAndroidRoot(), path.substr(7)});
  } else if (StartsWith(path, "/system_ext/")) {
    return Concatenate({GetSystemExtRoot(), path.substr(11)});
  } else {
    return path;
  }
}

template <typename T>
Result<void> CheckComponents(
    const std::vector<T>& expected_components,
    const std::vector<T>& actual_components,
    const std::function<Result<void>(const T& expected, const T& actual)>& custom_checker =
        [](const T&, const T&) -> Result<void> { return {}; }) {
  if (expected_components.size() != actual_components.size()) {
    return Errorf(
        "Component count differs ({} != {})", expected_components.size(), actual_components.size());
  }

  for (size_t i = 0; i < expected_components.size(); ++i) {
    const T& expected = expected_components[i];
    const T& actual = actual_components[i];

    if (expected.getFile() != actual.getFile()) {
      return Errorf(
          "Component {} file differs ('{}' != '{}')", i, expected.getFile(), actual.getFile());
    }

    if (expected.getSize() != actual.getSize()) {
      return Errorf(
          "Component {} size differs ({} != {})", i, expected.getSize(), actual.getSize());
    }

    if (expected.getChecksums() != actual.getChecksums()) {
      return Errorf("Component {} checksums differ ('{}' != '{}')",
                    i,
                    expected.getChecksums(),
                    actual.getChecksums());
    }

    Result<void> result = custom_checker(expected, actual);
    if (!result.ok()) {
      return Errorf("Component {} {}", i, result.error().message());
    }
  }

  return {};
}

Result<void> CheckSystemServerComponents(
    const std::vector<art_apex::SystemServerComponent>& expected_components,
    const std::vector<art_apex::SystemServerComponent>& actual_components) {
  return CheckComponents<art_apex::SystemServerComponent>(
      expected_components,
      actual_components,
      [](const art_apex::SystemServerComponent& expected,
         const art_apex::SystemServerComponent& actual) -> Result<void> {
        if (expected.getIsInClasspath() != actual.getIsInClasspath()) {
          return Errorf("isInClasspath differs ({} != {})",
                        expected.getIsInClasspath(),
                        actual.getIsInClasspath());
        }

        return {};
      });
}

template <typename T>
std::vector<T> GenerateComponents(
    const std::vector<std::string>& jars,
    const std::function<T(const std::string& path, uint64_t size, const std::string& checksum)>&
        custom_generator) {
  std::vector<T> components;

  for (const std::string& path : jars) {
    std::string actual_path = RewriteParentDirectoryIfNeeded(path);
    struct stat sb;
    if (stat(actual_path.c_str(), &sb) == -1) {
      PLOG(ERROR) << "Failed to stat component: " << QuotePath(actual_path);
      return {};
    }

    std::vector<uint32_t> checksums;
    std::vector<std::string> dex_locations;
    std::string error_msg;
    if (!ArtDexFileLoader::GetMultiDexChecksums(
            actual_path.c_str(), &checksums, &dex_locations, &error_msg)) {
      LOG(ERROR) << "Failed to get multi-dex checksums: " << error_msg;
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

    Result<T> component = custom_generator(path, static_cast<uint64_t>(sb.st_size), checksum);
    if (!component.ok()) {
      LOG(ERROR) << "Failed to generate component: " << component.error();
      return {};
    }

    components.push_back(*std::move(component));
  }

  return components;
}

std::vector<art_apex::Component> GenerateComponents(const std::vector<std::string>& jars) {
  return GenerateComponents<art_apex::Component>(
      jars, [](const std::string& path, uint64_t size, const std::string& checksum) {
        return art_apex::Component{path, size, checksum};
      });
}

// Checks whether a group of artifacts exists. Returns true if all are present, false otherwise.
// If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
bool ArtifactsExist(const OdrArtifacts& artifacts,
                    bool check_art_file,
                    /*out*/ std::string* error_msg,
                    /*out*/ std::vector<std::string>* checked_artifacts = nullptr) {
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
  // This should be done after checking all artifacts because either all of them are valid or none
  // of them is valid.
  if (checked_artifacts != nullptr) {
    for (const char* path : paths) {
      checked_artifacts->emplace_back(path);
    }
  }
  return true;
}

void AddDex2OatCommonOptions(/*inout*/ std::vector<std::string>& args) {
  args.emplace_back("--android-root=out/empty");
  args.emplace_back("--abort-on-hard-verifier-error");
  args.emplace_back("--no-abort-on-soft-verifier-error");
  args.emplace_back("--compilation-reason=boot");
  args.emplace_back("--image-format=lz4");
  args.emplace_back("--force-determinism");
  args.emplace_back("--resolve-startup-const-strings=true");

  // Avoid storing dex2oat cmdline in oat header. We want to be sure that the compiled artifacts
  // are identical regardless of where the compilation happened. But some of the cmdline flags tends
  // to be unstable, e.g. those contains FD numbers. To avoid the problem, the whole cmdline is not
  // added to the oat header.
  args.emplace_back("--avoid-storing-invocation");
}

bool IsCpuSetSpecValid(const std::string& cpu_set) {
  for (auto& str : android::base::Split(cpu_set, ",")) {
    int id;
    if (!android::base::ParseInt(str, &id, 0)) {
      return false;
    }
  }
  return true;
}

bool AddDex2OatConcurrencyArguments(/*inout*/ std::vector<std::string>& args,
                                    bool is_compilation_os) {
  std::string threads;
  if (is_compilation_os) {
    threads = android::base::GetProperty("dalvik.vm.background-dex2oat-threads", "");
    if (threads.empty()) {
      threads = android::base::GetProperty("dalvik.vm.dex2oat-threads", "");
    }
  } else {
    threads = android::base::GetProperty("dalvik.vm.boot-dex2oat-threads", "");
  }
  if (!threads.empty()) {
    args.push_back("-j" + threads);
  }

  std::string cpu_set;
  if (is_compilation_os) {
    cpu_set = android::base::GetProperty("dalvik.vm.background-dex2oat-cpu-set", "");
    if (cpu_set.empty()) {
      cpu_set = android::base::GetProperty("dalvik.vm.dex2oat-cpu-set", "");
    }
  } else {
    cpu_set = android::base::GetProperty("dalvik.vm.boot-dex2oat-cpu-set", "");
  }
  if (!cpu_set.empty()) {
    if (!IsCpuSetSpecValid(cpu_set)) {
      LOG(ERROR) << "Invalid CPU set spec: " << cpu_set;
      return false;
    }
    args.push_back("--cpu-set=" + cpu_set);
  }

  return true;
}

void AddDex2OatDebugInfo(/*inout*/ std::vector<std::string>& args) {
  args.emplace_back("--generate-mini-debug-info");
  args.emplace_back("--strip");
}

void AddDex2OatInstructionSet(/*inout*/ std::vector<std::string>& args, InstructionSet isa) {
  const char* isa_str = GetInstructionSetString(isa);
  args.emplace_back(Concatenate({"--instruction-set=", isa_str}));
}

// Returns true if any profile has been added.
bool AddDex2OatProfile(
    /*inout*/ std::vector<std::string>& args,
    /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
    const std::vector<std::string>& profile_paths) {
  bool has_any_profile = false;
  for (auto& path : profile_paths) {
    std::unique_ptr<File> profile_file(OS::OpenFileForReading(path.c_str()));
    if (profile_file && profile_file->IsOpened()) {
      args.emplace_back(android::base::StringPrintf("--profile-file-fd=%d", profile_file->Fd()));
      output_files.emplace_back(std::move(profile_file));
      has_any_profile = true;
    }
  }
  return has_any_profile;
}

bool AddBootClasspathFds(/*inout*/ std::vector<std::string>& args,
                         /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
                         const std::vector<std::string>& bcp_jars) {
  std::vector<std::string> bcp_fds;
  for (const std::string& jar : bcp_jars) {
    // Special treatment for Compilation OS. JARs in staged APEX may not be visible to Android, and
    // may only be visible in the VM where the staged APEX is mounted. On the contrary, JARs in
    // /system is not available by path in the VM, and can only made available via (remote) FDs.
    if (StartsWith(jar, "/apex/")) {
      bcp_fds.emplace_back("-1");
    } else {
      std::string actual_path = RewriteParentDirectoryIfNeeded(jar);
      std::unique_ptr<File> jar_file(OS::OpenFileForReading(actual_path.c_str()));
      if (!jar_file || !jar_file->IsValid()) {
        LOG(ERROR) << "Failed to open a BCP jar " << actual_path;
        return false;
      }
      bcp_fds.push_back(std::to_string(jar_file->Fd()));
      output_files.push_back(std::move(jar_file));
    }
  }
  args.emplace_back("--runtime-arg");
  args.emplace_back(Concatenate({"-Xbootclasspathfds:", android::base::Join(bcp_fds, ':')}));
  return true;
}

std::string GetBootImageComponentBasename(const std::string& jar_path, bool is_first_jar) {
  if (is_first_jar) {
    return kFirstBootImageBasename;
  }
  const std::string jar_name = android::base::Basename(jar_path);
  return "boot-" + ReplaceFileExtension(jar_name, "art");
}

void AddCompiledBootClasspathFdsIfAny(
    /*inout*/ std::vector<std::string>& args,
    /*inout*/ std::vector<std::unique_ptr<File>>& output_files,
    const std::vector<std::string>& bcp_jars,
    const InstructionSet isa,
    const std::string& artifact_dir) {
  std::vector<std::string> bcp_image_fds;
  std::vector<std::string> bcp_oat_fds;
  std::vector<std::string> bcp_vdex_fds;
  std::vector<std::unique_ptr<File>> opened_files;
  bool added_any = false;
  for (size_t i = 0; i < bcp_jars.size(); i++) {
    const std::string& jar = bcp_jars[i];
    std::string image_path =
        artifact_dir + "/" + GetBootImageComponentBasename(jar, /*is_first_jar=*/i == 0);
    image_path = GetSystemImageFilename(image_path.c_str(), isa);
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
    args.emplace_back(
        Concatenate({"-Xbootclasspathimagefds:", android::base::Join(bcp_image_fds, ':')}));
    args.emplace_back("--runtime-arg");
    args.emplace_back(
        Concatenate({"-Xbootclasspathoatfds:", android::base::Join(bcp_oat_fds, ':')}));
    args.emplace_back("--runtime-arg");
    args.emplace_back(
        Concatenate({"-Xbootclasspathvdexfds:", android::base::Join(bcp_vdex_fds, ':')}));
  }
}

std::string GetStagingLocation(const std::string& staging_dir, const std::string& path) {
  return Concatenate({staging_dir, "/", android::base::Basename(path)});
}

WARN_UNUSED bool CheckCompilationSpace() {
  // Check the available storage space against an arbitrary threshold because dex2oat does not
  // report when it runs out of storage space and we do not want to completely fill
  // the users data partition.
  //
  // We do not have a good way of pre-computing the required space for a compilation step, but
  // typically observe no more than 48MiB as the largest total size of AOT artifacts for a single
  // dex2oat invocation, which includes an image file, an executable file, and a verification data
  // file.
  static constexpr uint64_t kMinimumSpaceForCompilation = 48 * 1024 * 1024;

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

std::string GetSystemBootImageDir() { return GetAndroidRoot() + "/framework"; }

bool HasVettedDeviceSystemServerProfiles() {
  // While system_server profiles were bundled on the device prior to U+, they were not used by
  // default or rigorously tested, so we cannot vouch for their efficacy.
  static const bool kDeviceIsAtLeastU = IsAtLeastU();
  return kDeviceIsAtLeastU;
}

}  // namespace

OnDeviceRefresh::OnDeviceRefresh(const OdrConfig& config)
    : OnDeviceRefresh(config,
                      Concatenate({config.GetArtifactDirectory(), "/", kCacheInfoFile}),
                      std::make_unique<ExecUtils>()) {}

OnDeviceRefresh::OnDeviceRefresh(const OdrConfig& config,
                                 const std::string& cache_info_filename,
                                 std::unique_ptr<ExecUtils> exec_utils)
    : config_{config},
      cache_info_filename_{cache_info_filename},
      start_time_{time(nullptr)},
      exec_utils_{std::move(exec_utils)} {
  for (const std::string& jar : android::base::Split(config_.GetDex2oatBootClasspath(), ":")) {
    // Updatable APEXes should not have DEX files in the DEX2OATBOOTCLASSPATH. At the time of
    // writing i18n is a non-updatable APEX and so does appear in the DEX2OATBOOTCLASSPATH.
    boot_classpath_compilable_jars_.emplace_back(jar);
  }

  all_systemserver_jars_ = android::base::Split(config_.GetSystemServerClasspath(), ":");
  systemserver_classpath_jars_ = {all_systemserver_jars_.begin(), all_systemserver_jars_.end()};
  boot_classpath_jars_ = android::base::Split(config_.GetBootClasspath(), ":");
  std::string standalone_system_server_jars_str = config_.GetStandaloneSystemServerJars();
  if (!standalone_system_server_jars_str.empty()) {
    std::vector<std::string> standalone_systemserver_jars =
        android::base::Split(standalone_system_server_jars_str, ":");
    std::move(standalone_systemserver_jars.begin(),
              standalone_systemserver_jars.end(),
              std::back_inserter(all_systemserver_jars_));
  }
}

time_t OnDeviceRefresh::GetExecutionTimeUsed() const { return time(nullptr) - start_time_; }

time_t OnDeviceRefresh::GetExecutionTimeRemaining() const {
  return std::max(static_cast<time_t>(0),
                  kMaximumExecutionSeconds - GetExecutionTimeUsed());
}

time_t OnDeviceRefresh::GetSubprocessTimeout() const {
  return std::min(GetExecutionTimeRemaining(), kMaxChildProcessSeconds);
}

std::optional<std::vector<apex::ApexInfo>> OnDeviceRefresh::GetApexInfoList() const {
  std::optional<apex::ApexInfoList> info_list =
      apex::readApexInfoList(config_.GetApexInfoListFile().c_str());
  if (!info_list.has_value()) {
    return std::nullopt;
  }

  // We are only interested in active APEXes that contain compilable JARs.
  std::unordered_set<std::string_view> relevant_apexes;
  relevant_apexes.reserve(info_list->getApexInfo().size());
  for (const std::vector<std::string>* jar_list :
       {&boot_classpath_compilable_jars_, &all_systemserver_jars_, &boot_classpath_jars_}) {
    for (auto& jar : *jar_list) {
      std::string_view apex = ApexNameFromLocation(jar);
      if (!apex.empty()) {
        relevant_apexes.insert(apex);
      }
    }
  }
  // The ART APEX is always relevant no matter it contains any compilable JAR or not, because it
  // contains the runtime.
  relevant_apexes.insert("com.android.art");

  std::vector<apex::ApexInfo> filtered_info_list;
  std::copy_if(info_list->getApexInfo().begin(),
               info_list->getApexInfo().end(),
               std::back_inserter(filtered_info_list),
               [&](const apex::ApexInfo& info) {
                 return info.getIsActive() && relevant_apexes.count(info.getModuleName()) != 0;
               });
  return filtered_info_list;
}

Result<art_apex::CacheInfo> OnDeviceRefresh::ReadCacheInfo() const {
  std::optional<art_apex::CacheInfo> cache_info = art_apex::read(cache_info_filename_.c_str());
  if (!cache_info.has_value()) {
    if (errno != 0) {
      return ErrnoErrorf("Failed to load {}", QuotePath(cache_info_filename_));
    } else {
      return Errorf("Failed to parse {}", QuotePath(cache_info_filename_));
    }
  }
  return cache_info.value();
}

Result<void> OnDeviceRefresh::WriteCacheInfo() const {
  if (OS::FileExists(cache_info_filename_.c_str())) {
    if (unlink(cache_info_filename_.c_str()) != 0) {
      return ErrnoErrorf("Failed to unlink() file {}", QuotePath(cache_info_filename_));
    }
  }

  const std::string dir_name = android::base::Dirname(cache_info_filename_);
  if (!EnsureDirectoryExists(dir_name)) {
    return Errorf("Could not create directory {}", QuotePath(dir_name));
  }

  std::vector<art_apex::KeyValuePair> system_properties;
  for (const auto& [key, value] : config_.GetSystemProperties()) {
    system_properties.emplace_back(key, value);
  }

  std::optional<std::vector<apex::ApexInfo>> apex_info_list = GetApexInfoList();
  if (!apex_info_list.has_value()) {
    return Errorf("Could not update {}: no APEX info", QuotePath(cache_info_filename_));
  }

  std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list.value());
  if (!art_apex_info.has_value()) {
    return Errorf("Could not update {}: no ART APEX info", QuotePath(cache_info_filename_));
  }

  art_apex::ModuleInfo art_module_info = GenerateModuleInfo(art_apex_info.value());
  std::vector<art_apex::ModuleInfo> module_info_list =
      GenerateModuleInfoList(apex_info_list.value());

  std::optional<std::vector<art_apex::Component>> bcp_components =
      GenerateBootClasspathComponents();
  if (!bcp_components.has_value()) {
    return Errorf("No boot classpath components.");
  }

  std::optional<std::vector<art_apex::Component>> bcp_compilable_components =
      GenerateBootClasspathCompilableComponents();
  if (!bcp_compilable_components.has_value()) {
    return Errorf("No boot classpath compilable components.");
  }

  std::optional<std::vector<art_apex::SystemServerComponent>> system_server_components =
      GenerateSystemServerComponents();
  if (!system_server_components.has_value()) {
    return Errorf("No system_server components.");
  }

  std::ofstream out(cache_info_filename_.c_str());
  if (out.fail()) {
    return Errorf("Cannot open {} for writing.", QuotePath(cache_info_filename_));
  }

  std::unique_ptr<art_apex::CacheInfo> info(new art_apex::CacheInfo(
      {art_apex::KeyValuePairList(system_properties)},
      {art_module_info},
      {art_apex::ModuleInfoList(module_info_list)},
      {art_apex::Classpath(bcp_components.value())},
      {art_apex::Classpath(bcp_compilable_components.value())},
      {art_apex::SystemServerComponents(system_server_components.value())},
      config_.GetCompilationOsMode() ? std::make_optional(true) : std::nullopt));

  art_apex::write(out, *info);
  out.close();
  if (out.fail()) {
    return Errorf("Cannot write to {}", QuotePath(cache_info_filename_));
  }

  return {};
}

static void ReportNextBootAnimationProgress(uint32_t current_compilation,
                                            uint32_t number_of_compilations) {
  // We arbitrarily show progress until 90%, expecting that our compilations take a large chunk of
  // boot time.
  uint32_t value = (90 * current_compilation) / number_of_compilations;
  android::base::SetProperty("service.bootanim.progress", std::to_string(value));
}

std::vector<art_apex::Component> OnDeviceRefresh::GenerateBootClasspathComponents() const {
  return GenerateComponents(boot_classpath_jars_);
}

std::vector<art_apex::Component> OnDeviceRefresh::GenerateBootClasspathCompilableComponents()
    const {
  return GenerateComponents(boot_classpath_compilable_jars_);
}

std::vector<art_apex::SystemServerComponent> OnDeviceRefresh::GenerateSystemServerComponents()
    const {
  return GenerateComponents<art_apex::SystemServerComponent>(
      all_systemserver_jars_,
      [&](const std::string& path, uint64_t size, const std::string& checksum) {
        bool isInClasspath = ContainsElement(systemserver_classpath_jars_, path);
        return art_apex::SystemServerComponent{path, size, checksum, isInClasspath};
      });
}

std::string OnDeviceRefresh::GetBootImage(bool on_system, bool minimal) const {
  DCHECK(!on_system || !minimal);
  const char* basename = minimal ? kMinimalBootImageBasename : kFirstBootImageBasename;
  if (on_system) {
    // Typically "/system/framework/boot.art".
    return GetPrebuiltPrimaryBootImageDir() + "/" + basename;
  } else {
    // Typically "/data/misc/apexdata/com.android.art/dalvik-cache/boot.art".
    return config_.GetArtifactDirectory() + "/" + basename;
  }
}

std::string OnDeviceRefresh::GetBootImagePath(bool on_system,
                                              bool minimal,
                                              const InstructionSet isa) const {
  // Typically "/data/misc/apexdata/com.android.art/dalvik-cache/<isa>/boot.art".
  return GetSystemImageFilename(GetBootImage(on_system, minimal).c_str(), isa);
}

std::string OnDeviceRefresh::GetSystemBootImageExtension() const {
  std::string art_root = GetArtRoot() + "/";
  // Find the first boot extension jar.
  auto it = std::find_if_not(
      boot_classpath_compilable_jars_.begin(),
      boot_classpath_compilable_jars_.end(),
      [&](const std::string& jar) { return android::base::StartsWith(jar, art_root); });
  CHECK(it != boot_classpath_compilable_jars_.end());
  // Typically "/system/framework/boot-framework.art".
  return GetSystemBootImageDir() + "/" + GetBootImageComponentBasename(*it, /*is_first_jar=*/false);
}

std::string OnDeviceRefresh::GetSystemBootImageExtensionPath(const InstructionSet isa) const {
  // Typically "/system/framework/<isa>/boot-framework.art".
  return GetSystemImageFilename(GetSystemBootImageExtension().c_str(), isa);
}

std::string OnDeviceRefresh::GetSystemServerImagePath(bool on_system,
                                                      const std::string& jar_path) const {
  if (on_system) {
    if (LocationIsOnApex(jar_path)) {
      return GetSystemOdexFilenameForApex(jar_path, config_.GetSystemServerIsa());
    }
    const std::string jar_name = android::base::Basename(jar_path);
    const std::string image_name = ReplaceFileExtension(jar_name, "art");
    const char* isa_str = GetInstructionSetString(config_.GetSystemServerIsa());
    // Typically "/system/framework/oat/<isa>/services.art".
    return Concatenate({android::base::Dirname(jar_path), "/oat/", isa_str, "/", image_name});
  } else {
    // Typically
    // "/data/misc/apexdata/.../dalvik-cache/<isa>/system@framework@services.jar@classes.art".
    const std::string image = GetApexDataImage(jar_path.c_str());
    return GetSystemImageFilename(image.c_str(), config_.GetSystemServerIsa());
  }
}

WARN_UNUSED bool OnDeviceRefresh::RemoveArtifactsDirectory() const {
  if (config_.GetDryRun()) {
    LOG(INFO) << "Directory " << QuotePath(config_.GetArtifactDirectory())
              << " and contents would be removed (dry-run).";
    return true;
  }
  return RemoveDirectory(config_.GetArtifactDirectory());
}

WARN_UNUSED bool OnDeviceRefresh::BootClasspathArtifactsExist(
    bool on_system,
    bool minimal,
    const InstructionSet isa,
    /*out*/ std::string* error_msg,
    /*out*/ std::vector<std::string>* checked_artifacts) const {
  std::string path = GetBootImagePath(on_system, minimal, isa);
  OdrArtifacts artifacts = OdrArtifacts::ForBootImage(path);
  if (!ArtifactsExist(artifacts, /*check_art_file=*/true, error_msg, checked_artifacts)) {
    return false;
  }
  // There is a split between the primary boot image and the extension on /system, so they need to
  // be checked separately. This does not apply to the boot image on /data.
  if (on_system) {
    std::string extension_path = GetSystemBootImageExtensionPath(isa);
    OdrArtifacts extension_artifacts = OdrArtifacts::ForBootImage(extension_path);
    if (!ArtifactsExist(
            extension_artifacts, /*check_art_file=*/true, error_msg, checked_artifacts)) {
      return false;
    }
  }
  return true;
}

bool OnDeviceRefresh::SystemServerArtifactsExist(
    bool on_system,
    /*out*/ std::string* error_msg,
    /*out*/ std::set<std::string>* jars_missing_artifacts,
    /*out*/ std::vector<std::string>* checked_artifacts) const {
  for (const std::string& jar_path : all_systemserver_jars_) {
    const std::string image_location = GetSystemServerImagePath(on_system, jar_path);
    const OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
    // .art files are optional and are not generated for all jars by the build system.
    const bool check_art_file = !on_system;
    std::string error_msg_tmp;
    if (!ArtifactsExist(artifacts, check_art_file, &error_msg_tmp, checked_artifacts)) {
      jars_missing_artifacts->insert(jar_path);
      *error_msg = error_msg->empty() ? error_msg_tmp : *error_msg + "\n" + error_msg_tmp;
    }
  }
  return jars_missing_artifacts->empty();
}

WARN_UNUSED bool OnDeviceRefresh::CheckSystemPropertiesAreDefault() const {
  // We don't have to check properties that match `kCheckedSystemPropertyPrefixes` here because none
  // of them is persistent. This only applies when `cache-info.xml` does not exist. When
  // `cache-info.xml` exists, we call `CheckSystemPropertiesHaveNotChanged` instead.
  DCHECK(std::none_of(std::begin(kCheckedSystemPropertyPrefixes),
                      std::end(kCheckedSystemPropertyPrefixes),
                      [](const char* prefix) { return StartsWith(prefix, "persist."); }));

  const std::unordered_map<std::string, std::string>& system_properties =
      config_.GetSystemProperties();

  for (const SystemPropertyConfig& system_property_config : *kSystemProperties.get()) {
    auto property = system_properties.find(system_property_config.name);
    DCHECK(property != system_properties.end());

    if (property->second != system_property_config.default_value) {
      LOG(INFO) << "System property " << system_property_config.name << " has a non-default value ("
                << property->second << ").";
      return false;
    }
  }

  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CheckSystemPropertiesHaveNotChanged(
    const art_apex::CacheInfo& cache_info) const {
  std::unordered_map<std::string, std::string> cached_system_properties;
  std::unordered_set<std::string> checked_properties;

  const art_apex::KeyValuePairList* list = cache_info.getFirstSystemProperties();
  if (list == nullptr) {
    // This should never happen. We have already checked the ART module version, and the cache
    // info is generated by the latest version of the ART module if it exists.
    LOG(ERROR) << "Missing system properties from cache-info.";
    return false;
  }

  for (const art_apex::KeyValuePair& pair : list->getItem()) {
    cached_system_properties[pair.getK()] = pair.getV();
    checked_properties.insert(pair.getK());
  }

  const std::unordered_map<std::string, std::string>& system_properties =
      config_.GetSystemProperties();

  for (const auto& [key, value] : system_properties) {
    checked_properties.insert(key);
  }

  for (const std::string& name : checked_properties) {
    auto property_it = system_properties.find(name);
    std::string property = property_it != system_properties.end() ? property_it->second : "";
    std::string cached_property = cached_system_properties[name];

    if (property != cached_property) {
      LOG(INFO) << "System property " << name << " value changed (before: \"" << cached_property
                << "\", now: \"" << property << "\").";
      return false;
    }
  }

  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CheckBuildUserfaultFdGc() const {
  auto it = config_.GetSystemProperties().find("ro.dalvik.vm.enable_uffd_gc");
  bool build_enable_uffd_gc = it != config_.GetSystemProperties().end() ?
                                  ParseBool(it->second) == ParseBoolResult::kTrue :
                                  false;
  bool kernel_supports_uffd = KernelSupportsUffd();
  if (build_enable_uffd_gc && !kernel_supports_uffd) {
    // Normally, this should not happen. If this happens, the system image was probably built with a
    // wrong PRODUCT_ENABLE_UFFD_GC flag.
    LOG(WARNING) << "Userfaultfd GC check failed (build-time: {}, runtime: {})."_format(
        build_enable_uffd_gc, kernel_supports_uffd);
    return false;
  }
  return true;
}

WARN_UNUSED PreconditionCheckResult OnDeviceRefresh::CheckPreconditionForSystem(
    const std::vector<apex::ApexInfo>& apex_info_list) const {
  if (!CheckSystemPropertiesAreDefault()) {
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  if (!CheckBuildUserfaultFdGc()) {
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  std::optional<apex::ApexInfo> art_apex_info = GetArtApexInfo(apex_info_list);
  if (!art_apex_info.has_value()) {
    // This should never happen, further up-to-date checks are not possible if it does.
    LOG(ERROR) << "Could not get ART APEX info.";
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kUnknown);
  }

  if (!art_apex_info->getIsFactory()) {
    LOG(INFO) << "Updated ART APEX mounted";
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  if (std::any_of(apex_info_list.begin(),
                  apex_info_list.end(),
                  [](const apex::ApexInfo& apex_info) { return !apex_info.getIsFactory(); })) {
    LOG(INFO) << "Updated APEXes mounted";
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  return PreconditionCheckResult::AllOk();
}

WARN_UNUSED static bool CheckModuleInfo(const art_apex::ModuleInfo& cached_info,
                                        const apex::ApexInfo& current_info) {
  if (cached_info.getVersionCode() != current_info.getVersionCode()) {
    LOG(INFO) << "APEX ({}) version code mismatch (before: {}, now: {})"_format(
        current_info.getModuleName(), cached_info.getVersionCode(), current_info.getVersionCode());
    return false;
  }

  if (cached_info.getVersionName() != current_info.getVersionName()) {
    LOG(INFO) << "APEX ({}) version name mismatch (before: {}, now: {})"_format(
        current_info.getModuleName(), cached_info.getVersionName(), current_info.getVersionName());
    return false;
  }

  // Check lastUpdateMillis for samegrade installs. If `cached_info` is missing the lastUpdateMillis
  // field then it is not current with the schema used by this binary so treat it as a samegrade
  // update. Otherwise check whether the lastUpdateMillis changed.
  const int64_t cached_last_update_millis =
      cached_info.hasLastUpdateMillis() ? cached_info.getLastUpdateMillis() : -1;
  if (cached_last_update_millis != current_info.getLastUpdateMillis()) {
    LOG(INFO) << "APEX ({}) last update time mismatch (before: {}, now: {})"_format(
        current_info.getModuleName(),
        cached_info.getLastUpdateMillis(),
        current_info.getLastUpdateMillis());
    return false;
  }

  return true;
}

WARN_UNUSED PreconditionCheckResult OnDeviceRefresh::CheckPreconditionForData(
    const std::vector<com::android::apex::ApexInfo>& apex_info_list) const {
  Result<art_apex::CacheInfo> cache_info = ReadCacheInfo();
  if (!cache_info.ok()) {
    if (cache_info.error().code() == ENOENT) {
      // If the cache info file does not exist, it usually means it's the first boot, or the
      // dalvik-cache directory is cleared by odsign due to corrupted files. Set the trigger to be
      // `kApexVersionMismatch` to force generate the cache info file and compile if necessary.
      LOG(INFO) << "No prior cache-info file: " << QuotePath(cache_info_filename_);
    } else {
      // This should not happen unless odrefresh is updated to a new version that is not compatible
      // with an old cache-info file. Further up-to-date checks are not possible if it does.
      LOG(ERROR) << cache_info.error().message();
    }
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  if (!CheckSystemPropertiesHaveNotChanged(cache_info.value())) {
    // We don't have a trigger kind for system property changes. For now, we reuse
    // `kApexVersionMismatch` as it implies the expected behavior: re-compile regardless of the last
    // compilation attempt.
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  // Check whether the current cache ART module info differs from the current ART module info.
  const art_apex::ModuleInfo* cached_art_info = cache_info->getFirstArtModuleInfo();
  if (cached_art_info == nullptr) {
    LOG(ERROR) << "Missing ART APEX info from cache-info.";
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  std::optional<apex::ApexInfo> current_art_info = GetArtApexInfo(apex_info_list);
  if (!current_art_info.has_value()) {
    // This should never happen, further up-to-date checks are not possible if it does.
    LOG(ERROR) << "Could not get ART APEX info.";
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kUnknown);
  }

  if (!CheckModuleInfo(*cached_art_info, *current_art_info)) {
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  // Check boot class components.
  //
  // This checks the size and checksums of odrefresh compilable files on the DEX2OATBOOTCLASSPATH
  // (the Odrefresh constructor determines which files are compilable). If the number of files
  // there changes, or their size or checksums change then compilation will be triggered.
  //
  // The boot class components may change unexpectedly, for example an OTA could update
  // framework.jar.
  const std::vector<art_apex::Component> current_bcp_compilable_components =
      GenerateBootClasspathCompilableComponents();

  const art_apex::Classpath* cached_bcp_compilable_components =
      cache_info->getFirstDex2oatBootClasspath();
  if (cached_bcp_compilable_components == nullptr) {
    LOG(INFO) << "Missing Dex2oatBootClasspath components.";
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  Result<void> result = CheckComponents(current_bcp_compilable_components,
                                        cached_bcp_compilable_components->getComponent());
  if (!result.ok()) {
    LOG(INFO) << "Dex2OatClasspath components mismatch: " << result.error();
    return PreconditionCheckResult::NoneOk(OdrMetrics::Trigger::kDexFilesChanged);
  }

  // Check whether the current cached module info differs from the current module info.
  const art_apex::ModuleInfoList* cached_module_info_list = cache_info->getFirstModuleInfoList();
  if (cached_module_info_list == nullptr) {
    LOG(ERROR) << "Missing APEX info list from cache-info.";
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  std::unordered_map<std::string, const art_apex::ModuleInfo*> cached_module_info_map;
  for (const art_apex::ModuleInfo& module_info : cached_module_info_list->getModuleInfo()) {
    cached_module_info_map[module_info.getName()] = &module_info;
  }

  // Note that apex_info_list may omit APEXes that are included in cached_module_info - e.g. if an
  // apex used to be compilable, but now isn't. That won't be detected by this loop, but will be
  // detected below in CheckComponents.
  for (const apex::ApexInfo& current_apex_info : apex_info_list) {
    auto& apex_name = current_apex_info.getModuleName();

    auto it = cached_module_info_map.find(apex_name);
    if (it == cached_module_info_map.end()) {
      LOG(INFO) << "Missing APEX info from cache-info (" << apex_name << ").";
      return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
    }

    const art_apex::ModuleInfo* cached_module_info = it->second;
    if (!CheckModuleInfo(*cached_module_info, current_apex_info)) {
      return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
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
  const std::vector<art_apex::SystemServerComponent> current_system_server_components =
      GenerateSystemServerComponents();

  const art_apex::SystemServerComponents* cached_system_server_components =
      cache_info->getFirstSystemServerComponents();
  if (cached_system_server_components == nullptr) {
    LOG(INFO) << "Missing SystemServerComponents.";
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  result = CheckSystemServerComponents(current_system_server_components,
                                       cached_system_server_components->getComponent());
  if (!result.ok()) {
    LOG(INFO) << "SystemServerComponents mismatch: " << result.error();
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kDexFilesChanged);
  }

  const std::vector<art_apex::Component> current_bcp_components = GenerateBootClasspathComponents();

  const art_apex::Classpath* cached_bcp_components = cache_info->getFirstBootClasspath();
  if (cached_bcp_components == nullptr) {
    LOG(INFO) << "Missing BootClasspath components.";
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kApexVersionMismatch);
  }

  result = CheckComponents(current_bcp_components, cached_bcp_components->getComponent());
  if (!result.ok()) {
    LOG(INFO) << "BootClasspath components mismatch: " << result.error();
    // Boot classpath components can be dependencies of system_server components, so system_server
    // components need to be recompiled if boot classpath components are changed.
    return PreconditionCheckResult::SystemServerNotOk(OdrMetrics::Trigger::kDexFilesChanged);
  }

  return PreconditionCheckResult::AllOk();
}

WARN_UNUSED bool OnDeviceRefresh::CheckBootClasspathArtifactsAreUpToDate(
    OdrMetrics& metrics,
    const InstructionSet isa,
    const PreconditionCheckResult& system_result,
    const PreconditionCheckResult& data_result,
    /*out*/ std::vector<std::string>* checked_artifacts) const {
  if (system_result.IsBootClasspathOk()) {
    // We can use the artifacts on /system. Check if they exist.
    std::string error_msg;
    if (BootClasspathArtifactsExist(/*on_system=*/true, /*minimal=*/false, isa, &error_msg)) {
      return true;
    }

    LOG(INFO) << "Incomplete boot classpath artifacts on /system: " << error_msg;
    LOG(INFO) << "Checking /data";
  }

  if (!data_result.IsBootClasspathOk()) {
    metrics.SetTrigger(data_result.GetTrigger());
    return false;
  }

  // Cache info looks good, check all compilation artifacts exist.
  std::string error_msg;
  if (!BootClasspathArtifactsExist(
          /*on_system=*/false, /*minimal=*/false, isa, &error_msg, checked_artifacts)) {
    LOG(INFO) << "Incomplete boot classpath artifacts on /data: " << error_msg;
    metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    // Add the minimal boot image to `checked_artifacts` if exists. This is to prevent the minimal
    // boot image from being deleted. It does not affect the return value because we should still
    // attempt to generate a full boot image even if the minimal one exists.
    if (BootClasspathArtifactsExist(
            /*on_system=*/false, /*minimal=*/true, isa, &error_msg, checked_artifacts)) {
      LOG(INFO) << "Found minimal boot classpath artifacts";
    }
    return false;
  }

  LOG(INFO) << "Boot classpath artifacts on /data OK";
  return true;
}

bool OnDeviceRefresh::CheckSystemServerArtifactsAreUpToDate(
    OdrMetrics& metrics,
    const PreconditionCheckResult& system_result,
    const PreconditionCheckResult& data_result,
    /*out*/ std::set<std::string>* jars_to_compile,
    /*out*/ std::vector<std::string>* checked_artifacts) const {
  std::set<std::string> jars_missing_artifacts_on_system;
  if (system_result.IsSystemServerOk()) {
    // We can use the artifacts on /system. Check if they exist.
    std::string error_msg;
    if (SystemServerArtifactsExist(
            /*on_system=*/true, &error_msg, &jars_missing_artifacts_on_system)) {
      return true;
    }

    LOG(INFO) << "Incomplete system server artifacts on /system: " << error_msg;
    LOG(INFO) << "Checking /data";
  } else {
    jars_missing_artifacts_on_system = AllSystemServerJars();
  }

  std::set<std::string> jars_missing_artifacts_on_data;
  std::string error_msg;
  if (data_result.IsSystemServerOk()) {
    SystemServerArtifactsExist(
        /*on_system=*/false, &error_msg, &jars_missing_artifacts_on_data, checked_artifacts);
  } else {
    jars_missing_artifacts_on_data = AllSystemServerJars();
  }

  std::set_intersection(jars_missing_artifacts_on_system.begin(),
                        jars_missing_artifacts_on_system.end(),
                        jars_missing_artifacts_on_data.begin(),
                        jars_missing_artifacts_on_data.end(),
                        std::inserter(*jars_to_compile, jars_to_compile->end()));
  if (!jars_to_compile->empty()) {
    if (data_result.IsSystemServerOk()) {
      LOG(INFO) << "Incomplete system_server artifacts on /data: " << error_msg;
      metrics.SetTrigger(OdrMetrics::Trigger::kMissingArtifacts);
    } else {
      metrics.SetTrigger(data_result.GetTrigger());
    }
    return false;
  }

  LOG(INFO) << "system_server artifacts on /data OK";
  return true;
}

Result<void> OnDeviceRefresh::CleanupArtifactDirectory(
    OdrMetrics& metrics, const std::vector<std::string>& artifacts_to_keep) const {
  const std::string& artifact_dir = config_.GetArtifactDirectory();
  std::unordered_set<std::string> artifact_set{artifacts_to_keep.begin(), artifacts_to_keep.end()};

  // When anything unexpected happens, remove all artifacts.
  auto remove_artifact_dir = android::base::make_scope_guard([&]() {
    if (!RemoveDirectory(artifact_dir)) {
      LOG(ERROR) << "Failed to remove the artifact directory";
    }
  });

  std::vector<std::filesystem::directory_entry> entries;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(artifact_dir, ec)) {
    // Save the entries and use them later because modifications during the iteration will result in
    // undefined behavior;
    entries.push_back(entry);
  }
  if (ec && ec.value() != ENOENT) {
    metrics.SetStatus(ec.value() == EPERM ? OdrMetrics::Status::kDalvikCachePermissionDenied :
                                            OdrMetrics::Status::kIoError);
    return Errorf("Failed to iterate over entries in the artifact directory: {}", ec.message());
  }

  for (const std::filesystem::directory_entry& entry : entries) {
    std::string path = entry.path().string();
    if (entry.is_regular_file()) {
      if (!ContainsElement(artifact_set, path)) {
        LOG(INFO) << "Removing " << path;
        if (unlink(path.c_str()) != 0) {
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          return ErrnoErrorf("Failed to remove file {}", QuotePath(path));
        }
      }
    } else if (!entry.is_directory()) {
      // Neither a regular file nor a directory. Unexpected file type.
      LOG(INFO) << "Removing " << path;
      if (unlink(path.c_str()) != 0) {
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        return ErrnoErrorf("Failed to remove file {}", QuotePath(path));
      }
    }
  }

  remove_artifact_dir.Disable();
  return {};
}

Result<void> OnDeviceRefresh::RefreshExistingArtifacts() const {
  const std::string& artifact_dir = config_.GetArtifactDirectory();
  if (!OS::DirectoryExists(artifact_dir.c_str())) {
    return {};
  }

  std::vector<std::filesystem::directory_entry> entries;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(artifact_dir, ec)) {
    // Save the entries and use them later because modifications during the iteration will result in
    // undefined behavior;
    entries.push_back(entry);
  }
  if (ec) {
    return Errorf("Failed to iterate over entries in the artifact directory: {}", ec.message());
  }

  for (const std::filesystem::directory_entry& entry : entries) {
    std::string path = entry.path().string();
    if (entry.is_regular_file()) {
      // Unexpected files are already removed by `CleanupArtifactDirectory`. We can safely assume
      // that all the remaining files are good.
      LOG(INFO) << "Refreshing " << path;
      std::string content;
      if (!android::base::ReadFileToString(path, &content)) {
        return Errorf("Failed to read file {}", QuotePath(path));
      }
      if (unlink(path.c_str()) != 0) {
        return ErrnoErrorf("Failed to remove file {}", QuotePath(path));
      }
      if (!android::base::WriteStringToFile(content, path)) {
        return Errorf("Failed to write file {}", QuotePath(path));
      }
      if (chmod(path.c_str(), kFileMode) != 0) {
        return ErrnoErrorf("Failed to chmod file {}", QuotePath(path));
      }
    }
  }

  return {};
}

WARN_UNUSED ExitCode
OnDeviceRefresh::CheckArtifactsAreUpToDate(OdrMetrics& metrics,
                                           /*out*/ CompilationOptions* compilation_options) const {
  metrics.SetStage(OdrMetrics::Stage::kCheck);

  // Clean-up helper used to simplify clean-ups and handling failures there.
  auto cleanup_and_compile_all = [&, this]() {
    compilation_options->compile_boot_classpath_for_isas = config_.GetBootClasspathIsas();
    compilation_options->system_server_jars_to_compile = AllSystemServerJars();
    if (!RemoveArtifactsDirectory()) {
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      return ExitCode::kCleanupFailed;
    }
    return ExitCode::kCompilationRequired;
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

  // Log the version so there's a starting point for any issues reported (b/197489543).
  LOG(INFO) << "ART APEX version " << art_apex_info->getVersionCode();

  // Record ART APEX last update milliseconds (used in compilation log).
  metrics.SetArtApexLastUpdateMillis(art_apex_info->getLastUpdateMillis());

  InstructionSet system_server_isa = config_.GetSystemServerIsa();
  std::vector<std::string> checked_artifacts;

  PreconditionCheckResult system_result = CheckPreconditionForSystem(apex_info_list.value());
  PreconditionCheckResult data_result = CheckPreconditionForData(apex_info_list.value());

  for (const InstructionSet isa : config_.GetBootClasspathIsas()) {
    if (!CheckBootClasspathArtifactsAreUpToDate(
            metrics, isa, system_result, data_result, &checked_artifacts)) {
      compilation_options->compile_boot_classpath_for_isas.push_back(isa);
      // system_server artifacts are invalid without valid boot classpath artifacts.
      if (isa == system_server_isa) {
        compilation_options->system_server_jars_to_compile = AllSystemServerJars();
      }
    }
  }

  if (compilation_options->system_server_jars_to_compile.empty()) {
    CheckSystemServerArtifactsAreUpToDate(metrics,
                                          system_result,
                                          data_result,
                                          &compilation_options->system_server_jars_to_compile,
                                          &checked_artifacts);
  }

  // Return kCompilationRequired to generate the cache info even if there's nothing to compile.
  bool compilation_required = !compilation_options->compile_boot_classpath_for_isas.empty() ||
                              !compilation_options->system_server_jars_to_compile.empty() ||
                              !data_result.IsAllOk();

  // If partial compilation is disabled, we should compile everything regardless of what's in
  // `compilation_options`.
  if (compilation_required && !config_.GetPartialCompilation()) {
    return cleanup_and_compile_all();
  }

  // Always keep the cache info.
  checked_artifacts.push_back(cache_info_filename_);

  Result<void> result = CleanupArtifactDirectory(metrics, checked_artifacts);
  if (!result.ok()) {
    LOG(ERROR) << result.error();
    return ExitCode::kCleanupFailed;
  }

  return compilation_required ? ExitCode::kCompilationRequired : ExitCode::kOkay;
}

WARN_UNUSED bool OnDeviceRefresh::CompileBootClasspathArtifacts(
    const InstructionSet isa,
    const std::string& staging_dir,
    OdrMetrics& metrics,
    const std::function<void()>& on_dex2oat_success,
    bool minimal,
    std::string* error_msg) const {
  ScopedOdrCompilationTimer compilation_timer(metrics);
  std::vector<std::string> args;
  args.push_back(config_.GetDex2Oat());

  AddDex2OatCommonOptions(args);
  AddDex2OatDebugInfo(args);
  AddDex2OatInstructionSet(args, isa);
  if (!AddDex2OatConcurrencyArguments(args, config_.GetCompilationOsMode())) {
    return false;
  }

  std::vector<std::unique_ptr<File>> readonly_files_raii;
  const std::string art_boot_profile_file = GetArtRoot() + "/etc/boot-image.prof";
  const std::string framework_boot_profile_file = GetAndroidRoot() + "/etc/boot-image.prof";
  bool has_any_profile = AddDex2OatProfile(
      args, readonly_files_raii, {art_boot_profile_file, framework_boot_profile_file});
  if (!has_any_profile) {
    *error_msg = "Missing boot image profile";
    return false;
  }
  const std::string& compiler_filter = config_.GetBootImageCompilerFilter();
  if (!compiler_filter.empty()) {
    args.emplace_back("--compiler-filter=" + compiler_filter);
  } else {
    args.emplace_back("--compiler-filter=speed-profile");
  }

  // Compile as a single image for fewer files and slightly less memory overhead.
  args.emplace_back("--single-image");

  args.emplace_back(android::base::StringPrintf("--base=0x%08x", ART_BASE_ADDRESS));

  const std::string dirty_image_objects_file(GetAndroidRoot() + "/etc/dirty-image-objects");
  if (OS::FileExists(dirty_image_objects_file.c_str())) {
    std::unique_ptr<File> file(OS::OpenFileForReading(dirty_image_objects_file.c_str()));
    args.emplace_back(android::base::StringPrintf("--dirty-image-objects-fd=%d", file->Fd()));
    readonly_files_raii.push_back(std::move(file));
  } else {
    LOG(WARNING) << "Missing dirty objects file : " << QuotePath(dirty_image_objects_file);
  }

  const std::string preloaded_classes_file(GetAndroidRoot() + "/etc/preloaded-classes");
  if (OS::FileExists(preloaded_classes_file.c_str())) {
    std::unique_ptr<File> file(OS::OpenFileForReading(preloaded_classes_file.c_str()));
    args.emplace_back(android::base::StringPrintf("--preloaded-classes-fds=%d", file->Fd()));
    readonly_files_raii.push_back(std::move(file));
  } else {
    LOG(WARNING) << "Missing preloaded classes file : " << QuotePath(preloaded_classes_file);
  }

  // Add boot classpath jars to compile.
  std::vector<std::string> jars_to_compile = boot_classpath_compilable_jars_;
  if (minimal) {
    auto end =
        std::remove_if(jars_to_compile.begin(), jars_to_compile.end(), [](const std::string& jar) {
          return !android::base::StartsWith(jar, GetArtRoot());
        });
    jars_to_compile.erase(end, jars_to_compile.end());
  }

  for (const std::string& component : jars_to_compile) {
    std::string actual_path = RewriteParentDirectoryIfNeeded(component);
    args.emplace_back("--dex-file=" + component);
    std::unique_ptr<File> file(OS::OpenFileForReading(actual_path.c_str()));
    args.emplace_back(android::base::StringPrintf("--dex-fd=%d", file->Fd()));
    readonly_files_raii.push_back(std::move(file));
  }

  args.emplace_back("--runtime-arg");
  args.emplace_back(Concatenate({"-Xbootclasspath:", android::base::Join(jars_to_compile, ":")}));
  if (!AddBootClasspathFds(args, readonly_files_raii, jars_to_compile)) {
    metrics.SetStatus(OdrMetrics::Status::kIoError);
    return false;
  }

  const std::string image_location = GetBootImagePath(/*on_system=*/false, minimal, isa);
  const OdrArtifacts artifacts = OdrArtifacts::ForBootImage(image_location);

  args.emplace_back("--oat-location=" + artifacts.OatPath());
  const std::pair<const std::string, const char*> location_kind_pairs[] = {
      std::make_pair(artifacts.ImagePath(), "image"),
      std::make_pair(artifacts.OatPath(), "oat"),
      std::make_pair(artifacts.VdexPath(), "output-vdex")};
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

  const time_t timeout = GetSubprocessTimeout();
  const std::string cmd_line = android::base::Join(args, ' ');
  LOG(INFO) << android::base::StringPrintf("Compiling boot classpath (%s%s): %s [timeout %lds]",
                                           GetInstructionSetString(isa),
                                           minimal ? ", minimal" : "",
                                           cmd_line.c_str(),
                                           timeout);
  if (config_.GetDryRun()) {
    LOG(INFO) << "Compilation skipped (dry-run).";
    return true;
  }

  ExecResult dex2oat_result = exec_utils_->ExecAndReturnResult(args, timeout, error_msg);
  metrics.SetDex2OatResult(dex2oat_result);

  if (dex2oat_result.status != ExecResult::kExited || dex2oat_result.exit_code != 0) {
    metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
    EraseFiles(staging_files);
    return false;
  }

  if (!MoveOrEraseFiles(staging_files, install_location)) {
    metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
    return false;
  }

  on_dex2oat_success();
  return true;
}

WARN_UNUSED bool OnDeviceRefresh::CompileSystemServerArtifacts(
    const std::string& staging_dir,
    OdrMetrics& metrics,
    const std::set<std::string>& system_server_jars_to_compile,
    const std::function<void()>& on_dex2oat_success,
    std::string* error_msg) const {
  ScopedOdrCompilationTimer compilation_timer(metrics);
  std::vector<std::string> classloader_context;

  const std::string dex2oat = config_.GetDex2Oat();
  const InstructionSet isa = config_.GetSystemServerIsa();
  for (const std::string& jar : all_systemserver_jars_) {
    auto scope_guard = android::base::make_scope_guard([&]() {
      if (ContainsElement(systemserver_classpath_jars_, jar)) {
        classloader_context.emplace_back(jar);
      }
    });

    if (!ContainsElement(system_server_jars_to_compile, jar)) {
      continue;
    }

    std::vector<std::unique_ptr<File>> readonly_files_raii;
    std::vector<std::string> args;
    args.emplace_back(dex2oat);
    args.emplace_back("--dex-file=" + jar);

    std::string actual_jar_path = RewriteParentDirectoryIfNeeded(jar);
    std::unique_ptr<File> dex_file(OS::OpenFileForReading(actual_jar_path.c_str()));
    args.emplace_back(android::base::StringPrintf("--dex-fd=%d", dex_file->Fd()));
    readonly_files_raii.push_back(std::move(dex_file));

    AddDex2OatCommonOptions(args);
    AddDex2OatDebugInfo(args);
    AddDex2OatInstructionSet(args, isa);
    if (!AddDex2OatConcurrencyArguments(args, config_.GetCompilationOsMode())) {
      return false;
    }

    const std::string jar_name(android::base::Basename(jar));
    const std::string profile = Concatenate({GetAndroidRoot(), "/framework/", jar_name, ".prof"});
    const std::string& compiler_filter = config_.GetSystemServerCompilerFilter();
    const bool maybe_add_profile =
        !compiler_filter.empty() || HasVettedDeviceSystemServerProfiles();
    const bool has_added_profile =
        maybe_add_profile && AddDex2OatProfile(args, readonly_files_raii, {profile});
    if (!compiler_filter.empty()) {
      args.emplace_back("--compiler-filter=" + compiler_filter);
    } else if (has_added_profile) {
      args.emplace_back("--compiler-filter=speed-profile");
    } else {
      args.emplace_back("--compiler-filter=speed");
    }

    const std::string image_location = GetSystemServerImagePath(/*on_system=*/false, jar);
    const std::string install_location = android::base::Dirname(image_location);
    if (!EnsureDirectoryExists(install_location)) {
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      return false;
    }

    OdrArtifacts artifacts = OdrArtifacts::ForSystemServer(image_location);
    CHECK_EQ(artifacts.OatPath(), GetApexDataOdexFilename(jar.c_str(), isa));

    const std::pair<const std::string, const char*> location_kind_pairs[] = {
        std::make_pair(artifacts.ImagePath(), "app-image"),
        std::make_pair(artifacts.OatPath(), "oat"),
        std::make_pair(artifacts.VdexPath(), "output-vdex")};

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

    args.emplace_back("--runtime-arg");
    args.emplace_back(Concatenate({"-Xbootclasspath:", config_.GetBootClasspath()}));

    auto bcp_jars = android::base::Split(config_.GetBootClasspath(), ":");
    if (!AddBootClasspathFds(args, readonly_files_raii, bcp_jars)) {
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      return false;
    }
    std::string unused_error_msg;
    // If the boot classpath artifacts are not on /data, then the boot classpath are not re-compiled
    // and the artifacts must exist on /system.
    bool boot_image_on_system = !BootClasspathArtifactsExist(
        /*on_system=*/false, /*minimal=*/false, isa, &unused_error_msg);
    AddCompiledBootClasspathFdsIfAny(
        args,
        readonly_files_raii,
        bcp_jars,
        isa,
        boot_image_on_system ? GetSystemBootImageDir() : config_.GetArtifactDirectory());
    args.emplace_back(
        Concatenate({"--boot-image=",
                     boot_image_on_system ? GetBootImage(/*on_system=*/true, /*minimal=*/false) +
                                                ":" + GetSystemBootImageExtension() :
                                            GetBootImage(/*on_system=*/false, /*minimal=*/false)}));

    const std::string context_path = android::base::Join(classloader_context, ':');
    if (art::ContainsElement(systemserver_classpath_jars_, jar)) {
      args.emplace_back("--class-loader-context=PCL[" + context_path + "]");
    } else {
      args.emplace_back("--class-loader-context=PCL[];PCL[" + context_path + "]");
    }
    if (!classloader_context.empty()) {
      std::vector<int> fds;
      for (const std::string& path : classloader_context) {
        std::string actual_path = RewriteParentDirectoryIfNeeded(path);
        std::unique_ptr<File> file(OS::OpenFileForReading(actual_path.c_str()));
        if (!file->IsValid()) {
          PLOG(ERROR) << "Failed to open classloader context " << actual_path;
          metrics.SetStatus(OdrMetrics::Status::kIoError);
          return false;
        }
        fds.emplace_back(file->Fd());
        readonly_files_raii.emplace_back(std::move(file));
      }
      const std::string context_fds = android::base::Join(fds, ':');
      args.emplace_back(Concatenate({"--class-loader-context-fds=", context_fds}));
    }

    const time_t timeout = GetSubprocessTimeout();
    const std::string cmd_line = android::base::Join(args, ' ');
    LOG(INFO) << "Compiling " << jar << ": " << cmd_line << " [timeout " << timeout << "s]";
    if (config_.GetDryRun()) {
      LOG(INFO) << "Compilation skipped (dry-run).";
      return true;
    }

    ExecResult dex2oat_result = exec_utils_->ExecAndReturnResult(args, timeout, error_msg);
    metrics.SetDex2OatResult(dex2oat_result);

    if (dex2oat_result.status != ExecResult::kExited || dex2oat_result.exit_code != 0) {
      metrics.SetStatus(OdrMetrics::Status::kDex2OatError);
      EraseFiles(staging_files);
      return false;
    }

    if (!MoveOrEraseFiles(staging_files, install_location)) {
      metrics.SetStatus(OdrMetrics::Status::kInstallFailed);
      return false;
    }

    on_dex2oat_success();
  }

  return true;
}

WARN_UNUSED ExitCode OnDeviceRefresh::Compile(OdrMetrics& metrics,
                                              const CompilationOptions& compilation_options) const {
  const char* staging_dir = nullptr;
  metrics.SetStage(OdrMetrics::Stage::kPreparation);

  if (!EnsureDirectoryExists(config_.GetArtifactDirectory())) {
    LOG(ERROR) << "Failed to prepare artifact directory";
    metrics.SetStatus(errno == EPERM ? OdrMetrics::Status::kDalvikCachePermissionDenied :
                                       OdrMetrics::Status::kIoError);
    return ExitCode::kCleanupFailed;
  }

  if (config_.GetRefresh()) {
    Result<void> result = RefreshExistingArtifacts();
    if (!result.ok()) {
      LOG(ERROR) << "Failed to refresh existing artifacts: " << result.error();
      metrics.SetStatus(OdrMetrics::Status::kIoError);
      return ExitCode::kCleanupFailed;
    }
  }

  // Emit cache info before compiling. This can be used to throttle compilation attempts later.
  Result<void> result = WriteCacheInfo();
  if (!result.ok()) {
    LOG(ERROR) << result.error();
    metrics.SetStatus(OdrMetrics::Status::kIoError);
    return ExitCode::kCleanupFailed;
  }

  if (!config_.GetStagingDir().empty()) {
    staging_dir = config_.GetStagingDir().c_str();
  } else {
    // Create staging area and assign label for generating compilation artifacts.
    if (PaletteCreateOdrefreshStagingDirectory(&staging_dir) != PALETTE_STATUS_OK) {
      metrics.SetStatus(OdrMetrics::Status::kStagingFailed);
      return ExitCode::kCleanupFailed;
    }
  }

  std::string error_msg;

  uint32_t dex2oat_invocation_count = 0;
  uint32_t total_dex2oat_invocation_count =
      compilation_options.compile_boot_classpath_for_isas.size() +
      compilation_options.system_server_jars_to_compile.size();
  ReportNextBootAnimationProgress(dex2oat_invocation_count, total_dex2oat_invocation_count);
  auto advance_animation_progress = [&]() {
    ReportNextBootAnimationProgress(++dex2oat_invocation_count, total_dex2oat_invocation_count);
  };

  const auto& bcp_instruction_sets = config_.GetBootClasspathIsas();
  DCHECK(!bcp_instruction_sets.empty() && bcp_instruction_sets.size() <= 2);
  bool full_compilation_failed = false;
  for (const InstructionSet isa : compilation_options.compile_boot_classpath_for_isas) {
    auto stage = (isa == bcp_instruction_sets.front()) ? OdrMetrics::Stage::kPrimaryBootClasspath :
                                                         OdrMetrics::Stage::kSecondaryBootClasspath;
    metrics.SetStage(stage);
    if (!config_.GetMinimal()) {
      if (CheckCompilationSpace()) {
        if (CompileBootClasspathArtifacts(isa,
                                          staging_dir,
                                          metrics,
                                          advance_animation_progress,
                                          /*minimal=*/false,
                                          &error_msg)) {
          // Remove the minimal boot image only if the full boot image is successfully generated.
          std::string path = GetBootImagePath(/*on_system=*/false, /*minimal=*/true, isa);
          OdrArtifacts artifacts = OdrArtifacts::ForBootImage(path);
          unlink(artifacts.ImagePath().c_str());
          unlink(artifacts.OatPath().c_str());
          unlink(artifacts.VdexPath().c_str());
          continue;
        }
        LOG(ERROR) << "Compilation of BCP failed: " << error_msg;
      } else {
        metrics.SetStatus(OdrMetrics::Status::kNoSpace);
      }
    }

    // Fall back to generating a minimal boot image.
    // The compilation of the full boot image will be retried on later reboots with a backoff time,
    // and the minimal boot image will be removed once the compilation of the full boot image
    // succeeds.
    full_compilation_failed = true;
    std::string ignored_error_msg;
    if (BootClasspathArtifactsExist(
            /*on_system=*/false, /*minimal=*/true, isa, &ignored_error_msg)) {
      continue;
    }
    if (CompileBootClasspathArtifacts(isa,
                                      staging_dir,
                                      metrics,
                                      advance_animation_progress,
                                      /*minimal=*/true,
                                      &error_msg)) {
      continue;
    }
    LOG(ERROR) << "Compilation of minimal BCP failed: " << error_msg;
    if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
      return ExitCode::kCleanupFailed;
    }
    return ExitCode::kCompilationFailed;
  }

  if (full_compilation_failed) {
    if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
      return ExitCode::kCleanupFailed;
    }
    return ExitCode::kCompilationFailed;
  }

  if (!compilation_options.system_server_jars_to_compile.empty()) {
    metrics.SetStage(OdrMetrics::Stage::kSystemServerClasspath);

    if (!CheckCompilationSpace()) {
      metrics.SetStatus(OdrMetrics::Status::kNoSpace);
      // Return kCompilationFailed so odsign will keep and sign whatever we have been able to
      // compile.
      return ExitCode::kCompilationFailed;
    }

    if (!CompileSystemServerArtifacts(staging_dir,
                                      metrics,
                                      compilation_options.system_server_jars_to_compile,
                                      advance_animation_progress,
                                      &error_msg)) {
      LOG(ERROR) << "Compilation of system_server failed: " << error_msg;
      if (!config_.GetDryRun() && !RemoveDirectory(staging_dir)) {
        return ExitCode::kCleanupFailed;
      }
      return ExitCode::kCompilationFailed;
    }
  }

  metrics.SetStage(OdrMetrics::Stage::kComplete);
  metrics.SetStatus(OdrMetrics::Status::kOK);
  return ExitCode::kCompilationSuccess;
}

}  // namespace odrefresh
}  // namespace art
