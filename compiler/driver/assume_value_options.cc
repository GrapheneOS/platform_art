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

#include "assume_value_options.h"

#include "base/logging.h"
#include "com_android_art_flags.h"

namespace art_flags = com::android::art::flags;

namespace art HIDDEN {

bool AssumeValueOptions::MaybeSetAssumedValue(const detail::AssumeValueSignature& signature,
                                              int32_t value) {
  if (kSdkInt.Equals(signature)) {
    DCHECK(art_flags::compile_sdk_int_constant());
    sdk_int_ = value;
    return true;
  }
  return false;
}

}  // namespace art
