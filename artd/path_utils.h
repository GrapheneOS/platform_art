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

#ifndef ART_ARTD_PATH_UTILS_H_
#define ART_ARTD_PATH_UTILS_H_

#include <string>
#include <type_traits>
#include <vector>

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/logging.h"
#include "android-base/result.h"
#include "base/macros.h"

namespace art {
namespace artd {

struct RawArtifactsPath {
  std::string oat_path;
  std::string vdex_path;
  std::string art_path;
};

android::base::Result<std::string> GetAndroidDataOrError();

android::base::Result<std::string> GetAndroidExpandOrError();

android::base::Result<std::string> GetArtRootOrError();

// Returns all existing files that are managed by artd.
std::vector<std::string> ListManagedFiles(const std::string& android_data,
                                          const std::string& android_expand);

std::vector<std::string> ListRuntimeArtifactsFiles(
    const std::string& android_data,
    const std::string& android_expand,
    const aidl::com::android::server::art::RuntimeArtifactsPath& runtime_artifacts_path);

android::base::Result<void> ValidateRuntimeArtifactsPath(
    const aidl::com::android::server::art::RuntimeArtifactsPath& runtime_artifacts_path);

android::base::Result<std::string> BuildArtBinPath(const std::string& binary_name);

android::base::Result<std::string> BuildOatPath(const std::string& dex_path,
                                                const std::string& isa_str,
                                                bool is_in_dalvik_cache);

// Returns the absolute paths to files built from the `ArtifactsPath`.
android::base::Result<RawArtifactsPath> BuildArtifactsPath(
    const aidl::com::android::server::art::ArtifactsPath& artifacts_path);

android::base::Result<std::string> BuildPrimaryRefProfilePath(
    const aidl::com::android::server::art::ProfilePath::PrimaryRefProfilePath&
        primary_ref_profile_path);

android::base::Result<std::string> BuildPrebuiltProfilePath(
    const aidl::com::android::server::art::ProfilePath::PrebuiltProfilePath& prebuilt_profile_path);

android::base::Result<std::string> BuildPrimaryCurProfilePath(
    const aidl::com::android::server::art::ProfilePath::PrimaryCurProfilePath&
        primary_cur_profile_path);

android::base::Result<std::string> BuildSecondaryRefProfilePath(
    const aidl::com::android::server::art::ProfilePath::SecondaryRefProfilePath&
        secondary_ref_profile_path);

android::base::Result<std::string> BuildSecondaryCurProfilePath(
    const aidl::com::android::server::art::ProfilePath::SecondaryCurProfilePath&
        secondary_cur_profile_path);

android::base::Result<std::string> BuildWritableProfilePath(
    const aidl::com::android::server::art::ProfilePath::WritableProfilePath& profile_path);

android::base::Result<std::string> BuildFinalProfilePath(
    const aidl::com::android::server::art::ProfilePath::TmpProfilePath& tmp_profile_path);

android::base::Result<std::string> BuildTmpProfilePath(
    const aidl::com::android::server::art::ProfilePath::TmpProfilePath& tmp_profile_path);

android::base::Result<std::string> BuildDexMetadataPath(
    const aidl::com::android::server::art::DexMetadataPath& dex_metadata_path);

android::base::Result<std::string> BuildProfileOrDmPath(
    const aidl::com::android::server::art::ProfilePath& profile_path);

android::base::Result<std::string> BuildVdexPath(
    const aidl::com::android::server::art::VdexPath& vdex_path);

android::base::Result<std::string> BuildSdmPath(
    const aidl::com::android::server::art::SecureDexMetadataWithCompanionPaths& sdm_path);

android::base::Result<std::string> BuildSdcPath(
    const aidl::com::android::server::art::SecureDexMetadataWithCompanionPaths& sdc_path);

// Takes an argument of type `WritableProfilePath`. Returns the pre-reboot flag by value if the
// argument is const, or by reference otherwise.
template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<std::remove_cv_t<T>,
                             aidl::com::android::server::art::ProfilePath::WritableProfilePath>>>
std::conditional_t<std::is_const_v<T>, bool, bool&> PreRebootFlag(T& profile_path) {
  switch (profile_path.getTag()) {
    case T::forPrimary:
      return profile_path.template get<T::forPrimary>().isPreReboot;
    case T::forSecondary:
      return profile_path.template get<T::forSecondary>().isPreReboot;
      // No default. All cases should be explicitly handled, or the compilation will fail.
  }
  // This should never happen. Just in case we get a non-enumerator value.
  LOG(FATAL) << ART_FORMAT("Unexpected writable profile path type {}",
                           fmt::underlying(profile_path.getTag()));
}

template bool PreRebootFlag(
    const aidl::com::android::server::art::ProfilePath::WritableProfilePath& profile_path);
template bool& PreRebootFlag(
    aidl::com::android::server::art::ProfilePath::WritableProfilePath& profile_path);

bool PreRebootFlag(const aidl::com::android::server::art::ProfilePath& profile_path);
bool PreRebootFlag(
    const aidl::com::android::server::art::ProfilePath::TmpProfilePath& tmp_profile_path);
bool PreRebootFlag(const aidl::com::android::server::art::OutputProfile& profile);
bool PreRebootFlag(const aidl::com::android::server::art::ArtifactsPath& artifacts_path);
bool PreRebootFlag(const aidl::com::android::server::art::OutputArtifacts& artifacts);
bool PreRebootFlag(const aidl::com::android::server::art::VdexPath& vdex_path);

bool IsPreRebootStagedFile(std::string_view filename);

// Sets the root dir for `ListManagedFiles` and `ListRuntimeImageFiles`.
// The passed string must be alive until the test ends.
// For testing use only.
void TestOnlySetListRootDir(std::string_view root_dir);

}  // namespace artd
}  // namespace art

#endif  // ART_ARTD_PATH_UTILS_H_
