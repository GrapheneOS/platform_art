/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "exec_utils.h"

#include <sys/utsname.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <tuple>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "base/file_utils.h"
#include "base/memory_tool.h"
#include "common_runtime_test.h"
#include "gtest/gtest.h"

namespace art {

std::string PrettyArguments(const char* signature);
std::string PrettyReturnType(const char* signature);

std::string GetBin(const std::string& name) {
  if (kIsTargetBuild) {
    std::string android_root(GetAndroidRoot());
    return android_root + "/bin/" + name;
  } else if (std::filesystem::exists("/usr/bin/" + name)) {
    return "/usr/bin/" + name;
  } else {
    return "/bin/" + name;
  }
}

std::tuple<int, int> GetKernelVersion() {
  std::tuple<int, int> version;
  utsname uts;
  CHECK_EQ(uname(&uts), 0);
  CHECK_EQ(sscanf(uts.release, "%d.%d", &std::get<0>(version), &std::get<1>(version)), 2);
  return version;
}

class AlwaysFallbackExecUtils : public ExecUtils {
 protected:
  android::base::unique_fd PidfdOpen(pid_t) const override { return android::base::unique_fd(-1); }
};

class NeverFallbackExecUtils : public ExecUtils {
 protected:
  android::base::unique_fd PidfdOpen(pid_t pid) const override {
    android::base::unique_fd pidfd = ExecUtils::PidfdOpen(pid);
    CHECK_GE(pidfd.get(), 0) << strerror(errno);
    return pidfd;
  }
};

class ExecUtilsTest : public CommonRuntimeTest, public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    CommonRuntimeTest::SetUp();
    bool always_fallback = GetParam();
    if (always_fallback) {
      exec_utils_ = std::make_unique<AlwaysFallbackExecUtils>();
    } else {
      if (GetKernelVersion() >= std::make_tuple(5, 4)) {
        exec_utils_ = std::make_unique<NeverFallbackExecUtils>();
      } else {
        GTEST_SKIP() << "Kernel version older than 5.4";
      }
    }
  }

  std::unique_ptr<ExecUtils> exec_utils_;
};

TEST_P(ExecUtilsTest, ExecSuccess) {
  std::vector<std::string> command;
  command.push_back(GetBin("id"));
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_TRUE(exec_utils_->Exec(command, &error_msg));
  EXPECT_EQ(0U, error_msg.size()) << error_msg;
}

TEST_P(ExecUtilsTest, ExecError) {
  std::vector<std::string> command;
  command.push_back("bogus");
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_FALSE(exec_utils_->Exec(command, &error_msg));
  EXPECT_FALSE(error_msg.empty());
}

TEST_P(ExecUtilsTest, EnvSnapshotAdditionsAreNotVisible) {
  static constexpr const char* kModifiedVariable = "EXEC_SHOULD_NOT_EXPORT_THIS";
  static constexpr int kOverwrite = 1;
  // Set an variable in the current environment.
  EXPECT_EQ(setenv(kModifiedVariable, "NEVER", kOverwrite), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  command.push_back(GetBin("printenv"));
  command.push_back(kModifiedVariable);
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_FALSE(exec_utils_->Exec(command, &error_msg));
  EXPECT_NE(0U, error_msg.size()) << error_msg;
}

TEST_P(ExecUtilsTest, EnvSnapshotDeletionsAreNotVisible) {
  static constexpr const char* kDeletedVariable = "PATH";
  static constexpr int kOverwrite = 1;
  // Save the variable's value.
  const char* save_value = getenv(kDeletedVariable);
  EXPECT_NE(save_value, nullptr);
  // Delete the variable.
  EXPECT_EQ(unsetenv(kDeletedVariable), 0);
  // Test that it is not exported.
  std::vector<std::string> command;
  command.push_back(GetBin("printenv"));
  command.push_back(kDeletedVariable);
  std::string error_msg;
  // Historical note: Running on Valgrind failed due to some memory
  // that leaks in thread alternate signal stacks.
  EXPECT_TRUE(exec_utils_->Exec(command, &error_msg));
  EXPECT_EQ(0U, error_msg.size()) << error_msg;
  // Restore the variable's value.
  EXPECT_EQ(setenv(kDeletedVariable, save_value, kOverwrite), 0);
}

static std::vector<std::string> SleepCommand(int sleep_seconds) {
  std::vector<std::string> command;
  command.push_back(GetBin("sleep"));
  command.push_back(android::base::StringPrintf("%d", sleep_seconds));
  return command;
}

TEST_P(ExecUtilsTest, ExecTimeout) {
  static constexpr int kSleepSeconds = 5;
  static constexpr int kWaitSeconds = 1;
  std::vector<std::string> command = SleepCommand(kSleepSeconds);
  std::string error_msg;
  bool timed_out;
  ASSERT_EQ(exec_utils_->ExecAndReturnCode(command, kWaitSeconds, &timed_out, &error_msg), -1);
  EXPECT_TRUE(timed_out) << error_msg;
}

TEST_P(ExecUtilsTest, ExecNoTimeout) {
  static constexpr int kSleepSeconds = 1;
  static constexpr int kWaitSeconds = 5;
  std::vector<std::string> command = SleepCommand(kSleepSeconds);
  std::string error_msg;
  bool timed_out;
  ASSERT_EQ(exec_utils_->ExecAndReturnCode(command, kWaitSeconds, &timed_out, &error_msg), 0)
      << error_msg;
  EXPECT_FALSE(timed_out);
}

INSTANTIATE_TEST_SUITE_P(AlwaysOrNeverFallback, ExecUtilsTest, testing::Values(true, false));

}  // namespace art
