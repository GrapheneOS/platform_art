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

#ifndef ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_H_
#define ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_H_

#include "asm_support.h"

// FS0 - FS11, S0 - S11, RA and ArtMethod*, total 8*(12 + 12 + 1 + 1) = 208
#define FRAME_SIZE_SAVE_ALL_CALLEE_SAVES 208
// FA0 - FA7, A1 - A7, S0, S2 - S11, RA and ArtMethod* total 8*(8 + 7 + 11 + 1 + 1) = 224
// A0 is excluded as the ArtMethod*, and S1 is excluded as the ART thread register TR.
#define FRAME_SIZE_SAVE_REFS_AND_ARGS    224
// All 32 FPRs, 28 GPRs (no SP, Zero, TP, GP), ArtMethod*, padding, total 8*(32 + 28 + 1 + 1) = 496
#define FRAME_SIZE_SAVE_EVERYTHING       496

#endif  // ART_RUNTIME_ARCH_RISCV64_ASM_SUPPORT_RISCV64_H_
