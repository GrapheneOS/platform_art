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

#include "membarrier.h"

#include <errno.h>
#include <stdio.h>

#if !defined(_WIN32)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "macros.h"
#include "utils.h"

#if __has_include(<linux/membarrier.h>)

#include <linux/membarrier.h>

#define CHECK_MEMBARRIER_CMD(art_value, membarrier_value) \
  static_assert(static_cast<int>(art_value) == (membarrier_value), "Bad value for " # art_value)
CHECK_MEMBARRIER_CMD(art::MembarrierCommand::kQuery, MEMBARRIER_CMD_QUERY);
CHECK_MEMBARRIER_CMD(art::MembarrierCommand::kGlobal, MEMBARRIER_CMD_SHARED);
CHECK_MEMBARRIER_CMD(art::MembarrierCommand::kPrivateExpedited, MEMBARRIER_CMD_PRIVATE_EXPEDITED);
CHECK_MEMBARRIER_CMD(art::MembarrierCommand::kRegisterPrivateExpedited,
                     MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED);
CHECK_MEMBARRIER_CMD(art::MembarrierCommand::kPrivateExpedited, MEMBARRIER_CMD_PRIVATE_EXPEDITED);
#undef CHECK_MEMBARRIER_CMD

#endif  // __has_include(<linux/membarrier.h>)

namespace art {

#if defined(__linux__)

static bool IsMemBarrierSupported() {
  // Check kernel version supports membarrier(2).
  // MEMBARRIER_CMD_QUERY is supported since Linux 4.3.
  // MEMBARRIER_CMD_PRIVATE_EXPEDITED is supported since Linux 4.14.
  // MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE is supported since Linux 4.16.
  // Lowest Linux version useful for ART is 4.14.
  return IsKernelVersionAtLeast(4, 14);
}

int membarrier(MembarrierCommand command) {
  static const bool membarrier_supported = IsMemBarrierSupported();
  if (UNLIKELY(!membarrier_supported)) {
    errno = ENOSYS;
    return -1;
  }
  return syscall(__NR_membarrier, static_cast<int>(command), 0);
}

#else  // __linux__

int membarrier([[maybe_unused]] MembarrierCommand command) {
  errno = ENOSYS;
  return -1;
}

#endif  // __linux__

}  // namespace art
