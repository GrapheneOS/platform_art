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

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from typing import Callable, Dict, List, Optional


I18N_APEX = "com.android.i18n"
TZDATA_APEX = "com.android.tzdata"
# Define a more specific type for build variables, which are strings.
BuildVarsDict = Dict[str, str]


def run_subprocess(
    command: List[str],
    cwd: Optional[str] = None,
    env: Optional[Dict[str, str]] = None,
    capture_stdout: bool = False,
) -> subprocess.CompletedProcess:
    """Runs a subprocess command (always with shell=False). Exits on failure.

    Args:
        command: The command to execute as a list of strings. The first
                 element is the program, subsequent are arguments.
        cwd: The current working directory for the subprocess.
        env: Environment variables to set for the subprocess.
        capture_stdout: If True, stdout will be captured. stderr passes through.
                        Defaults to False (both stdout/stderr pass through).
    """
    current_env = os.environ.copy()
    if env:
        current_env.update(env)

    stdout_setting = subprocess.PIPE if capture_stdout else None
    stderr_setting = None

    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=current_env,
            shell=False,
            stdout=stdout_setting,
            stderr=stderr_setting,
            text=True,
            check=True,
        )
        return result
    except subprocess.CalledProcessError as e:
        print(f"Error: Command failed with return code {e.returncode}")
        print(f"  Command: {shlex.join(e.cmd)}")
        if capture_stdout:
            if e.stdout:
                print(f"  Stdout (captured): {e.stdout.strip()}")
            print("  (Stderr from the subprocess should have already printed")
            print("   to the console above.)")
        else:
            print("  (Stdout and stderr from the subprocess should have already")
            print("   printed to the console above.)")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred during subprocess execution: {e}")
        sys.exit(1)


def _build_dumpvars_command(
    vars_to_get: List[str]
) -> List[str]:
    """Constructs the command list for soong_ui.bash --dumpvars-mode.
    Assumes CWD is the Android root.
    """
    soong_ui_path: str = "build/soong/soong_ui.bash"

    vars_string: str = " ".join(vars_to_get)

    command_list: List[str] = [
        soong_ui_path,
        "--dumpvars-mode",
        f"--vars={vars_string}"
    ]
    return command_list


def _parse_dumpvars_output(output_string: str) -> BuildVarsDict:
    """Parses the standard output from soong_ui.bash --dumpvars-mode.

    Args:
        output_string: The raw string output from soong_ui.bash --dumpvars-mode.

    Returns:
        A BuildVarsDict (a dictionary with a predefined structure) containing
        the requested build variables as string key-value pairs. Refer to
        the BuildVarsDict type definition for the specific expected keys.
    """
    parsed_vars: BuildVarsDict = {}
    output_lines: List[str] = output_string.strip().splitlines()

    for line in output_lines:
        # Dumpvars output looks like KEY='value' or KEY="value" or KEY=value
        # Simple split on first '=' is usually sufficient.
        if "=" in line:
            key: str
            value_with_quotes: str
            key, value_with_quotes = line.split("=", 1)

            # Remove potential quotes around the value and strip whitespace
            value = value_with_quotes.strip()
            if value.startswith("'") and value.endswith("'"):
                value = value[1:-1]
            elif value.startswith('"') and value.endswith('"'):
                value = value[1:-1]

            parsed_vars[key.strip()] = value
        else:
            # Lines without '=' are unexpected for variable output, might be
            # warnings/info from the build system itself.
            print(f"Error: Unparseable line from dumpvars output: "
                  f"'{line}'")
            sys.exit(1)

    return parsed_vars


def get_android_build_vars(
    vars_to_get: List[str]
) -> BuildVarsDict:
    """Dumps specified variables from the Android build system.
    Assumes CWD is already the Android root and necessary TARGET_* env vars
    are set.
    """
    command_list = _build_dumpvars_command(vars_to_get=vars_to_get)

    print(f"Executing build variable retrieval command in Android root "
          f"({os.getcwd()}):")
    print(f"  $ {shlex.join(command_list)}")

    process_result = run_subprocess(
        command_list,
        capture_stdout=True
    )

    parsed_vars: BuildVarsDict = _parse_dumpvars_output(
        process_result.stdout)

    for var_name in vars_to_get:
        if var_name not in parsed_vars or not parsed_vars.get(var_name):
            print(f"Error: Essential variable '{var_name}' not found or "
                  "empty in retrieved build variables. This indicates a "
                  "problem with the build setup or dumpvars output.")
            print("\nRetrieved Variables:")
            for k, v in parsed_vars.items():
                print(f"  {k}='{v}'")
            sys.exit(1)

    return parsed_vars


