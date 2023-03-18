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

/** A commandline tool to generate a primary boot image for testing. */

#include <sys/stat.h>
#include <sysexits.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "base/os.h"
#include "base/testing.h"

namespace art {

namespace {

using ::android::base::Join;
using ::android::base::StringPrintf;
using ::art::testing::GetLibCoreDexFileNames;

std::string GetCompilerExecutable() {
  std::string compiler_executable = GetArtBinDir() + "/dex2oat";
  if (kIsDebugBuild) {
    compiler_executable += 'd';
  }
  compiler_executable += Is64BitInstructionSet(kRuntimeISA) ? "64" : "32";
  return compiler_executable;
}

// Joins a list of commandline args into a single string, where each part is quoted with double
// quotes. Note that this is a naive implementation that does NOT escape existing double quotes,
// which is fine since we don't have existing double quotes in the args in this particular use case
// and this code is never used in production.
std::string BuildCommand(const std::vector<std::string>& args) {
  std::string command = "";
  for (const std::string& arg : args) {
    if (!command.empty()) {
      command += " ";
    }
    command += '"' + arg + '"';
  }
  return command;
}

int GenerateBootImage(const std::string& dir, const std::string& compiler_filter) {
  std::string isa = GetInstructionSetString(kRuntimeISA);

  std::vector<std::string> args;
  args.push_back(GetCompilerExecutable());

  std::vector<std::string> dex_files = GetLibCoreDexFileNames(/*core_only=*/true);
  args.push_back("--runtime-arg");
  args.push_back("-Xbootclasspath:" + Join(dex_files, ":"));
  for (const std::string& file : dex_files) {
    args.push_back("--dex-file=" + file);
  }

  args.push_back("--instruction-set=" + isa);
  args.push_back(StringPrintf("--base=0x%08x", ART_BASE_ADDRESS));
  args.push_back("--compiler-filter=" + compiler_filter);
  args.push_back(StringPrintf("--profile-file=%s/etc/boot-image.prof", GetArtRoot().c_str()));
  args.push_back("--avoid-storing-invocation");
  args.push_back("--generate-debug-info");
  args.push_back("--generate-build-id");
  args.push_back("--image-format=lz4hc");
  args.push_back("--strip");
  args.push_back("--android-root=out/empty");

  std::string path = StringPrintf("%s/%s", dir.c_str(), isa.c_str());
  if (!OS::DirectoryExists(path.c_str())) {
    CHECK_EQ(mkdir(path.c_str(), S_IRWXU), 0);
  }
  args.push_back(StringPrintf("--image=%s/boot.art", path.c_str()));
  args.push_back(StringPrintf("--oat-file=%s/boot.oat", path.c_str()));

  int exit_code = system(BuildCommand(args).c_str());
  if (exit_code != 0) {
    LOG(ERROR) << "dex2oat invocation failed. Exit code: " << exit_code;
  }
  return exit_code;
}

}  // namespace
}  // namespace art

int main(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));

  std::string dir = "";
  // Set the compiler filter to `verify` by default to make test preparation
  // faster.
  std::string compiler_filter = "verify";
  for (int i = 1; i < argc; i++) {
    std::string_view arg{argv[i]};
    if (android::base::ConsumePrefix(&arg, "--output-dir=")) {
      dir = arg;
    } else if (android::base::ConsumePrefix(&arg, "--compiler-filter=")) {
      compiler_filter = arg;
    } else {
      LOG(ERROR) << android::base::StringPrintf("Unrecognized argument: '%s'", argv[i]);
      exit(EX_USAGE);
    }
  }

  if (dir.empty()) {
    LOG(ERROR) << "--output-dir must be specified";
    exit(EX_USAGE);
  }

  return art::GenerateBootImage(dir, compiler_filter);
}
