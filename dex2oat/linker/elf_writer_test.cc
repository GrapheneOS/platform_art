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

#include <gelf.h>
#include <libelf.h>
#include <sys/mman.h>  // For the PROT_NONE constant.

#include <cstdint>

#include "android-base/scopeguard.h"
#include "base/file_utils.h"
#include "base/mem_map.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "common_compiler_driver_test.h"
#include "driver/compiler_driver.h"
#include "elf/elf_builder.h"
#include "elf_writer_quick.h"
#include "oat/elf_file.h"
#include "oat/elf_file_impl.h"
#include "oat/oat.h"

namespace art {
namespace linker {

class ElfWriterTest : public CommonCompilerDriverTest {
 protected:
  void SetUp() override {
    ReserveImageSpace();
    CommonCompilerTest::SetUp();
    CreateCompilerDriver();
  }

  void WriteElf(File* oat_file,
                const std::vector<uint8_t>& rodata,
                const std::vector<uint8_t>& text,
                const std::vector<uint8_t>& data_img_rel_ro,
                size_t data_img_rel_ro_app_image_offset,
                size_t bss_size,
                size_t bss_methods_offset,
                size_t bss_roots_offset,
                size_t dex_section_size) {
    std::unique_ptr<ElfWriter> elf_writer = CreateElfWriterQuick(
      compiler_driver_->GetCompilerOptions(),
      oat_file);

    elf_writer->Start();
    OutputStream* rodata_section = elf_writer->StartRoData();

    elf_writer->PrepareDynamicSection(rodata.size(),
                                      text.size(),
                                      data_img_rel_ro.size(),
                                      data_img_rel_ro_app_image_offset,
                                      bss_size,
                                      bss_methods_offset,
                                      bss_roots_offset,
                                      dex_section_size);

    ASSERT_TRUE(rodata_section->WriteFully(rodata.data(), rodata.size()));
    elf_writer->EndRoData(rodata_section);

    OutputStream* text_section = elf_writer->StartText();
    ASSERT_TRUE(text_section->WriteFully(text.data(), text.size()));
    elf_writer->EndText(text_section);

    if (!data_img_rel_ro.empty()) {
      OutputStream* data_img_rel_ro_section = elf_writer->StartDataImgRelRo();
      ASSERT_TRUE(data_img_rel_ro_section->WriteFully(data_img_rel_ro.data(),
          data_img_rel_ro.size()));
      elf_writer->EndDataImgRelRo(data_img_rel_ro_section);
    }

    elf_writer->WriteDynamicSection();
    ASSERT_TRUE(elf_writer->End());
  }
};

static void FindSymbolAddress(File* file, const char* symbol_name, /*out*/ uint8_t** addr) {
  ASSERT_NE(elf_version(EV_CURRENT), EV_NONE) << "libelf initialization failed: " << elf_errmsg(-1);

  Elf* elf = elf_begin(file->Fd(), ELF_C_READ, /*ref=*/nullptr);
  ASSERT_NE(elf, nullptr) << elf_errmsg(-1);
  auto elf_cleanup = android::base::make_scope_guard([&]() { elf_end(elf); });

  Elf_Scn* dyn_scn = nullptr;
  GElf_Shdr scn_hdr;
  while ((dyn_scn = elf_nextscn(elf, dyn_scn)) != nullptr) {
    ASSERT_EQ(gelf_getshdr(dyn_scn, &scn_hdr), &scn_hdr) << elf_errmsg(-1);
    if (scn_hdr.sh_type == SHT_DYNSYM) {
      break;
    }
  }
  ASSERT_NE(dyn_scn, nullptr) << "Section SHT_DYNSYM not found";

  Elf_Data* data = elf_getdata(dyn_scn, /*data=*/nullptr);

  // Iterate through dynamic section entries.
  for (int i = 0; i < scn_hdr.sh_size / scn_hdr.sh_entsize; i++) {
    GElf_Sym sym;
    ASSERT_EQ(gelf_getsym(data, i, &sym), &sym) << elf_errmsg(-1);
    const char* name = elf_strptr(elf, scn_hdr.sh_link, sym.st_name);
    if (strcmp(name, symbol_name) == 0) {
      *addr = reinterpret_cast<uint8_t*>(sym.st_value);
      break;
    }
  }

  ASSERT_NE(*addr, nullptr) << "Symbol " << symbol_name << "not found";
}

TEST_F(ElfWriterTest, dlsym) {
  std::string elf_location = GetCoreOatLocation();
  std::string elf_filename = GetSystemImageFilename(elf_location.c_str(), kRuntimeISA);
  LOG(INFO) << "elf_filename=" << elf_filename;

  UnreserveImageSpace();
  uint8_t* dl_oatdata = nullptr;
  uint8_t* dl_oatexec = nullptr;
  uint8_t* dl_oatlastword = nullptr;

  std::unique_ptr<File> file(OS::OpenFileForReading(elf_filename.c_str()));
  ASSERT_TRUE(file.get() != nullptr) << elf_filename;
  ASSERT_NO_FATAL_FAILURE(FindSymbolAddress(file.get(), "oatdata", &dl_oatdata));
  ASSERT_NO_FATAL_FAILURE(FindSymbolAddress(file.get(), "oatexec", &dl_oatexec));
  ASSERT_NO_FATAL_FAILURE(FindSymbolAddress(file.get(), "oatlastword", &dl_oatlastword));
  {
    std::string error_msg;
    std::unique_ptr<ElfFile> ef(ElfFile::Open(file.get(),
                                              /*low_4gb=*/false,
                                              &error_msg));
    CHECK(ef.get() != nullptr) << error_msg;
    size_t size;
    bool success = ef->GetLoadedSize(&size, &error_msg);
    CHECK(success) << error_msg;
    MemMap reservation = MemMap::MapAnonymous("ElfWriterTest#dlsym reservation",
                                              RoundUp(size, MemMap::GetPageSize()),
                                              PROT_NONE,
                                              /*low_4gb=*/true,
                                              &error_msg);
    CHECK(reservation.IsValid()) << error_msg;
    uint8_t* base = reservation.Begin();
    success = ef->Load(/*executable=*/false, /*low_4gb=*/false, &reservation, &error_msg);
    CHECK(success) << error_msg;
    CHECK(!reservation.IsValid());
    EXPECT_EQ(reinterpret_cast<uintptr_t>(dl_oatdata) + reinterpret_cast<uintptr_t>(base),
              reinterpret_cast<uintptr_t>(ef->FindDynamicSymbolAddress("oatdata")));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(dl_oatexec) + reinterpret_cast<uintptr_t>(base),
              reinterpret_cast<uintptr_t>(ef->FindDynamicSymbolAddress("oatexec")));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(dl_oatlastword) + reinterpret_cast<uintptr_t>(base),
              reinterpret_cast<uintptr_t>(ef->FindDynamicSymbolAddress("oatlastword")));
  }
}

