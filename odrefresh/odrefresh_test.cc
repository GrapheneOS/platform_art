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

#include "aidl/com/android/art/CompilerFilter.h"
#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"
#include "aidl/com/android/art/Isa.h"
#include "android-base/parseint.h"
#include "android-base/properties.h"
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
#include "odr_dexopt.h"
#include "odr_fs_utils.h"
#include "odr_metrics.h"
#include "odrefresh/odrefresh.h"

namespace art {
namespace odrefresh {

using ::aidl::com::android::art::CompilerFilter;
using ::aidl::com::android::art::DexoptBcpExtArgs;
using ::aidl::com::android::art::DexoptSystemServerArgs;
using ::aidl::com::android::art::Isa;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::IsEmpty;
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

android::base::ScopeGuard<std::function<void()>> ScopedSetProperty(const std::string& key,
                                                                   const std::string& value) {
  std::string old_value = android::base::GetProperty(key, /*default_value=*/{});
  android::base::SetProperty(key, value);
  return android::base::ScopeGuard([=]() { android::base::SetProperty(key, old_value); });
}

class MockOdrDexopt : public OdrDexopt {
 public:
  // A workaround to avoid MOCK_METHOD on a method with an `std::string*` parameter, which will lead
  // to a conflict between gmock and android-base/logging.h (b/132668253).
  int DexoptBcpExtension(const DexoptBcpExtArgs& args, time_t, bool*, std::string*) override {
    return DoDexoptBcpExtension(args);
  }

  int DexoptSystemServer(const DexoptSystemServerArgs& args, time_t, bool*, std::string*) override {
    return DoDexoptSystemServer(args);
  }

  MOCK_METHOD(int, DoDexoptBcpExtension, (const DexoptBcpExtArgs&));
  MOCK_METHOD(int, DoDexoptSystemServer, (const DexoptSystemServerArgs&));
};

// Matches an FD of a file whose path matches `matcher`.
MATCHER_P(FdOf, matcher, "") {
  char path[PATH_MAX];
  int fd = arg;
  std::string proc_path = android::base::StringPrintf("/proc/self/fd/%d", fd);
  ssize_t len = readlink(proc_path.c_str(), path, sizeof(path));
  if (len < 0) {
    return false;
  }
  std::string path_str{path, static_cast<size_t>(len)};
  return ExplainMatchResult(matcher, path_str, result_listener);
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
    boot_profile_file_ = system_etc_dir + "/boot-image.prof";
    CreateEmptyFile(boot_profile_file_);

    framework_dir_ = android_root_path + "/framework";
    framework_jar_ = framework_dir_ + "/framework.jar";
    location_provider_jar_ = framework_dir_ + "/com.android.location.provider.jar";
    services_jar_ = framework_dir_ + "/services.jar";
    services_foo_jar_ = framework_dir_ + "/services-foo.jar";
    services_bar_jar_ = framework_dir_ + "/services-bar.jar";
    std::string services_jar_prof = framework_dir_ + "/services.jar.prof";
    std::string javalib_dir = android_art_root_path + "/javalib";
    std::string boot_art = javalib_dir + "/boot.art";

    // Create placeholder files.
    ASSERT_TRUE(EnsureDirectoryExists(framework_dir_ + "/x86_64"));
    CreateEmptyFile(framework_jar_);
    CreateEmptyFile(location_provider_jar_);
    CreateEmptyFile(services_jar_);
    CreateEmptyFile(services_foo_jar_);
    CreateEmptyFile(services_bar_jar_);
    CreateEmptyFile(services_jar_prof);
    ASSERT_TRUE(EnsureDirectoryExists(javalib_dir));
    CreateEmptyFile(boot_art);

    config_.SetApexInfoListFile(Concatenate({temp_dir_path, "/apex-info-list.xml"}));
    config_.SetArtBinDir(Concatenate({temp_dir_path, "/bin"}));
    config_.SetBootClasspath(framework_jar_);
    config_.SetDex2oatBootclasspath(framework_jar_);
    config_.SetSystemServerClasspath(Concatenate({location_provider_jar_, ":", services_jar_}));
    config_.SetStandaloneSystemServerJars(Concatenate({services_foo_jar_, ":", services_bar_jar_}));
    config_.SetIsa(InstructionSet::kX86_64);
    config_.SetZygoteKind(ZygoteKind::kZygote64_32);

    std::string staging_dir = dalvik_cache_dir_ + "/staging";
    ASSERT_TRUE(EnsureDirectoryExists(staging_dir));
    config_.SetStagingDir(staging_dir);

    metrics_ = std::make_unique<OdrMetrics>(dalvik_cache_dir_);
  }

