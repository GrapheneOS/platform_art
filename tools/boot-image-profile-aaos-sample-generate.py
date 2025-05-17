#!/usr/bin/env python3
#
# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Automates boot image profile generation for Android Automotive.

This script orchestrates the process of generating a boot image profile
by performing the following main stages:

1.  Configuring a device using art/tools/boot-image-profile-configure-device.sh
    to generate a `boot.zip` file.
2.  Executing a simple set of activities (Maps, Play Store) on the Android
    device via ADB shell commands to generate usage profiles.
    Note: This "Starting Maps and Play activities" is a simple CUJ
    implementation as a starting point. It may need to be expanded or customized
    for better profiling by reflecting real usage.
3.  Generating boot image profiles based on the generated `boot.zip` file
    and the collected device profiles, using
    art/tools/boot-image-profile-generate.sh.

The script requires the ANDROID_BUILD_TOP environment variable to be set.

Example Usage:
    $ source build/envsetup.sh # Or equivalent setup
    $ python3 art/tools/boot-image-profile-aaos-sample-generate.py

Example Output Trace:
    --- Stage 1: Configuring Device ---
    --- Stage 2: Starting ADB/CUJ Sequence ---
    --- Stage 3: Running Generation Script ---
    --- All stages completed successfully! ---
"""

import os
import shlex
import subprocess
import sys
import time
from typing import List, Optional


# --- Helper Functions ---
def print_message(message: str) -> None:
  """Prints a message to stderr."""
  print(f"{message}", file=sys.stderr)


def print_separator() -> None:
  """Prints a separator line."""
  print_message("---------------------------------------\n")


# --- End Helper Functions ---

# --- Configuration ---
# Pattern to look for in logcat to indicate the device is ready for profiling.
# Set to None to skip the logcat check and only wait for ADB device.
# LOGCAT_READY_PATTERN: Optional[str] = (
#     "Displayed com.android.car.carlauncher/.CarLauncher"
# )
LOGCAT_READY_PATTERN = None  # Example: Disable logcat check

# Logcat filters to apply when monitoring for the ready pattern.
LOGCAT_FILTER_SPECS: List[str] = [
    "ActivityTaskManager:I",
    "*:S",
]

# Ensure ANDROID_BUILD_TOP is set and resolve its absolute path.
ANDROID_BUILD_TOP: str = os.getenv("ANDROID_BUILD_TOP", "")
if not ANDROID_BUILD_TOP:
  print_message(
      "Error: ANDROID_BUILD_TOP environment variable is not set. "
      "Please run `source build/envsetup.sh` and select a target."
  )
  sys.exit(1)
ANDROID_BUILD_TOP = os.path.abspath(ANDROID_BUILD_TOP)
print(f"--- Using ANDROID_BUILD_TOP: {ANDROID_BUILD_TOP} ---")

# Define paths to required scripts and generated files.
_SCRIPT_DIR = "art/tools"  # Relative path to scripts within build top
_FRAMEWORKS_CONFIG_DIR = "frameworks/base/config"  # Relative path to denylist

SCRIPT1_PATH: str = os.path.join(
    ANDROID_BUILD_TOP, _SCRIPT_DIR, "boot-image-profile-configure-device.sh"
)
SCRIPT1_ARG: str = "boot.zip"

SCRIPT2_PATH: str = os.path.join(
    ANDROID_BUILD_TOP, _SCRIPT_DIR, "boot-image-profile-generate.sh"
)
BOOT_ZIP_ARG: str = os.path.join(ANDROID_BUILD_TOP, "boot.zip")
DENYLIST_ARG: str = os.path.join(
    ANDROID_BUILD_TOP, _FRAMEWORKS_CONFIG_DIR, "preloaded-classes-denylist"
)
MAPS_PROF_ARG: str = os.path.join(ANDROID_BUILD_TOP, "maps.prof")
VENDING_PROF_ARG: str = os.path.join(ANDROID_BUILD_TOP, "vending.prof")

# Arguments for the generation script (SCRIPT2).
SCRIPT2_ARGS_LIST: List[str] = [
    ANDROID_BUILD_TOP,
    BOOT_ZIP_ARG,
    DENYLIST_ARG,
    MAPS_PROF_ARG,
    VENDING_PROF_ARG,
    "--profman-arg",
    "--upgrade-startup-to-hot=false",
]

# Package names and activities for the CUJ steps.
MAPS_PACKAGE: str = (
    "com.google.android.apps.maps/com.google.android.maps.MapsActivity"
)
VENDING_PACKAGE: str = (
    "com.android.vending/com.google.android.finsky.activities.MainActivity"
)

# Duration to sleep after a CUJ activity is launched, to allow it to run
# before taking a snapshot of the profile.
CUJ_ACTIVITY_SLEEP_SECONDS: int = 15

# Duration to sleep after starting logcat, to allow it to initialize.
LOGCAT_START_SLEEP_SECONDS: float = 0.5

# --- End Configuration ---


def run_generic_command(
    command_list: List[str],
    check: bool = True,
    capture: bool = True,
    cmd_timeout: Optional[int | float] = None,
    **kwargs,
) -> Optional[subprocess.CompletedProcess]:
  """Runs a command, handling errors and timeouts.

  Args:
      command_list: A list of strings representing the command and its
        arguments.
      check: If True, raise CalledProcessError on non-zero exit code. If False,
        return the CompletedProcess object regardless of exit code.
      capture: If True, capture stdout/stderr. If False, let them pass through.
      cmd_timeout: Timeout in seconds for the command.
      **kwargs: Additional arguments to pass to subprocess.run.

  Returns:
      A CompletedProcess object or None on error.

  Raises:
      subprocess.CalledProcessError: If check is True and the command fails.
      subprocess.TimeoutExpired: If the command times out and check is True.
  """
  command_str = " ".join(shlex.quote(part) for part in command_list)
  print_message(f"--- Running command: {command_str} ---")
  try:
    result = subprocess.run(
        command_list,
        check=check,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        text=True,  # Use text mode for string I/O
        encoding="utf-8",  # Explicitly set encoding
        errors="replace",  # Replace uninterpretable characters
        timeout=cmd_timeout,
        **kwargs,
    )
    # If check=True, subprocess.run already raises on error.
    # If check=False, we just return the result.
    return result
  except FileNotFoundError:
    print_message(
        f"Error: Command not found: '{command_list[0]}'. Is it in your PATH?"
    )
  except subprocess.TimeoutExpired:
    print_message(
        f"Error: Command '{command_str}' timed out after {cmd_timeout} seconds."
    )
    if check:
      raise
  except subprocess.CalledProcessError as e:
    print_message(
        f"Error: Command '{command_str}' failed with exit code {e.returncode}."
    )
    if e.stdout:
      print_message(f"--- stdout ---\n{e.stdout.strip()}")
    if e.stderr:
      print_message(f"--- stderr ---\n{e.stderr.strip()}")
    if check:
      raise
  except Exception as e:
    print_message(f"Unexpected Python error running {command_str}: {e}")
  return None  # Return None on any caught exception


def run_shell_script(
    script_path: str, *script_args: str, interpreter: str = "bash"
) -> bool:
  """Runs a shell script using the specified interpreter.

  Args:
      script_path: The path to the shell script.
      *script_args: Positional arguments to pass to the script.
      interpreter: The shell interpreter to use (e.g., "bash", "sh").

  Returns:
      True if the script exited with code 0, False otherwise or if an
      error occurred running the command.
  """
  command = [interpreter, script_path] + list(script_args)
  print_message(f"--- Running script: {script_path} ---")
  # Run with check=False so we can inspect the return code manually
  result = run_generic_command(command, check=False, capture=True)

  if result is None:
    # An exception occurred in run_generic_command
    print_message(f"--- Script {script_path} command execution failed ---")
    print_separator()
    return False

  # Output captured stdout/stderr for user information
  if result.stdout:
    print_message(f"--- stdout ---\n{result.stdout.strip()}")
  if result.stderr:
    print_message(f"--- stderr ---\n{result.stderr.strip()}")

  if result.returncode == 0:
    print_message(f"--- Script {script_path} SUCCESS ---")
    print_separator()
    return True
  print_message(
      f"--- Script {script_path} FAILED with exit code {result.returncode} ---"
  )
  print_separator()
  return False


def wait_for_device_ready(
    timeout_seconds: int = 360,
    check_interval: int = 1,
    logcat_pattern: Optional[str] = LOGCAT_READY_PATTERN,
    logcat_filters: List[str] = LOGCAT_FILTER_SPECS,
) -> bool:
  """Waits for ADB connection and optionally a specific logcat pattern.

  Args:
      timeout_seconds: Maximum time in seconds to wait.
      check_interval: Time in seconds to wait between logcat reads.
      logcat_pattern: The string or regex pattern to look for in logcat. If
        None, only waits for ADB device connection.
      logcat_filters: List of logcat filter specifications (e.g., "TAG:LEVEL").

  Returns:
      True if the device becomes ready within the timeout, False otherwise.
  """
  print_message(f"--- Waiting for ADB device (max {timeout_seconds}s) ---")
  start_time = time.time()

  print_message("[Wait Step 1/2] Waiting for ADB connection...")
  adb_wait_cmd = ["adb", "wait-for-device"]
  try:
    # Using run here is simpler as wait-for-device holds until ready or timeout
    subprocess.run(
        adb_wait_cmd,
        timeout=timeout_seconds,
        capture_output=True,
        text=True,
        check=True,  # check=True is appropriate here
        encoding="utf-8",
        errors="replace",
    )
    elapsed_time = time.time() - start_time
    print_message(
        "[Wait Step 1/2] Complete: Device connected. (Elapsed:"
        f" {elapsed_time:.1f}s)"
    )
  except (
      subprocess.TimeoutExpired,
      subprocess.CalledProcessError,
      FileNotFoundError,
  ) as e:
    print_message(f"ERROR: Initial ADB connection failed: {e}")
    print_message("Is device connected? Is adb in PATH?")
    print_separator()
    return False
  except Exception as e:
    print_message(f"ERROR during 'adb wait-for-device': {e}")
    print_separator()
    return False

  if not logcat_pattern:
    print_message(
        "[Wait Step 2/2] Skipping logcat pattern check"
        " (LOGCAT_READY_PATTERN is None)."
    )
    print_message("--- Device ready based on ADB connection ---")
    print_separator()
    return True

  logcat_proc: Optional[subprocess.Popen] = None
  logcat_found: bool = False
  logcat_failed: bool = False

  print_message(
      "\n[Wait Step 2/2] Starting logcat check for pattern"
      f" '{logcat_pattern}'..."
  )

  try:
    print_message("  Clearing logcat buffer...")
    run_generic_command(["adb", "logcat", "-c"], check=False)

    logcat_cmd = ["adb", "logcat", "-v", "brief"] + logcat_filters
    print_message(
        f"  Starting logcat: {' '.join(shlex.quote(p) for p in logcat_cmd)}"
    )
    try:
      # Use Popen to read line by line
      logcat_proc = subprocess.Popen(
          logcat_cmd,
          stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
          text=True,
          encoding="utf-8",
          errors="replace",
      )
      time.sleep(LOGCAT_START_SLEEP_SECONDS)
    except (FileNotFoundError, Exception) as proc_err:
      print_message(f"ERROR: Failed to start logcat process: {proc_err}")
      logcat_failed = True

    if not logcat_failed:
      while time.time() - start_time < timeout_seconds:
        # Check if logcat process died unexpectedly
        if logcat_proc and logcat_proc.poll() is not None:
          print_message(
              "  Error: logcat process terminated unexpectedly (Code:"
              f" {logcat_proc.returncode})."
              f" Stderr:\n{logcat_proc.stderr.read()}"
          )
          logcat_failed = True
          break

        # Attempt to read line. readline() can block, but the outer
        # timeout loop will eventually catch it.
        try:
          # Check if stdout is not None before reading
          if logcat_proc and logcat_proc.stdout:
            line = logcat_proc.stdout.readline()
            if not line:
              # If readline returns empty string, process might be exiting
              # or stream is just slow. Check poll() again or wait.
              if logcat_proc.poll() is not None:
                print_message("  logcat stream ended.")
                logcat_failed = True
                break
              # Wait a bit if no line was read but process is still running
              time.sleep(check_interval)
              continue

            line = line.strip()
            if not line:
              continue  # Skip empty lines

            if logcat_pattern in line:
              print_message(f'  Found target logcat line: "{line}"')
              print_message(
                  "[Wait Step 2/2] SUCCESS: Found logcat pattern. (Elapsed:"
                  f" {time.time() - start_time:.1f}s)"
              )
              logcat_found = True
              break

        except Exception as read_err:
          print_message(f"  Warning: Error reading logcat stream: {read_err}.")
          # Continue the loop after a short pause
          time.sleep(check_interval)

  finally:
    # Ensure the logcat subprocess is terminated
    if logcat_proc and logcat_proc.poll() is None:
      print_message("  Terminating logcat process...")
      try:
        # Give it a moment to terminate gracefully
        logcat_proc.terminate()
        logcat_proc.wait(timeout=2)
      except subprocess.TimeoutExpired:
        # If it doesn't terminate, kill it
        print_message("  Logcat process did not terminate, killing...")
        logcat_proc.kill()
      except Exception as term_err:
        # Catch any other errors during termination
        print_message(f"  Error during logcat termination: {term_err}")

  if logcat_found:
    print_message("--- Device ready based on logcat pattern ---")
    print_separator()
    return True
  else:
    print_message(
        f"--- Logcat pattern '{logcat_pattern}' not found within timeout or"
        " logcat failed. ---"
    )
    print_separator()
    return False


def execute_adb_step(
    cmd_list: List[str],
    step_num: int,
    description: str,
    allow_fail: bool = False,
) -> bool:
  """Executes a single ADB shell command step.

  Args:
      cmd_list: The list representing the ADB command and its arguments.
      step_num: The sequential number of this step.
      description: A brief description of the step.
      allow_fail: If True, the function returns True even if the command fails
        (non-zero exit code). If False, it returns False on failure.

  Returns:
      True if the step completed successfully or allow_fail is True and
      the command ran (even if it failed). False if the command execution
      itself failed (e.g., command not found, timeout) or if allow_fail
      is False and the command returned a non-zero exit code.
  """
  print_message(f"\n--- ADB Step {step_num}: {description} ---")
  result = run_generic_command(cmd_list, check=False, capture=True)

  if result is None:
    print_message(
        f"ERROR: Step {step_num} ({description}) command execution failed."
    )
    print_separator()
    return False

  # Decode stdout and stderr if they exist
  cmd_stdout = result.stdout.strip() if result.stdout else ""
  cmd_stderr = result.stderr.strip() if result.stderr else ""

  if result.returncode != 0:
    error_message_base = (
        f"Step {step_num} ({description}) failed with exit code"
        f" {result.returncode}."
    )
    if cmd_stdout:
      print_message(f"Stdout from failed command:\n{cmd_stdout}")
    if cmd_stderr:
      print_message(f"Stderr from failed command:\n{cmd_stderr}")
    if allow_fail:
      print_message(
          f"Warning: {error_message_base} Proceeding as allow_fail is True.",
      )
      print_separator()
      return True  # Command ran, allowed to fail
    else:
      print_message(f"ERROR: {error_message_base} Stopping ADB sequence.")
      print_separator()
      return False  # Command ran, not allowed to fail

  # Command succeeded (returncode == 0)
  print_separator()
  return True


def execute_adb_step_or_exit(
    cmd_list: List[str],
    step_num: int,
    description: str,
    allow_fail: bool = False,
) -> None:
  """Executes an ADB step and exits if it fails (unless allow_fail is True).

  Args:
      cmd_list: The list representing the ADB command and its arguments.
      step_num: The sequential number of this step.
      description: A brief description of the step.
      allow_fail: If True, the function does not exit on failure, just prints a
        warning.
  """
  if not execute_adb_step(cmd_list, step_num, description, allow_fail):
    print_message(
        f"ERROR: ADB Step {step_num} ({description}) failed. Aborting."
    )
    sys.exit(1)


def main() -> None:
  """Main function to orchestrate the profile generation process."""
  print_message("-" * 20)  # Separator before start
  print_message(f"Starting script from directory: {os.getcwd()}")
  print_message(f"Script 1: {SCRIPT1_PATH}")
  print_message(f"Script 2: {SCRIPT2_PATH}")
  print_message(
      f"Wait method: Logcat pattern ('{LOGCAT_READY_PATTERN}')"
      if LOGCAT_READY_PATTERN
      else "Wait method: Only ADB connection (Logcat pattern disabled)"
  )
  print_message("-" * 20)  # Separator after info

  print_message("\n--- Stage 1: Configuring Device ---")
  print_message(f"\n--- Running Configuration Script ---")
  script1_abs_path = os.path.join(ANDROID_BUILD_TOP, SCRIPT1_PATH)
  if not os.path.exists(script1_abs_path):
    print_message(
        f"Error: Configuration script not found at {script1_abs_path}"
    )
    sys.exit(1)
  if not run_shell_script(script1_abs_path, SCRIPT1_ARG):
    print_message("Configuration Script failed. Aborting.")
    sys.exit(1)
  print_message("Configuration Script successful.")
  print_message("\n--- Waiting for Device Readiness ---")
  if not wait_for_device_ready():
    print_message("Device not ready within timeout. Aborting.")
    sys.exit(1)
  print_message("Device reported ready.")

  print_message("\n--- Stage 2: Starting ADB/CUJ Sequence ---")
  execute_adb_step_or_exit(
      [
          "adb",
          "shell",
          "find /data/misc/profiles -name '*.prof' -exec truncate -s 0 {} \\;",
      ],
      1,
      "Clear existing profiles",
      allow_fail=True,  # Allowed to fail if dir doesn't exist etc.
  )
  execute_adb_step_or_exit(
      ["adb", "shell", "am", "get-current-user"], 2, "Get current user"
  )
  execute_adb_step_or_exit(
      [
          "adb",
          "shell",
          "am",
          "start",
          "-S",
          "-W",
          "-a",
          "android.intent.action.MAIN",
          "-n",
          MAPS_PACKAGE,
      ],
      3,
      f"Start Maps activity ({MAPS_PACKAGE})",
  )
  print_message(
      f"Pausing {CUJ_ACTIVITY_SLEEP_SECONDS} seconds after Maps activity"
      " launch..."
  )
  time.sleep(CUJ_ACTIVITY_SLEEP_SECONDS)
  execute_adb_step_or_exit(
      ["adb", "shell", "cmd", "package", "snapshot-profile", "android"],
      4,
      "Snapshot profile after Maps",
  )
  execute_adb_step_or_exit(
      ["adb", "pull", "/data/misc/profman/android.prof", MAPS_PROF_ARG],
      5,
      f"Pull profile to {MAPS_PROF_ARG}",
  )
  execute_adb_step_or_exit(
      ["adb", "shell", "am", "start", "-S", "-W", "-n", VENDING_PACKAGE],
      6,
      f"Start Vending activity ({VENDING_PACKAGE})",
  )
  print_message(
      f"Pausing {CUJ_ACTIVITY_SLEEP_SECONDS} seconds after Vending activity"
      " launch..."
  )
  time.sleep(CUJ_ACTIVITY_SLEEP_SECONDS)
  execute_adb_step_or_exit(
      ["adb", "shell", "cmd", "package", "snapshot-profile", "android"],
      7,
      "Snapshot profile after Vending",
  )
  execute_adb_step_or_exit(
      ["adb", "pull", "/data/misc/profman/android.prof", VENDING_PROF_ARG],
      8,
      f"Pull profile to {VENDING_PROF_ARG}",
  )

  print_message("\n--- Stage 3: Running Generation Script ---")
  print_message(
      "ADB/CUJ sequence finished (check logs for warnings). Proceeding."
  )
  script2_abs_path = os.path.join(ANDROID_BUILD_TOP, SCRIPT2_PATH)
  if not os.path.exists(script2_abs_path):
    print_message(f"Error: Generation script not found at {script2_abs_path}")
    sys.exit(1)
  if run_shell_script(script2_abs_path, *SCRIPT2_ARGS_LIST):
    print_message("\n--- All stages completed successfully! ---")
    sys.exit(0)
  else:
    print_message(f"\n--- Generation Script ({SCRIPT2_PATH}) failed. ---")
    sys.exit(1)


if __name__ == "__main__":
  main()
