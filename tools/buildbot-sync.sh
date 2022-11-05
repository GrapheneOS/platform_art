#! /bin/bash
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

# Push ART artifacts and its dependencies to a chroot directory for on-device testing.

set -e

. "$(dirname $0)/buildbot-utils.sh"

# Setup as root, as some actions performed here require it.
adb root
adb wait-for-device

if [[ -z "$ANDROID_BUILD_TOP" ]]; then
  msgerror 'ANDROID_BUILD_TOP environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ANDROID_PRODUCT_OUT" ]]; then
  msgerror 'ANDROID_PRODUCT_OUT environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ART_TEST_CHROOT" ]]; then
  msgerror 'ART_TEST_CHROOT environment variable is empty; ' \
      'please set it before running this script.'
  exit 1
fi


# Sync relevant product directories
# ---------------------------------

(
  cd $ANDROID_PRODUCT_OUT
  for dir in system/* linkerconfig data; do
    [ -d $dir ] || continue
    if [ $dir == system/apex ]; then
      # We sync the APEXes later.
      continue
    fi
    msginfo "Syncing $dir directory..."
    adb shell mkdir -p "$ART_TEST_CHROOT/$dir"
    adb push $dir "$ART_TEST_CHROOT/$(dirname $dir)"
  done
)

# Overwrite the default public.libraries.txt file with a smaller one that
# contains only the public libraries pushed to the chroot directory.
adb push "$ANDROID_BUILD_TOP/art/tools/public.libraries.buildbot.txt" \
  "$ART_TEST_CHROOT/system/etc/public.libraries.txt"

# Create the framework directory if it doesn't exist. Some gtests need it.
adb shell mkdir -p "$ART_TEST_CHROOT/system/framework"

# APEX packages activation.
# -------------------------

adb shell mkdir -p "$ART_TEST_CHROOT/apex"

# Manually "activate" the flattened APEX $1 by syncing it to /apex/$2 in the
# chroot. $2 defaults to $1.
activate_apex() {
  local src_apex=${1}
  local dst_apex=${2:-${src_apex}}

  # Unpack the .apex or .capex file in the product directory, but if we already
  # see a directory we assume buildbot-build.sh has already done it for us and
  # just use it.
  src_apex_path=$ANDROID_PRODUCT_OUT/system/apex/${src_apex}
  if [ ! -d $src_apex_path ]; then
    unset src_apex_file
    if [ -f "${src_apex_path}.apex" ]; then
      src_apex_file="${src_apex_path}.apex"
    elif [ -f "${src_apex_path}.capex" ]; then
      src_apex_file="${src_apex_path}.capex"
    fi
    if [ -z "${src_apex_file}" ]; then
      msgerror "Failed to find .apex or .capex file to extract for ${src_apex_path}"
      exit 1
    fi
    msginfo "Extracting APEX ${src_apex_file}..."
    mkdir -p $src_apex_path
    $ANDROID_HOST_OUT/bin/deapexer --debugfs_path $ANDROID_HOST_OUT/bin/debugfs_static \
      --fsckerofs_path $ANDROID_HOST_OUT/bin/fsck.erofs \
      --blkid_path $ANDROID_HOST_OUT/bin/blkid \
      extract ${src_apex_file} $src_apex_path
  fi

  msginfo "Activating APEX ${src_apex} as ${dst_apex}..."
  adb shell rm -rf "$ART_TEST_CHROOT/apex/${dst_apex}"
  adb push $src_apex_path "$ART_TEST_CHROOT/apex/${dst_apex}"
}

# "Activate" the required APEX modules.
activate_apex com.android.art.testing com.android.art
activate_apex com.android.i18n
activate_apex com.android.runtime
activate_apex com.android.tzdata
activate_apex com.android.conscrypt
activate_apex com.android.os.statsd

# Generate primary boot images on device for testing.
for b in {32,64}; do
  basename="generate-boot-image$b"
  bin_on_host="$ANDROID_PRODUCT_OUT/system/bin/$basename"
  bin_on_device="/data/local/tmp/$basename"
  output_dir="/system/framework/art_boot_images"
  if [ -f $bin_on_host ]; then
    msginfo "Generating the primary boot image ($b-bit)..."
    adb push "$bin_on_host" "$ART_TEST_CHROOT$bin_on_device"
    adb shell mkdir -p "$ART_TEST_CHROOT$output_dir"
    # `compiler-filter=speed-profile` is required because OatDumpTest checks the compiled code in
    # the boot image.
    adb shell chroot "$ART_TEST_CHROOT" \
      "$bin_on_device" --output-dir=$output_dir --compiler-filter=speed-profile
  fi
done
