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
import shutil
import subprocess
import sys
from typing import List, Optional, Callable

ANDROID_BUILD_TOP = os.environ.get("ANDROID_BUILD_TOP")
HOST_OUT = os.environ.get("ANDROID_HOST_OUT", "out/host/linux-x86")
PRODUCT_OUT = os.environ.get(
    "ANDROID_PRODUCT_OUT", "out/target/product/vsoc_x86_64"
)
TARGET_OUT = os.path.join(PRODUCT_OUT, "system")

I18N_APEX = "com.android.i18n"
TZDATA_APEX = "com.android.tzdata"


def run_subprocess(command: List[str], cwd: Optional[str] = None):
    """Runs a subprocess command and prints it."""
    try:
        subprocess.run(command, check=True, env=os.environ.copy(), cwd=cwd)
    except subprocess.CalledProcessError as e:
        print(f"Command failed with error: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"Error: Command not found: {command[0]}")
        sys.exit(1)


def extract_from_apex(apex_name: str):
    """Extract files from an APEX file.

    Args:
        apex_name: The name of the APEX file (without the .apex extension).
    """
    print(f"Extracting from apex: {apex_name}")
    apex_root = os.path.join(TARGET_OUT, "apex")
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

    deapexer_path = os.path.join(HOST_OUT, "bin", "deapexer")
    # TODO(b/404365860): Use debugfs instead of debugfs_static.
    debugfs_path = os.path.join(HOST_OUT, "bin", "debugfs_static")
    fsckerofs_path = os.path.join(HOST_OUT, "bin", "fsck.erofs")

    deapexer_command = [
        deapexer_path,
        "--debugfs_path", debugfs_path,
        "--fsckerofs_path", fsckerofs_path,
        "extract",
        apex_input_file,
        apex_out_dir,
    ]
    run_subprocess(deapexer_command)

    host_apex_out_dir = os.path.join(HOST_OUT, apex_name)
    shutil.rmtree(host_apex_out_dir, ignore_errors=True)
    os.makedirs(host_apex_out_dir, exist_ok=True)

    etc_src = os.path.join(apex_out_dir, "etc")
    etc_dest = os.path.join(host_apex_out_dir, "etc")
    if os.path.exists(etc_src):
        shutil.copytree(etc_src, etc_dest, dirs_exist_ok=True)
    else:
        print(f"No 'etc' directory found in extracted {apex_name}.")


def host_i18n_data_action():
    """Custom action to process i18n data."""
    extract_from_apex(I18N_APEX)


def host_tzdata_data_action():
    """Custom action to process tzdata data."""
    extract_from_apex(TZDATA_APEX)


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

    def execute_post_build_action(self):
        """Executes the target's post-build action, if defined."""
        if self.action:
            self.action()


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
            make_targets=[I18N_APEX, "deapexer", "debugfs_static", "fsck.erofs"],
        ))
        self.add_target(Target(
            name="extract-host-tzdata-data",
            action=host_tzdata_data_action,
            make_targets=[TZDATA_APEX, "deapexer", "debugfs_static", "fsck.erofs"],
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
        """Adds or updates an internal target definition."""
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

    def build(self):
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
            make_command = (["./build/soong/soong_ui.bash", "--make-mode"]
                            + unique_make_targets)

            print(f"Running make command: {' '.join(make_command)}")
            run_subprocess(make_command, cwd=ANDROID_BUILD_TOP)
        else:
            print("No make targets specified or collected.")

        # dict.fromkeys preserves the order, ensuring actions are executed
        # in dependency order.
        unique_actions = list(dict.fromkeys(all_actions))

        if unique_actions:
             print("Executing post-build actions...")
             for target_obj in unique_actions:
                 target_obj.execute_post_build_action()
        else:
             print("No post-build actions to execute.")


def parse_command_line_arguments(builder: Builder) -> argparse.ArgumentParser:
    """Parses args, populates builder lists, and returns the parser."""
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
    if not ANDROID_BUILD_TOP:
        print("Error: ANDROID_BUILD_TOP environment variable not set. "
              "Please run 'source build/envsetup.sh' and 'lunch' first.")
        sys.exit(1)

    builder = Builder()
    parser = parse_command_line_arguments(builder)

    if builder.enabled_internal_targets or builder.positional_make_targets:
        builder.build()
    else:
        print("No build targets specified. Printing help:")
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
