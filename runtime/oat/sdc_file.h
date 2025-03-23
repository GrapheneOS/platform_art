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

#ifndef ART_RUNTIME_OAT_SDC_FILE_H_
#define ART_RUNTIME_OAT_SDC_FILE_H_

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/macros.h"
#include "base/os.h"

namespace art HIDDEN {

// A helper class to read a secure dex metadata companion (SDC) file.
//
// Secure dex metadata companion (SDC) file is a file type that augments a secure dex metadata (SDM)
// file with additional metadata.
//
// 1. There may be exactly one SDC file accompanying each SDM file. An SDC file without a
//    corresponding SDM file, or with a mismatching SDM timestamp, is garbage.
// 2. They are always local on device.
// 3. They are only read and written by the ART module.
// 4. A later version of the ART module must be able to understand the contents.
//
// It is a text file in the format of:
//   key1=value1\n
//   key2=value2\n
//   ...
// Repeated keys are not allowed. This is an extensible format, so versioning is not needed.
//
// In principle, ART Service generates an SDC file for an SDM file during installation.
// Specifically, during dexopt, which typically takes place during installation, if there is an SDM
// file while the corresponding SDC file is missing (meaning the SDM file is newly installed) or
// stale (meaning the SDM file is newly replaced), ART Service will generate a new SDC file. This
// means an SDM file without a corresponding SDC file is a transient state and is valid from ART
// Service's perspective.
//
// From the runtime's perspective, an SDM file without a corresponding SDC file is incomplete. That
// means:
// - At app execution time, the runtime ignores an SDM file without a corresponding SDC.
// - ART Service's file GC, which uses the runtime's judgement, considers an SDM file without a
//   corresponding SDC invalid and may clean it up. This may race with a package installation before
//   the SDC is created, but it's rare and the effect is recoverable, so it's considered acceptable.
class EXPORT SdcReader {
 public:
  static std::unique_ptr<SdcReader> Load(const std::string& filename, std::string* error_msg);

  // The mtime of the SDM file on device, in nanoseconds.
  // This is for detecting obsolete SDC files.
  int64_t GetSdmTimestampNs() const { return sdm_timestamp_ns_; }

  // The value of `Runtime::GetApexVersions` at the time where the SDM file was first seen on
  // device. This is for detecting samegrade placebos.
  std::string_view GetApexVersions() const { return apex_versions_; }

 private:
  SdcReader() = default;

  std::string content_;
  int64_t sdm_timestamp_ns_;
  std::string_view apex_versions_;
};

// A helper class to write a secure dex metadata companion (SDC) file.
class EXPORT SdcWriter {
 public:
  // Takes ownership of the file.
  explicit SdcWriter(File&& file) : file_(std::move(file)) {}

  // See `SdcReader::GetSdmTimestampNs`.
  void SetSdmTimestampNs(int64_t value) { sdm_timestamp_ns_ = value; }

  // See `SdcReader::GetApexVersions`.
  void SetApexVersions(std::string_view value) { apex_versions_ = value; }

  bool Save(std::string* error_msg);

 private:
  File file_;
  int64_t sdm_timestamp_ns_ = 0;
  std::string apex_versions_;
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_SDC_FILE_H_
