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

#include <functional>
#include <memory>
#include <string_view>

#include "android-base/properties.h"
#include "android-base/scopeguard.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/common_art_test.h"
#include "base/file_utils.h"
#include "exec_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "odr_common.h"
#include "odr_config.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Return;

constexpr int kReplace = 1;

void CreateEmptyFile(const std::string& name) {
  File* file = OS::CreateEmptyFile(name.c_str());
  ASSERT_TRUE(file != nullptr);
  file->Release();
  delete file;
}

android::base::ScopeGuard<std::function<void()>> ScopedSetProperty(const std::string& key,
                                                                   const std::string& value) {
  std::string old_value = android::base::GetProperty(key, /*default_value=*/{});
  android::base::SetProperty(key, value);
  return android::base::ScopeGuard([=]() { android::base::SetProperty(key, old_value); });
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

    std::string dalvik_cache_dir = art_apex_data_path + "/dalvik-cache";
    ASSERT_TRUE(EnsureDirectoryExists(dalvik_cache_dir));

    std::string framework_dir = android_root_path + "/framework";
    framework_jar_ = framework_dir + "/framework.jar";
    location_provider_jar_ = framework_dir + "/com.android.location.provider.jar";
    services_jar_ = framework_dir + "/services.jar";
    std::string services_jar_prof = framework_dir + "/services.jar.prof";
    std::string javalib_dir = android_art_root_path + "/javalib";
    std::string boot_art = javalib_dir + "/boot.art";

    // Create placeholder files.
    ASSERT_TRUE(EnsureDirectoryExists(framework_dir));
    CreateEmptyFile(framework_jar_);
    CreateEmptyFile(location_provider_jar_);
    CreateEmptyFile(services_jar_);
    CreateEmptyFile(services_jar_prof);
    ASSERT_TRUE(EnsureDirectoryExists(javalib_dir));
    CreateEmptyFile(boot_art);

    config_.SetApexInfoListFile(Concatenate({temp_dir_path, "/apex-info-list.xml"}));
    config_.SetArtBinDir(Concatenate({temp_dir_path, "/bin"}));
    config_.SetBootClasspath(framework_jar_);
    config_.SetDex2oatBootclasspath(framework_jar_);
    config_.SetSystemServerClasspath(Concatenate({location_provider_jar_, ":", services_jar_}));
    config_.SetIsa(InstructionSet::kX86_64);
    config_.SetZygoteKind(ZygoteKind::kZygote64_32);

    std::string staging_dir = dalvik_cache_dir + "/staging";
    ASSERT_TRUE(EnsureDirectoryExists(staging_dir));
    config_.SetStagingDir(staging_dir);

    auto mock_exec_utils = std::make_unique<MockExecUtils>();
    mock_exec_utils_ = mock_exec_utils.get();

    metrics_ = std::make_unique<OdrMetrics>(dalvik_cache_dir);
    odrefresh_ = std::make_unique<OnDeviceRefresh>(
        config_, dalvik_cache_dir + "/cache-info.xml", std::move(mock_exec_utils));
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
  std::unique_ptr<OdrMetrics> metrics_;
  std::unique_ptr<OnDeviceRefresh> odrefresh_;
  std::string framework_jar_;
  std::string location_provider_jar_;
  std::string services_jar_;
};

TEST_F(OdRefreshTest, OdrefreshArtifactDirectory) {
  // odrefresh.h defines kOdrefreshArtifactDirectory for external callers of odrefresh. This is
  // where compilation artifacts end up.
  ScopedUnsetEnvironmentVariable no_env("ART_APEX_DATA");
  EXPECT_EQ(kOdrefreshArtifactDirectory, GetArtApexData() + "/dalvik-cache");
}

TEST_F(OdRefreshTest, CompileSetsCompilerFilter) {
  {
    // Defaults to "speed".
    EXPECT_CALL(
        *mock_exec_utils_,
        DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                                  Not(Contains(HasSubstr("--profile-file-fd="))),
                                  Contains("--compiler-filter=speed"))))
        .WillOnce(Return(0));
    EXPECT_CALL(*mock_exec_utils_,
                DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", services_jar_})),
                                          Not(Contains(HasSubstr("--profile-file-fd="))),
                                          Contains("--compiler-filter=speed"))))
        .WillOnce(Return(0));
    EXPECT_EQ(odrefresh_->Compile(
                  *metrics_, /*compile_boot_extensions=*/{}, /*compile_system_server=*/true),
              ExitCode::kCompilationSuccess);
  }

  {
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "speed-profile");
    // services.jar has a profile, while location.provider.jar does not.
    EXPECT_CALL(
        *mock_exec_utils_,
        DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                                  Not(Contains(HasSubstr("--profile-file-fd="))),
                                  Contains("--compiler-filter=speed"))))
        .WillOnce(Return(0));
    EXPECT_CALL(*mock_exec_utils_,
                DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", services_jar_})),
                                          Contains(HasSubstr("--profile-file-fd=")),
                                          Contains("--compiler-filter=speed-profile"))))
        .WillOnce(Return(0));
    EXPECT_EQ(odrefresh_->Compile(
                  *metrics_, /*compile_boot_extensions=*/{}, /*compile_system_server=*/true),
              ExitCode::kCompilationSuccess);
  }

  {
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "verify");
    EXPECT_CALL(
        *mock_exec_utils_,
        DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", location_provider_jar_})),
                                  Not(Contains(HasSubstr("--profile-file-fd="))),
                                  Contains("--compiler-filter=verify"))))
        .WillOnce(Return(0));
    EXPECT_CALL(*mock_exec_utils_,
                DoExecAndReturnCode(AllOf(Contains(Concatenate({"--dex-file=", services_jar_})),
                                          Not(Contains(HasSubstr("--profile-file-fd="))),
                                          Contains("--compiler-filter=verify"))))
        .WillOnce(Return(0));
    EXPECT_EQ(odrefresh_->Compile(
                  *metrics_, /*compile_boot_extensions=*/{}, /*compile_system_server=*/true),
              ExitCode::kCompilationSuccess);
  }
}

}  // namespace odrefresh
}  // namespace art
