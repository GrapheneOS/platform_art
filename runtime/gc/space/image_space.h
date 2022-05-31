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

#ifndef ART_RUNTIME_GC_SPACE_IMAGE_SPACE_H_
#define ART_RUNTIME_GC_SPACE_IMAGE_SPACE_H_

#include "gc/accounting/space_bitmap.h"
#include "image.h"
#include "space.h"

namespace art {

template <typename T> class ArrayRef;
class DexFile;
enum class InstructionSet;
class OatFile;

namespace gc {
namespace space {

// An image space is a space backed with a memory mapped image.
class ImageSpace : public MemMapSpace {
 public:
  SpaceType GetType() const override {
    return kSpaceTypeImageSpace;
  }

  // The separator for boot image location components.
  static constexpr char kComponentSeparator = ':';
  // The separator for profile filename.
  static constexpr char kProfileSeparator = '!';

  // Load boot image spaces for specified boot class path, image location, instruction set, etc.
  //
  // On successful return, the loaded spaces are added to boot_image_spaces (which must be
  // empty on entry) and `extra_reservation` is set to the requested reservation located
  // after the end of the last loaded oat file.
  //
  // IMAGE LOCATION
  //
  // The "image location" is a colon-separated list that specifies one or more
  // components by name and may also specify search paths for extensions
  // corresponding to the remaining boot class path (BCP) extensions.
  //
  // The primary boot image can be specified as one of
  //     <path>/<base-name>
  //     <base-name>
  // and the path of the first BCP component is used for the second form.
  // The specification may be followed by one or more profile specifications, where each profile
  // specification is one of
  //     !<profile-path>/<profile-name>
  //     !<profile-name>
  // and the profiles will be used to compile the primary boot image when loading the boot image if
  // the on-disk version is not acceptable (either not present or fails validation, presumably
  // because it's out of date). The primary boot image is compiled with no dependency.
  //
  // Named extension specifications must correspond to an expansion of the
  // <base-name> with a BCP component (for example boot.art with the BCP
  // component name <jar-path>/framework.jar expands to boot-framework.art).
  // They can be similarly specified as one of
  //     <ext-path>/<ext-name>
  //     <ext-name>
  // and must be listed in the order of their corresponding BCP components.
  // Similarly, the specification may be followed by one or more profile specifications, where each
  // profile specification is one of
  //     !<profile-path>/<profile-name>
  //     !<profile-name>
  // and the profiles will be used to compile the extension when loading the boot image if the
  // on-disk version is not acceptable (either not present or fails validation, presumably because
  // it's out of date). The primary boot image (i.e., the first element in "image location") is the
  // dependency that each extension is compiled against.
  //
  // Search paths for remaining extensions can be specified after named
  // components as one of
  //     <search-path>/*
  //     *
  // where the second form means that the path of a particular BCP component
  // should be used to search for that component's boot image extension.
  //
  // The actual filename shall be derived from the specified locations using
  // `GetSystemImageFilename()`.
  //
  // Example image locations:
  //     /system/framework/boot.art
  //         - only primary boot image with full path.
  //     /data/misc/apexdata/com.android.art/dalvik-cache/boot.art!/apex/com.android.art/etc/boot-image.prof!/system/etc/boot-image.prof
  //         - only primary boot image with full path; if the primary boot image is not found or
  //           broken, compile it in memory using two specified profile files at the exact paths.
  //     boot.art:boot-framework.art
  //         - primary and one extension, use BCP component paths.
  //     /apex/com.android.art/boot.art:*
  //         - primary with exact location, search for the rest based on BCP
  //           component paths.
  //     boot.art:/system/framework/*
  //         - primary based on BCP component path, search for extensions in
  //           /system/framework.
  //     /apex/com.android.art/boot.art:/system/framework/*:*
  //         - primary with exact location, search for extensions first in
  //           /system/framework, then in the corresponding BCP component path.
  //     /apex/com.android.art/boot.art:*:/system/framework/*
  //         - primary with exact location, search for extensions first in the
  //           corresponding BCP component path and then in /system/framework.
  //     /apex/com.android.art/boot.art:*:boot-framework.jar
  //         - invalid, named components may not follow search paths.
  //     boot.art:boot-framework.jar!/system/framework/framework.prof
  //         - primary and one extension, use BCP component paths; if extension
  //           is not found or broken compile it in memory using the specified
  //           profile file from the exact path.
  //     boot.art:boot-framework.jar:conscrypt.jar!conscrypt.prof
  //         - primary and two extensions, use BCP component paths; only the
  //           second extension has a profile file and can be compiled in memory
  //           when it is not found or broken, using the specified profile file
  //           in the BCP component path and it is compiled against the primary
  //           and first extension and only if the first extension is OK.
  //     boot.art:boot-framework.jar!framework.prof:conscrypt.jar!conscrypt.prof
  //         - primary and two extensions, use BCP component paths; if any
  //           extension is not found or broken compile it in memory using
  //           the specified profile file in the BCP component path, each
  //           extension is compiled only against the primary boot image.
  static bool LoadBootImage(
      const std::vector<std::string>& boot_class_path,
      const std::vector<std::string>& boot_class_path_locations,
      const std::vector<int>& boot_class_path_fds,
      const std::vector<int>& boot_class_path_image_fds,
      const std::vector<int>& boot_class_path_vdex_fds,
      const std::vector<int>& boot_class_path_oat_fds,
      const std::vector<std::string>& image_locations,
      const InstructionSet image_isa,
      bool relocate,
      bool executable,
      size_t extra_reservation_size,
      /*out*/std::vector<std::unique_ptr<ImageSpace>>* boot_image_spaces,
      /*out*/MemMap* extra_reservation) REQUIRES_SHARED(Locks::mutator_lock_);

