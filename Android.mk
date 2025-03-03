#
# Copyright (C) 2011 The Android Open Source Project
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
#

LOCAL_PATH := $(call my-dir)

art_path := $(LOCAL_PATH)

include $(art_path)/build/Android.common_path.mk

########################################################################
# cpplint rules to style check art source files

include $(art_path)/build/Android.cpplint.mk

########################################################################
# product rules

ART_HOST_DEPENDENCIES := \
  $(ART_HOST_EXECUTABLES) \
  $(ART_HOST_DEX_DEPENDENCIES) \
  $(ART_HOST_SHARED_LIBRARY_DEPENDENCIES)

ifeq ($(ART_BUILD_HOST_DEBUG),true)
ART_HOST_DEPENDENCIES += $(ART_HOST_SHARED_LIBRARY_DEBUG_DEPENDENCIES)
endif

ART_TARGET_DEPENDENCIES := \
  $(ART_TARGET_DEX_DEPENDENCIES)

########################################################################
# test rules

# All the dependencies that must be built ahead of sync-ing them onto the target device.
TEST_ART_TARGET_SYNC_DEPS := $(ADB_EXECUTABLE)

include $(art_path)/build/Android.common_test.mk
include $(art_path)/build/Android.gtest.mk
include $(art_path)/test/Android.run-test.mk

TEST_ART_TARGET_SYNC_DEPS += $(ART_TEST_TARGET_GTEST_DEPENDENCIES) $(ART_TEST_TARGET_RUN_TEST_DEPENDENCIES)

# "mm test-art" to build and run all tests on host and device
.PHONY: test-art
test-art:
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-gtest
test-art-gtest:
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-run-test
test-art-run-test:
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

########################################################################
# host test rules

test-art: test-art-host
test-art-gtest: test-art-host-gtest
test-art-run-test: test-art-host-run-test

VIXL_TEST_DEPENDENCY :=
# We can only run the vixl tests on 64-bit hosts (vixl testing issue) when its a
# top-level build (to declare the vixl test rule).
ifneq ($(HOST_PREFER_32_BIT),true)
ifeq ($(ONE_SHOT_MAKEFILE),)
VIXL_TEST_DEPENDENCY := run-vixl-tests
endif
endif

.PHONY: test-art-host-vixl
test-art-host-vixl: $(VIXL_TEST_DEPENDENCY)

