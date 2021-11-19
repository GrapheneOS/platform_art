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

#include "libdexopt.h"
#define LOG_TAG "libdexopt"

#include <map>
#include <string>
#include <vector>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android-base/result.h"
#include "base/file_utils.h"
#include "log/log.h"

#include "aidl/com/android/art/CompilerFilter.h"
#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"
#include "aidl/com/android/art/Isa.h"

namespace art {

namespace {

using aidl::com::android::art::CompilerFilter;
using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;
using aidl::com::android::art::Isa;
using android::base::Error;
using android::base::Result;

std::string GetBootImage() {
  // Typically "/apex/com.android.art/javalib/boot.art".
  return art::GetArtRoot() + "/javalib/boot.art";
}

std::string GetEnvironmentVariableOrDie(const char* name) {
  const char* value = getenv(name);
  LOG_ALWAYS_FATAL_IF(value == nullptr, "%s is not defined.", name);
  return value;
}

std::string GetDex2oatBootClasspath() {
  return GetEnvironmentVariableOrDie("DEX2OATBOOTCLASSPATH");
}

std::string GetBootClasspath() {
  return GetEnvironmentVariableOrDie("BOOTCLASSPATH");
}

std::string ToInstructionSetString(Isa isa) {
  switch (isa) {
    case Isa::ARM:
    case Isa::THUMB2:
      return "arm";
    case Isa::ARM64:
      return "arm64";
    case Isa::X86:
      return "x86";
    case Isa::X86_64:
      return "x86_64";
    default:
      UNREACHABLE();
  }
}

const char* CompilerFilterAidlToString(CompilerFilter compiler_filter) {
  switch (compiler_filter) {
    case CompilerFilter::SPEED_PROFILE:
      return "speed-profile";
    case CompilerFilter::SPEED:
      return"speed";
    case CompilerFilter::VERIFY:
      return "verify";
    default:
      UNREACHABLE();
  }
}

Result<void> AddBootClasspath(/*inout*/ std::vector<std::string>& cmdline,
                              const std::string& bootclasspath_env,
                              const std::vector<std::string>& boot_classpaths,
                              const std::vector<int>& boot_classpath_fds) {
  if (boot_classpaths.empty()) {
    return Errorf("Missing BCP files");
  }

  if (boot_classpaths.size() != boot_classpath_fds.size()) {
    return Errorf("Number of BCP paths (%d) != number of FDs (%d)", boot_classpaths.size(),
                  boot_classpath_fds.size());
  }

  cmdline.emplace_back("--runtime-arg");
  cmdline.emplace_back("-Xbootclasspath:" + bootclasspath_env);

  // Construct a path->fd map from both arrays. If the client provides duplicated paths, only one
  // will be used. This is fine since the client may not be trusted any way.
  std::map<std::string, int> bcp_map;
  auto zip = [](const std::string &path, int fd) { return std::make_pair(path, fd); };
  std::transform(boot_classpaths.begin(), boot_classpaths.end(), boot_classpath_fds.begin(),
                 std::inserter(bcp_map, bcp_map.end()), zip);

  std::vector<std::string> jar_paths = android::base::Split(bootclasspath_env, ":");
  std::stringstream ss;
  ss << "-Xbootclasspathfds";
  for (auto& jar_path : jar_paths) {
    auto iter = bcp_map.find(jar_path);
    if (iter == bcp_map.end()) {
      ss << ":-1";
    } else {
      ss << ":" << std::to_string(iter->second);
      bcp_map.erase(iter);
    }
  }
  cmdline.emplace_back("--runtime-arg");
  cmdline.emplace_back(ss.str());

  if (!bcp_map.empty()) {
    std::stringstream error_ss;
    for (const auto &[key, _] : bcp_map) {
      error_ss << key << ":";
    }
    return Error() << "Residual BCP paths: " << error_ss.str();
  }
  return {};
}

Result<void> AddCompiledBootClasspathFdsIfAny(/*inout*/ std::vector<std::string>& cmdline,
                                              const DexoptSystemServerArgs& args) {
  // Result
  if ((args.bootClasspathImageFds.size() != args.bootClasspathOatFds.size()) ||
      (args.bootClasspathImageFds.size() != args.bootClasspathVdexFds.size()) ||
      (args.bootClasspathImageFds.size() != args.bootClasspaths.size())) {
    return Errorf("Inconsistent FD numbers of BCP artifacts: jar/image/vdex/oat: %d/%d/%d/%d",
                  args.bootClasspaths.size(), args.bootClasspathImageFds.size(),
                  args.bootClasspathVdexFds.size(), args.bootClasspathOatFds.size());
  }

  if (!args.bootClasspathImageFds.empty()) {
    cmdline.emplace_back("--runtime-arg");
    cmdline.emplace_back(
        "-Xbootclasspathimagefds:" + android::base::Join(args.bootClasspathImageFds, ':'));
    cmdline.emplace_back("--runtime-arg");
    cmdline.emplace_back(
        "-Xbootclasspathoatfds:" + android::base::Join(args.bootClasspathOatFds, ':'));
    cmdline.emplace_back("--runtime-arg");
    cmdline.emplace_back(
        "-Xbootclasspathvdexfds:" + android::base::Join(args.bootClasspathVdexFds, ':'));
  }
  return {};
}

void AddDex2OatConcurrencyArguments(/*inout*/ std::vector<std::string>& cmdline,
                                    int threads,
                                    const std::vector<int>& cpu_set) {
  if (threads > 0) {
      cmdline.emplace_back(android::base::StringPrintf("-j%d", threads));
  }
  if (!cpu_set.empty()) {
      cmdline.emplace_back("--cpu-set=" + android::base::Join(cpu_set, ','));
  }
}

void AddDex2OatCommonOptions(/*inout*/ std::vector<std::string>& cmdline) {
  cmdline.emplace_back("--android-root=out/empty");
  cmdline.emplace_back("--abort-on-hard-verifier-error");
  cmdline.emplace_back("--no-abort-on-soft-verifier-error");
  cmdline.emplace_back("--compilation-reason=boot");
  cmdline.emplace_back("--image-format=lz4");
  cmdline.emplace_back("--force-determinism");
  cmdline.emplace_back("--resolve-startup-const-strings=true");

  // Avoid storing dex2oat cmdline in oat header. We want to be sure that the compiled artifacts
  // are identical regardless of where the compilation happened. But some of the cmdline flags tends
  // to be unstable, e.g. those contains FD numbers. To avoid the problem, the whole cmdline is not
  // added to the oat header.
  cmdline.emplace_back("--avoid-storing-invocation");
}

void AddDex2OatDebugInfo(/*inout*/ std::vector<std::string>& cmdline) {
  cmdline.emplace_back("--generate-mini-debug-info");
  cmdline.emplace_back("--strip");
}

}  // namespace

Result<void> AddDex2oatArgsFromBcpExtensionArgs(const DexoptBcpExtArgs& args,
                                                /*out*/ std::vector<std::string>& cmdline) {
  // Common dex2oat flags
  AddDex2OatCommonOptions(cmdline);
  AddDex2OatDebugInfo(cmdline);

  cmdline.emplace_back("--instruction-set=" + ToInstructionSetString(args.isa));

  if (args.profileFd >= 0) {
    cmdline.emplace_back(android::base::StringPrintf("--profile-file-fd=%d", args.profileFd));
    cmdline.emplace_back("--compiler-filter=speed-profile");
  } else {
    cmdline.emplace_back("--compiler-filter=speed");
  }

  // Compile as a single image for fewer files and slightly less memory overhead.
  cmdline.emplace_back("--single-image");

  // Set boot-image and expectation of compiling boot classpath extensions.
  cmdline.emplace_back("--boot-image=" + GetBootImage());

  if (args.dirtyImageObjectsFd >= 0) {
    cmdline.emplace_back(android::base::StringPrintf("--dirty-image-objects-fd=%d",
                                                     args.dirtyImageObjectsFd));
  }

  if (args.dexPaths.size() != args.dexFds.size()) {
    return Errorf("Mismatched number of dexPaths (%d) and dexFds (%d)",
                  args.dexPaths.size(),
                  args.dexFds.size());
  }
  for (unsigned int i = 0; i < args.dexPaths.size(); ++i) {
    cmdline.emplace_back("--dex-file=" + args.dexPaths[i]);
    cmdline.emplace_back(android::base::StringPrintf("--dex-fd=%d", args.dexFds[i]));
  }

  std::string bcp_env = GetDex2oatBootClasspath();
  auto result = AddBootClasspath(cmdline, bcp_env, args.bootClasspaths, args.bootClasspathFds);
  if (!result.ok()) {
    return result.error();
  }

  cmdline.emplace_back("--oat-location=" + args.oatLocation);

  // Output files
  if (args.imageFd < 0) {
    return Error() << "imageFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--image-fd=%d", args.imageFd));
  if (args.vdexFd < 0) {
    return Error() << "vdexFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--output-vdex-fd=%d", args.vdexFd));
  if (args.oatFd < 0) {
    return Error() << "oatFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--oat-fd=%d", args.oatFd));

  AddDex2OatConcurrencyArguments(cmdline, args.threads, args.cpuSet);

  return {};
}

