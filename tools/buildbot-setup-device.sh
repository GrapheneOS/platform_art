#!/bin/bash
#
# Copyright (C) 2015 The Android Open Source Project
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

# The work does by this script is (mostly) undone by tools/buildbot-teardown-device.sh.
# Make sure to keep these files in sync.

. "$(dirname $0)/buildbot-utils.sh"

if [ "$1" = --verbose ]; then
  verbose=true
else
  verbose=false
fi

# Setup as root, as some actions performed here require it.
adb version
adb root
adb wait-for-device

msginfo "Date on host"
date

msginfo "Date on device"
adb shell date

host_seconds_since_epoch=$(date -u +%s)

# Get the device time in seconds, but filter the output as some
# devices emit CRLF at the end of the command which then breaks the
# time comparisons in this script (Hammerhead, MRA59G 2457013).
device_seconds_since_epoch=$(adb shell date -u +%s | tr -c -d '[:digit:]')

abs_time_difference_in_seconds=$(expr $host_seconds_since_epoch - $device_seconds_since_epoch)
if [ $abs_time_difference_in_seconds -lt 0 ]; then
  abs_time_difference_in_seconds=$(expr 0 - $abs_time_difference_in_seconds)
fi

seconds_per_hour=3600

# b/187295147 : Disable live-lock kill daemon.
# It can confuse long running processes for issues and kill them.
# This usually manifests as temporarily lost adb connection.
msginfo "Killing llkd, seen killing adb"
adb shell setprop ctl.stop llkd-0
adb shell setprop ctl.stop llkd-1

product_name=$(adb shell getprop ro.build.product)

if [ "x$product_name" = xfugu ]; then
  # Kill logd first, so that when we set the adb buffer size later in this file,
  # it is brought up again.
  msginfo "Killing logd, seen leaking on fugu/N"
  adb shell pkill -9 -U logd logd && msginfo "...logd killed"
fi

# Update date on device if the difference with host is more than one hour.
if [ $abs_time_difference_in_seconds -gt $seconds_per_hour ]; then
  msginfo "Update date on device"
  adb shell date -u @$host_seconds_since_epoch
fi

msginfo "Turn off selinux"
adb shell setenforce 0
$verbose && adb shell getenforce

msginfo "Setting local loopback"
adb shell ifconfig lo up
$verbose && adb shell ifconfig

if $verbose; then
  msginfo "List properties"
  adb shell getprop

  msginfo "Uptime"
  adb shell uptime

  msginfo "Battery info"
  adb shell dumpsys battery
fi

# Fugu only handles buffer size up to 16MB.
if [ "x$product_name" = xfugu ]; then
  buffer_size=16MB
else
  buffer_size=32MB
fi

msginfo "Setting adb buffer size to ${buffer_size}"
adb logcat -G ${buffer_size}
$verbose && adb logcat -g

msginfo "Removing adb spam filter"
adb logcat -P ""
$verbose && adb logcat -p

msginfo "Kill stalled dalvikvm processes"
# 'ps' on M can sometimes hang.
timeout 2s adb shell "ps" >/dev/null
if [[ $? == 124 ]] && [[ "$ART_TEST_RUN_ON_ARM_FVP" != true ]]; then
  msginfo "Rebooting device to fix 'ps'"
  adb reboot
  adb wait-for-device root
else
  processes=$(adb shell "ps" | grep dalvikvm | awk '{print $2}')
  for i in $processes; do adb shell kill -9 $i; done
fi

# Chroot environment.
# ===================

if [[ -n "$ART_TEST_CHROOT" ]]; then
  # Prepare the chroot dir.
  msginfo "Prepare the chroot dir in $ART_TEST_CHROOT"

  # Check that ART_TEST_CHROOT is correctly defined.
  [[ "x$ART_TEST_CHROOT" = x/* ]] || { echo "$ART_TEST_CHROOT is not an absolute path"; exit 1; }

  # Create chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT"

  # Provide property_contexts file(s) in chroot.
  # This is required to have Android system properties work from the chroot.
  # Notes:
  # - In Android N, only '/property_contexts' is expected.
  # - In Android O+, property_context files are expected under /system and /vendor.
  # (See bionic/libc/bionic/system_properties.cpp or
  # bionic/libc/system_properties/contexts_split.cpp for more information.)
  property_context_files="/property_contexts \
    /system/etc/selinux/plat_property_contexts \
    /vendor/etc/selinux/nonplat_property_context \
    /plat_property_contexts \
    /nonplat_property_contexts"
  for f in $property_context_files; do
    adb shell test -f "$f" \
      "&&" mkdir -p "$ART_TEST_CHROOT$(dirname $f)" \
      "&&" cp -f "$f" "$ART_TEST_CHROOT$f"
  done

  # Create directories required for ART testing in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/tmp"
  adb shell mkdir -p "$ART_TEST_CHROOT/data/dalvik-cache"
  adb shell mkdir -p "$ART_TEST_CHROOT/data/local/tmp"

  # Populate /etc in chroot with required files.
  adb shell mkdir -p "$ART_TEST_CHROOT/system/etc"
  adb shell test -L "$ART_TEST_CHROOT/etc" \
    || adb shell ln -s system/etc "$ART_TEST_CHROOT/etc"

  # Provide /proc in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/proc"
  adb shell mount | grep -q "^proc on $ART_TEST_CHROOT/proc type proc " \
    || adb shell mount -t proc proc "$ART_TEST_CHROOT/proc"

  # Provide /sys in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/sys"
  adb shell mount | grep -q "^sysfs on $ART_TEST_CHROOT/sys type sysfs " \
    || adb shell mount -t sysfs sysfs "$ART_TEST_CHROOT/sys"
  # Provide /sys/kernel/debug in chroot.
  adb shell mount | grep -q "^debugfs on $ART_TEST_CHROOT/sys/kernel/debug type debugfs " \
    || adb shell mount -t debugfs debugfs "$ART_TEST_CHROOT/sys/kernel/debug"

  # Provide /dev in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/dev"
  adb shell mount | grep -q "^tmpfs on $ART_TEST_CHROOT/dev type tmpfs " \
    || adb shell mount -o bind /dev "$ART_TEST_CHROOT/dev"
  adb shell mount | grep -q "^devpts on $ART_TEST_CHROOT/dev/pts type devpts " \
    || adb shell mount -o bind /dev/pts "$ART_TEST_CHROOT/dev/pts"
  adb shell mount | grep -q " on $ART_TEST_CHROOT/dev/cpuset type cgroup " \
    || adb shell mount -o bind /dev/cpuset "$ART_TEST_CHROOT/dev/cpuset"

  # Create /apex directory in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/apex"

  # Create /linkerconfig directory in chroot.
  adb shell mkdir -p "$ART_TEST_CHROOT/linkerconfig"

  # Create /bin symlink for shebang compatibility.
  adb shell test -L "$ART_TEST_CHROOT/bin" \
    || adb shell ln -s system/bin "$ART_TEST_CHROOT/bin"
fi
