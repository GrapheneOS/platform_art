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

#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

#include "aidl/com/android/server/art/BnDexoptChrootSetup.h"
#include "android-base/errors.h"
#include "android-base/file.h"
#include "android-base/logging.h"
#include "android-base/no_destructor.h"
#include "android-base/properties.h"
#include "android-base/result.h"
#include "android-base/scopeguard.h"
#include "android-base/strings.h"
#include "android-base/unique_fd.h"
#include "android/binder_auto_utils.h"
#include "android/binder_manager.h"
#include "android/binder_process.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/utils.h"
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
using ::android::base::GetProperty;
using ::android::base::Join;
using ::android::base::make_scope_guard;
using ::android::base::NoDestructor;
using ::android::base::ReadFileToString;
using ::android::base::Readlink;
using ::android::base::Result;
using ::android::base::SetProperty;
using ::android::base::Split;
using ::android::base::Tokenize;
using ::android::base::unique_fd;
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
constexpr std::array<const char*, 4> kExternalLibDirs = {
    "/system/lib", "/system/lib64", "/system_ext/lib", "/system_ext/lib64"};

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

Result<bool> IsSymlink(const std::string& path) {
  std::error_code ec;
  bool res = std::filesystem::is_symlink(path, ec);
  if (ec) {
    return Errorf("Failed to create dir '{}': {}", path, ec.message());
  }
  return res;
}

Result<bool> IsSelfOrParentSymlink(const std::string& path) {
  // We don't use `Realpath` because it does a `stat(2)` call which requires the SELinux "getattr"
  // permission. which we don't have on all mount points.
  unique_fd fd(open(path.c_str(), O_PATH | O_CLOEXEC));
  if (fd.get() < 0) {
    return ErrnoErrorf("Failed to open '{}' to resolve real path", path);
  }
  std::string real_path;
  if (!Readlink(ART_FORMAT("/proc/self/fd/{}", fd.get()), &real_path)) {
    return ErrnoErrorf("Failed to resolve real path for '{}'", path);
  }
  return path != real_path;
}

Result<void> Unmount(const std::string& target, bool logging = true) {
  if (umount2(target.c_str(), UMOUNT_NOFOLLOW) == 0) {
    LOG_IF(INFO, logging) << ART_FORMAT("Unmounted '{}'", target);
    return {};
  }
  LOG(WARNING) << ART_FORMAT(
      "Failed to umount2 '{}': {}. Retrying with MNT_DETACH", target, strerror(errno));
  if (umount2(target.c_str(), UMOUNT_NOFOLLOW | MNT_DETACH) == 0) {
    LOG_IF(INFO, logging) << ART_FORMAT("Unmounted '{}' with MNT_DETACH", target);
    return {};
  }
  return ErrnoErrorf("Failed to umount2 '{}'", target);
}

