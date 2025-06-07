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
CORE_IMG_JARS: List[str] = [
    'core-oj',
    'core-libart',
    'okhttp',
    'bouncycastle',
    'apache-xml',
]
HOSTDEX_JARS: List[tuple[str, str]] = [
    ("com.android.conscrypt", "conscrypt"),
    ("com.android.i18n", "core-icu4j"),
]

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
        # The command is constructed from trusted sources (hardcoded values
        # or build system variables). With shell=False, this call is not
        # vulnerable to command injection.
        # nosemgrep: default-ruleset.python.lang.security.audit.dangerous-subprocess-use-audit
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


def perform_copy(source_path: str, target_path: str) -> None:
    """
    Performs a single file copy operation. Overwrites target.
    Exits the script with status 1 if any error occurs.

    Args:
        source_path (str): The path to the source file.
        target_path (str): The path to the target file.

    Returns:
        None
    """
    try:
        if not os.path.exists(source_path):
            print(f"  ERROR: Source file not found! ({source_path})",
                  file=sys.stderr)
            sys.exit(1)

        target_dir: str = os.path.dirname(target_path)
        os.makedirs(target_dir, exist_ok=True)

        shutil.copy(source_path, target_path)

    except Exception as e:
        err_msg = (f"  ERROR: An unexpected error occurred during copy "
                   f"({source_path} -> {target_path}): {e}")
        print(err_msg, file=sys.stderr)
        sys.exit(1)


def host_i18n_data_action(build_vars: BuildVarsDict):
    """Custom action to process i18n data."""
    extract_from_apex(I18N_APEX, build_vars)


def host_tzdata_data_action(build_vars: BuildVarsDict):
    """Custom action to process tzdata data."""
    extract_from_apex(TZDATA_APEX, build_vars)


def _get_core_img_jar_source_path(
    build_vars: BuildVarsDict, jar_basename: str
) -> str:
    """
    Returns the full source path for a given core image JAR.
    This path is where the build system places JARs for dexpreopting.
    """
    out_dir = build_vars.get("OUT_DIR")
    target_arch = build_vars.get("TARGET_ARCH")
    # Path corresponds to art/build/Android.common_path.mk variable
    # CORE_IMG_JAR_DIR.
    core_img_jar_dir = os.path.join(
        out_dir, 'soong', f'dexpreopt_{target_arch}', 'dex_artjars_input'
    )
    return os.path.join(core_img_jar_dir, f"{jar_basename}.jar")


def _get_hostdex_jar_source_path(
    build_vars: BuildVarsDict, jar_basename: str
) -> str:
    """Returns the source path for a given hostdex JAR."""
    host_out_java_libs = build_vars.get("HOST_OUT_JAVA_LIBRARIES")
    return os.path.join(host_out_java_libs, f"{jar_basename}-hostdex.jar")


def copy_core_img_jars_action(build_vars: BuildVarsDict):
    """Copies JARs listed in the global CORE_IMG_JARS."""
    target_base_dir = os.path.join(
        build_vars.get("HOST_OUT"), 'apex/com.android.art/javalib'
    )

    for jar_base_name in CORE_IMG_JARS:
        source: str = _get_core_img_jar_source_path(
            build_vars, jar_base_name
        )
        target: str = os.path.join(target_base_dir, f"{jar_base_name}.jar")
        perform_copy(source, target)


def copy_hostdex_jars_action(build_vars: BuildVarsDict):
    """Copies hostdex JARs to their respective APEX javalib dirs."""
    # We can still use the host variant of `conscrypt` and `core-icu4j`
    # because they don't go into the primary boot image that is used in
    # host gtests, and hence can't lead to checksum mismatches.
    host_out = build_vars.get("HOST_OUT")
    for apex_name, jar_basename in HOSTDEX_JARS:
        source = _get_hostdex_jar_source_path(build_vars, jar_basename)
        target = os.path.join(
            host_out, f'apex/{apex_name}/javalib/{jar_basename}.jar')
        perform_copy(source, target)


