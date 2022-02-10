/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art {
namespace odrefresh {

TEST(OdrCommonTest, ShouldDisableRefresh) {
  EXPECT_TRUE(ShouldDisableRefresh("32"));
  EXPECT_TRUE(ShouldDisableRefresh("33"));
  EXPECT_FALSE(ShouldDisableRefresh("31"));
  EXPECT_FALSE(ShouldDisableRefresh(""));
  EXPECT_FALSE(ShouldDisableRefresh("invalid"));
}

}  // namespace odrefresh
}  // namespace art
