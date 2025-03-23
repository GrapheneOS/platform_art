/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "instruction_simplifier.h"

#include <initializer_list>
#include <tuple>

#include "gtest/gtest.h"

#include "class_root-inl.h"
#include "nodes.h"
#include "optimizing/data_type.h"
#include "optimizing_unit_test.h"

namespace art HIDDEN {

namespace mirror {
class ClassExt;
class Throwable;
}  // namespace mirror

static constexpr bool kDebugSimplifierTests = false;

template<typename SuperClass>
class InstructionSimplifierTestBase : public SuperClass, public OptimizingUnitTestHelper {
 public:
  InstructionSimplifierTestBase() {
    this->use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }

  void SetUp() override {
    SuperClass::SetUp();
    gLogVerbosity.compiler = true;
  }

  void TearDown() override {
    SuperClass::TearDown();
    gLogVerbosity.compiler = false;
  }

  void PerformSimplification() {
    if (kDebugSimplifierTests) {
      graph_->Dump(LOG_STREAM(INFO) << "Pre simplification ", /* codegen_= */ nullptr);
    }
    graph_->ClearDominanceInformation();
    graph_->BuildDominatorTree();
    InstructionSimplifier simp(graph_, /*codegen=*/nullptr);
    simp.Run();
    if (kDebugSimplifierTests) {
      graph_->Dump(LOG_STREAM(INFO) << "Post simplify ", /* codegen_= */ nullptr);
    }
  }
};

class InstructionSimplifierTest : public InstructionSimplifierTestBase<CommonCompilerTest> {};

// Various configs we can use for testing. Currently used in PartialComparison tests.
enum class InstanceOfKind {
  kSelf,
  kUnrelatedLoaded,
  kUnrelatedUnloaded,
  kSupertype,
};

std::ostream& operator<<(std::ostream& os, const InstanceOfKind& comp) {
  switch (comp) {
    case InstanceOfKind::kSupertype:
      return os << "kSupertype";
    case InstanceOfKind::kSelf:
      return os << "kSelf";
    case InstanceOfKind::kUnrelatedLoaded:
      return os << "kUnrelatedLoaded";
    case InstanceOfKind::kUnrelatedUnloaded:
      return os << "kUnrelatedUnloaded";
  }
}

class InstanceOfInstructionSimplifierTestGroup
    : public InstructionSimplifierTestBase<CommonCompilerTestWithParam<InstanceOfKind>> {
 public:
  bool GetConstantResult() const {
    switch (GetParam()) {
      case InstanceOfKind::kSupertype:
      case InstanceOfKind::kSelf:
        return true;
      case InstanceOfKind::kUnrelatedLoaded:
      case InstanceOfKind::kUnrelatedUnloaded:
        return false;
    }
  }

  std::pair<HLoadClass*, HLoadClass*> GetLoadClasses(HBasicBlock* block,
                                                     VariableSizedHandleScope* vshs)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    InstanceOfKind kind = GetParam();
    // New inst always needs to have a valid rti since we dcheck that.
    HLoadClass* new_inst = MakeLoadClass(
        block,
        /* ti= */ std::nullopt,
        vshs->NewHandle<mirror::Class>(GetClassRoot<mirror::ClassExt>()));
    new_inst->SetValidLoadedClassRTI();
    if (kind == InstanceOfKind::kSelf) {
      return {new_inst, new_inst};
    }
    if (kind == InstanceOfKind::kUnrelatedUnloaded) {
      HLoadClass* target_class = MakeLoadClass(block);
      EXPECT_FALSE(target_class->GetLoadedClassRTI().IsValid());
      return {new_inst, target_class};
    }
    // Force both classes to be a real classes.
    // For simplicity we use class-roots as the types. The new-inst will always
    // be a ClassExt, unrelated-loaded will always be Throwable and super will
    // always be Object
    HLoadClass* target_class = MakeLoadClass(
        block,
        /* ti= */ std::nullopt,
        vshs->NewHandle<mirror::Class>(kind == InstanceOfKind::kSupertype ?
                                           GetClassRoot<mirror::Object>() :
                                           GetClassRoot<mirror::Throwable>()));
    target_class->SetValidLoadedClassRTI();
    EXPECT_TRUE(target_class->GetLoadedClassRTI().IsValid());
    return {new_inst, target_class};
  }
};

