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

// Check that the current test executable only links known exported libraries
// dynamically. Intended to be statically linked into standalone tests.

#include <dlfcn.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "android-base/result-gmock.h"
#include "android-base/result.h"
#include "android-base/scopeguard.h"
#include "android-base/strings.h"
#include "android-base/unique_fd.h"
#include "base/stl_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::android::base::ErrnoError;
using ::android::base::Error;
using ::android::base::Result;

// The allow-listed libraries. Standalone tests can assume that the ART module
// is from the same build as the test(*), but not the platform nor any other
// module. Hence all dynamic libraries listed here must satisfy at least one of
// these conditions:
//
// -  Have a stable ABI and be available since the APEX min_sdk_version (31).
//    This includes NDK and system APIs.
// -  Be loaded from the ART APEX itself(*). Note that linker namespaces aren't
//    set up to allow this for libraries that aren't exported, so in practice it
//    is restricted to them.
// -  Always be pushed to device together with the test.
// -  Be a runtime instrumentation library or similar, e.g. for sanitizer test
//    builds, where everything is always built from source - platform, module,
//    and tests.
//
// *) (Non-MCTS) CTS tests is an exception - they must work with any future
// version of the module and hence restrict themselves to the exported module
// APIs.
constexpr const char* kAllowedDynamicLibDeps[] = {
    // LLVM
    "libclang_rt.hwasan-aarch64-android.so",
    // Bionic
    "libc.so",
    "libdl.so",
    "libdl_android.so",
    "libm.so",
    // Platform
    "heapprofd_client_api.so",
    "libbinder_ndk.so",
    "liblog.so",
    "libselinux.so",
    "libz.so",
    // Other modules
    "libstatspull.so",
    "libstatssocket.so",
    // ART exported
    "libdexfile.so",
    "libnativebridge.so",
    "libnativehelper.so",
    "libnativeloader.so",
};

Result<std::string> GetCurrentElfObjectPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(GetCurrentElfObjectPath), &info) == 0) {
    return Error() << "dladdr failed to map own address to a shared object.";
  }
  return info.dli_fname;
}

Result<std::vector<std::string>> GetDynamicLibDeps(const std::string& filename) {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    return Errorf("libelf initialization failed: {}", elf_errmsg(-1));
  }

  android::base::unique_fd fd(open(filename.c_str(), O_RDONLY));
  if (fd.get() == -1) {
    return ErrnoErrorf("Error opening {}", filename);
  }

  Elf* elf = elf_begin(fd.get(), ELF_C_READ, /*ref=*/nullptr);
  if (elf == nullptr) {
    return Errorf("Error creating ELF object for {}: {}", filename, elf_errmsg(-1));
  }
  auto elf_cleanup = android::base::make_scope_guard([&]() { elf_end(elf); });

  std::vector<std::string> libs;

  // Find the dynamic section.
  for (Elf_Scn* dyn_scn = nullptr; (dyn_scn = elf_nextscn(elf, dyn_scn)) != nullptr;) {
    GElf_Shdr scn_hdr;
    if (gelf_getshdr(dyn_scn, &scn_hdr) != &scn_hdr) {
      return Errorf("Failed to retrieve ELF section header in {}: {}", filename, elf_errmsg(-1));
    }

    if (scn_hdr.sh_type == SHT_DYNAMIC) {
      Elf_Data* data = elf_getdata(dyn_scn, /*data=*/nullptr);

      // Iterate through dynamic section entries.
      for (int i = 0; i < scn_hdr.sh_size / scn_hdr.sh_entsize; i++) {
        GElf_Dyn dyn_entry;
        if (gelf_getdyn(data, i, &dyn_entry) != &dyn_entry) {
          return Errorf("Failed to get entry {} in ELF dynamic section of {}: {}",
                        i,
                        filename,
                        elf_errmsg(-1));
        }

        if (dyn_entry.d_tag == DT_NEEDED) {
          const char* lib_name = elf_strptr(elf, scn_hdr.sh_link, dyn_entry.d_un.d_val);
          if (lib_name == nullptr) {
            return Errorf("Failed to get string from entry {} in ELF dynamic section of {}: {}",
                          i,
                          filename,
                          elf_errmsg(-1));
          }
          libs.push_back(lib_name);
        }
      }
      break;  // Found the dynamic section, no need to continue.
    }
  }

  return libs;
}

}  // namespace

TEST(StandaloneTestAllowedLibDeps, test) {
  Result<std::string> path_to_self = GetCurrentElfObjectPath();
  ASSERT_RESULT_OK(path_to_self);
  Result<std::vector<std::string>> dyn_lib_deps = GetDynamicLibDeps(path_to_self.value());
  ASSERT_RESULT_OK(dyn_lib_deps);

  // Allow .so files in the same directory as the test binary, for shared libs
  // pushed with the test using `data_libs`.
  std::filesystem::path self_dir = std::filesystem::path(path_to_self.value()).parent_path();
  std::vector<std::string> test_libs;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(self_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".so") {
      test_libs.push_back(entry.path().filename());
    }
  }

  std::vector<std::string> disallowed_libs;
  for (const std::string& dyn_lib_dep : dyn_lib_deps.value()) {
    if (std::find(std::begin(kAllowedDynamicLibDeps),
                  std::end(kAllowedDynamicLibDeps),
                  dyn_lib_dep) != std::end(kAllowedDynamicLibDeps)) {
      continue;
    }
    if (art::ContainsElement(test_libs, dyn_lib_dep)) {
      continue;
    }
    disallowed_libs.push_back(dyn_lib_dep);
  }

  EXPECT_THAT(disallowed_libs, ::testing::IsEmpty())
      << path_to_self.value() << " has disallowed shared library dependencies.";
}
