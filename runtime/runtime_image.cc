/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "runtime_image.h"

#include <lz4.h>
#include <sstream>
#include <unistd.h>

#include "android-base/stringprintf.h"

#include "base/bit_utils.h"
#include "base/file_utils.h"
#include "base/length_prefixed_array.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_loader_utils.h"
#include "class_root-inl.h"
#include "gc/space/image_space.h"
#include "image.h"
#include "mirror/object-inl.h"
#include "mirror/object-refvisitor-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object_array.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art {

/**
 * Helper class to generate an app image at runtime.
 */
class RuntimeImageHelper {
 public:
  explicit RuntimeImageHelper(gc::Heap* heap) :
    boot_image_begin_(heap->GetBootImagesStartAddress()),
    boot_image_size_(heap->GetBootImagesSize()),
    image_begin_(boot_image_begin_ + boot_image_size_),
    // Note: image relocation considers the image header in the bitmap.
    object_section_size_(sizeof(ImageHeader)) {}

  bool Generate(std::string* error_msg) {
    if (!WriteImageRoot(error_msg)) {
      return false;
    }

    // Generate the sections information stored in the header.
    dchecked_vector<ImageSection> sections(ImageHeader::kSectionCount);
    CreateImageSections(sections);

    // Generate the bitmap section, stored page aligned after the sections data
    // and of size `object_section_size_` page aligned.
    size_t sections_end = sections[ImageHeader::kSectionMetadata].End();
    image_bitmap_ = gc::accounting::ContinuousSpaceBitmap::Create(
        "image bitmap",
        reinterpret_cast<uint8_t*>(image_begin_),
        RoundUp(object_section_size_, kPageSize));
    for (uint32_t offset : object_offsets_) {
      DCHECK(IsAligned<kObjectAlignment>(image_begin_ + sizeof(ImageHeader) + offset));
      image_bitmap_.Set(
          reinterpret_cast<mirror::Object*>(image_begin_ + sizeof(ImageHeader) + offset));
    }
    const size_t bitmap_bytes = image_bitmap_.Size();
    auto* bitmap_section = &sections[ImageHeader::kSectionImageBitmap];
    *bitmap_section = ImageSection(RoundUp(sections_end, kPageSize),
                                   RoundUp(bitmap_bytes, kPageSize));

    // Compute boot image checksum and boot image components, to be stored in
    // the header.
    gc::Heap* const heap = Runtime::Current()->GetHeap();
    uint32_t boot_image_components = 0u;
    uint32_t boot_image_checksums = 0u;
    const std::vector<gc::space::ImageSpace*>& image_spaces = heap->GetBootImageSpaces();
    for (size_t i = 0u, size = image_spaces.size(); i != size; ) {
      const ImageHeader& header = image_spaces[i]->GetImageHeader();
      boot_image_components += header.GetComponentCount();
      boot_image_checksums ^= header.GetImageChecksum();
      DCHECK_LE(header.GetImageSpaceCount(), size - i);
      i += header.GetImageSpaceCount();
    }

    header_ = ImageHeader(
        /* image_reservation_size= */ RoundUp(sections_end, kPageSize),
        /* component_count= */ 1,
        image_begin_,
        sections_end,
        sections.data(),
        /* image_roots= */ image_begin_ + sizeof(ImageHeader),
        /* oat_checksum= */ 0,
        /* oat_file_begin= */ 0,
        /* oat_data_begin= */ 0,
        /* oat_data_end= */ 0,
        /* oat_file_end= */ 0,
        heap->GetBootImagesStartAddress(),
        heap->GetBootImagesSize(),
        boot_image_components,
        boot_image_checksums,
        static_cast<uint32_t>(kRuntimePointerSize));

    // Data size includes everything except the bitmap.
    header_.data_size_ = sections_end;

    // Write image methods - needs to happen after creation of the header.
    WriteImageMethods();

    return true;
  }

  const std::vector<uint8_t>& GetData() const {
    return image_data_;
  }

  const ImageHeader& GetHeader() const {
    return header_;
  }

  const gc::accounting::ContinuousSpaceBitmap& GetImageBitmap() const {
    return image_bitmap_;
  }

  const std::string& GetDexLocation() const {
    return dex_location_;
  }

 private:
  bool IsInBootImage(const void* obj) const {
    return reinterpret_cast<uintptr_t>(obj) - boot_image_begin_ < boot_image_size_;
  }

