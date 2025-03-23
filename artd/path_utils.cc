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

#include "path_utils.h"

#include <filesystem>
#include <string>
#include <vector>

#include "aidl/com/android/server/art/ArtConstants.h"
#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/errors.h"
#include "android-base/result.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "file_utils.h"
#include "oat/oat_file_assistant.h"
#include "runtime_image.h"
#include "service.h"
#include "tools/tools.h"

namespace art {
namespace artd {

namespace {

using ::aidl::com::android::server::art::ArtConstants;
using ::aidl::com::android::server::art::ArtifactsPath;
using ::aidl::com::android::server::art::DexMetadataPath;
using ::aidl::com::android::server::art::OutputArtifacts;
using ::aidl::com::android::server::art::OutputProfile;
using ::aidl::com::android::server::art::ProfilePath;
using ::aidl::com::android::server::art::RuntimeArtifactsPath;
using ::aidl::com::android::server::art::SecureDexMetadataWithCompanionPaths;
using ::aidl::com::android::server::art::VdexPath;
using ::android::base::Error;
using ::android::base::Result;
using ::art::service::ValidateDexPath;
using ::art::service::ValidatePathElement;
using ::art::service::ValidatePathElementSubstring;

using PrebuiltProfilePath = ProfilePath::PrebuiltProfilePath;
using PrimaryCurProfilePath = ProfilePath::PrimaryCurProfilePath;
using PrimaryRefProfilePath = ProfilePath::PrimaryRefProfilePath;
using SecondaryCurProfilePath = ProfilePath::SecondaryCurProfilePath;
using SecondaryRefProfilePath = ProfilePath::SecondaryRefProfilePath;
using TmpProfilePath = ProfilePath::TmpProfilePath;
using WritableProfilePath = ProfilePath::WritableProfilePath;

constexpr const char* kPreRebootSuffix = ".staged";

// Only to be changed for testing.
std::string_view gListRootDir = "/";

}  // namespace

Result<std::string> GetAndroidDataOrError() {
  std::string error_msg;
  std::string result = GetAndroidDataSafe(&error_msg);
  if (!error_msg.empty()) {
    return Error() << error_msg;
  }
  return result;
}

Result<std::string> GetAndroidExpandOrError() {
  std::string error_msg;
  std::string result = GetAndroidExpandSafe(&error_msg);
  if (!error_msg.empty()) {
    return Error() << error_msg;
  }
  return result;
}

Result<std::string> GetArtRootOrError() {
  std::string error_msg;
  std::string result = GetArtRootSafe(&error_msg);
  if (!error_msg.empty()) {
    return Error() << error_msg;
  }
  return result;
}

std::vector<std::string> ListManagedFiles(const std::string& android_data,
                                          const std::string& android_expand) {
  // See `art::tools::Glob` for the syntax.
  std::vector<std::string> patterns = {
      // Profiles for primary dex files.
      android_data + "/misc/profiles/**",
      // Artifacts for primary dex files.
      android_data + "/dalvik-cache/**",
  };

  for (const std::string& data_root : {android_data, android_expand + "/*"}) {
    // Artifacts for primary dex files.
    patterns.push_back(data_root + "/app/*/*/oat/**");
    patterns.push_back(data_root + "/app/*/*/*.sdm");

    for (const char* user_dir : {"/user", "/user_de"}) {
      std::string data_dir = data_root + user_dir + "/*/*";
      // Profiles and artifacts for secondary dex files. Those files are in app data directories, so
      // we use more granular patterns to avoid accidentally deleting apps' files.
      std::string secondary_oat_dir = data_dir + "/**/oat";
      for (const char* suffix : {"", ".*.tmp", kPreRebootSuffix}) {
        patterns.push_back(secondary_oat_dir + "/*" + ArtConstants::PROFILE_FILE_EXT + suffix);
        patterns.push_back(secondary_oat_dir + "/*/*" + kOdexExtension + suffix);
        patterns.push_back(secondary_oat_dir + "/*/*" + kVdexExtension + suffix);
        patterns.push_back(secondary_oat_dir + "/*/*" + kArtExtension + suffix);
      }
      // Runtime image files.
      patterns.push_back(RuntimeImage::GetRuntimeImageDir(data_dir) + "**");
    }
  }

  return tools::Glob(patterns, gListRootDir);
}

std::vector<std::string> ListRuntimeArtifactsFiles(
    const std::string& android_data,
    const std::string& android_expand,
    const RuntimeArtifactsPath& runtime_artifacts_path) {
  // See `art::tools::Glob` for the syntax.
  std::vector<std::string> patterns;

  for (const std::string& data_root : {android_data, android_expand + "/*"}) {
    for (const char* user_dir : {"/user", "/user_de"}) {
      std::string data_dir =
          data_root + user_dir + "/*/" + tools::EscapeGlob(runtime_artifacts_path.packageName);
      patterns.push_back(
          RuntimeImage::GetRuntimeImagePath(data_dir,
                                            tools::EscapeGlob(runtime_artifacts_path.dexPath),
                                            tools::EscapeGlob(runtime_artifacts_path.isa)));
    }
  }

  return tools::Glob(patterns, gListRootDir);
}

static Result<InstructionSet> ValidateAndGetIsa(const std::string& isa_str) {
  InstructionSet isa = GetInstructionSetFromString(isa_str.c_str());
  if (isa == InstructionSet::kNone) {
    return Errorf("Instruction set '{}' is invalid", isa_str);
  }
  return isa;
}

Result<void> ValidateRuntimeArtifactsPath(const RuntimeArtifactsPath& runtime_artifacts_path) {
  OR_RETURN(ValidatePathElement(runtime_artifacts_path.packageName, "packageName"));
  OR_RETURN(ValidateAndGetIsa(runtime_artifacts_path.isa));
  OR_RETURN(ValidateDexPath(runtime_artifacts_path.dexPath));
  return {};
}

Result<std::string> BuildArtBinPath(const std::string& binary_name) {
  return ART_FORMAT("{}/bin/{}", OR_RETURN(GetArtRootOrError()), binary_name);
}

Result<std::string> BuildOatPath(const std::string& dex_path,
                                 const std::string& isa_str,
                                 bool is_in_dalvik_cache) {
  OR_RETURN(ValidateDexPath(dex_path));
  InstructionSet isa = OR_RETURN(ValidateAndGetIsa(isa_str));

  std::string oat_path;
  std::string error_msg;
  if (is_in_dalvik_cache) {
    // Apps' OAT files are never in ART APEX data.
    if (!OatFileAssistant::DexLocationToOatFilename(dex_path,
                                                    isa,
                                                    /*deny_art_apex_data_files=*/true,
                                                    &oat_path,
                                                    &error_msg)) {
      return Errorf("{}", error_msg);
    }
  } else {
    if (!OatFileAssistant::DexLocationToOdexFilename(dex_path, isa, &oat_path, &error_msg)) {
      return Errorf("{}", error_msg);
    }
  }

  return oat_path;
}

Result<RawArtifactsPath> BuildArtifactsPath(const ArtifactsPath& artifacts_path) {
  RawArtifactsPath path;
  path.oat_path = OR_RETURN(
      BuildOatPath(artifacts_path.dexPath, artifacts_path.isa, artifacts_path.isInDalvikCache));
  path.vdex_path = ReplaceFileExtension(path.oat_path, kVdexExtension);
  path.art_path = ReplaceFileExtension(path.oat_path, kArtExtension);

  if (artifacts_path.isPreReboot) {
    path.oat_path += kPreRebootSuffix;
    path.vdex_path += kPreRebootSuffix;
    path.art_path += kPreRebootSuffix;
  }

  return path;
}

Result<std::string> BuildPrimaryRefProfilePath(
    const PrimaryRefProfilePath& primary_ref_profile_path) {
  OR_RETURN(ValidatePathElement(primary_ref_profile_path.packageName, "packageName"));
  OR_RETURN(ValidatePathElementSubstring(primary_ref_profile_path.profileName, "profileName"));
  return ART_FORMAT("{}/misc/profiles/ref/{}/{}{}{}",
                    OR_RETURN(GetAndroidDataOrError()),
                    primary_ref_profile_path.packageName,
                    primary_ref_profile_path.profileName,
                    ArtConstants::PROFILE_FILE_EXT,
                    primary_ref_profile_path.isPreReboot ? kPreRebootSuffix : "");
}

Result<std::string> BuildPrebuiltProfilePath(const PrebuiltProfilePath& prebuilt_profile_path) {
  OR_RETURN(ValidateDexPath(prebuilt_profile_path.dexPath));
  return prebuilt_profile_path.dexPath + ArtConstants::PROFILE_FILE_EXT;
}

Result<std::string> BuildPrimaryCurProfilePath(
    const PrimaryCurProfilePath& primary_cur_profile_path) {
  OR_RETURN(ValidatePathElement(primary_cur_profile_path.packageName, "packageName"));
  OR_RETURN(ValidatePathElementSubstring(primary_cur_profile_path.profileName, "profileName"));
  return ART_FORMAT("{}/misc/profiles/cur/{}/{}/{}{}",
                    OR_RETURN(GetAndroidDataOrError()),
                    primary_cur_profile_path.userId,
                    primary_cur_profile_path.packageName,
                    primary_cur_profile_path.profileName,
                    ArtConstants::PROFILE_FILE_EXT);
}

Result<std::string> BuildSecondaryRefProfilePath(
    const SecondaryRefProfilePath& secondary_ref_profile_path) {
  OR_RETURN(ValidateDexPath(secondary_ref_profile_path.dexPath));
  std::filesystem::path dex_path(secondary_ref_profile_path.dexPath);
  return ART_FORMAT("{}/oat/{}{}{}",
                    dex_path.parent_path().string(),
                    dex_path.filename().string(),
                    ArtConstants::PROFILE_FILE_EXT,
                    secondary_ref_profile_path.isPreReboot ? kPreRebootSuffix : "");
}

Result<std::string> BuildSecondaryCurProfilePath(
    const SecondaryCurProfilePath& secondary_cur_profile_path) {
  OR_RETURN(ValidateDexPath(secondary_cur_profile_path.dexPath));
  std::filesystem::path dex_path(secondary_cur_profile_path.dexPath);
  return ART_FORMAT("{}/oat/{}.cur{}",
                    dex_path.parent_path().string(),
                    dex_path.filename().string(),
                    ArtConstants::PROFILE_FILE_EXT);
}

Result<std::string> BuildWritableProfilePath(const WritableProfilePath& profile_path) {
  switch (profile_path.getTag()) {
    case WritableProfilePath::forPrimary:
      return BuildPrimaryRefProfilePath(profile_path.get<WritableProfilePath::forPrimary>());
    case WritableProfilePath::forSecondary:
      return BuildSecondaryRefProfilePath(profile_path.get<WritableProfilePath::forSecondary>());
      // No default. All cases should be explicitly handled, or the compilation will fail.
  }
  // This should never happen. Just in case we get a non-enumerator value.
  LOG(FATAL) << ART_FORMAT("Unexpected writable profile path type {}",
                           fmt::underlying(profile_path.getTag()));
}

Result<std::string> BuildFinalProfilePath(const TmpProfilePath& tmp_profile_path) {
  return BuildWritableProfilePath(tmp_profile_path.finalPath);
}

Result<std::string> BuildTmpProfilePath(const TmpProfilePath& tmp_profile_path) {
  OR_RETURN(ValidatePathElementSubstring(tmp_profile_path.id, "id"));
  return NewFile::BuildTempPath(OR_RETURN(BuildFinalProfilePath(tmp_profile_path)),
                                tmp_profile_path.id);
}

Result<std::string> BuildDexMetadataPath(const DexMetadataPath& dex_metadata_path) {
  OR_RETURN(ValidateDexPath(dex_metadata_path.dexPath));
  return ReplaceFileExtension(dex_metadata_path.dexPath, kDmExtension);
}

Result<std::string> BuildProfileOrDmPath(const ProfilePath& profile_path) {
  switch (profile_path.getTag()) {
    case ProfilePath::primaryRefProfilePath:
      return BuildPrimaryRefProfilePath(profile_path.get<ProfilePath::primaryRefProfilePath>());
    case ProfilePath::prebuiltProfilePath:
      return BuildPrebuiltProfilePath(profile_path.get<ProfilePath::prebuiltProfilePath>());
    case ProfilePath::primaryCurProfilePath:
      return BuildPrimaryCurProfilePath(profile_path.get<ProfilePath::primaryCurProfilePath>());
    case ProfilePath::secondaryRefProfilePath:
      return BuildSecondaryRefProfilePath(profile_path.get<ProfilePath::secondaryRefProfilePath>());
    case ProfilePath::secondaryCurProfilePath:
      return BuildSecondaryCurProfilePath(profile_path.get<ProfilePath::secondaryCurProfilePath>());
    case ProfilePath::tmpProfilePath:
      return BuildTmpProfilePath(profile_path.get<ProfilePath::tmpProfilePath>());
    case ProfilePath::dexMetadataPath:
      return BuildDexMetadataPath(profile_path.get<ProfilePath::dexMetadataPath>());
      // No default. All cases should be explicitly handled, or the compilation will fail.
  }
  // This should never happen. Just in case we get a non-enumerator value.
  LOG(FATAL) << ART_FORMAT("Unexpected profile path type {}",
                           fmt::underlying(profile_path.getTag()));
}

Result<std::string> BuildVdexPath(const VdexPath& vdex_path) {
  DCHECK(vdex_path.getTag() == VdexPath::artifactsPath);
  return OR_RETURN(BuildArtifactsPath(vdex_path.get<VdexPath::artifactsPath>())).vdex_path;
}

Result<std::string> BuildSdmPath(const SecureDexMetadataWithCompanionPaths& sdm_path) {
  // `sdm_path.isInDalvikCache` is intentionally ignored because it's only applicable to SDC files.
  OR_RETURN(ValidateDexPath(sdm_path.dexPath));
  OR_RETURN(ValidateAndGetIsa(sdm_path.isa));
  return ReplaceFileExtension(sdm_path.dexPath, ART_FORMAT(".{}{}", sdm_path.isa, kSdmExtension));
}

Result<std::string> BuildSdcPath(const SecureDexMetadataWithCompanionPaths& sdc_path) {
  std::string oat_path =
      OR_RETURN(BuildOatPath(sdc_path.dexPath, sdc_path.isa, sdc_path.isInDalvikCache));
  return ReplaceFileExtension(oat_path, ".sdc");
}

bool PreRebootFlag(const ProfilePath& profile_path) {
  switch (profile_path.getTag()) {
    case ProfilePath::primaryRefProfilePath:
      return profile_path.get<ProfilePath::primaryRefProfilePath>().isPreReboot;
    case ProfilePath::secondaryRefProfilePath:
      return profile_path.get<ProfilePath::secondaryRefProfilePath>().isPreReboot;
    case ProfilePath::tmpProfilePath:
      return PreRebootFlag(profile_path.get<ProfilePath::tmpProfilePath>());
    case ProfilePath::prebuiltProfilePath:
    case ProfilePath::primaryCurProfilePath:
    case ProfilePath::secondaryCurProfilePath:
    case ProfilePath::dexMetadataPath:
      return false;
      // No default. All cases should be explicitly handled, or the compilation will fail.
  }
  // This should never happen. Just in case we get a non-enumerator value.
  LOG(FATAL) << ART_FORMAT("Unexpected profile path type {}",
                           fmt::underlying(profile_path.getTag()));
}

bool PreRebootFlag(const TmpProfilePath& tmp_profile_path) {
  return PreRebootFlag(tmp_profile_path.finalPath);
}

bool PreRebootFlag(const OutputProfile& profile) { return PreRebootFlag(profile.profilePath); }

bool PreRebootFlag(const ArtifactsPath& artifacts_path) { return artifacts_path.isPreReboot; }

bool PreRebootFlag(const OutputArtifacts& artifacts) {
  return PreRebootFlag(artifacts.artifactsPath);
}

bool PreRebootFlag(const VdexPath& vdex_path) {
  return PreRebootFlag(vdex_path.get<VdexPath::artifactsPath>());
}

bool IsPreRebootStagedFile(std::string_view filename) {
  return filename.ends_with(kPreRebootSuffix);
}

void TestOnlySetListRootDir(std::string_view root_dir) { gListRootDir = root_dir; }

}  // namespace artd
}  // namespace art
