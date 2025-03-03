/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_VERIFIER_VERIFIER_COMPILER_BINDING_H_
#define ART_RUNTIME_VERIFIER_VERIFIER_COMPILER_BINDING_H_

#include <inttypes.h>

#include "base/macros.h"
#include "verifier_enums.h"

namespace art HIDDEN {
namespace verifier {

ALWAYS_INLINE
static inline bool CanCompilerHandleVerificationFailure(uint32_t encountered_failure_types) {
  // These are and should remain the only two reasons a verified method cannot
  // be compiled. The vdex file will mark classes where those methods are defined
  // as verify-at-runtime and we should ideally not break that format in adding
  // a new kind of failure.
  constexpr uint32_t errors_needing_reverification =
      verifier::VerifyError::VERIFY_ERROR_RUNTIME_THROW |
      verifier::VerifyError::VERIFY_ERROR_LOCKING;
  return (encountered_failure_types & errors_needing_reverification) == 0;
}

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_VERIFIER_COMPILER_BINDING_H_
