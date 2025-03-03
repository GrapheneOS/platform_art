/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <android-base/logging.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <cstring>
#include <memory>

#include "base/zip_archive.h"
#include "os.h"
#include "unix_file/fd_file.h"

namespace art {

FileWithRange FileWithRange::Invalid() { return {.file = nullptr, .start = 0, .length = 0}; }

File* OS::OpenFileForReading(const char* name) {
  return OpenFileWithFlags(name, O_RDONLY);
}

File* OS::OpenFileReadWrite(const char* name) {
  return OpenFileWithFlags(name, O_RDWR);
}

static File* CreateEmptyFile(const char* name, int extra_flags) {
  // In case the file exists, unlink it so we get a new file. This is necessary as the previous
  // file may be in use and must not be changed.
  unlink(name);

  return OS::OpenFileWithFlags(name, O_CREAT | extra_flags);
}

File* OS::CreateEmptyFile(const char* name) {
  return art::CreateEmptyFile(name, O_RDWR | O_TRUNC);
}

File* OS::CreateEmptyFileWriteOnly(const char* name) {
#ifdef _WIN32
  int flags = O_WRONLY | O_TRUNC;
#else
  int flags = O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC;
#endif
  return art::CreateEmptyFile(name, flags);
}

File* OS::OpenFileWithFlags(const char* name, int flags, bool auto_flush) {
  CHECK(name != nullptr);
  bool read_only = ((flags & O_ACCMODE) == O_RDONLY);
  bool check_usage = !read_only && auto_flush;
  std::unique_ptr<File> file(
      new File(name, flags,  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, check_usage));
  if (!file->IsOpened()) {
    return nullptr;
  }
  return file.release();
}

bool OS::FileExists(const char* name, bool check_file_type) {
  struct stat st;
  if (stat(name, &st) == 0) {
    if (check_file_type) {
      return S_ISREG(st.st_mode);  // TODO: Deal with symlinks?
    } else {
      return true;
    }
  } else {
    return false;
  }
}

bool OS::DirectoryExists(const char* name) {
  struct stat st;
  if (stat(name, &st) == 0) {
    return S_ISDIR(st.st_mode);  // TODO: Deal with symlinks?
  } else {
    return false;
  }
}

int64_t OS::GetFileSizeBytes(const char* name) {
  struct stat st;
  if (stat(name, &st) == 0) {
    return st.st_size;  // TODO: Deal with symlinks? According to the documentation,
                        // the st_size for a symlink is "the length of the pathname
                        // it contains, without a terminating null byte."
  } else {
    return -1;
  }
}

FileWithRange OS::OpenFileDirectlyOrFromZip(const std::string& name_and_zip_entry,
                                            const char* zip_separator,
                                            size_t alignment,
                                            std::string* error_msg) {
  std::string filename = name_and_zip_entry;
  std::string zip_entry_name;
  size_t pos = filename.find(zip_separator);
  if (pos != std::string::npos) {
    zip_entry_name = filename.substr(pos + strlen(zip_separator));
    filename.resize(pos);
    if (filename.empty() || zip_entry_name.empty()) {
      *error_msg = ART_FORMAT("Malformed zip path '{}'", name_and_zip_entry);
      return FileWithRange::Invalid();
    }
  }

  std::unique_ptr<File> file(OS::OpenFileForReading(filename.c_str()));
  if (file == nullptr) {
    *error_msg = ART_FORMAT("Failed to open '{}' for reading: {}", filename, strerror(errno));
    return FileWithRange::Invalid();
  }

  off_t start = 0;
  int64_t total_file_length = file->GetLength();
  if (total_file_length < 0) {
    *error_msg = ART_FORMAT("Failed to get file length of '{}': {}", filename, strerror(errno));
    return FileWithRange::Invalid();
  }
  size_t length = total_file_length;

  if (!zip_entry_name.empty()) {
    std::unique_ptr<ZipArchive> zip_archive(
        ZipArchive::OpenFromOwnedFd(file->Fd(), filename.c_str(), error_msg));
    if (zip_archive == nullptr) {
      *error_msg = ART_FORMAT("Failed to open '{}' as zip", filename);
      return FileWithRange::Invalid();
    }
    std::unique_ptr<ZipEntry> zip_entry(zip_archive->Find(zip_entry_name.c_str(), error_msg));
    if (zip_entry == nullptr) {
      *error_msg = ART_FORMAT("Failed to find entry '{}' in zip '{}'", zip_entry_name, filename);
      return FileWithRange::Invalid();
    }
    if (!zip_entry->IsUncompressed() || !zip_entry->IsAlignedTo(alignment)) {
      *error_msg =
          ART_FORMAT("The entry '{}' in zip '{}' must be uncompressed and aligned to {} bytes",
                     zip_entry_name,
                     filename,
                     alignment);
      return FileWithRange::Invalid();
    }
    start = zip_entry->GetOffset();
    length = zip_entry->GetUncompressedLength();
    if (start + length > static_cast<size_t>(total_file_length)) {
      *error_msg = ART_FORMAT(
          "Invalid zip entry offset or length (offset: {}, length: {}, total_file_length: {})",
          start,
          length,
          total_file_length);
      return FileWithRange::Invalid();
    }
  }

  return {.file = std::move(file), .start = start, .length = length};
}

}  // namespace art
