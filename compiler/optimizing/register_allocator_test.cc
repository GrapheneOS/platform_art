/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "register_allocator.h"

#include "arch/x86/instruction_set_features_x86.h"
#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "code_generator.h"
#include "code_generator_x86.h"
#include "com_android_art_flags.h"
#include "dex/dex_file.h"
#include "dex/dex_file_types.h"
#include "dex/dex_instruction.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "register_allocator_linear_scan.h"
#include "ssa_liveness_analysis.h"
#include "ssa_phi_elimination.h"

namespace art HIDDEN {

// Note: the register allocator tests rely on the fact that constants have live
// intervals and registers get allocated to them.

class RegisterAllocatorTest : public CommonCompilerTest, public OptimizingUnitTestHelper {
 protected:
  void SetUp() override {
    CommonCompilerTest::SetUp();
    // This test is using the x86 ISA.
    compiler_options_ = CommonCompilerTest::CreateCompilerOptions(InstructionSet::kX86, "default");
  }

  // Helper functions that make use of the OptimizingUnitTest's members.
  bool Check(const std::vector<uint16_t>& data);
  void BuildIfElseWithPhi(HPhi** phi, HInstruction** input1, HInstruction** input2);
  void BuildFieldReturn(HInstruction** field, HInstruction** ret);
  void BuildTwoSubs(HInstruction** first_sub, HInstruction** second_sub);
  void BuildDiv(HInstruction** div);

  bool ValidateIntervals(const ScopedArenaVector<LiveInterval*>& intervals,
                         const CodeGenerator& codegen) {
    return RegisterAllocator::ValidateIntervals(ArrayRef<LiveInterval* const>(intervals),
                                                /* number_of_spill_slots= */ 0u,
                                                /* number_of_out_slots= */ 0u,
                                                codegen,
                                                /*liveness=*/ nullptr,
                                                RegisterAllocator::RegisterType::kCoreRegister,
                                                /* log_fatal_on_failure= */ false);
  }

  void TestFreeUntil(bool special_first);
  void TestSpillInactive();

  std::unique_ptr<CompilerOptions> compiler_options_;
};

bool RegisterAllocatorTest::Check(const std::vector<uint16_t>& data) {
  HGraph* graph = CreateCFG(data);
  x86::CodeGeneratorX86 codegen(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, &codegen, GetScopedAllocator());
  liveness.Analyze();
  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
  register_allocator->AllocateRegisters();
  return register_allocator->Validate(false);
}

/**
 * Unit testing of RegisterAllocator::ValidateIntervals. Register allocator
 * tests are based on this validation method.
 */
TEST_F(RegisterAllocatorTest, ValidateIntervals) {
  HGraph* graph = CreateGraph();
  x86::CodeGeneratorX86 codegen(graph, *compiler_options_);
  ScopedArenaVector<LiveInterval*> intervals(GetScopedAllocator()->Adapter());

  // Test with two intervals of the same range.
  {
    static constexpr size_t ranges[][2] = {{0, 42}};
    intervals.push_back(BuildInterval(ranges, arraysize(ranges), GetScopedAllocator(), 0));
    intervals.push_back(BuildInterval(ranges, arraysize(ranges), GetScopedAllocator(), 1));
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[1]->SetRegister(0);
    ASSERT_FALSE(ValidateIntervals(intervals, codegen));
    intervals.clear();
  }

  // Test with two non-intersecting intervals.
  {
    static constexpr size_t ranges1[][2] = {{0, 42}};
    intervals.push_back(BuildInterval(ranges1, arraysize(ranges1), GetScopedAllocator(), 0));
    static constexpr size_t ranges2[][2] = {{42, 43}};
    intervals.push_back(BuildInterval(ranges2, arraysize(ranges2), GetScopedAllocator(), 1));
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[1]->SetRegister(0);
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));
    intervals.clear();
  }

  // Test with two non-intersecting intervals, with one with a lifetime hole.
  {
    static constexpr size_t ranges1[][2] = {{0, 42}, {45, 48}};
    intervals.push_back(BuildInterval(ranges1, arraysize(ranges1), GetScopedAllocator(), 0));
    static constexpr size_t ranges2[][2] = {{42, 43}};
    intervals.push_back(BuildInterval(ranges2, arraysize(ranges2), GetScopedAllocator(), 1));
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[1]->SetRegister(0);
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));
    intervals.clear();
  }

  // Test with intersecting intervals.
  {
    static constexpr size_t ranges1[][2] = {{0, 42}, {44, 48}};
    intervals.push_back(BuildInterval(ranges1, arraysize(ranges1), GetScopedAllocator(), 0));
    static constexpr size_t ranges2[][2] = {{42, 47}};
    intervals.push_back(BuildInterval(ranges2, arraysize(ranges2), GetScopedAllocator(), 1));
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[1]->SetRegister(0);
    ASSERT_FALSE(ValidateIntervals(intervals, codegen));
    intervals.clear();
  }

  // Test with siblings.
  {
    static constexpr size_t ranges1[][2] = {{0, 42}, {44, 48}};
    intervals.push_back(BuildInterval(ranges1, arraysize(ranges1), GetScopedAllocator(), 0));
    intervals[0]->SplitAt(43);
    static constexpr size_t ranges2[][2] = {{42, 47}};
    intervals.push_back(BuildInterval(ranges2, arraysize(ranges2), GetScopedAllocator(), 1));
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[1]->SetRegister(0);
    // Sibling of the first interval has no register allocated to it.
    ASSERT_TRUE(ValidateIntervals(intervals, codegen));

    intervals[0]->GetNextSibling()->SetRegister(0);
    ASSERT_FALSE(ValidateIntervals(intervals, codegen));
  }
}

