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

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/result.h"

namespace art {
namespace artd {

// Returns the absolute path to the OAT file built from the `ArtifactsPath`.
android::base::Result<std::string> BuildOatPath(
    const aidl::com::android::server::art::ArtifactsPath& artifacts_path);

// Returns the path to the VDEX file that corresponds to the OAT file.
std::string OatPathToVdexPath(const std::string& oat_path);

// Returns the path to the ART file that corresponds to the OAT file.
std::string OatPathToArtPath(const std::string& oat_path);

}  // namespace artd
}  // namespace art

#endif  // ART_ARTD_PATH_UTILS_H_
