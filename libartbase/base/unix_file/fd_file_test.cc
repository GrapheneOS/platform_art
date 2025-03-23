/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include "base/common_art_test.h"  // For ScratchFile
#include "base/file_utils.h"
#include "base/mem_map.h"
#include "gtest/gtest.h"
#include "fd_file.h"
#include "random_access_file_test.h"

#include "sys/mman.h"

namespace unix_file {

class FdFileTest : public RandomAccessFileTest {
 protected:
  RandomAccessFile* MakeTestFile() override {
    FILE* tmp = tmpfile();
    int fd = art::DupCloexec(fileno(tmp));
    fclose(tmp);
    return new FdFile(fd, false);
  }
  // Variables and functions related to sparse copy and move (rename) tests.
  void TestDataMatches(const FdFile* src,
                       const FdFile* dest,
                       size_t input_offset,
                       size_t output_offset,
                       size_t copy_length);
#ifdef __linux__
  static constexpr int kNumChunks = 8;
  static constexpr size_t kChunkSize = 64 * art::KB;
  static constexpr size_t kStatBlockSize = 512;
  void CreateSparseSourceFile(size_t empty_prefix,
                              size_t empty_suffix,
                              /*out*/ std::unique_ptr<art::ScratchFile>& out_file);
  size_t GetFilesystemBlockSize();
#endif
};

TEST_F(FdFileTest, Read) {
  TestRead();
}

TEST_F(FdFileTest, SetLength) {
  TestSetLength();
}

TEST_F(FdFileTest, Write) {
  TestWrite();
}

TEST_F(FdFileTest, UnopenedFile) {
  FdFile file;
  EXPECT_EQ(FdFile::kInvalidFd, file.Fd());
  EXPECT_FALSE(file.IsOpened());
  EXPECT_TRUE(file.GetPath().empty());
}

TEST_F(FdFileTest, IsOpenFd) {
  art::ScratchFile scratch_file;
  FdFile* file = scratch_file.GetFile();
  ASSERT_TRUE(file->IsOpened());
  EXPECT_GE(file->Fd(), 0);
  EXPECT_NE(file->Fd(), FdFile::kInvalidFd);
  EXPECT_TRUE(FdFile::IsOpenFd(file->Fd()));
  int old_fd = file->Fd();
  ASSERT_TRUE(file != nullptr);
  ASSERT_EQ(file->FlushClose(), 0);
  EXPECT_FALSE(file->IsOpened());
  EXPECT_FALSE(FdFile::IsOpenFd(old_fd));
}

TEST_F(FdFileTest, OpenClose) {
  std::string good_path(GetTmpPath("some-file.txt"));
  FdFile file(good_path, O_CREAT | O_WRONLY, true);
  ASSERT_TRUE(file.IsOpened());
  EXPECT_GE(file.Fd(), 0);
  EXPECT_TRUE(file.IsOpened());
  EXPECT_FALSE(file.ReadOnlyMode());
  EXPECT_EQ(0, file.Flush());
  EXPECT_EQ(0, file.Close());
  EXPECT_EQ(FdFile::kInvalidFd, file.Fd());
  EXPECT_FALSE(file.IsOpened());
  FdFile file2(good_path, O_RDONLY, true);
  EXPECT_TRUE(file2.IsOpened());
  EXPECT_TRUE(file2.ReadOnlyMode());
  EXPECT_GE(file2.Fd(), 0);

  ASSERT_EQ(file2.Close(), 0);
  ASSERT_EQ(unlink(good_path.c_str()), 0);
}

TEST_F(FdFileTest, ReadFullyEmptyFile) {
  // New scratch file, zero-length.
  art::ScratchFile tmp;
  FdFile file(tmp.GetFilename(), O_RDONLY, false);
  ASSERT_TRUE(file.IsOpened());
  EXPECT_TRUE(file.ReadOnlyMode());
  EXPECT_GE(file.Fd(), 0);
  uint8_t buffer[16];
  EXPECT_FALSE(file.ReadFully(&buffer, 4));
}

template <size_t Size>
static void NullTerminateCharArray(char (&array)[Size]) {
  array[Size - 1] = '\0';
}

TEST_F(FdFileTest, ReadFullyWithOffset) {
  // New scratch file, zero-length.
  art::ScratchFile tmp;
  FdFile file(tmp.GetFilename(), O_RDWR, false);
  ASSERT_TRUE(file.IsOpened());
  EXPECT_GE(file.Fd(), 0);
  EXPECT_FALSE(file.ReadOnlyMode());

  char ignore_prefix[20] = {'a', };
  NullTerminateCharArray(ignore_prefix);
  char read_suffix[10] = {'b', };
  NullTerminateCharArray(read_suffix);

  off_t offset = 0;
  // Write scratch data to file that we can read back into.
  EXPECT_TRUE(file.Write(ignore_prefix, sizeof(ignore_prefix), offset));
  offset += sizeof(ignore_prefix);
  EXPECT_TRUE(file.Write(read_suffix, sizeof(read_suffix), offset));

  ASSERT_EQ(file.Flush(), 0);

  // Reading at an offset should only produce 'bbbb...', since we ignore the 'aaa...' prefix.
  char buffer[sizeof(read_suffix)];
  EXPECT_TRUE(file.PreadFully(buffer, sizeof(read_suffix), offset));
  EXPECT_STREQ(&read_suffix[0], &buffer[0]);

  ASSERT_EQ(file.Close(), 0);
}

TEST_F(FdFileTest, ReadWriteFullyWithOffset) {
  // New scratch file, zero-length.
  art::ScratchFile tmp;
  FdFile file(tmp.GetFilename(), O_RDWR, false);
  ASSERT_GE(file.Fd(), 0);
  EXPECT_TRUE(file.IsOpened());
  EXPECT_FALSE(file.ReadOnlyMode());

  const char* test_string = "This is a test string";
  size_t length = strlen(test_string) + 1;
  const size_t offset = 12;
  std::unique_ptr<char[]> offset_read_string(new char[length]);
  std::unique_ptr<char[]> read_string(new char[length]);

  // Write scratch data to file that we can read back into.
  EXPECT_TRUE(file.PwriteFully(test_string, length, offset));
  ASSERT_EQ(file.Flush(), 0);

  // Test reading both the offsets.
  EXPECT_TRUE(file.PreadFully(&offset_read_string[0], length, offset));
  EXPECT_STREQ(test_string, &offset_read_string[0]);

  EXPECT_TRUE(file.PreadFully(&read_string[0], length, 0u));
  EXPECT_NE(memcmp(&read_string[0], test_string, length), 0);

  ASSERT_EQ(file.Close(), 0);
}

// Create a sparse file and return a pointer to it via the 'out_file' argument, necessary because
// gtest assertions require the function to return void.
void FdFileTest::CreateSparseSourceFile(size_t empty_prefix,
                                        size_t empty_suffix,
                                        /*out*/ std::unique_ptr<art::ScratchFile>& out_file) {
/*
   * Layout of the source file:
   *   [ optional <empty_prefix> empty region ]
   *   [ <kChunkSize> data chunk              ]  -\
   *   [ <kChunkSize> empty chunk             ]   |
   *   [ <kChunkSize> data chunk              ]   |
   *   [ <kChunkSize> empty chunk             ]    > (2 * kNumChunks - 1) kChunkSize chunks
   *   [ <kChunkSize> data chunk              ]   |
   *   [   ...                                ]   |
   *   [ <kChunkSize> data chunk              ]  -/
   *   [ optional <empty_suffix> empty region ]
   */
  out_file = std::make_unique<art::ScratchFile>();
  FdFile* src = out_file->GetFile();
  ASSERT_TRUE(src->IsOpened());

  ASSERT_EQ(lseek(src->Fd(), empty_prefix, SEEK_CUR), empty_prefix);

  std::vector<int8_t> data_buffer(/*n=*/kChunkSize, /*val=*/1);

  ASSERT_TRUE(src->WriteFully(data_buffer.data(), kChunkSize));
  for (size_t i = 0; i < kNumChunks - 1; i++) {
    // Leave a chunk size of unwritten space between each data chunk.
    ASSERT_GT(lseek(src->Fd(), kChunkSize, SEEK_CUR), 0);
    ASSERT_TRUE(src->WriteFully(data_buffer.data(), kChunkSize));
  }
  ASSERT_EQ(src->SetLength(src->GetLength() + empty_suffix), 0);
  ASSERT_EQ(src->Flush(), 0);

  size_t expected_length = (2 * kNumChunks - 1) * kChunkSize + empty_prefix + empty_suffix;
  ASSERT_EQ(src->GetLength(), expected_length);
}

TEST_F(FdFileTest, Rename) {
  // To test that rename preserves sparsity (on systems that support file sparsity), create a sparse
  // source file.
  std::unique_ptr<art::ScratchFile> src;
  ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(/*empty_prefix=*/0, /*empty_suffix=*/0, src));

