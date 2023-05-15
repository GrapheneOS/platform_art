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

#include "base/mem_map.h"
#include "dex/dex_file_loader.h"

extern "C" int LLVMFuzzerInitialize(int* argc ATTRIBUTE_UNUSED, char*** argv ATTRIBUTE_UNUSED) {
  // Initialize environment.
  // TODO(solanes): `art::MemMap::Init` is not needed for the current DexFileLoader code path.
  // Consider removing it once the fuzzer stabilizes and check that it is actually not needed.
  art::MemMap::Init();
  // Set logging to error and above to avoid warnings about unexpected checksums.
  android::base::SetMinimumLogSeverity(android::base::ERROR);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Skip compact DEX.
  // TODO(dsrbecky): Remove after removing compact DEX.
  const char* dex_string = "cdex";
  if (size >= strlen(dex_string) &&
      strncmp(dex_string, (const char*)data, strlen(dex_string)) == 0) {
    // A -1 indicates we don't want this DEX added to the corpus.
    return -1;
  }

  // Open and verify the DEX file. Do not verify the checksum as we only care about the DEX file
  // contents, and know that the checksum would probably be erroneous.
  std::string error_msg;
  art::DexFileLoader loader(data, size, /*location=*/"");
  std::unique_ptr<const art::DexFile> dex_file = loader.Open(
      /*location_checksum=*/0, /*verify=*/true, /*verify_checksum=*/false, &error_msg);
  return 0;
}
