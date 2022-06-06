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
#include "oat_file_assistant.h"

namespace art {
namespace artd {

namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::android::base::EndsWith;
using ::android::base::Error;
using ::android::base::Result;

Result<void> ValidateAbsoluteNormalPath(const std::string& path_str) {
  if (path_str.empty()) {
    return Errorf("Path is empty");
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

Result<void> ValidateDexPath(const std::string& dex_path) {
  OR_RETURN(ValidateAbsoluteNormalPath(dex_path));
  if (!EndsWith(dex_path, ".apk") && !EndsWith(dex_path, ".jar")) {
    return Errorf("Dex path '{}' has an invalid extension", dex_path);
  }
  return {};
}

}  // namespace

Result<std::string> BuildOatPath(const ArtifactsPath& artifacts_path) {
  OR_RETURN(ValidateDexPath(artifacts_path.dexPath));

  InstructionSet isa = GetInstructionSetFromString(artifacts_path.isa.c_str());
  if (isa == InstructionSet::kNone) {
    return Errorf("Instruction set '{}' is invalid", artifacts_path.isa.c_str());
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

std::string OatPathToVdexPath(const std::string& oat_path) {
  return ReplaceFileExtension(oat_path, "vdex");
}

std::string OatPathToArtPath(const std::string& oat_path) {
  return ReplaceFileExtension(oat_path, "art");
}

}  // namespace artd
}  // namespace art