static void HasSection(File* file, const char* section_name, /*out*/ bool* result) {
  ASSERT_NE(elf_version(EV_CURRENT), EV_NONE) << "libelf initialization failed: " << elf_errmsg(-1);

  Elf* elf = elf_begin(file->Fd(), ELF_C_READ, /*ref=*/nullptr);
  ASSERT_NE(elf, nullptr) << elf_errmsg(-1);
  auto elf_cleanup = android::base::make_scope_guard([&]() { elf_end(elf); });

  size_t shstrndx = 0;
  ASSERT_EQ(elf_getshdrstrndx(elf, &shstrndx), 0) << elf_errmsg(-1);

  Elf_Scn* dyn_scn = nullptr;
  GElf_Shdr scn_hdr;
  while ((dyn_scn = elf_nextscn(elf, dyn_scn)) != nullptr) {
    ASSERT_EQ(gelf_getshdr(dyn_scn, &scn_hdr), &scn_hdr) << elf_errmsg(-1);
    const char* name = elf_strptr(elf, shstrndx, scn_hdr.sh_name);
    if (strcmp(name, section_name) == 0) {
      *result = true;
      return;
    }
  }
  *result = false;
}

TEST_F(ElfWriterTest, CheckBuildIdPresent) {
  std::string elf_location = GetCoreOatLocation();
  std::string elf_filename = GetSystemImageFilename(elf_location.c_str(), kRuntimeISA);
  LOG(INFO) << "elf_filename=" << elf_filename;

  std::unique_ptr<File> file(OS::OpenFileForReading(elf_filename.c_str()));
  ASSERT_TRUE(file.get() != nullptr);

  bool result;
  ASSERT_NO_FATAL_FAILURE(HasSection(file.get(), ".note.gnu.build-id", &result));
  EXPECT_TRUE(result);
}

