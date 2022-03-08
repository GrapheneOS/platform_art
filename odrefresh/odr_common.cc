/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "odr_common.h"

#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>

#include "android-base/logging.h"
#include "android-base/parseint.h"

namespace art {
namespace odrefresh {

std::string Concatenate(std::initializer_list<std::string_view> args) {
  std::stringstream ss;
  for (auto arg : args) {
    ss << arg;
  }
  return ss.str();
}

std::string QuotePath(std::string_view path) {
  return Concatenate({"'", path, "'"});
}

bool ShouldDisableRefresh(const std::string& sdk_version_str) {
  int sdk_version = 0;
  if (!android::base::ParseInt(sdk_version_str, &sdk_version)) {
    LOG(ERROR) << "Invalid SDK version string \"" << sdk_version_str << "\"";
    return false;
  }
  return sdk_version >= 32;
}

}  // namespace odrefresh
}  // namespace art
