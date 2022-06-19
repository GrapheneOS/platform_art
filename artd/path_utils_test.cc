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

#include "path_utils.h"

#include "aidl/com/android/server/art/BnArtd.h"
#include "android-base/result-gmock.h"
#include "base/common_art_test.h"
#include "gtest/gtest.h"

namespace art {
namespace artd {
namespace {

using ::aidl::com::android::server::art::ArtifactsPath;
using ::android::base::testing::HasError;
using ::android::base::testing::HasValue;
using ::android::base::testing::WithMessage;

class PathUtilsTest : public CommonArtTest {};

TEST_F(PathUtilsTest, BuildOatPath) {
  EXPECT_THAT(
      BuildOatPath(ArtifactsPath{.dexPath = "/a/b.apk", .isa = "arm64", .isInDalvikCache = false}),
      HasValue("/a/oat/arm64/b.odex"));
}

TEST_F(PathUtilsTest, BuildOatPathDalvikCache) {
  EXPECT_THAT(
      BuildOatPath(ArtifactsPath{.dexPath = "/a/b.apk", .isa = "arm64", .isInDalvikCache = true}),
      HasValue(android_data_ + "/dalvik-cache/arm64/a@b.apk@classes.dex"));
}

TEST_F(PathUtilsTest, BuildOatPathEmptyDexPath) {
  EXPECT_THAT(BuildOatPath(ArtifactsPath{.dexPath = "", .isa = "arm64", .isInDalvikCache = false}),
              HasError(WithMessage("Path is empty")));
}

TEST_F(PathUtilsTest, BuildOatPathRelativeDexPath) {
  EXPECT_THAT(
      BuildOatPath(ArtifactsPath{.dexPath = "a/b.apk", .isa = "arm64", .isInDalvikCache = false}),
      HasError(WithMessage("Path 'a/b.apk' is not an absolute path")));
}

TEST_F(PathUtilsTest, BuildOatPathNonNormalDexPath) {
  EXPECT_THAT(BuildOatPath(ArtifactsPath{
                  .dexPath = "/a/c/../b.apk", .isa = "arm64", .isInDalvikCache = false}),
              HasError(WithMessage("Path '/a/c/../b.apk' is not in normal form")));
}

TEST_F(PathUtilsTest, BuildOatPathInvalidDexExtension) {
  EXPECT_THAT(BuildOatPath(ArtifactsPath{
                  .dexPath = "/a/b.invalid", .isa = "arm64", .isInDalvikCache = false}),
              HasError(WithMessage("Dex path '/a/b.invalid' has an invalid extension")));
}

TEST_F(PathUtilsTest, BuildOatPathInvalidIsa) {
  EXPECT_THAT(BuildOatPath(
                  ArtifactsPath{.dexPath = "/a/b.apk", .isa = "invalid", .isInDalvikCache = false}),
              HasError(WithMessage("Instruction set 'invalid' is invalid")));
}

TEST_F(PathUtilsTest, OatPathToVdexPath) {
  EXPECT_EQ(OatPathToVdexPath("/a/oat/arm64/b.odex"), "/a/oat/arm64/b.vdex");
}

TEST_F(PathUtilsTest, OatPathToArtPath) {
  EXPECT_EQ(OatPathToArtPath("/a/oat/arm64/b.odex"), "/a/oat/arm64/b.art");
}

}  // namespace
}  // namespace artd
}  // namespace art
