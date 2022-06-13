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

#include "android-base/logging.h"
#include "odr_metrics_record.h"
#include "tinyxml2.h"

#include <iosfwd>
#include <string>

namespace art {
namespace odrefresh {

namespace {
android::base::Result<int64_t> ReadInt64(tinyxml2::XMLElement* parent, const char* name) {
  tinyxml2::XMLElement* element = parent->FirstChildElement(name);
  if (element == nullptr) {
    return Errorf("Expected Odrefresh metric {} not found", name);
  }

  int64_t metric;
  tinyxml2::XMLError result = element->QueryInt64Text(&metric);
  if (result == tinyxml2::XML_SUCCESS) {
    return metric;
  } else {
    return Errorf("Odrefresh metric {} is not an int64", name);
  }
}

android::base::Result<int32_t> ReadInt32(tinyxml2::XMLElement* parent, const char* name) {
  tinyxml2::XMLElement* element = parent->FirstChildElement(name);
  if (element == nullptr) {
    return Errorf("Expected Odrefresh metric {} not found", name);
  }

  int32_t metric;
  tinyxml2::XMLError result = element->QueryIntText(&metric);
  if (result == tinyxml2::XML_SUCCESS) {
    return metric;
  } else {
    return Errorf("Odrefresh metric {} is not an int32", name);
  }
}

template <typename T>
void AddMetric(tinyxml2::XMLElement* parent, const char* name, const T& value) {
  parent->InsertNewChildElement(name)->SetText(value);
}
}  // namespace

android::base::Result<void> OdrMetricsRecord::ReadFromFile(const std::string& filename) {
  tinyxml2::XMLDocument xml_document;
  tinyxml2::XMLError result = xml_document.LoadFile(filename.data());
  if (result != tinyxml2::XML_SUCCESS) {
    return android::base::Error() << xml_document.ErrorStr();
  }

  tinyxml2::XMLElement* metrics = xml_document.FirstChildElement("odrefresh_metrics");
  if (metrics == nullptr) {
    return Errorf("odrefresh_metrics element not found in {}", filename);
  }

  odrefresh_metrics_version = OR_RETURN(ReadInt32(metrics, "odrefresh_metrics_version"));
  if (odrefresh_metrics_version != kOdrefreshMetricsVersion) {
    return Errorf("odrefresh_metrics_version {} is different than expected ({})",
                  odrefresh_metrics_version,
                  kOdrefreshMetricsVersion);
  }

  art_apex_version = OR_RETURN(ReadInt64(metrics, "art_apex_version"));
  trigger = OR_RETURN(ReadInt32(metrics, "trigger"));
  stage_reached = OR_RETURN(ReadInt32(metrics, "stage_reached"));
  status = OR_RETURN(ReadInt32(metrics, "status"));
  primary_bcp_compilation_seconds = OR_RETURN(
      ReadInt32(metrics, "primary_bcp_compilation_seconds"));
  secondary_bcp_compilation_seconds = OR_RETURN(
      ReadInt32(metrics, "secondary_bcp_compilation_seconds"));
  system_server_compilation_seconds = OR_RETURN(
      ReadInt32(metrics, "system_server_compilation_seconds"));
  cache_space_free_start_mib = OR_RETURN(ReadInt32(metrics, "cache_space_free_start_mib"));
  cache_space_free_end_mib = OR_RETURN(ReadInt32(metrics, "cache_space_free_end_mib"));
  return {};
}

android::base::Result<void> OdrMetricsRecord::WriteToFile(const std::string& filename) const {
  tinyxml2::XMLDocument xml_document;
  tinyxml2::XMLElement* metrics = xml_document.NewElement("odrefresh_metrics");
  xml_document.InsertEndChild(metrics);

  // The order here matches the field order of MetricsRecord.
  AddMetric(metrics, "odrefresh_metrics_version", odrefresh_metrics_version);
  AddMetric(metrics, "art_apex_version", art_apex_version);
  AddMetric(metrics, "trigger", trigger);
  AddMetric(metrics, "stage_reached", stage_reached);
  AddMetric(metrics, "status", status);
  AddMetric(metrics, "primary_bcp_compilation_seconds", primary_bcp_compilation_seconds);
  AddMetric(metrics, "secondary_bcp_compilation_seconds", secondary_bcp_compilation_seconds);
  AddMetric(metrics, "system_server_compilation_seconds", system_server_compilation_seconds);
  AddMetric(metrics, "cache_space_free_start_mib", cache_space_free_start_mib);
  AddMetric(metrics, "cache_space_free_end_mib", cache_space_free_end_mib);

  tinyxml2::XMLError result = xml_document.SaveFile(filename.data(), /*compact=*/true);
  if (result == tinyxml2::XML_SUCCESS) {
    return {};
  } else {
    return android::base::Error() << xml_document.ErrorStr();
  }
}

}  // namespace odrefresh
}  // namespace art
