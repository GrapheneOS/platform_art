/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "elf_file.h"

#include <inttypes.h>
#include <sys/mman.h>  // For the PROT_* and MAP_* constants.
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <memory>

#include "android-base/stringprintf.h"
#include "arch/instruction_set.h"
#include "base/casts.h"
#include "base/os.h"
#include "base/unix_file/fd_file.h"
#include "elf/elf_utils.h"
#include "elf_file_impl.h"

namespace art HIDDEN {

using android::base::StringPrintf;

template <typename ElfTypes>
ElfFileImpl<ElfTypes>* ElfFileImpl<ElfTypes>::Open(File* file,
                                                   off_t start,
                                                   size_t file_length,
                                                   const std::string& file_location,
                                                   bool low_4gb,
                                                   std::string* error_msg) {
  std::unique_ptr<ElfFileImpl<ElfTypes>> elf_file(
      new ElfFileImpl<ElfTypes>(file, start, file_length, file_location));
  if (!elf_file->Setup(low_4gb, error_msg)) {
    return nullptr;
  }
  return elf_file.release();
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::Setup(bool low_4gb, std::string* error_msg) {
  if (file_length_ < sizeof(Elf_Ehdr)) {
    *error_msg = StringPrintf(
        "File size of %zd bytes not large enough to contain ELF header of "
        "%zd bytes: '%s'",
        file_length_,
        sizeof(Elf_Ehdr),
        file_location_.c_str());
    return false;
  }

  int prot = PROT_READ;
  int flags = MAP_PRIVATE;

  // first just map ELF header to get program header size information
  size_t elf_header_size = sizeof(Elf_Ehdr);
  if (!SetMap(MemMap::MapFile(elf_header_size,
                              prot,
                              flags,
                              file_->Fd(),
                              start_,
                              low_4gb,
                              file_location_.c_str(),
                              error_msg),
              error_msg)) {
    return false;
  }
  // then remap to cover program header
  size_t program_header_size = header_->e_phoff + (header_->e_phentsize * header_->e_phnum);
  if (file_length_ < program_header_size) {
    *error_msg = StringPrintf(
        "File size of %zd bytes not large enough to contain ELF program header of %zd bytes: '%s'",
        file_length_,
        sizeof(Elf_Ehdr),
        file_location_.c_str());
    return false;
  }
  if (!SetMap(MemMap::MapFile(program_header_size,
                              prot,
                              flags,
                              file_->Fd(),
                              start_,
                              low_4gb,
                              file_location_.c_str(),
                              error_msg),
              error_msg)) {
    *error_msg = StringPrintf("Failed to map ELF program headers: %s", error_msg->c_str());
    return false;
  }

  program_headers_start_ = Begin() + GetHeader().e_phoff;

  return true;
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::CheckSectionsExist(std::string* error_msg) const {
  // This is redundant, but defensive.
  if (dynamic_program_header_ == nullptr) {
    *error_msg = StringPrintf("Failed to find PT_DYNAMIC program header in ELF file: '%s'",
                              file_location_.c_str());
    return false;
  }

  // Need a dynamic section. This is redundant, but defensive.
  if (dynamic_section_start_ == nullptr) {
    *error_msg =
        StringPrintf("Failed to find dynamic section in ELF file: '%s'", file_location_.c_str());
    return false;
  }

  // Symtab validation. These is not really a hard failure, as we are currently not using the
  // symtab internally, but it's nice to be defensive.
  if (symtab_section_start_ != nullptr) {
    // When there's a symtab, there should be a strtab.
    if (strtab_section_start_ == nullptr) {
      *error_msg = StringPrintf("No strtab for symtab in ELF file: '%s'", file_location_.c_str());
      return false;
    }
  }

  // We always need a dynstr & dynsym.
  if (dynstr_section_start_ == nullptr) {
    *error_msg = StringPrintf("No dynstr in ELF file: '%s'", file_location_.c_str());
    return false;
  }
  if (dynsym_section_start_ == nullptr) {
    *error_msg = StringPrintf("No dynsym in ELF file: '%s'", file_location_.c_str());
    return false;
  }

  // Need a hash section for dynamic symbol lookup.
  if (hash_section_start_ == nullptr) {
    *error_msg =
        StringPrintf("Failed to find hash section in ELF file: '%s'", file_location_.c_str());
    return false;
  }

  // We'd also like to confirm a shstrtab. This is usually the last in an oat file, and a good
  // indicator of whether writing was successful (or the process crashed and left garbage).
  // It might not be mapped, but we can compare against the file size.
  size_t offset = GetHeader().e_shoff + (GetHeader().e_shstrndx * GetHeader().e_shentsize);
  if (offset >= file_length_) {
    *error_msg =
        StringPrintf("Shstrtab is not in the mapped ELF file: '%s'", file_location_.c_str());
    return false;
  }

  return true;
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::SetMap(MemMap&& map, std::string* error_msg) {
  if (!map.IsValid()) {
    // MemMap::Open should have already set an error.
    DCHECK(!error_msg->empty());
    return false;
  }
  map_ = std::move(map);
  CHECK(map_.IsValid()) << file_location_;
  CHECK(map_.Begin() != nullptr) << file_location_;

  header_ = reinterpret_cast<Elf_Ehdr*>(map_.Begin());
  if ((ELFMAG0 != header_->e_ident[EI_MAG0])
      || (ELFMAG1 != header_->e_ident[EI_MAG1])
      || (ELFMAG2 != header_->e_ident[EI_MAG2])
      || (ELFMAG3 != header_->e_ident[EI_MAG3])) {
    *error_msg = StringPrintf("Failed to find ELF magic value %d %d %d %d in %s, found %d %d %d %d",
                              ELFMAG0,
                              ELFMAG1,
                              ELFMAG2,
                              ELFMAG3,
                              file_location_.c_str(),
                              header_->e_ident[EI_MAG0],
                              header_->e_ident[EI_MAG1],
                              header_->e_ident[EI_MAG2],
                              header_->e_ident[EI_MAG3]);
    return false;
  }
  uint8_t elf_class = (sizeof(Elf_Addr) == sizeof(Elf64_Addr)) ? ELFCLASS64 : ELFCLASS32;
  if (elf_class != header_->e_ident[EI_CLASS]) {
    *error_msg = StringPrintf("Failed to find expected EI_CLASS value %d in %s, found %d",
                              elf_class,
                              file_location_.c_str(),
                              header_->e_ident[EI_CLASS]);
    return false;
  }
  if (ELFDATA2LSB != header_->e_ident[EI_DATA]) {
    *error_msg = StringPrintf("Failed to find expected EI_DATA value %d in %s, found %d",
                              ELFDATA2LSB,
                              file_location_.c_str(),
                              header_->e_ident[EI_CLASS]);
    return false;
  }
  if (EV_CURRENT != header_->e_ident[EI_VERSION]) {
    *error_msg = StringPrintf("Failed to find expected EI_VERSION value %d in %s, found %d",
                              EV_CURRENT,
                              file_location_.c_str(),
                              header_->e_ident[EI_CLASS]);
    return false;
  }
  if (ET_DYN != header_->e_type) {
    *error_msg = StringPrintf("Failed to find expected e_type value %d in %s, found %d",
                              ET_DYN,
                              file_location_.c_str(),
                              header_->e_type);
    return false;
  }
  if (EV_CURRENT != header_->e_version) {
    *error_msg = StringPrintf("Failed to find expected e_version value %d in %s, found %d",
                              EV_CURRENT,
                              file_location_.c_str(),
                              header_->e_version);
    return false;
  }
  if (0 != header_->e_entry) {
    *error_msg = StringPrintf("Failed to find expected e_entry value %d in %s, found %d",
                              0,
                              file_location_.c_str(),
                              static_cast<int32_t>(header_->e_entry));
    return false;
  }
  if (0 == header_->e_phoff) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_phoff value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_shoff) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_shoff value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_ehsize) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_ehsize value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_phentsize) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_phentsize value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_phnum) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_phnum value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_shentsize) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_shentsize value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_shnum) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_shnum value in %s", file_location_.c_str());
    return false;
  }
  if (0 == header_->e_shstrndx) {
    *error_msg =
        StringPrintf("Failed to find non-zero e_shstrndx value in %s", file_location_.c_str());
    return false;
  }
  if (header_->e_shstrndx >= header_->e_shnum) {
    *error_msg = StringPrintf("Failed to find e_shnum value %d less than %d in %s",
                              header_->e_shstrndx,
                              header_->e_shnum,
                              file_location_.c_str());
    return false;
  }
  return true;
}