TEST_F(RegisterAllocatorTest, CFG1) {
  /*
   * Test the following snippet:
   *  return 0;
   *
   * Which becomes the following graph:
   *       constant0
   *       goto
   *        |
   *       return
   *        |
   *       exit
   */
  const std::vector<uint16_t> data = ONE_REGISTER_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::RETURN);

  ASSERT_TRUE(Check(data));
}

TEST_F(RegisterAllocatorTest, Loop1) {
  /*
   * Test the following snippet:
   *  int a = 0;
   *  while (a == a) {
   *    a = 4;
   *  }
   *  return 5;
   *
   * Which becomes the following graph:
   *       constant0
   *       constant4
   *       constant5
   *       goto
   *        |
   *       goto
   *        |
   *       phi
   *       equal
   *       if +++++
   *        |       \ +
   *        |     goto
   *        |
   *       return
   *        |
   *       exit
   */

  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::IF_EQ, 4,
    Instruction::CONST_4 | 4 << 12 | 0,
    Instruction::GOTO | 0xFD00,
    Instruction::CONST_4 | 5 << 12 | 1 << 8,
    Instruction::RETURN | 1 << 8);

  ASSERT_TRUE(Check(data));
}

TEST_F(RegisterAllocatorTest, Loop2) {
  /*
   * Test the following snippet:
   *  int a = 0;
   *  while (a == 8) {
   *    a = 4 + 5;
   *  }
   *  return 6 + 7;
   *
   * Which becomes the following graph:
   *       constant0
   *       constant4
   *       constant5
   *       constant6
   *       constant7
   *       constant8
   *       goto
   *        |
   *       goto
   *        |
   *       phi
   *       equal
   *       if +++++
   *        |       \ +
   *        |      4 + 5
   *        |      goto
   *        |
   *       6 + 7
   *       return
   *        |
   *       exit
   */

  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 8 << 12 | 1 << 8,
    Instruction::IF_EQ | 1 << 8, 7,
    Instruction::CONST_4 | 4 << 12 | 0 << 8,
    Instruction::CONST_4 | 5 << 12 | 1 << 8,
    Instruction::ADD_INT, 1 << 8 | 0,
    Instruction::GOTO | 0xFA00,
    Instruction::CONST_4 | 6 << 12 | 1 << 8,
    Instruction::CONST_4 | 7 << 12 | 1 << 8,
    Instruction::ADD_INT, 1 << 8 | 0,
    Instruction::RETURN | 1 << 8);

  ASSERT_TRUE(Check(data));
}

