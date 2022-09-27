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

#include "artd.h"

#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/parseint.h"
#include "android-base/result.h"
#include "android-base/scopeguard.h"
#include "android-base/strings.h"
#include "android/binder_auto_utils.h"
#include "android/binder_status.h"
#include "base/common_art_test.h"
#include "exec_utils.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "path_utils.h"
#include "profman/profman_result.h"
#include "tools/system_properties.h"

namespace art {
namespace artd {
namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::aidl::com::android::server::art::DexMetadataPath;
using ::aidl::com::android::server::art::DexoptOptions;
using ::aidl::com::android::server::art::DexoptResult;
using ::aidl::com::android::server::art::FileVisibility;
using ::aidl::com::android::server::art::FsPermission;
using ::aidl::com::android::server::art::IArtdCancellationSignal;
using ::aidl::com::android::server::art::OutputArtifacts;
using ::aidl::com::android::server::art::OutputProfile;
using ::aidl::com::android::server::art::PriorityClass;
using ::aidl::com::android::server::art::ProfilePath;
using ::aidl::com::android::server::art::VdexPath;
using ::android::base::Error;
using ::android::base::make_scope_guard;
using ::android::base::ParseInt;
using ::android::base::ReadFileToString;
using ::android::base::Result;
using ::android::base::ScopeGuard;
using ::android::base::Split;
using ::android::base::WriteStringToFd;
using ::android::base::WriteStringToFile;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Contains;
using ::testing::ContainsRegex;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::MockFunction;
using ::testing::Not;
using ::testing::ResultOf;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::WithArg;

using RefProfilePath = ProfilePath::RefProfilePath;
using TmpRefProfilePath = ProfilePath::TmpRefProfilePath;

using ::fmt::literals::operator""_format;  // NOLINT

ScopeGuard<std::function<void()>> ScopedSetLogger(android::base::LogFunction&& logger) {
  android::base::LogFunction old_logger = android::base::SetLogger(std::move(logger));
  return make_scope_guard([old_logger = std::move(old_logger)]() mutable {
    android::base::SetLogger(std::move(old_logger));
  });
}

void CheckContent(const std::string& path, const std::string& expected_content) {
  std::string actual_content;
  ASSERT_TRUE(ReadFileToString(path, &actual_content));
  EXPECT_EQ(actual_content, expected_content);
}

// Writes `content` to the FD specified by the `flag`.
ACTION_P(WriteToFdFlag, flag, content) {
  for (const std::string& arg : arg0) {
    std::string_view value(arg);
    if (android::base::ConsumePrefix(&value, flag)) {
      int fd;
      ASSERT_TRUE(ParseInt(std::string(value), &fd));
      ASSERT_TRUE(WriteStringToFd(content, fd));
      return;
    }
  }
  FAIL() << "Flag '{}' not found"_format(flag);
}

// Matches a flag that starts with `flag` and whose value matches `matcher`.
MATCHER_P2(Flag, flag, matcher, "") {
  std::string_view value(arg);
  if (!android::base::ConsumePrefix(&value, flag)) {
    return false;
  }
  return ExplainMatchResult(matcher, std::string(value), result_listener);
}

// Matches a flag that starts with `flag` and whose value is a colon-separated list that matches
// `matcher`. The matcher acts on an `std::vector<std::string>` of the split list argument.
MATCHER_P2(ListFlag, flag, matcher, "") {
  return ExplainMatchResult(
      Flag(flag, ResultOf(std::bind(Split, std::placeholders::_1, ":"), matcher)),
      arg,
      result_listener);
}

// Matches an FD of a file whose path matches `matcher`.
MATCHER_P(FdOf, matcher, "") {
  int fd;
  if (!ParseInt(arg, &fd)) {
    return false;
  }
  std::string proc_path = "/proc/self/fd/{}"_format(fd);
  char path[PATH_MAX];
  ssize_t len = readlink(proc_path.c_str(), path, sizeof(path));
  if (len < 0) {
    return false;
  }
  return ExplainMatchResult(matcher, std::string(path, static_cast<size_t>(len)), result_listener);
}

// Matches a container that, when split by `separator`, the first part matches `head_matcher`, and
// the second part matches `tail_matcher`.
MATCHER_P3(WhenSplitBy, separator, head_matcher, tail_matcher, "") {
  using Value = const typename std::remove_reference<decltype(arg)>::type::value_type;
  auto it = std::find(arg.begin(), arg.end(), separator);
  if (it == arg.end()) {
    return false;
  }
  size_t pos = it - arg.begin();
  return ExplainMatchResult(head_matcher, ArrayRef<Value>(arg).SubArray(0, pos), result_listener) &&
         ExplainMatchResult(tail_matcher, ArrayRef<Value>(arg).SubArray(pos + 1), result_listener);
}

class MockSystemProperties : public tools::SystemProperties {
 public:
  MOCK_METHOD(std::string, GetProperty, (const std::string& key), (const, override));
};

class MockExecUtils : public ExecUtils {
 public:
  // A workaround to avoid MOCK_METHOD on a method with an `std::string*` parameter, which will lead
  // to a conflict between gmock and android-base/logging.h (b/132668253).
  ExecResult ExecAndReturnResult(const std::vector<std::string>& arg_vector,
                                 int,
                                 const ExecCallbacks& callbacks,
                                 ProcessStat* stat,
                                 std::string*) const override {
    Result<int> code = DoExecAndReturnCode(arg_vector, callbacks, stat);
    if (code.ok()) {
      return {.status = ExecResult::kExited, .exit_code = code.value()};
    }
    return {.status = ExecResult::kUnknown};
  }