def extract_from_apex(apex_name: str, build_vars: BuildVarsDict):
    """Extract files from an APEX file"""
    host_out = build_vars.get("HOST_OUT")
    target_out = build_vars.get("TARGET_OUT")

    print(f"Extracting from apex: {apex_name}")
    apex_root = os.path.join(target_out, "apex")
    apex_input_file = os.path.join(apex_root, f"{apex_name}.apex")
    apex_out_dir = os.path.join(apex_root, apex_name)

    if not os.path.exists(apex_input_file):
        apex_input_file = os.path.join(apex_root, f"{apex_name}.capex")

    if not os.path.exists(apex_input_file):
        print(f"Error: APEX file for '{apex_name}' not found in "
              f"'{apex_root}'.")
        sys.exit(1)

    shutil.rmtree(apex_out_dir, ignore_errors=True)
    os.makedirs(apex_out_dir, exist_ok=True)

    deapexer_path = os.path.join(host_out, "bin", "deapexer")
    debugfs_path = os.path.join(host_out, "bin", "debugfs")
    fsckerofs_path = os.path.join(host_out, "bin", "fsck.erofs")

    deapexer_command = [
        deapexer_path,
        "--debugfs_path", debugfs_path,
        "--fsckerofs_path", fsckerofs_path,
        "extract",
        apex_input_file,
        apex_out_dir,
    ]
    run_subprocess(deapexer_command)

    host_apex_out_dir = os.path.join(host_out, apex_name)
    shutil.rmtree(host_apex_out_dir, ignore_errors=True)
    os.makedirs(host_apex_out_dir, exist_ok=True)

    etc_src = os.path.join(apex_out_dir, "etc")
    etc_dest = os.path.join(host_apex_out_dir, "etc")
    if os.path.exists(etc_src):
        shutil.copytree(etc_src, etc_dest, dirs_exist_ok=True)
    else:
        print(f"No 'etc' directory found in extracted {apex_name}.")


def host_i18n_data_action(build_vars: BuildVarsDict):
    """Custom action to process i18n data."""
    extract_from_apex(I18N_APEX, build_vars)


def host_tzdata_data_action(build_vars: BuildVarsDict):
    """Custom action to process tzdata data."""
    extract_from_apex(TZDATA_APEX, build_vars)


class Target:
    """Represents an internal build target with potential actions/dependencies.

    Attributes:
        name: The unique identifier for this internal target.
        make_targets: List of actual Soong/Make targets required by this target.
        dependencies: List of names of other internal Targets this one depends on.
        action: An optional function to execute after make_targets are built.
    """

    def __init__(self, name: str,
                 make_targets: Optional[List[str]] = None,
                 dependencies: Optional[List[str]] = None,
                 action: Optional[Callable] = None):
        """Initializes a Target."""
        self.name = name
        self.make_targets = make_targets or []
        self.dependencies = dependencies or []
        self.action = action

    def execute_post_build_action(self, build_vars: BuildVarsDict):
        """Executes the target's post-build action, if defined."""
        if self.action:
            self.action(build_vars)


