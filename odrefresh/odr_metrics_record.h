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

#ifndef ART_ODREFRESH_ODR_METRICS_RECORD_H_
#define ART_ODREFRESH_ODR_METRICS_RECORD_H_

#include <cstdint>
#include <iosfwd>  // For forward-declaration of std::string.

#include "android-base/result.h"
#include "tinyxml2.h"

namespace art {
namespace odrefresh {

// Default location for storing metrics from odrefresh.
constexpr const char* kOdrefreshMetricsFile = "/data/misc/odrefresh/odrefresh-metrics.xml";

// Initial OdrefreshMetrics version
static constexpr int32_t kOdrefreshMetricsVersion = 1;

// MetricsRecord is a simpler container for Odrefresh metric values reported to statsd. The order
// and types of fields here mirror definition of `OdrefreshReported` in
// frameworks/proto_logging/stats/atoms.proto.
struct OdrMetricsRecord {
  int32_t odrefresh_metrics_version;
  int64_t art_apex_version;
  int32_t trigger;
  int32_t stage_reached;
  int32_t status;
  int32_t primary_bcp_compilation_seconds;
  int32_t secondary_bcp_compilation_seconds;
  int32_t system_server_compilation_seconds;
  int32_t cache_space_free_start_mib;
  int32_t cache_space_free_end_mib;

  // Reads a `MetricsRecord` from an XML file.
  // Returns an error if the XML document was not found or parsed correctly.
  android::base::Result<void> ReadFromFile(const std::string& filename);

  // Writes a `MetricsRecord` to an XML file.
  // Returns an error if the XML document was not saved correctly.
  android::base::Result<void> WriteToFile(const std::string& filename) const;
};

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_METRICS_RECORD_H_
