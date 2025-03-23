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

#include "testing.h"

#include <string>
#include <vector>

#include "android-base/file.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/os.h"

namespace art {
namespace testing {

std::string GetAndroidBuildTop() {
  CHECK(IsHost());
  std::string android_build_top;

  // Look at how we were invoked to find the expected directory.
  std::string argv;
  if (android::base::ReadFileToString("/proc/self/cmdline", &argv)) {
    // /proc/self/cmdline is the programs 'argv' with elements delimited by '\0'.
    std::filesystem::path path(argv.substr(0, argv.find('\0')));
    path = std::filesystem::absolute(path);
    // Walk up until we find the one of the well-known directories.
    for (; path.parent_path() != path; path = path.parent_path()) {
      // We are running tests from out/host/linux-x86 on developer machine.
      if (path.filename() == std::filesystem::path("linux-x86")) {
        android_build_top = path.parent_path().parent_path().parent_path();
        break;
      }
      // We are running tests from testcases (extracted from zip) on tradefed.
      // The first path is for remote runs and the second path for local runs.
      if (path.filename() == std::filesystem::path("testcases") ||
          path.filename().string().starts_with("host_testcases")) {
        android_build_top = path.append("art_common");
        break;
      }
    }
  }
  CHECK(!android_build_top.empty());

  // Check that the expected directory matches the environment variable.
  const char* android_build_top_from_env = getenv("ANDROID_BUILD_TOP");
  android_build_top = std::filesystem::path(android_build_top).string();
  CHECK(!android_build_top.empty());
  if (android_build_top_from_env != nullptr) {
    if (std::filesystem::weakly_canonical(android_build_top).string() !=
        std::filesystem::weakly_canonical(android_build_top_from_env).string()) {
      android_build_top = android_build_top_from_env;
    }
  } else {
    setenv("ANDROID_BUILD_TOP", android_build_top.c_str(), /*overwrite=*/0);
  }
  if (android_build_top.back() != '/') {
    android_build_top += '/';
  }
  return android_build_top;
}

std::string GetAndroidHostOut() {
  CHECK(IsHost());

  // Check that the expected directory matches the environment variable.
  // ANDROID_HOST_OUT is set by envsetup or unset and is the full path to host binaries/libs
  const char* android_host_out_from_env = getenv("ANDROID_HOST_OUT");
  // OUT_DIR is a user-settable ENV_VAR that controls where soong puts build artifacts. It can
  // either be relative to ANDROID_BUILD_TOP or a concrete path.
  const char* android_out_dir = getenv("OUT_DIR");
  // Take account of OUT_DIR setting.
  if (android_out_dir == nullptr) {
    android_out_dir = "out";
  }
  std::string android_host_out;
  if (android_out_dir[0] == '/') {
    android_host_out = (std::filesystem::path(android_out_dir) / "host" / "linux-x86").string();
  } else {
    android_host_out =
        (std::filesystem::path(GetAndroidBuildTop()) / android_out_dir / "host" / "linux-x86")
            .string();
  }
  std::filesystem::path expected(android_host_out);
  if (android_host_out_from_env != nullptr) {
    std::filesystem::path from_env(std::filesystem::weakly_canonical(android_host_out_from_env));
    if (std::filesystem::weakly_canonical(expected).string() != from_env.string()) {
      LOG(WARNING) << "Execution path (" << expected << ") not below ANDROID_HOST_OUT (" << from_env
                   << ")! Using env-var.";
      expected = from_env;
    }
  } else {
    setenv("ANDROID_HOST_OUT", android_host_out.c_str(), /*overwrite=*/0);
  }
  return expected.string();
}

std::string GetHostBootClasspathInstallRoot() {
  CHECK(IsHost());
  std::string build_install_root = GetAndroidHostOut() + "/testcases/art_common/out/host/linux-x86";
  // Look for the `apex` subdirectory as a discriminator to check the location.
  if (OS::DirectoryExists((build_install_root + "/apex").c_str())) {
    // This is the path where "m art-host-tests" installs support files for host
    // tests, so use it when the tests are run in a build tree (which is the
    // case when testing locally).
    return build_install_root;
  }
  if (OS::DirectoryExists((GetAndroidRoot() + "/apex").c_str())) {
    // This is the location for host tests in CI when the files are unzipped
    // from art-host-tests.zip.
    return GetAndroidRoot();
  }
  LOG(ERROR) << "Neither location has a boot classpath (forgot \"m art-host-tests\"?): "
             << build_install_root << " or " << GetAndroidRoot();
  return "<no boot classpath found>";
}

static std::string GetDexFileName(const std::string& jar_prefix, const std::string& prefix) {
  const char* apexPath =
      (jar_prefix == "conscrypt") ?
          kAndroidConscryptApexDefaultPath :
          (jar_prefix == "core-icu4j" ? kAndroidI18nApexDefaultPath : kAndroidArtApexDefaultPath);
  return android::base::StringPrintf(
      "%s%s/javalib/%s.jar", prefix.c_str(), apexPath, jar_prefix.c_str());
}

static std::vector<std::string> GetPrefixedDexFileNames(const std::string& prefix,
                                                        const std::vector<std::string>& modules) {
  std::vector<std::string> result;
  result.reserve(modules.size());
  for (const std::string& module : modules) {
    result.push_back(GetDexFileName(module, prefix));
  }
  return result;
}

std::vector<std::string> GetLibCoreModuleNames(bool core_only) {
  // Note: This must start with the CORE_IMG_JARS in Android.common_path.mk because that's what we
  // use for compiling the boot.art image. It may contain additional modules from TEST_CORE_JARS.

  // CORE_IMG_JARS modules.
  std::vector<std::string> modules{
      "core-oj",
      "core-libart",
      "okhttp",
      "bouncycastle",
      "apache-xml",
  };

  // Additional modules.
  if (!core_only) {
    modules.push_back("core-icu4j");
    modules.push_back("conscrypt");
  }

  return modules;
}

std::vector<std::string> GetLibCoreDexFileNames(const std::vector<std::string>& modules) {
  return GetPrefixedDexFileNames(kIsTargetBuild ? "" : GetHostBootClasspathInstallRoot(), modules);
}

std::vector<std::string> GetLibCoreDexFileNames(const std::string& prefix, bool core_only) {
  std::vector<std::string> modules = GetLibCoreModuleNames(core_only);
  return GetPrefixedDexFileNames(prefix, modules);
}

std::vector<std::string> GetLibCoreDexLocations(const std::vector<std::string>& modules) {
  std::string prefix = "";
  if (IsHost()) {
    std::string android_root = GetAndroidRoot();
    std::string build_top = GetAndroidBuildTop();
    CHECK(android_root.starts_with(build_top))
        << " android_root=" << android_root << " build_top=" << build_top;
    prefix = android_root.substr(build_top.size());
  }
  return GetPrefixedDexFileNames(prefix, modules);
}

std::vector<std::string> GetLibCoreDexLocations(bool core_only) {
  std::vector<std::string> modules = GetLibCoreModuleNames(core_only);
  return GetLibCoreDexLocations(modules);
}

std::string GetClassPathOption(const char* option, const std::vector<std::string>& class_path) {
  return option + android::base::Join(class_path, ':');
}

}  // namespace testing
}  // namespace art
