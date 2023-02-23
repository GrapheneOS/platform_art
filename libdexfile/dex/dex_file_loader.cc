/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "dex_file_loader.h"

#include <sys/stat.h>

#include <memory>

#include "android-base/stringprintf.h"
#include "base/bit_utils.h"
#include "base/file_magic.h"
#include "base/mem_map.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/unix_file/fd_file.h"
#include "base/zip_archive.h"
#include "compact_dex_file.h"
#include "dex_file.h"
#include "dex_file_verifier.h"
#include "standard_dex_file.h"

namespace art {

namespace {

using android::base::StringPrintf;

class VectorContainer : public DexFileContainer {
 public:
  explicit VectorContainer(std::vector<uint8_t>&& vector) : vector_(std::move(vector)) { }
  ~VectorContainer() override { }

  bool IsReadOnly() const override { return true; }

  bool EnableWrite() override { return true; }

  bool DisableWrite() override { return false; }

  const uint8_t* Begin() const override { return vector_.data(); }

  const uint8_t* End() const override { return vector_.data() + vector_.size(); }

 private:
  std::vector<uint8_t> vector_;
  DISALLOW_COPY_AND_ASSIGN(VectorContainer);
};

class MemMapContainer : public DexFileContainer {
 public:
  explicit MemMapContainer(MemMap&& mem_map, bool is_file_map = false)
      : mem_map_(std::move(mem_map)), is_file_map_(is_file_map) {}

  int GetPermissions() const {
    if (!mem_map_.IsValid()) {
      return 0;
    } else {
      return mem_map_.GetProtect();
    }
  }

  bool IsReadOnly() const override { return GetPermissions() == PROT_READ; }

  bool EnableWrite() override {
    CHECK(IsReadOnly());
    if (!mem_map_.IsValid()) {
      return false;
    } else {
      return mem_map_.Protect(PROT_READ | PROT_WRITE);
    }
  }

  bool DisableWrite() override {
    CHECK(!IsReadOnly());
    if (!mem_map_.IsValid()) {
      return false;
    } else {
      return mem_map_.Protect(PROT_READ);
    }
  }

  const uint8_t* Begin() const override { return mem_map_.Begin(); }

  const uint8_t* End() const override { return mem_map_.End(); }

  bool IsFileMap() const override { return is_file_map_; }

