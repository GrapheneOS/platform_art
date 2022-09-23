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

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/errors.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "file_utils.h"
#include "fmt/format.h"
#include "oat_file_assistant.h"

namespace art {
namespace artd {

namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::aidl::com::android::server::art::DexMetadataPath;
using ::aidl::com::android::server::art::ProfilePath;
using ::aidl::com::android::server::art::VdexPath;
using ::android::base::EndsWith;
using ::android::base::Error;
using ::android::base::Result;

using ::fmt::literals::operator""_format;  // NOLINT

using CurProfilePath = ProfilePath::CurProfilePath;
using PrebuiltProfilePath = ProfilePath::PrebuiltProfilePath;
using RefProfilePath = ProfilePath::RefProfilePath;
using TmpRefProfilePath = ProfilePath::TmpRefProfilePath;

Result<void> ValidateAbsoluteNormalPath(const std::string& path_str) {
  if (path_str.empty()) {
    return Errorf("Path is empty");
  }
  if (path_str.find('\0') != std::string::npos) {
    return Errorf("Path '{}' has invalid character '\\0'", path_str);
  }
  std::filesystem::path path(path_str);
  if (!path.is_absolute()) {
    return Errorf("Path '{}' is not an absolute path", path_str);
  }
  if (path.lexically_normal() != path_str) {
    return Errorf("Path '{}' is not in normal form", path_str);
  }
  return {};
}

Result<void> ValidatePathElementSubstring(const std::string& path_element_substring,
                                          const std::string& name) {
  if (path_element_substring.empty()) {
    return Errorf("{} is empty", name);
  }
  if (path_element_substring.find('/') != std::string::npos) {
    return Errorf("{} '{}' has invalid character '/'", name, path_element_substring);
  }
  if (path_element_substring.find('\0') != std::string::npos) {
    return Errorf("{} '{}' has invalid character '\\0'", name, path_element_substring);
  }
  return {};
}

Result<void> ValidatePathElement(const std::string& path_element, const std::string& name) {
  OR_RETURN(ValidatePathElementSubstring(path_element, name));
  if (path_element == "." || path_element == "..") {
    return Errorf("Invalid {} '{}'", name, path_element);
  }
  return {};
}

Result<std::string> GetAndroidDataOrError() {
  std::string error_msg;
  std::string result = GetAndroidDataSafe(&error_msg);
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

}  // namespace

Result<void> ValidateDexPath(const std::string& dex_path) {
  OR_RETURN(ValidateAbsoluteNormalPath(dex_path));
  if (!EndsWith(dex_path, ".apk") && !EndsWith(dex_path, ".jar")) {
    return Errorf("Dex path '{}' has an invalid extension", dex_path);
  }
  return {};
}

Result<std::string> BuildArtBinPath(const std::string& binary_name) {
  return "{}/bin/{}"_format(OR_RETURN(GetArtRootOrError()), binary_name);
}

Result<std::string> BuildOatPath(const ArtifactsPath& artifacts_path) {
  OR_RETURN(ValidateDexPath(artifacts_path.dexPath));

  InstructionSet isa = GetInstructionSetFromString(artifacts_path.isa.c_str());
  if (isa == InstructionSet::kNone) {
    return Errorf("Instruction set '{}' is invalid", artifacts_path.isa);
  }

  std::string error_msg;
  std::string path;
  if (artifacts_path.isInDalvikCache) {
    // Apps' OAT files are never in ART APEX data.
    if (!OatFileAssistant::DexLocationToOatFilename(
            artifacts_path.dexPath, isa, /*deny_art_apex_data_files=*/true, &path, &error_msg)) {
      return Error() << error_msg;
    }
    return path;
  } else {
    if (!OatFileAssistant::DexLocationToOdexFilename(
            artifacts_path.dexPath, isa, &path, &error_msg)) {
      return Error() << error_msg;
    }
    return path;
  }
}

Result<std::string> BuildRefProfilePath(const RefProfilePath& ref_profile_path) {
  OR_RETURN(ValidatePathElement(ref_profile_path.packageName, "packageName"));
  OR_RETURN(ValidatePathElementSubstring(ref_profile_path.profileName, "profileName"));
  return "{}/misc/profiles/ref/{}/{}.prof"_format(OR_RETURN(GetAndroidDataOrError()),
                                                  ref_profile_path.packageName,
                                                  ref_profile_path.profileName);
}

Result<std::string> BuildTmpRefProfilePath(const TmpRefProfilePath& tmp_ref_profile_path) {
  OR_RETURN(ValidatePathElementSubstring(tmp_ref_profile_path.id, "id"));
  return NewFile::BuildTempPath(
      OR_RETURN(BuildRefProfilePath(tmp_ref_profile_path.refProfilePath)).c_str(),
      tmp_ref_profile_path.id.c_str());
}

Result<std::string> BuildPrebuiltProfilePath(const PrebuiltProfilePath& prebuilt_profile_path) {
  OR_RETURN(ValidateDexPath(prebuilt_profile_path.dexPath));
  return prebuilt_profile_path.dexPath + ".prof";
}

Result<std::string> BuildCurProfilePath(const CurProfilePath& cur_profile_path) {
  OR_RETURN(ValidatePathElement(cur_profile_path.packageName, "packageName"));
  OR_RETURN(ValidatePathElementSubstring(cur_profile_path.profileName, "profileName"));
  return "{}/misc/profiles/cur/{}/{}/{}.prof"_format(OR_RETURN(GetAndroidDataOrError()),
                                                     cur_profile_path.userId,
                                                     cur_profile_path.packageName,
                                                     cur_profile_path.profileName);
}

Result<std::string> BuildDexMetadataPath(const DexMetadataPath& dex_metadata_path) {
  OR_RETURN(ValidateDexPath(dex_metadata_path.dexPath));
  return ReplaceFileExtension(dex_metadata_path.dexPath, "dm");
}

Result<std::string> BuildDexMetadataPath(const VdexPath& vdex_path) {
  DCHECK(vdex_path.getTag() == VdexPath::dexMetadataPath);
  return BuildDexMetadataPath(vdex_path.get<VdexPath::dexMetadataPath>());
}

Result<std::string> BuildProfileOrDmPath(const ProfilePath& profile_path) {
  switch (profile_path.getTag()) {
    case ProfilePath::refProfilePath:
      return BuildRefProfilePath(profile_path.get<ProfilePath::refProfilePath>());
    case ProfilePath::tmpRefProfilePath:
      return BuildTmpRefProfilePath(profile_path.get<ProfilePath::tmpRefProfilePath>());
    case ProfilePath::prebuiltProfilePath:
      return BuildPrebuiltProfilePath(profile_path.get<ProfilePath::prebuiltProfilePath>());
    case ProfilePath::curProfilePath:
      return BuildCurProfilePath(profile_path.get<ProfilePath::curProfilePath>());
    case ProfilePath::dexMetadataPath:
      return BuildDexMetadataPath(profile_path.get<ProfilePath::dexMetadataPath>());
      // No default. All cases should be explicitly handled, or the compilation will fail.
  }
  // This should never happen. Just in case we get a non-enumerator value.
  LOG(FATAL) << "Unexpected profile path type {}"_format(profile_path.getTag());
}

Result<std::string> BuildVdexPath(const VdexPath& vdex_path) {
  DCHECK(vdex_path.getTag() == VdexPath::artifactsPath);
  return OatPathToVdexPath(OR_RETURN(BuildOatPath(vdex_path.get<VdexPath::artifactsPath>())));
}

}  // namespace artd
}  // namespace art
