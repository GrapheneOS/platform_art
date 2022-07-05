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

#ifdef __BIONIC__
#include <sys/pidfd.h>
#endif

#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "android-base/scopeguard.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "android-base/unique_fd.h"
#include "base/macros.h"
#include "runtime.h"

namespace art {

namespace {

using ::android::base::StringPrintf;
using ::android::base::unique_fd;

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

int WaitChild(pid_t pid,
              const std::vector<std::string>& arg_vector,
              bool no_wait,
              std::string* error_msg) {
  siginfo_t info;
  // WNOWAIT leaves the child in a waitable state. The call is still blocking.
  int options = WEXITED | (no_wait ? WNOWAIT : 0);
  if (TEMP_FAILURE_RETRY(waitid(P_PID, pid, &info, options)) != 0) {
    *error_msg = StringPrintf("Failed to execute (%s) because waitid failed for pid %d: %s",
                              ToCommandLine(arg_vector).c_str(),
                              pid,
                              strerror(errno));
    return -1;
  }
  if (info.si_pid != pid) {
    *error_msg = StringPrintf("Failed to execute (%s) because waitid failed: wanted %d, got %d: %s",
                              ToCommandLine(arg_vector).c_str(),
                              pid,
                              info.si_pid,
                              strerror(errno));
    return -1;
  }
  if (info.si_code != CLD_EXITED) {
    *error_msg =
        StringPrintf("Failed to execute (%s) because the child process is terminated by signal %d",
                     ToCommandLine(arg_vector).c_str(),
                     info.si_status);
    return -1;
  }
  return info.si_status;
}

int WaitChild(pid_t pid, const std::vector<std::string>& arg_vector, std::string* error_msg) {
  return WaitChild(pid, arg_vector, /*no_wait=*/false, error_msg);
}

// A fallback implementation of `WaitChildWithTimeout` that creates a thread to wait instead of
// relying on `pidfd_open`.
int WaitChildWithTimeoutFallback(pid_t pid,
                                 const std::vector<std::string>& arg_vector,
                                 int timeout_ms,
                                 bool* timed_out,
                                 std::string* error_msg) {
  bool child_exited = false;
  std::condition_variable cv;
  std::mutex m;

  std::thread wait_thread([&]() {
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return child_exited; })) {
      *timed_out = true;
      *error_msg =
          StringPrintf("Child process %d timed out after %dms. Killing it", pid, timeout_ms);
      kill(pid, SIGKILL);
    }
  });

  // Leave the child in a waitable state just in case `wait_thread` sends a `SIGKILL` after the
  // child exits.
  std::string ignored_error_msg;
  WaitChild(pid, arg_vector, /*no_wait=*/true, &ignored_error_msg);

  {
    std::unique_lock<std::mutex> lock(m);
    child_exited = true;
  }
  cv.notify_all();
  wait_thread.join();

  if (*timed_out) {
    WaitChild(pid, arg_vector, &ignored_error_msg);
    return -1;
  }
  return WaitChild(pid, arg_vector, error_msg);
}

int WaitChildWithTimeout(pid_t pid,
                         unique_fd pidfd,
                         const std::vector<std::string>& arg_vector,
                         int timeout_ms,
                         bool* timed_out,
                         std::string* error_msg) {
  auto cleanup = android::base::make_scope_guard([&]() {
    kill(pid, SIGKILL);
    std::string ignored_error_msg;
    WaitChild(pid, arg_vector, &ignored_error_msg);
  });

  struct pollfd pfd;
  pfd.fd = pidfd.get();
  pfd.events = POLLIN;
  int poll_ret = TEMP_FAILURE_RETRY(poll(&pfd, /*nfds=*/1, timeout_ms));

  pidfd.reset();

  if (poll_ret < 0) {
    *error_msg = StringPrintf("poll failed for pid %d: %s", pid, strerror(errno));
    return -1;
  }
  if (poll_ret == 0) {
    *timed_out = true;
    *error_msg = StringPrintf("Child process %d timed out after %dms. Killing it", pid, timeout_ms);
    return -1;
  }

  cleanup.Disable();
  return WaitChild(pid, arg_vector, error_msg);
}

}  // namespace

int ExecUtils::ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                                 std::string* error_msg) const {
  bool ignored_timed_out;
  return ExecAndReturnCode(arg_vector, /*timeout_sec=*/-1, &ignored_timed_out, error_msg);
}

int ExecUtils::ExecAndReturnCode(const std::vector<std::string>& arg_vector,
                                 int timeout_sec,
                                 bool* timed_out,
                                 std::string* error_msg) const {
  *timed_out = false;

  if (timeout_sec > INT_MAX / 1000) {
    *error_msg = "Timeout too large";
    return -1;
  }

  // Start subprocess.
  pid_t pid = ExecWithoutWait(arg_vector, error_msg);
  if (pid == -1) {
    return -1;
  }

  // Wait for subprocess to finish.
  if (timeout_sec >= 0) {
    unique_fd pidfd = PidfdOpen(pid);
    if (pidfd.get() >= 0) {
      return WaitChildWithTimeout(
          pid, std::move(pidfd), arg_vector, timeout_sec * 1000, timed_out, error_msg);
    } else {
      LOG(DEBUG) << StringPrintf(
          "pidfd_open failed for pid %d: %s, falling back", pid, strerror(errno));
      return WaitChildWithTimeoutFallback(
          pid, arg_vector, timeout_sec * 1000, timed_out, error_msg);
    }
  } else {
    return WaitChild(pid, arg_vector, error_msg);
  }
}

bool ExecUtils::Exec(const std::vector<std::string>& arg_vector, std::string* error_msg) const {
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

unique_fd ExecUtils::PidfdOpen(pid_t pid) const {
#ifdef __BIONIC__
  return unique_fd(pidfd_open(pid, /*flags=*/0));
#else
  // There is no glibc wrapper for pidfd_open.
#ifndef SYS_pidfd_open
  constexpr int SYS_pidfd_open = 434;
#endif
  return unique_fd(syscall(SYS_pidfd_open, pid, /*flags=*/0));
#endif
}

}  // namespace art
