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

#include "odrefresh.h"

#include <unistd.h>

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "android-base/parseint.h"
#include "android-base/scopeguard.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/common_art_test.h"
#include "base/file_utils.h"
#include "base/stl_util.h"
#include "exec_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "odr_artifacts.h"
#include "odr_common.h"
#include "odr_config.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Return;

constexpr int kReplace = 1;

void CreateEmptyFile(const std::string& name) {
  File* file = OS::CreateEmptyFile(name.c_str());
  ASSERT_TRUE(file != nullptr) << "Cannot create file " << name;
  file->Release();
  delete file;
}

android::base::ScopeGuard<std::function<void()>> ScopedCreateEmptyFile(const std::string& name) {
  CreateEmptyFile(name);
  return android::base::ScopeGuard([=]() { unlink(name.c_str()); });
}

class MockExecUtils : public ExecUtils {
 public:
  // A workaround to avoid MOCK_METHOD on a method with an `std::string*` parameter, which will lead
  // to a conflict between gmock and android-base/logging.h (b/132668253).
  int ExecAndReturnCode(std::vector<std::string>& arg_vector,
                        time_t,
                        bool*,
                        std::string*) const override {
    return DoExecAndReturnCode(arg_vector);
  }

  MOCK_METHOD(int, DoExecAndReturnCode, (std::vector<std::string> & arg_vector), (const));
};

// Matches a flag that starts with `flag` and is a colon-separated list that contains an element
// that matches `matcher`.
MATCHER_P2(FlagContains, flag, matcher, "") {
  std::string_view value = arg;
  if (!android::base::ConsumePrefix(&value, flag)) {
    return false;
  }
  for (std::string_view s : SplitString(value, ':')) {
    if (ExplainMatchResult(matcher, s, result_listener)) {
      return true;
    }
  }
  return false;
}

// Matches an FD of a file whose path matches `matcher`.
MATCHER_P(FdOf, matcher, "") {
  char path[PATH_MAX];
  int fd;
  if (!android::base::ParseInt(std::string{arg}, &fd)) {
    return false;
  }
  std::string proc_path = android::base::StringPrintf("/proc/self/fd/%d", fd);
  ssize_t len = readlink(proc_path.c_str(), path, sizeof(path));
  if (len < 0) {
    return false;
  }
  std::string path_str{path, static_cast<size_t>(len)};
  return ExplainMatchResult(matcher, path_str, result_listener);
}

void WriteFakeApexInfoList(const std::string& filename) {
  std::string content = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<apex-info-list>
  <apex-info
      moduleName="com.android.art"
      modulePath="/data/apex/active/com.android.art@319999900.apex"
      preinstalledModulePath="/system/apex/com.android.art.capex"
      versionCode="319999900"
      versionName=""
      isFactory="false"
      isActive="true"
      lastUpdateMillis="12345678">
  </apex-info>
</apex-info-list>
)xml";
  android::base::WriteStringToFile(content, filename);
}

class OdRefreshTest : public CommonArtTest {
 public:
  OdRefreshTest() : config_("odrefresh") {}