  // Returns a pointer that can be stored in `image_data_`:
  // - The pointer itself for boot image objects,
  // - The offset in the image for all other objects.
  mirror::Object* GetOrComputeImageAddress(ObjPtr<mirror::Object> object)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (object == nullptr || IsInBootImage(object.Ptr())) {
      DCHECK(object == nullptr || Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(object));
      return object.Ptr();
    } else if (object->IsClassLoader()) {
      // DexCache and Class point to class loaders. For runtime-generated app
      // images, we don't encode the class loader. It will be set when the
      // runtime is loading the image.
      return nullptr;
    } else {
      uint32_t offset = CopyObject(object);
      return reinterpret_cast<mirror::Object*>(image_begin_ + sizeof(ImageHeader) + offset);
    }
  }

  void CreateImageSections(dchecked_vector<ImageSection>& sections) const {
    sections[ImageHeader::kSectionObjects] =
        ImageSection(0u, object_section_size_);
    sections[ImageHeader::kSectionArtFields] =
        ImageSection(sections[ImageHeader::kSectionObjects].End(), 0u);
    sections[ImageHeader::kSectionArtMethods] =
        ImageSection(sections[ImageHeader::kSectionArtFields].End(), 0u);
    sections[ImageHeader::kSectionImTables] =
        ImageSection(sections[ImageHeader::kSectionArtMethods].End(), 0u);
    sections[ImageHeader::kSectionIMTConflictTables] =
        ImageSection(sections[ImageHeader::kSectionImTables].End(), 0u);
    sections[ImageHeader::kSectionRuntimeMethods] =
        ImageSection(sections[ImageHeader::kSectionIMTConflictTables].End(), 0u);

    // Round up to the alignment the string table expects. See HashSet::WriteToMemory.
    size_t cur_pos = RoundUp(sections[ImageHeader::kSectionRuntimeMethods].End(), sizeof(uint64_t));

    sections[ImageHeader::kSectionInternedStrings] =
        ImageSection(cur_pos, 0u);

    // Obtain the new position and round it up to the appropriate alignment.
    cur_pos = RoundUp(sections[ImageHeader::kSectionInternedStrings].End(), sizeof(uint64_t));

    sections[ImageHeader::kSectionClassTable] =
        ImageSection(cur_pos, 0u);

    // Round up to the alignment of the offsets we are going to store.
    cur_pos = RoundUp(sections[ImageHeader::kSectionClassTable].End(), sizeof(uint32_t));

    sections[ImageHeader::kSectionStringReferenceOffsets] =
        ImageSection(cur_pos, 0u);

    // Round up to the alignment of the offsets we are going to store.
    cur_pos =
        RoundUp(sections[ImageHeader::kSectionStringReferenceOffsets].End(), sizeof(uint32_t));

    sections[ImageHeader::kSectionMetadata] =
        ImageSection(cur_pos, 0u);
  }

  bool WriteImageRoot(std::string* error_msg) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    ScopedObjectAccess soa(Thread::Current());
    VariableSizedHandleScope handles(soa.Self());

    Handle<mirror::Class> object_array_class = handles.NewHandle(
        GetClassRoot<mirror::ObjectArray<mirror::Object>>(class_linker));

    Handle<mirror::ObjectArray<mirror::Object>> image_roots = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), ImageHeader::kImageRootsMax));

    if (image_roots == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    // Find the dex files that will be used for generating the app image.
    dchecked_vector<Handle<mirror::DexCache>> dex_caches;
    FindDexCaches(soa.Self(), dex_caches, handles);

    if (dex_caches.size() == 0) {
      *error_msg = "Did not find dex caches to generate an app image";
      return false;
    }
    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    VdexFile* vdex_file = oat_dex_file->GetOatFile()->GetVdexFile();
    // The first entry in `dex_caches` contains the location of the primary APK.
    dex_location_ = oat_dex_file->GetDexFileLocation();

    size_t number_of_dex_files = vdex_file->GetNumberOfDexFiles();
    if (number_of_dex_files != dex_caches.size()) {
      // This means some dex files haven't been executed. For simplicity, just
      // register them and recollect dex caches.
      Handle<mirror::ClassLoader> loader = handles.NewHandle(dex_caches[0]->GetClassLoader());
      VisitClassLoaderDexFiles(soa.Self(), loader, [&](const art::DexFile* dex_file)
          REQUIRES_SHARED(Locks::mutator_lock_) {
        class_linker->RegisterDexFile(*dex_file, dex_caches[0]->GetClassLoader());
        return true;  // Continue with other dex files.
      });
      dex_caches.clear();
      FindDexCaches(soa.Self(), dex_caches, handles);
      if (number_of_dex_files != dex_caches.size()) {
        *error_msg = "Number of dex caches does not match number of dex files in the primary APK";
        return false;
      }
    }

    // Create and populate the checksums aray.
    Handle<mirror::IntArray> checksums_array = handles.NewHandle(
        mirror::IntArray::Alloc(soa.Self(), number_of_dex_files));

    if (checksums_array == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    const VdexFile::VdexChecksum* checksums = vdex_file->GetDexChecksumsArray();
    static_assert(sizeof(VdexFile::VdexChecksum) == sizeof(int32_t));
    for (uint32_t i = 0; i < number_of_dex_files; ++i) {
      checksums_array->Set(i, checksums[i]);
    }

    // Create and populate the dex caches aray.
    Handle<mirror::ObjectArray<mirror::Object>> dex_cache_array = handles.NewHandle(
        mirror::ObjectArray<mirror::Object>::Alloc(
            soa.Self(), object_array_class.Get(), dex_caches.size()));

    if (dex_cache_array == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
      *error_msg = "Out of memory when trying to generate a runtime app image";
      return false;
    }

    for (uint32_t i = 0; i < dex_caches.size(); ++i) {
      dex_cache_array->Set(i, dex_caches[i].Get());
    }

    image_roots->Set(ImageHeader::kDexCaches, dex_cache_array.Get());
    image_roots->Set(ImageHeader::kClassRoots, class_linker->GetClassRoots());
    image_roots->Set(ImageHeader::kAppImageDexChecksums, checksums_array.Get());

    // Now that we have created all objects needed for the `image_roots`, copy
    // it into the buffer. Note that this will recursively copy all objects
    // contained in `image_roots`. That's acceptable as we don't have cycles,
    // nor a deep graph.
    ScopedAssertNoThreadSuspension sants("Writing runtime app image");
    CopyObject(image_roots.Get());
    return true;
  }

  class FixupVisitor {
   public:
    FixupVisitor(RuntimeImageHelper* image, size_t copy_offset)
        : image_(image), copy_offset_(copy_offset) {}

    // We do not visit native roots. These are handled with other logic.
    void VisitRootIfNonNull(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED)
        const {
      LOG(FATAL) << "UNREACHABLE";
    }
    void VisitRoot(mirror::CompressedReference<mirror::Object>* root ATTRIBUTE_UNUSED) const {
      LOG(FATAL) << "UNREACHABLE";
    }

    void operator()(ObjPtr<mirror::Object> obj,
                    MemberOffset offset,
                    bool is_static ATTRIBUTE_UNUSED) const
        REQUIRES_SHARED(Locks::mutator_lock_) {
      ObjPtr<mirror::Object> ref = obj->GetFieldObject<mirror::Object>(offset);
      mirror::Object* address = image_->GetOrComputeImageAddress(ref.Ptr());
      mirror::Object* copy =
          reinterpret_cast<mirror::Object*>(image_->image_data_.data() + copy_offset_);
      copy->GetFieldObjectReferenceAddr<kVerifyNone>(offset)->Assign(address);
    }

    // java.lang.ref.Reference visitor.
    void operator()(ObjPtr<mirror::Class> klass ATTRIBUTE_UNUSED,
                    ObjPtr<mirror::Reference> ref) const
        REQUIRES_SHARED(Locks::mutator_lock_) {
      operator()(ref, mirror::Reference::ReferentOffset(), /* is_static */ false);
    }

   private:
    RuntimeImageHelper* image_;
    size_t copy_offset_;
  };

  // Copy `obj` in `image_data_` and relocate references. Returns the offset
  // within our buffer.
  uint32_t CopyObject(ObjPtr<mirror::Object> obj) REQUIRES_SHARED(Locks::mutator_lock_) {
    // Copy the object in `image_data_`.
    size_t object_size = obj->SizeOf();
    size_t offset = image_data_.size();
    DCHECK(IsAligned<kObjectAlignment>(offset));
    object_offsets_.push_back(offset);
    image_data_.resize(RoundUp(image_data_.size() + object_size, kObjectAlignment));
    memcpy(image_data_.data() + offset, obj.Ptr(), object_size);
    object_section_size_ += RoundUp(object_size, kObjectAlignment);

    // Fixup reference pointers.
    FixupVisitor visitor(this, offset);
    obj->VisitReferences</*kVisitNativeRoots=*/ false>(visitor, visitor);

    // For dex caches, clear pointers to data that will be set at runtime.
    mirror::Object* copy = reinterpret_cast<mirror::Object*>(image_data_.data() + offset);
    if (obj->IsDexCache()) {
      reinterpret_cast<mirror::DexCache*>(copy)->ResetNativeArrays();
      reinterpret_cast<mirror::DexCache*>(copy)->SetDexFile(nullptr);
    }
    return offset;
  }

  class CollectDexCacheVisitor : public DexCacheVisitor {
   public:
    explicit CollectDexCacheVisitor(VariableSizedHandleScope& handles) : handles_(handles) {}

    void Visit(ObjPtr<mirror::DexCache> dex_cache)
        REQUIRES_SHARED(Locks::dex_lock_, Locks::mutator_lock_) override {
      dex_caches_.push_back(handles_.NewHandle(dex_cache));
    }
    const std::vector<Handle<mirror::DexCache>>& GetDexCaches() const {
      return dex_caches_;
    }
   private:
    VariableSizedHandleScope& handles_;
    std::vector<Handle<mirror::DexCache>> dex_caches_;
  };

  // Find dex caches corresponding to the primary APK.
  void FindDexCaches(Thread* self,
                     dchecked_vector<Handle<mirror::DexCache>>& dex_caches,
                     VariableSizedHandleScope& handles)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(dex_caches.empty());
    // Collect all dex caches.
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    CollectDexCacheVisitor visitor(handles);
    {
      ReaderMutexLock mu(self, *Locks::dex_lock_);
      class_linker->VisitDexCaches(&visitor);
    }

    // Find the primary APK.
    AppInfo* app_info = Runtime::Current()->GetAppInfo();
    for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
      if (app_info->GetRegisteredCodeType(cache->GetDexFile()->GetLocation()) ==
              AppInfo::CodeType::kPrimaryApk) {
        dex_caches.push_back(handles.NewHandle(cache.Get()));
        break;
      }
    }

    if (dex_caches.empty()) {
      return;
    }

    const OatDexFile* oat_dex_file = dex_caches[0]->GetDexFile()->GetOatDexFile();
    if (oat_dex_file == nullptr) {
      // We need a .oat file for loading an app image;
      dex_caches.clear();
      return;
    }
    const OatFile* oat_file = oat_dex_file->GetOatFile();
    for (Handle<mirror::DexCache> cache : visitor.GetDexCaches()) {
      if (cache.Get() != dex_caches[0].Get()) {
        const OatDexFile* other_oat_dex_file = cache->GetDexFile()->GetOatDexFile();
        if (other_oat_dex_file != nullptr && other_oat_dex_file->GetOatFile() == oat_file) {
          dex_caches.push_back(handles.NewHandle(cache.Get()));
        }
      }
    }
  }

  static uint64_t PointerToUint64(void* ptr) {
    return reinterpret_cast64<uint64_t>(ptr);
  }

  void WriteImageMethods() {
    ScopedObjectAccess soa(Thread::Current());
    // We can just use plain runtime pointers.
    Runtime* runtime = Runtime::Current();
    header_.image_methods_[ImageHeader::kResolutionMethod] =
        PointerToUint64(runtime->GetResolutionMethod());
    header_.image_methods_[ImageHeader::kImtConflictMethod] =
        PointerToUint64(runtime->GetImtConflictMethod());
    header_.image_methods_[ImageHeader::kImtUnimplementedMethod] =
        PointerToUint64(runtime->GetImtUnimplementedMethod());
    header_.image_methods_[ImageHeader::kSaveAllCalleeSavesMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveAllCalleeSaves));
    header_.image_methods_[ImageHeader::kSaveRefsOnlyMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsOnly));
    header_.image_methods_[ImageHeader::kSaveRefsAndArgsMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveRefsAndArgs));
    header_.image_methods_[ImageHeader::kSaveEverythingMethod] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverything));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForClinit] =
        PointerToUint64(runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForClinit));
    header_.image_methods_[ImageHeader::kSaveEverythingMethodForSuspendCheck] =
        PointerToUint64(
            runtime->GetCalleeSaveMethod(CalleeSaveType::kSaveEverythingForSuspendCheck));
  }

  // Header for the image, created at the end once we know the size of all
  // sections.
  ImageHeader header_;

  // Contents of the image sections.
  std::vector<uint8_t> image_data_;

  // Bitmap of live objects in `image_data_`. Populated from `object_offsets_`
  // once we know `object_section_size`.
  gc::accounting::ContinuousSpaceBitmap image_bitmap_;

  // A list of offsets in `image_data_` where objects begin.
  std::vector<uint32_t> object_offsets_;

  // Cached values of boot image information.
  const uint32_t boot_image_begin_;
  const uint32_t boot_image_size_;

  // Where the image begins: just after the boot image.
  const uint32_t image_begin_;

  // Size of the `kSectionObjects` section.
  size_t object_section_size_;

  // The location of the primary APK / dex file.
  std::string dex_location_;
};

