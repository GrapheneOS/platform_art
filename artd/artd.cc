/*
** Copyright 2021, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <string>
#define LOG_TAG "artd"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <unistd.h>
#include <utils/Errors.h>

#include "aidl/android/os/BnArtd.h"
#include "base/logging.h"
#include "base/macros.h"
#include "tools/tools.h"

using ::ndk::ScopedAStatus;

namespace android {
namespace artd {

class ArtD : public aidl::android::os::BnArtd {
  constexpr static const char* const SERVICE_NAME = "artd";

 public:

  /*
   * Server API
   */

  ScopedAStatus Start() {
    LOG(INFO) << "Starting artd";

    status_t ret = AServiceManager_addService(this->asBinder().get(), SERVICE_NAME);
    if (ret != android::OK) {
      return ScopedAStatus::fromStatus(ret);
    }

    ABinderProcess_startThreadPool();

    return ScopedAStatus::ok();
  }
};

}  // namespace artd
}  // namespace android

int main(const int argc __attribute__((unused)), char* argv[]) {
  setenv("ANDROID_LOG_TAGS", "*:v", 1);
  android::base::InitLogging(argv);

  android::artd::ArtD artd;

  if (auto ret = artd.Start(); !ret.isOk()) {
    LOG(ERROR) << "Unable to start artd: " << ret.getMessage();
    exit(1);
  }

  ABinderProcess_joinThreadPool();

  LOG(INFO) << "artd shutting down";

  return 0;
}
