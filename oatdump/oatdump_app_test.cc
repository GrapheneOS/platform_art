/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "oatdump_test.h"

namespace art {

TEST_P(OatDumpTest, TestDumpOatWithBootImage) {
  TEST_DISABLED_FOR_RISCV64();
  ASSERT_TRUE(GenerateAppOdexFile(GetParam()));
  ASSERT_TRUE(Exec(
      GetParam(), kArgOatApp | kArgBootImage | kArgBcp | kArgIsa, {}, kExpectOat | kExpectCode));
}

TEST_P(OatDumpTest, TestDumpAppImageWithBootImage) {
  TEST_DISABLED_FOR_RISCV64();
  TEST_DISABLED_WITHOUT_BAKER_READ_BARRIERS();  // GC bug, b/126305867
  const std::string app_image_arg = "--app-image-file=" + GetAppImageName();
  ASSERT_TRUE(GenerateAppOdexFile(GetParam(), {app_image_arg}));
  ASSERT_TRUE(Exec(GetParam(),
                   kArgAppImage | kArgImage | kArgBcp | kArgIsa,
                   {"--app-oat=" + GetAppOdexName()},
                   kExpectImage | kExpectOat | kExpectCode));
}

TEST_P(OatDumpTest, TestDumpAppImageInvalidPath) {
  TEST_DISABLED_FOR_RISCV64();
  TEST_DISABLED_WITHOUT_BAKER_READ_BARRIERS();  // GC bug, b/126305867
  const std::string app_image_arg = "--app-image-file=" + GetAppImageName();
  ASSERT_TRUE(GenerateAppOdexFile(GetParam(), {app_image_arg}));
  ASSERT_TRUE(Exec(GetParam(),
                   kArgImage | kArgBcp | kArgIsa,
                   {"--app-image=missing_app_image.art", "--app-oat=" + GetAppOdexName()},
                   /*expects=*/0,
                   /*expect_failure=*/true));
}

}  // namespace art
