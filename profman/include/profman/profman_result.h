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

#ifndef ART_PROFMAN_INCLUDE_PROFMAN_PROFMAN_RESULT_H_
#define ART_PROFMAN_INCLUDE_PROFMAN_PROFMAN_RESULT_H_

namespace art {

class ProfmanResult {
 public:
  static constexpr int kErrorUsage = 100;

  // The return codes of processing profiles (running profman in normal mode).
  //
  // Note that installd consumes the returns codes with its own copy of these values
  // (frameworks/native/cmds/installd/dexopt.cpp).
  enum ProcessingResult {
    kSuccess = 0,  // Generic success code for non-analysis runs.
    kCompile = 1,
    kSkipCompilationSmallDelta = 2,
    kErrorBadProfiles = 3,
    kErrorIO = 4,
    kErrorCannotLock = 5,
    kErrorDifferentVersions = 6,
    kSkipCompilationEmptyProfiles = 7,
  };

  // The return codes of running profman with `--copy-and-update-profile-key`.
  enum CopyAndUpdateResult {
    kCopyAndUpdateSuccess = 0,
    kCopyAndUpdateNoUpdate = 21,
    kCopyAndUpdateErrorFailedToUpdateProfile = 22,
    kCopyAndUpdateErrorFailedToSaveProfile = 23,
    kCopyAndUpdateErrorFailedToLoadProfile = 24,
  };
};

}  // namespace art

#endif  // ART_PROFMAN_INCLUDE_PROFMAN_PROFMAN_RESULT_H_
