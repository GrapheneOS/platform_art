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

#include "base/flags.h"
#include "com_android_art_flags.h"
#include "com_android_libcore.h"
#include "gtest/gtest.h"

namespace art {

static_assert(COM_ANDROID_ART_FLAGS_TEST == true);

TEST(AconfigFlagsTest, TestFlag) { EXPECT_TRUE(com::android::art::flags::test()); }

TEST(AconfigFlagsTest, TestLibcoreVApisFlag) { EXPECT_TRUE(com::android::libcore::v_apis()); }

TEST(AconfigFlagsTest, TestRwFlag) {
  // EXPECT_TRUE when this flag is fully ramped.
  // EXPECT_TRUE(is_test_rw_flag_enabled());
  is_test_rw_flag_enabled();
}

}  // namespace art