  MOCK_METHOD(Result<int>,
              DoExecAndReturnCode,
              (const std::vector<std::string>& arg_vector,
               const ExecCallbacks& callbacks,
               ProcessStat* stat),
              (const));
};

class ArtdTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    auto mock_props = std::make_unique<MockSystemProperties>();
    mock_props_ = mock_props.get();
    EXPECT_CALL(*mock_props_, GetProperty).Times(AnyNumber()).WillRepeatedly(Return(""));
    auto mock_exec_utils = std::make_unique<MockExecUtils>();
    mock_exec_utils_ = mock_exec_utils.get();
    artd_ = ndk::SharedRefBase::make<Artd>(
        std::move(mock_props), std::move(mock_exec_utils), mock_kill_.AsStdFunction());
    scratch_dir_ = std::make_unique<ScratchDir>();
    scratch_path_ = scratch_dir_->GetPath();
    // Remove the trailing '/';
    scratch_path_.resize(scratch_path_.length() - 1);

    // Use an arbitrary existing directory as ART root.
    art_root_ = scratch_path_ + "/com.android.art";
    std::filesystem::create_directories(art_root_);
    setenv("ANDROID_ART_ROOT", art_root_.c_str(), /*overwrite=*/1);

    // Use an arbitrary existing directory as Android data.
    android_data_ = scratch_path_ + "/data";
    std::filesystem::create_directories(android_data_);
    setenv("ANDROID_DATA", android_data_.c_str(), /*overwrite=*/1);

    dex_file_ = scratch_path_ + "/a/b.apk";
    isa_ = "arm64";
    artifacts_path_ = ArtifactsPath{
        .dexPath = dex_file_,
        .isa = isa_,
        .isInDalvikCache = false,
    };
    struct stat st;
    ASSERT_EQ(stat(scratch_path_.c_str(), &st), 0);
    output_artifacts_ = OutputArtifacts{
        .artifactsPath = artifacts_path_,
        .permissionSettings =
            OutputArtifacts::PermissionSettings{
                .dirFsPermission =
                    FsPermission{
                        .uid = static_cast<int32_t>(st.st_uid),
                        .gid = static_cast<int32_t>(st.st_gid),
                        .isOtherReadable = true,
                        .isOtherExecutable = true,
                    },
                .fileFsPermission =
                    FsPermission{
                        .uid = static_cast<int32_t>(st.st_uid),
                        .gid = static_cast<int32_t>(st.st_gid),
                        .isOtherReadable = true,
                    },
            },
    };
    clc_1_ = GetTestDexFileName("Main");
    clc_2_ = GetTestDexFileName("Nested");
    class_loader_context_ = "PCL[{}:{}]"_format(clc_1_, clc_2_);
    compiler_filter_ = "speed";
    profile_path_ =
        TmpRefProfilePath{.refProfilePath = RefProfilePath{.packageName = "com.android.foo",
                                                           .profileName = "primary"},
                          .id = "12345"};
  }

  void TearDown() override {
    scratch_dir_.reset();
    CommonArtTest::TearDown();
  }

  void RunDexopt(binder_exception_t expected_status = EX_NONE,
                 Matcher<DexoptResult> aidl_return_matcher = Field(&DexoptResult::cancelled, false),
                 std::shared_ptr<IArtdCancellationSignal> cancellation_signal = nullptr) {
    InitDexoptInputFiles();
    if (cancellation_signal == nullptr) {
      ASSERT_TRUE(artd_->createCancellationSignal(&cancellation_signal).isOk());
    }
    DexoptResult aidl_return;
    ndk::ScopedAStatus status = artd_->dexopt(output_artifacts_,
                                              dex_file_,
                                              isa_,
                                              class_loader_context_,
                                              compiler_filter_,
                                              profile_path_,
                                              vdex_path_,
                                              priority_class_,
                                              dexopt_options_,
                                              cancellation_signal,
                                              &aidl_return);
    ASSERT_EQ(status.getExceptionCode(), expected_status) << status.getMessage();
    if (status.isOk()) {
      ASSERT_THAT(aidl_return, std::move(aidl_return_matcher));
    }
  }

  void CreateFile(const std::string& filename, const std::string& content = "") {
    std::filesystem::path path(filename);
    std::filesystem::create_directories(path.parent_path());
    WriteStringToFile(content, filename);
  }

  std::shared_ptr<Artd> artd_;
  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string scratch_path_;
  std::string art_root_;
  std::string android_data_;
  MockFunction<android::base::LogFunction> mock_logger_;
  ScopedUnsetEnvironmentVariable art_root_env_ = ScopedUnsetEnvironmentVariable("ANDROID_ART_ROOT");
  ScopedUnsetEnvironmentVariable android_data_env_ = ScopedUnsetEnvironmentVariable("ANDROID_DATA");
  MockSystemProperties* mock_props_;
  MockExecUtils* mock_exec_utils_;
  MockFunction<int(pid_t, int)> mock_kill_;