  size_t src_offset = lseek(src->GetFd(), /*offset=*/0, SEEK_CUR);
  size_t source_length = src->GetFile()->GetLength();
  struct stat src_stat;
  ASSERT_EQ(fstat(src->GetFd(), &src_stat), 0);

  // Move the file via a rename.
  art::ScratchFile dest;
  const std::string& new_filename = dest.GetFilename();
  const std::string& old_filename = src->GetFilename();
  ASSERT_TRUE(src->GetFile()->Rename(new_filename));

  // Confirm the FdFile path has correctly updated.
  EXPECT_EQ(src->GetFile()->GetPath(), new_filename);
  // Check the offset of the moved file has not been modified.
  EXPECT_EQ(lseek(src->GetFd(), /*offset=*/0, SEEK_CUR), src_offset);

  // Test that the file no longer exists in the old location, and there is a file at the new
  // location with the expected length.
  EXPECT_FALSE(art::OS::FileExists(old_filename.c_str()));
  FdFile dest_file(new_filename, O_RDONLY, /*check_usage=*/false);
  ASSERT_TRUE(dest_file.IsOpened());
  EXPECT_EQ(dest_file.GetLength(), source_length);

  // Confirm the file at the new location has the same number of allocated data blocks as the source
  // file. If the source file was a sparse file, this confirms that the sparsity was preserved
  // by the move.
  struct stat dest_stat;
  ASSERT_EQ(fstat(dest_file.Fd(), &dest_stat), 0);
  EXPECT_EQ(dest_stat.st_blocks, src_stat.st_blocks);

