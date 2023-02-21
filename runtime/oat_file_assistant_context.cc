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

#include "oat_file_assistant_context.h"

#include <memory>
#include <string>
#include <vector>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "arch/instruction_set.h"
#include "base/array_ref.h"
#include "base/logging.h"
#include "base/mem_map.h"
#include "class_linker.h"
#include "dex/art_dex_file_loader.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"

namespace art {

using ::android::base::StringPrintf;
using ::art::gc::space::ImageSpace;

OatFileAssistantContext::OatFileAssistantContext(
    std::unique_ptr<OatFileAssistantContext::RuntimeOptions> runtime_options)
    : runtime_options_(std::move(runtime_options)) {
  DCHECK_EQ(runtime_options_->boot_class_path.size(),
            runtime_options_->boot_class_path_locations.size());
  DCHECK_IMPLIES(
      runtime_options_->boot_class_path_fds != nullptr,
      runtime_options_->boot_class_path.size() == runtime_options_->boot_class_path_fds->size());
  // Opening dex files and boot images require MemMap.
  MemMap::Init();
}

OatFileAssistantContext::OatFileAssistantContext(Runtime* runtime)
    : OatFileAssistantContext(std::make_unique<OatFileAssistantContext::RuntimeOptions>(
          OatFileAssistantContext::RuntimeOptions{
              .image_locations = runtime->GetImageLocations(),
              .boot_class_path = runtime->GetBootClassPath(),
              .boot_class_path_locations = runtime->GetBootClassPathLocations(),
              .boot_class_path_fds = !runtime->GetBootClassPathFds().empty() ?
                                         &runtime->GetBootClassPathFds() :
                                         nullptr,
              .deny_art_apex_data_files = runtime->DenyArtApexDataFiles(),
          })) {
  // Fetch boot image info from the runtime.
  std::vector<BootImageInfo>& boot_image_info_list = boot_image_info_list_by_isa_[kRuntimeISA];
  for (const ImageSpace* image_space : runtime->GetHeap()->GetBootImageSpaces()) {
    // We only need the checksum of the first component for each boot image. They are in image
    // spaces that have a non-zero component count.
    if (image_space->GetComponentCount() > 0) {
      BootImageInfo& boot_image_info = boot_image_info_list.emplace_back();
      boot_image_info.component_count = image_space->GetComponentCount();
      ImageSpace::AppendImageChecksum(image_space->GetComponentCount(),
                                      image_space->GetImageHeader().GetImageChecksum(),
                                      &boot_image_info.checksum);
    }
  }

  // Fetch BCP checksums from the runtime.
  size_t bcp_index = 0;
  std::vector<std::string>* current_bcp_checksums = nullptr;
  for (const DexFile* dex_file : runtime->GetClassLinker()->GetBootClassPath()) {
    if (!DexFileLoader::IsMultiDexLocation(dex_file->GetLocation().c_str())) {
      DCHECK_LT(bcp_index, runtime_options_->boot_class_path.size());
      current_bcp_checksums = &bcp_checksums_by_index_[bcp_index++];
    }
    DCHECK_NE(current_bcp_checksums, nullptr);
    current_bcp_checksums->push_back(StringPrintf("/%08x", dex_file->GetLocationChecksum()));
  }
  DCHECK_EQ(bcp_index, runtime_options_->boot_class_path.size());

  // Fetch APEX versions from the runtime.
  apex_versions_ = runtime->GetApexVersions();
}

const OatFileAssistantContext::RuntimeOptions& OatFileAssistantContext::GetRuntimeOptions() const {
  return *runtime_options_;
}

bool OatFileAssistantContext::FetchAll(std::string* error_msg) {
  std::vector<InstructionSet> isas = GetSupportedInstructionSets(error_msg);
  if (isas.empty()) {
    return false;
  }
  for (InstructionSet isa : isas) {
    GetBootImageInfoList(isa);
  }
  for (size_t i = 0; i < runtime_options_->boot_class_path.size(); i++) {
    if (GetBcpChecksums(i, error_msg) == nullptr) {
      return false;
    }
  }
  GetApexVersions();
  return true;
}

const std::vector<OatFileAssistantContext::BootImageInfo>&
OatFileAssistantContext::GetBootImageInfoList(InstructionSet isa) {
  if (auto it = boot_image_info_list_by_isa_.find(isa); it != boot_image_info_list_by_isa_.end()) {
    return it->second;
  }

  ImageSpace::BootImageLayout layout(
      ArrayRef<const std::string>(runtime_options_->image_locations),
      ArrayRef<const std::string>(runtime_options_->boot_class_path),
      ArrayRef<const std::string>(runtime_options_->boot_class_path_locations),
      runtime_options_->boot_class_path_fds != nullptr ?
          ArrayRef<const int>(*runtime_options_->boot_class_path_fds) :
          ArrayRef<const int>(),
      /*boot_class_path_image_fds=*/ArrayRef<const int>(),
      /*boot_class_path_vdex_fds=*/ArrayRef<const int>(),
      /*boot_class_path_oat_fds=*/ArrayRef<const int>(),
      &GetApexVersions());

  std::string error_msg;
  if (!layout.LoadFromSystem(isa, /*allow_in_memory_compilation=*/false, &error_msg)) {
    // At this point, `layout` contains nothing.
    VLOG(oat) << "Some error occurred when loading boot images for oat file validation: "
              << error_msg;
    // Create an empty entry so that we don't have to retry when the function is called again.
    return boot_image_info_list_by_isa_[isa];
  }

  std::vector<BootImageInfo>& boot_image_info_list = boot_image_info_list_by_isa_[isa];
  for (const ImageSpace::BootImageLayout::ImageChunk& chunk : layout.GetChunks()) {
    BootImageInfo& boot_image_info = boot_image_info_list.emplace_back();
    boot_image_info.component_count = chunk.component_count;
    ImageSpace::AppendImageChecksum(
        chunk.component_count, chunk.checksum, &boot_image_info.checksum);
  }
  return boot_image_info_list;
}

const std::vector<std::string>* OatFileAssistantContext::GetBcpChecksums(size_t bcp_index,
                                                                         std::string* error_msg) {
  DCHECK_LT(bcp_index, runtime_options_->boot_class_path.size());

  if (auto it = bcp_checksums_by_index_.find(bcp_index); it != bcp_checksums_by_index_.end()) {
    return &it->second;
  }

  std::vector<uint32_t> checksums;
  std::vector<std::string> dex_locations;
  if (!ArtDexFileLoader::GetMultiDexChecksums(
          runtime_options_->boot_class_path[bcp_index].c_str(),
          &checksums,
          &dex_locations,
          error_msg,
          runtime_options_->boot_class_path_fds != nullptr ?
              (*runtime_options_->boot_class_path_fds)[bcp_index] :
              -1)) {
    return nullptr;
  }

  DCHECK(!checksums.empty());
  std::vector<std::string>& bcp_checksums = bcp_checksums_by_index_[bcp_index];
  for (uint32_t checksum : checksums) {
    bcp_checksums.push_back(StringPrintf("/%08x", checksum));
  }
  return &bcp_checksums;
}

const std::string& OatFileAssistantContext::GetApexVersions() {
  if (apex_versions_.has_value()) {
    return apex_versions_.value();
  }

  apex_versions_ = Runtime::GetApexVersions(
      ArrayRef<const std::string>(runtime_options_->boot_class_path_locations));
  return apex_versions_.value();
}

}  // namespace art
