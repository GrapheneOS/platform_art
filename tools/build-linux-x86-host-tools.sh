 #!/bin/bash
#
# Copyright (C) 2023 The Android Open Source Project
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

if [ ! -e 'build/make/core/Makefile' ]; then
  echo "Script $0 needs to be run at the root of the android tree"
  exit 1
fi

vars="$(build/soong/soong_ui.bash --dumpvars-mode --vars="OUT_DIR DIST_DIR")"
# Assign to a variable and eval that, since bash ignores any error status from
# the command substitution if it's directly on the eval line.
eval $vars

HOST_BINARIES=(
  ${OUT_DIR}/host/linux-x86/bin/dex2oat64
  ${OUT_DIR}/host/linux-x86/bin/dex2oatd64
  ${OUT_DIR}/host/linux-x86/bin/dex2oat
  ${OUT_DIR}/host/linux-x86/bin/dex2oatd
  ${OUT_DIR}/host/linux-x86/bin/deapexer
  ${OUT_DIR}/host/linux-x86/bin/debugfs_static
)

# Build and zip statically linked musl binaries for linux-x86 hosts without the
# standard glibc implementation.
build/soong/soong_ui.bash --make-mode USE_HOST_MUSL=true BUILD_HOST_static=true ${HOST_BINARIES[*]}
prebuilts/build-tools/linux-x86/bin/soong_zip -o ${DIST_DIR}/art-host-tools-linux-x86.zip \
  -j ${HOST_BINARIES[*]/#/-f }
