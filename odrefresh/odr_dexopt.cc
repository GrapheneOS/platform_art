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
#include <android-base/result.h>
#include "android-base/strings.h"
#include "exec_utils.h"
#include "libcompos_client.h"
#include "libdexopt.h"
#include "log/log.h"
#include "odr_config.h"

#include "aidl/com/android/art/DexoptBcpExtArgs.h"
#include "aidl/com/android/art/DexoptSystemServerArgs.h"
#include "aidl/com/android/art/ExtendableParcelable.h"
#include "aidl/com/android/art/TaskType.h"

namespace art {
namespace odrefresh {

using aidl::com::android::art::DexoptBcpExtArgs;
using aidl::com::android::art::DexoptSystemServerArgs;
using aidl::com::android::art::ExtendableParcelable;
using aidl::com::android::art::TaskType;
using android::base::Result;

namespace {

// TODO(193668901): Once migrated to the new API, remove the old implementation.
constexpr bool USE_NEW_COMPILIATION_OS_API = false;

int ExecAndReturnCode(ExecUtils* exec_utils,
                      std::vector<std::string>& cmdline,
                      time_t timeout_secs,
                      /*out*/ bool* timed_out,
                      /*out*/ std::string* error_msg) {
  LOG(DEBUG) << "odr_dexopt cmdline: " << android::base::Join(cmdline, ' ') << " [timeout "
             << timeout_secs << "s]";
  return exec_utils->ExecAndReturnCode(cmdline, timeout_secs, timed_out, error_msg);
}

bool insertFd(/*inout*/ std::vector<int>& vec, int n) {
  if (n < 0) {
    return false;
  }
  vec.emplace_back(n);
  return true;
}

void insertIfNonNegative(/*inout*/ std::vector<int>& vec, int n) {
  if (n >= 0) {
    vec.emplace_back(n);
  }
}

void insertOnlyNonNegative(/*inout*/ std::vector<int>& vec, const std::vector<int>& ns) {
  std::copy_if(ns.begin(), ns.end(), std::back_inserter(vec), [](int n) { return n >= 0; });
}

class OdrDexoptLocal final : public OdrDexopt {
 public:
  static OdrDexoptLocal* Create(const std::string& dex2oat_path,
                                std::unique_ptr<ExecUtils> exec_utils) {
    return new OdrDexoptLocal(dex2oat_path, std::move(exec_utils));
  }

  int DexoptBcpExtension(const DexoptBcpExtArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<std::string> cmdline = { dex2oat_path_ };
    auto result = art::AddDex2oatArgsFromBcpExtensionArgs(args, cmdline);
    if (!result.ok()) {
      LOG(ERROR) << "Dexopt (local) failed: " << result.error().message() << ", cmdline: "
                 << android::base::Join(cmdline, ' ');
      return -1;
    }
    return ExecAndReturnCode(exec_utils_.get(), cmdline, args.timeoutSecs, timed_out, error_msg);
  }

  int DexoptSystemServer(const DexoptSystemServerArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<std::string> cmdline = { dex2oat_path_ };
    auto result = art::AddDex2oatArgsFromSystemServerArgs(args, cmdline);
    if (!result.ok()) {
      LOG(ERROR) << "Dexopt (local) failed: " << result.error().message() << ", cmdline: "
                 << android::base::Join(cmdline, ' ');
      return -1;
    }
    return ExecAndReturnCode(exec_utils_.get(), cmdline, args.timeoutSecs, timed_out, error_msg);
  }

 private:
  OdrDexoptLocal(const std::string& dex2oat_path, std::unique_ptr<ExecUtils> exec_utils)
      : dex2oat_path_(dex2oat_path), exec_utils_(std::move(exec_utils)) {}

  std::string dex2oat_path_;
  std::unique_ptr<ExecUtils> exec_utils_;
};

class OdrDexoptCompilationOSCmdline final : public OdrDexopt {
 public:
  static OdrDexoptCompilationOSCmdline* Create(int cid, std::unique_ptr<ExecUtils> exec_utils) {
    return new OdrDexoptCompilationOSCmdline(cid, std::move(exec_utils));
  }