  // And it is exactly the same file in the new location, with the same contents.
  EXPECT_EQ(dest_stat.st_dev, src_stat.st_dev);
  EXPECT_EQ(dest_stat.st_ino, src_stat.st_ino);

  ASSERT_NO_FATAL_FAILURE(TestDataMatches(src->GetFile(),
                                          &dest_file,
                                          /*input_offset=*/0u,
                                          /*output_offset=*/0u,
                                          source_length));
  src->Close();
}

TEST_F(FdFileTest, Copy) {
  art::ScratchFile src_tmp;
  FdFile src(src_tmp.GetFilename(), O_RDWR, false);
  ASSERT_GE(src.Fd(), 0);
  ASSERT_TRUE(src.IsOpened());

  char src_data[] = "Some test data.";
  ASSERT_TRUE(src.WriteFully(src_data, sizeof(src_data)));  // Including the zero terminator.
  ASSERT_EQ(0, src.Flush());
  ASSERT_EQ(static_cast<int64_t>(sizeof(src_data)), src.GetLength());

  art::ScratchFile dest_tmp;
  FdFile dest(dest_tmp.GetFilename(), O_RDWR, false);
  ASSERT_GE(dest.Fd(), 0);
  ASSERT_TRUE(dest.IsOpened());

  ASSERT_TRUE(dest.Copy(&src, 0, sizeof(src_data)));
  ASSERT_EQ(0, dest.Flush());
  ASSERT_EQ(static_cast<int64_t>(sizeof(src_data)), dest.GetLength());

  char check_data[sizeof(src_data)];
  ASSERT_TRUE(dest.PreadFully(check_data, sizeof(src_data), 0u));
  CHECK_EQ(0, memcmp(check_data, src_data, sizeof(src_data)));

  ASSERT_EQ(0, dest.Close());
  ASSERT_EQ(0, src.Close());
}