 protected:
  void SetUp() override {
    CommonArtTest::SetUp();

    temp_dir_ = std::make_unique<ScratchDir>();
    std::string_view temp_dir_path = temp_dir_->GetPath();
    android::base::ConsumeSuffix(&temp_dir_path, "/");

    std::string android_root_path = Concatenate({temp_dir_path, "/system"});
    ASSERT_TRUE(EnsureDirectoryExists(android_root_path));
    android_root_env_ = std::make_unique<ScopedUnsetEnvironmentVariable>("ANDROID_ROOT");
    setenv("ANDROID_ROOT", android_root_path.c_str(), kReplace);

    std::string android_art_root_path = Concatenate({temp_dir_path, "/apex/com.android.art"});
    ASSERT_TRUE(EnsureDirectoryExists(android_art_root_path));
    android_art_root_env_ = std::make_unique<ScopedUnsetEnvironmentVariable>("ANDROID_ART_ROOT");
    setenv("ANDROID_ART_ROOT", android_art_root_path.c_str(), kReplace);

    std::string art_apex_data_path = Concatenate({temp_dir_path, kArtApexDataDefaultPath});
    ASSERT_TRUE(EnsureDirectoryExists(art_apex_data_path));
    art_apex_data_env_ = std::make_unique<ScopedUnsetEnvironmentVariable>("ART_APEX_DATA");
    setenv("ART_APEX_DATA", art_apex_data_path.c_str(), kReplace);

    dalvik_cache_dir_ = art_apex_data_path + "/dalvik-cache";
    ASSERT_TRUE(EnsureDirectoryExists(dalvik_cache_dir_ + "/x86_64"));

    std::string system_etc_dir = Concatenate({android_root_path, "/etc"});
    ASSERT_TRUE(EnsureDirectoryExists(system_etc_dir));
    framework_profile_ = system_etc_dir + "/boot-image.prof";
    CreateEmptyFile(framework_profile_);
    std::string art_etc_dir = Concatenate({android_art_root_path, "/etc"});
    ASSERT_TRUE(EnsureDirectoryExists(art_etc_dir));
    art_profile_ = art_etc_dir + "/boot-image.prof";
    CreateEmptyFile(art_profile_);

    framework_dir_ = android_root_path + "/framework";
    framework_jar_ = framework_dir_ + "/framework.jar";
    location_provider_jar_ = framework_dir_ + "/com.android.location.provider.jar";
    services_jar_ = framework_dir_ + "/services.jar";
    services_foo_jar_ = framework_dir_ + "/services-foo.jar";
    services_bar_jar_ = framework_dir_ + "/services-bar.jar";
    std::string services_jar_prof = framework_dir_ + "/services.jar.prof";
    art_javalib_dir_ = android_art_root_path + "/javalib";
    core_oj_jar_ = art_javalib_dir_ + "/core-oj.jar";

    // Create placeholder files.
    ASSERT_TRUE(EnsureDirectoryExists(framework_dir_ + "/x86_64"));
    CreateEmptyFile(framework_jar_);
    CreateEmptyFile(location_provider_jar_);
    CreateEmptyFile(services_jar_);
    CreateEmptyFile(services_foo_jar_);
    CreateEmptyFile(services_bar_jar_);
    CreateEmptyFile(services_jar_prof);
    ASSERT_TRUE(EnsureDirectoryExists(art_javalib_dir_));
    CreateEmptyFile(core_oj_jar_);

    std::string apex_info_filename = Concatenate({temp_dir_path, "/apex-info-list.xml"});
    WriteFakeApexInfoList(apex_info_filename);
    config_.SetApexInfoListFile(apex_info_filename);

    config_.SetArtBinDir(Concatenate({temp_dir_path, "/bin"}));
    config_.SetBootClasspath(Concatenate({core_oj_jar_, ":", framework_jar_}));
    config_.SetDex2oatBootclasspath(Concatenate({core_oj_jar_, ":", framework_jar_}));
    config_.SetSystemServerClasspath(Concatenate({location_provider_jar_, ":", services_jar_}));
    config_.SetStandaloneSystemServerJars(Concatenate({services_foo_jar_, ":", services_bar_jar_}));
    config_.SetIsa(InstructionSet::kX86_64);
    config_.SetZygoteKind(ZygoteKind::kZygote64_32);
    config_.SetSystemServerCompilerFilter("");
    config_.SetArtifactDirectory(dalvik_cache_dir_);

    std::string staging_dir = dalvik_cache_dir_ + "/staging";
    ASSERT_TRUE(EnsureDirectoryExists(staging_dir));
    config_.SetStagingDir(staging_dir);

    auto mock_exec_utils = std::make_unique<MockExecUtils>();
    mock_exec_utils_ = mock_exec_utils.get();

    metrics_ = std::make_unique<OdrMetrics>(dalvik_cache_dir_);
    odrefresh_ = std::make_unique<OnDeviceRefresh>(
        config_, dalvik_cache_dir_ + "/cache-info.xml", std::move(mock_exec_utils));
  }

  void TearDown() override {
    metrics_.reset();
    temp_dir_.reset();
    android_root_env_.reset();
    android_art_root_env_.reset();
    art_apex_data_env_.reset();

    CommonArtTest::TearDown();
  }

  std::unique_ptr<ScratchDir> temp_dir_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> android_root_env_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> android_art_root_env_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> art_apex_data_env_;
  OdrConfig config_;
  MockExecUtils* mock_exec_utils_;
  std::unique_ptr<OnDeviceRefresh> odrefresh_;
  std::unique_ptr<OdrMetrics> metrics_;
  std::string core_oj_jar_;
  std::string framework_jar_;
  std::string location_provider_jar_;
  std::string services_jar_;
  std::string services_foo_jar_;
  std::string services_bar_jar_;
  std::string dalvik_cache_dir_;
  std::string framework_dir_;
  std::string art_javalib_dir_;
  std::string framework_profile_;
  std::string art_profile_;
};

