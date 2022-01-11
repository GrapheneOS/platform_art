/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "odr_dexopt.h"

#include <vector>

#include "android-base/logging.h"
#include "android-base/strings.h"
#include "exec_utils.h"
#include "log/log.h"
#include "odr_config.h"
#include "libdexopt.h"

#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"

namespace art {
namespace odrefresh {

using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;

namespace {

int ExecAndReturnCode(ExecUtils* exec_utils,
                      std::vector<std::string>& cmdline,
                      time_t timeout_secs,
                      /*out*/ bool* timed_out,
                      /*out*/ std::string* error_msg) {
  LOG(DEBUG) << "odr_dexopt cmdline: " << android::base::Join(cmdline, ' ') << " [timeout "
             << timeout_secs << "s]";
  return exec_utils->ExecAndReturnCode(cmdline, timeout_secs, timed_out, error_msg);
}

}  // namespace

OdrDexopt::OdrDexopt(const OdrConfig& config, std::unique_ptr<ExecUtils> exec_utils)
  : dex2oat_path_(config.GetDex2Oat()), exec_utils_(std::move(exec_utils)) {}

int OdrDexopt::DexoptBcpExtension(const DexoptBcpExtArgs& args,
                                  time_t timeout_secs,
                                  /*out*/ bool* timed_out,
                                  /*out*/ std::string* error_msg) {
  std::vector<std::string> cmdline = { dex2oat_path_ };
  auto result = art::AddDex2oatArgsFromBcpExtensionArgs(args, cmdline);
  if (!result.ok()) {
    LOG(ERROR) << "Dexopt (local) failed: " << result.error().message() << ", cmdline: "
               << android::base::Join(cmdline, ' ');
    return -1;
  }
  return ExecAndReturnCode(exec_utils_.get(), cmdline, timeout_secs, timed_out, error_msg);
}

int OdrDexopt::DexoptSystemServer(const DexoptSystemServerArgs& args,
                                  time_t timeout_secs,
                                  /*out*/ bool* timed_out,
                                  /*out*/ std::string* error_msg) {
  std::vector<std::string> cmdline = { dex2oat_path_ };
  auto result = art::AddDex2oatArgsFromSystemServerArgs(args, cmdline);
  if (!result.ok()) {
    LOG(ERROR) << "Dexopt (local) failed: " << result.error().message() << ", cmdline: "
               << android::base::Join(cmdline, ' ');
    return -1;
  }
  return ExecAndReturnCode(exec_utils_.get(), cmdline, timeout_secs, timed_out, error_msg);
}

}  // namespace odrefresh
}  // namespace art
