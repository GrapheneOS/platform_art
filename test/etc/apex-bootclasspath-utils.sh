#!/bin/bash
#
# Copyright (C) 2022 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This file contains utils for constructing -Xbootclasspath and -Xbootclasspath-location
# for dex2oat and dalvikvm from apex modules list.
#
# Those utils could be used outside of art/test/ to run ART in chroot setup.

# Note: This must start with the CORE_IMG_JARS in Android.common_path.mk
# because that's what we use for compiling the boot.art image.
# It may contain additional modules from TEST_CORE_JARS.
readonly bpath_modules="core-oj core-libart okhttp bouncycastle apache-xml core-icu4j conscrypt"

# Helper function to construct paths for apex modules (for both -Xbootclasspath and
# -Xbootclasspath-location).
#
#  Arguments.
#   ${1}: path prefix.
get_apex_bootclasspath_impl() {
  local -r bpath_prefix="$1"
  local bpath_separator=""
  local bpath=""
  local bpath_jar=""
  for bpath_module in ${bpath_modules}; do
    local apex_module="com.android.art"
    case "$bpath_module" in
      (conscrypt)  apex_module="com.android.conscrypt";;
      (core-icu4j) apex_module="com.android.i18n";;
      (*)          apex_module="com.android.art";;
    esac
    bpath_jar="/apex/${apex_module}/javalib/${bpath_module}.jar"
    bpath+="${bpath_separator}${bpath_prefix}${bpath_jar}"
    bpath_separator=":"
  done
  echo "${bpath}"
}

# Gets a -Xbootclasspath paths with the apex modules.
#
#  Arguments.
#   ${1}: host (y|n).
get_apex_bootclasspath() {
  local -r host="${1}"
  local bpath_prefix=""

  if [[ "${host}" == "y" ]]; then
    bpath_prefix="${ANDROID_HOST_OUT}"
  fi

  get_apex_bootclasspath_impl "${bpath_prefix}"
}

# Gets a -Xbootclasspath-location paths with the apex modules.
#
#  Arguments.
#   ${1}: host (y|n).
get_apex_bootclasspath_locations() {
  local -r host="${1}"
  local bpath_location_prefix=""

  if [[ "${host}" == "y" ]]; then
    if [[ "${ANDROID_HOST_OUT:0:${#ANDROID_BUILD_TOP}+1}" == "${ANDROID_BUILD_TOP}/" ]]; then
      bpath_location_prefix="${ANDROID_HOST_OUT:${#ANDROID_BUILD_TOP}+1}"
    else
      error_msg "ANDROID_BUILD_TOP/ is not a prefix of ANDROID_HOST_OUT"\
                "\nANDROID_BUILD_TOP=${ANDROID_BUILD_TOP}"\
                "\nANDROID_HOST_OUT=${ANDROID_HOST_OUT}"
      exit
    fi
  fi

  get_apex_bootclasspath_impl "${bpath_location_prefix}"
}