class Builder:
    """Manages internal target definitions and orchestrates the build process."""

    def __init__(self):
        """Initializes the Builder."""
        self.targets: dict[str, Target] = {}
        self.enabled_internal_targets: List[str] = []
        self.positional_make_targets: List[str] = []
        self._setup_default_targets()

    def _setup_default_targets(self):
        """Defines the built-in internal targets known to this script."""
        # These test_* lists are placeholders; actual dependencies might differ.
        test_art_host_deps = ["dalvikvm", "dexlist"]
        test_host_core_img_outs = [
            "core-oj", "core-libart", "okhttp", "bouncycastle", "apache-xml"
        ]
        test_art_target_deps = [
            "com.android.art.testing", "com.android.conscrypt",
            "com.android.i18n"
        ]
        test_target_core_img_outs = ["core-oj", "core-libart"]

        self.add_target(Target(
            name="extract-host-i18n-data",
            action=host_i18n_data_action,
            make_targets=[I18N_APEX, "deapexer", "debugfs", "fsck.erofs"],
        ))
        self.add_target(Target(
            name="extract-host-tzdata-data",
            action=host_tzdata_data_action,
            make_targets=[TZDATA_APEX, "deapexer", "debugfs", "fsck.erofs"],
        ))
        self.add_target(Target(
            name="build-art-host",
            make_targets=(
                ["art-script"] + test_art_host_deps + test_host_core_img_outs
            ),
            dependencies=[
                "extract-host-i18n-data",
                "extract-host-tzdata-data",
            ],
        ))
        self.add_target(Target(
            name="build-art-target",
            make_targets=(
                ["art-script"] + test_art_target_deps
                + test_target_core_img_outs
            ),
        ))
        self.add_target(Target(
            name="build-art",
            dependencies=["build-art-host", "build-art-target"],
        ))

    def add_target(self, target: Target):
        """Adds or updates an internal target definition.

        Args:
            target: The Target object to add.
        """
        if target.name in self.targets:
            print(f"Error: Redefining internal target '{target.name}'.")
            sys.exit(1)
        self.targets[target.name] = target

    def _collect_recursive(self,
                           target_name: str,
                           collected_make_targets: List[str],
                           collected_actions: List[Target],
                           visited: set[str]):
        """Recursively collects dependencies, detecting cycles.

        Internal helper for collect_targets. Modifies lists in place.

        Args:
            target_name: The name of the internal target to collect.
            collected_make_targets: List to store collected make targets.
            collected_actions: List to store collected Target objects with actions.
            visited: A set of visited target names to detect cycles.
        """
        if target_name in visited:
            print(f"Error: Cycle detected in internal target dependencies "
                  f"involving '{target_name}'.")
            sys.exit(1)

        visited.add(target_name)

        if target_name not in self.targets:
            print(f"Error: Definition error - Internal target '{target_name}' "
                  "(specified as a dependency for another internal target) "
                  "was not found in the defined internal targets"
                  f" {self.targets}.")
            sys.exit(1)

        target = self.targets[target_name]

        for dependency_name in target.dependencies:
            self._collect_recursive(
                dependency_name, collected_make_targets, collected_actions,
                visited
            )

        collected_make_targets.extend(target.make_targets)

        if target.action:
            collected_actions.append(target)

    def collect_targets(self,
                        target_name: str,
                        collected_make_targets: List[str],
                        collected_actions: List[Target]):
        """Collects make targets and actions for an internal target.

        Handles the top-level call for recursive collection.

        Args:
            target_name: The name of the internal target to collect.
            collected_make_targets: List to store collected make targets.
            collected_actions: List to store collected Target objects with actions.
        """
        visited = set()
        self._collect_recursive(target_name, collected_make_targets,
                                collected_actions, visited)

    def build(self, build_vars: BuildVarsDict):
        """Builds targets based on enabled internal and positional targets."""
        all_make_targets: List[str] = []
        all_actions: List[Target] = []

        for internal_target_name in self.enabled_internal_targets:
            if internal_target_name in self.targets:
                self.collect_targets(internal_target_name,
                                     all_make_targets,
                                     all_actions)
            else:
                print(f"Error: Enabled internal target "
                      f"'{internal_target_name}' not found in definitions. "
                      "Exiting.")
                sys.exit(1)

        if self.positional_make_targets:
            all_make_targets.extend(self.positional_make_targets)

        unique_make_targets = list(dict.fromkeys(all_make_targets))

        if unique_make_targets:
            env_for_make = os.environ.copy()
            frameworks_base_dir_path = "frameworks/base"
            if not os.path.isdir(frameworks_base_dir_path):
                # This is often necessary for reduced manifest branches (e.g.,
                # master-art) to allow them to build successfully when certain
                # framework dependencies are not present in the source tree.
                print("Info: 'frameworks/base' directory not found at "
                      f"'{os.path.abspath(frameworks_base_dir_path)}'.")
                print("      Setting SOONG_ALLOW_MISSING_DEPENDENCIES=true and "
                      "TARGET_BUILD_UNBUNDLED=true.")
                env_for_make["SOONG_ALLOW_MISSING_DEPENDENCIES"] = "true"
                env_for_make["TARGET_BUILD_UNBUNDLED"] = "true"

            make_command = ["./build/soong/soong_ui.bash", "--make-mode"]
            make_command.extend(unique_make_targets)

            print_env_parts = []
            # Only show a few key env vars for the log to avoid clutter
            for key in ["TARGET_PRODUCT", "TARGET_RELEASE",
                        "TARGET_BUILD_VARIANT",
                        "SOONG_ALLOW_MISSING_DEPENDENCIES",
                        "TARGET_BUILD_UNBUNDLED"]:
                if key in env_for_make:
                    print_env_parts.append(
                        f"{key}={shlex.quote(env_for_make[key])}"
                    )

            print_cmd_str = " ".join(print_env_parts)
            if print_cmd_str:
                 print_cmd_str += " "
            print_cmd_str += ' '.join(shlex.quote(arg) for arg in make_command)

            print(f"Running make command: {print_cmd_str}")

            run_subprocess(make_command, env=env_for_make)
        else:
            print("No make targets specified or collected.")

        # dict.fromkeys preserves the order, ensuring actions are executed
        # in dependency order.
        unique_actions = list(dict.fromkeys(all_actions))

        if unique_actions:
            print("Executing post-build actions...")
            for target_obj in unique_actions:
                target_obj.execute_post_build_action(build_vars)
        else:
            print("No post-build actions to execute.")


