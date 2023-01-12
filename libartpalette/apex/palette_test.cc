/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "palette/palette.h"

#include <jni.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "gtest/gtest.h"

namespace {

pid_t GetTid() {
#ifdef __BIONIC__
  return gettid();
#else  // __BIONIC__
  return syscall(__NR_gettid);
#endif  // __BIONIC__
}

}  // namespace

class PaletteClientTest : public testing::Test {};

TEST_F(PaletteClientTest, SchedPriority) {
  int32_t tid = GetTid();
  int32_t saved_priority;
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteSchedGetPriority(tid, &saved_priority));

  EXPECT_EQ(PALETTE_STATUS_INVALID_ARGUMENT, PaletteSchedSetPriority(tid, /*java_priority=*/ 0));
  EXPECT_EQ(PALETTE_STATUS_INVALID_ARGUMENT, PaletteSchedSetPriority(tid, /*java_priority=*/ -1));
  EXPECT_EQ(PALETTE_STATUS_INVALID_ARGUMENT, PaletteSchedSetPriority(tid, /*java_priority=*/ 11));

  EXPECT_EQ(PALETTE_STATUS_OK, PaletteSchedSetPriority(tid, /*java_priority=*/ 1));
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteSchedSetPriority(tid, saved_priority));
}

TEST_F(PaletteClientTest, Trace) {
  bool enabled = false;
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteTraceEnabled(&enabled));
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteTraceBegin("Hello world!"));
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteTraceEnd());
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteTraceIntegerValue("Beans", /*value=*/ 3));
}

TEST_F(PaletteClientTest, Ashmem) {
#ifndef ART_TARGET_ANDROID
  GTEST_SKIP() << "ashmem is only supported on Android";
#else
  int fd;
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteAshmemCreateRegion("ashmem-test", 4096, &fd));
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteAshmemSetProtRegion(fd, PROT_READ | PROT_EXEC));
  EXPECT_EQ(0, close(fd));
#endif
}

TEST_F(PaletteClientTest, JniInvocation) {
#ifndef ART_TARGET_ANDROID
  // On host we need to set up a boot classpath and pass it in here. Let's not
  // bother since this test is only for native API coverage on target.
  GTEST_SKIP() << "Will only spin up a VM on Android";
#else
  bool enabled;
  EXPECT_EQ(PALETTE_STATUS_OK, PaletteShouldReportJniInvocations(&enabled));

  JavaVMInitArgs vm_args;
  JavaVMOption options[] = {
      {.optionString = "-verbose:jni", .extraInfo = nullptr},
  };
  vm_args.version = JNI_VERSION_1_6;
  vm_args.nOptions = std::size(options);
  vm_args.options = options;
  vm_args.ignoreUnrecognized = JNI_TRUE;

  JavaVM* jvm = nullptr;
  JNIEnv* env = nullptr;
  EXPECT_EQ(JNI_OK, JNI_CreateJavaVM(&jvm, &env, &vm_args));
  ASSERT_NE(nullptr, env);

  PaletteNotifyBeginJniInvocation(env);
  PaletteNotifyEndJniInvocation(env);

  EXPECT_EQ(JNI_OK, jvm->DestroyJavaVM());
#endif
}