# "mm test-art-host" to build and run all host tests.
.PHONY: test-art-host
test-art-host: test-art-host-gtest test-art-host-run-test \
               test-art-host-vixl test-art-host-dexdump
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All host tests that run solely with the default compiler.
.PHONY: test-art-host-default
test-art-host-default: test-art-host-run-test-default
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All host tests that run solely with the optimizing compiler.
.PHONY: test-art-host-optimizing
test-art-host-optimizing: test-art-host-run-test-optimizing
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All host tests that run solely on the interpreter.
.PHONY: test-art-host-interpreter
test-art-host-interpreter: test-art-host-run-test-interpreter
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All host tests that run solely on the jit.
.PHONY: test-art-host-jit
test-art-host-jit: test-art-host-run-test-jit
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# Primary host architecture variants:
.PHONY: test-art-host$(ART_PHONY_TEST_HOST_SUFFIX)
test-art-host$(ART_PHONY_TEST_HOST_SUFFIX): test-art-host-gtest$(ART_PHONY_TEST_HOST_SUFFIX) \
    test-art-host-run-test$(ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-default$(ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-default$(ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-default$(ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-optimizing$(ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-optimizing$(ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-optimizing$(ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-interpreter$(ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-interpreter$(ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-interpreter$(ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-jit$(ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-jit$(ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-jit$(ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# Secondary host architecture variants:
ifneq ($(HOST_PREFER_32_BIT),true)
.PHONY: test-art-host$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
test-art-host$(2ND_ART_PHONY_TEST_HOST_SUFFIX): test-art-host-gtest$(2ND_ART_PHONY_TEST_HOST_SUFFIX) \
    test-art-host-run-test$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-default$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-default$(2ND_ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-default$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-optimizing$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-optimizing$(2ND_ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-optimizing$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-interpreter$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-interpreter$(2ND_ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-interpreter$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-host-jit$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
test-art-host-jit$(2ND_ART_PHONY_TEST_HOST_SUFFIX): test-art-host-run-test-jit$(2ND_ART_PHONY_TEST_HOST_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)
endif

# Dexdump/list regression test.
.PHONY: test-art-host-dexdump
test-art-host-dexdump: $(addprefix $(HOST_OUT_EXECUTABLES)/, dexdump dexlist)
	ANDROID_HOST_OUT=$(realpath $(HOST_OUT)) art/test/dexdump/run-all-tests

########################################################################
# target test rules

test-art: test-art-target
test-art-gtest: test-art-target-gtest
test-art-run-test: test-art-target-run-test

# "mm test-art-target" to build and run all target tests.
.PHONY: test-art-target
test-art-target: test-art-target-gtest test-art-target-run-test
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All target tests that run solely with the default compiler.
.PHONY: test-art-target-default
test-art-target-default: test-art-target-run-test-default
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All target tests that run solely with the optimizing compiler.
.PHONY: test-art-target-optimizing
test-art-target-optimizing: test-art-target-run-test-optimizing
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All target tests that run solely on the interpreter.
.PHONY: test-art-target-interpreter
test-art-target-interpreter: test-art-target-run-test-interpreter
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# All target tests that run solely on the jit.
.PHONY: test-art-target-jit
test-art-target-jit: test-art-target-run-test-jit
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# Primary target architecture variants:
.PHONY: test-art-target$(ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target$(ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-gtest$(ART_PHONY_TEST_TARGET_SUFFIX) \
    test-art-target-run-test$(ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-default$(ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-default$(ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-default$(ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-optimizing$(ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-optimizing$(ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-optimizing$(ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-interpreter$(ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-interpreter$(ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-interpreter$(ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-jit$(ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-jit$(ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-jit$(ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

# Secondary target architecture variants:
ifdef 2ND_ART_PHONY_TEST_TARGET_SUFFIX
.PHONY: test-art-target$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target$(2ND_ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-gtest$(2ND_ART_PHONY_TEST_TARGET_SUFFIX) \
    test-art-target-run-test$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-default$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-default$(2ND_ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-default$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-optimizing$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-optimizing$(2ND_ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-optimizing$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-interpreter$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-interpreter$(2ND_ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-interpreter$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)

.PHONY: test-art-target-jit$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
test-art-target-jit$(2ND_ART_PHONY_TEST_TARGET_SUFFIX): test-art-target-run-test-jit$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)
	$(hide) $(call ART_TEST_PREREQ_FINISHED,$@)
endif

#######################
# Reset LOCAL_PATH because previous includes may override its value.
# Keep this after all "include $(art_path)/..." are done, and before any
# "include $(BUILD_...)".
LOCAL_PATH := $(art_path)

########################################################################
# "m build-art" for quick minimal build
.PHONY: build-art

build-art: build-art-host

# For host, we extract the ICU data from the apex and install it to HOST_OUT/I18N_APEX.
$(HOST_I18N_DATA): $(TARGET_OUT)/apex/$(I18N_APEX).apex $(HOST_OUT)/bin/deapexer
	$(call extract-from-apex,$(I18N_APEX))
	rm -rf $(HOST_OUT)/$(I18N_APEX)
	mkdir -p $(HOST_OUT)/$(I18N_APEX)/
	cp -R $(TARGET_OUT)/apex/$(I18N_APEX)/etc/ $(HOST_OUT)/$(I18N_APEX)/
	touch $@

$(HOST_TZDATA_DATA): $(TARGET_OUT)/apex/$(TZDATA_APEX).apex $(HOST_OUT)/bin/deapexer
	$(call extract-from-apex,$(TZDATA_APEX))
	rm -rf $(HOST_OUT)/$(TZDATA_APEX)
	mkdir -p $(HOST_OUT)/$(TZDATA_APEX)/
	cp -R $(TARGET_OUT)/apex/$(TZDATA_APEX)/etc/ $(HOST_OUT)/$(TZDATA_APEX)/
	touch $@

.PHONY: build-art-host
build-art-host:   $(HOST_OUT_EXECUTABLES)/art $(ART_HOST_DEPENDENCIES) $(HOST_CORE_IMG_OUTS) $(HOST_I18N_DATA) $(HOST_TZDATA_DATA)

build-art: build-art-target

.PHONY: build-art-target
build-art-target: $(TARGET_OUT_EXECUTABLES)/art $(ART_TARGET_DEPENDENCIES) $(TARGET_CORE_IMG_OUTS)

TARGET_BOOT_IMAGE_SYSTEM_DIR := $(PRODUCT_OUT)/system/apex/art_boot_images
TARGET_ART_APEX_SYSTEM := $(PRODUCT_OUT)/system/apex/com.android.art
TARGET_BOOT_IMAGE_PROFILE := $(TARGET_ART_APEX_SYSTEM).testing/etc/boot-image.prof

.PHONY: build-art-simulator-profile
build-art-simulator-profile: $(HOST_OUT_EXECUTABLES)/profmand $(TARGET_CORE_IMG_DEX_FILES) \
		$(PRODUCT_DEX_PREOPT_BOOT_IMAGE_PROFILE_LOCATION)
	mkdir -p $(dir $(TARGET_BOOT_IMAGE_PROFILE))
	# Generate a profile from the core boot jars. This allows the simulator and boot image to use a
	# stable profile that is generated on the host.
	$(HOST_OUT_EXECUTABLES)/profmand \
	  --output-profile-type=boot \
	  --create-profile-from=$(PRODUCT_DEX_PREOPT_BOOT_IMAGE_PROFILE_LOCATION) \
	  $(foreach jar,$(TARGET_CORE_IMG_DEX_FILES),--apk=$(jar)) \
	  $(foreach jar,$(TARGET_CORE_IMG_DEX_LOCATIONS),--dex-location=$(jar)) \
	  --reference-profile-file=$(TARGET_BOOT_IMAGE_PROFILE)

.PHONY: build-art-simulator-boot-image
build-art-simulator-boot-image: $(HOST_OUT_EXECUTABLES)/generate-boot-image64 \
		$(HOST_OUT_EXECUTABLES)/dex2oatd $(TARGET_CORE_IMG_DEX_FILES) build-art-simulator-profile
	# Note: The target boot image needs to be in a trusted system directory to be used by the
	# zygote or if -Xonly-use-system-oat-files is passed to the runtime.
	rm -rf $(TARGET_BOOT_IMAGE_SYSTEM_DIR)
	mkdir -p $(TARGET_BOOT_IMAGE_SYSTEM_DIR)/javalib
	mkdir -p $(TARGET_ART_APEX_SYSTEM)/javalib
	# Copy the core boot jars to the expected directory for generate-boot-image.
	$(foreach i,$(call int_range_list, 1, $(words $(TARGET_CORE_IMG_JARS))), \
	  cp $(word $(i),$(TARGET_CORE_IMG_DEX_FILES)) \
	    $(TARGET_ART_APEX_SYSTEM)/javalib/$(word $(i),$(TARGET_CORE_IMG_JARS)).jar;)
	# Generate a target boot image using the host dex2oat. Note: a boot image using a profile is
	# required for certain run tests to pass.
	$(HOST_OUT_EXECUTABLES)/generate-boot-image64 \
	  --output-dir=$(TARGET_BOOT_IMAGE_SYSTEM_DIR)/javalib \
	  --compiler-filter=speed-profile \
	  --use-profile=true \
	  --profile-file=$(TARGET_BOOT_IMAGE_PROFILE) \
	  --dex2oat-bin=$(HOST_OUT_EXECUTABLES)/dex2oatd \
	  --android-root=$(TARGET_OUT) \
	  --android-root-for-location=true \
	  --core-only=true \
	  --instruction-set=$(TARGET_ARCH)

# For simulator, build a target profile and boot image on the host.
.PHONY: build-art-simulator
build-art-simulator: build-art-simulator-profile build-art-simulator-boot-image

PRIVATE_ART_APEX_DEPENDENCY_FILES := \
  bin/dalvikvm32 \
  bin/dalvikvm64 \
  bin/dalvikvm \
  bin/dex2oat32 \
  bin/dex2oat64 \
  bin/dexdump \

PRIVATE_ART_APEX_DEPENDENCY_LIBS := \
  lib/libadbconnection.so \
  lib/libandroidio.so \
  lib/libartbase.so \
  lib/libart-dexlayout.so \
  lib/libart-disassembler.so \
  lib/libartpalette.so \
  lib/libart.so \
  lib/libdexfile.so \
  lib/libdt_fd_forward.so \
  lib/libdt_socket.so \
  lib/libexpat.so \
  lib/libjavacore.so \
  lib/libjdwp.so \
  lib/liblzma.so \
  lib/libmeminfo.so \
  lib/libnativebridge.so \
  lib/libnativehelper.so \
  lib/libnativeloader.so \
  lib/libnpt.so \
  lib/libopenjdkjvm.so \
  lib/libopenjdkjvmti.so \
  lib/libopenjdk.so \
  lib/libpac.so \
  lib/libprocinfo.so \
  lib/libprofile.so \
  lib/libsigchain.so \
  lib/libunwindstack.so \
  lib64/libadbconnection.so \
  lib64/libandroidio.so \
  lib64/libartbase.so \
  lib64/libart-dexlayout.so \
  lib64/libart-disassembler.so \
  lib64/libartpalette.so \
  lib64/libart.so \
  lib64/libdexfile.so \
  lib64/libdt_fd_forward.so \
  lib64/libdt_socket.so \
  lib64/libexpat.so \
  lib64/libjavacore.so \
  lib64/libjdwp.so \
  lib64/liblzma.so \
  lib64/libmeminfo.so \
  lib64/libnativebridge.so \
  lib64/libnativehelper.so \
  lib64/libnativeloader.so \
  lib64/libnpt.so \
  lib64/libopenjdkjvm.so \
  lib64/libopenjdkjvmti.so \
  lib64/libopenjdk.so \
  lib64/libpac.so \
  lib64/libprocinfo.so \
  lib64/libprofile.so \
  lib64/libsigchain.so \
  lib64/libunwindstack.so \

PRIVATE_RUNTIME_APEX_DEPENDENCY_FILES := \
  bin/linker \
  bin/linker64 \
  lib/bionic/libc.so \
  lib/bionic/libdl.so \
  lib/bionic/libdl_android.so \
  lib/bionic/libm.so \
  lib64/bionic/libc.so \
  lib64/bionic/libdl.so \
  lib64/bionic/libdl_android.so \
  lib64/bionic/libm.so \

PRIVATE_CONSCRYPT_APEX_DEPENDENCY_LIBS := \
  lib/libcrypto.so \
  lib/libjavacrypto.so \
  lib/libssl.so \
  lib64/libcrypto.so \
  lib64/libjavacrypto.so \
  lib64/libssl.so \

PRIVATE_I18N_APEX_DEPENDENCY_LIBS := \
  lib/libicu.so \
  lib/libicui18n.so \
  lib/libicu_jni.so \
  lib/libicuuc.so \
  lib64/libicu.so \
  lib64/libicui18n.so \
  lib64/libicu_jni.so \
  lib64/libicuuc.so \

PRIVATE_STATSD_APEX_DEPENDENCY_LIBS := \
  lib/libstatspull.so \
  lib/libstatssocket.so \
  lib64/libstatspull.so \
  lib64/libstatssocket.so \

# Extracts files from an APEX into a location. The APEX can be either a .apex or
# .capex file in $(TARGET_OUT)/apex, or a directory in the same location. Files
# are extracted to $(TARGET_OUT) with the same relative paths as under the APEX
# root.
# $(1): APEX base name
# $(2): List of files to extract, with paths relative to the APEX root
#
# "cp -d" below doesn't work on Darwin, but this is only used for Golem builds
# and won't run on mac anyway.
define extract-from-apex
  apex_root=$(TARGET_OUT)/apex && \
  apex_file=$$apex_root/$(1).apex && \
  apex_dir=$$apex_root/$(1) && \
  if [ ! -f $$apex_file ]; then \
    apex_file=$$apex_root/$(1).capex; \
  fi && \
  if [ -f $$apex_file ]; then \
    rm -rf $$apex_dir && \
    mkdir -p $$apex_dir && \
    debugfs=$(HOST_OUT)/bin/debugfs_static && \
    fsckerofs=$(HOST_OUT)/bin/fsck.erofs && \
    $(HOST_OUT)/bin/deapexer --debugfs_path $$debugfs \
		--fsckerofs_path $$fsckerofs extract $$apex_file $$apex_dir; \
  fi && \
  for f in $(2); do \
    sf=$$apex_dir/$$f && \
    df=$(TARGET_OUT)/$$f && \
    if [ -f $$sf -o -h $$sf ]; then \
      mkdir -p $$(dirname $$df) && \
      cp -fd $$sf $$df; \
    fi || exit 1; \
  done
endef

# Copy or extract some required files from APEXes to the `system` (TARGET_OUT)
# directory. This is dangerous as these files could inadvertently stay in this
# directory and be included in a system image.
#
# This target is only used by Golem now.
#
# NB Android build does not use cp from:
#  $ANDROID_BUILD_TOP/prebuilts/build-tools/path/{linux-x86,darwin-x86}
# which has a non-standard set of command-line flags.
#
# TODO(b/129332183): Remove this when Golem has full support for the
# ART APEX.
#
# TODO(b/129332183): This approach is flawed: We mix DSOs from prebuilt APEXes
# with source built ones, and some of them don't have stable ABIs. libbase.so in
# particular is such a problematic dependency. When those dependencies
# eventually don't work anymore we don't have much choice but to update all
# prebuilts.
.PHONY: standalone-apex-files
standalone-apex-files: deapexer \
                       $(RELEASE_ART_APEX) \
                       $(RUNTIME_APEX) \
                       $(CONSCRYPT_APEX) \
                       $(I18N_APEX) \
                       $(STATSD_APEX) \
                       $(TZDATA_APEX) \
                       $(HOST_OUT)/bin/generate-boot-image64 \
                       $(HOST_OUT)/bin/dex2oat64 \
                       libartpalette_fake \
                       art_fake_heapprofd_client_api
	$(call extract-from-apex,$(RELEASE_ART_APEX),\
	  $(PRIVATE_ART_APEX_DEPENDENCY_LIBS) $(PRIVATE_ART_APEX_DEPENDENCY_FILES))
	# The Runtime APEX has the Bionic libs in ${LIB}/bionic subdirectories,
	# so we need to move them up a level after extraction.
	# Also, copy fake platform libraries.
	$(call extract-from-apex,$(RUNTIME_APEX),\
	  $(PRIVATE_RUNTIME_APEX_DEPENDENCY_FILES)) && \
	  libdir=$(TARGET_OUT)/lib$$(expr $(TARGET_ARCH) : '.*\(64\)' || :) && \
	  if [ -d $$libdir/bionic ]; then \
	    mv -f $$libdir/bionic/*.so $$libdir; \
	  fi && \
	  cp $$libdir/art_fake/*.so $$libdir
	$(call extract-from-apex,$(CONSCRYPT_APEX),\
	  $(PRIVATE_CONSCRYPT_APEX_DEPENDENCY_LIBS))
	$(call extract-from-apex,$(I18N_APEX),\
	  $(PRIVATE_I18N_APEX_DEPENDENCY_LIBS))
	$(call extract-from-apex,$(STATSD_APEX),\
	  $(PRIVATE_STATSD_APEX_DEPENDENCY_LIBS))
	$(call extract-from-apex,$(TZDATA_APEX),)
	rm -rf $(PRODUCT_OUT)/apex/art_boot_images && \
	  mkdir -p $(PRODUCT_OUT)/apex/art_boot_images/javalib && \
	  $(HOST_OUT)/bin/generate-boot-image64 \
	    --output-dir=$(PRODUCT_OUT)/apex/art_boot_images/javalib \
	    --compiler-filter=speed \
	    --use-profile=false \
	    --dex2oat-bin=$(HOST_OUT)/bin/dex2oat64 \
	    --android-root=$(TARGET_OUT) \
	    --instruction-set=$(TARGET_ARCH)

########################################################################
# Phony target for only building what go/lem requires for pushing ART on /data.

.PHONY: build-art-target-golem

ART_TARGET_PLATFORM_LIBS := \
  libcutils \
  libprocessgroup \
  libprocinfo \
  libselinux \
  libtombstoned_client \
  libz \

ART_TARGET_PLATFORM_DEPENDENCIES := \
  $(TARGET_OUT)/etc/public.libraries.txt \
  $(foreach lib,$(ART_TARGET_PLATFORM_LIBS), $(TARGET_OUT_SHARED_LIBRARIES)/$(lib).so)
ifdef TARGET_2ND_ARCH
ART_TARGET_PLATFORM_DEPENDENCIES += \
  $(foreach lib,$(ART_TARGET_PLATFORM_LIBS), $(2ND_TARGET_OUT_SHARED_LIBRARIES)/$(lib).so)
endif

# Despite `liblz4` being included in the ART apex, Golem benchmarks need another one in /system/ .
ART_TARGET_PLATFORM_DEPENDENCIES_GOLEM= \
  $(TARGET_OUT_SHARED_LIBRARIES)/liblz4.so

# Also include libartbenchmark, we always include it when running golem.
# libstdc++ is needed when building for ART_TARGET_LINUX.
ART_TARGET_SHARED_LIBRARY_BENCHMARK := $(TARGET_OUT_SHARED_LIBRARIES)/libartbenchmark.so

build-art-target-golem: $(RELEASE_ART_APEX) com.android.runtime $(CONSCRYPT_APEX) \
                        $(TARGET_OUT_EXECUTABLES)/art \
                        $(TARGET_OUT_EXECUTABLES)/dex2oat_wrapper \
                        $(ART_TARGET_PLATFORM_DEPENDENCIES) \
                        $(ART_TARGET_PLATFORM_DEPENDENCIES_GOLEM) \
                        $(ART_TARGET_SHARED_LIBRARY_BENCHMARK) \
                        $(TARGET_OUT_SHARED_LIBRARIES)/libgolemtiagent.so \
                        standalone-apex-files
	# remove debug libraries from public.libraries.txt because golem builds
	# won't have it.
	sed -i '/libartd.so/d' $(TARGET_OUT)/etc/public.libraries.txt
	sed -i '/libdexfiled.so/d' $(TARGET_OUT)/etc/public.libraries.txt
	sed -i '/libprofiled.so/d' $(TARGET_OUT)/etc/public.libraries.txt
	sed -i '/libartbased.so/d' $(TARGET_OUT)/etc/public.libraries.txt

########################################################################
# Phony target for building what go/lem requires on host.

.PHONY: build-art-host-golem
# Also include libartbenchmark, we always include it when running golem.
ART_HOST_SHARED_LIBRARY_BENCHMARK := $(ART_HOST_OUT_SHARED_LIBRARIES)/libartbenchmark.so
build-art-host-golem: build-art-host \
                      $(ART_HOST_SHARED_LIBRARY_BENCHMARK) \
		      $(ART_HOST_OUT_SHARED_LIBRARIES)/libgolemtiagent.so \
                      $(HOST_OUT_EXECUTABLES)/dex2oat_wrapper

########################################################################
# Rules for building all dependencies for tests.

.PHONY: build-art-host-gtests build-art-host-run-tests build-art-host-tests

build-art-host-gtests: build-art-host $(ART_TEST_HOST_GTEST_DEPENDENCIES)

build-art-host-run-tests: build-art-host \
                          $(TEST_ART_RUN_TEST_DEPENDENCIES) \
                          $(ART_TEST_HOST_RUN_TEST_DEPENDENCIES) \
                          art-run-test-host-data \
                          art-run-test-jvm-data

build-art-host-tests: build-art-host-gtests build-art-host-run-tests

.PHONY: build-art-target-gtests build-art-target-run-tests build-art-target-tests

build-art-target-gtests: build-art-target \
                         $(ART_TEST_TARGET_GTEST_DEPENDENCIES) \
                         $(ART_TARGET_PLATFORM_DEPENDENCIES)

build-art-target-run-tests: build-art-target \
                            $(TEST_ART_RUN_TEST_DEPENDENCIES) \
                            $(ART_TEST_TARGET_RUN_TEST_DEPENDENCIES) \
                            $(ART_TARGET_PLATFORM_DEPENDENCIES) \
                            art-run-test-target-data

build-art-target-tests: build-art-target-gtests build-art-target-run-tests

########################################################################

# Clear locally used variables.
TEST_ART_TARGET_SYNC_DEPS :=

# These files only exist if this flag is off. WITH_DEXPREOPT_ART_BOOT_IMG_ONLY is the
# minimal dexpreopt mode we use on eng builds for build speed.
ifneq ($(WITH_DEXPREOPT_ART_BOOT_IMG_ONLY),true)

# Helper target that depends on boot image creation.
#
# Can be used, for example, to dump initialization failures:
#   m art-boot-image ART_BOOT_IMAGE_EXTRA_ARGS=--dump-init-failures=fails.txt
.PHONY: art-boot-image
art-boot-image:  $(DEXPREOPT_IMAGE_boot_$(TARGET_ARCH))

.PHONY: art-job-images
art-job-images: \
  art-boot-image \
  $(2ND_DEFAULT_DEX_PREOPT_BUILT_IMAGE_FILENAME) \
  $(HOST_OUT_EXECUTABLES)/dex2oats \
  $(HOST_OUT_EXECUTABLES)/dex2oatds \
  $(HOST_OUT_EXECUTABLES)/profman

endif # TARGET_BUILD_VARIANT == eng

########################################################################

# Build a target that contains dex public SDK stubs for SDK version in the list.
# Zip files structure:
#   public-sdk-28-stub.zip
#     classes.dex
#   public-sdk-29-stub.zip
#     classes.dex
#   public-sdk-30-stub.zip
#     classes.dex
MIN_SDK_VERSION := 28
SDK_VERSIONS := $(call numbers_greater_or_equal_to,$(MIN_SDK_VERSION),$(TARGET_AVAIALBLE_SDK_VERSIONS))

# Create dex public SDK stubs.
define get_public_sdk_stub_dex
$(TARGET_OUT_COMMON_INTERMEDIATES)/PACKAGING/public_sdk_$(1)_stub_intermediates/classes.dex
endef

# The input is the SDK version.
define create_public_sdk_dex
public_sdk_$(1)_stub := $$(call get_public_sdk_stub_dex,$(1))
$$(public_sdk_$(1)_stub): PRIVATE_MIN_SDK_VERSION := $(1)
$$(public_sdk_$(1)_stub): $$(call resolve-prebuilt-sdk-jar-path,$(1)) $$(D8) $$(ZIP2ZIP)
	$$(transform-classes.jar-to-dex)

$$(call declare-1p-target,$$(public_sdk_$(1)_stub),art)
endef

$(foreach version,$(SDK_VERSIONS),$(eval $(call create_public_sdk_dex,$(version))))

# Create dex public SDK stubs zip.
define get_public_sdk_stub_zip
$(call intermediates-dir-for,PACKAGING,public_sdk_stub,HOST)/public-sdk-$(1)-stub.zip
endef

define create_public_sdk_zip
PUBLIC_SDK_$(1)_STUB_ZIP_PATH := $$(call get_public_sdk_stub_zip,$(1))
$$(PUBLIC_SDK_$(1)_STUB_ZIP_PATH): PRIVATE_SDK_STUBS_DEX_DIR := $$(dir $$(public_sdk_$(1)_stub))
$$(PUBLIC_SDK_$(1)_STUB_ZIP_PATH): $$(SOONG_ZIP) $$(public_sdk_$(1)_stub)
	rm -f $$@
	$$(SOONG_ZIP) -o $$@ -C $$(PRIVATE_SDK_STUBS_DEX_DIR) -D $$(PRIVATE_SDK_STUBS_DEX_DIR)

$$(call declare-1p-container,$$(PUBLIC_SDK_$(1)_STUB_ZIP_PATH),art)
$$(call declare-container-license-deps,$$(PUBLIC_SDK_$(1)_STUB_ZIP_PATH),$$(public_sdk_$(1)_stub),$$(PUBLIC_SDK_$(1)_STUB_PATH):)
endef

$(foreach version,$(SDK_VERSIONS),$(eval $(call create_public_sdk_zip,$(version))))

# Make the zip files available for prebuilts.
$(foreach version,$(SDK_VERSIONS),$(call dist-for-goals,sdk,$(call get_public_sdk_stub_zip,$(version))))

STUB_ZIP_FILES = $(foreach version,$(SDK_VERSIONS),$(call get_public_sdk_stub_zip,$(version)))

.PHONY: public_sdk_stubs
public_sdk_stubs: $(STUB_ZIP_FILES)

MIN_SDK_VERSION :=
SDK_VERSIONS :=
