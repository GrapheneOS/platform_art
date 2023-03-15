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
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

struct CompilationOptions {
  // If not empty, compile the bootclasspath jars for ISAs in the list.
  std::vector<InstructionSet> compile_boot_classpath_for_isas;

  // If not empty, compile the system server jars in the list.
  std::set<std::string> system_server_jars_to_compile;
};

class PreconditionCheckResult {
 public:
  static PreconditionCheckResult NoneOk(OdrMetrics::Trigger trigger) {
    return PreconditionCheckResult(trigger,
                                   /*boot_classpath_ok=*/false,
                                   /*system_server_ok=*/false);
  }
  static PreconditionCheckResult SystemServerNotOk(OdrMetrics::Trigger trigger) {
    return PreconditionCheckResult(trigger,
                                   /*boot_classpath_ok=*/true,
                                   /*system_server_ok=*/false);
  }
  static PreconditionCheckResult AllOk() {
    return PreconditionCheckResult(/*trigger=*/std::nullopt,
                                   /*boot_classpath_ok=*/true,
                                   /*system_server_ok=*/true);
  }
  bool IsAllOk() const { return !trigger_.has_value(); }
  OdrMetrics::Trigger GetTrigger() const { return trigger_.value(); }
  bool IsBootClasspathOk() const { return boot_classpath_ok_; }
  bool IsSystemServerOk() const { return system_server_ok_; }

 private:
  // Use static factory methods instead.
  PreconditionCheckResult(std::optional<OdrMetrics::Trigger> trigger,
                          bool boot_classpath_ok,
                          bool system_server_ok)
      : trigger_(trigger),
        boot_classpath_ok_(boot_classpath_ok),
        system_server_ok_(system_server_ok) {}

  // Indicates why the precondition is not okay, or `std::nullopt` if it's okay.
  std::optional<OdrMetrics::Trigger> trigger_;
  bool boot_classpath_ok_;
  bool system_server_ok_;
};

class OnDeviceRefresh final {
 public:
  explicit OnDeviceRefresh(const OdrConfig& config);

  // Constructor with injections. For testing and internal use only.
  OnDeviceRefresh(const OdrConfig& config,
                  const std::string& cache_info_filename,
                  std::unique_ptr<ExecUtils> exec_utils);

  // Returns the exit code and specifies what should be compiled in `compilation_options`.
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
  android::base::Result<com::android::art::CacheInfo> ReadCacheInfo() const;

  // Writes ART APEX cache information to `kOnDeviceRefreshOdrefreshArtifactDirectory`.
  android::base::Result<void> WriteCacheInfo() const;

  std::vector<com::android::art::Component> GenerateBootClasspathComponents() const;

  std::vector<com::android::art::Component> GenerateBootClasspathCompilableComponents() const;

  std::vector<com::android::art::SystemServerComponent> GenerateSystemServerComponents() const;

  // Returns the symbolic boot image location (without ISA). If `minimal` is true, returns the
  // symbolic location of the minimal boot image.
  std::string GetBootImage(bool on_system, bool minimal) const;

  // Returns the real boot image location (with ISA).  If `minimal` is true, returns the
  // symbolic location of the minimal boot image.
  std::string GetBootImagePath(bool on_system, bool minimal, const InstructionSet isa) const;

  // Returns the symbolic boot image extension location (without ISA). Note that this only applies
  // to boot images on /system.
  std::string GetSystemBootImageExtension() const;

  // Returns the real boot image location extension (with ISA). Note that this only applies to boot
  // images on /system.
  std::string GetSystemBootImageExtensionPath(const InstructionSet isa) const;

  std::string GetSystemServerImagePath(bool on_system, const std::string& jar_path) const;

  // Removes files that are not in the list.
  android::base::Result<void> CleanupArtifactDirectory(
      OdrMetrics& metrics, const std::vector<std::string>& artifacts_to_keep) const;

  // Loads artifacts to memory and writes them back. This is a workaround for old versions of
  // odsign, which encounters "file exists" error when it adds existing artifacts to fs-verity. This
  // function essentially removes existing artifacts from fs-verity to avoid the error.
  android::base::Result<void> RefreshExistingArtifacts() const;

  // Checks whether all boot classpath artifacts are present. Returns true if all are present, false
  // otherwise.
  // If `minimal` is true, checks the minimal boot image.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  WARN_UNUSED bool BootClasspathArtifactsExist(
      bool on_system,
      bool minimal,
      const InstructionSet isa,
      /*out*/ std::string* error_msg,
      /*out*/ std::vector<std::string>* checked_artifacts = nullptr) const;

  // Checks whether all system_server artifacts are present. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  // Adds the paths to the jars that are missing artifacts in `jars_with_missing_artifacts`.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  bool SystemServerArtifactsExist(
      bool on_system,
      /*out*/ std::string* error_msg,
      /*out*/ std::set<std::string>* jars_missing_artifacts,
      /*out*/ std::vector<std::string>* checked_artifacts = nullptr) const;

  // Returns true if all of the system properties listed in `kSystemProperties` are set to the
  // default values. This function is usually called when cache-info.xml does not exist (i.e.,
  // compilation has not been done before).
  WARN_UNUSED bool CheckSystemPropertiesAreDefault() const;

  // Returns true if none of the system properties listed in `kSystemProperties` has changed since
  // the last compilation. This function is usually called when cache-info.xml exists.
  WARN_UNUSED bool CheckSystemPropertiesHaveNotChanged(
      const com::android::art::CacheInfo& cache_info) const;

  // Returns true if the system image is built with the right userfaultfd GC flag.
  WARN_UNUSED bool CheckBuildUserfaultFdGc() const;

  // Returns whether the precondition for using artifacts on /system is met. Note that this function
  // does not check the artifacts.
  WARN_UNUSED PreconditionCheckResult
  CheckPreconditionForSystem(const std::vector<com::android::apex::ApexInfo>& apex_info_list) const;

  // Returns whether the precondition for using artifacts on /data is met. Note that this function
  // does not check the artifacts.
  WARN_UNUSED PreconditionCheckResult
  CheckPreconditionForData(const std::vector<com::android::apex::ApexInfo>& apex_info_list) const;

  // Checks whether all boot classpath artifacts are up to date. Returns true if all are present,
  // false otherwise.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  WARN_UNUSED bool CheckBootClasspathArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const InstructionSet isa,
      const PreconditionCheckResult& system_result,
      const PreconditionCheckResult& data_result,
      /*out*/ std::vector<std::string>* checked_artifacts) const;

  // Checks whether all system_server artifacts are up to date. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  // Adds the paths to the jars that needs to be compiled in `jars_to_compile`.
  // If `checked_artifacts` is present, adds checked artifacts to `checked_artifacts`.
  bool CheckSystemServerArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const PreconditionCheckResult& system_result,
      const PreconditionCheckResult& data_result,
      /*out*/ std::set<std::string>* jars_to_compile,
      /*out*/ std::vector<std::string>* checked_artifacts) const;

  // Compiles boot classpath. If `minimal` is true, only compiles the jars in the ART module.
  WARN_UNUSED bool CompileBootClasspathArtifacts(const InstructionSet isa,
                                                 const std::string& staging_dir,
                                                 OdrMetrics& metrics,
                                                 const std::function<void()>& on_dex2oat_success,
                                                 bool minimal,
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

  // List of boot classpath components that should be compiled.
  std::vector<std::string> boot_classpath_compilable_jars_;

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

  DISALLOW_COPY_AND_ASSIGN(OnDeviceRefresh);
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODREFRESH_H_
