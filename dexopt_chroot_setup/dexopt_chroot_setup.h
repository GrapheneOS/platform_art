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

#ifndef ART_DEXOPT_CHROOT_SETUP_DEXOPT_CHROOT_SETUP_H_
#define ART_DEXOPT_CHROOT_SETUP_DEXOPT_CHROOT_SETUP_H_

#include <optional>
#include <string>

#include "aidl/com/android/server/art/BnDexoptChrootSetup.h"
#include "android-base/result.h"
#include "android-base/thread_annotations.h"

namespace art {
namespace dexopt_chroot_setup {

// A service that sets up the chroot environment for Pre-reboot Dexopt.
class DexoptChrootSetup : public aidl::com::android::server::art::BnDexoptChrootSetup {
 public:
  ndk::ScopedAStatus setUp(const std::optional<std::string>& in_otaSlot,
                           bool in_mapSnapshotsForOta) override;

  ndk::ScopedAStatus init() override;

  ndk::ScopedAStatus tearDown(bool in_allowConcurrent) override;

  android::base::Result<void> Start();

 private:
  android::base::Result<void> SetUpChroot(const std::optional<std::string>& ota_slot,
                                          bool map_snapshots_for_ota) const REQUIRES(mu_);

  android::base::Result<void> InitChroot() const REQUIRES(mu_);

  android::base::Result<void> TearDownChroot() const REQUIRES(mu_);

  std::mutex mu_;
};

std::string PathInChroot(std::string_view path);

}  // namespace dexopt_chroot_setup
}  // namespace art

#endif  // ART_DEXOPT_CHROOT_SETUP_DEXOPT_CHROOT_SETUP_H_
