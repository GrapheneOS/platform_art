/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "base/macros.h"
#include "jni.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

#include <errno.h>
#include <sstream>
#include <string.h>
#include <sys/resource.h>

namespace art {

extern "C" JNIEXPORT jint JNICALL Java_Main_getNativePriority(JNIEnv* env,
                                                              [[maybe_unused]] jclass clazz) {
  return Thread::ForEnv(env)->GetNativePriority();
}

extern "C" JNIEXPORT jboolean JNICALL Java_Main_supportsThreadPriorities(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz) {
  int my_niceness = getpriority(PRIO_PROCESS, 0 /* self */);
  int my_priority = Thread::NicenessToPriority(my_niceness);
  int new_priority = (my_priority == kMaxThreadPriority ? my_priority - 2 : my_priority + 1);
  {
    ScopedObjectAccess soa(env);
    Thread::Current()->SetNativePriority(new_priority);
  }
  int new_niceness = getpriority(PRIO_PROCESS, 0 /* self */);
  {
    ScopedObjectAccess soa(env);
    Thread::Current()->SetNativePriority(my_priority);
  }
  if (new_niceness == my_niceness) {
    // Had no effect.
    return JNI_FALSE;
  } else {
    // Assum3e it did the right thing.
    return JNI_TRUE;
  }
}

// Returns a description of Java to Posix priority mapping, and information about how we obtained
// it. Used only for test failure reporting, and hence a bit sloppy in terms of error checks.
extern "C" JNIEXPORT jstring JNICALL Java_Main_getPriorityInfo(JNIEnv* env,
                                                               [[maybe_unused]] jclass clazz) {
  std::ostringstream result;

  result << "Java priorities mapping: ";
  for (int p = kMinThreadPriority; p <= kMaxThreadPriority; ++p) {
    if (p != kMinThreadPriority) {
      result << ", ";
    }
    result << p << ": " << Thread::PriorityToNiceness(p);
  }
  result << ";\nSetpriority effects: ";
  int32_t me = static_cast<int32_t>(::art::GetTid());
  int my_niceness = getpriority(PRIO_PROCESS, 0 /* self */);
  for (int p = -20; p <= 19; p += 3) {
    if (p != -20) {
      result << ", ";
    }
    int ret = setpriority(PRIO_PROCESS, 0 /* self */, p);
    result << p << ": ";
    if (ret == 0) {
      result << getpriority(PRIO_PROCESS, 0 /* self */);
    } else {
      result << "failed:" << strerror(errno);
    }
  }
  // Test whether SetNativePriority has any impact. If not, that suggests that either setpriority
  // doesn't work in this range, or we've decided that setpriority cannot be relied upon for other
  // reasons. See `canSetPriority` in thread.cc. Intentionally slightly different from
  // supportsThreadPriorities test, so that we have a chance of detecting inconsistencies.
  {
    ScopedObjectAccess soa(env);
    Thread::Current()->SetNativePriority(6);
  }
  if (getpriority(PRIO_PROCESS, 0 /* self */) != Thread::PriorityToNiceness(6)) {
    result << ";\nPriority setting not working";
  }
  result << ";\nSDK version is " << android::base::GetIntProperty("ro.build.version.sdk", 0);
  // Restore priority to something plausible, if possible.
  setpriority(PRIO_PROCESS, 0 /* self */, my_niceness);
  return env->NewStringUTF(result.str().c_str());
}

}  // namespace art