TEST_F(RegisterAllocatorTest, Loop3) {
  /*
   * Test the following snippet:
   *  int a = 0
   *  do {
   *    b = a;
   *    a++;
   *  } while (a != 5)
   *  return b;
   *
   * Which becomes the following graph:
   *       constant0
   *       constant1
   *       constant5
   *       goto
   *        |
   *       goto
   *        |++++++++++++
   *       phi          +
   *       a++          +
   *       equals       +
   *       if           +
   *        |++++++++++++
   *       return
   *        |
   *       exit
   */

  const std::vector<uint16_t> data = THREE_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::ADD_INT_LIT8 | 1 << 8, 1 << 8,
    Instruction::CONST_4 | 5 << 12 | 2 << 8,
    Instruction::IF_NE | 1 << 8 | 2 << 12, 3,
    Instruction::RETURN | 0 << 8,
    Instruction::MOVE | 1 << 12 | 0 << 8,
    Instruction::GOTO | 0xF900);

  HGraph* graph = CreateCFG(data);
  x86::CodeGeneratorX86 codegen(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, &codegen, GetScopedAllocator());
  liveness.Analyze();
  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
  register_allocator->AllocateRegisters();
  ASSERT_TRUE(register_allocator->Validate(false));

  HBasicBlock* loop_header = graph->GetBlocks()[2];
  HPhi* phi = loop_header->GetFirstPhi()->AsPhi();

  LiveInterval* phi_interval = phi->GetLiveInterval();
  LiveInterval* loop_update = phi->InputAt(1)->GetLiveInterval();
  ASSERT_TRUE(phi_interval->HasRegister());
  ASSERT_TRUE(loop_update->HasRegister());
  ASSERT_NE(phi_interval->GetRegister(), loop_update->GetRegister());

  HBasicBlock* return_block = graph->GetBlocks()[3];
  HReturn* ret = return_block->GetLastInstruction()->AsReturn();
  ASSERT_EQ(phi_interval->GetRegister(), ret->InputAt(0)->GetLiveInterval()->GetRegister());
}

TEST_F(RegisterAllocatorTest, FirstRegisterUse) {
  const std::vector<uint16_t> data = THREE_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::XOR_INT_LIT8 | 1 << 8, 1 << 8,
    Instruction::XOR_INT_LIT8 | 0 << 8, 1 << 8,
    Instruction::XOR_INT_LIT8 | 1 << 8, 1 << 8 | 1,
    Instruction::RETURN_VOID);

  HGraph* graph = CreateCFG(data);
  x86::CodeGeneratorX86 codegen(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, &codegen, GetScopedAllocator());
  liveness.Analyze();

  HXor* first_xor = graph->GetBlocks()[1]->GetFirstInstruction()->AsXor();
  HXor* last_xor = graph->GetBlocks()[1]->GetLastInstruction()->GetPrevious()->AsXor();
  ASSERT_EQ(last_xor->InputAt(0), first_xor);
  LiveInterval* interval = first_xor->GetLiveInterval();
  ASSERT_EQ(interval->GetEnd(), last_xor->GetLifetimePosition());
  ASSERT_TRUE(interval->GetNextSibling() == nullptr);

  // We need a register for the output of the instruction.
  ASSERT_EQ(interval->FirstRegisterUse(), first_xor->GetLifetimePosition());

  // Split at the next instruction.
  interval = interval->SplitAt(first_xor->GetLifetimePosition() + 2);
  // The user of the split is the last add.
  ASSERT_EQ(interval->FirstRegisterUse(), last_xor->GetLifetimePosition());

  // Split before the last add.
  LiveInterval* new_interval = interval->SplitAt(last_xor->GetLifetimePosition() - 1);
  // Ensure the current interval has no register use...
  ASSERT_EQ(interval->FirstRegisterUse(), kNoLifetime);
  // And the new interval has it for the last add.
  ASSERT_EQ(new_interval->FirstRegisterUse(), last_xor->GetLifetimePosition());
}

TEST_F(RegisterAllocatorTest, DeadPhi) {
  /* Test for a dead loop phi taking as back-edge input a phi that also has
   * this loop phi as input. Walking backwards in SsaDeadPhiElimination
   * does not solve the problem because the loop phi will be visited last.
   *
   * Test the following snippet:
   *  int a = 0
   *  do {
   *    if (true) {
   *      a = 2;
   *    }
   *  } while (true);
   */

  const std::vector<uint16_t> data = TWO_REGISTERS_CODE_ITEM(
    Instruction::CONST_4 | 0 | 0,
    Instruction::CONST_4 | 1 << 8 | 0,
    Instruction::IF_NE | 1 << 8 | 1 << 12, 3,
    Instruction::CONST_4 | 2 << 12 | 0 << 8,
    Instruction::GOTO | 0xFD00,
    Instruction::RETURN_VOID);

  HGraph* graph = CreateCFG(data);
  SsaDeadPhiElimination(graph).Run();
  x86::CodeGeneratorX86 codegen(graph, *compiler_options_);
  SsaLivenessAnalysis liveness(graph, &codegen, GetScopedAllocator());
  liveness.Analyze();
  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
  register_allocator->AllocateRegisters();
  ASSERT_TRUE(register_allocator->Validate(false));
}

