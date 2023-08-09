/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "intrinsics_riscv64.h"

#include "code_generator_riscv64.h"

namespace art HIDDEN {
namespace riscv64 {

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(RISCV64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_RISCV64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(RISCV64)

}  // namespace riscv64
}  // namespace art
