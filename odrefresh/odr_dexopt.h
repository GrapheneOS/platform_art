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

#ifndef ART_ODREFRESH_ODR_DEXOPT_H_
#define ART_ODREFRESH_ODR_DEXOPT_H_

#include <memory>
#include <string>
#include <time.h>

#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"

namespace art {

class ExecUtils;

namespace odrefresh {

using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;

class OdrConfig;

class OdrDexopt {
 public:
  static std::unique_ptr<OdrDexopt> Create(const OdrConfig& confg,
                                           std::unique_ptr<ExecUtils> exec_utils);

  virtual ~OdrDexopt() {}

  virtual int DexoptBcpExtension(const DexoptBcpExtArgs& args,
                                 time_t timeout_secs,
                                 /*out*/ bool* timed_out,
                                 /*out*/ std::string* error_msg) = 0;
  virtual int DexoptSystemServer(const DexoptSystemServerArgs& args,
                                 time_t timeout_secs,
                                 /*out*/ bool* timed_out,
                                 /*out*/ std::string* error_msg) = 0;
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_DEXOPT_H_
