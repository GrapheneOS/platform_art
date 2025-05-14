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
    --- Stage 1: Running Configuration Script ---
    --- [Waiting for Device Readiness] ---
    --- Stage 2: Starting ADB/CUJ Sequence ---
    --- Stage 3: Running Generation Script ---
    --- All stages completed successfully! ---
"""

import os
import shlex
import subprocess
import sys
from typing import List, Optional

# --- Configuration ---
# Pattern to look for in logcat to indicate the device is ready for profiling.
# Set to None to skip the logcat check and only wait for ADB device.
LOGCAT_READY_PATTERN: Optional[str] = (
    "Displayed com.android.car.carlauncher/.CarLauncher"
)
# LOGCAT_READY_PATTERN = None # Example: Disable logcat check

# Logcat filters to apply when monitoring for the ready pattern.
LOGCAT_FILTER_SPECS: List[str] = [
    "ActivityTaskManager:I",
    "*:S",
]

# Ensure ANDROID_BUILD_TOP is set and resolve its absolute path.
ANDROID_BUILD_TOP: str = os.getenv("ANDROID_BUILD_TOP", "")
if not ANDROID_BUILD_TOP:
  print(
      "Error: ANDROID_BUILD_TOP not set. Set it to your Android source dir.",
      file=sys.stderr,
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
  print(f"--- Running command: {command_str} ---", file=sys.stderr)
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
    print(
        f"Error: Command not found: '{command_list[0]}'. Is it in your PATH?",
        file=sys.stderr,
    )
  except subprocess.TimeoutExpired:
    print(
        f"Error: Command '{command_str}' timed out after"
        f" {cmd_timeout} seconds.",
        file=sys.stderr,
    )
    if check:
      raise
  except subprocess.CalledProcessError as e:
    print(
        f"Error: Command '{command_str}' failed with exit code {e.returncode}.",
        file=sys.stderr,
    )
    if e.stdout:
      print(f"--- stdout ---\n{e.stdout.strip()}", file=sys.stderr)
    if e.stderr:
      print(f"--- stderr ---\n{e.stderr.strip()}", file=sys.stderr)
    if check:
      raise
  except Exception as e:
    print(
        f"Unexpected Python error running {command_str}: {e}",
        file=sys.stderr,
    )
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
  print(f"--- Running script: {script_path} ---", file=sys.stderr)
  # Run with check=False so we can inspect the return code manually
  result = run_generic_command(command, check=False, capture=True)

  if result is None:
    # An exception occurred in run_generic_command
    print(
        f"--- Script {script_path} command execution failed ---",
        file=sys.stderr,
    )
    print("---------------------------------------\n", file=sys.stderr)
    return False

  # Output captured stdout/stderr for user information
  if result.stdout:
    print(f"--- stdout ---\n{result.stdout.strip()}", file=sys.stderr)
  if result.stderr:
    print(f"--- stderr ---\n{result.stderr.strip()}", file=sys.stderr)

  if result.returncode == 0:
    print(f"--- Script {script_path} SUCCESS ---", file=sys.stderr)
    print("---------------------------------------\n", file=sys.stderr)
    return True
  print(
      f"--- Script {script_path} FAILED with exit code {result.returncode} ---",
      file=sys.stderr,
  )
  print("---------------------------------------\n", file=sys.stderr)
  return False


# TODO(b/410924328): Implement this function
def wait_for_device_ready(
    timeout_seconds: int = 360,
    check_interval: int = 1,
    logcat_pattern: Optional[str] = LOGCAT_READY_PATTERN,
    logcat_filters: List[str] = LOGCAT_FILTER_SPECS,
) -> bool:
  """Waits for ADB connection and optionally a specific logcat pattern.

  Args:
      timeout_seconds: Maximum time in seconds to wait.
      check_interval: Time in seconds to wait between logcat reads/checks if the
        stream is empty.
      logcat_pattern: The string or regex pattern to look for in logcat. If
        None, only waits for ADB device connection.
      logcat_filters: List of logcat filter specifications (e.g., "TAG:LEVEL").

  Returns:
      True if the device becomes ready within the timeout, False otherwise.
  """
  return True


# TODO(b/410924328): Implement this function
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
  return True


def main() -> None:
  """Main function to orchestrate the profile generation process."""
  print("-" * 20, file=sys.stderr)  # Separator before start
  print(f"Starting script from directory: {os.getcwd()}", file=sys.stderr)
  print(f"Script 1: {SCRIPT1_PATH}", file=sys.stderr)
  print(f"Script 2: {SCRIPT2_PATH}", file=sys.stderr)
  print(
      f"Wait method: Logcat pattern ('{LOGCAT_READY_PATTERN}')"
      if LOGCAT_READY_PATTERN
      else "Wait method: Only ADB connection (Logcat pattern disabled)",
      file=sys.stderr,
  )
  print("-" * 20, file=sys.stderr)  # Separator after info

  # --- Stage 1: Run Configuration Script ---
  print("\n--- Stage 1: Running Configuration Script ---", file=sys.stderr)
  script1_abs_path = os.path.join(ANDROID_BUILD_TOP, SCRIPT1_PATH)
  if not os.path.exists(script1_abs_path):
    print(
        f"Error: Configuration script not found at {script1_abs_path}",
        file=sys.stderr,
    )
    sys.exit(1)

  if not run_shell_script(script1_abs_path, SCRIPT1_ARG):
    print("Configuration Script failed. Aborting.", file=sys.stderr)
    sys.exit(1)
  print("Configuration Script successful.", file=sys.stderr)

  # --- Wait for Device Readiness ---
  # TODO(b/410924328): Check device readiness using wait_for_device_ready()

  # --- Stage 2: Run ADB Commands / CUJs ---
  # TODO(b/410924328): Implement CUJs using execute_adb_step()

  # --- Stage 3: Run Generation Script ---
  print("\n--- Stage 3: Running Generation Script ---", file=sys.stderr)
  script2_abs_path = os.path.join(ANDROID_BUILD_TOP, SCRIPT2_PATH)
  if not os.path.exists(script2_abs_path):
    print(
        f"Error: Generation script not found at {script2_abs_path}",
        file=sys.stderr,
    )
    sys.exit(1)

  if run_shell_script(script2_abs_path, *SCRIPT2_ARGS_LIST):
    print("\n--- All stages completed successfully! ---", file=sys.stderr)
    sys.exit(0)
  else:
    print(
        f"\n--- Generation Script ({SCRIPT2_PATH}) failed. ---",
        file=sys.stderr,
    )
    sys.exit(1)


if __name__ == "__main__":
  main()