  int DexoptBcpExtension(const DexoptBcpExtArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<int> input_fds, output_fds;
    collectFdsFromDexoptBcpExtensionArgs(input_fds, output_fds, args);

    std::vector<std::string> cmdline;
    AppendPvmExecArgs(cmdline, input_fds, output_fds);

    // Original dex2oat flags
    cmdline.push_back("/apex/com.android.art/bin/dex2oat64");
    auto result = AddDex2oatArgsFromBcpExtensionArgs(args, cmdline);
    if (!result.ok()) {
      LOG(ERROR) << "Dexopt (CompOS) failed: " << result.error().message() << ", cmdline: "
                 << android::base::Join(cmdline, ' ');
      return -1;
    }

    return ExecAndReturnCode(exec_utils_.get(), cmdline, args.timeoutSecs, timed_out, error_msg);
  }

  int DexoptSystemServer(const DexoptSystemServerArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<int> input_fds, output_fds;
    collectFdsFromDexoptSystemServerArgs(input_fds, output_fds, args);

    std::vector<std::string> cmdline;
    AppendPvmExecArgs(cmdline, input_fds, output_fds);

    // Original dex2oat flags
    cmdline.push_back("/apex/com.android.art/bin/dex2oat64");
    auto result = AddDex2oatArgsFromSystemServerArgs(args, cmdline);
    if (!result.ok()) {
      LOG(ERROR) << "Dexopt (CompOS) failed: " << result.error().message() << ", cmdline: "
                 << android::base::Join(cmdline, ' ');
      return -1;
    }

    return ExecAndReturnCode(exec_utils_.get(), cmdline, args.timeoutSecs, timed_out, error_msg);
  }

 private:
  OdrDexoptCompilationOSCmdline(int cid, std::unique_ptr<ExecUtils> exec_utils)
      : cid_(cid), exec_utils_(std::move(exec_utils)) {}

  void AppendPvmExecArgs(/*inout*/ std::vector<std::string>& cmdline,
                         const std::vector<int>& input_fds,
                         const std::vector<int>& output_fds) {
    cmdline.emplace_back("/apex/com.android.compos/bin/pvm_exec");
    cmdline.emplace_back("--cid=" + std::to_string(cid_));
    cmdline.emplace_back("--in-fd=" + android::base::Join(input_fds, ','));
    cmdline.emplace_back("--out-fd=" + android::base::Join(output_fds, ','));
    cmdline.emplace_back("--");
  }

  void collectFdsFromDexoptBcpExtensionArgs(/*inout*/ std::vector<int>& input_fds,
                                            /*inout*/ std::vector<int>& output_fds,
                                            const DexoptBcpExtArgs& args) {
    // input
    insertOnlyNonNegative(input_fds, args.dexFds);
    insertIfNonNegative(input_fds, args.profileFd);
    insertIfNonNegative(input_fds, args.dirtyImageObjectsFd);
    insertOnlyNonNegative(input_fds, args.bootClasspathFds);
    // output
    insertIfNonNegative(output_fds, args.imageFd);
    insertIfNonNegative(output_fds, args.vdexFd);
    insertIfNonNegative(output_fds, args.oatFd);
  }

  void collectFdsFromDexoptSystemServerArgs(/*inout*/ std::vector<int>& input_fds,
                                            /*inout*/ std::vector<int>& output_fds,
                                            const DexoptSystemServerArgs& args) {
    // input
    insertIfNonNegative(input_fds, args.dexFd);
    insertIfNonNegative(input_fds, args.profileFd);
    insertOnlyNonNegative(input_fds, args.bootClasspathFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathImageFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathVdexFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathOatFds);
    insertOnlyNonNegative(input_fds, args.classloaderFds);
    // output
    insertIfNonNegative(output_fds, args.imageFd);
    insertIfNonNegative(output_fds, args.vdexFd);
    insertIfNonNegative(output_fds, args.oatFd);
  }

  int cid_;
  std::unique_ptr<ExecUtils> exec_utils_;
};

class OdrDexoptCompilationOS final : public OdrDexopt {
 public:
  static OdrDexoptCompilationOS* Create(int cid, std::unique_ptr<ExecUtils> exec_utils) {
    return new OdrDexoptCompilationOS(cid, std::move(exec_utils));
  }

  int DexoptBcpExtension(const DexoptBcpExtArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<int> input_fds, output_fds;
    if (!insertFd(input_fds, args.profileFd) ||
        !insertFd(input_fds, args.dirtyImageObjectsFd) ||
        !insertFd(output_fds, args.imageFd) ||
        !insertFd(output_fds, args.vdexFd) ||
        !insertFd(output_fds, args.oatFd)) {
      *error_msg = "Some required FDs for dexopting BCP extension are missing";
      return -1;
    }
    insertOnlyNonNegative(input_fds, args.dexFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathFds);

    return DexoptCommon(args, TaskType::DEXOPT_BCP_EXTENSION, input_fds, output_fds, timed_out,
                        error_msg);
  }