// Helper to assert correctness of the copied data.
void FdFileTest::TestDataMatches(const FdFile* src,
                                 const FdFile* dest,
                                 size_t input_offset,
                                 size_t output_offset,
                                 size_t copy_length) {
  art::MemMap::Init();
  std::string error_msg;
  art::MemMap src_mmap = art::MemMap::MapFile(copy_length + input_offset,
                                              PROT_READ,
                                              MAP_PRIVATE,
                                              src->Fd(),
                                              /*start=*/0,
                                              /*low_4gb=*/false,
                                              src->GetPath().c_str(),
                                              &error_msg);
  ASSERT_TRUE(src_mmap.IsValid()) << error_msg;
  art::MemMap dest_mmap = art::MemMap::MapFile(copy_length + output_offset,
                                               PROT_READ,
                                               MAP_PRIVATE,
                                               dest->Fd(),
                                               /*start=*/0,
                                               /*low_4gb=*/false,
                                               dest->GetPath().c_str(),
                                               &error_msg);
  ASSERT_TRUE(dest_mmap.IsValid()) << error_msg;

  EXPECT_EQ(0, memcmp(src_mmap.Begin() + input_offset,
                      dest_mmap.Begin() + output_offset,
                      copy_length));
}

#ifdef __linux__
// Test that the file created by FdFileTest::CreateSparseSourceFile is sparse on the test
// environment.
TEST_F(FdFileTest, CopySparseCreateSparseFile) {
  // Disable on host as sparsity is filesystem dependent and some hosts may break test assumptions.
  TEST_DISABLED_FOR_HOST();

  // Create file with no empty prefix or suffix.
  std::unique_ptr<art::ScratchFile> src1;
  ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(/*empty_prefix=*/0, /*empty_suffix=*/0, src1));

  struct stat src1_stat;
  ASSERT_EQ(fstat(src1->GetFd(), &src1_stat), 0);

  // It has at least as many allocated blocks required to represent the data chunks.
  EXPECT_GE(src1_stat.st_blocks * kStatBlockSize, kNumChunks * kChunkSize);
  // It is sparse: it has fewer allocated blocks than would be required if the whole file was data.
  EXPECT_LT(src1_stat.st_blocks * kStatBlockSize, src1_stat.st_size);

  // Create file with an empty prefix and empty suffix.
  std::unique_ptr<art::ScratchFile> src2;
  ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(kChunkSize, kChunkSize, src2));

  // File should have the same number of allocated blocks.
  struct stat src2_stat;
  ASSERT_EQ(fstat(src2->GetFd(), &src2_stat), 0);
  EXPECT_EQ(src2_stat.st_blocks, src1_stat.st_blocks);
}

// Test complete copies of the source file produced by FdFileTest::CreateSparseSourceFile.
TEST_F(FdFileTest, CopySparseFullCopy) {
  // Disable on host as sparsity is filesystem dependent and some hosts may break test assumptions.
  TEST_DISABLED_FOR_HOST();

  auto verify_fullcopy = [&](size_t empty_prefix, size_t empty_suffix) {
    SCOPED_TRACE(::testing::Message() << "prefix:" << empty_prefix << ", suffix:" << empty_suffix);

    std::unique_ptr<art::ScratchFile> src;
    ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(empty_prefix, empty_suffix, src));

    art::ScratchFile dest;
    ASSERT_TRUE(dest.GetFile()->IsOpened());

    off_t copy_size = src->GetFile()->GetLength();
    EXPECT_TRUE(dest.GetFile()->Copy(src->GetFile(), /*offset=*/0, copy_size));
    EXPECT_EQ(dest.GetFile()->Flush(), 0);

    // Test destination length.
    EXPECT_EQ(dest.GetFile()->GetLength(), copy_size);

    // Test FD offsets.
    EXPECT_EQ(lseek(dest.GetFd(), /*offset=*/0, SEEK_CUR), dest.GetFile()->GetLength());
    EXPECT_EQ(lseek(src->GetFd(), /*offset=*/0, SEEK_CUR), src->GetFile()->GetLength());

    // Test output sparsity matches the input sparsity.
    struct stat src_stat, dest_stat;
    ASSERT_EQ(fstat(src->GetFd(), &src_stat), 0);
    ASSERT_EQ(fstat(dest.GetFd(), &dest_stat), 0);
    EXPECT_EQ(dest_stat.st_blocks, src_stat.st_blocks);

    // Test the resulting data in the destination is correct.
    ASSERT_NO_FATAL_FAILURE(TestDataMatches(src->GetFile(),
                                            dest.GetFile(),
                                            /*input_offset=*/0u,
                                            /*output_offset=*/0u,
                                            copy_size));
  };

  // Test full copies using different offsets and outer skip regions of sizes [0, 128, 2048, 32768].
  ASSERT_NO_FATAL_FAILURE(verify_fullcopy(0, 0));
  for (size_t empty_region_size = 128;
       empty_region_size <= kChunkSize / 2;
       empty_region_size <<= 4) {
    // Empty prefix.
    ASSERT_NO_FATAL_FAILURE(verify_fullcopy(/*empty_prefix=*/empty_region_size,
                                            /*empty_suffix=*/0u));
    // Empty suffix.
    ASSERT_NO_FATAL_FAILURE(verify_fullcopy(/*empty_prefix=*/0u,
                                            /*empty_suffix=*/empty_region_size));
    // Both.
    ASSERT_NO_FATAL_FAILURE(verify_fullcopy(/*empty_prefix=*/empty_region_size,
                                            /*empty_suffix=*/empty_region_size));
  }
}