// Bind-mounts `source` at `target` with the mount propagation type being "shared". You generally
// want to use `BindMount` instead.
//
// `BindMountDirect` is safe to use only if there is no child mount points under `target`. DO NOT
// mount or unmount under `target` because mount events propagate to `source`.
Result<void> BindMountDirect(const std::string& source, const std::string& target) {
  // Don't follow symlinks.
  CHECK(!OR_RETURN(IsSelfOrParentSymlink(target))) << target;
  if (mount(source.c_str(),
            target.c_str(),
            /*fs_type=*/nullptr,
            MS_BIND,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to bind-mount '{}' at '{}'", source, target);
  }
  LOG(INFO) << ART_FORMAT("Bind-mounted '{}' at '{}'", source, target);
  return {};
}

// Bind-mounts `source` at `target` with the mount propagation type being "slave+shared".
// By default, this function rejects `source` in chroot, to avoid accidental repeated bind-mounting.
// If you intentionally want `source` to be in chroot, set `check_source_is_not_in_chroot` to false.
Result<void> BindMount(const std::string& source,
                       const std::string& target,
                       bool check_source_is_not_in_chroot = true) {
  // Don't bind-mount repeatedly.
  if (check_source_is_not_in_chroot) {
    CHECK(!PathStartsWith(source, DexoptChrootSetup::CHROOT_DIR));
  }
  // Don't follow symlinks.
  CHECK(!OR_RETURN(IsSelfOrParentSymlink(target))) << target;
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
    return ErrnoErrorf("Failed to bind-mount '{}' at '{}' ('{}' -> '{}')",
                       source,
                       *kBindMountTmpDir,
                       source,
                       target);
  }
  auto cleanup = make_scope_guard([&]() {
    Result<void> result = Unmount(*kBindMountTmpDir, /*logging=*/false);
    if (!result.ok()) {
      LOG(ERROR) << result.error().message();
    }
  });
  if (mount(/*source=*/nullptr,
            kBindMountTmpDir->c_str(),
            /*fs_type=*/nullptr,
            MS_SLAVE,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf(
        "Failed to make mount slave for '{}' ('{}' -> '{}')", *kBindMountTmpDir, source, target);
  }
  if (mount(kBindMountTmpDir->c_str(),
            target.c_str(),
            /*fs_type=*/nullptr,
            MS_BIND,
            /*data=*/nullptr) != 0) {
    return ErrnoErrorf("Failed to bind-mount '{}' at '{}' ('{}' -> '{}')",
                       *kBindMountTmpDir,
                       target,
                       source,
                       target);
  }
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
    if (Result<void> result = BindMount(entry.mount_point, std::string(target).append(sub_dir));
        !result.ok()) {
      // Match paths for the "u:object_r:apk_tmp_file:s0" file context in
      // system/sepolicy/private/file_contexts.
      std::regex apk_tmp_file_re(R"re((/data|/mnt/expand/[^/]+)/app/vmdl[^/]+\.tmp(/.*)?)re");
      if (std::regex_match(entry.mount_point, apk_tmp_file_re)) {
        // Don't bother. The mount point is a temporary directory created by Package Manager during
        // app install. We won't be able to dexopt the app there anyway because it's not in the
        // Package Manager's snapshot.
        LOG(INFO) << ART_FORMAT("Skipped temporary mount point '{}'", entry.mount_point);
        continue;
      }

      std::regex vendor_file_re(R"re(/data/vendor(/.*)?)re");
      if (std::regex_match(entry.mount_point, vendor_file_re)) {
        // We can't reliably bind-mount vendor-specific files because those files can have
        // vendor-specific SELinux file contexts, which by design cannot be referenced by
        // `dexopt_chroot_setup.te`. In practice, we don't need to bind-mount those files because
        // they are unlikely to contain things useful to us.
        LOG(INFO) << ART_FORMAT("Skipped vendor mount point '{}'", entry.mount_point);
        continue;
      }

      return result;
    }
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

Result<void> PatchLinkerConfigForCompatEnv() {
  std::string art_linker_config_content;
  if (!ReadFileToString(PathInChroot("/linkerconfig/com.android.art/ld.config.txt"),
                        &art_linker_config_content)) {
    return ErrnoErrorf("Failed to read ART linker config");
  }

  std::string compat_section =
      OR_RETURN(ConstructLinkerConfigCompatEnvSection(art_linker_config_content));

  // Append the patched section to the global linker config. Because the compat env path doesn't
  // start with "/apex", the global linker config is the one that takes effect.
  std::string global_linker_config_path = PathInChroot("/linkerconfig/ld.config.txt");
  std::string global_linker_config_content;
  if (!ReadFileToString(global_linker_config_path, &global_linker_config_content)) {
    return ErrnoErrorf("Failed to read global linker config");
  }

  if (!WriteStringToFile("dir.com.android.art.compat = /mnt/compat_env/apex/com.android.art/bin\n" +
                             global_linker_config_content + compat_section,
                         global_linker_config_path)) {
    return ErrnoErrorf("Failed to write global linker config");
  }

  LOG(INFO) << "Patched " << global_linker_config_path;
  return {};
}

// Platform libraries communicate with things outside of chroot through unstable APIs. Examples are
// `libbinder_ndk.so` talking to `servicemanager` and `libcgrouprc.so` reading
// `/dev/cgroup_info/cgroup.rc`. To work around incompatibility issues, we bind-mount the old
// platform library directories into chroot so that both sides of a communication are old and
// therefore align with each other.
// After bind-mounting old platform libraries, the chroot environment has a combination of new
// modules and old platform libraries. We currently use the new linker config in such an
// environment, which is potentially problematic. If we start to see problems, we should consider
// generating a more correct linker config in a more complex way.
Result<void> PrepareExternalLibDirs() {
  std::vector<const char*> existing_lib_dirs;
  std::copy_if(kExternalLibDirs.begin(),
               kExternalLibDirs.end(),
               std::back_inserter(existing_lib_dirs),
               OS::DirectoryExists);
  if (existing_lib_dirs.empty()) {
    return Errorf("Unexpectedly missing platform library directories. Tried '{}'",
                  android::base::Join(kExternalLibDirs, "', '"));
  }

  // We should bind-mount all existing lib dirs or none of them. Try the first one to decide what
  // to do next.
  Result<void> result = BindMount(existing_lib_dirs[0], PathInChroot(existing_lib_dirs[0]));
  if (result.ok()) {
    for (size_t i = 1; i < existing_lib_dirs.size(); ++i) {
      OR_RETURN(BindMount(existing_lib_dirs[i], PathInChroot(existing_lib_dirs[i])));
    }
  } else if (result.error().code() == EACCES) {
    // We don't have the permission to do so on V. Fall back to bind-mounting elsewhere.
    LOG(WARNING) << result.error().message();

    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env")));
    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env/system")));
    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env/system_ext")));
    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env/apex")));
    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env/apex/com.android.art")));
    OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env/apex/com.android.art/bin")));
    OR_RETURN(BindMountDirect(PathInChroot("/apex/com.android.art/bin"),
                              PathInChroot("/mnt/compat_env/apex/com.android.art/bin")));
    for (const char* lib_dir : existing_lib_dirs) {
      OR_RETURN(CreateDir(PathInChroot("/mnt/compat_env") + lib_dir));
      OR_RETURN(BindMountDirect(lib_dir, PathInChroot("/mnt/compat_env") + lib_dir));
    }

    OR_RETURN(PatchLinkerConfigForCompatEnv());
  } else {
    return result;
  }

  // Back up the new classpaths dir before bind-mounting etc dirs. We need the new classpaths dir
  // for derive_classpath.
  std::string classpaths_tmp_dir = PathInChroot("/mnt/classpaths");
  OR_RETURN(CreateDir(classpaths_tmp_dir));
  OR_RETURN(BindMount(PathInChroot("/system/etc/classpaths"),
                      classpaths_tmp_dir,
                      /*check_source_is_not_in_chroot=*/false));

  // Old platform libraries expect old etc dirs, so we should bind-mount them as well.
  OR_RETURN(BindMount("/system/etc", PathInChroot("/system/etc")));
  OR_RETURN(BindMount("/system_ext/etc", PathInChroot("/system_ext/etc")));
  OR_RETURN(BindMount("/product/etc", PathInChroot("/product/etc")));
  result = BindMount("/vendor/etc", PathInChroot("/vendor/etc"));
  if (!result.ok()) {
    if (result.error().code() == EACCES) {
      // We don't have the permission to do so on V. That's fine because the V version of the
      // platform libraries are fine with the B version of /vendor/etc at the time of writing. Even
      // if it's not fine, there is nothing we can do.
      LOG(WARNING) << result.error().message();
    } else {
      return result;
    }
  }

  // Restore the classpaths dir.
  OR_RETURN(BindMount(classpaths_tmp_dir,
                      PathInChroot("/system/etc/classpaths"),
                      /*check_source_is_not_in_chroot=*/false));
  OR_RETURN(Unmount(classpaths_tmp_dir));

  return {};
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

  if (OS::FileExists(PathInChroot("/linkerconfig/ld.config.txt").c_str())) {
    return Fatal("init must not be repeatedly called");
  }

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

  std::vector<std::tuple<std::string, std::string>> additional_system_partitions = {
      {"system_ext", "/system_ext"},
      {"vendor", "/vendor"},
      {"product", "/product"},
  };

  std::string partitions_from_sysprop =
      GetProperty(kAdditionalPartitionsSysprop, /*default_value=*/"");
  std::vector<std::string_view> partitions_from_sysprop_entries;
  art::Split(partitions_from_sysprop, ',', &partitions_from_sysprop_entries);
  for (std::string_view entry : partitions_from_sysprop_entries) {
    std::vector<std::string_view> pair;
    art::Split(entry, ':', &pair);
    if (pair.size() != 2 || pair[0].empty() || pair[1].empty() || !pair[1].starts_with('/')) {
      return Errorf("Malformed entry in '{}': '{}'", kAdditionalPartitionsSysprop, entry);
    }
    additional_system_partitions.emplace_back(std::string(pair[0]), std::string(pair[1]));
  }

  if (!IsOtaUpdate(ota_slot)) {  // Mainline update
    OR_RETURN(BindMount("/", CHROOT_DIR));
    // Normally, we don't need to bind-mount "/system" because it's a part of the image mounted at
    // "/". However, when readonly partitions are remounted read-write, an overlay is created at
    // "/system", so we need to bind-mount "/system" to handle this case. On devices where readonly
    // partitions are not remounted, bind-mounting "/system" doesn't hurt.
    OR_RETURN(BindMount("/system", PathInChroot("/system")));
    for (const auto& [partition, mount_point] : additional_system_partitions) {
      // Some additional partitions are optional. On a device where an additional partition doesn't
      // exist, the mount point of the partition is a symlink to a directory inside /system.
      if (!OR_RETURN(IsSymlink(mount_point))) {
        OR_RETURN(BindMount(mount_point, PathInChroot(mount_point)));
      }
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

    for (const auto& [partition, mount_point] : additional_system_partitions) {
      OR_RETURN(Mount(GetBlockDeviceName(partition, ota_slot.value()),
                      PathInChroot(mount_point),
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

  if (IsOtaUpdate(ota_slot)) {
    OR_RETURN(PrepareExternalLibDirs());
  }

  return {};
}

Result<void> DexoptChrootSetup::TearDownChroot() const {
  // For platform library dirs and etc dirs, make sure we have unmounted them before running apexd,
  // as apexd expects new libraries (and probably new etc dirs).
  // For mount points under "/mnt/compat_env", make sure we have unmounted them before running
  // apexd, as apexd doesn't expect apexes to be in-use.
  // The list is in mount order.
  std::vector<FstabEntry> entries = OR_RETURN(GetProcMountsDescendantsOfPath(CHROOT_DIR));
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const FstabEntry& entry = *it;
    std::string_view mount_point_in_chroot = entry.mount_point;
    CHECK(ConsumePrefix(&mount_point_in_chroot, CHROOT_DIR));
    if (mount_point_in_chroot.empty()) {
      continue;  // The root mount.
    }
    if (ContainsElement(kExternalLibDirs, mount_point_in_chroot) ||
        PathStartsWith(mount_point_in_chroot, "/mnt/compat_env") ||
        ContainsElement({"/system/etc",
                         "/system_ext/etc",
                         "/product/etc",
                         "/vendor/etc",
                         "/system/etc/classpaths",
                         "/mnt/classpaths"},
                        mount_point_in_chroot)) {
      OR_RETURN(Unmount(entry.mount_point));
    }
  }

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
  entries = OR_RETURN(GetProcMountsDescendantsOfPath(CHROOT_DIR));
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    OR_RETURN(Unmount(it->mount_point));
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

Result<std::string> ConstructLinkerConfigCompatEnvSection(
    const std::string& art_linker_config_content) {
  std::regex system_lib_re(R"re((=\s*|:)/(system(?:_ext)?/\$\{LIB\}))re");
  constexpr const char* kSystemLibFmt = "$1/mnt/compat_env/$2";

  // Make a copy of the [com.android.art] section and patch particular lines.
  std::string compat_section;
  bool is_in_art_section = false;
  bool replaced = false;
  std::vector<std::string_view> art_linker_config_lines;
  art::Split(art_linker_config_content, '\n', &art_linker_config_lines);
  for (std::string_view line : art_linker_config_lines) {
    if (!is_in_art_section && line == "[com.android.art]") {
      is_in_art_section = true;
    } else if (is_in_art_section && line.starts_with('[')) {
      is_in_art_section = false;
    }

    if (is_in_art_section) {
      if (line == "[com.android.art]") {
        compat_section += "[com.android.art.compat]\n";
      } else {
        std::string patched_line =
            std::regex_replace(std::string(line), system_lib_re, kSystemLibFmt);
        if (line != patched_line) {
          LOG(DEBUG) << ART_FORMAT("Replacing '{}' with '{}'", line, patched_line);
          replaced = true;
        }
        compat_section += patched_line;
        compat_section += '\n';
      }
    }
  }
  if (!replaced) {
    return Errorf("No matching lines to patch in ART linker config");
  }
  return compat_section;
}

}  // namespace dexopt_chroot_setup
}  // namespace art