def _setup_env_and_get_primary_build_vars(
    args: argparse.Namespace,
) -> BuildVarsDict:
    """
    Sets up the script's execution environment (CWD, ANDROID_BUILD_TOP env var)
    and retrieves primary build variables from dumpvars.
    """
    actual_android_root = os.path.abspath(args.android_root)
    print(f"Info: Effective Android build root: {actual_android_root}")
    os.environ["ANDROID_BUILD_TOP"] = actual_android_root
    try:
        os.chdir(actual_android_root)
        print(f"Info: Changed CWD to: {actual_android_root}")
    except Exception as e:
        print(f"Error: Could not change CWD to {actual_android_root}: {e}")
        sys.exit(1)

    required_env_vars = ["TARGET_PRODUCT", "TARGET_RELEASE",
                         "TARGET_BUILD_VARIANT"]
    missing_vars = [v for v in required_env_vars if not os.environ.get(v)]
    if missing_vars:
        error_msg1 = (
            "Error: The following essential environment variables must be set:"
        )
        error_msg_part2 = f"        {', '.join(missing_vars)}."
        print(error_msg1)
        print(error_msg_part2)
        sys.exit(1)

    vars_to_get_via_dumpvars = [
        "HOST_OUT", "TARGET_OUT", "TARGET_ARCH", "HOST_OUT_JAVA_LIBRARIES"
    ]

    build_vars_from_dumpvars: BuildVarsDict = get_android_build_vars(
        vars_to_get=vars_to_get_via_dumpvars,
    )
    return build_vars_from_dumpvars


def parse_command_line_arguments(builder: Builder) -> argparse.ArgumentParser:
    """Parses args, populates builder lists, and returns the parser.

    Args:
        builder: The Builder instance to populate with parsed targets.

    Returns:
        The configured ArgumentParser object.
    """
    parser = argparse.ArgumentParser(
        description="Builds ART targets with Soong, handling APEX extractions."
    )
    # Arguments for enabling internal targets
    parser.add_argument(
        "--extract-host-i18n-data",
        action="store_true",
        help="Build and extract host i18n data (activates internal target)."
    )
    parser.add_argument(
        "--extract-host-tzdata-data",
        action="store_true",
        help="Build and extract host tzdata data (activates internal target)."
    )
    parser.add_argument(
        "--build-art-host",
        action="store_true",
        help="Build build-art-host components (activates internal target)."
    )
    parser.add_argument(
        "--build-art-target",
        action="store_true",
        help="Build build-art-target components (activates internal target)."
    )
    parser.add_argument(
        "--build-art",
        action="store_true",
        help="Build build-art components (activates internal target)."
    )
    parser.add_argument(
        "--android-root",
        default=os.environ.get("ANDROID_BUILD_TOP", "."),
        help="Path to the Android root directory. Overrides "
             "ANDROID_BUILD_TOP environment variable if set. "
             "Defaults to $ANDROID_BUILD_TOP or '.' if not set."
    )
    # Argument for positional real build targets
    parser.add_argument(
        "positional_targets",
        metavar="MAKE_TARGET",
        nargs="*",
        help="Additional Soong/Make build targets to build."
    )

    args = parser.parse_args()

    if args.extract_host_i18n_data:
        builder.enabled_internal_targets.append("extract-host-i18n-data")
    if args.extract_host_tzdata_data:
        builder.enabled_internal_targets.append("extract-host-tzdata-data")
    if args.build_art_host:
        builder.enabled_internal_targets.append("build-art-host")
    if args.build_art_target:
        builder.enabled_internal_targets.append("build-art-target")
    if args.build_art:
        builder.enabled_internal_targets.append("build-art")

    builder.positional_make_targets.extend(args.positional_targets)

    return parser


def main():
    """Main execution function."""
    builder = Builder()
    parser = parse_command_line_arguments(builder)
    args = parser.parse_args()

    build_vars = _setup_env_and_get_primary_build_vars(args)

    if builder.enabled_internal_targets or builder.positional_make_targets:
        builder.build(build_vars)
    else:
        print("No build targets specified. Printing help:")
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