Result<void> AddDex2oatArgsFromSystemServerArgs(const DexoptSystemServerArgs& args,
                                                /*out*/ std::vector<std::string>& cmdline) {
  cmdline.emplace_back("--dex-file=" + args.dexPath);
  cmdline.emplace_back(android::base::StringPrintf("--dex-fd=%d", args.dexFd));

  // Common dex2oat flags
  AddDex2OatCommonOptions(cmdline);
  AddDex2OatDebugInfo(cmdline);

  cmdline.emplace_back("--instruction-set=" + ToInstructionSetString(args.isa));

  if (args.compilerFilter == CompilerFilter::SPEED_PROFILE) {
    if (args.profileFd < 0) {
      return Error() << "profileFd is missing";
    }
    cmdline.emplace_back(android::base::StringPrintf("--profile-file-fd=%d", args.profileFd));
    cmdline.emplace_back("--compiler-filter=speed-profile");
  } else {
    cmdline.emplace_back("--compiler-filter=" +
                         std::string(CompilerFilterAidlToString(args.compilerFilter)));
  }

  // Output files
  if (args.imageFd < 0) {
    return Error() << "imageFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--app-image-fd=%d", args.imageFd));
  if (args.vdexFd < 0) {
    return Error() << "vdexFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--output-vdex-fd=%d", args.vdexFd));
  if (args.oatFd < 0) {
    return Error() << "oatFd is missing";
  }
  cmdline.emplace_back(android::base::StringPrintf("--oat-fd=%d", args.oatFd));
  cmdline.emplace_back("--oat-location=" + args.oatLocation);

  std::string bcp_env = GetBootClasspath();
  auto result = AddBootClasspath(cmdline, bcp_env, args.bootClasspaths, args.bootClasspathFds);
  if (!result.ok()) {
    return result.error();
  }
  result = AddCompiledBootClasspathFdsIfAny(cmdline, args);
  if (!result.ok()) {
    return result.error();
  }

  if (args.classloaderFds.empty()) {
    cmdline.emplace_back("--class-loader-context=PCL[]");
  } else {
    const std::string context_path = android::base::Join(args.classloaderContext, ':');
    if (args.classloaderContextAsParent) {
      cmdline.emplace_back("--class-loader-context=PCL[];PCL[" + context_path + "]");
    } else {
      cmdline.emplace_back("--class-loader-context=PCL[" + context_path + "]");
    }
    cmdline.emplace_back("--class-loader-context-fds=" +
                         android::base::Join(args.classloaderFds, ':'));
  }

  // Derive boot image
  // b/197176583
  // If the boot extension artifacts are not on /data, then boot extensions are not re-compiled
  // and the artifacts must exist on /system.
  std::vector<std::string> jar_paths = android::base::Split(GetDex2oatBootClasspath(), ":");
  auto iter = std::find_if_not(jar_paths.begin(), jar_paths.end(), &LocationIsOnArtModule);
  if (iter == jar_paths.end()) {
    return Error() << "Missing BCP extension compatible JAR";
  }
  const std::string& first_boot_extension_compatible_jars = *iter;
  // TODO(197176583): Support compiling against BCP extension in /system.
  const std::string extension_image = GetBootImagePath(args.isBootImageOnSystem,
                                                       first_boot_extension_compatible_jars);
  if (extension_image.empty()) {
    return Error() << "Can't identify the first boot extension compatible jar";
  }
  cmdline.emplace_back("--boot-image=" + GetBootImage() + ":" + extension_image);

  AddDex2OatConcurrencyArguments(cmdline, args.threads, args.cpuSet);

  return {};
}

}  // namespace art
