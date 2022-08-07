/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ART_ARTD_ARTD_H_
#define ART_ARTD_ARTD_H_

#include <memory>
#include <string>
#include <vector>

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/result.h"
#include "android/binder_auto_utils.h"
#include "oat_file_assistant_context.h"
#include "tools/system_properties.h"

namespace art {
namespace artd {

class Artd : public aidl::com::android::server::art::BnArtd {
 public:
  explicit Artd(std::unique_ptr<art::tools::SystemProperties> props =
                    std::make_unique<art::tools::SystemProperties>())
      : props_(std::move(props)) {}

  ndk::ScopedAStatus isAlive(bool* _aidl_return) override;

  ndk::ScopedAStatus deleteArtifacts(
      const aidl::com::android::server::art::ArtifactsPath& in_artifactsPath,
      int64_t* _aidl_return) override;

  ndk::ScopedAStatus getOptimizationStatus(
      const std::string& in_dexFile,
      const std::string& in_instructionSet,
      const std::string& in_classLoaderContext,
      aidl::com::android::server::art::GetOptimizationStatusResult* _aidl_return) override;

  android::base::Result<void> Start();

 private:
  android::base::Result<OatFileAssistantContext*> GetOatFileAssistantContext();

  android::base::Result<const std::vector<std::string>*> GetBootImageLocations();

  android::base::Result<const std::vector<std::string>*> GetBootClassPath();

  bool UseJitZygote();

  bool DenyArtApexDataFiles();

  std::optional<std::vector<std::string>> cached_boot_image_locations_;
  std::optional<std::vector<std::string>> cached_boot_class_path_;
  std::optional<std::string> cached_apex_versions_;
  std::optional<bool> cached_use_jit_zygote_;
  std::optional<bool> cached_deny_art_apex_data_files_;

  std::unique_ptr<OatFileAssistantContext> ofa_context_;

  std::unique_ptr<art::tools::SystemProperties> props_;
};

}  // namespace artd
}  // namespace art

#endif  // ART_ARTD_ARTD_H_
