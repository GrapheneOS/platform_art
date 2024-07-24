/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "dexopt_chroot_setup.h"

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#include "aidl/com/android/server/art/BnDexoptChrootSetup.h"
#include "android-base/properties.h"
#include "android-base/scopeguard.h"
#include "android/binder_auto_utils.h"
#include "base/common_art_test.h"
#include "base/macros.h"
#include "exec_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "selinux/selinux.h"
#include "tools/binder_utils.h"
#include "tools/cmdline_builder.h"

namespace art {
namespace dexopt_chroot_setup {
namespace {

using ::android::base::ScopeGuard;
using ::android::base::WaitForProperty;
using ::art::tools::CmdlineBuilder;

class DexoptChrootSetupTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    dexopt_chroot_setup_ = ndk::SharedRefBase::make<DexoptChrootSetup>();

    // TODO(jiakaiz): Delete this one the SDK version is bumped to 35.
    char* con;
    if (getfilecon(DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR, &con) < 0) {
      ASSERT_EQ(errno, ENOENT) << ART_FORMAT("Failed to getfilecon '{}': {}",
                                             DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR,
                                             strerror(errno));
      GTEST_SKIP() << ART_FORMAT("This platform is too old and doesn't have directory '{}'",
                                 DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR);
    }
    {
      auto cleanup = ScopeGuard([&]() { freecon(con); });
      constexpr std::string_view kExpectedCon = "u:object_r:pre_reboot_dexopt_file:s0";
      if (con != kExpectedCon) {
        GTEST_SKIP() << ART_FORMAT("This platform is too old and doesn't have SELinux context '{}'",
                                   kExpectedCon);
      }
    }

    // Note that if a real Pre-reboot Dexopt is kicked off after this check, the test will still
    // fail, but that should be very rare.
    if (std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR)) {
      GTEST_SKIP() << "A real Pre-reboot Dexopt is running";
    }

    ASSERT_TRUE(WaitForProperty("dev.bootcomplete", "1", /*relative_timeout=*/std::chrono::minutes(3)));

    test_skipped = false;

    scratch_dir_ = std::make_unique<ScratchDir>();
    scratch_path_ = scratch_dir_->GetPath();
    // Remove the trailing '/';
    scratch_path_.resize(scratch_path_.length() - 1);
  }

  void TearDown() override {
    if (test_skipped) {
      return;
    }
    scratch_dir_.reset();
    dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false);
    CommonArtTest::TearDown();
  }

  std::shared_ptr<DexoptChrootSetup> dexopt_chroot_setup_;
  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string scratch_path_;
  bool test_skipped = true;
};

TEST_F(DexoptChrootSetupTest, Run) {
  // We only test the Mainline update case here. There isn't an easy way to test the OTA update case
  // in such a unit test. The OTA update case is assumed to be covered by the E2E test.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->init());

  // Some important dirs that should be the same as outside.
  std::vector<const char*> same_dirs = {
      "/",
      "/system",
      "/system_ext",
      "/vendor",
      "/product",
      "/data",
      "/mnt/expand",
      "/dev",
      "/dev/cpuctl",
      "/dev/cpuset",
      "/proc",
      "/sys",
      "/sys/fs/cgroup",
      "/sys/fs/selinux",
      "/metadata",
  };

  for (const std::string& dir : same_dirs) {
    struct stat st_outside;
    ASSERT_EQ(stat(dir.c_str(), &st_outside), 0);
    struct stat st_inside;
    ASSERT_EQ(stat(PathInChroot(dir).c_str(), &st_inside), 0);
    EXPECT_EQ(st_outside.st_dev, st_inside.st_dev);
    EXPECT_EQ(st_outside.st_ino, st_inside.st_ino);
  }

  // Some important dirs that are expected to be writable.
  std::vector<const char*> writable_dirs = {
      "/data",
      "/mnt/expand",
  };

  for (const std::string& dir : writable_dirs) {
    EXPECT_EQ(access(PathInChroot(dir).c_str(), W_OK), 0);
  }

  // Some important dirs that are not the same as outside but should be prepared.
  std::vector<const char*> prepared_dirs = {
      "/apex/com.android.art",
      "/linkerconfig/com.android.art",
  };

  for (const std::string& dir : prepared_dirs) {
    EXPECT_FALSE(std::filesystem::is_empty(PathInChroot(dir)));
  }

  EXPECT_TRUE(std::filesystem::is_directory(PathInChroot("/mnt/artd_tmp")));

  // Check that the chroot environment is capable to run programs. `dex2oat` is arbitrarily picked
  // here. The test dex file and the scratch dir in /data are the same inside the chroot as outside.
  CmdlineBuilder args;
  args.Add(GetArtBinDir() + "/art_exec")
      .Add("--chroot=%s", DexoptChrootSetup::CHROOT_DIR)
      .Add("--")
      .Add(GetArtBinDir() + "/dex2oat" + (Is64BitInstructionSet(kRuntimeISA) ? "64" : "32"))
      .Add("--dex-file=%s", GetTestDexFileName("Main"))
      .Add("--oat-file=%s", scratch_path_ + "/output.odex")
      .Add("--output-vdex=%s", scratch_path_ + "/output.vdex")
      .Add("--compiler-filter=speed")
      .Add("--boot-image=/nonx/boot.art");
  std::string error_msg;
  EXPECT_TRUE(Exec(args.Get(), &error_msg)) << error_msg;

  // Check that `setUp` can be repetitively called, to simulate the case where an instance of the
  // caller (typically system_server) called `setUp` and crashed later, and a new instance called
  // `setUp` again.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->init());

  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));

  EXPECT_FALSE(std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR));

  // Check that `tearDown` can be repetitively called too.
  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));

  // Check that `setUp` can be followed directly by a `tearDown`.
  ASSERT_STATUS_OK(
      dexopt_chroot_setup_->setUp(/*in_otaSlot=*/std::nullopt, /*in_mapSnapshotsForOta=*/false));
  ASSERT_STATUS_OK(dexopt_chroot_setup_->tearDown(/*in_allowConcurrent=*/false));
  EXPECT_FALSE(std::filesystem::exists(DexoptChrootSetup::CHROOT_DIR));
}

}  // namespace
}  // namespace dexopt_chroot_setup
}  // namespace art
