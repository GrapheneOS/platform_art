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

#include <unistd.h>

#include <string>

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/logging.h"
#include "android-base/result.h"
#include "android/binder_auto_utils.h"
#include "android/binder_manager.h"
#include "android/binder_process.h"
#include "tools/tools.h"

namespace art {
namespace artd {

namespace {

using ::aidl::com::android::server::art::BnArtd;
using ::android::base::Error;
using ::android::base::Result;
using ::ndk::ScopedAStatus;

}  // namespace

class Artd : public BnArtd {
  constexpr static const char* kServiceName = "artd";

 public:
  ScopedAStatus isAlive(bool* _aidl_return) override {
    *_aidl_return = true;
    return ScopedAStatus::ok();
  }

  Result<void> Start() {
    LOG(INFO) << "Starting artd";

    ScopedAStatus status = ScopedAStatus::fromStatus(
        AServiceManager_registerLazyService(this->asBinder().get(), kServiceName));
    if (!status.isOk()) {
      return Error() << status.getDescription();
    }

    ABinderProcess_startThreadPool();

    return {};
  }
};

}  // namespace artd
}  // namespace art

int main(const int argc __attribute__((unused)), char* argv[]) {
  setenv("ANDROID_LOG_TAGS", "*:v", 1);
  android::base::InitLogging(argv);

  art::artd::Artd artd;

  if (auto ret = artd.Start(); !ret.ok()) {
    LOG(ERROR) << "Unable to start artd: " << ret.error();
    exit(1);
  }

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "artd shutting down";

  return 0;
}