template <typename ElfTypes>
typename ElfTypes::Ehdr& ElfFileImpl<ElfTypes>::GetHeader() const {
  CHECK(header_ != nullptr);  // Header has been checked in SetMap
  return *header_;
}

template <typename ElfTypes>
uint8_t* ElfFileImpl<ElfTypes>::GetProgramHeadersStart() const {
  CHECK(program_headers_start_ != nullptr);  // Header has been set in Setup
  return program_headers_start_;
}

template <typename ElfTypes>
typename ElfTypes::Phdr& ElfFileImpl<ElfTypes>::GetDynamicProgramHeader() const {
  CHECK(dynamic_program_header_ != nullptr);  // Is checked in CheckSectionsExist
  return *dynamic_program_header_;
}

template <typename ElfTypes>
typename ElfTypes::Dyn* ElfFileImpl<ElfTypes>::GetDynamicSectionStart() const {
  CHECK(dynamic_section_start_ != nullptr);  // Is checked in CheckSectionsExist
  return dynamic_section_start_;
}

template <typename ElfTypes>
typename ElfTypes::Sym* ElfFileImpl<ElfTypes>::GetSymbolSectionStart(
    Elf_Word section_type) const {
  CHECK(IsSymbolSectionType(section_type)) << file_location_ << " " << section_type;
  switch (section_type) {
    case SHT_SYMTAB: {
      return symtab_section_start_;
      break;
    }
    case SHT_DYNSYM: {
      return dynsym_section_start_;
      break;
    }
    default: {
      LOG(FATAL) << section_type;
      return nullptr;
    }
  }
}

