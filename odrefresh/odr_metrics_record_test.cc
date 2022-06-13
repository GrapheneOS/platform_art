/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "odr_metrics_record.h"

#include <string.h>

#include <fstream>

#include "android-base/result-gmock.h"
#include "base/common_art_test.h"

namespace art {
namespace odrefresh {

class OdrMetricsRecordTest : public CommonArtTest {};

using android::base::testing::Ok;
using android::base::testing::HasError;
using android::base::testing::WithMessage;

TEST_F(OdrMetricsRecordTest, HappyPath) {
  const OdrMetricsRecord expected{.odrefresh_metrics_version = 0x1,
                                  .art_apex_version = 0x01233456'789abcde,
                                  .trigger = 0x01020304,
                                  .stage_reached = 0x11121314,
                                  .status = 0x21222324,
                                  .primary_bcp_compilation_seconds = 0x31323334,
                                  .secondary_bcp_compilation_seconds = 0x41424344,
                                  .system_server_compilation_seconds = 0x51525354,
                                  .cache_space_free_start_mib = 0x61626364,
                                  .cache_space_free_end_mib = 0x71727374};

  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";
  ASSERT_THAT(expected.WriteToFile(file_path), Ok());

  OdrMetricsRecord actual {};
  ASSERT_THAT(actual.ReadFromFile(file_path), Ok());

  ASSERT_EQ(expected.odrefresh_metrics_version, actual.odrefresh_metrics_version);
  ASSERT_EQ(expected.art_apex_version, actual.art_apex_version);
  ASSERT_EQ(expected.trigger, actual.trigger);
  ASSERT_EQ(expected.stage_reached, actual.stage_reached);
  ASSERT_EQ(expected.status, actual.status);
  ASSERT_EQ(expected.primary_bcp_compilation_seconds, actual.primary_bcp_compilation_seconds);
  ASSERT_EQ(expected.secondary_bcp_compilation_seconds, actual.secondary_bcp_compilation_seconds);
  ASSERT_EQ(expected.system_server_compilation_seconds, actual.system_server_compilation_seconds);
  ASSERT_EQ(expected.cache_space_free_start_mib, actual.cache_space_free_start_mib);
  ASSERT_EQ(expected.cache_space_free_end_mib, actual.cache_space_free_end_mib);
  ASSERT_EQ(0, memcmp(&expected, &actual, sizeof(expected)));
}

TEST_F(OdrMetricsRecordTest, EmptyInput) {
  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";

  OdrMetricsRecord record{};
  ASSERT_THAT(record.ReadFromFile(file_path), testing::Not(Ok()));
}

TEST_F(OdrMetricsRecordTest, UnexpectedInput) {
  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";

  std::ofstream ofs(file_path);
  ofs << "<not_odrefresh_metrics></not_odrefresh_metrics>";
  ofs.close();

  OdrMetricsRecord record{};
  ASSERT_THAT(
      record.ReadFromFile(file_path),
      HasError(WithMessage("odrefresh_metrics element not found in " + file_path)));
}

TEST_F(OdrMetricsRecordTest, ExpectedElementNotFound) {
  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";

  std::ofstream ofs(file_path);
  ofs << "<odrefresh_metrics>";
  ofs << "<not_valid_metric>25</not_valid_metric>";
  ofs << "</odrefresh_metrics>";
  ofs.close();

  OdrMetricsRecord record{};
  ASSERT_THAT(
      record.ReadFromFile(file_path),
      HasError(WithMessage("Expected Odrefresh metric odrefresh_metrics_version not found")));
}

TEST_F(OdrMetricsRecordTest, UnexpectedOdrefreshMetricsVersion) {
  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";

  std::ofstream ofs(file_path);
  ofs << "<odrefresh_metrics>";
  ofs << "<odrefresh_metrics_version>0</odrefresh_metrics_version>";
  ofs << "</odrefresh_metrics>";
  ofs.close();

  OdrMetricsRecord record{};
  ASSERT_THAT(record.ReadFromFile(file_path),
              HasError(WithMessage("odrefresh_metrics_version 0 is different than expected (1)")));
}

TEST_F(OdrMetricsRecordTest, UnexpectedType) {
  ScratchDir dir(/*keep_files=*/false);
  std::string file_path = dir.GetPath() + "/metrics-record.xml";

  std::ofstream ofs(file_path);
  ofs << "<odrefresh_metrics>";
  ofs << "<odrefresh_metrics_version>" << kOdrefreshMetricsVersion
      << "</odrefresh_metrics_version>";
  ofs << "<art_apex_version>81966764218039518</art_apex_version>";
  ofs << "<trigger>16909060</trigger>";
  ofs << "<stage_reached>286397204</stage_reached>";
  ofs << "<status>abcd</status>";  // It should be an int32.
  ofs << "<primary_bcp_compilation_seconds>825373492</primary_bcp_compilation_seconds>";
  ofs << "<secondary_bcp_compilation_seconds>1094861636</secondary_bcp_compilation_seconds>";
  ofs << "<system_server_compilation_seconds>1364349780</system_server_compilation_seconds>";
  ofs << "<cache_space_free_start_mib>1633837924</cache_space_free_start_mib>";
  ofs << "<cache_space_free_end_mib>1903326068</cache_space_free_end_mib>";
  ofs << "</odrefresh_metrics>";
  ofs.close();

  OdrMetricsRecord record{};
  ASSERT_THAT(
      record.ReadFromFile(file_path),
      HasError(WithMessage("Odrefresh metric status is not an int32")));
}

}  // namespace odrefresh
}  // namespace art
