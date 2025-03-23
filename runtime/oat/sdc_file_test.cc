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

#include "sdc_file.h"

#include <cstdint>
#include <memory>
#include <string>

#include "android-base/file.h"
#include "base/common_art_test.h"
#include "base/macros.h"
#include "base/os.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art HIDDEN {

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;
using ::testing::HasSubstr;
using ::testing::StartsWith;

class SdcFileTestBase : public CommonArtTest {
 protected:
  void SetUp() override {
    CommonArtTest::SetUp();

    scratch_dir_ = std::make_unique<ScratchDir>();
    test_file_ = scratch_dir_->GetPath() + "test.sdc";
  }

  void TearDown() override {
    scratch_dir_.reset();
    CommonArtTest::TearDown();
  }

  std::unique_ptr<ScratchDir> scratch_dir_;
  std::string test_file_;
};

class SdcReaderTest : public SdcFileTestBase {};

TEST_F(SdcReaderTest, Success) {
  ASSERT_TRUE(WriteStringToFile(
      "sdm-timestamp-ns=987654321000000003\napex-versions=/12345678/12345679\n", test_file_));

  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_NE(reader, nullptr) << error_msg;

  EXPECT_EQ(reader->GetApexVersions(), "/12345678/12345679");
  EXPECT_EQ(reader->GetSdmTimestampNs(), INT64_C(987654321000000003));
}

TEST_F(SdcReaderTest, NotFound) {
  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_EQ(reader, nullptr);

  EXPECT_THAT(error_msg, StartsWith("Failed to load sdc file"));
}

TEST_F(SdcReaderTest, MissingApexVersions) {
  ASSERT_TRUE(WriteStringToFile("sdm-timestamp-ns=987654321\n", test_file_));

  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_EQ(reader, nullptr);

  EXPECT_THAT(error_msg, StartsWith("Missing key 'apex-versions' in sdc file"));
}

TEST_F(SdcReaderTest, InvalidSdmTimestamp) {
  ASSERT_TRUE(
      WriteStringToFile("sdm-timestamp-ns=0\napex-versions=/12345678/12345679\n", test_file_));

  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_EQ(reader, nullptr);

  EXPECT_THAT(error_msg, HasSubstr("Invalid 'sdm-timestamp-ns'"));
}

TEST_F(SdcReaderTest, InvalidApexVersions) {
  ASSERT_TRUE(WriteStringToFile("sdm-timestamp-ns=987654321\napex-versions=abc\n", test_file_));

  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_EQ(reader, nullptr);

  EXPECT_THAT(error_msg, HasSubstr("Invalid 'apex-versions'"));
}

TEST_F(SdcReaderTest, UnrecognizedKey) {
  ASSERT_TRUE(WriteStringToFile(
      "sdm-timestamp-ns=987654321\napex-versions=/12345678/12345679\nwrong-key=12345678\n",
      test_file_));

  std::string error_msg;
  std::unique_ptr<SdcReader> reader = SdcReader::Load(test_file_, &error_msg);
  ASSERT_EQ(reader, nullptr);

  EXPECT_THAT(error_msg, HasSubstr("Unrecognized keys"));
}

class SdcWriterTest : public SdcFileTestBase {};

TEST_F(SdcWriterTest, Success) {
  std::unique_ptr<File> file(OS::CreateEmptyFileWriteOnly(test_file_.c_str()));
  ASSERT_NE(file, nullptr);
  SdcWriter writer(std::move(*file));

  writer.SetApexVersions("/12345678/12345679");
  writer.SetSdmTimestampNs(987654321l);

  std::string error_msg;
  ASSERT_TRUE(writer.Save(&error_msg)) << error_msg;

  std::string content;
  ASSERT_TRUE(ReadFileToString(test_file_, &content));

  EXPECT_EQ(content, "sdm-timestamp-ns=987654321\napex-versions=/12345678/12345679\n");
}

TEST_F(SdcWriterTest, SaveFailed) {
  ASSERT_TRUE(WriteStringToFile("", test_file_));

  std::unique_ptr<File> file(OS::OpenFileForReading(test_file_.c_str()));
  ASSERT_NE(file, nullptr);
  SdcWriter writer(
      File(file->Release(), file->GetPath(), /*check_usage=*/false, /*read_only_mode=*/false));

  writer.SetApexVersions("/12345678/12345679");
  writer.SetSdmTimestampNs(987654321l);

  std::string error_msg;
  EXPECT_FALSE(writer.Save(&error_msg));

  EXPECT_THAT(error_msg, StartsWith("Failed to write sdc file"));
}

TEST_F(SdcWriterTest, InvalidSdmTimestamp) {
  std::unique_ptr<File> file(OS::CreateEmptyFileWriteOnly(test_file_.c_str()));
  ASSERT_NE(file, nullptr);
  SdcWriter writer(std::move(*file));

  writer.SetApexVersions("/12345678/12345679");

  std::string error_msg;
  EXPECT_FALSE(writer.Save(&error_msg));

  EXPECT_THAT(error_msg, StartsWith("Invalid 'sdm-timestamp-ns'"));
}

}  // namespace art