template <typename ElfTypes>
const char* ElfFileImpl<ElfTypes>::GetStringSectionStart(
    Elf_Word section_type) const {
  CHECK(IsSymbolSectionType(section_type)) << file_location_ << " " << section_type;
  switch (section_type) {
    case SHT_SYMTAB: {
      return strtab_section_start_;
    }
    case SHT_DYNSYM: {
      return dynstr_section_start_;
    }
    default: {
      LOG(FATAL) << section_type;
      return nullptr;
    }
  }
}

template <typename ElfTypes>
const char* ElfFileImpl<ElfTypes>::GetString(Elf_Word section_type,
                                             Elf_Word i) const {
  CHECK(IsSymbolSectionType(section_type)) << file_location_ << " " << section_type;
  if (i == 0) {
    return nullptr;
  }
  const char* string_section_start = GetStringSectionStart(section_type);
  if (string_section_start == nullptr) {
    return nullptr;
  }
  return string_section_start + i;
}

// WARNING: The following methods do not check for an error condition (non-existent hash section).
//          It is the caller's job to do this.

template <typename ElfTypes>
typename ElfTypes::Word* ElfFileImpl<ElfTypes>::GetHashSectionStart() const {
  return hash_section_start_;
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetHashBucketNum() const {
  return GetHashSectionStart()[0];
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetHashChainNum() const {
  return GetHashSectionStart()[1];
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetHashBucket(size_t i, bool* ok) const {
  if (i >= GetHashBucketNum()) {
    *ok = false;
    return 0;
  }
  *ok = true;
  // 0 is nbucket, 1 is nchain
  return GetHashSectionStart()[2 + i];
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetHashChain(size_t i, bool* ok) const {
  if (i >= GetHashChainNum()) {
    *ok = false;
    return 0;
  }
  *ok = true;
  // 0 is nbucket, 1 is nchain, & chains are after buckets
  return GetHashSectionStart()[2 + GetHashBucketNum() + i];
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetProgramHeaderNum() const {
  return GetHeader().e_phnum;
}

template <typename ElfTypes>
typename ElfTypes::Phdr* ElfFileImpl<ElfTypes>::GetProgramHeader(Elf_Word i) const {
  CHECK_LT(i, GetProgramHeaderNum()) << file_location_;  // Validity check for caller.
  uint8_t* program_header = GetProgramHeadersStart() + (i * GetHeader().e_phentsize);
  CHECK_LT(program_header, End());
  return reinterpret_cast<Elf_Phdr*>(program_header);
}

template <typename ElfTypes>
typename ElfTypes::Phdr* ElfFileImpl<ElfTypes>::FindProgamHeaderByType(Elf_Word type) const {
  for (Elf_Word i = 0; i < GetProgramHeaderNum(); i++) {
    Elf_Phdr* program_header = GetProgramHeader(i);
    if (program_header->p_type == type) {
      return program_header;
    }
  }
  return nullptr;
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetSectionHeaderNum() const {
  return GetHeader().e_shnum;
}

// from bionic
static unsigned elfhash(const char *_name) {
  const unsigned char *name = (const unsigned char *) _name;
  unsigned h = 0, g;

  while (*name) {
    h = (h << 4) + *name++;
    g = h & 0xf0000000;
    h ^= g;
    h ^= g >> 24;
  }
  return h;
}

template <typename ElfTypes>
const uint8_t* ElfFileImpl<ElfTypes>::FindDynamicSymbolAddress(
    const std::string& symbol_name) const {
  // Check that we have a hash section.
  if (GetHashSectionStart() == nullptr) {
    return nullptr;  // Failure condition.
  }
  const Elf_Sym* sym = FindDynamicSymbol(symbol_name);
  if (sym != nullptr) {
    // TODO: we need to change this to calculate base_address_ in ::Open,
    // otherwise it will be wrongly 0 if ::Load has not yet been called.
    return base_address_ + sym->st_value;
  } else {
    return nullptr;
  }
}

// WARNING: Only called from FindDynamicSymbolAddress. Elides check for hash section.
template <typename ElfTypes>
const typename ElfTypes::Sym* ElfFileImpl<ElfTypes>::FindDynamicSymbol(
    const std::string& symbol_name) const {
  if (GetHashBucketNum() == 0) {
    // No dynamic symbols at all.
    return nullptr;
  }
  Elf_Word hash = elfhash(symbol_name.c_str());
  Elf_Word bucket_index = hash % GetHashBucketNum();
  bool ok;
  Elf_Word symbol_and_chain_index = GetHashBucket(bucket_index, &ok);
  if (!ok) {
    return nullptr;
  }
  while (symbol_and_chain_index != 0 /* STN_UNDEF */) {
    Elf_Sym* symbol = GetSymbol(SHT_DYNSYM, symbol_and_chain_index);
    if (symbol == nullptr) {
      return nullptr;  // Failure condition.
    }
    const char* name = GetString(SHT_DYNSYM, symbol->st_name);
    if (symbol_name == name) {
      return symbol;
    }
    symbol_and_chain_index = GetHashChain(symbol_and_chain_index, &ok);
    if (!ok) {
      return nullptr;
    }
  }
  return nullptr;
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::IsSymbolSectionType(Elf_Word section_type) {
  return ((section_type == SHT_SYMTAB) || (section_type == SHT_DYNSYM));
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetSymbolNum(Elf_Shdr& section_header) const {
  CHECK(IsSymbolSectionType(section_header.sh_type))
      << file_location_ << " " << section_header.sh_type;
  CHECK_NE(0U, section_header.sh_entsize) << file_location_;
  return section_header.sh_size / section_header.sh_entsize;
}

template <typename ElfTypes>
typename ElfTypes::Sym* ElfFileImpl<ElfTypes>::GetSymbol(Elf_Word section_type, Elf_Word i) const {
  Elf_Sym* sym_start = GetSymbolSectionStart(section_type);
  if (sym_start == nullptr) {
    return nullptr;
  }
  return sym_start + i;
}

template <typename ElfTypes>
typename ElfTypes::Word ElfFileImpl<ElfTypes>::GetDynamicNum() const {
  return GetDynamicProgramHeader().p_filesz / sizeof(Elf_Dyn);
}

template <typename ElfTypes>
typename ElfTypes::Dyn& ElfFileImpl<ElfTypes>::GetDynamic(Elf_Word i) const {
  CHECK_LT(i, GetDynamicNum()) << file_location_;
  return *(GetDynamicSectionStart() + i);
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::GetLoadedSize(size_t* size, std::string* error_msg) const {
  uint8_t* vaddr_begin;
  return GetLoadedAddressRange(&vaddr_begin, size, error_msg);
}

template <typename ElfTypes>
size_t ElfFileImpl<ElfTypes>::GetElfSegmentAlignmentFromFile() const {
  // Return the alignment of the first loadable program segment.
  for (Elf_Word i = 0; i < GetProgramHeaderNum(); i++) {
    Elf_Phdr* program_header = GetProgramHeader(i);
    if (program_header->p_type != PT_LOAD) {
      continue;
    }
    return program_header->p_align;
  }
  LOG(ERROR) << "No loadable segment found in ELF file " << file_location_;
  return 0;
}

// Base on bionic phdr_table_get_load_size
template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::GetLoadedAddressRange(/*out*/uint8_t** vaddr_begin,
                                                  /*out*/size_t* vaddr_size,
                                                  /*out*/std::string* error_msg) const {
  Elf_Addr min_vaddr = static_cast<Elf_Addr>(-1);
  Elf_Addr max_vaddr = 0u;
  for (Elf_Word i = 0; i < GetProgramHeaderNum(); i++) {
    Elf_Phdr* program_header = GetProgramHeader(i);
    if (program_header->p_type != PT_LOAD) {
      continue;
    }
    Elf_Addr begin_vaddr = program_header->p_vaddr;
    if (begin_vaddr < min_vaddr) {
       min_vaddr = begin_vaddr;
    }
    Elf_Addr end_vaddr = program_header->p_vaddr + program_header->p_memsz;
    if (UNLIKELY(begin_vaddr > end_vaddr)) {
      std::ostringstream oss;
      oss << "Program header #" << i << " has overflow in p_vaddr+p_memsz: 0x" << std::hex
          << program_header->p_vaddr << "+0x" << program_header->p_memsz << "=0x" << end_vaddr
          << " in ELF file \"" << file_location_ << "\"";
      *error_msg = oss.str();
      *vaddr_begin = nullptr;
      *vaddr_size = static_cast<size_t>(-1);
      return false;
    }
    if (end_vaddr > max_vaddr) {
      max_vaddr = end_vaddr;
    }
  }
  min_vaddr = RoundDown(min_vaddr, kElfSegmentAlignment);
  max_vaddr = RoundUp(max_vaddr, kElfSegmentAlignment);
  CHECK_LT(min_vaddr, max_vaddr) << file_location_;
  // Check that the range fits into the runtime address space.
  if (UNLIKELY(max_vaddr - 1u > std::numeric_limits<size_t>::max())) {
    std::ostringstream oss;
    oss << "Loaded range is 0x" << std::hex << min_vaddr << "-0x" << max_vaddr
        << " but maximum size_t is 0x" << std::numeric_limits<size_t>::max() << " for ELF file \""
        << file_location_ << "\"";
    *error_msg = oss.str();
    *vaddr_begin = nullptr;
    *vaddr_size = static_cast<size_t>(-1);
    return false;
  }
  *vaddr_begin = reinterpret_cast<uint8_t*>(min_vaddr);
  *vaddr_size = dchecked_integral_cast<size_t>(max_vaddr - min_vaddr);
  return true;
}

static InstructionSet GetInstructionSetFromELF(uint16_t e_machine,
                                               [[maybe_unused]] uint32_t e_flags) {
  switch (e_machine) {
    case EM_ARM:
      return InstructionSet::kArm;
    case EM_AARCH64:
      return InstructionSet::kArm64;
    case EM_RISCV:
      return InstructionSet::kRiscv64;
    case EM_386:
      return InstructionSet::kX86;
    case EM_X86_64:
      return InstructionSet::kX86_64;
  }
  return InstructionSet::kNone;
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::Load(bool executable,
                                 bool low_4gb,
                                 /*inout*/ MemMap* reservation,
                                 /*out*/ std::string* error_msg) {
  if (executable) {
    InstructionSet elf_ISA = GetInstructionSetFromELF(GetHeader().e_machine, GetHeader().e_flags);
    if (elf_ISA != kRuntimeQuickCodeISA) {
      std::ostringstream oss;
      oss << "Expected ISA " << kRuntimeQuickCodeISA << " but found " << elf_ISA;
      *error_msg = oss.str();
      return false;
    }
  }

  bool reserved = false;
  for (Elf_Word i = 0; i < GetProgramHeaderNum(); i++) {
    Elf_Phdr* program_header = GetProgramHeader(i);

    // Record .dynamic header information for later use
    if (program_header->p_type == PT_DYNAMIC) {
      dynamic_program_header_ = program_header;
      continue;
    }

    // Not something to load, move on.
    if (program_header->p_type != PT_LOAD) {
      continue;
    }

    // Found something to load.

    // Before load the actual segments, reserve a contiguous chunk
    // of required size and address for all segments, but with no
    // permissions. We'll then carve that up with the proper
    // permissions as we load the actual segments. If p_vaddr is
    // non-zero, the segments require the specific address specified,
    // which either was specified in the file because we already set
    // base_address_ after the first zero segment).
    if (!reserved) {
      uint8_t* vaddr_begin;
      size_t vaddr_size;
      if (!GetLoadedAddressRange(&vaddr_begin, &vaddr_size, error_msg)) {
        DCHECK(!error_msg->empty());
        return false;
      }
      std::string reservation_name = "ElfFile reservation for " + file_location_;
      MemMap local_reservation =
          MemMap::MapAnonymous(reservation_name.c_str(),
                               (reservation != nullptr) ? reservation->Begin() : nullptr,
                               vaddr_size,
                               PROT_NONE,
                               low_4gb,
                               /*reuse=*/false,
                               reservation,
                               error_msg);
      if (!local_reservation.IsValid()) {
        *error_msg = StringPrintf("Failed to allocate %s: %s",
                                  reservation_name.c_str(),
                                  error_msg->c_str());
        return false;
      }
      reserved = true;

      // Base address is the difference of actual mapped location and the vaddr_begin.
      base_address_ = reinterpret_cast<uint8_t*>(
          static_cast<uintptr_t>(local_reservation.Begin() - vaddr_begin));
      // By adding the p_vaddr of a section/symbol to base_address_ we will always get the
      // dynamic memory address of where that object is actually mapped
      //
      // TODO: base_address_ needs to be calculated in ::Open, otherwise
      // FindDynamicSymbolAddress returns the wrong values until Load is called.
      segments_.push_back(std::move(local_reservation));
    }
    // empty segment, nothing to map
    if (program_header->p_memsz == 0) {
      continue;
    }
    uint8_t* p_vaddr = base_address_ + program_header->p_vaddr;
    int prot = 0;
    if (executable && ((program_header->p_flags & PF_X) != 0)) {
      prot |= PROT_EXEC;
    }
    if ((program_header->p_flags & PF_W) != 0) {
      prot |= PROT_WRITE;
    }
    if ((program_header->p_flags & PF_R) != 0) {
      prot |= PROT_READ;
    }
    if (program_header->p_filesz > program_header->p_memsz) {
      *error_msg = StringPrintf("Invalid p_filesz > p_memsz (%" PRIu64 " > %" PRIu64 "): %s",
                                static_cast<uint64_t>(program_header->p_filesz),
                                static_cast<uint64_t>(program_header->p_memsz),
                                file_location_.c_str());
      return false;
    }
    if (program_header->p_filesz < program_header->p_memsz &&
        !IsAligned<kElfSegmentAlignment>(program_header->p_filesz)) {
      *error_msg =
          StringPrintf("Unsupported unaligned p_filesz < p_memsz (%" PRIu64 " < %" PRIu64 "): %s",
                       static_cast<uint64_t>(program_header->p_filesz),
                       static_cast<uint64_t>(program_header->p_memsz),
                       file_location_.c_str());
      return false;
    }
    if (file_length_ < (program_header->p_offset + program_header->p_filesz)) {
      *error_msg = StringPrintf(
          "File size of %zd bytes not large enough to contain ELF segment "
          "%d of %" PRIu64 " bytes: '%s'",
          file_length_,
          i,
          static_cast<uint64_t>(program_header->p_offset + program_header->p_filesz),
          file_location_.c_str());
      return false;
    }
    if (program_header->p_filesz != 0u) {
      MemMap segment = MemMap::MapFileAtAddress(p_vaddr,
                                                program_header->p_filesz,
                                                prot,
                                                MAP_PRIVATE,
                                                file_->Fd(),
                                                start_ + program_header->p_offset,
                                                /*low_4gb=*/false,
                                                file_location_.c_str(),
                                                /*reuse=*/true,  // implies MAP_FIXED
                                                /*reservation=*/nullptr,
                                                error_msg);
      if (!segment.IsValid()) {
        *error_msg = StringPrintf("Failed to map ELF file segment %d from %s: %s",
                                  i,
                                  file_location_.c_str(),
                                  error_msg->c_str());
        return false;
      }
      if (segment.Begin() != p_vaddr) {
        *error_msg = StringPrintf(
            "Failed to map ELF file segment %d from %s at expected address %p, "
            "instead mapped to %p",
            i,
            file_location_.c_str(),
            p_vaddr,
            segment.Begin());
        return false;
      }
      segments_.push_back(std::move(segment));
    }
    if (program_header->p_filesz < program_header->p_memsz) {
      std::string name = StringPrintf("Zero-initialized segment %" PRIu64 " of ELF file %s",
                                      static_cast<uint64_t>(i),
                                      file_location_.c_str());
      MemMap segment = MemMap::MapAnonymous(name.c_str(),
                                            p_vaddr + program_header->p_filesz,
                                            program_header->p_memsz - program_header->p_filesz,
                                            prot,
                                            /*low_4gb=*/false,
                                            /*reuse=*/true,
                                            /*reservation=*/nullptr,
                                            error_msg);
      if (!segment.IsValid()) {
        *error_msg = StringPrintf("Failed to map zero-initialized ELF file segment %d from %s: %s",
                                  i,
                                  file_location_.c_str(),
                                  error_msg->c_str());
        return false;
      }
      if (segment.Begin() != p_vaddr) {
        *error_msg = StringPrintf(
            "Failed to map zero-initialized ELF file segment %d from %s "
            "at expected address %p, instead mapped to %p",
            i,
            file_location_.c_str(),
            p_vaddr,
            segment.Begin());
        return false;
      }
      segments_.push_back(std::move(segment));
    }
  }

  // Now that we are done loading, .dynamic should be in memory to find .dynstr, .dynsym, .hash
  uint8_t* dsptr = base_address_ + GetDynamicProgramHeader().p_vaddr;
  if ((dsptr < Begin() || dsptr >= End()) && !ValidPointer(dsptr)) {
    *error_msg =
        StringPrintf("dynamic section address invalid in ELF file %s", file_location_.c_str());
    return false;
  }
  dynamic_section_start_ = reinterpret_cast<Elf_Dyn*>(dsptr);

  for (Elf_Word i = 0; i < GetDynamicNum(); i++) {
    Elf_Dyn& elf_dyn = GetDynamic(i);
    uint8_t* d_ptr = base_address_ + elf_dyn.d_un.d_ptr;
    switch (elf_dyn.d_tag) {
      case DT_HASH: {
        if (!ValidPointer(d_ptr)) {
          *error_msg = StringPrintf("DT_HASH value %p does not refer to a loaded ELF segment of %s",
                                    d_ptr,
                                    file_location_.c_str());
          return false;
        }
        hash_section_start_ = reinterpret_cast<Elf_Word*>(d_ptr);
        break;
      }
      case DT_STRTAB: {
        if (!ValidPointer(d_ptr)) {
          *error_msg = StringPrintf("DT_HASH value %p does not refer to a loaded ELF segment of %s",
                                    d_ptr,
                                    file_location_.c_str());
          return false;
        }
        dynstr_section_start_ = reinterpret_cast<char*>(d_ptr);
        break;
      }
      case DT_SYMTAB: {
        if (!ValidPointer(d_ptr)) {
          *error_msg = StringPrintf("DT_HASH value %p does not refer to a loaded ELF segment of %s",
                                    d_ptr,
                                    file_location_.c_str());
          return false;
        }
        dynsym_section_start_ = reinterpret_cast<Elf_Sym*>(d_ptr);
        break;
      }
      case DT_NULL: {
        if (GetDynamicNum() != i+1) {
          *error_msg = StringPrintf(
              "DT_NULL found after %d .dynamic entries, "
              "expected %d as implied by size of PT_DYNAMIC segment in %s",
              i + 1,
              GetDynamicNum(),
              file_location_.c_str());
          return false;
        }
        break;
      }
    }
  }

  // Check for the existence of some sections.
  if (!CheckSectionsExist(error_msg)) {
    return false;
  }

  return true;
}

template <typename ElfTypes>
bool ElfFileImpl<ElfTypes>::ValidPointer(const uint8_t* start) const {
  for (const MemMap& segment : segments_) {
    if (segment.Begin() <= start && start < segment.End()) {
      return true;
    }
  }
  return false;
}

// Explicit instantiations
template class ElfFileImpl<ElfTypes32>;
template class ElfFileImpl<ElfTypes64>;

ElfFile* ElfFile::Open(File* file,
                       off_t start,
                       size_t file_length,
                       const std::string& file_location,
                       bool low_4gb,
                       /*out*/ std::string* error_msg) {
  if (file_length < EI_NIDENT) {
    *error_msg = StringPrintf("File %s is too short to be a valid ELF file", file_location.c_str());
    return nullptr;
  }
  MemMap map = MemMap::MapFile(EI_NIDENT,
                               PROT_READ,
                               MAP_PRIVATE,
                               file->Fd(),
                               start,
                               low_4gb,
                               file_location.c_str(),
                               error_msg);
  if (!map.IsValid() || map.Size() != EI_NIDENT) {
    return nullptr;
  }
  uint8_t* header = map.Begin();
  if (header[EI_CLASS] == ELFCLASS64) {
    return ElfFileImpl64::Open(file, start, file_length, file_location, low_4gb, error_msg);
  } else if (header[EI_CLASS] == ELFCLASS32) {
    return ElfFileImpl32::Open(file, start, file_length, file_location, low_4gb, error_msg);
  } else {
    *error_msg = StringPrintf("Failed to find expected EI_CLASS value %d or %d in %s, found %d",
                              ELFCLASS32,
                              ELFCLASS64,
                              file_location.c_str(),
                              header[EI_CLASS]);
    return nullptr;
  }
}

ElfFile* ElfFile::Open(File* file,
                       bool low_4gb,
                       /*out*/ std::string* error_msg) {
  int64_t file_length = file->GetLength();
  if (file_length < 0) {
    *error_msg =
        ART_FORMAT("Failed to get file length of '{}': {}", file->GetPath(), strerror(errno));
    return nullptr;
  }
  return Open(file, /*start=*/0, file_length, file->GetPath(), low_4gb, error_msg);
}

}  // namespace art