  int DexoptSystemServer(const DexoptSystemServerArgs& args,
                         /*out*/ bool* timed_out,
                         /*out*/ std::string* error_msg) override {
    std::vector<int> input_fds, output_fds;
    if (!insertFd(input_fds, args.dexFd) ||
        !insertFd(output_fds, args.imageFd) ||
        !insertFd(output_fds, args.vdexFd) ||
        !insertFd(output_fds, args.oatFd)) {
      *error_msg = "Some required FDs for dexopting system server jar are missing";
      return -1;
    }
    insertIfNonNegative(input_fds, args.profileFd);
    insertOnlyNonNegative(input_fds, args.bootClasspathFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathImageFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathVdexFds);
    insertOnlyNonNegative(input_fds, args.bootClasspathOatFds);
    insertOnlyNonNegative(input_fds, args.classloaderFds);

    return DexoptCommon(args, TaskType::DEXOPT_SYSTEM_SERVER, input_fds, output_fds, timed_out,
                        error_msg);
  }

 private:
  OdrDexoptCompilationOS(int cid, std::unique_ptr<ExecUtils> exec_utils)
      : cid_(cid), exec_utils_(std::move(exec_utils)) {}

  template<typename Args>
  int DexoptCommon(const Args& args,
                   TaskType task_type,
                   const std::vector<int>& input_fds,
                   const std::vector<int>& output_fds,
                   /*out*/ bool* timed_out,
                   /*out*/ std::string* error_msg) {
    *timed_out = false;
    *error_msg = "Unknown error";

    // Serialize to byte array to send as an opaque object to CompOS (which will later call
    // ADexopt_CreateAndValidateDexoptContext to reconstruct). The reason to use opaque object is
    // to allow the ART APEX to continue to update independently by providing the flexibility for
    // ART to evolve, e.g. add or remove compilation parameters. Thereforce, we choose not to
    // stabilize the arguments to avoid the client/CompOS explicitly depending on them.
    ExtendableParcelable ep;
    ep.taskType = task_type;
    ep.ext.setParcelable(args);
    auto parcel = std::unique_ptr<AParcel, decltype(&AParcel_delete)>(AParcel_create(),
                                                                      AParcel_delete);
    if (ep.writeToParcel(parcel.get()) != STATUS_OK) {
      *error_msg = "Failed to write args to the parcel";
      return -1;
    }
    std::vector<uint8_t> buffer(AParcel_getDataSize(parcel.get()));
    if (AParcel_marshal(parcel.get(), buffer.data(), 0, buffer.size()) != STATUS_OK) {
      *error_msg = "Failed to marshal the parcel";
      return -1;
    }

    // Send the request over RPC binder and wait for the response.
    int exit_code = AComposClient_Request(cid_,
                                          buffer.data(),
                                          buffer.size(),
                                          input_fds.data(),
                                          input_fds.size(),
                                          output_fds.data(),
                                          output_fds.size());

    if (exit_code != 0) {
      if (exit_code == kDex2oatExitCode_Timeout) {
        *timed_out = true;
        *error_msg = "dex2oat has timed out (see service log in the VM)";
      } else {
        *error_msg = "dex2oat failed (see service log in the VM)";
      }
    }
    return exit_code;
  }

  int cid_;
  std::unique_ptr<ExecUtils> exec_utils_;
};

}  // namespace

// static
std::unique_ptr<OdrDexopt> OdrDexopt::Create(const OdrConfig& config,
                                             std::unique_ptr<ExecUtils> exec_utils) {
  if (config.UseCompilationOs()) {
    int cid = config.GetCompilationOsAddress();
    if (USE_NEW_COMPILIATION_OS_API) {
      return std::unique_ptr<OdrDexopt>(OdrDexoptCompilationOS::Create(cid, std::move(exec_utils)));
    } else {
      return std::unique_ptr<OdrDexopt>(
          OdrDexoptCompilationOSCmdline::Create(cid, std::move(exec_utils)));
    }
  } else {
    return std::unique_ptr<OdrDexopt>(OdrDexoptLocal::Create(config.GetDex2Oat(),
                                                             std::move(exec_utils)));
  }
}

}  // namespace odrefresh
}  // namespace art
