/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "oat/jni_stub_hash_map-inl.h"

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "arch/instruction_set.h"
#include "art_method.h"
#include "base/array_ref.h"
#include "base/locks.h"
#include "base/utils.h"
#include "class_linker.h"
#include "common_compiler_test.h"
#include "compiler.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "handle.h"
#include "handle_scope.h"
#include "handle_scope-inl.h"
#include "jni.h"
#include "mirror/class.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"
#include "oat/image-inl.h"
#include "oat/oat_quick_method_header.h"
#include "obj_ptr.h"
#include "runtime.h"
#include "scoped_thread_state_change.h"

namespace art HIDDEN {

// Teach gtest how to print the ArrayRef<const uint8_t>. The customized output is easier used
// for converting to assembly instructions.
static void PrintTo(const ArrayRef<const uint8_t>& array, std::ostream* os) {
  *os << "[[[";
  for (const uint8_t& element : array) {
    *os << " ";
    *os << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(element);
  }
  *os << " ]]]";
}

class JniStubHashMapTest : public CommonCompilerTest {
 protected:
  JniStubHashMapTest()
      : jni_stub_hash_map_(JniStubKeyHash(kRuntimeISA), JniStubKeyEquals(kRuntimeISA)) {
    if (kRuntimeISA == InstructionSet::kArm64 || kRuntimeISA == InstructionSet::kX86_64) {
      // Only arm64 and x86_64 use strict check.
      strict_check_ = true;
    } else {
      // Other archs use loose check.
      strict_check_ = false;
    }
  }

  void SetStrictCheck(bool value) {
    strict_check_ = value;
  }

  void SetUpForTest() {
    ScopedObjectAccess soa(Thread::Current());
    jobject jclass_loader = LoadDex("MyClassNatives");
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(jclass_loader)));
    pointer_size_ = class_linker_->GetImagePointerSize();
    ObjPtr<mirror::Class> klass = FindClass("LMyClassNatives;", class_loader);
    ASSERT_TRUE(klass != nullptr);
    jklass_ = soa.AddLocalReference<jclass>(klass);
  }

  void SetBaseMethod(std::string_view base_method_name, std::string_view base_method_sig) {
    jni_stub_hash_map_.clear();
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Class> klass = soa.Decode<mirror::Class>(jklass_);
    base_method_ = klass->FindClassMethod(base_method_name, base_method_sig, pointer_size_);
    ASSERT_TRUE(base_method_ != nullptr);
    ASSERT_TRUE(base_method_->IsNative());

    base_method_code_ = JniCompileCode(base_method_);

    jni_stub_hash_map_.insert(std::make_pair(JniStubKey(base_method_), base_method_));
  }

  void CompareMethod(std::string_view cmp_method_name, std::string_view cmp_method_sig) {
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Class> klass = soa.Decode<mirror::Class>(jklass_);
    ArtMethod* cmp_method = klass->FindClassMethod(cmp_method_name, cmp_method_sig, pointer_size_);
    ASSERT_TRUE(cmp_method != nullptr);
    ASSERT_TRUE(cmp_method->IsNative());

    std::vector<uint8_t> cmp_method_code = JniCompileCode(cmp_method);

    auto it = jni_stub_hash_map_.find(JniStubKey(cmp_method));
    if (it != jni_stub_hash_map_.end()) {
      ASSERT_EQ(base_method_code_, cmp_method_code)
          << "base method: " << base_method_->PrettyMethod() << ", compared method: "
          << cmp_method->PrettyMethod();
    } else if (strict_check_){
      // If the compared method maps to a different entry, then its compiled JNI stub should be
      // also different from the base one.
      ASSERT_NE(base_method_code_, cmp_method_code)
          << "base method: " << base_method_->PrettyMethod() << ", compared method: "
          << cmp_method->PrettyMethod();
    }
  }

  bool strict_check_;
  JniStubHashMap<ArtMethod*> jni_stub_hash_map_;
  PointerSize pointer_size_;
  jclass jklass_;
  ArtMethod* base_method_;
  std::vector<uint8_t> base_method_code_;
};