  std::string dex_file_;
  std::string isa_;
  ArtifactsPath artifacts_path_;
  OutputArtifacts output_artifacts_;
  std::string clc_1_;
  std::string clc_2_;
  std::string class_loader_context_;
  std::string compiler_filter_;
  std::optional<VdexPath> vdex_path_;
  PriorityClass priority_class_ = PriorityClass::BACKGROUND;
  DexoptOptions dexopt_options_;
  std::optional<ProfilePath> profile_path_;

 private:
  void InitDexoptInputFiles() {
    CreateFile(dex_file_);
    if (vdex_path_.has_value()) {
      if (vdex_path_->getTag() == VdexPath::dexMetadataPath) {
        CreateFile(OR_FATAL(BuildDexMetadataPath(vdex_path_.value())));
      } else {
        CreateFile(OR_FATAL(BuildVdexPath(vdex_path_.value())));
      }
    }
    if (profile_path_.has_value()) {
      CreateFile(OR_FATAL(BuildProfileOrDmPath(profile_path_.value())));
    }
  }
};

TEST_F(ArtdTest, isAlive) {
  bool result = false;
  artd_->isAlive(&result);
  EXPECT_TRUE(result);
}

TEST_F(ArtdTest, deleteArtifacts) {
  std::string oat_dir = scratch_path_ + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  WriteStringToFile("ab", oat_dir + "/b.vdex");    // 2 bytes.
  WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  int64_t result = -1;
  EXPECT_TRUE(artd_->deleteArtifacts(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, 4 + 2 + 1);

  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.odex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.vdex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.art"));
}

TEST_F(ArtdTest, deleteArtifactsMissingFile) {
  // Missing VDEX file.
  std::string oat_dir = android_data_ + "/dalvik-cache/arm64";
  std::filesystem::create_directories(oat_dir);
  WriteStringToFile("abcd", oat_dir + "/a@b.apk@classes.dex");  // 4 bytes.
  WriteStringToFile("a", oat_dir + "/a@b.apk@classes.art");     // 1 byte.

  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_, Call(_, _, _, _, _, HasSubstr("Failed to get the file size"))).Times(0);

  int64_t result = -1;
  EXPECT_TRUE(artd_
                  ->deleteArtifacts(
                      ArtifactsPath{
                          .dexPath = "/a/b.apk",
                          .isa = "arm64",
                          .isInDalvikCache = true,
                      },
                      &result)
                  .isOk());
  EXPECT_EQ(result, 4 + 1);

  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/a@b.apk@classes.dex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/a@b.apk@classes.art"));
}

TEST_F(ArtdTest, deleteArtifactsNoFile) {
  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_, Call(_, _, _, _, _, HasSubstr("Failed to get the file size"))).Times(0);

  int64_t result = -1;
  EXPECT_TRUE(artd_->deleteArtifacts(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, 0);
}

TEST_F(ArtdTest, deleteArtifactsPermissionDenied) {
  std::string oat_dir = scratch_path_ + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  WriteStringToFile("ab", oat_dir + "/b.vdex");    // 2 bytes.
  WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_, Call(_, _, _, _, _, HasSubstr("Failed to get the file size"))).Times(3);

  auto scoped_inaccessible = ScopedInaccessible(oat_dir);
  auto scoped_unroot = ScopedUnroot();

  int64_t result = -1;
  EXPECT_TRUE(artd_->deleteArtifacts(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, 0);
}

TEST_F(ArtdTest, deleteArtifactsFileIsDir) {
  // VDEX file is a directory.
  std::string oat_dir = scratch_path_ + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  std::filesystem::create_directories(oat_dir + "/b.vdex");
  WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_,
              Call(_, _, _, _, _, ContainsRegex(R"re(Failed to get the file size.*b\.vdex)re")))
      .Times(1);

  int64_t result = -1;
  EXPECT_TRUE(artd_->deleteArtifacts(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, 4 + 1);

  // The directory is kept because getting the file size failed.
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.odex"));
  EXPECT_TRUE(std::filesystem::exists(oat_dir + "/b.vdex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.art"));
}

TEST_F(ArtdTest, dexopt) {
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--",
              AllOf(Contains(art_root_ + "/bin/art_exec"), Contains("--drop-capabilities")),
              AllOf(Contains(art_root_ + "/bin/dex2oat32"),
                    Contains(Flag("--zip-fd=", FdOf(dex_file_))),
                    Contains(Flag("--zip-location=", dex_file_)),
                    Contains(Flag("--oat-location=", scratch_path_ + "/a/oat/arm64/b.odex")),
                    Contains(Flag("--instruction-set=", "arm64")),
                    Contains(Flag("--compiler-filter=", "speed")),
                    Contains(
                        Flag("--profile-file-fd=",
                             FdOf(android_data_ +
                                  "/misc/profiles/ref/com.android.foo/primary.prof.12345.tmp"))))),
          _,
          _))
      .WillOnce(DoAll(WithArg<0>(WriteToFdFlag("--oat-fd=", "oat")),
                      WithArg<0>(WriteToFdFlag("--output-vdex-fd=", "vdex")),
                      SetArgPointee<2>(ProcessStat{.wall_time_ms = 100, .cpu_time_ms = 400}),
                      Return(0)));
  RunDexopt(EX_NONE,
            AllOf(Field(&DexoptResult::cancelled, false),
                  Field(&DexoptResult::wallTimeMs, 100),
                  Field(&DexoptResult::cpuTimeMs, 400)));

  CheckContent(scratch_path_ + "/a/oat/arm64/b.odex", "oat");
  CheckContent(scratch_path_ + "/a/oat/arm64/b.vdex", "vdex");
}

