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

#include <string>
#include <vector>

#include "android-base/result.h"
#include "android-base/strings.h"
#include "base/common_art_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "aidl/com/android/art/CompilerFilter.h"
#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"
#include "aidl/com/android/art/Isa.h"

namespace art {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Not;
using aidl::com::android::art::CompilerFilter;
using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;
using aidl::com::android::art::Isa;
using android::base::Result;

std::string GetEnvironmentVariableOrDie(const char* name) {
  const char* value = getenv(name);
  EXPECT_NE(value, nullptr);
  return value;
}

// See art_artd_tests.xml for *CLASSPATH setup.
class LibDexoptTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();

    default_bcp_ext_args_.dexFds = {10, 11};
    default_bcp_ext_args_.bootClasspaths = android::base::Split(
        GetEnvironmentVariableOrDie("DEX2OATBOOTCLASSPATH"), ":");  // from art_artd_tests.xml
    default_bcp_ext_args_.bootClasspathFds = {21, 22};
    default_bcp_ext_args_.profileFd = 30;
    default_bcp_ext_args_.dirtyImageObjectsFd = 31;
    default_bcp_ext_args_.imageFd = 90;
    default_bcp_ext_args_.vdexFd = 91;
    default_bcp_ext_args_.oatFd = 92;
    default_bcp_ext_args_.dexPaths = {"/path/to/foo.jar", "/path/to/bar.jar"};
    default_bcp_ext_args_.oatLocation = "/oat/location/bar.odex";
    default_bcp_ext_args_.isa = Isa::X86_64;
    default_bcp_ext_args_.cpuSet = {0, 1};
    default_bcp_ext_args_.threads = 42;
    ASSERT_EQ(default_bcp_ext_args_.bootClasspaths.size(),
              default_bcp_ext_args_.bootClasspathFds.size());

    default_system_server_args_.dexFd = 10;
    default_system_server_args_.profileFd = 11;
    default_system_server_args_.bootClasspaths = android::base::Split(
        GetEnvironmentVariableOrDie("BOOTCLASSPATH"), ":");  // from art_artd_tests.xml
    default_system_server_args_.bootClasspathFds = {21, 22, 23};
    default_system_server_args_.bootClasspathImageFds = {-1, 31, -1};
    default_system_server_args_.bootClasspathVdexFds = {-1, 32, -1};
    default_system_server_args_.bootClasspathOatFds = {-1, 33, -1};
    default_system_server_args_.classloaderFds = {40, 41};
    default_system_server_args_.classloaderContext = {"/cl/abc.jar", "/cl/def.jar"};
    default_system_server_args_.imageFd = 90;
    default_system_server_args_.vdexFd = 91;
    default_system_server_args_.oatFd = 92;
    default_system_server_args_.dexPath = "/path/to/foo.jar";
    default_system_server_args_.oatLocation = "/oat/location/bar.odex";
    default_system_server_args_.isa = Isa::X86_64;
    default_system_server_args_.compilerFilter = CompilerFilter::SPEED_PROFILE;
    default_system_server_args_.cpuSet = {0, 1};
    default_system_server_args_.threads = 42;
    default_system_server_args_.isBootImageOnSystem = true;
    ASSERT_EQ(default_system_server_args_.bootClasspaths.size(),
              default_system_server_args_.bootClasspathFds.size());
  }

  void TearDown() override {
    CommonArtTest::TearDown();
  }

  std::vector<std::string> Dex2oatArgsFromBcpExtensionArgs(const DexoptBcpExtArgs& args) {
    std::vector<std::string> cmdline;
    Result<void> result = AddDex2oatArgsFromBcpExtensionArgs(args, cmdline);
    EXPECT_TRUE(result.ok()) << "Failed expectation: " << result.error().message();
    return cmdline;
  }

  std::vector<std::string> Dex2oatArgsFromSystemServerArgs(const DexoptSystemServerArgs& args) {
    std::vector<std::string> cmdline;
    Result<void> result = AddDex2oatArgsFromSystemServerArgs(args, cmdline);
    EXPECT_TRUE(result.ok()) << "Failed expectation: " << result.error().message();
    return cmdline;
  }

  bool DexoptBcpExtArgsIsInvalid(const DexoptBcpExtArgs& args) {
    std::vector<std::string> cmdline;
    Result<void> result = AddDex2oatArgsFromBcpExtensionArgs(args, cmdline);
    return result.ok();
  }

  bool DexoptSystemServerArgsIsInvalid(const DexoptSystemServerArgs& args) {
    std::vector<std::string> cmdline;
    Result<void> result = AddDex2oatArgsFromSystemServerArgs(args, cmdline);
    return result.ok();
  }

  DexoptBcpExtArgs default_bcp_ext_args_;
  DexoptSystemServerArgs default_system_server_args_;
};

TEST_F(LibDexoptTest, AddDex2oatArgsFromBcpExtensionArgs) {
  // Test basics with default args
  {
    std::vector<std::string> cmdline = Dex2oatArgsFromBcpExtensionArgs(default_bcp_ext_args_);

    EXPECT_THAT(cmdline, AllOf(
        Contains("--dex-fd=10"),
        Contains("--dex-fd=11"),
        Contains("--dex-file=/path/to/foo.jar"),
        Contains("--dex-file=/path/to/bar.jar"),
        Contains(HasSubstr("-Xbootclasspath:")),
        Contains("-Xbootclasspathfds:21:22"),

        Contains("--profile-file-fd=30"),
        Contains("--compiler-filter=speed-profile"),

        Contains("--image-fd=90"),
        Contains("--output-vdex-fd=91"),
        Contains("--oat-fd=92"),
        Contains("--oat-location=/oat/location/bar.odex"),

        Contains("--dirty-image-objects-fd=31"),
        Contains("--instruction-set=x86_64"),
        Contains("--cpu-set=0,1"),
        Contains("-j42")));
  }

  // No profile
  {
    auto args = default_bcp_ext_args_;
    args.profileFd = -1;
    std::vector<std::string> cmdline = Dex2oatArgsFromBcpExtensionArgs(args);

    EXPECT_THAT(cmdline, AllOf(
        Not(Contains(HasSubstr("--profile-file-fd="))),
        Contains("--compiler-filter=speed")));
  }

  // No dirty image objects fd
  {
    auto args = default_bcp_ext_args_;
    args.dirtyImageObjectsFd = -1;
    std::vector<std::string> cmdline = Dex2oatArgsFromBcpExtensionArgs(args);

    EXPECT_THAT(cmdline, Not(Contains(HasSubstr("--dirty-image-objects-fd"))));
  }
}

