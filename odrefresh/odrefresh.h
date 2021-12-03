/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef ART_ODREFRESH_ODREFRESH_H_
#define ART_ODREFRESH_ODREFRESH_H_

#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "android-base/result.h"
#include "com_android_apex.h"
#include "com_android_art.h"
#include "exec_utils.h"
#include "odr_artifacts.h"
#include "odr_config.h"
#include "odr_dexopt.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

struct CompilationOptions {
  // If not empty, compile the bootclasspath extensions for ISAs in the list.
  std::vector<InstructionSet> compile_boot_extensions_for_isas;

  // If not empty, compile the system server jars in the list.
  std::set<std::string> system_server_jars_to_compile;
};

class OnDeviceRefresh final {
 public:
  explicit OnDeviceRefresh(const OdrConfig& config);

  // Constructor with injections. For testing and internal use only.
  OnDeviceRefresh(const OdrConfig& config,
                  const std::string& cache_info_filename,
                  std::unique_ptr<ExecUtils> exec_utils,
                  std::unique_ptr<OdrDexopt> odr_dexopt);

  // Returns the exit code, a list of ISAs that boot extensions should be compiled for, and a
  // boolean indicating whether the system server should be compiled.
  WARN_UNUSED ExitCode
  CheckArtifactsAreUpToDate(OdrMetrics& metrics,
                            /*out*/ CompilationOptions* compilation_options) const;

  WARN_UNUSED ExitCode Compile(OdrMetrics& metrics,
                               const CompilationOptions& compilation_options) const;

  WARN_UNUSED bool RemoveArtifactsDirectory() const;

  // Returns a set of all system server jars.
  std::set<std::string> AllSystemServerJars() const {
    return {all_systemserver_jars_.begin(), all_systemserver_jars_.end()};
  }

 private:
  time_t GetExecutionTimeUsed() const;

  time_t GetExecutionTimeRemaining() const;

  time_t GetSubprocessTimeout() const;

  // Gets the `ApexInfo` for active APEXes.
  std::optional<std::vector<com::android::apex::ApexInfo>> GetApexInfoList() const;

  // Reads the ART APEX cache information (if any) found in the output artifact directory.
  std::optional<com::android::art::CacheInfo> ReadCacheInfo() const;

  // Write ART APEX cache information to `kOnDeviceRefreshOdrefreshArtifactDirectory`.
  void WriteCacheInfo() const;

  std::vector<com::android::art::Component> GenerateBootClasspathComponents() const;

  std::vector<com::android::art::Component> GenerateBootExtensionCompilableComponents() const;

  std::vector<com::android::art::SystemServerComponent> GenerateSystemServerComponents() const;

  std::string GetBootImageExtensionImage(bool on_system) const;

  std::string GetBootImageExtensionImagePath(bool on_system, const InstructionSet isa) const;

  std::string GetSystemServerImagePath(bool on_system, const std::string& jar_path) const;

  // Removes files that are not in the list.
  android::base::Result<void> CleanupArtifactDirectory(
      const std::vector<std::string>& artifacts_to_keep) const;

  // Loads artifacts to memory and writes them back. This essentially removes the existing artifacts
  // from fs-verity so that odsign will not encounter "file exists" error when it adds the existing
  // artifacts to fs-verity.
  android::base::Result<void> RefreshExistingArtifacts() const;

  // Checks whether all boot extension artifacts are present. Returns true if all are present, false
  // otherwise.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  WARN_UNUSED bool BootExtensionArtifactsExist(
      bool on_system,
      const InstructionSet isa,
      /*out*/ std::string* error_msg,
      /*out*/ std::vector<std::string>* checked_artifacts = nullptr) const;

  // Checks whether all system_server artifacts are present. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  // Adds the paths to the jars that are missing artifacts in `jars_with_missing_artifacts`.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  WARN_UNUSED bool SystemServerArtifactsExist(
      bool on_system,
      /*out*/ std::string* error_msg,
      /*out*/ std::set<std::string>* jars_missing_artifacts,
      /*out*/ std::vector<std::string>* checked_artifacts = nullptr) const;

  // Checks whether all boot extension artifacts are up to date. Returns true if all are present,
  // false otherwise.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  WARN_UNUSED bool CheckBootExtensionArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const InstructionSet isa,
      const com::android::apex::ApexInfo& art_apex_info,
      const std::optional<com::android::art::CacheInfo>& cache_info,
      /*out*/ std::vector<std::string>* checked_artifacts) const;

  // Checks whether all system_server artifacts are up to date. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  // Adds the paths to the jars that needs to be compiled in `jars_to_compile`.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  bool CheckSystemServerArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const std::vector<com::android::apex::ApexInfo>& apex_info_list,
      const std::optional<com::android::art::CacheInfo>& cache_info,
      /*out*/ std::set<std::string>* jars_to_compile,
      /*out*/ std::vector<std::string>* checked_artifacts) const;

  WARN_UNUSED bool CompileBootExtensionArtifacts(const InstructionSet isa,
                                                 const std::string& staging_dir,
                                                 OdrMetrics& metrics,
                                                 const std::function<void()>& on_dex2oat_success,
                                                 std::string* error_msg) const;

  WARN_UNUSED bool CompileSystemServerArtifacts(
      const std::string& staging_dir,
      OdrMetrics& metrics,
      const std::set<std::string>& system_server_jars_to_compile,
      const std::function<void()>& on_dex2oat_success,
      std::string* error_msg) const;

  // Configuration to use.
  const OdrConfig& config_;

  // Path to cache information file that is used to speed up artifact checking.
  const std::string cache_info_filename_;

  // List of boot extension components that should be compiled.
  std::vector<std::string> boot_extension_compilable_jars_;

  // Set of system_server components in SYSTEMSERVERCLASSPATH that should be compiled.
  std::unordered_set<std::string> systemserver_classpath_jars_;

  // List of all boot classpath components. Used as the dependencies for compiling the
  // system_server.
  std::vector<std::string> boot_classpath_jars_;

  // List of all system_server components, including those in SYSTEMSERVERCLASSPATH and those in
  // STANDALONE_SYSTEMSERVER_JARS (jars that system_server loads dynamically using separate
  // classloaders).
  std::vector<std::string> all_systemserver_jars_;

  const time_t start_time_;

  std::unique_ptr<ExecUtils> exec_utils_;

  std::unique_ptr<OdrDexopt> odr_dexopt_;

  DISALLOW_COPY_AND_ASSIGN(OnDeviceRefresh);
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODREFRESH_H_