/**
 * Test that the TryAllocateFreeReg method works in the presence of inactive intervals
 * that share the same register. It should split the interval it is currently
 * allocating for at the minimum lifetime position between the two inactive intervals.
 * This test only applies to the linear scan allocator.
 */
void RegisterAllocatorTest::TestFreeUntil(bool special_first) {
  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();
  HInstruction* const0 = graph_->GetIntConstant(0);

  HAdd* add = MakeBinOp<HAdd>(block, DataType::Type::kInt32, const0, const0);
  HInstruction* placeholder1 = MakeUnOp<HNeg>(block, DataType::Type::kInt32, const0);
  HInstruction* placeholder2 = MakeUnOp<HNeg>(block, DataType::Type::kInt32, const0);
  HInstruction* ret = MakeReturn(block, add);

  graph_->ComputeDominanceInformation();
  x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
  SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
  liveness.Analyze();

  // Avoid allocating the register for the `const0` when used by the `add`.
  add->GetLocations()->SetInAt(0, Location::ConstantLocation(const0));
  ASSERT_TRUE(add->GetLocations()->InAt(1).IsConstant());

  // Record placeholder positions for blocking intervals and remove placeholders.
  size_t blocking_pos1 = placeholder1->GetLiveInterval()->GetStart();
  size_t blocking_pos2 = placeholder2->GetLiveInterval()->GetStart();
  auto& const0_uses = const0->GetLiveInterval()->uses_;
  ASSERT_EQ(4, std::distance(const0_uses.begin(), const0_uses.end()));
  auto add_it = std::next(const0_uses.begin());
  ASSERT_TRUE(add_it->GetUser() == add);
  for (HInstruction* placeholder : {placeholder1, placeholder2}) {
    block->RemoveInstruction(placeholder);
    ASSERT_TRUE(std::next(add_it)->GetUser() == placeholder);
    const0_uses.erase_after(add_it);
    // Set the block again in the dead placeholders to allow `liveness` to retrieve the block.
    placeholder->SetBlock(block);
  }

  RegisterAllocatorLinearScan register_allocator(GetScopedAllocator(), &codegen, liveness);

  // Test two variants, so that we hit the desired configuration once, no matter the order
  // in which the register allocator inserts the blocking intervals into inactive intervals.
  size_t call_pos = special_first ? blocking_pos2 : blocking_pos1;
  size_t special_pos = special_first ? blocking_pos1 : blocking_pos2;
  register_allocator.block_registers_for_call_interval_->AddRange(call_pos, call_pos + 1);
  register_allocator.block_registers_special_interval_->AddRange(special_pos, special_pos + 1);

  // Set just one register available to make all intervals compete for the same.
  bool* blocked_registers = codegen.GetBlockedCoreRegisters();
  std::fill_n(blocked_registers + 1, codegen.GetNumberOfCoreRegisters() - 1, true);

  register_allocator.AllocateRegistersInternal();

  std::pair<size_t, int> expected_add_start_and_reg[] = {
    {add->GetLifetimePosition(), 0},
    {blocking_pos1, -1},
  };
  LiveInterval* li = add->GetLiveInterval();
  for (const std::pair<size_t, int>& expected_start_and_reg : expected_add_start_and_reg) {
    ASSERT_TRUE(li != nullptr);
    ASSERT_EQ(expected_start_and_reg.first, li->GetStart());
    ASSERT_EQ(expected_start_and_reg.second, li->GetRegister());
    li = li->GetNextSibling();
  }
  ASSERT_TRUE(li == nullptr);
}

TEST_F(RegisterAllocatorTest, FreeUntilCallFirst) {
  TestFreeUntil(/*special_first=*/ false);
}

TEST_F(RegisterAllocatorTest, FreeUntilSpecialFirst) {
  TestFreeUntil(/*special_first=*/ true);
}

