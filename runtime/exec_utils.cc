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

#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/macros.h"

#ifdef __BIONIC__
#include <sys/pidfd.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

#include "android-base/scopeguard.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "runtime.h"

namespace art {

namespace {

using ::android::base::StringPrintf;

std::string ToCommandLine(const std::vector<std::string>& args) {
  return android::base::Join(args, ' ');
}

// Fork and execute a command specified in a subprocess.
// If there is a runtime (Runtime::Current != nullptr) then the subprocess is created with the
// same environment that existed when the runtime was started.
// Returns the process id of the child process on success, -1 otherwise.
pid_t ExecWithoutWait(const std::vector<std::string>& arg_vector, std::string* error_msg) {
  // Convert the args to char pointers.
  const char* program = arg_vector[0].c_str();
  std::vector<char*> args;
  for (const auto& arg : arg_vector) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  // fork and exec
  pid_t pid = fork();
  if (pid == 0) {
    // no allocation allowed between fork and exec

    // change process groups, so we don't get reaped by ProcessManager
    setpgid(0, 0);

    // (b/30160149): protect subprocesses from modifications to LD_LIBRARY_PATH, etc.
    // Use the snapshot of the environment from the time the runtime was created.
    char** envp = (Runtime::Current() == nullptr) ? nullptr : Runtime::Current()->GetEnvSnapshot();
    if (envp == nullptr) {
      execv(program, &args[0]);
    } else {
      execve(program, &args[0], envp);
    }
    // This should be regarded as a crash rather than a normal return.
    PLOG(FATAL) << "Failed to execute (" << ToCommandLine(arg_vector) << ")";
    UNREACHABLE();
  } else if (pid == -1) {
    *error_msg = StringPrintf("Failed to execute (%s) because fork failed: %s",
                              ToCommandLine(arg_vector).c_str(),
                              strerror(errno));
    return -1;
  } else {
    return pid;
  }
}

int WaitChild(pid_t pid, const std::vector<std::string>& arg_vector, std::string* error_msg) {
  int status = -1;
  pid_t got_pid = TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
  if (got_pid != pid) {
    *error_msg =
        StringPrintf("Failed to execute (%s) because waitpid failed: wanted %d, got %d: %s",
                     ToCommandLine(arg_vector).c_str(),
                     pid,
                     got_pid,
                     strerror(errno));
    return -1;
  }
  if (!WIFEXITED(status)) {
    *error_msg =
        StringPrintf("Failed to execute (%s) because the child process is terminated by signal %d",
                     ToCommandLine(arg_vector).c_str(),
                     WTERMSIG(status));
    return -1;
  }
  return WEXITSTATUS(status);
}

int WaitChildWithTimeout(pid_t pid,
                         const std::vector<std::string>& arg_vector,
                         int timeout_sec,
                         bool* timed_out,
                         std::string* error_msg) {
  auto cleanup = android::base::make_scope_guard([&]() {
    kill(pid, SIGKILL);
    std::string ignored_error_msg;
    WaitChild(pid, arg_vector, &ignored_error_msg);
  });

#ifdef __BIONIC__
  int pidfd = pidfd_open(pid, /*flags=*/0);
#else
  // There is no glibc wrapper for pidfd_open.
  constexpr int SYS_pidfd_open = 434;
  int pidfd = syscall(SYS_pidfd_open, pid, /*flags=*/0);
#endif
  if (pidfd < 0) {
    *error_msg = StringPrintf("pidfd_open failed for pid %d: %s", pid, strerror(errno));
    return -1;
  }

  struct pollfd pfd;
  pfd.fd = pidfd;
  pfd.events = POLLIN;
  int poll_ret = TEMP_FAILURE_RETRY(poll(&pfd, /*nfds=*/1, timeout_sec * 1000));

  close(pidfd);

  if (poll_ret < 0) {
    *error_msg = StringPrintf("poll failed for pid %d: %s", pid, strerror(errno));
    return -1;
  }
  if (poll_ret == 0) {
    *timed_out = true;
    *error_msg = StringPrintf("Child process %d timed out after %ds. Killing it", pid, timeout_sec);
    return -1;
  }

  cleanup.Disable();
  return WaitChild(pid, arg_vector, error_msg);
}

}  // namespace

int ExecAndReturnCode(const std::vector<std::string>& arg_vector, std::string* error_msg) {
  // Start subprocess.
  pid_t pid = ExecWithoutWait(arg_vector, error_msg);
  if (pid == -1) {
    return -1;
  }

  // Wait for subprocess to finish.
  return WaitChild(pid, arg_vector, error_msg);
}

int ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                      int timeout_sec,
                      bool* timed_out,
                      std::string* error_msg) {
  *timed_out = false;

  // Start subprocess.
  pid_t pid = ExecWithoutWait(arg_vector, error_msg);
  if (pid == -1) {
    return -1;
  }

  // Wait for subprocess to finish.
  return WaitChildWithTimeout(pid, arg_vector, timeout_sec, timed_out, error_msg);
}

bool Exec(const std::vector<std::string>& arg_vector, std::string* error_msg) {
  int status = ExecAndReturnCode(arg_vector, error_msg);
  if (status < 0) {
    // Internal error. The error message is already set.
    return false;
  }
  if (status > 0) {
    *error_msg =
        StringPrintf("Failed to execute (%s) because the child process returns non-zero exit code",
                     ToCommandLine(arg_vector).c_str());
    return false;
  }
  return true;
}

}  // namespace art