TEST_F(ArtdTest, dexoptClassLoaderContext) {
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy("--",
                      _,
                      AllOf(Contains(ListFlag("--class-loader-context-fds=",
                                              ElementsAre(FdOf(clc_1_), FdOf(clc_2_)))),
                            Contains(Flag("--class-loader-context=", class_loader_context_)),
                            Contains(Flag("--classpath-dir=", scratch_path_ + "/a")))),
          _,
          _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptNoInputVdex) {
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(WhenSplitBy("--",
                                              _,
                                              AllOf(Not(Contains(Flag("--dm-fd=", _))),
                                                    Not(Contains(Flag("--input-vdex-fd=", _))))),
                                  _,
                                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptInputVdex) {
  vdex_path_ = artifacts_path_;
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              _,
                              AllOf(Not(Contains(Flag("--dm-fd=", _))),
                                    Contains(Flag("--input-vdex-fd=",
                                                  FdOf(scratch_path_ + "/a/oat/arm64/b.vdex"))))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptInputVdexDm) {
  vdex_path_ = DexMetadataPath{.dexPath = dex_file_};
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              _,
                              AllOf(Contains(Flag("--dm-fd=", FdOf(scratch_path_ + "/a/b.dm"))),
                                    Not(Contains(Flag("--input-vdex-fd=", _))))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptPriorityClassBoot) {
  priority_class_ = PriorityClass::BOOT;
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(WhenSplitBy("--",
                                              AllOf(Not(Contains(Flag("--set-task-profile=", _))),
                                                    Not(Contains(Flag("--set-priority=", _)))),
                                              Contains(Flag("--compact-dex-level=", "none"))),
                                  _,
                                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptPriorityClassInteractive) {
  priority_class_ = PriorityClass::INTERACTIVE;
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              AllOf(Contains(Flag("--set-task-profile=", "Dex2OatBootComplete")),
                                    Contains(Flag("--set-priority=", "background"))),
                              Contains(Flag("--compact-dex-level=", "none"))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptPriorityClassInteractiveFast) {
  priority_class_ = PriorityClass::INTERACTIVE_FAST;
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              AllOf(Contains(Flag("--set-task-profile=", "Dex2OatBootComplete")),
                                    Contains(Flag("--set-priority=", "background"))),
                              Contains(Flag("--compact-dex-level=", "none"))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptPriorityClassBackground) {
  priority_class_ = PriorityClass::BACKGROUND;
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              AllOf(Contains(Flag("--set-task-profile=", "Dex2OatBootComplete")),
                                    Contains(Flag("--set-priority=", "background"))),
                              Not(Contains(Flag("--compact-dex-level=", _)))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptDexoptOptions) {
  dexopt_options_ = DexoptOptions{
      .compilationReason = "install",
      .targetSdkVersion = 123,
      .debuggable = false,
      .generateAppImage = false,
      .hiddenApiPolicyEnabled = false,
  };

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(WhenSplitBy("--",
                                      _,
                                      AllOf(Contains(Flag("--compilation-reason=", "install")),
                                            Contains(Flag("-Xtarget-sdk-version:", "123")),
                                            Not(Contains("--debuggable")),
                                            Not(Contains(Flag("--app-image-fd=", _))),
                                            Not(Contains(Flag("-Xhidden-api-policy:", _))))),
                          _,
                          _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptDexoptOptions2) {
  dexopt_options_ = DexoptOptions{
      .compilationReason = "bg-dexopt",
      .targetSdkVersion = 456,
      .debuggable = true,
      .generateAppImage = true,
      .hiddenApiPolicyEnabled = true,
  };

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(WhenSplitBy("--",
                                      _,
                                      AllOf(Contains(Flag("--compilation-reason=", "bg-dexopt")),
                                            Contains(Flag("-Xtarget-sdk-version:", "456")),
                                            Contains("--debuggable"),
                                            Contains(Flag("-Xhidden-api-policy:", "enabled")))),
                          _,
                          _))
      .WillOnce(DoAll(WithArg<0>(WriteToFdFlag("--app-image-fd=", "art")), Return(0)));
  RunDexopt();

  CheckContent(scratch_path_ + "/a/oat/arm64/b.art", "art");
}

TEST_F(ArtdTest, dexoptDefaultFlagsWhenNoSystemProps) {
  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              _,
                              AllOf(Contains(Flag("--swap-fd=", FdOf(_))),
                                    Not(Contains(Flag("--instruction-set-features=", _))),
                                    Not(Contains(Flag("--instruction-set-variant=", _))),
                                    Not(Contains(Flag("--max-image-block-size=", _))),
                                    Not(Contains(Flag("--very-large-app-threshold=", _))),
                                    Not(Contains(Flag("--resolve-startup-const-strings=", _))),
                                    Not(Contains("--generate-debug-info")),
                                    Not(Contains("--generate-mini-debug-info")),
                                    Contains("-Xdeny-art-apex-data-files"),
                                    Not(Contains(Flag("--cpu-set=", _))),
                                    Not(Contains(Flag("-j", _))),
                                    Not(Contains(Flag("-Xms", _))),
                                    Not(Contains(Flag("-Xmx", _))),
                                    Not(Contains("--compile-individually")))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

TEST_F(ArtdTest, dexoptFlagsFromSystemProps) {
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-swap")).WillOnce(Return("0"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.isa.arm64.features"))
      .WillOnce(Return("features"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.isa.arm64.variant")).WillOnce(Return("variant"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-max-image-block-size"))
      .WillOnce(Return("size"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-very-large"))
      .WillOnce(Return("threshold"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-resolve-startup-strings"))
      .WillOnce(Return("strings"));
  EXPECT_CALL(*mock_props_, GetProperty("debug.generate-debug-info")).WillOnce(Return("1"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-minidebuginfo")).WillOnce(Return("1"));
  EXPECT_CALL(*mock_props_, GetProperty("odsign.verification.success")).WillOnce(Return("1"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-Xms")).WillOnce(Return("xms"));
  EXPECT_CALL(*mock_props_, GetProperty("dalvik.vm.dex2oat-Xmx")).WillOnce(Return("xmx"));
  EXPECT_CALL(*mock_props_, GetProperty("ro.config.low_ram")).WillOnce(Return("1"));

  EXPECT_CALL(*mock_exec_utils_,
              DoExecAndReturnCode(
                  WhenSplitBy("--",
                              _,
                              AllOf(Not(Contains(Flag("--swap-fd=", _))),
                                    Contains(Flag("--instruction-set-features=", "features")),
                                    Contains(Flag("--instruction-set-variant=", "variant")),
                                    Contains(Flag("--max-image-block-size=", "size")),
                                    Contains(Flag("--very-large-app-threshold=", "threshold")),
                                    Contains(Flag("--resolve-startup-const-strings=", "strings")),
                                    Contains("--generate-debug-info"),
                                    Contains("--generate-mini-debug-info"),
                                    Not(Contains("-Xdeny-art-apex-data-files")),
                                    Contains(Flag("-Xms", "xms")),
                                    Contains(Flag("-Xmx", "xmx")),
                                    Contains("--compile-individually"))),
                  _,
                  _))
      .WillOnce(Return(0));
  RunDexopt();
}

static void SetDefaultResourceControlProps(MockSystemProperties* mock_props) {
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.dex2oat-cpu-set")).WillRepeatedly(Return("0,2"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.dex2oat-threads")).WillRepeatedly(Return("4"));
}

TEST_F(ArtdTest, dexoptDefaultResourceControlBoot) {
  SetDefaultResourceControlProps(mock_props_);

  // The default resource control properties don't apply to BOOT.
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--", _, AllOf(Not(Contains(Flag("--cpu-set=", _))), Contains(Not(Flag("-j", _))))),
          _,
          _))
      .WillOnce(Return(0));
  priority_class_ = PriorityClass::BOOT;
  RunDexopt();
}

TEST_F(ArtdTest, dexoptDefaultResourceControlOther) {
  SetDefaultResourceControlProps(mock_props_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--", _, AllOf(Contains(Flag("--cpu-set=", "0,2")), Contains(Flag("-j", "4")))),
          _,
          _))
      .Times(3)
      .WillRepeatedly(Return(0));
  priority_class_ = PriorityClass::INTERACTIVE_FAST;
  RunDexopt();
  priority_class_ = PriorityClass::INTERACTIVE;
  RunDexopt();
  priority_class_ = PriorityClass::BACKGROUND;
  RunDexopt();
}

static void SetAllResourceControlProps(MockSystemProperties* mock_props) {
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.dex2oat-cpu-set")).WillRepeatedly(Return("0,2"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.dex2oat-threads")).WillRepeatedly(Return("4"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.boot-dex2oat-cpu-set"))
      .WillRepeatedly(Return("0,1,2,3"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.boot-dex2oat-threads"))
      .WillRepeatedly(Return("8"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.restore-dex2oat-cpu-set"))
      .WillRepeatedly(Return("0,2,3"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.restore-dex2oat-threads"))
      .WillRepeatedly(Return("6"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.background-dex2oat-cpu-set"))
      .WillRepeatedly(Return("0"));
  EXPECT_CALL(*mock_props, GetProperty("dalvik.vm.background-dex2oat-threads"))
      .WillRepeatedly(Return("2"));
}

TEST_F(ArtdTest, dexoptAllResourceControlBoot) {
  SetAllResourceControlProps(mock_props_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--", _, AllOf(Contains(Flag("--cpu-set=", "0,1,2,3")), Contains(Flag("-j", "8")))),
          _,
          _))
      .WillOnce(Return(0));
  priority_class_ = PriorityClass::BOOT;
  RunDexopt();
}

TEST_F(ArtdTest, dexoptAllResourceControlInteractiveFast) {
  SetAllResourceControlProps(mock_props_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--", _, AllOf(Contains(Flag("--cpu-set=", "0,2,3")), Contains(Flag("-j", "6")))),
          _,
          _))
      .WillOnce(Return(0));
  priority_class_ = PriorityClass::INTERACTIVE_FAST;
  RunDexopt();
}

TEST_F(ArtdTest, dexoptAllResourceControlInteractive) {
  SetAllResourceControlProps(mock_props_);

  // INTERACTIVE always uses the default resource control properties.
  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy(
              "--", _, AllOf(Contains(Flag("--cpu-set=", "0,2")), Contains(Flag("-j", "4")))),
          _,
          _))
      .WillOnce(Return(0));
  priority_class_ = PriorityClass::INTERACTIVE;
  RunDexopt();
}

TEST_F(ArtdTest, dexoptAllResourceControlBackground) {
  SetAllResourceControlProps(mock_props_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy("--", _, AllOf(Contains(Flag("--cpu-set=", "0")), Contains(Flag("-j", "2")))),
          _,
          _))
      .WillOnce(Return(0));
  priority_class_ = PriorityClass::BACKGROUND;
  RunDexopt();
}

TEST_F(ArtdTest, dexoptFailed) {
  dexopt_options_.generateAppImage = true;
  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce(DoAll(WithArg<0>(WriteToFdFlag("--oat-fd=", "oat")),
                      WithArg<0>(WriteToFdFlag("--output-vdex-fd=", "vdex")),
                      WithArg<0>(WriteToFdFlag("--app-image-fd=", "art")),
                      Return(1)));
  RunDexopt(EX_SERVICE_SPECIFIC);

  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.odex"));
  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.vdex"));
  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.art"));
}

TEST_F(ArtdTest, dexoptCancelledBeforeDex2oat) {
  std::shared_ptr<IArtdCancellationSignal> cancellation_signal;
  ASSERT_TRUE(artd_->createCancellationSignal(&cancellation_signal).isOk());

  constexpr pid_t kPid = 123;

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce([&](auto, const ExecCallbacks& callbacks, auto) {
        callbacks.on_start(kPid);
        callbacks.on_end(kPid);
        return Error();
      });
  EXPECT_CALL(mock_kill_, Call(kPid, SIGKILL));

  cancellation_signal->cancel();

  RunDexopt(EX_NONE, Field(&DexoptResult::cancelled, true), cancellation_signal);

  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.odex"));
  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.vdex"));
}

TEST_F(ArtdTest, dexoptCancelledDuringDex2oat) {
  std::shared_ptr<IArtdCancellationSignal> cancellation_signal;
  ASSERT_TRUE(artd_->createCancellationSignal(&cancellation_signal).isOk());

  constexpr pid_t kPid = 123;
  constexpr std::chrono::duration<int> kTimeout = std::chrono::seconds(1);

  std::condition_variable process_started_cv, process_killed_cv;
  std::mutex mu;

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce([&](auto, const ExecCallbacks& callbacks, auto) {
        std::unique_lock<std::mutex> lock(mu);
        // Step 2.
        callbacks.on_start(kPid);
        process_started_cv.notify_one();
        EXPECT_EQ(process_killed_cv.wait_for(lock, kTimeout), std::cv_status::no_timeout);
        // Step 5.
        callbacks.on_end(kPid);
        return Error();
      });

  EXPECT_CALL(mock_kill_, Call(kPid, SIGKILL)).WillOnce([&](auto, auto) {
    // Step 4.
    process_killed_cv.notify_one();
    return 0;
  });

  std::thread t;
  {
    std::unique_lock<std::mutex> lock(mu);
    // Step 1.
    t = std::thread(
        [&] { RunDexopt(EX_NONE, Field(&DexoptResult::cancelled, true), cancellation_signal); });
    EXPECT_EQ(process_started_cv.wait_for(lock, kTimeout), std::cv_status::no_timeout);
    // Step 3.
    cancellation_signal->cancel();
  }

  t.join();

  // Step 6.
  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.odex"));
  EXPECT_FALSE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.vdex"));
}

TEST_F(ArtdTest, dexoptCancelledAfterDex2oat) {
  std::shared_ptr<IArtdCancellationSignal> cancellation_signal;
  ASSERT_TRUE(artd_->createCancellationSignal(&cancellation_signal).isOk());

  constexpr pid_t kPid = 123;

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce([&](auto, const ExecCallbacks& callbacks, auto) {
        callbacks.on_start(kPid);
        callbacks.on_end(kPid);
        return 0;
      });
  EXPECT_CALL(mock_kill_, Call).Times(0);

  RunDexopt(EX_NONE, Field(&DexoptResult::cancelled, false), cancellation_signal);

  // This signal should be ignored.
  cancellation_signal->cancel();

  EXPECT_TRUE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.odex"));
  EXPECT_TRUE(std::filesystem::exists(scratch_path_ + "/a/oat/arm64/b.vdex"));
}

TEST_F(ArtdTest, isProfileUsable) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);
  CreateFile(dex_file_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy("--",
                      AllOf(Contains(art_root_ + "/bin/art_exec"), Contains("--drop-capabilities")),
                      AllOf(Contains(art_root_ + "/bin/profman"),
                            Contains(Flag("--reference-profile-file-fd=", FdOf(profile_file))),
                            Contains(Flag("--apk-fd=", FdOf(dex_file_))))),
          _,
          _))
      .WillOnce(Return(ProfmanResult::kSkipCompilationSmallDelta));

  bool result;
  EXPECT_TRUE(artd_->isProfileUsable(profile_path_.value(), dex_file_, &result).isOk());
  EXPECT_TRUE(result);
}

TEST_F(ArtdTest, isProfileUsableFalse) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);
  CreateFile(dex_file_);

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce(Return(ProfmanResult::kSkipCompilationEmptyProfiles));

  bool result;
  EXPECT_TRUE(artd_->isProfileUsable(profile_path_.value(), dex_file_, &result).isOk());
  EXPECT_FALSE(result);
}

TEST_F(ArtdTest, isProfileUsableNotFound) {
  CreateFile(dex_file_);

  bool result;
  EXPECT_TRUE(artd_->isProfileUsable(profile_path_.value(), dex_file_, &result).isOk());
  EXPECT_FALSE(result);
}

TEST_F(ArtdTest, isProfileUsableFailed) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);
  CreateFile(dex_file_);

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _)).WillOnce(Return(100));

  bool result;
  ndk::ScopedAStatus status = artd_->isProfileUsable(profile_path_.value(), dex_file_, &result);

  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(status.getMessage(), HasSubstr("profman returned an unexpected code: 100"));
}