  // Try to open an existing app image space for an oat file,
  // using the boot image spaces from the current Runtime.
  static std::unique_ptr<ImageSpace> CreateFromAppImage(const char* image,
                                                        const OatFile* oat_file,
                                                        std::string* error_msg)
      REQUIRES_SHARED(Locks::mutator_lock_);
  // Try to open an existing app image space for an the oat file and given boot image spaces.
  static std::unique_ptr<ImageSpace> CreateFromAppImage(
      const char* image,
      const OatFile* oat_file,
      ArrayRef<ImageSpace* const> boot_image_spaces,
      std::string* error_msg) REQUIRES_SHARED(Locks::mutator_lock_);

  // Checks whether we have a primary boot image on the disk.
  static bool IsBootClassPathOnDisk(InstructionSet image_isa);

  // Give access to the OatFile.
  const OatFile* GetOatFile() const;

  // Releases the OatFile from the ImageSpace so it can be transfer to
  // the caller, presumably the OatFileManager.
  std::unique_ptr<const OatFile> ReleaseOatFile();

  void VerifyImageAllocations()
      REQUIRES_SHARED(Locks::mutator_lock_);

  const ImageHeader& GetImageHeader() const {
    return *reinterpret_cast<ImageHeader*>(Begin());
  }

  // Actual filename where image was loaded from.
  // For example: /system/framework/arm64/boot.art
  const std::string GetImageFilename() const {
    return GetName();
  }

  // Symbolic location for image.
  // For example: /system/framework/boot.art
  const std::string GetImageLocation() const {
    return image_location_;
  }

  const std::vector<std::string>& GetProfileFiles() const { return profile_files_; }

  accounting::ContinuousSpaceBitmap* GetLiveBitmap() override {
    return &live_bitmap_;
  }

  accounting::ContinuousSpaceBitmap* GetMarkBitmap() override {
    // ImageSpaces have the same bitmap for both live and marked. This helps reduce the number of
    // special cases to test against.
    return &live_bitmap_;
  }

  // Compute the number of components in the image (contributing jar files).
  size_t GetComponentCount() const {
    return GetImageHeader().GetComponentCount();
  }

  void Dump(std::ostream& os) const override;

  // Sweeping image spaces is a NOP.
  void Sweep(bool /* swap_bitmaps */, size_t* /* freed_objects */, size_t* /* freed_bytes */) {
  }

  bool CanMoveObjects() const override {
    return false;
  }

  // Returns the filename of the image corresponding to
  // requested image_location, or the filename where a new image
  // should be written if one doesn't exist. Looks for a generated
  // image in the specified location.
  //
  // Returns true if an image was found, false otherwise.
  static bool FindImageFilename(const char* image_location,
                                InstructionSet image_isa,
                                std::string* system_location,
                                bool* has_system);

  // The leading character in an image checksum part of boot class path checksums.
  static constexpr char kImageChecksumPrefix = 'i';
  // The leading character in a dex file checksum part of boot class path checksums.
  static constexpr char kDexFileChecksumPrefix = 'd';

  // Returns the checksums for the boot image, extensions and extra boot class path dex files,
  // based on the image spaces and boot class path dex files loaded in memory.
  // The `image_spaces` must correspond to the head of the `boot_class_path`.
  static std::string GetBootClassPathChecksums(ArrayRef<ImageSpace* const> image_spaces,
                                               ArrayRef<const DexFile* const> boot_class_path);

  // Returns the total number of components (jar files) associated with the image spaces.
  static size_t GetNumberOfComponents(ArrayRef<gc::space::ImageSpace* const> image_spaces);

  // Returns whether the checksums are valid for the given boot class path,
  // image location and ISA (may differ from the ISA of an initialized Runtime).
  // The boot image and dex files do not need to be loaded in memory.
  static bool VerifyBootClassPathChecksums(std::string_view oat_checksums,
                                           std::string_view oat_boot_class_path,
                                           ArrayRef<const std::string> image_locations,
                                           ArrayRef<const std::string> boot_class_path_locations,
                                           ArrayRef<const std::string> boot_class_path,
                                           ArrayRef<const int> boot_class_path_fds,
                                           InstructionSet image_isa,
                                           const std::string& apex_versions,
                                           /*out*/std::string* error_msg);

