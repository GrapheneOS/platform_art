/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ostream>
#include <string>
#include <vector>

#include "android-base/parseint.h"
#include "android-base/stringprintf.h"
#include "base/os.h"
#include "cmdline.h"
#include "page_util.h"
#include "procinfo/process_map.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

using android::base::StringPrintf;

struct PageInfo {
  // Page start address.
  uint64_t start = -1;
  // Page end address.
  uint64_t end = -1;
  // Number of times the physical page is mapped.
  uint64_t page_count = -1;
  // Physical frame number of the page.
  uint64_t pfn = -1;
  // Number of zero bytes in the page.
  uint64_t zero_bytes_count = -1;
  // Memory region of the page.
  const android::procinfo::MapInfo* mem_map = nullptr;
};

struct ProcPages {
  // Memory maps of the process.
  std::vector<android::procinfo::MapInfo> maps;
  // Page contents hash -> PageInfo
  std::unordered_map<size_t, std::vector<PageInfo>> pages;
};

bool ReadProcessPages(
    std::ostream& os, pid_t pid, ProcPages& proc_pages, ProcFiles& proc_files, uint64_t page_size) {
  if (!android::procinfo::ReadProcessMaps(pid, &proc_pages.maps)) {
    os << "Could not read process maps for " << pid;
    return false;
  }

  std::string error_msg;
  std::vector<uint8_t> page_contents(page_size);
  for (const android::procinfo::MapInfo& map_info : proc_pages.maps) {
    for (uint64_t start = map_info.start; start < map_info.end; start += page_size) {
      PageInfo page_info;
      page_info.start = start;
      page_info.end = start + page_size;
      page_info.mem_map = &map_info;
      const size_t virtual_page_index = start / page_size;
      if (!GetPageFrameNumber(proc_files.pagemap, virtual_page_index, page_info.pfn, error_msg)) {
        os << error_msg;
        return false;
      }
      if (!GetPageFlagsOrCounts(proc_files.kpagecount,
                                ArrayRef<const uint64_t>(&page_info.pfn, 1),
                                /*out*/ ArrayRef<uint64_t>(&page_info.page_count, 1),
                                /*out*/ error_msg)) {
        os << error_msg << std::endl;
        os << "mem_map name: " << map_info.name << std::endl;
        os << "pfn: " << page_info.pfn << std::endl;
        os << "page_start: " << page_info.start << std::endl;
        os << "mem_map start: " << map_info.start << std::endl;
        // start = map_info.end;
        continue;
      }

      if (page_info.page_count == 0) {
        // Page is not present in memory.
        continue;
      }

      // Handle present page.
      if (!proc_files.mem.PreadFully(page_contents.data(), page_contents.size(), start)) {
        os << StringPrintf("Failed to read present page %" PRIx64 " for mem_map %s\n",
                           start,
                           map_info.name.c_str());
        return false;
      }

      page_info.zero_bytes_count =
          static_cast<uint64_t>(std::count(page_contents.begin(), page_contents.end(), 0));

      const size_t content_hash = DataHash::HashBytes(page_contents.data(), page_contents.size());
      proc_pages.pages[content_hash].push_back(page_info);
    }
  }

  return true;
}