TEST_F(ArtdTest, copyProfile) {
  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  std::string src_file = OR_FATAL(BuildTmpRefProfilePath(src));
  CreateFile(src_file, "abc");
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  EXPECT_TRUE(artd_->copyProfile(src, &dst).isOk());

  EXPECT_THAT(dst.profilePath.id, Not(IsEmpty()));
  CheckContent(OR_FATAL(BuildTmpRefProfilePath(dst.profilePath)), "abc");
}

TEST_F(ArtdTest, copyProfileFailed) {
  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  ndk::ScopedAStatus status = artd_->copyProfile(src, &dst);

  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(status.getMessage(),
              ContainsRegex(R"re(Failed to read file .*primary\.prof\.12345\.tmp)re"));
}

TEST_F(ArtdTest, copyAndRewriteProfile) {
  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  std::string src_file = OR_FATAL(BuildTmpRefProfilePath(src));
  CreateFile(src_file, "abc");
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  CreateFile(dex_file_);

  EXPECT_CALL(
      *mock_exec_utils_,
      DoExecAndReturnCode(
          WhenSplitBy("--",
                      AllOf(Contains(art_root_ + "/bin/art_exec"), Contains("--drop-capabilities")),
                      AllOf(Contains(art_root_ + "/bin/profman"),
                            Contains("--copy-and-update-profile-key"),
                            Contains(Flag("--profile-file-fd=", FdOf(src_file))),
                            Contains(Flag("--apk-fd=", FdOf(dex_file_))))),
          _,
          _))
      .WillOnce(DoAll(WithArg<0>(WriteToFdFlag("--reference-profile-file-fd=", "def")),
                      Return(ProfmanResult::kCopyAndUpdateSuccess)));

  bool result;
  EXPECT_TRUE(artd_->copyAndRewriteProfile(src, &dst, dex_file_, &result).isOk());
  EXPECT_TRUE(result);
  EXPECT_THAT(dst.profilePath.id, Not(IsEmpty()));
  CheckContent(OR_FATAL(BuildTmpRefProfilePath(dst.profilePath)), "def");
}