std::string RuntimeImage::GetRuntimeImagePath(const std::string& dex_location) {
  const std::string& data_dir = Runtime::Current()->GetProcessDataDirectory();

  std::string new_location = ReplaceFileExtension(
      dex_location, (kRuntimePointerSize == PointerSize::k32 ? "art32" : "art64"));

  if (data_dir.empty()) {
    // The data ditectory is empty for tests.
    return new_location;
  } else {
    std::replace(new_location.begin(), new_location.end(), '/', '@');
    return data_dir + "/" + new_location;
  }
}

bool RuntimeImage::WriteImageToDisk(std::string* error_msg) {
  gc::Heap* heap = Runtime::Current()->GetHeap();
  if (!heap->HasBootImageSpace()) {
    *error_msg = "Cannot generate an app image without a boot image";
    return false;
  }
  RuntimeImageHelper image(heap);
  if (!image.Generate(error_msg)) {
    return false;
  }

  const std::string path = GetRuntimeImagePath(image.GetDexLocation());
  // We first generate the app image in a temporary file, which we will then
  // move to `path`.
  const std::string temp_path = path + std::to_string(getpid());
  std::unique_ptr<File> out(OS::CreateEmptyFileWriteOnly(temp_path.c_str()));
  if (out == nullptr) {
    *error_msg = "Could not open " + temp_path + " for writing";
    return false;
  }

  // Write section infos. The header is written at the end in case we get killed.
  if (!out->Write(reinterpret_cast<const char*>(image.GetData().data()),
                  image.GetData().size(),
                  sizeof(ImageHeader))) {
    *error_msg = "Could not write image data to " + temp_path;
    out->Unlink();
    return false;
  }

  // Write bitmap at aligned offset.
  size_t aligned_offset = RoundUp(sizeof(ImageHeader) + image.GetData().size(), kPageSize);
  if (!out->Write(reinterpret_cast<const char*>(image.GetImageBitmap().Begin()),
                  image.GetImageBitmap().Size(),
                  aligned_offset)) {
    *error_msg = "Could not write image bitmap " + temp_path;
    out->Unlink();
    return false;
  }

  // Set the file length page aligned.
  size_t total_size = aligned_offset + RoundUp(image.GetImageBitmap().Size(), kPageSize);
  if (out->SetLength(total_size) != 0) {
    *error_msg = "Could not change size of image " + temp_path;
    out->Unlink();
    return false;
  }

  // Now write header.
  if (!out->Write(reinterpret_cast<const char*>(&image.GetHeader()), sizeof(ImageHeader), 0u)) {
    *error_msg = "Could not write image header to " + temp_path;
    out->Unlink();
    return false;
  }

  if (out->FlushClose() != 0) {
    *error_msg = "Could not flush and close " + temp_path;
    // Unlink directly: we cannot use `out` as we may have closed it.
    unlink(temp_path.c_str());
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) != 0) {
    *error_msg =
        "Failed to move runtime app image to " + path + ": " + std::string(strerror(errno));
    // Unlink directly: we cannot use `out` as we have closed it.
    unlink(temp_path.c_str());
    return false;
  }

  return true;
}

}  // namespace art