// Find the filesystem blocksize of the test environment by creating and calling fstat on a
// temporary file.
size_t FdFileTest::GetFilesystemBlockSize() {
  art::ScratchFile tmpfile;
  if (!tmpfile.GetFile()->IsOpened()) {
    return 0;
  }
  struct stat tmp_stat;
  if (fstat(tmpfile.GetFd(), &tmp_stat) != 0) {
    return 0;
  }
  return tmp_stat.st_blksize;
}

// Test partial copies of the source file produced by FdFileTest::CreateSparseSourceFile.
TEST_F(FdFileTest, CopySparsePartialCopy) {
  // Disable on host as sparsity is filesystem dependent and some hosts may break test assumptions.
  TEST_DISABLED_FOR_HOST();

  size_t blocksize = GetFilesystemBlockSize();
  ASSERT_GT(blocksize, 0u);

  auto verify_partialcopy = [&](size_t empty_prefix,
                                size_t empty_suffix,
                                size_t copy_start_offset,
                                size_t copy_end_offset) {
    // The copy starts <copy_start_offset> from the start of the source file.
    // The copy ends <copy_end_offset> from the end of the source file.
    SCOPED_TRACE(::testing::Message() << "prefix:" << empty_prefix << ", suffix:" << empty_suffix
                                      << ", copy_start_offset:" << copy_start_offset
                                      << ", copy_end_offset:" << copy_end_offset);

    std::unique_ptr<art::ScratchFile> src;
    ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(empty_prefix, empty_suffix, src));

    art::ScratchFile dest;
    ASSERT_TRUE(dest.GetFile()->IsOpened());

    off_t copy_size = src->GetFile()->GetLength() - copy_start_offset - copy_end_offset;
    EXPECT_TRUE(dest.GetFile()->Copy(src->GetFile(), copy_start_offset, copy_size));
    EXPECT_EQ(dest.GetFile()->Flush(), 0);

    // Test destination length.
    EXPECT_EQ(dest.GetFile()->GetLength(), copy_size);

    // Test FD offsets.
    EXPECT_EQ(lseek(dest.GetFd(), /*offset=*/0, SEEK_CUR), copy_size);
    EXPECT_EQ(lseek(src->GetFd(), /*offset=*/0, SEEK_CUR), copy_start_offset + copy_size);

    // Test output sparsity matches the input sparsity, accounting for any discarded blocks.
    // For simplicity, only reason about the sparsity when there is no empty prefix/suffix, and we
    // are discarding no more than the first and/or last chunk of data.
    if (empty_prefix == 0 && empty_suffix == 0 && copy_start_offset <= kChunkSize
        && copy_end_offset <= kChunkSize) {
      // Round down to whole filesystem blocks, then convert to fstat blocksize.
      size_t discarded_blocks = (copy_start_offset / blocksize) + (copy_end_offset / blocksize);
      discarded_blocks *= (blocksize / kStatBlockSize);

      struct stat src_stat, dest_stat;
      ASSERT_EQ(fstat(src->GetFd(), &src_stat), 0);
      ASSERT_EQ(fstat(dest.GetFd(), &dest_stat), 0);

      if (art::IsAlignedParam(copy_start_offset, blocksize)) {
        // We expect the sparsity to be preserved.
        EXPECT_EQ(dest_stat.st_blocks, src_stat.st_blocks - discarded_blocks);
      } else {
        // As all data chunks are aligned, an non-aligned copy can only decrease the sparsity.
        EXPECT_GT(dest_stat.st_blocks, src_stat.st_blocks - discarded_blocks);
      }
    }

    // Test the resulting data in the destination is correct.
    ASSERT_NO_FATAL_FAILURE(TestDataMatches(src->GetFile(),
                                            dest.GetFile(),
                                            /*input_offset=*/copy_start_offset,
                                            /*output_offset=*/0u,
                                            copy_size));
  };

  // Test partial copies with outer skip regions.
  std::vector<size_t> outer_regions = {0, 128, 2 * art::KB, 32 * art::KB};
  for (const auto& empty : outer_regions) {
    for (size_t discard = 0; discard <= 8 * art::KB; discard += 1 * art::KB) {
      // Start copy <discard> bytes after the file start.
      ASSERT_NO_FATAL_FAILURE(verify_partialcopy(/*empty_prefix=*/empty,
                                                 /*empty_suffix=*/empty,
                                                 /*copy_start_offset=*/discard,
                                                 /*copy_end_offset=*/0u));
      // End copy <discard> bytes before the file end.
      ASSERT_NO_FATAL_FAILURE(verify_partialcopy(/*empty_prefix=*/empty,
                                                 /*empty_suffix=*/empty,
                                                 /*copy_start_offset=*/0u,
                                                 /*copy_end_offset=*/discard));
      // Both.
      ASSERT_NO_FATAL_FAILURE(verify_partialcopy(/*empty_prefix=*/empty,
                                                 /*empty_suffix=*/empty,
                                                 /*copy_start_offset=*/discard,
                                                 /*copy_end_offset=*/discard));
    }
  }
}