TEST_F(OdRefreshTest, BootClasspathJars) {
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", core_oj_jar_})),
                  Contains(Concatenate({"--dex-file=", framework_jar_})),
                  Contains(FlagContains("--dex-fd=", FdOf(core_oj_jar_))),
                  Contains(FlagContains("--dex-fd=", FdOf(framework_jar_))),
                  Contains(FlagContains("--profile-file-fd=", FdOf(art_profile_))),
                  Contains(FlagContains("--profile-file-fd=", FdOf(framework_profile_))),
                  Contains(Concatenate({"--oat-location=", dalvik_cache_dir_, "/x86_64/boot.oat"})),
                  Contains(HasSubstr("--base=")),
                  Contains("--compiler-filter=speed-profile"))))
      .WillOnce(Return(0));

  EXPECT_EQ(odrefresh_->Compile(*metrics_,
                                CompilationOptions{
                                    .compile_boot_classpath_for_isas = {InstructionSet::kX86_64},
                                }),
            ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, BootClasspathJarsFallback) {
  // Simulate the case where dex2oat fails when generating the full boot image.
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", core_oj_jar_})),
                                        Contains(Concatenate({"--dex-file=", framework_jar_})))))
      .Times(2)
      .WillRepeatedly(Return(1));

  // It should fall back to generating a minimal boot image.
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", core_oj_jar_})),
                                Not(Contains(Concatenate({"--dex-file=", framework_jar_}))))))
      .Times(2)
      .WillOnce(Return(0));

  EXPECT_EQ(
      odrefresh_->Compile(
          *metrics_,
          CompilationOptions{
              .compile_boot_classpath_for_isas = {InstructionSet::kX86, InstructionSet::kX86_64},
              .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
          }),
      ExitCode::kCompilationFailed);
}

TEST_F(OdRefreshTest, AllSystemServerJars) {
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                  Contains("--class-loader-context=PCL[]"))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", services_jar_})),
                  Contains(Concatenate({"--class-loader-context=PCL[", location_provider_jar_,
                                        "]"})))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", services_foo_jar_})),
                  Contains(Concatenate({"--class-loader-context=PCL[];PCL[", location_provider_jar_,
                                        ":", services_jar_, "]"})))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", services_bar_jar_})),
                  Contains(Concatenate({"--class-loader-context=PCL[];PCL[", location_provider_jar_,
                                        ":", services_jar_, "]"})))))
      .WillOnce(Return(0));

  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                              .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, PartialSystemServerJars) {
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", services_jar_})),
                  Contains(Concatenate({"--class-loader-context=PCL[", location_provider_jar_, "]"})))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(
                  Contains(Concatenate({"--dex-file=", services_bar_jar_})),
                  Contains(Concatenate({"--class-loader-context=PCL[];PCL[", location_provider_jar_,
                                        ":", services_jar_, "]"})))))
      .WillOnce(Return(0));

  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                              .system_server_jars_to_compile = {services_jar_, services_bar_jar_},
                          }),
      ExitCode::kCompilationSuccess);
}

// Verifies that odrefresh can run properly when the STANDALONE_SYSTEM_SERVER_JARS variable is
// missing, which is expected on Android S.
TEST_F(OdRefreshTest, MissingStandaloneSystemServerJars) {
  config_.SetStandaloneSystemServerJars("");
  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_)).WillRepeatedly(Return(0));
  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                              .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

// Test setup: The compiler filter is explicitly set to "speed-profile". Use it regardless of
// whether the profile exists or not. Dex2oat will fall back to "verify" if the profile doesn't
// exist.
TEST_F(OdRefreshTest, CompileSetsCompilerFilterWithExplicitValue) {
  config_.SetSystemServerCompilerFilter("speed-profile");

  // Uninteresting calls.
  EXPECT_CALL(
      *mock_exec_utils_, DoExecAndReturnCode(_))
      .Times(odrefresh_->AllSystemServerJars().size() - 2)
      .WillRepeatedly(Return(0));

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                                Not(Contains(HasSubstr("--profile-file-fd="))),
                                Contains("--compiler-filter=speed-profile"))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", services_jar_})),
                                        Contains(HasSubstr("--profile-file-fd=")),
                                        Contains("--compiler-filter=speed-profile"))))
      .WillOnce(Return(0));
  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                            .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

