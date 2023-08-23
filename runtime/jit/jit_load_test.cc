/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "common_runtime_test.h"
#include "compiler_callbacks.h"

namespace art {

class JitLoadTest : public CommonRuntimeTest {
 protected:
  void SetUpRuntimeOptions(RuntimeOptions *options) override {
    callbacks_.reset();
    CommonRuntimeTest::SetUpRuntimeOptions(options);
    options->push_back(std::make_pair("-Xusejit:true", nullptr));
  }
};


TEST_F(JitLoadTest, JitLoad) {
  Thread::Current()->TransitionFromSuspendedToRunnable();
  runtime_->Start();
  ASSERT_NE(runtime_->GetJit(), nullptr);
}

}  // namespace art