TEST_F(ArtdTest, copyAndRewriteProfileFalse) {
  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  std::string src_file = OR_FATAL(BuildTmpRefProfilePath(src));
  CreateFile(src_file, "abc");
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  CreateFile(dex_file_);

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _))
      .WillOnce(Return(ProfmanResult::kCopyAndUpdateNoMatch));

  bool result;
  EXPECT_TRUE(artd_->copyAndRewriteProfile(src, &dst, dex_file_, &result).isOk());
  EXPECT_FALSE(result);
}

TEST_F(ArtdTest, copyAndRewriteProfileNotFound) {
  CreateFile(dex_file_);

  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  bool result;
  EXPECT_TRUE(artd_->copyAndRewriteProfile(src, &dst, dex_file_, &result).isOk());
  EXPECT_FALSE(result);
}

TEST_F(ArtdTest, copyAndRewriteProfileFailed) {
  const TmpRefProfilePath& src = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  std::string src_file = OR_FATAL(BuildTmpRefProfilePath(src));
  CreateFile(src_file, "abc");
  OutputProfile dst{.profilePath = src, .fsPermission = FsPermission{.uid = -1, .gid = -1}};
  dst.profilePath.id = "";

  CreateFile(dex_file_);

  EXPECT_CALL(*mock_exec_utils_, DoExecAndReturnCode(_, _, _)).WillOnce(Return(100));

  bool result;
  ndk::ScopedAStatus status = artd_->copyAndRewriteProfile(src, &dst, dex_file_, &result);

  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(status.getMessage(), HasSubstr("profman returned an unexpected code: 100"));
}

