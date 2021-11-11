#! /bin/bash
#
# Copyright (C) 2021 The Android Open Source Project
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

# Utilities for buildbot. This script is sourced by other buildbot-*.sh scripts.

if [ -t 1 ]; then
  # Color sequences if terminal is a tty.

  red='\033[0;31m'
  green='\033[0;32m'
  yellow='\033[0;33m'
  blue='\033[0;34m'
  magenta='\033[0;35m'
  cyan='\033[0;36m'

  boldred='\033[1;31m'
  boldgreen='\033[1;32m'
  boldyellow='\033[1;33m'
  boldblue='\033[1;34m'
  boldmagenta='\033[1;95m'
  boldcyan='\033[1;36m'

  nc='\033[0m'
fi

function msginfo() {
  local heading="$1"
  shift
  local message="$*"
  echo -e "${green}${heading}${nc} ${message}"
}

function msgwarning() {
  local message="$*"
  echo -e "${boldmagenta}Warning: ${nc}${message}"
}

function msgerror() {
  local message="$*"
  echo -e "${boldred}Error: ${nc}${message}"
}

function msgnote() {
  local message="$*"
  echo -e "${boldcyan}Note: ${nc}${message}"
}
