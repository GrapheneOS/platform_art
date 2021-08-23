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

#ifndef ART_ODREFRESH_ODREFRESH_H_
#define ART_ODREFRESH_ODREFRESH_H_

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "com_android_apex.h"
#include "com_android_art.h"
#include "odr_artifacts.h"
#include "odr_config.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

class OnDeviceRefresh final {
 public:
  explicit OnDeviceRefresh(const OdrConfig& config);

  // Returns the exit code, a list of ISAs that boot extensions should be compiled for, and a
  // boolean indicating whether the system server should be compiled.
  WARN_UNUSED ExitCode
  CheckArtifactsAreUpToDate(OdrMetrics& metrics,
                            /*out*/ std::vector<InstructionSet>* compile_boot_extensions,
                            /*out*/ bool* compile_system_server) const;

  WARN_UNUSED ExitCode Compile(OdrMetrics& metrics,
                               const std::vector<InstructionSet>& compile_boot_extensions,
                               bool compile_system_server) const;

  // Verify all artifacts are up-to-date.
  //
  // This method checks artifacts can be loaded by the runtime.
  //
  // Returns ExitCode::kOkay if artifacts are up-to-date, ExitCode::kCompilationRequired
  // otherwise.
  //
  // NB This is the main function used by the --verify command-line option.
  WARN_UNUSED ExitCode VerifyArtifactsAreUpToDate() const;

  WARN_UNUSED bool RemoveArtifactsDirectory() const;

 private:
  time_t GetExecutionTimeUsed() const;

  time_t GetExecutionTimeRemaining() const;

  time_t GetSubprocessTimeout() const;

  // Gets the `ApexInfo` for active APEXes.
  std::optional<std::vector<com::android::apex::ApexInfo>> GetApexInfoList() const;

  // Reads the ART APEX cache information (if any) found in `kOdrefreshArtifactDirectory`.
  std::optional<com::android::art::CacheInfo> ReadCacheInfo() const;

  // Write ART APEX cache information to `kOnDeviceRefreshOdrefreshArtifactDirectory`.
  void WriteCacheInfo() const;

  void ReportNextBootAnimationProgress(uint32_t current_compilation) const;

  std::vector<com::android::art::Component> GenerateBootClasspathComponents() const;

  std::vector<com::android::art::Component> GenerateBootExtensionCompilableComponents() const;

  std::vector<com::android::art::Component> GenerateSystemServerComponents() const;

  std::string GetBootImageExtensionImage(bool on_system) const;

  std::string GetBootImageExtensionImagePath(bool on_system, const InstructionSet isa) const;

  std::string GetSystemServerImagePath(bool on_system, const std::string& jar_path) const;

  WARN_UNUSED bool RemoveSystemServerArtifactsFromData() const;

  // Remove boot extension artifacts from /data.
  WARN_UNUSED bool RemoveBootExtensionArtifactsFromData(InstructionSet isa) const;

  WARN_UNUSED bool RemoveArtifacts(const OdrArtifacts& artifacts) const;

  // Checks whether all boot extension artifacts are present. Returns true if all are present, false
  // otherwise.
  WARN_UNUSED bool BootExtensionArtifactsExist(bool on_system,
                                               const InstructionSet isa,
                                               /*out*/ std::string* error_msg) const;

  // Checks whether all system_server artifacts are present. The artifacts are checked in their
  // order of compilation. Returns true if all are present, false otherwise.
  WARN_UNUSED bool SystemServerArtifactsExist(bool on_system,
                                              /*out*/ std::string* error_msg) const;

  WARN_UNUSED bool CheckBootExtensionArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const InstructionSet isa,
      const com::android::apex::ApexInfo& art_apex_info,
      const std::optional<com::android::art::CacheInfo>& cache_info,
      /*out*/ bool* cleanup_required) const;

  WARN_UNUSED bool CheckSystemServerArtifactsAreUpToDate(
      OdrMetrics& metrics,
      const std::vector<com::android::apex::ApexInfo>& apex_info_list,
      const std::optional<com::android::art::CacheInfo>& cache_info,
      /*out*/ bool* cleanup_required) const;

  // Check the validity of boot class path extension artifacts.
  //
  // Returns true if artifacts exist and are valid according to dexoptanalyzer.
  WARN_UNUSED bool VerifyBootExtensionArtifactsAreUpToDate(const InstructionSet isa,
                                                           bool on_system) const;

  // Verify whether boot extension artifacts for `isa` are valid on system partition or in
  // apexdata. This method has the side-effect of removing boot classpath extension artifacts on
  // /data, if there are valid artifacts on /system, or if the artifacts on /data are not valid.
  // Returns true if valid boot externsion artifacts are valid.
  WARN_UNUSED bool VerifyBootExtensionArtifactsAreUpToDate(InstructionSet isa) const;

  WARN_UNUSED bool VerifySystemServerArtifactsAreUpToDate(bool on_system) const;

  // Verify the validity of system server artifacts on both /system and /data.
  // This method has the side-effect of removing system server artifacts on /data, if there are
  // valid artifacts on /system, or if the artifacts on /data are not valid.
  // Returns true if valid artifacts are found.
  WARN_UNUSED bool VerifySystemServerArtifactsAreUpToDate() const;

  WARN_UNUSED bool CompileBootExtensionArtifacts(const InstructionSet isa,
                                                 const std::string& staging_dir,
                                                 OdrMetrics& metrics,
                                                 uint32_t* dex2oat_invocation_count,
                                                 std::string* error_msg) const;

  WARN_UNUSED bool CompileSystemServerArtifacts(const std::string& staging_dir,
                                                OdrMetrics& metrics,
                                                uint32_t* dex2oat_invocation_count,
                                                std::string* error_msg) const;

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

  DISALLOW_COPY_AND_ASSIGN(OnDeviceRefresh);
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODREFRESH_H_
