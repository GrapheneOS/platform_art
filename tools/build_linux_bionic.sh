#!/bin/bash
#
# Copyright (C) 2018 The Android Open Source Project
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

# This will build a target using linux_bionic. It can be called with normal make
# flags.

set -e

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

export TARGET_PRODUCT=linux_bionic

# Avoid Soong error about invalid dependencies on disabled libLLVM_android,
# which we get due to the --soong-only mode. (Another variant is to set
# SOONG_ALLOW_MISSING_DEPENDENCIES).
export FORCE_BUILD_LLVM_COMPONENTS=true

# TODO(b/194433871): Set MODULE_BUILD_FROM_SOURCE to disable prebuilt modules,
# which Soong otherwise can create duplicate install rules for in --soong-only
# mode.
export MODULE_BUILD_FROM_SOURCE=true

# Switch the build system to unbundled mode in the reduced manifest branch.
if [ ! -d frameworks/base ]; then
  export TARGET_BUILD_UNBUNDLED=true
fi

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars="OUT_DIR BUILD_NUMBER")"
# Assign to a variable and eval that, since bash ignores any error status from
# the command substitution if it's directly on the eval line.
eval $vars

# This file is currently not created in --soong-only mode, but some build
# targets depend on it.
printf %s "${BUILD_NUMBER}" > ${OUT_DIR}/soong/build_number.txt

build/soong/soong_ui.bash --make-mode --soong-only "$@"