TEST_F(ArtdTest, commitTmpProfile) {
  const TmpRefProfilePath& tmp_profile_path = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  std::string tmp_profile_file = OR_FATAL(BuildTmpRefProfilePath(tmp_profile_path));
  CreateFile(tmp_profile_file);

  EXPECT_TRUE(artd_->commitTmpProfile(tmp_profile_path).isOk());

  EXPECT_FALSE(std::filesystem::exists(tmp_profile_file));
  EXPECT_TRUE(
      std::filesystem::exists(OR_FATAL(BuildProfileOrDmPath(tmp_profile_path.refProfilePath))));
}

TEST_F(ArtdTest, commitTmpProfileFailed) {
  const TmpRefProfilePath& tmp_profile_path = profile_path_->get<ProfilePath::tmpRefProfilePath>();
  ndk::ScopedAStatus status = artd_->commitTmpProfile(tmp_profile_path);

  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(
      status.getMessage(),
      ContainsRegex(R"re(Failed to move .*primary\.prof\.12345\.tmp.* to .*primary\.prof)re"));

  EXPECT_FALSE(
      std::filesystem::exists(OR_FATAL(BuildProfileOrDmPath(tmp_profile_path.refProfilePath))));
}

TEST_F(ArtdTest, deleteProfile) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);

  EXPECT_TRUE(artd_->deleteProfile(profile_path_.value()).isOk());

  EXPECT_FALSE(std::filesystem::exists(profile_file));
}