void RegisterAllocatorTest::BuildIfElseWithPhi(HPhi** phi,
                                               HInstruction** input1,
                                               HInstruction** input2) {
  HBasicBlock* join = InitEntryMainExitGraph();
  auto [if_block, then, else_] = CreateDiamondPattern(join);
  HInstruction* parameter = MakeParam(DataType::Type::kReference);
  HInstruction* test = MakeIFieldGet(if_block, parameter, DataType::Type::kBool, MemberOffset(22));
  MakeIf(if_block, test);

  *input1 = MakeIFieldGet(then, parameter, DataType::Type::kInt32, MemberOffset(42));
  *input2 = MakeIFieldGet(else_, parameter, DataType::Type::kInt32, MemberOffset(42));
  *phi = MakePhi(join, {*input1, *input2});
  MakeReturn(join, *phi);

  graph_->BuildDominatorTree();
  graph_->AnalyzeLoops();
}

TEST_F(RegisterAllocatorTest, PhiHint) {
  HPhi *phi;
  HInstruction *input1, *input2;

  {
    BuildIfElseWithPhi(&phi, &input1, &input2);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // Check that the register allocator is deterministic.
    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(input1->GetLiveInterval()->GetRegister(), 0);
    ASSERT_EQ(input2->GetLiveInterval()->GetRegister(), 0);
    ASSERT_EQ(phi->GetLiveInterval()->GetRegister(), 0);
  }

  {
    BuildIfElseWithPhi(&phi, &input1, &input2);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // Set the phi to a specific register, and check that the inputs get allocated
    // the same register.
    phi->GetLocations()->UpdateOut(Location::RegisterLocation(2));
    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(input1->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(input2->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(phi->GetLiveInterval()->GetRegister(), 2);
  }

  {
    BuildIfElseWithPhi(&phi, &input1, &input2);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // Set input1 to a specific register, and check that the phi and other input get allocated
    // the same register.
    input1->GetLocations()->UpdateOut(Location::RegisterLocation(2));
    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(input1->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(input2->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(phi->GetLiveInterval()->GetRegister(), 2);
  }

  {
    BuildIfElseWithPhi(&phi, &input1, &input2);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // Set input2 to a specific register, and check that the phi and other input get allocated
    // the same register.
    input2->GetLocations()->UpdateOut(Location::RegisterLocation(2));
    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(input1->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(input2->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(phi->GetLiveInterval()->GetRegister(), 2);
  }
}

void RegisterAllocatorTest::BuildFieldReturn(HInstruction** field, HInstruction** ret) {
  HBasicBlock* block = InitEntryMainExitGraph();
  HInstruction* parameter = MakeParam(DataType::Type::kReference);

  *field = MakeIFieldGet(block, parameter, DataType::Type::kInt32, MemberOffset(42));
  *ret = MakeReturn(block, *field);

  graph_->BuildDominatorTree();
}

TEST_F(RegisterAllocatorTest, ExpectedInRegisterHint) {
  HInstruction *field, *ret;

  {
    BuildFieldReturn(&field, &ret);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    // Check the validity that in normal conditions, the register should be hinted to 0 (EAX).
    ASSERT_EQ(field->GetLiveInterval()->GetRegister(), 0);
  }

  {
    BuildFieldReturn(&field, &ret);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // Check that the field gets put in the register expected by its use.
    // Don't use SetInAt because we are overriding an already allocated location.
    ret->GetLocations()->Inputs()[0] = Location::RegisterLocation(2);

    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(field->GetLiveInterval()->GetRegister(), 2);
  }
}

void RegisterAllocatorTest::BuildTwoSubs(HInstruction** first_sub, HInstruction** second_sub) {
  HBasicBlock* block = InitEntryMainExitGraph();
  HInstruction* parameter = MakeParam(DataType::Type::kInt32);
  HInstruction* constant1 = graph_->GetIntConstant(1);
  HInstruction* constant2 = graph_->GetIntConstant(2);

  *first_sub = new (GetAllocator()) HSub(DataType::Type::kInt32, parameter, constant1);
  block->AddInstruction(*first_sub);
  *second_sub = new (GetAllocator()) HSub(DataType::Type::kInt32, *first_sub, constant2);
  block->AddInstruction(*second_sub);
  MakeReturn(block, *second_sub);

  graph_->BuildDominatorTree();
}

TEST_F(RegisterAllocatorTest, SameAsFirstInputHint) {
  HInstruction *first_sub, *second_sub;

  {
    BuildTwoSubs(&first_sub, &second_sub);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    // Check the validity that in normal conditions, the registers are the same.
    ASSERT_EQ(first_sub->GetLiveInterval()->GetRegister(), 1);
    ASSERT_EQ(second_sub->GetLiveInterval()->GetRegister(), 1);
  }

  {
    BuildTwoSubs(&first_sub, &second_sub);
    x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
    SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
    liveness.Analyze();

    // check that both adds get the same register.
    // Don't use UpdateOutput because output is already allocated.
    first_sub->InputAt(0)->GetLocations()->output_ = Location::RegisterLocation(2);
    ASSERT_EQ(first_sub->GetLocations()->Out().GetPolicy(), Location::kSameAsFirstInput);
    ASSERT_EQ(second_sub->GetLocations()->Out().GetPolicy(), Location::kSameAsFirstInput);

    std::unique_ptr<RegisterAllocator> register_allocator =
        RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
    register_allocator->AllocateRegisters();

    ASSERT_EQ(first_sub->GetLiveInterval()->GetRegister(), 2);
    ASSERT_EQ(second_sub->GetLiveInterval()->GetRegister(), 2);
  }
}

void RegisterAllocatorTest::BuildDiv(HInstruction** div) {
  HBasicBlock* block = InitEntryMainExitGraph();
  HInstruction* first = MakeParam(DataType::Type::kInt32);
  HInstruction* second = MakeParam(DataType::Type::kInt32);

  *div = new (GetAllocator()) HDiv(
      DataType::Type::kInt32, first, second, 0);  // don't care about dex_pc.
  block->AddInstruction(*div);
  MakeReturn(block, *div);

  graph_->BuildDominatorTree();
}

TEST_F(RegisterAllocatorTest, ExpectedExactInRegisterAndSameOutputHint) {
  HInstruction *div;
  BuildDiv(&div);
  x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
  SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
  liveness.Analyze();

  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
  register_allocator->AllocateRegisters();

  // div on x86 requires its first input in eax and the output be the same as the first input.
  ASSERT_EQ(div->GetLiveInterval()->GetRegister(), 0);
}

// Test a bug in the register allocator, where allocating a blocked
// register would lead to spilling an inactive interval at the wrong
// position.
// This test only applies to the linear scan allocator.
void RegisterAllocatorTest::TestSpillInactive() {
  // Define a shortcut for the `kLivenessPositionsPerInstruction`.
  static constexpr size_t kLppi = kLivenessPositionsPerInstruction;

  HBasicBlock* block = InitEntryMainExitGraphWithReturnVoid();
  HInstruction* one = MakeParam(DataType::Type::kInt32);
  HInstruction* two = MakeParam(DataType::Type::kInt32);
  HInstruction* three = MakeParam(DataType::Type::kInt32);
  HInstruction* four = MakeParam(DataType::Type::kInt32);

  // We create a synthesized user requesting a register, to avoid just spilling the
  // intervals.
  HPhi* user = new (GetAllocator()) HPhi(GetAllocator(), 0, 1, DataType::Type::kInt32);
  user->SetBlock(block);
  user->AddInput(one);
  LocationSummary* locations = LocationSummary::CreateNoCall(GetAllocator(), user);
  locations->SetInAt(0, Location::RequiresRegister());
  static constexpr size_t phi_ranges[][2] = {{10 * kLppi, 15 * kLppi}};
  BuildInterval(phi_ranges, arraysize(phi_ranges), GetScopedAllocator(), -1, user);

  // Create an interval with lifetime holes.
  static constexpr size_t ranges1[][2] =
      {{0u * kLppi, 2u * kLppi}, {4u * kLppi, 5u * kLppi}, {7u * kLppi, 8u * kLppi}};
  LiveInterval* first = BuildInterval(ranges1, arraysize(ranges1), GetScopedAllocator(), -1, one);
  first->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 7u * kLppi));
  first->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 6u * kLppi));
  first->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 5u * kLppi));

  locations = LocationSummary::CreateNoCall(GetAllocator(), first->GetDefinedBy());
  locations->SetOut(Location::RequiresRegister());
  first = first->SplitAt(1u * kLppi);

  // Create an interval that conflicts with the next interval, to force the next
  // interval to call `AllocateBlockedReg`.
  static constexpr size_t ranges2[][2] = {{2u * kLppi, 4u * kLppi}};
  LiveInterval* second = BuildInterval(ranges2, arraysize(ranges2), GetScopedAllocator(), -1, two);
  locations = LocationSummary::CreateNoCall(GetAllocator(), second->GetDefinedBy());
  locations->SetOut(Location::RequiresRegister());

  // Create an interval that will lead to splitting the first interval. The bug occured
  // by splitting at a wrong position, in this case at the next intersection between
  // this interval and the first interval. We would have then put the interval with ranges
  // "[0, 2(, [4, 6(" in the list of handled intervals, even though we haven't processed intervals
  // before lifetime position 6 yet.
  static constexpr size_t ranges3[][2] = {{2u * kLppi, 4u * kLppi}, {7u * kLppi, 8u * kLppi}};
  LiveInterval* third = BuildInterval(ranges3, arraysize(ranges3), GetScopedAllocator(), -1, three);
  third->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 7u * kLppi));
  third->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 4u * kLppi));
  third->uses_.push_front(*new (GetScopedAllocator()) UsePosition(user, 0u, 3u * kLppi));
  locations = LocationSummary::CreateNoCall(GetAllocator(), third->GetDefinedBy());
  locations->SetOut(Location::RequiresRegister());
  third = third->SplitAt(3u * kLppi);

  // Because the first part of the split interval was considered handled, this interval
  // was free to allocate the same register, even though it conflicts with it.
  static constexpr size_t ranges4[][2] = {{4u * kLppi, 5u * kLppi}};
  LiveInterval* fourth = BuildInterval(ranges4, arraysize(ranges4), GetScopedAllocator(), -1, four);
  locations = LocationSummary::CreateNoCall(GetAllocator(), fourth->GetDefinedBy());
  locations->SetOut(Location::RequiresRegister());

  x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
  SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
  // Populate the instructions in the liveness object, to please the register allocator.
  liveness.instructions_from_lifetime_position_.assign(16, user);

  RegisterAllocatorLinearScan register_allocator(GetScopedAllocator(), &codegen, liveness);
  register_allocator.unhandled_core_intervals_.assign({fourth, third, second, first});

  // Set just one register available to make all intervals compete for the same.
  bool* blocked_registers = codegen.GetBlockedCoreRegisters();
  std::fill_n(blocked_registers + 1, codegen.GetNumberOfCoreRegisters() - 1, true);

  // We have set up all intervals manually and we want `AllocateRegistersInternal()` to run
  // the linear scan without processing instructions - check that the linear order is empty.
  ASSERT_TRUE(codegen.GetGraph()->GetLinearOrder().empty());
  register_allocator.AllocateRegistersInternal();

  // Test that there is no conflicts between intervals.
  ScopedArenaVector<LiveInterval*> intervals({first, second, third, fourth},
                                             GetScopedAllocator()->Adapter());
  ASSERT_TRUE(ValidateIntervals(intervals, codegen));
}