TEST_F(LibDexoptTest, AddDex2oatArgsFromBcpExtensionArgs_InvalidArguments) {
  // Mismatched dex number
  {
    auto args = default_bcp_ext_args_;
    args.dexPaths = {"/path/to/foo.jar", "/path/to/bar.jar"};
    args.dexFds = {100, 101, 102};

    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));
  }

  // Mismatched classpath arguments
  {
    auto args = default_bcp_ext_args_;
    args.bootClasspathFds.emplace_back(200);

    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));
  }

  // Mismatched classpath arguments
  {
    auto args = default_bcp_ext_args_;
    args.bootClasspaths.emplace_back("/unrecognized/jar");

    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));
  }

  // Missing output fds
  {
    auto args = default_bcp_ext_args_;
    args.imageFd = -1;
    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));

    args = default_bcp_ext_args_;
    args.vdexFd = -1;
    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));

    args = default_bcp_ext_args_;
    args.oatFd = -1;
    EXPECT_FALSE(DexoptBcpExtArgsIsInvalid(args));
  }
}

TEST_F(LibDexoptTest, AddDex2oatArgsFromSystemServerArgs) {
  // Test basics with default args
  {
    std::vector<std::string> cmdline = Dex2oatArgsFromSystemServerArgs(default_system_server_args_);

    EXPECT_THAT(cmdline, AllOf(
        Contains("--dex-fd=10"),
        Contains("--dex-file=/path/to/foo.jar"),
        Contains(HasSubstr("-Xbootclasspath:")),
        Contains("-Xbootclasspathfds:21:22:23"),
        Contains("-Xbootclasspathimagefds:-1:31:-1"),
        Contains("-Xbootclasspathvdexfds:-1:32:-1"),
        Contains("-Xbootclasspathoatfds:-1:33:-1"),

        Contains("--profile-file-fd=11"),
        Contains("--compiler-filter=speed-profile"),

        Contains("--app-image-fd=90"),
        Contains("--output-vdex-fd=91"),
        Contains("--oat-fd=92"),
        Contains("--oat-location=/oat/location/bar.odex"),

        Contains("--class-loader-context-fds=40:41"),
        Contains("--class-loader-context=PCL[/cl/abc.jar:/cl/def.jar]"),

        Contains("--instruction-set=x86_64"),
        Contains("--cpu-set=0,1"),
        Contains("-j42")));
  }

  // Test different compiler filters
  {
    // speed
    auto args = default_system_server_args_;
    args.compilerFilter = CompilerFilter::SPEED;
    std::vector<std::string> cmdline = Dex2oatArgsFromSystemServerArgs(args);

    EXPECT_THAT(cmdline, AllOf(
        Not(Contains(HasSubstr("--profile-file-fd="))),
        Contains("--compiler-filter=speed")));

    // verify
    args = default_system_server_args_;
    args.compilerFilter = CompilerFilter::VERIFY;
    cmdline = Dex2oatArgsFromSystemServerArgs(args);

    EXPECT_THAT(cmdline, AllOf(
        Not(Contains(HasSubstr("--profile-file-fd="))),
        Contains("--compiler-filter=verify")));
  }

  // Test empty classloader context
  {
    auto args = default_system_server_args_;
    args.classloaderFds = {};
    args.classloaderContext = {};
    std::vector<std::string> cmdline = Dex2oatArgsFromSystemServerArgs(args);

    EXPECT_THAT(cmdline, AllOf(
        Not(Contains(HasSubstr("--class-loader-context-fds"))),
        Contains("--class-loader-context=PCL[]")));
  }

  // Test classloader context as parent
  {
    auto args = default_system_server_args_;
    args.classloaderContextAsParent = true;
    std::vector<std::string> cmdline = Dex2oatArgsFromSystemServerArgs(args);

    EXPECT_THAT(cmdline, Contains("--class-loader-context=PCL[];PCL[/cl/abc.jar:/cl/def.jar]"));
  }
}

TEST_F(LibDexoptTest, AddDex2oatArgsFromSystemServerArgs_InvalidArguments) {
  // Mismatched classpath arguments
  {
    auto args = default_system_server_args_;
    args.bootClasspathFds.emplace_back(200);

    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));
  }

  // Unrecognized jar path
  {
    auto args = default_system_server_args_;
    args.bootClasspaths.emplace_back("/unrecognized/jar");

    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));
  }

  // speed-profile without profile fd
  {
    auto args = default_system_server_args_;
    args.compilerFilter = CompilerFilter::SPEED_PROFILE;
    args.profileFd = -1;
    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));
  }

  // Missing output fds
  {
    auto args = default_system_server_args_;
    args.imageFd = -1;
    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));

    args = default_system_server_args_;
    args.vdexFd = -1;
    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));

    args = default_system_server_args_;
    args.oatFd = -1;
    EXPECT_FALSE(DexoptSystemServerArgsIsInvalid(args));
  }
}

}  // namespace art