 protected:
  MemMap mem_map_;
  bool is_file_map_;
  DISALLOW_COPY_AND_ASSIGN(MemMapContainer);
};

}  // namespace

bool DexFileLoader::IsMagicValid(uint32_t magic) {
  return IsMagicValid(reinterpret_cast<uint8_t*>(&magic));
}

bool DexFileLoader::IsMagicValid(const uint8_t* magic) {
  return StandardDexFile::IsMagicValid(magic) ||
      CompactDexFile::IsMagicValid(magic);
}

bool DexFileLoader::IsVersionAndMagicValid(const uint8_t* magic) {
  if (StandardDexFile::IsMagicValid(magic)) {
    return StandardDexFile::IsVersionValid(magic);
  }
  if (CompactDexFile::IsMagicValid(magic)) {
    return CompactDexFile::IsVersionValid(magic);
  }
  return false;
}

bool DexFileLoader::IsMultiDexLocation(const char* location) {
  return strrchr(location, kMultiDexSeparator) != nullptr;
}

std::string DexFileLoader::GetMultiDexClassesDexName(size_t index) {
  return (index == 0) ? "classes.dex" : StringPrintf("classes%zu.dex", index + 1);
}

std::string DexFileLoader::GetMultiDexLocation(size_t index, const char* dex_location) {
  return (index == 0)
      ? dex_location
      : StringPrintf("%s%cclasses%zu.dex", dex_location, kMultiDexSeparator, index + 1);
}

std::string DexFileLoader::GetDexCanonicalLocation(const char* dex_location) {
  CHECK_NE(dex_location, static_cast<const char*>(nullptr));
  std::string base_location = GetBaseLocation(dex_location);
  const char* suffix = dex_location + base_location.size();
  DCHECK(suffix[0] == 0 || suffix[0] == kMultiDexSeparator);
#ifdef _WIN32
  // Warning: No symbolic link processing here.
  PLOG(WARNING) << "realpath is unsupported on Windows.";
#else
  // Warning: Bionic implementation of realpath() allocates > 12KB on the stack.
  // Do not run this code on a small stack, e.g. in signal handler.
  UniqueCPtr<const char[]> path(realpath(base_location.c_str(), nullptr));
  if (path != nullptr && path.get() != base_location) {
    return std::string(path.get()) + suffix;
  }
#endif
  if (suffix[0] == 0) {
    return base_location;
  } else {
    return dex_location;
  }
}

// All of the implementations here should be independent of the runtime.

DexFileLoader::DexFileLoader(const uint8_t* base, size_t size, const std::string& location)
    : DexFileLoader(std::make_unique<MemoryDexFileContainer>(base, base + size), location) {}

DexFileLoader::DexFileLoader(std::vector<uint8_t>&& memory, const std::string& location)
    : DexFileLoader(std::make_unique<VectorContainer>(std::move(memory)), location) {}

DexFileLoader::DexFileLoader(MemMap&& mem_map, const std::string& location)
    : DexFileLoader(std::make_unique<MemMapContainer>(std::move(mem_map)), location) {}

std::unique_ptr<const DexFile> DexFileLoader::Open(uint32_t location_checksum,
                                                   const OatDexFile* oat_dex_file,
                                                   bool verify,
                                                   bool verify_checksum,
                                                   std::string* error_msg) {
  ScopedTrace trace(std::string("Open dex file ") + location_);

  uint32_t magic;
  if (!InitAndReadMagic(&magic, error_msg) || !MapRootContainer(error_msg)) {
    DCHECK(!error_msg->empty());
    return {};
  }
  std::unique_ptr<const DexFile> dex_file = OpenCommon(std::move(root_container_),
                                                       location_,
                                                       location_checksum,
                                                       oat_dex_file,
                                                       verify,
                                                       verify_checksum,
                                                       error_msg,
                                                       nullptr);
  return dex_file;
}

bool DexFileLoader::InitAndReadMagic(uint32_t* magic, std::string* error_msg) {
  if (root_container_ != nullptr) {
    if (root_container_->Size() < sizeof(uint32_t)) {
      *error_msg = StringPrintf("Unable to open '%s' : Size is too small", location_.c_str());
      return false;
    }
    *magic = *reinterpret_cast<const uint32_t*>(root_container_->Begin());
  } else {
    // Open the file if we have not been given the file-descriptor directly before.
    if (!file_.has_value()) {
      CHECK(!filename_.empty());
      file_.emplace(filename_, O_RDONLY, /* check_usage= */ false);
      if (file_->Fd() == -1) {
        *error_msg = StringPrintf("Unable to open '%s' : %s", filename_.c_str(), strerror(errno));
        return false;
      }
    }
    if (!ReadMagicAndReset(file_->Fd(), magic, error_msg)) {
      return false;
    }
  }
  return true;
}

bool DexFileLoader::MapRootContainer(std::string* error_msg) {
  if (root_container_ != nullptr) {
    return true;
  }

  CHECK(MemMap::IsInitialized());
  CHECK(file_.has_value());
  struct stat sbuf;
  memset(&sbuf, 0, sizeof(sbuf));
  if (fstat(file_->Fd(), &sbuf) == -1) {
    *error_msg = StringPrintf("DexFile: fstat '%s' failed: %s", filename_.c_str(), strerror(errno));
    return false;
  }
  if (S_ISDIR(sbuf.st_mode)) {
    *error_msg = StringPrintf("Attempt to mmap directory '%s'", filename_.c_str());
    return false;
  }
  MemMap map = MemMap::MapFile(sbuf.st_size,
                               PROT_READ,
                               MAP_PRIVATE,
                               file_->Fd(),
                               0,
                               /*low_4gb=*/false,
                               filename_.c_str(),
                               error_msg);
  if (!map.IsValid()) {
    DCHECK(!error_msg->empty());
    return false;
  }
  root_container_ = std::make_unique<MemMapContainer>(std::move(map));
  return true;
}

bool DexFileLoader::Open(bool verify,
                         bool verify_checksum,
                         bool allow_no_dex_files,
                         DexFileLoaderErrorCode* error_code,
                         std::string* error_msg,
                         std::vector<std::unique_ptr<const DexFile>>* dex_files) {
  ScopedTrace trace(std::string("Open dex file ") + location_);

  DCHECK(dex_files != nullptr) << "DexFile::Open: out-param is nullptr";

  uint32_t magic;
  if (!InitAndReadMagic(&magic, error_msg)) {
    return false;
  }

  if (IsZipMagic(magic)) {
    std::unique_ptr<ZipArchive> zip_archive(
        file_.has_value() ?
            ZipArchive::OpenFromOwnedFd(file_->Fd(), location_.c_str(), error_msg) :
            ZipArchive::OpenFromMemory(
                root_container_->Begin(), root_container_->Size(), location_.c_str(), error_msg));
    if (zip_archive.get() == nullptr) {
      DCHECK(!error_msg->empty());
      return false;
    }
    bool ok = OpenAllDexFilesFromZip(*zip_archive.get(),
                                     location_,
                                     verify,
                                     verify_checksum,
                                     allow_no_dex_files,
                                     error_code,
                                     error_msg,
                                     dex_files);
    return ok;
  }
  if (IsMagicValid(magic)) {
    if (!MapRootContainer(error_msg)) {
      return false;
    }
    const uint8_t* base = root_container_->Begin();
    size_t size = root_container_->Size();
    if (size < sizeof(DexFile::Header)) {
      *error_msg =
          StringPrintf("DexFile: failed to open dex file '%s' that is too short to have a header",
                       location_.c_str());
      return false;
    }
    const DexFile::Header* dex_header = reinterpret_cast<const DexFile::Header*>(base);
    std::unique_ptr<const DexFile> dex_file = OpenCommon(std::move(root_container_),
                                                         location_,
                                                         dex_header->checksum_,
                                                         /*oat_dex_file=*/nullptr,
                                                         verify,
                                                         verify_checksum,
                                                         error_msg,
                                                         nullptr);
    if (dex_file.get() != nullptr) {
      dex_files->push_back(std::move(dex_file));
      return true;
    } else {
      return false;
    }
  }
  *error_msg = StringPrintf("Expected valid zip or dex file");
  return false;
}

std::unique_ptr<DexFile> DexFileLoader::OpenCommon(std::unique_ptr<DexFileContainer> container,
                                                   const std::string& location,
                                                   uint32_t location_checksum,
                                                   const OatDexFile* oat_dex_file,
                                                   bool verify,
                                                   bool verify_checksum,
                                                   std::string* error_msg,
                                                   VerifyResult* verify_result) {
  CHECK(container != nullptr);
  const uint8_t* base = container->Begin();
  size_t size = container->Size();
  const uint8_t* data_base = container->DataBegin();
  size_t data_size = container->DataEnd() - container->DataBegin();
  if (verify_result != nullptr) {
    *verify_result = VerifyResult::kVerifyNotAttempted;
  }
  std::unique_ptr<DexFile> dex_file;
  if (size >= sizeof(StandardDexFile::Header) && StandardDexFile::IsMagicValid(base)) {
    if (data_size != 0) {
      CHECK_EQ(base, data_base) << "Unsupported for standard dex";
    }
    dex_file.reset(new StandardDexFile(
        base, size, location, location_checksum, oat_dex_file, std::move(container)));
  } else if (size >= sizeof(CompactDexFile::Header) && CompactDexFile::IsMagicValid(base)) {
    if (data_base == nullptr) {
      // TODO: Is there a clean way to support both an explicit data section and reading the one
      // from the header.
      CHECK_EQ(data_size, 0u);
      const CompactDexFile::Header* const header = CompactDexFile::Header::At(base);
      data_base = base + header->data_off_;
      data_size = header->data_size_;
    }
    dex_file.reset(new CompactDexFile(base,
                                      size,
                                      data_base,
                                      data_size,
                                      location,
                                      location_checksum,
                                      oat_dex_file,
                                      std::move(container)));
    // Disable verification for CompactDex input.
    verify = false;
  } else {
    *error_msg = "Invalid or truncated dex file";
  }
  if (dex_file == nullptr) {
    *error_msg =
        StringPrintf("Failed to open dex file '%s': %s", location.c_str(), error_msg->c_str());
    return nullptr;
  }
  if (!dex_file->Init(error_msg)) {
    dex_file.reset();
    return nullptr;
  }
  if (verify) {
    ScopedTrace trace(std::string("Verify dex file ") + location);
    if (!dex::Verify(dex_file.get(),
                     dex_file->Begin(),
                     dex_file->Size(),
                     location.c_str(),
                     verify_checksum,
                     error_msg)) {
      if (verify_result != nullptr) {
        *verify_result = VerifyResult::kVerifyFailed;
      }
      return nullptr;
    }
  }
  if (verify_result != nullptr) {
    *verify_result = VerifyResult::kVerifySucceeded;
  }
  return dex_file;
}

std::unique_ptr<const DexFile> DexFileLoader::OpenOneDexFileFromZip(
    const ZipArchive& zip_archive,
    const char* entry_name,
    const std::string& location,
    bool verify,
    bool verify_checksum,
    DexFileLoaderErrorCode* error_code,
    std::string* error_msg) const {
  CHECK(!location.empty());
  std::unique_ptr<ZipEntry> zip_entry(zip_archive.Find(entry_name, error_msg));
  if (zip_entry == nullptr) {
    *error_code = DexFileLoaderErrorCode::kEntryNotFound;
    return nullptr;
  }
  if (zip_entry->GetUncompressedLength() == 0) {
    *error_msg = StringPrintf("Dex file '%s' has zero length", location.c_str());
    *error_code = DexFileLoaderErrorCode::kDexFileError;
    return nullptr;
  }

  CHECK(MemMap::IsInitialized());
  MemMap map;
  bool is_file_map = false;
  if (file_.has_value() && zip_entry->IsUncompressed()) {
    if (!zip_entry->IsAlignedTo(alignof(DexFile::Header))) {
      // Do not mmap unaligned ZIP entries because
      // doing so would fail dex verification which requires 4 byte alignment.
      LOG(WARNING) << "Can't mmap dex file " << location << "!" << entry_name << " directly; "
                   << "please zipalign to " << alignof(DexFile::Header) << " bytes. "
                   << "Falling back to extracting file.";
    } else {
      // Map uncompressed files within zip as file-backed to avoid a dirty copy.
      map = zip_entry->MapDirectlyFromFile(location.c_str(), /*out*/ error_msg);
      if (!map.IsValid()) {
        LOG(WARNING) << "Can't mmap dex file " << location << "!" << entry_name << " directly; "
                     << "is your ZIP file corrupted? Falling back to extraction.";
        // Try again with Extraction which still has a chance of recovery.
      }
      is_file_map = true;
    }
  }
  if (!map.IsValid()) {
    ScopedTrace trace(std::string("Extract dex file ") + location);

    // Default path for compressed ZIP entries,
    // and fallback for stored ZIP entries.
    map = zip_entry->ExtractToMemMap(location.c_str(), entry_name, error_msg);
  }
  if (!map.IsValid()) {
    *error_msg = StringPrintf("Failed to extract '%s' from '%s': %s", entry_name, location.c_str(),
                              error_msg->c_str());
    *error_code = DexFileLoaderErrorCode::kExtractToMemoryError;
    return nullptr;
  }
  auto container = std::make_unique<MemMapContainer>(std::move(map), is_file_map);
  container->SetIsZip();

  VerifyResult verify_result;
  std::unique_ptr<const DexFile> dex_file = OpenCommon(std::move(container),
                                                       location,
                                                       zip_entry->GetCrc32(),
                                                       /*oat_dex_file=*/nullptr,
                                                       verify,
                                                       verify_checksum,
                                                       error_msg,
                                                       &verify_result);
  if (verify_result != VerifyResult::kVerifySucceeded) {
    if (verify_result == VerifyResult::kVerifyNotAttempted) {
      *error_code = DexFileLoaderErrorCode::kDexFileError;
    } else {
      *error_code = DexFileLoaderErrorCode::kVerifyError;
    }
    return nullptr;
  }
  if (!dex_file->DisableWrite()) {
    *error_msg = StringPrintf("Failed to make dex file '%s' read only", location.c_str());
    *error_code = DexFileLoaderErrorCode::kMakeReadOnlyError;
    return nullptr;
  }
  CHECK(dex_file->IsReadOnly()) << location;
  *error_code = DexFileLoaderErrorCode::kNoError;
  return dex_file;
}

// Technically we do not have a limitation with respect to the number of dex files that can be in a
// multidex APK. However, it's bad practice, as each dex file requires its own tables for symbols
// (types, classes, methods, ...) and dex caches. So warn the user that we open a zip with what
// seems an excessive number.
static constexpr size_t kWarnOnManyDexFilesThreshold = 100;

bool DexFileLoader::OpenAllDexFilesFromZip(
    const ZipArchive& zip_archive,
    const std::string& location,
    bool verify,
    bool verify_checksum,
    bool allow_no_dex_files,
    DexFileLoaderErrorCode* error_code,
    std::string* error_msg,
    std::vector<std::unique_ptr<const DexFile>>* dex_files) const {
  DCHECK(dex_files != nullptr) << "DexFile::OpenFromZip: out-param is nullptr";
  std::unique_ptr<const DexFile> dex_file(OpenOneDexFileFromZip(zip_archive,
                                                                kClassesDex,
                                                                location,
                                                                verify,
                                                                verify_checksum,
                                                                error_code,
                                                                error_msg));
  if (*error_code != DexFileLoaderErrorCode::kNoError) {
    if (allow_no_dex_files && *error_code == DexFileLoaderErrorCode::kEntryNotFound) {
      return true;
    }
    return false;
  } else {
    // Had at least classes.dex.
    dex_files->push_back(std::move(dex_file));

    // Now try some more.

    // We could try to avoid std::string allocations by working on a char array directly. As we
    // do not expect a lot of iterations, this seems too involved and brittle.

    for (size_t i = 1; ; ++i) {
      std::string name = GetMultiDexClassesDexName(i);
      std::string fake_location = GetMultiDexLocation(i, location.c_str());
      std::unique_ptr<const DexFile> next_dex_file(OpenOneDexFileFromZip(zip_archive,
                                                                         name.c_str(),
                                                                         fake_location,
                                                                         verify,
                                                                         verify_checksum,
                                                                         error_code,
                                                                         error_msg));
      if (next_dex_file.get() == nullptr) {
        if (*error_code != DexFileLoaderErrorCode::kEntryNotFound) {
          LOG(WARNING) << "Zip open failed: " << *error_msg;
        }
        break;
      } else {
        dex_files->push_back(std::move(next_dex_file));
      }

      if (i == kWarnOnManyDexFilesThreshold) {
        LOG(WARNING) << location << " has in excess of " << kWarnOnManyDexFilesThreshold
                     << " dex files. Please consider coalescing and shrinking the number to "
                        " avoid runtime overhead.";
      }

      if (i == std::numeric_limits<size_t>::max()) {
        LOG(ERROR) << "Overflow in number of dex files!";
        break;
      }
    }

    return true;
  }
}
}  // namespace art