// // ENTRY
// obj = new Obj();
// // Make sure this graph isn't broken
// if (obj instanceof <other>) {
//   // LEFT
// } else {
//   // RIGHT
// }
// EXIT
// return obj.field
TEST_P(InstanceOfInstructionSimplifierTestGroup, ExactClassInstanceOfOther) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* breturn = InitEntryMainExitGraph(/*handles=*/&vshs);
  auto [if_block, left, right] = CreateDiamondPattern(breturn);
  EnsurePredecessorOrder(breturn, {left, right});

  HInstruction* test_res = graph_->GetIntConstant(GetConstantResult() ? 1 : 0);

  auto [new_inst_klass, target_klass] = GetLoadClasses(if_block, &vshs);
  HInstruction* new_inst = MakeNewInstance(if_block, new_inst_klass);
  new_inst->SetReferenceTypeInfo(
      ReferenceTypeInfo::Create(new_inst_klass->GetClass(), /*is_exact=*/true));
  HInstanceOf* instance_of = new (GetAllocator()) HInstanceOf(new_inst,
                                                              target_klass,
                                                              TypeCheckKind::kClassHierarchyCheck,
                                                              target_klass->GetClass(),
                                                              0u,
                                                              GetAllocator(),
                                                              nullptr,
                                                              nullptr);
  if (target_klass->GetLoadedClassRTI().IsValid()) {
    instance_of->SetValidTargetClassRTI();
  }
  if_block->AddInstruction(instance_of);
  HIf* if_inst = MakeIf(if_block, instance_of);

  HInstruction* read_bottom =
      MakeIFieldGet(breturn, new_inst, DataType::Type::kInt32, MemberOffset(32));
  MakeReturn(breturn, read_bottom);

  PerformSimplification();

  if (!GetConstantResult() || GetParam() == InstanceOfKind::kSelf) {
    EXPECT_INS_RETAINED(target_klass);
  } else {
    EXPECT_INS_REMOVED(target_klass);
  }
  EXPECT_INS_REMOVED(instance_of);
  EXPECT_INS_EQ(if_inst->InputAt(0), test_res);
}

// // ENTRY
// obj = new Obj();
// (<other>)obj;
// // Make sure this graph isn't broken
// EXIT
// return obj
TEST_P(InstanceOfInstructionSimplifierTestGroup, ExactClassCheckCastOther) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope vshs(soa.Self());
  HBasicBlock* main = InitEntryMainExitGraph(/*handles=*/&vshs);

  auto [new_inst_klass, target_klass] = GetLoadClasses(main, &vshs);
  HInstruction* new_inst = MakeNewInstance(main, new_inst_klass);
  new_inst->SetReferenceTypeInfo(
      ReferenceTypeInfo::Create(new_inst_klass->GetClass(), /*is_exact=*/true));
  HCheckCast* check_cast = new (GetAllocator()) HCheckCast(new_inst,
                                                           target_klass,
                                                           TypeCheckKind::kClassHierarchyCheck,
                                                           target_klass->GetClass(),
                                                           0u,
                                                           GetAllocator(),
                                                           nullptr,
                                                           nullptr);
  if (target_klass->GetLoadedClassRTI().IsValid()) {
    check_cast->SetValidTargetClassRTI();
  }
  main->AddInstruction(check_cast);
  MakeReturn(main, new_inst);

  PerformSimplification();

  if (!GetConstantResult() || GetParam() == InstanceOfKind::kSelf) {
    EXPECT_INS_RETAINED(target_klass);
  } else {
    EXPECT_INS_REMOVED(target_klass);
  }
  if (GetConstantResult()) {
    EXPECT_INS_REMOVED(check_cast);
  } else {
    EXPECT_INS_RETAINED(check_cast);
  }
}

INSTANTIATE_TEST_SUITE_P(InstructionSimplifierTest,
                         InstanceOfInstructionSimplifierTestGroup,
                         ::testing::Values(InstanceOfKind::kSelf,
                                           InstanceOfKind::kUnrelatedLoaded,
                                           InstanceOfKind::kUnrelatedUnloaded,
                                           InstanceOfKind::kSupertype));

}  // namespace art
