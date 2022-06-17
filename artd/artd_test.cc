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

#include "artd.h"

#include <memory>

#include "android/binder_interface_utils.h"
#include "base/common_art_test.h"
#include "gtest/gtest.h"

namespace art {
namespace artd {
namespace {

class ArtdTest : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();
    artd_ = ndk::SharedRefBase::make<Artd>();
  }

  void TearDown() override { CommonArtTest::TearDown(); }

  std::shared_ptr<Artd> artd_;
};

TEST_F(ArtdTest, isAlive) {
  bool result = false;
  artd_->isAlive(&result);
  EXPECT_TRUE(result);
}

}  // namespace
}  // namespace artd
}  // namespace art