TEST_F(RegisterAllocatorTest, SpillInactive) {
  TestSpillInactive();
}

TEST_F(RegisterAllocatorTest, ReuseSpillSlots) {
  if (!com::android::art::flags::reg_alloc_spill_slot_reuse()) {
    GTEST_SKIP() << "Improved spill slot reuse disabled.";
  }
  HBasicBlock* return_block = InitEntryMainExitGraph();
  auto [start, left, right] = CreateDiamondPattern(return_block);
  HInstruction* obj = MakeParam(DataType::Type::kReference);
  HInstruction* cond = MakeIFieldGet(start, obj, DataType::Type::kBool, MemberOffset(32));
  MakeIf(start, cond);

  // Load two values from fields. Both shall be used as Phi inputs later.
  HInstruction* left_get1 = MakeIFieldGet(left, obj, DataType::Type::kInt32, MemberOffset(36));
  HInstruction* left_get2 = MakeIFieldGet(left, obj, DataType::Type::kInt32, MemberOffset(40));
  // Convert one of the values to `Int64` to spill the loaded values.
  HInstruction* left_conv1 = MakeUnOp<HTypeConversion>(left, DataType::Type::kInt64, left_get1);
  // Convert the `Int64` value back to `Int32`. x86 codegen uses EAX and EDX for conversion
  // which is not a normal pair, so avoid using this odd explicit pair for a Phi.
  HInstruction* left_conv2 = MakeUnOp<HTypeConversion>(left, DataType::Type::kInt32, left_conv1);

  // Repeat the sequence from `left` block in the `right` block (with different offsets). Without
  // spill slot hints, spill slots should be assigned the same way as in the `left` block.
  HInstruction* right_get1 = MakeIFieldGet(right, obj, DataType::Type::kInt32, MemberOffset(44));
  HInstruction* right_get2 = MakeIFieldGet(right, obj, DataType::Type::kInt32, MemberOffset(48));
  HInstruction* right_conv1 = MakeUnOp<HTypeConversion>(right, DataType::Type::kInt64, right_get1);
  HInstruction* right_conv2 = MakeUnOp<HTypeConversion>(right, DataType::Type::kInt32, right_conv1);

  // Add Phis that tie the first field load in `left` to the second field load in `right` and
  // vice versa, to check that the hints can align the spill slots assigned to inputs.
  HPhi* phi1 = MakePhi(return_block, {left_get1, right_get2});
  HPhi* phi2 = MakePhi(return_block, {left_get2, right_get1});

  // Add some instructions that use the `phi1`, `phi2` and even the converted values
  // to derive some return value.
  HPhi* phi_conv = MakePhi(return_block, {left_conv2, right_conv2});
  HMin* min1 = MakeBinOp<HMin>(return_block, DataType::Type::kInt32, phi1, phi2);
  HMin* min2 = MakeBinOp<HMin>(return_block, DataType::Type::kInt32, min1, phi_conv);
  MakeReturn(return_block, min2);

  graph_->ComputeDominanceInformation();
  x86::CodeGeneratorX86 codegen(graph_, *compiler_options_);
  SsaLivenessAnalysis liveness(graph_, &codegen, GetScopedAllocator());
  liveness.Analyze();

  // Set just two registers available to make it easy to force spills.
  // Choose EAX and EDX which are used by type conversion from Int32 to Int64, so that
  // we can use the type conversion to spill all live intervals wherever we want.
  // Note that the `obj` parameter comes in the blocked ECX which works fine for the test.
  bool* blocked_registers = codegen.GetBlockedCoreRegisters();
  std::fill_n(blocked_registers, codegen.GetNumberOfCoreRegisters(), true);
  blocked_registers[x86::EAX] = blocked_registers[x86::EDX] = false;

  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), &codegen, liveness);
  register_allocator->AllocateRegisters();

  // Field loads would be spilled even without using spill slot hints.
  ASSERT_TRUE(left_get1->GetLiveInterval()->HasSpillSlot());
  ASSERT_TRUE(left_get2->GetLiveInterval()->HasSpillSlot());
  ASSERT_TRUE(right_get1->GetLiveInterval()->HasSpillSlot());
  ASSERT_TRUE(right_get2->GetLiveInterval()->HasSpillSlot());

  // Input spill slots are aligned thanks to the spill slot hints.
  EXPECT_EQ(left_get1->GetLiveInterval()->GetSpillSlot(),
            right_get2->GetLiveInterval()->GetSpillSlot());
  EXPECT_EQ(left_get2->GetLiveInterval()->GetSpillSlot(),
            right_get1->GetLiveInterval()->GetSpillSlot());

  // Check that `phi1` and `phi2` use the spill slots used by their inputs.
  EXPECT_TRUE(phi1->GetLiveInterval()->HasSpillSlot());
  EXPECT_EQ(left_get1->GetLiveInterval()->GetSpillSlot(), phi1->GetLiveInterval()->GetSpillSlot());
  EXPECT_TRUE(phi2->GetLiveInterval()->HasSpillSlot());
  EXPECT_EQ(left_get2->GetLiveInterval()->GetSpillSlot(), phi2->GetLiveInterval()->GetSpillSlot());

  // Check that `phi1` and `phi2` are split and don't have a register in the first sibling.
  EXPECT_TRUE(phi1->GetLiveInterval()->GetNextSibling() != nullptr);
  EXPECT_TRUE(!phi1->GetLiveInterval()->HasRegister());
  EXPECT_TRUE(phi2->GetLiveInterval()->GetNextSibling() != nullptr);
  EXPECT_TRUE(!phi2->GetLiveInterval()->HasRegister());
}

}  // namespace art