// Test the case where the destination file's FD offset is non-zero before the copy.
TEST_F(FdFileTest, CopySparseToNonZeroOffset) {
  // Disable on host as sparsity is filesystem dependent and some hosts may break test assumptions.
  TEST_DISABLED_FOR_HOST();

  std::unique_ptr<art::ScratchFile> src;
  ASSERT_NO_FATAL_FAILURE(CreateSparseSourceFile(/*empty_prefix=*/0u, /*empty_suffix=*/0u, src));

  art::ScratchFile dest;
  FdFile* dest_file = dest.GetFile();
  ASSERT_TRUE(dest_file->IsOpened());

  // Move the destination file offset forward before starting the copy.
  constexpr size_t existing_length = kChunkSize;
  EXPECT_EQ(lseek(dest.GetFd(), existing_length, SEEK_SET), existing_length);
  ASSERT_EQ(dest_file->SetLength(existing_length), 0);

  off_t copy_size = src->GetFile()->GetLength();
  EXPECT_TRUE(dest_file->Copy(src->GetFile(), /*offset=*/0, copy_size));
  EXPECT_EQ(dest_file->Flush(), 0);

  // Test destination length.
  EXPECT_EQ(dest_file->GetLength(), existing_length + copy_size);

  // Test FD offsets.
  EXPECT_EQ(lseek(dest.GetFd(), /*offset=*/0, SEEK_CUR), dest_file->GetLength());
  EXPECT_EQ(lseek(src->GetFd(), /*offset=*/0, SEEK_CUR), src->GetFile()->GetLength());

  // Test the copied data appended to the destination is correct.
  ASSERT_NO_FATAL_FAILURE(TestDataMatches(src->GetFile(),
                                          dest_file,
                                          /*input_offset=*/0u,
                                          /*output_offset=*/existing_length,
                                          copy_size));
}
#endif

TEST_F(FdFileTest, MoveConstructor) {
  // New scratch file, zero-length.
  art::ScratchFile tmp;
  FdFile file(tmp.GetFilename(), O_RDWR, false);
  ASSERT_TRUE(file.IsOpened());
  EXPECT_GE(file.Fd(), 0);

  int old_fd = file.Fd();

  FdFile file2(std::move(file));
  EXPECT_FALSE(file.IsOpened());  // NOLINT - checking file is no longer opened after move
  EXPECT_TRUE(file2.IsOpened());
  EXPECT_EQ(old_fd, file2.Fd());

  ASSERT_EQ(file2.Flush(), 0);
  ASSERT_EQ(file2.Close(), 0);
}