// Test setup: The compiler filter is not explicitly set. Use "speed-profile" if there is a profile,
// otherwise fall back to "speed".
TEST_F(OdRefreshTest, CompileSetsCompilerFilterWithDefaultValue) {
  // Uninteresting calls.
  EXPECT_CALL(
      *mock_exec_utils_, DoExecAndReturnCode(_))
      .Times(odrefresh_->AllSystemServerJars().size() - 2)
      .WillRepeatedly(Return(0));

  // services.jar has a profile, while location.provider.jar does not.
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                                Not(Contains(HasSubstr("--profile-file-fd="))),
                                Contains("--compiler-filter=speed"))))
      .WillOnce(Return(0));
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", services_jar_})),
                                Contains(HasSubstr("--profile-file-fd=")),
                                Contains("--compiler-filter=speed-profile"))))
      .WillOnce(Return(0));
  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                            .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, OutputFilesAndIsa) {
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains("--instruction-set=x86_64"),
                                Contains(HasSubstr("--image-fd=")),
                                Contains(HasSubstr("--output-vdex-fd=")),
                                Contains(HasSubstr("--oat-fd=")))))
      .WillOnce(Return(0));

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(Contains("--instruction-set=x86_64"),
                                Contains(HasSubstr("--app-image-fd=")),
                                Contains(HasSubstr("--output-vdex-fd=")),
                                Contains(HasSubstr("--oat-fd=")))))
      .Times(odrefresh_->AllSystemServerJars().size())
      .WillRepeatedly(Return(0));

  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                            .compile_boot_classpath_for_isas = {InstructionSet::kX86_64},
                            .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, CompileChoosesBootImage_OnData) {
  // Boot image is on /data.
  OdrArtifacts artifacts = OdrArtifacts::ForBootImage(dalvik_cache_dir_ + "/x86_64/boot.art");
  auto file1 = ScopedCreateEmptyFile(artifacts.ImagePath());
  auto file2 = ScopedCreateEmptyFile(artifacts.VdexPath());
  auto file3 = ScopedCreateEmptyFile(artifacts.OatPath());

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(
          Contains(Concatenate({"--boot-image=", dalvik_cache_dir_, "/boot.art"})),
          Contains(FlagContains("-Xbootclasspathimagefds:", FdOf(artifacts.ImagePath()))),
          Contains(FlagContains("-Xbootclasspathvdexfds:", FdOf(artifacts.VdexPath()))),
          Contains(FlagContains("-Xbootclasspathoatfds:", FdOf(artifacts.OatPath()))))))
      .Times(odrefresh_->AllSystemServerJars().size())
      .WillRepeatedly(Return(0));
  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                            .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, CompileChoosesBootImage_OnSystem) {
  // Boot image is on /system.
  OdrArtifacts artifacts =
      OdrArtifacts::ForBootImage(framework_dir_ + "/x86_64/boot-framework.art");
  auto file1 = ScopedCreateEmptyFile(artifacts.ImagePath());
  auto file2 = ScopedCreateEmptyFile(artifacts.VdexPath());
  auto file3 = ScopedCreateEmptyFile(artifacts.OatPath());

  // Ignore the execution for compiling the boot classpath.
  EXPECT_CALL(
      *mock_exec_utils_, DoExecAndReturnCode(Contains(HasSubstr("--image-fd="))))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(AllOf(
          Contains(Concatenate({"--boot-image=",
              GetPrebuiltPrimaryBootImageDir(), "/boot.art:",
              framework_dir_, "/boot-framework.art"})),
          Contains(FlagContains("-Xbootclasspathimagefds:", FdOf(artifacts.ImagePath()))),
          Contains(FlagContains("-Xbootclasspathvdexfds:", FdOf(artifacts.VdexPath()))),
          Contains(FlagContains("-Xbootclasspathoatfds:", FdOf(artifacts.OatPath()))))))
      .Times(odrefresh_->AllSystemServerJars().size())
      .WillRepeatedly(Return(0));
  EXPECT_EQ(
      odrefresh_->Compile(*metrics_,
                          CompilationOptions{
                            .system_server_jars_to_compile = odrefresh_->AllSystemServerJars(),
                          }),
      ExitCode::kCompilationSuccess);
}

}  // namespace odrefresh
}  // namespace art