  // Returns whether all the boot images listed in `image_locations` are valid.
  static bool VerifyBootImages(ArrayRef<const std::string> image_locations,
                               ArrayRef<const std::string> boot_class_path_locations,
                               ArrayRef<const std::string> boot_class_path,
                               ArrayRef<const int> boot_class_path_fds,
                               InstructionSet image_isa,
                               const std::string& apex_versions,
                               /*out*/std::string* error_msg);

  // Returns whether the oat checksums and boot class path description are valid
  // for the given boot image spaces and boot class path. Used for boot image extensions.
  static bool VerifyBootClassPathChecksums(
      std::string_view oat_checksums,
      std::string_view oat_boot_class_path,
      ArrayRef<const std::unique_ptr<ImageSpace>> image_spaces,
      ArrayRef<const std::string> boot_class_path_locations,
      ArrayRef<const std::string> boot_class_path,
      /*out*/std::string* error_msg);

  // Expand a single image location to multi-image locations based on the dex locations.
  static std::vector<std::string> ExpandMultiImageLocations(
      ArrayRef<const std::string> dex_locations,
      const std::string& image_location,
      bool boot_image_extension = false);

  // Returns true if the APEX versions in the OAT file match the given APEX versions.
  static bool ValidateApexVersions(const OatFile& oat_file,
                                   const std::string& apex_versions,
                                   std::string* error_msg);

  // Returns true if the dex checksums in the given oat file match the
  // checksums of the original dex files on disk. This is intended to be used
  // to validate the boot image oat file, which may contain dex entries from
  // multiple different (possibly multidex) dex files on disk. Prefer the
  // OatFileAssistant for validating regular app oat files because the
  // OatFileAssistant caches dex checksums that are reused to check both the
  // oat and odex file.
  //
  // This function is exposed for testing purposes.
  //
  // Calling this function requires an active runtime.
  static bool ValidateOatFile(const OatFile& oat_file, std::string* error_msg);

  // Same as above, but allows to use `dex_filenames` and `dex_fds` to find the dex files instead of
  // using the dex filenames in the header of the oat file, and also takes `apex_versions` from the
  // input. This overload is useful when the actual dex filenames are different from what's in the
  // header (e.g., when we run dex2oat on host), when the runtime can only access files through FDs
  // (e.g., when we run dex2oat on target in a restricted SELinux domain), or when there is no
  // active runtime.
  //
  // Calling this function does not require an active runtime.
  static bool ValidateOatFile(const OatFile& oat_file,
                              std::string* error_msg,
                              ArrayRef<const std::string> dex_filenames,
                              ArrayRef<const int> dex_fds,
                              const std::string& apex_versions);

  // Return the end of the image which includes non-heap objects such as ArtMethods and ArtFields.
  uint8_t* GetImageEnd() const {
    return Begin() + GetImageHeader().GetImageSize();
  }

  void DumpSections(std::ostream& os) const;

  // De-initialize the image-space by undoing the effects in Init().
  virtual ~ImageSpace();

  void ReleaseMetadata() REQUIRES_SHARED(Locks::mutator_lock_);

 protected:
  // Tries to initialize an ImageSpace from the given image path, returning null on error.
  //
  // If validate_oat_file is false (for /system), do not verify that image's OatFile is up-to-date
  // relative to its DexFile inputs. Otherwise, validate `oat_file` and abandon it if the validation
  // fails. If the oat_file is null, it uses the oat file from the image.
  static std::unique_ptr<ImageSpace> Init(const char* image_filename,
                                          const char* image_location,
                                          bool validate_oat_file,
                                          const OatFile* oat_file,
                                          std::string* error_msg)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static Atomic<uint32_t> bitmap_index_;

  accounting::ContinuousSpaceBitmap live_bitmap_;

  ImageSpace(const std::string& name,
             const char* image_location,
             const std::vector<std::string>& profile_files,
             MemMap&& mem_map,
             accounting::ContinuousSpaceBitmap&& live_bitmap,
             uint8_t* end);

  // The OatFile associated with the image during early startup to
  // reserve space contiguous to the image. It is later released to
  // the ClassLinker during it's initialization.
  std::unique_ptr<OatFile> oat_file_;

  // There are times when we need to find the boot image oat file. As
  // we release ownership during startup, keep a non-owned reference.
  const OatFile* oat_file_non_owned_;

  const std::string image_location_;
  const std::vector<std::string> profile_files_;

  friend class Space;

 private:
  class BootImageLayout;
  class BootImageLoader;
  template <typename ReferenceVisitor>
  class ClassTableVisitor;
  class RemapInternedStringsVisitor;
  class Loader;
  template <typename PatchObjectVisitor>
  class PatchArtFieldVisitor;
  template <PointerSize kPointerSize, typename PatchObjectVisitor, typename PatchCodeVisitor>
  class PatchArtMethodVisitor;
  template <PointerSize kPointerSize, typename HeapVisitor, typename NativeVisitor>
  class PatchObjectVisitor;

  DISALLOW_COPY_AND_ASSIGN(ImageSpace);
};

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_IMAGE_SPACE_H_
