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

#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/errors.h"
#include "android-base/logging.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "android/binder_auto_utils.h"
#include "android/binder_manager.h"
#include "android/binder_process.h"
#include "base/array_ref.h"
#include "base/file_utils.h"
#include "oat_file_assistant.h"
#include "runtime.h"
#include "tools/tools.h"

namespace art {
namespace artd {

namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::aidl::com::android::server::art::BnArtd;
using ::aidl::com::android::server::art::GetOptimizationStatusResult;
using ::android::base::Error;
using ::android::base::GetBoolProperty;
using ::android::base::Result;
using ::android::base::Split;
using ::ndk::ScopedAStatus;

constexpr const char* kPhenotypeFlagPrefix = "persist.device_config.runtime_native_boot.";
constexpr const char* kDalvikVmFlagPrefix = "dalvik.vm.";

Result<std::vector<std::string>> GetBootClassPath() {
  const char* env_value = getenv("BOOTCLASSPATH");
  if (env_value == nullptr || strlen(env_value) == 0) {
    return Errorf("Failed to get environment variable 'BOOTCLASSPATH'");
  }
  return Split(env_value, ":");
}

Result<std::vector<std::string>> GetBootImageLocations(bool deny_art_apex_data_files) {
  std::string error_msg;
  std::string android_root = GetAndroidRootSafe(&error_msg);
  if (!error_msg.empty()) {
    return Errorf("Failed to get ANDROID_ROOT: {}", error_msg);
  }

  std::string location_str = GetDefaultBootImageLocation(android_root, deny_art_apex_data_files);
  return Split(location_str, ":");
}

bool UseJitZygote() {
  bool profile_boot_class_path_phenotype =
      GetBoolProperty(std::string(kPhenotypeFlagPrefix) + "profilebootclasspath",
                      /*default_value=*/false);

  bool profile_boot_class_path =
      GetBoolProperty(std::string(kDalvikVmFlagPrefix) + "profilebootclasspath",
                      /*default_value=*/profile_boot_class_path_phenotype);

  return profile_boot_class_path;
}

bool DenyArtApexDataFiles() {
  return !GetBoolProperty("odsign.verification.success", /*default_value=*/false);
}

}  // namespace

class Artd : public BnArtd {
  constexpr static const char* kServiceName = "artd";

 public:
  ScopedAStatus isAlive(bool* _aidl_return) override {
    *_aidl_return = true;
    return ScopedAStatus::ok();
  }

  ScopedAStatus deleteArtifacts(const ArtifactsPath& in_artifactsPath,
                                int64_t* _aidl_return) override {
    (void)in_artifactsPath;
    (void)_aidl_return;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }

  ScopedAStatus getOptimizationStatus(const std::string& in_dexFile,
                                      const std::string& in_instructionSet,
                                      const std::string& in_classLoaderContext,
                                      GetOptimizationStatusResult* _aidl_return) override {
    Result<OatFileAssistant::RuntimeOptions> runtime_options = GetRuntimeOptions();
    if (!runtime_options.ok()) {
      return ScopedAStatus::fromExceptionCodeWithMessage(
          EX_ILLEGAL_STATE,
          ("Failed to get runtime options: " + runtime_options.error().message()).c_str());
    }

    std::string error_msg;
    if (!OatFileAssistant::GetOptimizationStatus(
            in_dexFile.c_str(),
            in_instructionSet.c_str(),
            in_classLoaderContext.c_str(),
            std::make_unique<OatFileAssistant::RuntimeOptions>(std::move(*runtime_options)),
            &_aidl_return->compilerFilter,
            &_aidl_return->compilationReason,
            &_aidl_return->locationDebugString,
            &error_msg)) {
      return ScopedAStatus::fromExceptionCodeWithMessage(
          EX_ILLEGAL_STATE, ("Failed to get optimization status: " + error_msg).c_str());
    }

    return ScopedAStatus::ok();
  }

  Result<void> Start() {
    LOG(INFO) << "Starting artd";

    ScopedAStatus status = ScopedAStatus::fromStatus(
        AServiceManager_registerLazyService(this->asBinder().get(), kServiceName));
    if (!status.isOk()) {
      return Error() << status.getDescription();
    }

    ABinderProcess_startThreadPool();

    return {};
  }

 private:
  Result<OatFileAssistant::RuntimeOptions> GetRuntimeOptions() {
    // We don't cache this system property because it can change.
    bool use_jit_zygote = UseJitZygote();

    if (!HasRuntimeOptionsCache()) {
      OR_RETURN(BuildRuntimeOptionsCache());
    }

    return OatFileAssistant::RuntimeOptions{
        .image_locations = cached_boot_image_locations_,
        .boot_class_path = cached_boot_class_path_,
        .boot_class_path_locations = cached_boot_class_path_,
        .use_jit_zygote = use_jit_zygote,
        .deny_art_apex_data_files = cached_deny_art_apex_data_files_,
        .apex_versions = cached_apex_versions_,
    };
  }

  Result<void> BuildRuntimeOptionsCache() {
    // This system property can only be set by odsign on boot, so it won't change.
    bool deny_art_apex_data_files = DenyArtApexDataFiles();

    std::vector<std::string> image_locations =
        OR_RETURN(GetBootImageLocations(deny_art_apex_data_files));
    std::vector<std::string> boot_class_path = OR_RETURN(GetBootClassPath());
    std::string apex_versions =
        Runtime::GetApexVersions(ArrayRef<const std::string>(boot_class_path));

    cached_boot_image_locations_ = std::move(image_locations);
    cached_boot_class_path_ = std::move(boot_class_path);
    cached_apex_versions_ = std::move(apex_versions);
    cached_deny_art_apex_data_files_ = deny_art_apex_data_files;

    return {};
  }

  bool HasRuntimeOptionsCache() {
    return !cached_boot_image_locations_.empty();
  }

  std::vector<std::string> cached_boot_image_locations_;
  std::vector<std::string> cached_boot_class_path_;
  std::string cached_apex_versions_;
  bool cached_deny_art_apex_data_files_;
};

}  // namespace artd
}  // namespace art

int main(const int argc __attribute__((unused)), char* argv[]) {
  setenv("ANDROID_LOG_TAGS", "*:v", 1);
  android::base::InitLogging(argv);

  art::artd::Artd artd;

  if (auto ret = artd.Start(); !ret.ok()) {
    LOG(ERROR) << "Unable to start artd: " << ret.error();
    exit(1);
  }

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "artd shutting down";

  return 0;
}
