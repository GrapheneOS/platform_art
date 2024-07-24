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

#include <linux/mount.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "aidl/com/android/server/art/BnDexoptChrootSetup.h"
#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/no_destructor.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "android-base/strings.h"
#include "android/binder_auto_utils.h"
#include "android/binder_manager.h"
#include "android/binder_process.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "base/os.h"
#include "exec_utils.h"
#include "fstab/fstab.h"
#include "tools/binder_utils.h"
#include "tools/cmdline_builder.h"
#include "tools/tools.h"

namespace art {
namespace dexopt_chroot_setup {

namespace {

using ::android::base::ConsumePrefix;
using ::android::base::Error;
using ::android::base::Join;
using ::android::base::NoDestructor;
using ::android::base::ReadFileToString;
using ::android::base::Result;
using ::android::base::SetProperty;
using ::android::base::Split;
using ::android::base::Tokenize;
using ::android::base::WaitForProperty;
using ::android::base::WriteStringToFile;
using ::android::fs_mgr::FstabEntry;
using ::art::tools::CmdlineBuilder;
using ::art::tools::Fatal;
using ::art::tools::GetProcMountsDescendantsOfPath;
using ::art::tools::NonFatal;
using ::art::tools::PathStartsWith;
using ::ndk::ScopedAStatus;

constexpr const char* kServiceName = "dexopt_chroot_setup";
const NoDestructor<std::string> kBindMountTmpDir(
    std::string(DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR) + "/mount_tmp");
const NoDestructor<std::string> kOtaSlotFile(std::string(DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR) +
                                             "/ota_slot");
const NoDestructor<std::string> kSnapshotMappedFile(
    std::string(DexoptChrootSetup::PRE_REBOOT_DEXOPT_DIR) + "/snapshot_mapped");
constexpr mode_t kChrootDefaultMode = 0755;
constexpr std::chrono::milliseconds kSnapshotCtlTimeout = std::chrono::seconds(60);

bool IsOtaUpdate(const std::optional<std::string>& ota_slot) { return ota_slot.has_value(); }

Result<void> Run(std::string_view log_name, const std::vector<std::string>& args) {
  LOG(INFO) << "Running " << log_name << ": " << Join(args, /*separator=*/" ");

  std::string error_msg;
  if (!Exec(args, &error_msg)) {
    return Errorf("Failed to run {}: {}", log_name, error_msg);
  }

  LOG(INFO) << log_name << " returned code 0";
  return {};
}

Result<CmdlineBuilder> GetArtExecCmdlineBuilder() {
  std::string error_msg;
  std::string art_root = GetArtRootSafe(&error_msg);
  if (!error_msg.empty()) {
    return Error() << error_msg;
  }
  CmdlineBuilder args;
  args.Add(art_root + "/bin/art_exec")
      .Add("--chroot=%s", DexoptChrootSetup::CHROOT_DIR)
      .Add("--process-name-suffix=Pre-reboot Dexopt chroot");
  return args;
}

Result<void> CreateDir(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directory(path, ec);
  if (ec) {
    return Errorf("Failed to create dir '{}': {}", path, ec.message());
  }
  return {};
}

Result<void> Unmount(const std::string& target) {
  if (umount2(target.c_str(), UMOUNT_NOFOLLOW) == 0) {
    return {};
  }
  LOG(WARNING) << ART_FORMAT(
      "Failed to umount2 '{}': {}. Retrying with MNT_DETACH", target, strerror(errno));
  if (umount2(target.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) == 0) {
    return {};
  }
  return ErrnoErrorf("Failed to umount2 '{}'", target);
}

Result<void> BindMount(const std::string& source, const std::string& target) {
  // Don't bind-mount repeatedly.
  CHECK(!PathStartsWith(source, DexoptChrootSetup::CHROOT_DIR));
  // system_server has a different mount namespace from init, and it uses slave mounts. E.g:
  //
  //    a: init mount ns: shared(1):          /foo
  //    b: init mount ns: shared(2):          /mnt
  //    c: SS mount ns:   slave(1):           /foo
  //    d: SS mount ns:   slave(2):           /mnt
  //
  // We create our chroot setup in the init namespace but also want it to appear inside the
  // system_server one, since we need to access some files in it from system_server (in particular
  // service-art.jar).
  //
  // Hence we want the mount propagation type to be "slave+shared": Slave of the init namespace so
  // that unmounts in the chroot doesn't affect the rest of the system, while at the same time
  // shared with the system_server namespace so that it gets the same mounts recursively in the
  // chroot tree. This can be achieved in 4 steps:
  //
  // 1. Bind-mount /foo at a temp mount point /mnt/pre_reboot_dexopt/mount_tmp.
  //    a: init mount ns: shared(1):          /foo
  //    b: init mount ns: shared(2):          /mnt
  //    e: init mount ns: shared(1):          /mnt/pre_reboot_dexopt/mount_tmp
  //    c: SS mount ns:   slave(1):           /foo
  //    d: SS mount ns:   slave(2):           /mnt
  //    f: SS mount ns:   slave(1):           /mnt/pre_reboot_dexopt/mount_tmp
  //
  // 2. Make the temp mount point slave.
  //    a: init mount ns: shared(1):          /foo
  //    b: init mount ns: shared(2):          /mnt
  //    e: init mount ns: slave(1):           /mnt/pre_reboot_dexopt/mount_tmp
  //    c: SS mount ns:   slave(1):           /foo
  //    d: SS mount ns:   slave(2):           /mnt
  //    f: SS mount ns:   slave(1):           /mnt/pre_reboot_dexopt/mount_tmp
  //
  // 3. Bind-mount the temp mount point at /mnt/pre_reboot_dexopt/chroot/foo. (The new mount point
  //    gets "slave+shared". It gets "slave" because the source (`e`) is "slave", and it gets
  //    "shared" because the dest (`b`) is "shared".)
  //    a: init mount ns: shared(1):          /foo
  //    b: init mount ns: shared(2):          /mnt
  //    e: init mount ns: slave(1):           /mnt/pre_reboot_dexopt/mount_tmp
  //    g: init mount ns: slave(1),shared(3): /mnt/pre_reboot_dexopt/chroot/foo
  //    b: SS mount ns:   slave(1):           /foo
  //    d: SS mount ns:   slave(2):           /mnt
  //    f: SS mount ns:   slave(1):           /mnt/pre_reboot_dexopt/mount_tmp
  //    h: SS mount ns:   slave(3):           /mnt/pre_reboot_dexopt/chroot/foo
  //
  // 4. Unmount the temp mount point.
  //    a: init mount ns: shared(1):          /foo
  //    b: init mount ns: shared(2):          /mnt
  //    g: init mount ns: slave(1),shared(3): /mnt/pre_reboot_dexopt/chroot/foo
  //    b: SS mount ns:   slave(1):           /foo
  //    d: SS mount ns:   slave(2):           /mnt
  //    h: SS mount ns:   slave(3):           /mnt/pre_reboot_dexopt/chroot/foo
  //
  // At this point, we have achieved what we want. `g` is a slave of `a` so that unmounts in `g`
  // doesn't affect `a`, and `g` is shared with `h` so that mounts in `g` are propagated to `h`.
  OR_RETURN(CreateDir(*kBindMountTmpDir));
  if (mount(source.c_str(),
            kBindMountTmpDir->c_str(),
            /*fs_type=*/nullptr,
            MS_BIND,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to bind-mount '{}' at '{}'", source, *kBindMountTmpDir);
  }
  if (mount(/*source=*/nullptr,
            kBindMountTmpDir->c_str(),
            /*fs_type=*/nullptr,
            MS_SLAVE,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to make mount slave for '{}'", *kBindMountTmpDir);
  }
  if (mount(kBindMountTmpDir->c_str(),
            target.c_str(),
            /*fs_type=*/nullptr,
            MS_BIND,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to bind-mount '{}' at '{}'", *kBindMountTmpDir, target);
  }
  OR_RETURN(Unmount(*kBindMountTmpDir));
  LOG(INFO) << ART_FORMAT("Bind-mounted '{}' at '{}'", source, target);
  return {};
}

Result<void> BindMountRecursive(const std::string& source, const std::string& target) {
  CHECK(!source.ends_with('/'));
  OR_RETURN(BindMount(source, target));

  // Mount and make slave one by one. Do not use MS_REC because we don't want to mount a child if
  // the parent cannot be slave (i.e., is shared). Otherwise, unmount events will be undesirably
  // propagated to the source. For example, if "/dev" and "/dev/pts" are mounted at "/chroot/dev"
  // and "/chroot/dev/pts" respectively, and "/chroot/dev" is shared, then unmounting
  // "/chroot/dev/pts" will also unmount "/dev/pts".
  //
  // The list is in mount order.
  std::vector<FstabEntry> entries = OR_RETURN(GetProcMountsDescendantsOfPath(source));
  for (const FstabEntry& entry : entries) {
    CHECK(!entry.mount_point.ends_with('/'));
    std::string_view sub_dir = entry.mount_point;
    CHECK(ConsumePrefix(&sub_dir, source));
    if (sub_dir.empty()) {
      // `source` itself. Already mounted.
      continue;
    }
    OR_RETURN(BindMount(entry.mount_point, std::string(target).append(sub_dir)));
  }
  return {};
}

std::string GetBlockDeviceName(const std::string& partition, const std::string& slot) {
  return ART_FORMAT("/dev/block/mapper/{}{}", partition, slot);
}

Result<std::vector<std::string>> GetSupportedFilesystems() {
  std::string content;
  if (!ReadFileToString("/proc/filesystems", &content)) {
    return ErrnoErrorf("Failed to read '/proc/filesystems'");
  }
  std::vector<std::string> filesystems;
  for (const std::string& line : Split(content, "\n")) {
    std::vector<std::string> tokens = Tokenize(line, " \t");
    // If there are two tokens, the first token is a "nodev" mark, meaning it's not for a block
    // device, so we skip it.
    if (tokens.size() == 1) {
      filesystems.push_back(tokens[0]);
    }
  }
  // Prioritize the filesystems that are known to behave correctly, just in case some bad
  // filesystems are unexpectedly happy to mount volumes that aren't of their types. We have never
  // seen this case in practice though.
  constexpr const char* kWellKnownFilesystems[] = {"erofs", "ext4"};
  for (const char* well_known_fs : kWellKnownFilesystems) {
    auto it = std::find(filesystems.begin(), filesystems.end(), well_known_fs);
    if (it != filesystems.end()) {
      filesystems.erase(it);
      filesystems.insert(filesystems.begin(), well_known_fs);
    }
  }
  return filesystems;
}

Result<void> Mount(const std::string& block_device, const std::string& target, bool is_optional) {
  static const NoDestructor<Result<std::vector<std::string>>> supported_filesystems(
      GetSupportedFilesystems());
  if (!supported_filesystems->ok()) {
    return supported_filesystems->error();
  }
  std::vector<std::string> error_msgs;
  for (const std::string& filesystem : supported_filesystems->value()) {
    if (mount(block_device.c_str(),
              target.c_str(),
              filesystem.c_str(),
              MS_RDONLY,
              /*data=*/nullptr) == 0) {
      // Success.
      LOG(INFO) << ART_FORMAT(
          "Mounted '{}' at '{}' with type '{}'", block_device, target, filesystem);
      return {};
    } else {
      if (errno == ENOENT && is_optional) {
        LOG(INFO) << ART_FORMAT("Skipped non-existing block device '{}'", block_device);
        return {};
      }
      error_msgs.push_back(ART_FORMAT("Tried '{}': {}", filesystem, strerror(errno)));
      if (errno != EINVAL && errno != EBUSY) {
        // If the filesystem type is wrong, `errno` must be either `EINVAL` or `EBUSY`. For example,
        // we've seen that trying to mount a device with a wrong filesystem type yields `EBUSY` if
        // the device is also mounted elsewhere, though we can't find any document about this
        // behavior.
        break;
      }
    }
  }
  return Errorf("Failed to mount '{}' at '{}':\n{}", block_device, target, Join(error_msgs, '\n'));
}

Result<void> MountTmpfs(const std::string& target, std::string_view se_context) {
  if (mount(/*source=*/"tmpfs",
            target.c_str(),
            /*fs_type=*/"tmpfs",
            MS_NODEV | MS_NOEXEC | MS_NOSUID,
            ART_FORMAT("mode={:#o},rootcontext={}", kChrootDefaultMode, se_context).c_str()) != 0) {
    return ErrnoErrorf("Failed to mount tmpfs at '{}'", target);
  }
  return {};
}

Result<std::optional<std::string>> LoadOtaSlotFile() {
  std::string content;
  if (!ReadFileToString(*kOtaSlotFile, &content)) {
    return ErrnoErrorf("Failed to read '{}'", *kOtaSlotFile);
  }
  if (content == "_a" || content == "_b") {
    return content;
  }
  if (content.empty()) {
    return std::nullopt;
  }
  return Errorf("Invalid content of '{}': '{}'", *kOtaSlotFile, content);
}

}  // namespace

ScopedAStatus DexoptChrootSetup::setUp(const std::optional<std::string>& in_otaSlot,
                                       bool in_mapSnapshotsForOta) {
  if (!mu_.try_lock()) {
    return Fatal("Unexpected concurrent calls");
  }
  std::lock_guard<std::mutex> lock(mu_, std::adopt_lock);

  if (in_otaSlot.has_value() && (in_otaSlot.value() != "_a" && in_otaSlot.value() != "_b")) {
    return Fatal(ART_FORMAT("Invalid OTA slot '{}'", in_otaSlot.value()));
  }
  OR_RETURN_NON_FATAL(SetUpChroot(in_otaSlot, in_mapSnapshotsForOta));
  return ScopedAStatus::ok();
}

ScopedAStatus DexoptChrootSetup::init() {
  if (!mu_.try_lock()) {
    return Fatal("Unexpected concurrent calls");
  }
  std::lock_guard<std::mutex> lock(mu_, std::adopt_lock);

  OR_RETURN_NON_FATAL(InitChroot());
  return ScopedAStatus::ok();
}

ScopedAStatus DexoptChrootSetup::tearDown(bool in_allowConcurrent) {
  if (in_allowConcurrent) {
    // Normally, we don't expect concurrent calls, but this method may be called upon system server
    // restart when another call initiated by the previous system_server instance is still being
    // processed.
    mu_.lock();
  } else {
    if (!mu_.try_lock()) {
      return Fatal("Unexpected concurrent calls");
    }
  }
  std::lock_guard<std::mutex> lock(mu_, std::adopt_lock);

  OR_RETURN_NON_FATAL(TearDownChroot());
  return ScopedAStatus::ok();
}

Result<void> DexoptChrootSetup::Start() {
  ScopedAStatus status = ScopedAStatus::fromStatus(
      AServiceManager_registerLazyService(this->asBinder().get(), kServiceName));
  if (!status.isOk()) {
    return Error() << status.getDescription();
  }

  ABinderProcess_startThreadPool();

  return {};
}

Result<void> DexoptChrootSetup::SetUpChroot(const std::optional<std::string>& ota_slot,
                                            bool map_snapshots_for_ota) const {
  // Set the default permission mode for new files and dirs to be `kChrootDefaultMode`.
  umask(~kChrootDefaultMode & 0777);

  // In case there is some leftover.
  OR_RETURN(TearDownChroot());

  // Prepare the root dir of chroot. The parent directory has been created by init (see `init.rc`).
  OR_RETURN(CreateDir(CHROOT_DIR));
  LOG(INFO) << ART_FORMAT("Created '{}'", CHROOT_DIR);

  std::vector<std::string> additional_system_partitions = {
      "system_ext",
      "vendor",
      "product",
  };

  if (!IsOtaUpdate(ota_slot)) {  // Mainline update
    OR_RETURN(BindMount("/", CHROOT_DIR));
    for (const std::string& partition : additional_system_partitions) {
      // Some additional partitions are optional, but that's okay. The root filesystem (mounted at
      // `/`) has empty directories for additional partitions. If additional partitions don't exist,
      // we'll just be bind-mounting empty directories.
      OR_RETURN(BindMount("/" + partition, PathInChroot("/" + partition)));
    }
  } else {
    CHECK(ota_slot.value() == "_a" || ota_slot.value() == "_b");

    if (map_snapshots_for_ota) {
      // Write the file early in case `snapshotctl map` fails in the middle, leaving some devices
      // mapped. We don't assume that `snapshotctl map` is transactional.
      if (!WriteStringToFile("", *kSnapshotMappedFile)) {
        return ErrnoErrorf("Failed to write '{}'", *kSnapshotMappedFile);
      }

      // Run `snapshotctl map` through init to map block devices. We can't run it ourselves because
      // it requires the UID to be 0. See `sys.snapshotctl.map` in `init.rc`.
      if (!SetProperty("sys.snapshotctl.map", "requested")) {
        return Errorf("Failed to request snapshotctl map");
      }
      if (!WaitForProperty("sys.snapshotctl.map", "finished", kSnapshotCtlTimeout)) {
        return Errorf("snapshotctl timed out");
      }

      // We don't know whether snapshotctl succeeded or not, but if it failed, the mount operation
      // below will fail with `ENOENT`.
      OR_RETURN(
          Mount(GetBlockDeviceName("system", ota_slot.value()), CHROOT_DIR, /*is_optional=*/false));
    } else {
      // update_engine has mounted `system` at `/postinstall` for us.
      OR_RETURN(BindMount("/postinstall", CHROOT_DIR));
    }

    for (const std::string& partition : additional_system_partitions) {
      OR_RETURN(Mount(GetBlockDeviceName(partition, ota_slot.value()),
                      PathInChroot("/" + partition),
                      /*is_optional=*/true));
    }
  }

  OR_RETURN(MountTmpfs(PathInChroot("/apex"), "u:object_r:apex_mnt_dir:s0"));
  OR_RETURN(MountTmpfs(PathInChroot("/linkerconfig"), "u:object_r:linkerconfig_file:s0"));
  OR_RETURN(MountTmpfs(PathInChroot("/mnt"), "u:object_r:pre_reboot_dexopt_file:s0"));
  OR_RETURN(CreateDir(PathInChroot("/mnt/artd_tmp")));
  OR_RETURN(MountTmpfs(PathInChroot("/mnt/artd_tmp"), "u:object_r:pre_reboot_dexopt_artd_file:s0"));
  OR_RETURN(CreateDir(PathInChroot("/mnt/expand")));

  std::vector<std::string> bind_mount_srcs = {
      // Data partitions.
      "/data",
      "/mnt/expand",
      // Linux API filesystems.
      "/dev",
      "/proc",
      "/sys",
      // For apexd to query staged APEX sessions.
      "/metadata",
  };

  for (const std::string& src : bind_mount_srcs) {
    OR_RETURN(BindMountRecursive(src, PathInChroot(src)));
  }

  if (!WriteStringToFile(ota_slot.value_or(""), *kOtaSlotFile)) {
    return ErrnoErrorf("Failed to write '{}'", *kOtaSlotFile);
  }

  return {};
}

Result<void> DexoptChrootSetup::InitChroot() const {
  std::optional<std::string> ota_slot = OR_RETURN(LoadOtaSlotFile());

  // Generate empty linker config to suppress warnings.
  if (!android::base::WriteStringToFile("", PathInChroot("/linkerconfig/ld.config.txt"))) {
    PLOG(WARNING) << "Failed to generate empty linker config to suppress warnings";
  }

  CmdlineBuilder args = OR_RETURN(GetArtExecCmdlineBuilder());
  args.Add("--")
      .Add("/system/bin/apexd")
      .Add("--otachroot-bootstrap")
      .AddIf(!IsOtaUpdate(ota_slot), "--also-include-staged-apexes");
  OR_RETURN(Run("apexd", args.Get()));

  args = OR_RETURN(GetArtExecCmdlineBuilder());
  args.Add("--drop-capabilities")
      .Add("--")
      .Add("/apex/com.android.runtime/bin/linkerconfig")
      .Add("--target")
      .Add("/linkerconfig");
  OR_RETURN(Run("linkerconfig", args.Get()));

  return {};
}

Result<void> DexoptChrootSetup::TearDownChroot() const {
  std::vector<FstabEntry> apex_entries =
      OR_RETURN(GetProcMountsDescendantsOfPath(PathInChroot("/apex")));
  // If there is only one entry, it's /apex itself.
  bool has_apex = apex_entries.size() > 1;

  if (has_apex && OS::FileExists(PathInChroot("/system/bin/apexd").c_str())) {
    // Delegate to apexd to unmount all APEXes. It also cleans up loop devices.
    CmdlineBuilder args = OR_RETURN(GetArtExecCmdlineBuilder());
    args.Add("--")
        .Add("/system/bin/apexd")
        .Add("--unmount-all")
        .Add("--also-include-staged-apexes");
    OR_RETURN(Run("apexd", args.Get()));
  }

  // Double check to make sure all APEXes are unmounted, just in case apexd incorrectly reported
  // success.
  apex_entries = OR_RETURN(GetProcMountsDescendantsOfPath(PathInChroot("/apex")));
  for (const FstabEntry& entry : apex_entries) {
    if (entry.mount_point != PathInChroot("/apex")) {
      return Errorf("apexd didn't unmount '{}'. See logs for details", entry.mount_point);
    }
  }

  // The list is in mount order.
  std::vector<FstabEntry> entries = OR_RETURN(GetProcMountsDescendantsOfPath(CHROOT_DIR));
  for (auto it = entries.rbegin(); it != entries.rend(); it++) {
    OR_RETURN(Unmount(it->mount_point));
    LOG(INFO) << ART_FORMAT("Unmounted '{}'", it->mount_point);
  }

  std::error_code ec;
  std::uintmax_t removed = std::filesystem::remove_all(CHROOT_DIR, ec);
  if (ec) {
    return Errorf("Failed to remove dir '{}': {}", CHROOT_DIR, ec.message());
  }
  if (removed > 0) {
    LOG(INFO) << ART_FORMAT("Removed '{}'", CHROOT_DIR);
  }

  if (!OR_RETURN(GetProcMountsDescendantsOfPath(*kBindMountTmpDir)).empty()) {
    OR_RETURN(Unmount(*kBindMountTmpDir));
  }

  std::filesystem::remove_all(*kBindMountTmpDir, ec);
  if (ec) {
    return Errorf("Failed to remove dir '{}': {}", *kBindMountTmpDir, ec.message());
  }

  std::filesystem::remove(*kOtaSlotFile, ec);
  if (ec) {
    return Errorf("Failed to remove file '{}': {}", *kOtaSlotFile, ec.message());
  }

  if (OS::FileExists(kSnapshotMappedFile->c_str())) {
    if (!SetProperty("sys.snapshotctl.unmap", "requested")) {
      return Errorf("Failed to request snapshotctl unmap");
    }
    if (!WaitForProperty("sys.snapshotctl.unmap", "finished", kSnapshotCtlTimeout)) {
      return Errorf("snapshotctl timed out");
    }
    std::filesystem::remove(*kSnapshotMappedFile, ec);
    if (ec) {
      return Errorf("Failed to remove file '{}': {}", *kSnapshotMappedFile, ec.message());
    }
  }

  return {};
}

std::string PathInChroot(std::string_view path) {
  return std::string(DexoptChrootSetup::CHROOT_DIR).append(path);
}

}  // namespace dexopt_chroot_setup
}  // namespace art