class JniStubHashMapBootImageTest : public CommonRuntimeTest {
 protected:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    std::string runtime_args_image;
    runtime_args_image = android::base::StringPrintf("-Ximage:%s", GetCoreArtLocation().c_str());
    options->push_back(std::make_pair(runtime_args_image, nullptr));
  }
};

TEST_F(JniStubHashMapTest, ReturnType) {
  SetUpForTest();
  SetBaseMethod("fooI", "(I)I");
  CompareMethod("fooI_V", "(I)V");
  CompareMethod("fooI_B", "(I)B");
  CompareMethod("fooI_C", "(I)C");
  CompareMethod("fooI_S", "(I)S");
  CompareMethod("fooI_Z", "(I)Z");
  CompareMethod("fooI_J", "(I)J");
  CompareMethod("fooI_F", "(I)F");
  CompareMethod("fooI_D", "(I)D");
  CompareMethod("fooI_L", "(I)Ljava/lang/Object;");
}

TEST_F(JniStubHashMapTest, ArgType) {
  SetUpForTest();
  SetBaseMethod("sfooI", "(I)I");
  CompareMethod("sfooB", "(B)I");
  CompareMethod("sfooC", "(C)I");
  CompareMethod("sfooS", "(S)I");
  CompareMethod("sfooZ", "(Z)I");
  CompareMethod("sfooL", "(Ljava/lang/Object;)I");
}

TEST_F(JniStubHashMapTest, FloatingPointArg) {
  SetUpForTest();
  SetBaseMethod("sfooI", "(I)I");
  CompareMethod("sfoo7FI", "(FFFFFFFI)I");
  CompareMethod("sfoo3F5DI", "(FFFDDDDDI)I");
  CompareMethod("sfoo3F6DI", "(FFFDDDDDDI)I");
}

TEST_F(JniStubHashMapTest, IntegralArg) {
  SetUpForTest();
  SetBaseMethod("fooL", "(Ljava/lang/Object;)I");
  CompareMethod("fooL4I", "(Ljava/lang/Object;IIII)I");
  CompareMethod("fooL5I", "(Ljava/lang/Object;IIIII)I");
  CompareMethod("fooL3IJC", "(Ljava/lang/Object;IIIJC)I");
  CompareMethod("fooL3IJCS", "(Ljava/lang/Object;IIIJCS)I");
}

TEST_F(JniStubHashMapTest, StackOffsetMatters) {
  SetUpForTest();
  SetBaseMethod("foo7FDF", "(FFFFFFFDF)I");
  CompareMethod("foo9F", "(FFFFFFFFF)I");
  CompareMethod("foo7FIFF", "(FFFFFFFIFF)I");
  SetBaseMethod("foo5IJI", "(IIIIIJI)I");
  CompareMethod("foo7I", "(IIIIIII)I");
  CompareMethod("foo5IFII", "(IIIIIFII)I");
  SetBaseMethod("fooFDL", "(FDLjava/lang/Object;)I");
  CompareMethod("foo2FL", "(FFLjava/lang/Object;)I");
  CompareMethod("foo3FL", "(FFFLjava/lang/Object;)I");
  CompareMethod("foo2FIL", "(FFILjava/lang/Object;)I");
}

TEST_F(JniStubHashMapTest, IntLikeRegsMatters) {
  SetUpForTest();
  SetBaseMethod("fooICFL", "(ICFLjava/lang/Object;)I");
  CompareMethod("foo2IFL", "(IIFLjava/lang/Object;)I");
  CompareMethod("fooICIL", "(ICILjava/lang/Object;)I");
}

