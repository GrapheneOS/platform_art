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

/** Utils for testing with minimal dependencies. */

#ifndef ART_LIBARTBASE_BASE_TESTING_H_
#define ART_LIBARTBASE_BASE_TESTING_H_

#include <string>
#include <vector>

#include "base/globals.h"

namespace art {
namespace testing {

inline bool IsHost() { return !art::kIsTargetBuild; }

// Returns ${ANDROID_BUILD_TOP}. Ensure it has tailing /.
std::string GetAndroidBuildTop();

// Returns ${ANDROID_HOST_OUT}.
std::string GetAndroidHostOut();

// Returns the path where boot classpath and boot image files are installed
// for host tests (by the art_common mk module, typically built through "m
// art-host-tests"). Different in CI where they are unpacked from the
// art-host-tests.zip file.
std::string GetHostBootClasspathInstallRoot();

// Note: "libcore" here means art + conscrypt + icu.

// Gets the names of the libcore modules.
// If `core_only` is true, only returns the names of CORE_IMG_JARS in Android.common_path.mk.
std::vector<std::string> GetLibCoreModuleNames(bool core_only = false);

// Gets the paths of the libcore dex files for given modules, prefixed appropriately for host or
// target tests.
std::vector<std::string> GetLibCoreDexFileNames(const std::vector<std::string>& modules);

// Gets the paths of the libcore module dex files, prefixed appropriately for host or target tests.
inline std::vector<std::string> GetLibCoreDexFileNames() {
  return GetLibCoreDexFileNames(GetLibCoreModuleNames());
}

// Gets the paths of the libcore dex files, prefixed by the given string.
// If `core_only` is true, only returns the filenames of CORE_IMG_JARS in Android.common_path.mk.
std::vector<std::string> GetLibCoreDexFileNames(const std::string& prefix, bool core_only = false);

// Gets the on-device locations of the libcore dex files for given modules.
std::vector<std::string> GetLibCoreDexLocations(const std::vector<std::string>& modules);

// Gets the on-device locations of the libcore dex files.
// If `core_only` is true, only returns the filenames of CORE_IMG_JARS in Android.common_path.mk.
std::vector<std::string> GetLibCoreDexLocations(bool core_only = false);

std::string GetClassPathOption(const char* option, const std::vector<std::string>& class_path);

}  // namespace testing
}  // namespace art

#endif  // ART_LIBARTBASE_BASE_TESTING_H_
