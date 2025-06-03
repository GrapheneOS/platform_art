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

#ifndef ART_COMPILER_DRIVER_ASSUME_VALUE_OPTIONS_H_
#define ART_COMPILER_DRIVER_ASSUME_VALUE_OPTIONS_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "base/macros.h"
#include "base/sdk_version.h"

namespace art HIDDEN {

class AssumeValueOptions;

namespace detail {

class AssumeValueSignature {
 public:
  bool Equals(const AssumeValueSignature& other) const {
    return class_descriptor_ == other.class_descriptor_ && member_name_ == other.member_name_;
  }

 private:
  friend class art::AssumeValueOptions;

  constexpr AssumeValueSignature(std::string_view class_descriptor, std::string_view member_name)
      : class_descriptor_(class_descriptor), member_name_(member_name) {}

  const std::string_view class_descriptor_;
  const std::string_view member_name_;
};

}  // namespace detail

// A helper class containing configured values that can be safely assumed during compile time.
class AssumeValueOptions final {
 public:
  static constexpr detail::AssumeValueSignature kSdkInt{"Landroid/os/Build$VERSION;", "SDK_INT"};

  static constexpr uint32_t kUnsetSdkInt = static_cast<uint32_t>(SdkVersion::kUnset);

  // The assumed Build.VERSION.SDK_INT value to use for compilation.
  // Defaults to kUnsetSdkInt unless explicitly configured.
  uint32_t SdkInt() const { return sdk_int_; }

  // Whether a valid, explicit SDK_INT has been set for compilation.
  bool HasValidSdkInt() const { return sdk_int_ != kUnsetSdkInt; }

  // Sets the assumed value for a given member, if supported.
  EXPORT bool MaybeSetAssumedValue(const detail::AssumeValueSignature& signature, int32_t value);

  bool MaybeSetAssumedValue(std::string_view class_descriptor,
                            std::string_view member_name,
                            int32_t value) {
    return MaybeSetAssumedValue(detail::AssumeValueSignature(class_descriptor, member_name), value);
  }

 private:
  // The assumed android.os.Build.VERSION.SDK_INT value to use during compilation.
  uint32_t sdk_int_ = kUnsetSdkInt;
};

}  // namespace art

#endif  // ART_COMPILER_DRIVER_ASSUME_VALUE_OPTIONS_H_