TEST_F(JniStubHashMapTest, FastNative) {
  SetUpForTest();
  SetBaseMethod("fooI_Fast", "(I)I");
  CompareMethod("fooI_Z_Fast", "(I)Z");
  CompareMethod("fooI_J_Fast", "(I)J");
  SetBaseMethod("fooICFL_Fast", "(ICFLjava/lang/Object;)I");
  CompareMethod("foo2IFL_Fast", "(IIFLjava/lang/Object;)I");
  CompareMethod("fooICIL_Fast", "(ICILjava/lang/Object;)I");
  SetBaseMethod("fooFDL_Fast", "(FDLjava/lang/Object;)I");
  CompareMethod("foo2FL_Fast", "(FFLjava/lang/Object;)I");
  CompareMethod("foo3FL_Fast", "(FFFLjava/lang/Object;)I");
  CompareMethod("foo2FIL_Fast", "(FFILjava/lang/Object;)I");
  SetBaseMethod("foo7F_Fast", "(FFFFFFF)I");
  CompareMethod("foo3F5D_Fast", "(FFFDDDDD)I");
  CompareMethod("foo3F6D_Fast", "(FFFDDDDDD)I");
  SetBaseMethod("fooL5I_Fast", "(Ljava/lang/Object;IIIII)I");
  CompareMethod("fooL3IJC_Fast", "(Ljava/lang/Object;IIIJC)I");
  CompareMethod("fooL3IJCS_Fast", "(Ljava/lang/Object;IIIJCS)I");
}

TEST_F(JniStubHashMapTest, CriticalNative) {
  SetUpForTest();
  if (kRuntimeISA == InstructionSet::kX86_64) {
    // In x86_64, the return type seems be ignored in critical function.
    SetStrictCheck(false);
  }
  SetBaseMethod("returnInt_Critical", "()I");
  CompareMethod("returnDouble_Critical", "()D");
  CompareMethod("returnLong_Critical", "()J");
  SetBaseMethod("foo7F_Critical", "(FFFFFFF)I");
  CompareMethod("foo3F5D_Critical", "(FFFDDDDD)I");
  CompareMethod("foo3F6D_Critical", "(FFFDDDDDD)I");
}

TEST_F(JniStubHashMapBootImageTest, BootImageSelfCheck) {
  std::vector<gc::space::ImageSpace*> image_spaces =
      Runtime::Current()->GetHeap()->GetBootImageSpaces();
  ASSERT_TRUE(!image_spaces.empty());
  for (gc::space::ImageSpace* space : image_spaces) {
    const ImageHeader& header = space->GetImageHeader();
    PointerSize ptr_size = class_linker_->GetImagePointerSize();
    auto visitor = [&](ArtMethod& method) REQUIRES_SHARED(Locks::mutator_lock_) {
      if (method.IsNative() && !method.IsIntrinsic()) {
        const void* boot_jni_stub = class_linker_->FindBootJniStub(JniStubKey(&method));
        if (boot_jni_stub != nullptr) {
          const void* cmp_jni_stub = method.GetOatMethodQuickCode(ptr_size);
          size_t boot_jni_stub_size =
              OatQuickMethodHeader::FromEntryPoint(boot_jni_stub)->GetCodeSize();
          size_t cmp_jni_stub_size =
              OatQuickMethodHeader::FromEntryPoint(cmp_jni_stub)->GetCodeSize();
          ArrayRef<const uint8_t> boot_jni_stub_array = ArrayRef(
              reinterpret_cast<const uint8_t*>(EntryPointToCodePointer(boot_jni_stub)),
              boot_jni_stub_size);
          ArrayRef<const uint8_t> cmp_jni_stub_array = ArrayRef(
              reinterpret_cast<const uint8_t*>(EntryPointToCodePointer(cmp_jni_stub)),
              cmp_jni_stub_size);
          ASSERT_EQ(boot_jni_stub_array, cmp_jni_stub_array)
              << "method: " << method.PrettyMethod() << ", size = " << cmp_jni_stub_size;
        }
      }
    };
    header.VisitPackedArtMethods(visitor, space->Begin(), ptr_size);
  }
}

}  // namespace art
