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

set -e

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

# Switch the build system to unbundled mode in the reduced manifest branch.
if [ ! -d frameworks/base ]; then
  export TARGET_BUILD_UNBUNDLED=true
fi

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars="OUT_DIR HOST_OUT")"
# Assign to a variable and eval that, since bash ignores any error status from
# the command substitution if it's directly on the eval line.
eval $vars

# First build all the targets still in .mk files (also build normal glibc host
# targets so we know what's needed to run the tests).
build/soong/soong_ui.bash --make-mode "$@" test-art-host-run-test-dependencies build-art-host-tests

# Next build the Linux host Bionic targets in --soong-only mode.
export TARGET_PRODUCT=linux_bionic

# Avoid Soong error about invalid dependencies on disabled libLLVM_android,
# which we get due to the --soong-only mode. (Another variant is to set
# SOONG_ALLOW_MISSING_DEPENDENCIES).
export FORCE_BUILD_LLVM_COMPONENTS=true

soong_out=$OUT_DIR/soong/host/linux_bionic-x86
declare -a bionic_targets
# These are the binaries actually used in tests. Since some of the files are
# java targets or 32 bit we cannot just do the same find for the bin files.
#
# We look at what the earlier build generated to figure out what to ask soong to
# build since we cannot use the .mk defined phony targets.
bionic_targets=(
  $soong_out/bin/dalvikvm
  $soong_out/bin/dalvikvm64
  $soong_out/bin/dex2oat
  $soong_out/bin/dex2oatd
  $soong_out/bin/profman
  $soong_out/bin/profmand
  $soong_out/bin/hiddenapi
  $soong_out/bin/hprof-conv
  $soong_out/bin/signal_dumper
  $soong_out/lib64/libclang_rt.ubsan_standalone-x86_64-android.so
  $(find $HOST_OUT/apex/com.android.art.host.zipapex -type f | sed "s:$HOST_OUT:$soong_out:g")
  $(find $HOST_OUT/lib64 -type f | sed "s:$HOST_OUT:$soong_out:g")
  $(find $HOST_OUT/nativetest64 -type f | sed "s:$HOST_OUT:$soong_out:g"))

echo building ${bionic_targets[*]}

build/soong/soong_ui.bash --make-mode --soong-only "$@" ${bionic_targets[*]}