TEST_F(FdFileTest, OperatorMoveEquals) {
  // Make sure the read_only_ flag is correctly copied
  // over.
  art::ScratchFile tmp;
  FdFile file(tmp.GetFilename(), O_RDONLY, false);
  ASSERT_TRUE(file.ReadOnlyMode());

  FdFile file2(tmp.GetFilename(), O_RDWR, false);
  ASSERT_FALSE(file2.ReadOnlyMode());

  file2 = std::move(file);
  ASSERT_TRUE(file2.ReadOnlyMode());
}

TEST_F(FdFileTest, EraseWithPathUnlinks) {
  // New scratch file, zero-length.
  art::ScratchFile tmp;
  std::string filename = tmp.GetFilename();
  tmp.Close();  // This is required because of the unlink race between the scratch file and the
                // FdFile, which leads to close-guard breakage.
  FdFile file(filename, O_RDWR, false);
  ASSERT_TRUE(file.IsOpened());
  EXPECT_GE(file.Fd(), 0);
  uint8_t buffer[16] = { 0 };
  EXPECT_TRUE(file.WriteFully(&buffer, sizeof(buffer)));
  EXPECT_EQ(file.Flush(), 0);

  EXPECT_TRUE(file.Erase(true));

  EXPECT_FALSE(file.IsOpened());

  EXPECT_FALSE(art::OS::FileExists(filename.c_str())) << filename;
}

TEST_F(FdFileTest, Compare) {
  std::vector<uint8_t> buffer;
  constexpr int64_t length = 17 * art::KB;
  for (size_t i = 0; i < length; ++i) {
    buffer.push_back(static_cast<uint8_t>(i));
  }

  auto reset_compare = [&](art::ScratchFile& a, art::ScratchFile& b) {
    a.GetFile()->ResetOffset();
    b.GetFile()->ResetOffset();
    return a.GetFile()->Compare(b.GetFile());
  };

  art::ScratchFile tmp;
  EXPECT_TRUE(tmp.GetFile()->WriteFully(&buffer[0], length));
  EXPECT_EQ(tmp.GetFile()->GetLength(), length);

  art::ScratchFile tmp2;
  EXPECT_TRUE(tmp2.GetFile()->WriteFully(&buffer[0], length));
  EXPECT_EQ(tmp2.GetFile()->GetLength(), length);

  // Basic equality check.
  tmp.GetFile()->ResetOffset();
  tmp2.GetFile()->ResetOffset();
  // Files should be the same.
  EXPECT_EQ(reset_compare(tmp, tmp2), 0);

  // Change a byte near the start.
  ++buffer[2];
  art::ScratchFile tmp3;
  EXPECT_TRUE(tmp3.GetFile()->WriteFully(&buffer[0], length));
  --buffer[2];
  EXPECT_NE(reset_compare(tmp, tmp3), 0);

  // Change a byte near the middle.
  ++buffer[length / 2];
  art::ScratchFile tmp4;
  EXPECT_TRUE(tmp4.GetFile()->WriteFully(&buffer[0], length));
  --buffer[length / 2];
  EXPECT_NE(reset_compare(tmp, tmp4), 0);

  // Change a byte near the end.
  ++buffer[length - 5];
  art::ScratchFile tmp5;
  EXPECT_TRUE(tmp5.GetFile()->WriteFully(&buffer[0], length));
  --buffer[length - 5];
  EXPECT_NE(reset_compare(tmp, tmp5), 0);

  // Reference check
  art::ScratchFile tmp6;
  EXPECT_TRUE(tmp6.GetFile()->WriteFully(&buffer[0], length));
  EXPECT_EQ(reset_compare(tmp, tmp6), 0);
}

TEST_F(FdFileTest, PipeFlush) {
  int pipefd[2];
  ASSERT_EQ(0, pipe2(pipefd, O_CLOEXEC));

  FdFile file(pipefd[1], true);
  ASSERT_TRUE(file.WriteFully("foo", 3));
  ASSERT_EQ(0, file.Flush());
  ASSERT_EQ(0, file.FlushCloseOrErase());
  close(pipefd[0]);
}

}  // namespace unix_file
