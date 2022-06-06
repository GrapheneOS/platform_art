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

#include <sys/capability.h>

#include <filesystem>
#include <functional>
#include <memory>

#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/scopeguard.h"
#include "android/binder_interface_utils.h"
#include "base/common_art_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art {
namespace artd {
namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::android::base::make_scope_guard;
using ::android::base::ScopeGuard;
using ::testing::_;
using ::testing::ContainsRegex;
using ::testing::HasSubstr;
using ::testing::MockFunction;

// A wrapper of `cap_t` that automatically calls `cap_free`.
class ScopedCap {
 public:
  explicit ScopedCap(cap_t cap) : cap_(cap) { CHECK_NE(cap, nullptr); }

  ScopedCap(ScopedCap&& other) : cap_(std::exchange(other.cap_, nullptr)) {}

  ~ScopedCap() {
    if (cap_ != nullptr) {
      CHECK_EQ(cap_free(cap_), 0);
    }
  }

  cap_t Get() const { return cap_; }

 private:
  cap_t cap_;
};

// Temporarily drops all root capabilities when the test is run as root. This is a noop otherwise.
ScopeGuard<std::function<void()>> ScopedUnroot() {
  ScopedCap old_cap(cap_get_proc());
  ScopedCap new_cap(cap_dup(old_cap.Get()));
  CHECK_EQ(cap_clear_flag(new_cap.Get(), CAP_EFFECTIVE), 0);
  CHECK_EQ(cap_set_proc(new_cap.Get()), 0);
  // `old_cap` is actually not shared with anyone else, but we have to wrap it with a `shared_ptr`
  // because `std::function` requires captures to be copyable.
  return make_scope_guard([old_cap = std::make_shared<ScopedCap>(std::move(old_cap))]() {
    CHECK_EQ(cap_set_proc(old_cap->Get()), 0);
  });
}

// Temporarily drops all permission on a file/directory.
ScopeGuard<std::function<void()>> ScopedInaccessible(const std::string& path) {
  std::filesystem::perms old_perms = std::filesystem::status(path).permissions();
  std::filesystem::permissions(path, std::filesystem::perms::none);
  return make_scope_guard([=]() { std::filesystem::permissions(path, old_perms); });
}

ScopeGuard<std::function<void()>> ScopedSetLogger(android::base::LogFunction&& logger) {
  android::base::LogFunction old_logger = android::base::SetLogger(std::move(logger));
  return make_scope_guard([old_logger = std::move(old_logger)]() mutable {
    android::base::SetLogger(std::move(old_logger));
  });
}

class ArtdTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    artd_ = ndk::SharedRefBase::make<Artd>();
    scratch_dir_ = std::make_unique<ScratchDir>();
  }

  void TearDown() override {
    scratch_dir_.reset();
    CommonArtTest::TearDown();
  }

  std::shared_ptr<Artd> artd_;
  std::unique_ptr<ScratchDir> scratch_dir_;
  MockFunction<android::base::LogFunction> mock_logger_;
};

TEST_F(ArtdTest, isAlive) {
  bool result = false;
  artd_->isAlive(&result);
  EXPECT_TRUE(result);
}

TEST_F(ArtdTest, deleteArtifacts) {
  std::string oat_dir = scratch_dir_->GetPath() + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  android::base::WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  android::base::WriteStringToFile("ab", oat_dir + "/b.vdex");    // 2 bytes.
  android::base::WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  int64_t result = -1;
  EXPECT_TRUE(artd_
                  ->deleteArtifacts(
                      ArtifactsPath{
                          .dexPath = scratch_dir_->GetPath() + "/a/b.apk",
                          .isa = "arm64",
                          .isInDalvikCache = false,
                      },
                      &result)
                  .isOk());
  EXPECT_EQ(result, 4 + 2 + 1);

  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.odex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.vdex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.art"));
}

TEST_F(ArtdTest, deleteArtifactsMissingFile) {
  // Missing VDEX file.
  std::string oat_dir = dalvik_cache_ + "/arm64";
  std::filesystem::create_directories(oat_dir);
  android::base::WriteStringToFile("abcd", oat_dir + "/a@b.apk@classes.dex");  // 4 bytes.
  android::base::WriteStringToFile("a", oat_dir + "/a@b.apk@classes.art");     // 1 byte.

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
  EXPECT_TRUE(artd_
                  ->deleteArtifacts(
                      ArtifactsPath{
                          .dexPath = android_data_ + "/a/b.apk",
                          .isa = "arm64",
                          .isInDalvikCache = false,
                      },
                      &result)
                  .isOk());
  EXPECT_EQ(result, 0);
}

TEST_F(ArtdTest, deleteArtifactsPermissionDenied) {
  std::string oat_dir = scratch_dir_->GetPath() + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  android::base::WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  android::base::WriteStringToFile("ab", oat_dir + "/b.vdex");    // 2 bytes.
  android::base::WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_, Call(_, _, _, _, _, HasSubstr("Failed to get the file size"))).Times(3);

  auto scoped_inaccessible = ScopedInaccessible(oat_dir);
  auto scoped_unroot = ScopedUnroot();

  int64_t result = -1;
  EXPECT_TRUE(artd_
                  ->deleteArtifacts(
                      ArtifactsPath{
                          .dexPath = scratch_dir_->GetPath() + "/a/b.apk",
                          .isa = "arm64",
                          .isInDalvikCache = false,
                      },
                      &result)
                  .isOk());
  EXPECT_EQ(result, 0);
}

TEST_F(ArtdTest, deleteArtifactsFileIsDir) {
  // VDEX file is a directory.
  std::string oat_dir = scratch_dir_->GetPath() + "/a/oat/arm64";
  std::filesystem::create_directories(oat_dir);
  std::filesystem::create_directories(oat_dir + "/b.vdex");
  android::base::WriteStringToFile("abcd", oat_dir + "/b.odex");  // 4 bytes.
  android::base::WriteStringToFile("a", oat_dir + "/b.art");      // 1 byte.

  auto scoped_set_logger = ScopedSetLogger(mock_logger_.AsStdFunction());
  EXPECT_CALL(mock_logger_,
              Call(_, _, _, _, _, ContainsRegex(R"re(Failed to get the file size.*b\.vdex)re")))
      .Times(1);

  int64_t result = -1;
  EXPECT_TRUE(artd_
                  ->deleteArtifacts(
                      ArtifactsPath{
                          .dexPath = scratch_dir_->GetPath() + "/a/b.apk",
                          .isa = "arm64",
                          .isInDalvikCache = false,
                      },
                      &result)
                  .isOk());
  EXPECT_EQ(result, 4 + 1);

  // The directory is kept because getting the file size failed.
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.odex"));
  EXPECT_TRUE(std::filesystem::exists(oat_dir + "/b.vdex"));
  EXPECT_FALSE(std::filesystem::exists(oat_dir + "/b.art"));
}

}  // namespace
}  // namespace artd
}  // namespace art