  void TearDown() override {
    metrics_.reset();
    temp_dir_.reset();
    android_root_env_.reset();
    android_art_root_env_.reset();
    art_apex_data_env_.reset();

    CommonArtTest::TearDown();
  }

  std::pair<std::unique_ptr<OnDeviceRefresh>, MockOdrDexopt*> CreateOdRefresh() {
    auto mock_odr_dexopt = std::make_unique<MockOdrDexopt>();
    MockOdrDexopt* mock_odr_dexopt_ptr = mock_odr_dexopt.get();
    auto odrefresh = std::make_unique<OnDeviceRefresh>(config_,
                                                       dalvik_cache_dir_ + "/cache-info.xml",
                                                       std::make_unique<ExecUtils>(),
                                                       std::move(mock_odr_dexopt));
    return std::make_pair(std::move(odrefresh), mock_odr_dexopt_ptr);
  }

  std::unique_ptr<ScratchDir> temp_dir_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> android_root_env_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> android_art_root_env_;
  std::unique_ptr<ScopedUnsetEnvironmentVariable> art_apex_data_env_;
  OdrConfig config_;
  std::unique_ptr<OdrMetrics> metrics_;
  std::string framework_jar_;
  std::string location_provider_jar_;
  std::string services_jar_;
  std::string services_foo_jar_;
  std::string services_bar_jar_;
  std::string dalvik_cache_dir_;
  std::string framework_dir_;
  std::string boot_profile_file_;
};

TEST_F(OdRefreshTest, AllSystemServerJars) {
  auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptSystemServer(
                  AllOf(Field(&DexoptSystemServerArgs::dexPath, Eq(location_provider_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContext, IsEmpty()),
                        Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(false)))))
      .WillOnce(Return(0));
  EXPECT_CALL(
      *mock_odr_dexopt,
      DoDexoptSystemServer(AllOf(
          Field(&DexoptSystemServerArgs::dexPath, Eq(services_jar_)),
          Field(&DexoptSystemServerArgs::classloaderContext, ElementsAre(location_provider_jar_)),
          Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(false)))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptSystemServer(
                  AllOf(Field(&DexoptSystemServerArgs::dexPath, Eq(services_foo_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContext,
                              ElementsAre(location_provider_jar_, services_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(true)))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptSystemServer(
                  AllOf(Field(&DexoptSystemServerArgs::dexPath, Eq(services_bar_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContext,
                              ElementsAre(location_provider_jar_, services_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(true)))))
      .WillOnce(Return(0));

  EXPECT_EQ(
      odrefresh->Compile(*metrics_,
                         CompilationOptions{
                             .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                         }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, PartialSystemServerJars) {
  auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

  EXPECT_CALL(
      *mock_odr_dexopt,
      DoDexoptSystemServer(AllOf(
          Field(&DexoptSystemServerArgs::dexPath, Eq(services_jar_)),
          Field(&DexoptSystemServerArgs::classloaderContext, ElementsAre(location_provider_jar_)),
          Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(false)))))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptSystemServer(
                  AllOf(Field(&DexoptSystemServerArgs::dexPath, Eq(services_bar_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContext,
                              ElementsAre(location_provider_jar_, services_jar_)),
                        Field(&DexoptSystemServerArgs::classloaderContextAsParent, Eq(true)))))
      .WillOnce(Return(0));

  EXPECT_EQ(
      odrefresh->Compile(*metrics_,
                         CompilationOptions{
                             .system_server_jars_to_compile = {services_jar_, services_bar_jar_},
                         }),
      ExitCode::kCompilationSuccess);
}

// Verifies that odrefresh can run properly when the STANDALONE_SYSTEM_SERVER_JARS variable is
// missing, which is expected on Android S.
TEST_F(OdRefreshTest, MissingStandaloneSystemServerJars) {
  config_.SetStandaloneSystemServerJars("");
  auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();
  EXPECT_EQ(
      odrefresh->Compile(*metrics_,
                         CompilationOptions{
                             .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                         }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, CompileSetsCompilerFilter) {
  {
    // Check if the system property can be written.
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "foo");
    if (android::base::GetProperty("dalvik.vm.systemservercompilerfilter", /*default_value=*/{}) !=
        "foo") {
      // This test depends on a system property that doesn't exist on old platforms. Since the whole
      // odrefresh program is for S and later, we don't need to run the test on old platforms.
      return;
    }
  }

  {
    auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

    // Test setup: default compiler filter should be "speed".
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "");

    // Uninteresting calls.
    EXPECT_CALL(*mock_odr_dexopt, DoDexoptSystemServer(_))
        .Times(odrefresh->AllSystemServerJars().size() - 2)
        .WillRepeatedly(Return(0))
        .RetiresOnSaturation();

    EXPECT_CALL(*mock_odr_dexopt,
                DoDexoptSystemServer(AllOf(
                    Field(&DexoptSystemServerArgs::dexPath, Eq(location_provider_jar_)),
                    Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::SPEED)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_CALL(*mock_odr_dexopt,
                DoDexoptSystemServer(AllOf(
                    Field(&DexoptSystemServerArgs::dexPath, Eq(services_jar_)),
                    Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::SPEED)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_EQ(
        odrefresh->Compile(*metrics_,
                           CompilationOptions{
                               .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                           }),
        ExitCode::kCompilationSuccess);
  }

  {
    auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

    // Test setup: with "speed-profile" compiler filter in the request, only apply if there is a
    // profile, otherwise fallback to speed.
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "speed-profile");

    // Uninteresting calls.
    EXPECT_CALL(*mock_odr_dexopt, DoDexoptSystemServer(_))
        .Times(odrefresh->AllSystemServerJars().size() - 2)
        .WillRepeatedly(Return(0))
        .RetiresOnSaturation();

    // services.jar has a profile, while location.provider.jar does not.
    EXPECT_CALL(
        *mock_odr_dexopt,
        DoDexoptSystemServer(AllOf(
            Field(&DexoptSystemServerArgs::dexPath, Eq(services_jar_)),
            Field(&DexoptSystemServerArgs::profileFd, Ge(0)),
            Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::SPEED_PROFILE)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_CALL(*mock_odr_dexopt,
                DoDexoptSystemServer(AllOf(
                    Field(&DexoptSystemServerArgs::dexPath, Eq(location_provider_jar_)),
                    Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::SPEED)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_EQ(
        odrefresh->Compile(*metrics_,
                           CompilationOptions{
                               .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                           }),
        ExitCode::kCompilationSuccess);
  }

  {
    auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

    // Test setup: "verify" compiler filter should be simply applied.
    auto guard = ScopedSetProperty("dalvik.vm.systemservercompilerfilter", "verify");

    // Uninteresting calls.
    EXPECT_CALL(*mock_odr_dexopt, DoDexoptSystemServer(_))
        .Times(odrefresh->AllSystemServerJars().size() - 2)
        .WillRepeatedly(Return(0))
        .RetiresOnSaturation();

    EXPECT_CALL(*mock_odr_dexopt,
                DoDexoptSystemServer(AllOf(
                    Field(&DexoptSystemServerArgs::dexPath, Eq(location_provider_jar_)),
                    Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::VERIFY)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_CALL(*mock_odr_dexopt,
                DoDexoptSystemServer(AllOf(
                    Field(&DexoptSystemServerArgs::dexPath, Eq(services_jar_)),
                    Field(&DexoptSystemServerArgs::compilerFilter, Eq(CompilerFilter::VERIFY)))))
        .WillOnce(Return(0))
        .RetiresOnSaturation();
    EXPECT_EQ(
        odrefresh->Compile(*metrics_,
                           CompilationOptions{
                               .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                           }),
        ExitCode::kCompilationSuccess);
  }
}

TEST_F(OdRefreshTest, OutputFilesAndIsa) {
  auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptBcpExtension(AllOf(Field(&DexoptBcpExtArgs::isa, Eq(Isa::X86_64)),
                                         Field(&DexoptBcpExtArgs::imageFd, Ge(0)),
                                         Field(&DexoptBcpExtArgs::vdexFd, Ge(0)),
                                         Field(&DexoptBcpExtArgs::oatFd, Ge(0)))))
      .WillOnce(Return(0));

  EXPECT_CALL(*mock_odr_dexopt,
              DoDexoptSystemServer(AllOf(Field(&DexoptSystemServerArgs::isa, Eq(Isa::X86_64)),
                                         Field(&DexoptSystemServerArgs::imageFd, Ge(0)),
                                         Field(&DexoptSystemServerArgs::vdexFd, Ge(0)),
                                         Field(&DexoptSystemServerArgs::oatFd, Ge(0)))))
      .Times(odrefresh->AllSystemServerJars().size())
      .WillRepeatedly(Return(0));

  EXPECT_EQ(
      odrefresh->Compile(*metrics_,
                         CompilationOptions{
                             .compile_boot_extensions_for_isas = {InstructionSet::kX86_64},
                             .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                         }),
      ExitCode::kCompilationSuccess);
}

TEST_F(OdRefreshTest, CompileChoosesBootImage) {
  {
    auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

    // Boot image is on /data.
    OdrArtifacts artifacts =
        OdrArtifacts::ForBootImageExtension(dalvik_cache_dir_ + "/x86_64/boot-framework.art");
    auto file1 = ScopedCreateEmptyFile(artifacts.ImagePath());
    auto file2 = ScopedCreateEmptyFile(artifacts.VdexPath());
    auto file3 = ScopedCreateEmptyFile(artifacts.OatPath());

    EXPECT_CALL(
        *mock_odr_dexopt,
        DoDexoptSystemServer(AllOf(Field(&DexoptSystemServerArgs::isBootImageOnSystem, Eq(false)),
                                   Field(&DexoptSystemServerArgs::bootClasspathImageFds,
                                         Contains(FdOf(artifacts.ImagePath()))),
                                   Field(&DexoptSystemServerArgs::bootClasspathVdexFds,
                                         Contains(FdOf(artifacts.VdexPath()))),
                                   Field(&DexoptSystemServerArgs::bootClasspathOatFds,
                                         Contains(FdOf(artifacts.OatPath()))))))
        .Times(odrefresh->AllSystemServerJars().size())
        .WillRepeatedly(Return(0));
    EXPECT_EQ(
        odrefresh->Compile(*metrics_,
                           CompilationOptions{
                               .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                           }),
        ExitCode::kCompilationSuccess);
  }

  {
    auto [odrefresh, mock_odr_dexopt] = CreateOdRefresh();

    // Boot image is on /system.
    OdrArtifacts artifacts =
        OdrArtifacts::ForBootImageExtension(framework_dir_ + "/x86_64/boot-framework.art");
    auto file1 = ScopedCreateEmptyFile(artifacts.ImagePath());
    auto file2 = ScopedCreateEmptyFile(artifacts.VdexPath());
    auto file3 = ScopedCreateEmptyFile(artifacts.OatPath());

    EXPECT_CALL(
        *mock_odr_dexopt,
        DoDexoptSystemServer(AllOf(Field(&DexoptSystemServerArgs::isBootImageOnSystem, Eq(true)),
                                   Field(&DexoptSystemServerArgs::bootClasspathImageFds,
                                         Contains(FdOf(artifacts.ImagePath()))),
                                   Field(&DexoptSystemServerArgs::bootClasspathVdexFds,
                                         Contains(FdOf(artifacts.VdexPath()))),
                                   Field(&DexoptSystemServerArgs::bootClasspathOatFds,
                                         Contains(FdOf(artifacts.OatPath()))))))
        .Times(odrefresh->AllSystemServerJars().size())
        .WillRepeatedly(Return(0));
    EXPECT_EQ(
        odrefresh->Compile(*metrics_,
                           CompilationOptions{
                               .system_server_jars_to_compile = odrefresh->AllSystemServerJars(),
                           }),
        ExitCode::kCompilationSuccess);
  }
}

}  // namespace odrefresh
}  // namespace art