def copy_all_host_boot_image_jars_action(build_vars: BuildVarsDict):
    """
    Composite action to copy all necessary JARs for the host boot image.
    This includes CORE_IMG_JARS, conscrypt, and i18n JARs.
    """
    copy_core_img_jars_action(build_vars)
    copy_hostdex_jars_action(build_vars)


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
        self.build_vars: Optional[BuildVarsDict] = None

    def _get_copy_core_img_make_targets(self) -> List[str]:
        """
        Generates the list of make_targets (file paths) for CORE_IMG_JARS.
        These are the source JARs that the copy_core_img_jars_action will
        copy. Relies on self.build_vars having been set.
        """
        make_targets = []
        for jar_base_name in CORE_IMG_JARS:
            source_file_path = _get_core_img_jar_source_path(
                self.build_vars, jar_base_name
            )
            make_targets.append(source_file_path)
        return make_targets

    def setup_default_targets(self, build_vars: BuildVarsDict):
        """Defines built-in targets using build_vars and stores build_vars."""
        self.build_vars = build_vars

        # These test_* lists are placeholders; actual dependencies might differ.
        test_art_host_deps = ["dalvikvm", "dexlist"]
        test_art_target_deps = [
            "com.android.art.testing", "com.android.conscrypt",
            "com.android.i18n"
        ]
        test_target_core_img_outs = ["core-oj", "core-libart"]

        # HOST_CORE_IMG_OUTS
        copy_core_img_make_targets = self._get_copy_core_img_make_targets()
        hostdex_jar_make_targets = [
            _get_hostdex_jar_source_path(self.build_vars, basename)
            for _, basename in HOSTDEX_JARS
        ]
        all_boot_image_make_targets = (copy_core_img_make_targets +
                                       hostdex_jar_make_targets)
        self.add_target(Target(
            name="host_core_img_outs",
            action=copy_all_host_boot_image_jars_action,
            make_targets=all_boot_image_make_targets,
        ))
        # HOST_I18N_DATA
        self.add_target(Target(
            name="extract-host-i18n-data",
            action=host_i18n_data_action,
            make_targets=[I18N_APEX, "deapexer", "debugfs", "fsck.erofs"],
        ))
        # HOST_TZDATA_DATA
        self.add_target(Target(
            name="extract-host-tzdata-data",
            action=host_tzdata_data_action,
            make_targets=[TZDATA_APEX, "deapexer", "debugfs", "fsck.erofs"],
        ))
        self.add_target(Target(
            name="build-art-host",
            make_targets=(
                ["art-script"] + test_art_host_deps
            ),
            dependencies=[
                "host_core_img_outs",
                "extract-host-i18n-data",
                "extract-host-tzdata-data",
            ],
        ))
        # build-art-host-gtests depends on build-art-host  and
        #    $(ART_TEST_HOST_GTEST_DEPENDENCIES)
        # ART_TEST_HOST_GTEST_DEPENDENCIES := $(HOST_I18N_DATA)
        self.add_target(Target(
            name="build-art-host-gtests",
            dependencies=["build-art-host", "extract-host-i18n-data"],
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
                           recursion_stack: set[str],
                           processed_nodes: set[str]):
        """Recursively collects dependencies, detecting cycles.

        Internal helper for collect_targets. Modifies lists in place.

        Args:
            target_name: The name of the internal target to collect.
            collected_make_targets: List to store collected make targets.
            collected_actions: List to store collected Target objects with actions.
            recursion_stack: A set of targets in the current traversal path,
                             used to detect actual cycles.
            processed_nodes: A set of targets that have already been fully
                             processed, used to handle shared dependencies
                             (like diamond dependencies) efficiently.
        """
        if target_name in processed_nodes:
            return

        # If we encounter a node that is already in our current recursion
        # stack, we have found a genuine cycle.
        if target_name in recursion_stack:
            print(f"Error: Cycle detected in internal target dependencies "
                  f"involving '{target_name}'.")
            sys.exit(1)

        if target_name not in self.targets:
            print(f"Error: Definition error - Internal target '{target_name}' "
                  "(specified as a dependency for another internal target) "
                  "was not found in the defined internal targets"
                  f" {self.targets}.")
            sys.exit(1)

        # Add the current target to the recursion stack for this path.
        recursion_stack.add(target_name)
        target = self.targets[target_name]

        for dependency_name in target.dependencies:
            self._collect_recursive(
                dependency_name, collected_make_targets, collected_actions,
                recursion_stack, processed_nodes
            )

        # After traversing all children, remove from the recursion stack and
        # mark as fully processed.
        recursion_stack.remove(target_name)
        processed_nodes.add(target_name)

        collected_make_targets.extend(target.make_targets)

        if target.action:
            collected_actions.append(target)

    def collect_targets(self,
                        target_name: str,
                        collected_make_targets: List[str],
                        collected_actions: List[Target],
                        processed_nodes: set[str]):
        """Collects make targets and actions for an internal target.

        Handles the top-level call for recursive collection.

        Args:
            target_name: The name of the internal target to collect.
            collected_make_targets: List to store collected make targets.
            collected_actions: List to store collected Target objects with actions.
            processed_nodes: A set of targets already processed in this build.
        """
        # The recursion stack is specific to each top-level traversal.
        recursion_stack = set()
        self._collect_recursive(target_name, collected_make_targets,
                                collected_actions, recursion_stack,
                                processed_nodes)

    def build(self):
        """Builds targets based on enabled internal and positional targets."""
        if self.build_vars is None:
            print("Error: build_vars not set in Builder. "
                  "setup_default_targets must be called first.",
                  file=sys.stderr)
            sys.exit(1)

        all_make_targets: List[str] = []
        all_actions: List[Target] = []
        # This set will track all nodes processed during this build run
        # to avoid redundant work. It's passed through the collectors.
        processed_nodes_for_build = set()

        for internal_target_name in self.enabled_internal_targets:
            if internal_target_name in self.targets:
                self.collect_targets(internal_target_name,
                                     all_make_targets,
                                     all_actions,
                                     processed_nodes_for_build)
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
                target_obj.execute_post_build_action(self.build_vars)
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
        "OUT_DIR", "HOST_OUT", "TARGET_OUT", "TARGET_ARCH",
        "HOST_OUT_JAVA_LIBRARIES"
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
        "--build-art-host",
        action="store_true",
        help="Build build-art-host components (activates internal target)."
    )
    parser.add_argument(
        "--build-art-host-gtests",
        action="store_true",
        help="Build build-art-host-gtests components (internal target)."
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

    if args.build_art_host:
        builder.enabled_internal_targets.append("build-art-host")
    if args.build_art_host_gtests:
        builder.enabled_internal_targets.append("build-art-host-gtests")
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
    builder.setup_default_targets(build_vars)

    if builder.enabled_internal_targets or builder.positional_make_targets:
        builder.build()
    else:
        print("No build targets specified. Printing help:")
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