TEST_F(ArtdTest, deleteProfileFailed) {
  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(
      mock_logger_,
      Call(_, _, _, _, _, ContainsRegex(R"re(Failed to remove .*primary\.prof\.12345\.tmp)re")));

  EXPECT_TRUE(artd_->deleteProfile(profile_path_.value()).isOk());
}

TEST_F(ArtdTest, getProfileVisibilityOtherReadable) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);
  std::filesystem::permissions(
      profile_file, std::filesystem::perms::others_read, std::filesystem::perm_options::add);

  FileVisibility result;
  ASSERT_TRUE(artd_->getProfileVisibility(profile_path_.value(), &result).isOk());
  EXPECT_EQ(result, FileVisibility::OTHER_READABLE);
}

TEST_F(ArtdTest, getProfileVisibilityNotOtherReadable) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);
  std::filesystem::permissions(
      profile_file, std::filesystem::perms::others_read, std::filesystem::perm_options::remove);

  FileVisibility result;
  ASSERT_TRUE(artd_->getProfileVisibility(profile_path_.value(), &result).isOk());
  EXPECT_EQ(result, FileVisibility::NOT_OTHER_READABLE);
}

TEST_F(ArtdTest, getProfileVisibilityNotFound) {
  FileVisibility result;
  ASSERT_TRUE(artd_->getProfileVisibility(profile_path_.value(), &result).isOk());
  EXPECT_EQ(result, FileVisibility::NOT_FOUND);
}

TEST_F(ArtdTest, getProfileVisibilityPermissionDenied) {
  std::string profile_file = OR_FATAL(BuildProfileOrDmPath(profile_path_.value()));
  CreateFile(profile_file);

  auto scoped_inaccessible = ScopedInaccessible(std::filesystem::path(profile_file).parent_path());
  auto scoped_unroot = ScopedUnroot();

  FileVisibility result;
  ndk::ScopedAStatus status = artd_->getProfileVisibility(profile_path_.value(), &result);
  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(status.getMessage(),
              ContainsRegex(R"re(Failed to get status of .*primary\.prof\.12345\.tmp)re"));
}

TEST_F(ArtdTest, getArtifactsVisibilityOtherReadable) {
  std::string oat_file = OR_FATAL(BuildOatPath(artifacts_path_));
  CreateFile(oat_file);
  std::filesystem::permissions(
      oat_file, std::filesystem::perms::others_read, std::filesystem::perm_options::add);

  FileVisibility result;
  ASSERT_TRUE(artd_->getArtifactsVisibility(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, FileVisibility::OTHER_READABLE);
}

TEST_F(ArtdTest, getArtifactsVisibilityNotOtherReadable) {
  std::string oat_file = OR_FATAL(BuildOatPath(artifacts_path_));
  CreateFile(oat_file);
  std::filesystem::permissions(
      oat_file, std::filesystem::perms::others_read, std::filesystem::perm_options::remove);

  FileVisibility result;
  ASSERT_TRUE(artd_->getArtifactsVisibility(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, FileVisibility::NOT_OTHER_READABLE);
}

TEST_F(ArtdTest, getArtifactsVisibilityNotFound) {
  FileVisibility result;
  ASSERT_TRUE(artd_->getArtifactsVisibility(artifacts_path_, &result).isOk());
  EXPECT_EQ(result, FileVisibility::NOT_FOUND);
}

TEST_F(ArtdTest, getArtifactsVisibilityPermissionDenied) {
  std::string oat_file = OR_FATAL(BuildOatPath(artifacts_path_));
  CreateFile(oat_file);

  auto scoped_inaccessible = ScopedInaccessible(std::filesystem::path(oat_file).parent_path());
  auto scoped_unroot = ScopedUnroot();

  FileVisibility result;
  ndk::ScopedAStatus status = artd_->getArtifactsVisibility(artifacts_path_, &result);
  EXPECT_FALSE(status.isOk());
  EXPECT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
  EXPECT_THAT(status.getMessage(), ContainsRegex(R"re(Failed to get status of .*b\.odex)re"));
}

}  // namespace
}  // namespace artd
}  // namespace art
