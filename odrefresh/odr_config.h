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

#ifndef ART_ODREFRESH_ODR_CONFIG_H_
#define ART_ODREFRESH_ODR_CONFIG_H_

#include <optional>
#include <string>
#include <vector>

#include "android-base/file.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "log/log.h"
#include "odr_common.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

// An enumeration of the possible zygote configurations on Android.
enum class ZygoteKind : uint8_t {
  // 32-bit primary zygote, no secondary zygote.
  kZygote32 = 0,
  // 32-bit primary zygote, 64-bit secondary zygote.
  kZygote32_64 = 1,
  // 64-bit primary zygote, 32-bit secondary zygote.
  kZygote64_32 = 2,
  // 64-bit primary zygote, no secondary zygote.
  kZygote64 = 3
};

// Configuration class for odrefresh. Exists to enable abstracting environment variables and
// system properties into a configuration class for development and testing purposes.
class OdrConfig final {
 private:
  std::string apex_info_list_file_;
  std::string art_bin_dir_;
  std::string dex2oat_;
  std::string dex2oat_boot_classpath_;
  bool dry_run_;
  std::optional<bool> refresh_;
  std::optional<bool> partial_compilation_;
  InstructionSet isa_;
  std::string program_name_;
  std::string system_server_classpath_;
  std::string system_server_compiler_filter_;
  ZygoteKind zygote_kind_;
  std::string boot_classpath_;
  std::string artifact_dir_;
  std::string standalone_system_server_jars_;
  bool compilation_os_mode_ = false;
  bool minimal_ = false;

  // Staging directory for artifacts. The directory must exist and will be automatically removed
  // after compilation. If empty, use the default directory.
  std::string staging_dir_;

 public:
  explicit OdrConfig(const char* program_name)
    : dry_run_(false),
      isa_(InstructionSet::kNone),
      program_name_(android::base::Basename(program_name)),
      artifact_dir_(GetApexDataDalvikCacheDirectory(InstructionSet::kNone)) {
  }

  const std::string& GetApexInfoListFile() const { return apex_info_list_file_; }

  std::vector<InstructionSet> GetBootClasspathIsas() const {
    const auto [isa32, isa64] = GetPotentialInstructionSets();
    switch (zygote_kind_) {
      case ZygoteKind::kZygote32:
        return {isa32};
      case ZygoteKind::kZygote32_64:
      case ZygoteKind::kZygote64_32:
        return {isa32, isa64};
      case ZygoteKind::kZygote64:
        return {isa64};
    }
  }

  InstructionSet GetSystemServerIsa() const {
    const auto [isa32, isa64] = GetPotentialInstructionSets();
    switch (zygote_kind_) {
      case ZygoteKind::kZygote32:
      case ZygoteKind::kZygote32_64:
        return isa32;
      case ZygoteKind::kZygote64_32:
      case ZygoteKind::kZygote64:
        return isa64;
    }
  }

  const std::string& GetDex2oatBootClasspath() const { return dex2oat_boot_classpath_; }

  const std::string& GetArtifactDirectory() const { return artifact_dir_; }

  std::string GetDex2Oat() const {
    const char* prefix = UseDebugBinaries() ? "dex2oatd" : "dex2oat";
    const char* suffix = "";
    if (kIsTargetBuild) {
      switch (zygote_kind_) {
        case ZygoteKind::kZygote32:
          suffix = "32";
          break;
        case ZygoteKind::kZygote32_64:
        case ZygoteKind::kZygote64_32:
        case ZygoteKind::kZygote64:
          suffix = "64";
          break;
      }
    }
    return art_bin_dir_ + '/' + prefix + suffix;
  }

  bool GetDryRun() const { return dry_run_; }
  bool HasPartialCompilation() const {
    return partial_compilation_.has_value();
  }
  bool GetPartialCompilation() const {
    return partial_compilation_.value_or(true);
  }
  bool GetRefresh() const {
    return refresh_.value_or(true);
  }
  const std::string& GetSystemServerClasspath() const {
    return system_server_classpath_;
  }
  const std::string& GetSystemServerCompilerFilter() const {
    return system_server_compiler_filter_;
  }
  const std::string& GetStagingDir() const {
    return staging_dir_;
  }
  bool GetCompilationOsMode() const { return compilation_os_mode_; }
  bool GetMinimal() const { return minimal_; }

  void SetApexInfoListFile(const std::string& file_path) { apex_info_list_file_ = file_path; }
  void SetArtBinDir(const std::string& art_bin_dir) { art_bin_dir_ = art_bin_dir; }

  void SetDex2oatBootclasspath(const std::string& classpath) {
    dex2oat_boot_classpath_ = classpath;
  }

  void SetArtifactDirectory(const std::string& artifact_dir) {
    artifact_dir_ = artifact_dir;
  }

  void SetDryRun() { dry_run_ = true; }
  void SetPartialCompilation(bool value) {
    partial_compilation_ = value;
  }
  void SetRefresh(bool value) {
    refresh_ = value;
  }
  void SetIsa(const InstructionSet isa) { isa_ = isa; }

  void SetSystemServerClasspath(const std::string& classpath) {
    system_server_classpath_ = classpath;
  }

  void SetSystemServerCompilerFilter(const std::string& filter) {
    system_server_compiler_filter_ = filter;
  }

  void SetZygoteKind(ZygoteKind zygote_kind) { zygote_kind_ = zygote_kind; }

  const std::string& GetBootClasspath() const { return boot_classpath_; }

  void SetBootClasspath(const std::string& classpath) { boot_classpath_ = classpath; }

  void SetStagingDir(const std::string& staging_dir) {
    staging_dir_ = staging_dir;
  }

  const std::string& GetStandaloneSystemServerJars() const {
    return standalone_system_server_jars_;
  }

  void SetStandaloneSystemServerJars(const std::string& jars) {
    standalone_system_server_jars_ = jars;
  }

  void SetCompilationOsMode(bool value) { compilation_os_mode_ = value; }

  void SetMinimal(bool value) { minimal_ = value; }

 private:
  // Returns a pair for the possible instruction sets for the configured instruction set
  // architecture. The first item is the 32-bit architecture and the second item is the 64-bit
  // architecture. The current `isa` is based on `kRuntimeISA` on target, odrefresh is compiled
  // 32-bit by default so this method returns all options which are finessed based on the
  // `ro.zygote` property.
  std::pair<InstructionSet, InstructionSet> GetPotentialInstructionSets() const {
    switch (isa_) {
      case art::InstructionSet::kArm:
      case art::InstructionSet::kArm64:
        return std::make_pair(art::InstructionSet::kArm, art::InstructionSet::kArm64);
      case art::InstructionSet::kX86:
      case art::InstructionSet::kX86_64:
        return std::make_pair(art::InstructionSet::kX86, art::InstructionSet::kX86_64);
      case art::InstructionSet::kThumb2:
      case art::InstructionSet::kNone:
        LOG(FATAL) << "Invalid instruction set " << isa_;
        return std::make_pair(art::InstructionSet::kNone, art::InstructionSet::kNone);
    }
  }

  bool UseDebugBinaries() const { return program_name_ == "odrefreshd"; }

  OdrConfig() = delete;
  OdrConfig(const OdrConfig&) = delete;
  OdrConfig& operator=(const OdrConfig&) = delete;
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_CONFIG_H_
