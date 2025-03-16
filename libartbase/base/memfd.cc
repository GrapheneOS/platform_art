/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "memfd.h"

#include <errno.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include "macros.h"

namespace art {

#if defined(__linux__)

int memfd_create(const char* name, unsigned int flags) {
  // Check kernel version supports memfd_create(). Some older kernels segfault executing
  // memfd_create() rather than returning ENOSYS (b/116769556).
  static constexpr int kRequiredMajor = 3;
  static constexpr int kRequiredMinor = 17;
  struct utsname uts;
  int major, minor;
  if (uname(&uts) != 0 ||
      strcmp(uts.sysname, "Linux") != 0 ||
      sscanf(uts.release, "%d.%d", &major, &minor) != 2 ||
      (major < kRequiredMajor || (major == kRequiredMajor && minor < kRequiredMinor))) {
    errno = ENOSYS;
    return -1;
  }

  return syscall(__NR_memfd_create, name, flags);
}

static bool IsSealFutureWriteSupportedInternal() {
  android::base::unique_fd fd(art::memfd_create("test_android_memfd", MFD_ALLOW_SEALING));
  if (fd == -1) {
    LOG(INFO) << "memfd_create failed: " << strerror(errno) << ", no memfd support.";
    return false;
  }

  if (fcntl(fd, F_ADD_SEALS, F_SEAL_FUTURE_WRITE) == -1) {
    LOG(INFO) << "fcntl(F_ADD_SEALS) failed: " << strerror(errno) << ", no memfd support.";
    return false;
  }

  LOG(INFO) << "Using memfd for future sealing";
  return true;
}

bool IsSealFutureWriteSupported() {
  static bool is_seal_future_write_supported = IsSealFutureWriteSupportedInternal();
  return is_seal_future_write_supported;
}

#else  // __linux__

int memfd_create([[maybe_unused]] const char* name, [[maybe_unused]] unsigned int flags) {
  errno = ENOSYS;
  return -1;
}

bool IsSealFutureWriteSupported() {
  return false;
}

#endif  // __linux__

}  // namespace art
