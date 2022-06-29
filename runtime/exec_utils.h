/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_RUNTIME_EXEC_UTILS_H_
#define ART_RUNTIME_EXEC_UTILS_H_

#include <time.h>

#include <string>
#include <vector>

namespace art {

// Wrapper on fork/execv to run a command in a subprocess.
// These spawn child processes using the environment as it was set when the single instance
// of the runtime (Runtime::Current()) was started.  If no instance of the runtime was started, it
// will use the current environment settings.

bool Exec(const std::vector<std::string>& arg_vector, /*out*/ std::string* error_msg);
int ExecAndReturnCode(const std::vector<std::string>& arg_vector, /*out*/ std::string* error_msg);

// Execute the command specified in `argv_vector` in a subprocess with a timeout.
// Returns the process exit code on success, -1 otherwise.
int ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                      int timeout_sec,
                      /*out*/ bool* timed_out,
                      /*out*/ std::string* error_msg);

// A wrapper class to make the functions above mockable.
class ExecUtils {
 public:
  virtual ~ExecUtils() = default;

  virtual bool Exec(const std::vector<std::string>& arg_vector,
                    /*out*/ std::string* error_msg) const {
    return art::Exec(arg_vector, error_msg);
  }

  virtual int ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                                /*out*/ std::string* error_msg) const {
    return art::ExecAndReturnCode(arg_vector, error_msg);
  }

  virtual int ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                                int timeout_sec,
                                /*out*/ bool* timed_out,
                                /*out*/ std::string* error_msg) const {
    return art::ExecAndReturnCode(arg_vector, timeout_sec, timed_out, error_msg);
  }
};

}  // namespace art

#endif  // ART_RUNTIME_EXEC_UTILS_H_
