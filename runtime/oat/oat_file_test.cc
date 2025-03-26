/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "oat_file.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "android-base/scopeguard.h"
#include "base/file_utils.h"
#include "base/os.h"
#include "common_runtime_test.h"
#include "dexopt_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art HIDDEN {

using ::testing::HasSubstr;

using std::string_view_literals::operator""sv;

// Returns the offset of the first dex file in the vdex file.
static void GetFirstDexFileOffset(const std::string& vdex_filename, /*out*/ size_t* offset) {
  std::string error_msg;
  std::unique_ptr<VdexFile> vdex_file =
      VdexFile::Open(vdex_filename, /*low_4gb=*/false, &error_msg);
  ASSERT_NE(vdex_file, nullptr) << error_msg;
  const uint8_t* ptr = vdex_file->GetNextDexFileData(/*cursor=*/nullptr, /*dex_file_index=*/0);
  ASSERT_NE(ptr, nullptr) << "No dex code in vdex";
  ASSERT_GE(ptr, vdex_file->Begin());
  ASSERT_LT(ptr, vdex_file->End());
  *offset = ptr - vdex_file->Begin();
}

class OatFileTest : public DexoptTest {};

TEST_F(OatFileTest, LoadOat) {
  std::string dex_location = GetScratchDir() + "/LoadOat.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/false,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  ASSERT_TRUE(odex_file.get() != nullptr);

  // Check that the vdex file was loaded in the reserved space of odex file.
  EXPECT_EQ(odex_file->GetVdexFile()->Begin(), odex_file->VdexBegin());
}

TEST_F(OatFileTest, ChangingMultiDexUncompressed) {
  std::string dex_location = GetScratchDir() + "/MultiDexUncompressedAligned.jar";

  Copy(GetTestDexFileName("MultiDexUncompressedAligned"), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kVerify);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;

  // Ensure we can load that file. Just a precondition.
  {
    std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                     oat_location,
                                                     oat_location,
                                                     /*executable=*/false,
                                                     /*low_4gb=*/false,
                                                     dex_location,
                                                     &error_msg));
    ASSERT_TRUE(odex_file != nullptr);
    ASSERT_EQ(2u, odex_file->GetOatDexFiles().size());
  }

  // Now replace the source.
  Copy(GetTestDexFileName("MainUncompressedAligned"), dex_location);

  // And try to load again.
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/false,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  EXPECT_TRUE(odex_file == nullptr);
  EXPECT_NE(std::string::npos, error_msg.find("expected 2 uncompressed dex files, but found 1"))
      << error_msg;
}

TEST_F(OatFileTest, DlOpenLoad) {
  std::string dex_location = GetScratchDir() + "/LoadOat.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;

  // Clear previous errors if any.
  dlerror();
  error_msg.clear();
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/true,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  ASSERT_NE(odex_file.get(), nullptr) << error_msg;

#ifdef __GLIBC__
  if (!error_msg.empty()) {
    // If a valid oat file was returned but there was an error message, then dlopen failed
    // but the backup ART ELF loader successfully loaded the oat file.
    // The only expected reason for this is a bug in glibc that prevents loading dynamic
    // shared objects with a read-only dynamic section:
    // https://sourceware.org/bugzilla/show_bug.cgi?id=28340.
    ASSERT_TRUE(error_msg == "DlOpen does not support read-only .dynamic section.") << error_msg;
    GTEST_SKIP() << error_msg;
  }
#else
  // If a valid oat file was returned with no error message, then dlopen was successful.
  ASSERT_TRUE(error_msg.empty()) << error_msg;
#endif

  const char *dlerror_msg = dlerror();
  ASSERT_EQ(dlerror_msg, nullptr) << dlerror_msg;

  // Ensure that the oat file is loaded with dlopen by requesting information about it
  // using dladdr.
  Dl_info info;
  ASSERT_NE(dladdr(odex_file->Begin(), &info), 0);
  EXPECT_STREQ(info.dli_fname, oat_location.c_str())
      << "dli_fname: " << info.dli_fname
      << ", location: " << oat_location;
  EXPECT_STREQ(info.dli_sname, "oatdata") << info.dli_sname;
}

TEST_F(OatFileTest, RejectsCdex) {
  std::string dex_location = GetScratchDir() + "/LoadOat.jar";
  std::string odex_location = GetScratchDir() + "/LoadOat.odex";
  std::string vdex_location = GetVdexFilename(odex_location);

  Copy(GetDexSrc1(), dex_location);
  ASSERT_NO_FATAL_FAILURE(GenerateOdexForTest(dex_location, odex_location, CompilerFilter::kSpeed));

  // Patch the generated vdex file to simulate that it contains cdex.
  {
    size_t dex_offset;
    ASSERT_NO_FATAL_FAILURE(GetFirstDexFileOffset(vdex_location, &dex_offset));
    std::unique_ptr<File> vdex_file(OS::OpenFileReadWrite(vdex_location.c_str()));
    ASSERT_NE(vdex_file, nullptr) << strerror(errno);
    auto cleanup = android::base::make_scope_guard([&] { (void)vdex_file->FlushClose(); });
    constexpr std::string_view kCdexMagic = "cdex001\0"sv;
    ASSERT_LE(dex_offset + kCdexMagic.size(), vdex_file->GetLength()) << "Dex file too short";
    bool success = vdex_file->PwriteFully(kCdexMagic.data(), kCdexMagic.size(), dex_offset);
    ASSERT_TRUE(success) << strerror(errno);
    cleanup.Disable();
    ASSERT_EQ(vdex_file->FlushClose(), 0);
  }

  // Create `OatFile` from the vdex file together with the oat file. This should fail.
  {
    std::string error_msg;
    std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                     odex_location,
                                                     odex_location,
                                                     /*executable=*/false,
                                                     /*low_4gb=*/false,
                                                     dex_location,
                                                     &error_msg));
    EXPECT_EQ(odex_file, nullptr) << "Cdex accepted unexpectedly";
    EXPECT_THAT(error_msg, HasSubstr("invalid dex file magic"));
  }

  // Create `OatFile` from the vdex file alone. This should fail too.
  {
    std::string error_msg;
    std::unique_ptr<VdexFile> vdex_file =
        VdexFile::Open(vdex_location, /*low_4gb=*/false, &error_msg);
    ASSERT_NE(vdex_file, nullptr);
    std::unique_ptr<OatFile> odex_file(OatFile::OpenFromVdex(/*zip_fd=*/-1,
                                                             std::move(vdex_file),
                                                             vdex_location,
                                                             /*context=*/nullptr,
                                                             &error_msg));
    EXPECT_EQ(odex_file, nullptr) << "Cdex accepted unexpectedly";
    EXPECT_THAT(error_msg, HasSubstr("found dex file with invalid dex file version"));
  }
}

}  // namespace art