// Check that dynamic sections (.dynamic, .dynsym, .dynstr, .hash) in an oat file are formed
// correctly so that dynamic symbols can be successfully looked up.
TEST_F(ElfWriterTest, CheckDynamicSection) {
  // This function generates an oat file with the specified oat data sizes and offsets and
  // verifies it:
  // * Checks that the file can be loaded by the ELF loader.
  // * Checks that the expected dynamic symbols exist and point to the corresponding data
  //   in the loaded file.
  // * Checks the alignment of the oat data.
  // The function returns the number of dynamic symbols (excluding "lastword" ones) in the
  // generated oat file.
  auto verify = [this](size_t rodata_size,
                       size_t text_size,
                       size_t data_img_rel_ro_size,
                       size_t data_img_rel_ro_app_image_offset,
                       size_t bss_size,
                       size_t bss_methods_offset,
                       size_t bss_roots_offset,
                       size_t dex_section_size,
                       /*out*/ size_t *number_of_dynamic_symbols) {
    SCOPED_TRACE(testing::Message() << "rodata_size: " << rodata_size
                 << ", text_size: " << text_size
                 << ", data_img_rel_ro_size: " << data_img_rel_ro_size
                 << ", data_img_rel_ro_app_image_offset: " << data_img_rel_ro_app_image_offset
                 << ", bss_size: " << bss_size
                 << ", bss_methods_offset: " << bss_methods_offset
                 << ", bss_roots_offset: " << bss_roots_offset
                 << ", dex_section_size: " << dex_section_size);

    *number_of_dynamic_symbols = 1;  // "oatdata".
    std::vector<uint8_t> rodata(rodata_size, 0xAA);
    std::vector<uint8_t> text(text_size, 0xBB);
    std::vector<uint8_t> data_img_rel_ro(data_img_rel_ro_app_image_offset, 0xCC);
    size_t data_img_rel_ro_app_image_size = data_img_rel_ro_size - data_img_rel_ro_app_image_offset;
    data_img_rel_ro.insert(data_img_rel_ro.cend(), data_img_rel_ro_app_image_size, 0xDD);

    ScratchFile tmp_base, tmp_oat(tmp_base, ".oat");
    WriteElf(tmp_oat.GetFile(),
             rodata,
             text,
             data_img_rel_ro,
             data_img_rel_ro_app_image_offset,
             bss_size,
             bss_methods_offset,
             bss_roots_offset,
             dex_section_size);

    std::string error_msg;
    std::unique_ptr<ElfFile> ef(ElfFile::Open(tmp_oat.GetFile(),
                                              /*low_4gb=*/false,
                                              &error_msg));
    ASSERT_NE(ef.get(), nullptr) << error_msg;
    ASSERT_TRUE(ef->Load(/*executable=*/false,
                         /*low_4gb=*/false,
                         /*reservation=*/nullptr,
                         &error_msg))
        << error_msg;

    const uint8_t* oatdata_ptr = ef->FindDynamicSymbolAddress("oatdata");
    ASSERT_NE(oatdata_ptr, nullptr);
    EXPECT_EQ(memcmp(oatdata_ptr, rodata.data(), rodata.size()), 0);

    size_t page_size = GetPageSizeSlow();
    size_t elf_word_size = ef->Is64Bit() ? sizeof(ElfTypes64::Word) : sizeof(ElfTypes32::Word);

    if (text_size != 0u) {
      *number_of_dynamic_symbols += 1;
      const uint8_t* text_ptr = ef->FindDynamicSymbolAddress("oatexec");
      ASSERT_NE(text_ptr, nullptr);
      ASSERT_TRUE(IsAlignedParam(text_ptr, page_size));
      EXPECT_EQ(memcmp(text_ptr, text.data(), text.size()), 0);

      const uint8_t* oatlastword_ptr = ef->FindDynamicSymbolAddress("oatlastword");
      ASSERT_NE(oatlastword_ptr, nullptr);
      EXPECT_EQ(static_cast<size_t>(oatlastword_ptr - text_ptr), text_size - elf_word_size);
    } else if (rodata_size != 0u) {
      const uint8_t* oatlastword_ptr = ef->FindDynamicSymbolAddress("oatlastword");
      ASSERT_NE(oatlastword_ptr, nullptr);
      EXPECT_EQ(static_cast<size_t>(oatlastword_ptr - oatdata_ptr), rodata_size - elf_word_size);
    }

    if (data_img_rel_ro_size != 0u) {
      *number_of_dynamic_symbols += 1;
      const uint8_t* oatdataimgrelro_ptr = ef->FindDynamicSymbolAddress("oatdataimgrelro");
      ASSERT_NE(oatdataimgrelro_ptr, nullptr);
      ASSERT_TRUE(IsAlignedParam(oatdataimgrelro_ptr, page_size));
      EXPECT_EQ(memcmp(oatdataimgrelro_ptr, data_img_rel_ro.data(), data_img_rel_ro.size()), 0);

      const uint8_t* oatdataimgrelrolastword_ptr =
          ef->FindDynamicSymbolAddress("oatdataimgrelrolastword");
      ASSERT_NE(oatdataimgrelrolastword_ptr, nullptr);
      EXPECT_EQ(static_cast<size_t>(oatdataimgrelrolastword_ptr - oatdataimgrelro_ptr),
          data_img_rel_ro_size - elf_word_size);

      if (data_img_rel_ro_app_image_offset != data_img_rel_ro_size) {
        *number_of_dynamic_symbols += 1;
        const uint8_t* oatdataimgrelroappimage_ptr =
            ef->FindDynamicSymbolAddress("oatdataimgrelroappimage");
        ASSERT_NE(oatdataimgrelroappimage_ptr, nullptr);
        EXPECT_EQ(static_cast<size_t>(oatdataimgrelroappimage_ptr - oatdataimgrelro_ptr),
          data_img_rel_ro_app_image_offset);
      }

      if (bss_size != 0u) {
        *number_of_dynamic_symbols += 1;
        const uint8_t* bss_ptr = ef->FindDynamicSymbolAddress("oatbss");
        ASSERT_NE(bss_ptr, nullptr);
        ASSERT_TRUE(IsAlignedParam(bss_ptr, page_size));

        if (bss_methods_offset != bss_roots_offset) {
          *number_of_dynamic_symbols += 1;
          const uint8_t* oatbssmethods_ptr = ef->FindDynamicSymbolAddress("oatbssmethods");
          ASSERT_NE(oatbssmethods_ptr, nullptr);
          EXPECT_EQ(static_cast<size_t>(oatbssmethods_ptr - bss_ptr), bss_methods_offset);
        }

        if (bss_roots_offset != bss_size) {
          *number_of_dynamic_symbols += 1;
          const uint8_t* oatbssroots_ptr = ef->FindDynamicSymbolAddress("oatbssroots");
          ASSERT_NE(oatbssroots_ptr, nullptr);
          EXPECT_EQ(static_cast<size_t>(oatbssroots_ptr - bss_ptr), bss_roots_offset);
        }

        const uint8_t* oatbsslastword_ptr = ef->FindDynamicSymbolAddress("oatbsslastword");
        ASSERT_NE(oatbsslastword_ptr, nullptr);
        EXPECT_EQ(static_cast<size_t>(oatbsslastword_ptr - bss_ptr), bss_size - elf_word_size);
      }
    }

    if (dex_section_size != 0u) {
      *number_of_dynamic_symbols += 1;
      const uint8_t* dex_ptr = ef->FindDynamicSymbolAddress("oatdex");
      ASSERT_NE(dex_ptr, nullptr);
      ASSERT_TRUE(IsAlignedParam(dex_ptr, page_size));
      const uint8_t* oatdexlastword_ptr = ef->FindDynamicSymbolAddress("oatdexlastword");
      EXPECT_EQ(static_cast<size_t>(oatdexlastword_ptr - dex_ptr),
          dex_section_size - elf_word_size);
    }
  };

  // If a symbol requires some other ones (e.g. kBssMethods requires kBss),
  // it should be listed after them.
  enum class Symbol {
    kRodata,
    kText,
    kDataImgRelRo,
    kDataImgRelRoAppImage,
    kBss,
    kBssMethods,
    kBssRoots,
    kDex,
    kLast = kDex
  };

  constexpr size_t kNumberOfSymbols = static_cast<size_t>(Symbol::kLast) + 1;

  // Use an unaligned section size to verify that ElfWriter properly aligns sections in this case.
  // We can use an arbitrary value that is greater than or equal to an ElfWord (4 bytes).
  constexpr size_t kSectionSize = 127u;
  // Offset in .data.img.rel.ro section from its beginning. We can use any value in the range
  // [0, kSectionSize).
  constexpr size_t kDataImgRelRoAppImageOffset = kSectionSize / 2;
  // Offsets in .bss from its beginning. We can use any value in the range [0, kSectionSize),
  // kBssMethodsOffset should be less than or equal to kBssRootsOffset.
  constexpr size_t kBssMethodsOffset = kSectionSize / 3;
  constexpr size_t kBssRootsOffset = 2 * kBssMethodsOffset;

  auto exists = [](Symbol symbol, const std::bitset<kNumberOfSymbols> &symbols) {
    return symbols.test(static_cast<size_t>(symbol));
  };

  auto get_size = [&](Symbol symbol, const std::bitset<kNumberOfSymbols> &symbols) -> size_t {
    return exists(symbol, symbols) ? kSectionSize : 0;
  };

  std::bitset<kNumberOfSymbols> symbols;
  symbols.set();

  // Check cases that lead to a different number of dynamic symbols in an oat file.
  // We start with the case where all symbols exist (corresponding to the bitset 11111111)
  // and continue to the case where only "oatdata" exists:
  //  11111111 - all symbols exist.
  //  01111111 - "oatdex" doesn't exist (least significant bit corresponds to "oatdata").
  //  00111111 - "oatdex" and "oatbss" don't exist.
  //  ...
  //  00000001 - only "oatdata" exists.
  while (symbols.any()) {
    DCHECK_IMPLIES(exists(Symbol::kDataImgRelRoAppImage, symbols),
        exists(Symbol::kDataImgRelRo, symbols));
    DCHECK_IMPLIES(exists(Symbol::kBssMethods, symbols), exists(Symbol::kBss, symbols));
    DCHECK_IMPLIES(exists(Symbol::kBssRoots, symbols), exists(Symbol::kBss, symbols));
    DCHECK_IMPLIES(exists(Symbol::kBssRoots, symbols), exists(Symbol::kBssMethods, symbols));

    size_t data_img_rel_ro_size = get_size(Symbol::kDataImgRelRo, symbols);
    size_t bss_size = get_size(Symbol::kBss, symbols);
    size_t number_of_dynamic_symbols = 0;
    verify(get_size(Symbol::kRodata, symbols),
           get_size(Symbol::kText, symbols),
           data_img_rel_ro_size,
           exists(Symbol::kDataImgRelRoAppImage, symbols)
              ? kDataImgRelRoAppImageOffset
              : data_img_rel_ro_size,
           bss_size,
           exists(Symbol::kBssMethods, symbols) ? kBssMethodsOffset : bss_size,
           exists(Symbol::kBssRoots, symbols) ? kBssRootsOffset : bss_size,
           get_size(Symbol::kDex, symbols),
           &number_of_dynamic_symbols);
    EXPECT_EQ(number_of_dynamic_symbols, symbols.count())
      << "number_of_dynamic_symbols: " << number_of_dynamic_symbols
      << ", symbols: " << symbols;
    symbols >>= 1;
  }
}

}  // namespace linker
}  // namespace art