int FindUnsharedPages(std::ostream& os, pid_t pid1, pid_t pid2, size_t page_size) {
  ProcFiles proc_files1;
  ProcFiles proc_files2;
  std::string error_msg;
  if (!OpenProcFiles(pid1, proc_files1, error_msg)) {
    os << error_msg;
    return EXIT_FAILURE;
  }
  if (!OpenProcFiles(pid2, proc_files2, error_msg)) {
    os << error_msg;
    return EXIT_FAILURE;
  }

  ProcPages proc_pages1;
  if (!ReadProcessPages(os, pid1, proc_pages1, proc_files1, page_size)) {
    return EXIT_FAILURE;
  }

  ProcPages proc_pages2;
  if (!ReadProcessPages(os, pid2, proc_pages2, proc_files2, page_size)) {
    return EXIT_FAILURE;
  }

  for (const auto& [hash, pages1] : proc_pages1.pages) {
    // Skip zero pages.
    if (pages1.front().zero_bytes_count == page_size) {
      continue;
    }

    // Find pages with the same content in the second process.
    if (proc_pages2.pages.find(hash) != proc_pages2.pages.end()) {
      const auto& pages2 = proc_pages2.pages[hash];

      std::unordered_set<uint64_t> pfns1, pfns2;
      for (const auto& p : pages1) {
        pfns1.insert(p.pfn);
      }
      for (const auto& p : pages2) {
        pfns2.insert(p.pfn);
      }

      const bool is_different = (pfns1 != pfns2);
      if (is_different) {
        os << "\nDuplicate pages (pfn, start_addr, mem_map, zero_bytes_count)\nPID1:\n";
        for (const auto& page1 : pages1) {
          os << ART_FORMAT("\t{} {} {} {}\n",
                           page1.pfn,
                           page1.start,
                           page1.mem_map->name,
                           page1.zero_bytes_count);
        }
        os << "PID2:\n";
        for (const auto& page2 : pages2) {
          os << ART_FORMAT("\t{} {} {} {}\n",
                           page2.pfn,
                           page2.start,
                           page2.mem_map->name,
                           page2.zero_bytes_count);
        }
      }
    }
  }

  return EXIT_SUCCESS;
}

struct FindUnsharedPagesArgs : public CmdlineArgs {
 protected:
  using Base = CmdlineArgs;

  ParseStatus ParseCustom(const char* raw_option,
                          size_t raw_option_length,
                          std::string* error_msg) override {
    DCHECK_EQ(strlen(raw_option), raw_option_length);
    {
      ParseStatus base_parse = Base::ParseCustom(raw_option, raw_option_length, error_msg);
      if (base_parse != kParseUnknownArgument) {
        return base_parse;
      }
    }

    std::string_view option(raw_option, raw_option_length);
    if (option.starts_with("--pid1=")) {
      const char* value = raw_option + strlen("--pid1=");
      if (!android::base::ParseInt(value, &pid1_)) {
        *error_msg = "Failed to parse pid1";
        return kParseError;
      }
    } else if (option.starts_with("--pid2=")) {
      const char* value = raw_option + strlen("--pid2=");
      if (!android::base::ParseInt(value, &pid2_)) {
        *error_msg = "Failed to parse pid2";
        return kParseError;
      }
    } else {
      return kParseUnknownArgument;
    }

    return kParseOk;
  }

  ParseStatus ParseChecks(std::string* error_msg) override {
    ParseStatus parent_checks = Base::ParseChecks(error_msg);
    if (parent_checks != kParseOk) {
      return parent_checks;
    }

    if (pid1_ == -1 || pid2_ == -1) {
      *error_msg = "Missing --pid=";
      return kParseError;
    }

    for (pid_t pid : {pid1_, pid2_}) {
      if (kill(pid, /*sig*/ 0) != 0) {  // No signal is sent, perform error-checking only.
        // Check if the pid exists before proceeding.
        if (errno == ESRCH) {
          *error_msg = "Process specified does not exist, pid: " + std::to_string(pid);
        } else {
          *error_msg = StringPrintf("Failed to check process status: %s", strerror(errno));
        }
        return kParseError;
      }
    }
    return kParseOk;
  }

  std::string GetUsage() const override {
    std::string usage;

    usage +=
        "Usage: find_unshared_pages [options] ...\n"
        "    Example: find_unshared_pages --pid1=$(pidof system_server) --pid2=$(pidof "
        "com.android.camera2)\n"
        "\n";

    usage += Base::GetUsage();

    usage += "  --pid1=<pid> --pid2=<pid>: PIDs of the processes to analyze.\n";

    return usage;
  }

 public:
  pid_t pid1_ = -1;
  pid_t pid2_ = -1;
};

struct FindUnsharedPagesMain : public CmdlineMain<FindUnsharedPagesArgs> {
  bool ExecuteWithoutRuntime() override {
    CHECK(args_ != nullptr);
    CHECK(args_->os_ != nullptr);

    return FindUnsharedPages(*args_->os_, args_->pid1_, args_->pid2_, MemMap::GetPageSize()) ==
           EXIT_SUCCESS;
  }

  bool NeedsRuntime() override { return false; }
};

}  // namespace art

int main(int argc, char** argv) {
  art::FindUnsharedPagesMain main;
  return main.Main(argc, argv);
}
