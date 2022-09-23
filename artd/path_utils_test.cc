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
using ::aidl::com::android::server::art::DexMetadataPath;
using ::aidl::com::android::server::art::ProfilePath;
using ::aidl::com::android::server::art::VdexPath;
using ::android::base::testing::HasError;
using ::android::base::testing::HasValue;
using ::android::base::testing::WithMessage;

using CurProfilePath = ProfilePath::CurProfilePath;
using PrebuiltProfilePath = ProfilePath::PrebuiltProfilePath;
using RefProfilePath = ProfilePath::RefProfilePath;
using TmpRefProfilePath = ProfilePath::TmpRefProfilePath;

using std::literals::operator""s;  // NOLINT

class PathUtilsTest : public CommonArtTest {};

TEST_F(PathUtilsTest, BuildArtBinPath) {
  auto scratch_dir = std::make_unique<ScratchDir>();
  auto art_root_env = ScopedUnsetEnvironmentVariable("ANDROID_ART_ROOT");
  setenv("ANDROID_ART_ROOT", scratch_dir->GetPath().c_str(), /*overwrite=*/1);
  EXPECT_THAT(BuildArtBinPath("foo"), HasValue(scratch_dir->GetPath() + "/bin/foo"));
}

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

TEST_F(PathUtilsTest, BuildOatPathNul) {
  EXPECT_THAT(BuildOatPath(ArtifactsPath{
                  .dexPath = "/a/\0/b.apk"s, .isa = "arm64", .isInDalvikCache = false}),
              HasError(WithMessage("Path '/a/\0/b.apk' has invalid character '\\0'"s)));
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

TEST_F(PathUtilsTest, BuildRefProfilePath) {
  EXPECT_THAT(BuildRefProfilePath(
                  RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/primary.prof"));
}

TEST_F(PathUtilsTest, BuildRefProfilePathPackageNameOk) {
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "...", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/ref/.../primary.prof"));
  EXPECT_THAT(BuildRefProfilePath(
                  RefProfilePath{.packageName = "!@#$%^&*()_+-=", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/ref/!@#$%^&*()_+-=/primary.prof"));
}

TEST_F(PathUtilsTest, BuildRefProfilePathPackageNameWrong) {
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "", .profileName = "primary"}),
              HasError(WithMessage("packageName is empty")));
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = ".", .profileName = "primary"}),
              HasError(WithMessage("Invalid packageName '.'")));
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "..", .profileName = "primary"}),
              HasError(WithMessage("Invalid packageName '..'")));
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "a/b", .profileName = "primary"}),
              HasError(WithMessage("packageName 'a/b' has invalid character '/'")));
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "a\0b"s, .profileName = "primary"}),
              HasError(WithMessage("packageName 'a\0b' has invalid character '\\0'"s)));
}

TEST_F(PathUtilsTest, BuildRefProfilePathProfileNameOk) {
  EXPECT_THAT(
      BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo", .profileName = "."}),
      HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/..prof"));
  EXPECT_THAT(
      BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo", .profileName = ".."}),
      HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/...prof"));
  EXPECT_THAT(BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo",
                                                 .profileName = "!@#$%^&*()_+-="}),
              HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/!@#$%^&*()_+-=.prof"));
}

TEST_F(PathUtilsTest, BuildRefProfilePathProfileNameWrong) {
  EXPECT_THAT(
      BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo", .profileName = ""}),
      HasError(WithMessage("profileName is empty")));
  EXPECT_THAT(
      BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo", .profileName = "a/b"}),
      HasError(WithMessage("profileName 'a/b' has invalid character '/'")));
  EXPECT_THAT(
      BuildRefProfilePath(RefProfilePath{.packageName = "com.android.foo", .profileName = "a\0b"s}),
      HasError(WithMessage("profileName 'a\0b' has invalid character '\\0'"s)));
}

TEST_F(PathUtilsTest, BuildTmpRefProfilePath) {
  EXPECT_THAT(
      BuildTmpRefProfilePath(TmpRefProfilePath{
          .refProfilePath =
              RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"},
          .id = "12345"}),
      HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/primary.prof.12345.tmp"));
}

TEST_F(PathUtilsTest, BuildTmpRefProfilePathIdWrong) {
  EXPECT_THAT(BuildTmpRefProfilePath(TmpRefProfilePath{
                  .refProfilePath =
                      RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"},
                  .id = ""}),
              HasError(WithMessage("id is empty")));
  EXPECT_THAT(BuildTmpRefProfilePath(TmpRefProfilePath{
                  .refProfilePath =
                      RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"},
                  .id = "123/45"}),
              HasError(WithMessage("id '123/45' has invalid character '/'")));
  EXPECT_THAT(BuildTmpRefProfilePath(TmpRefProfilePath{
                  .refProfilePath =
                      RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"},
                  .id = "123\0a"s}),
              HasError(WithMessage("id '123\0a' has invalid character '\\0'"s)));
}

TEST_F(PathUtilsTest, BuildPrebuiltProfilePath) {
  EXPECT_THAT(BuildPrebuiltProfilePath(PrebuiltProfilePath{.dexPath = "/a/b.apk"}),
              HasValue("/a/b.apk.prof"));
}

TEST_F(PathUtilsTest, BuildCurProfilePath) {
  EXPECT_THAT(BuildCurProfilePath(CurProfilePath{
                  .userId = 1, .packageName = "com.android.foo", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/cur/1/com.android.foo/primary.prof"));
}

TEST_F(PathUtilsTest, BuildDexMetadataPath) {
  EXPECT_THAT(BuildDexMetadataPath(DexMetadataPath{.dexPath = "/a/b.apk"}), HasValue("/a/b.dm"));
}

TEST_F(PathUtilsTest, BuildDexMetadataPathForVdex) {
  EXPECT_THAT(BuildDexMetadataPath(VdexPath(DexMetadataPath{.dexPath = "/a/b.apk"})),
              HasValue("/a/b.dm"));
}

TEST_F(PathUtilsTest, BuildProfilePath) {
  EXPECT_THAT(BuildProfileOrDmPath(
                  RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/primary.prof"));
  EXPECT_THAT(
      BuildProfileOrDmPath(TmpRefProfilePath{
          .refProfilePath =
              RefProfilePath{.packageName = "com.android.foo", .profileName = "primary"},
          .id = "12345"}),
      HasValue(android_data_ + "/misc/profiles/ref/com.android.foo/primary.prof.12345.tmp"));
  EXPECT_THAT(BuildProfileOrDmPath(PrebuiltProfilePath{.dexPath = "/a/b.apk"}),
              HasValue("/a/b.apk.prof"));
  EXPECT_THAT(BuildProfileOrDmPath(CurProfilePath{
                  .userId = 1, .packageName = "com.android.foo", .profileName = "primary"}),
              HasValue(android_data_ + "/misc/profiles/cur/1/com.android.foo/primary.prof"));
  EXPECT_THAT(BuildProfileOrDmPath(DexMetadataPath{.dexPath = "/a/b.apk"}), HasValue("/a/b.dm"));
}

TEST_F(PathUtilsTest, BuildVdexPath) {
  EXPECT_THAT(
      BuildVdexPath(ArtifactsPath{.dexPath = "/a/b.apk", .isa = "arm64", .isInDalvikCache = false}),
      HasValue("/a/oat/arm64/b.vdex"));
}

}  // namespace
}  // namespace artd
}  // namespace art
