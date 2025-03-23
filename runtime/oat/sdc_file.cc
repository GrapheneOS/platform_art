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

#include "sdc_file.h"

#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "android-base/file.h"
#include "android-base/parseint.h"
#include "android-base/scopeguard.h"
#include "base/macros.h"
#include "base/utils.h"

namespace art HIDDEN {

using ::android::base::ParseInt;
using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFd;

std::unique_ptr<SdcReader> SdcReader::Load(const std::string& filename, std::string* error_msg) {
  std::unique_ptr<SdcReader> reader(new SdcReader());

  // The sdc file is supposed to be small, so read fully into memory for simplicity.
  if (!ReadFileToString(filename, &reader->content_)) {
    *error_msg = ART_FORMAT("Failed to load sdc file '{}': {}", filename, strerror(errno));
    return nullptr;
  }

  std::vector<std::string_view> lines;
  Split(reader->content_, '\n', &lines);
  std::unordered_map<std::string_view, std::string_view> map;
  for (std::string_view line : lines) {
    size_t pos = line.find('=');
    if (pos == std::string_view::npos || pos == 0) {
      *error_msg = ART_FORMAT("Malformed line '{}' in sdc file '{}'", line, filename);
      return nullptr;
    }
    if (!map.try_emplace(line.substr(0, pos), line.substr(pos + 1)).second) {
      *error_msg = ART_FORMAT("Duplicate key '{}' in sdc file '{}'", line.substr(0, pos), filename);
      return nullptr;
    }
  }

  decltype(map)::iterator it;
  if ((it = map.find("sdm-timestamp-ns")) == map.end()) {
    *error_msg = ART_FORMAT("Missing key 'sdm-timestamp-ns' in sdc file '{}'", filename);
    return nullptr;
  }
  if (!ParseInt(std::string(it->second), &reader->sdm_timestamp_ns_, /*min=*/INT64_C(1))) {
    *error_msg = ART_FORMAT("Invalid 'sdm-timestamp-ns' {}", it->second);
    return nullptr;
  }

  if ((it = map.find("apex-versions")) == map.end()) {
    *error_msg = ART_FORMAT("Missing key 'apex-versions' in sdc file '{}'", filename);
    return nullptr;
  }
  if (!std::regex_match(it->second.begin(), it->second.end(), std::regex("[0-9/]*"))) {
    *error_msg = ART_FORMAT("Invalid 'apex-versions' {}", it->second);
    return nullptr;
  }
  reader->apex_versions_ = it->second;

  if (map.size() > 2) {
    *error_msg = ART_FORMAT("Malformed sdc file '{}'. Unrecognized keys", filename);
    return nullptr;
  }

  return reader;
}

bool SdcWriter::Save(std::string* error_msg) {
  auto cleanup = android::base::make_scope_guard([this] { (void)file_.FlushClose(); });
  if (sdm_timestamp_ns_ <= 0) {
    *error_msg = ART_FORMAT("Invalid 'sdm-timestamp-ns' {}", sdm_timestamp_ns_);
    return false;
  }
  DCHECK_EQ(file_.GetLength(), 0);
  std::string content =
      ART_FORMAT("sdm-timestamp-ns={}\napex-versions={}\n", sdm_timestamp_ns_, apex_versions_);
  if (!WriteStringToFd(content, file_.Fd())) {
    *error_msg = ART_FORMAT("Failed to write sdc file '{}': {}", file_.GetPath(), strerror(errno));
    return false;
  }
  int res = file_.FlushClose();
  if (res != 0) {
    *error_msg =
        ART_FORMAT("Failed to flush close sdc file '{}': {}", file_.GetPath(), strerror(-res));
    return false;
  }
  cleanup.Disable();
  return true;
}

}  // namespace art
