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
#
# TODO This runs a 'm clean' prior to building the targets in order to ensure
# that obsolete kati files don't mess up the build.

set -e

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

# TODO(b/194433871): Set MODULE_BUILD_FROM_SOURCE to disable prebuilt modules,
# which Soong otherwise can create duplicate install rules for in --soong-only
# mode.
soong_args="MODULE_BUILD_FROM_SOURCE=true"

# Switch the build system to unbundled mode in the reduced manifest branch.
if [ ! -d frameworks/base ]; then
  soong_args="$soong_args TARGET_BUILD_UNBUNDLED=true"
fi

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars="OUT_DIR")"
# Assign to a variable and eval that, since bash ignores any error status from
# the command substitution if it's directly on the eval line.
eval $vars

# TODO(b/31559095) Figure out a better way to do this.
#
# There is no good way to force soong to generate host-bionic builds currently
# so this is a hacky workaround.

tmp_soong_var=$(mktemp --tmpdir soong.variables.bak.XXXXXX)

TARGET_PRODUCT=aosp_x86 build/soong/soong_ui.bash --make-mode $soong_args ${OUT_DIR}/soong/build_number.txt
tmp_build_number=$(cat ${OUT_DIR}/soong/build_number.txt)

cat $OUT_DIR/soong/soong.variables > ${tmp_soong_var}

# See comment above about b/123645297 for why we cannot just do m clean. Clear
# out all files except for intermediates and installed files and dexpreopt.config.
find $OUT_DIR/ -maxdepth 1 -mindepth 1 \
               -not -name soong        \
               -not -name host         \
               -not -name target | xargs -I '{}' rm -rf '{}'
find $OUT_DIR/soong/ -maxdepth 1 -mindepth 1   \
                     -not -name .intermediates \
                     -not -name host           \
                     -not -name dexpreopt.config \
                     -not -name target | xargs -I '{}' rm -rf '{}'

python3 <<END - ${tmp_soong_var} ${OUT_DIR}/soong/soong.variables
import json
import sys
x = json.load(open(sys.argv[1]))
x['Allow_missing_dependencies'] = True
x['HostArch'] = 'x86_64'
x['CrossHost'] = 'linux_bionic'
x['CrossHostArch'] = 'x86_64'
if 'CrossHostSecondaryArch' in x:
  del x['CrossHostSecondaryArch']
json.dump(x, open(sys.argv[2], mode='w'))
END

rm $tmp_soong_var

# Write a new build-number
echo ${tmp_build_number}_SOONG_ONLY_BUILD > ${OUT_DIR}/soong/build_number.txt

build/soong/soong_ui.bash --make-mode --skip-config --soong-only $soong_args "$@"
