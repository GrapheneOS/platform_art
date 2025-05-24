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

#include "register_allocator_linear_scan.h"

#include <iostream>
#include <sstream>

#include "base/bit_utils_iterator.h"
#include "base/bit_vector-inl.h"
#include "base/pointer_size.h"
#include "code_generator.h"
#include "com_android_art_flags.h"
#include "linear_order.h"
#include "register_allocation_resolver.h"
#include "register_allocator-inl.h"
#include "ssa_liveness_analysis.h"

namespace art HIDDEN {

static constexpr size_t kMaxLifetimePosition = -1;
static constexpr size_t kDefaultNumberOfSpillSlots = 4;

// For simplicity, we implement register pairs as (reg, reg + 1).
// Note that this is a requirement for double registers on ARM, since we
// allocate SRegister.
static int GetHighForLowRegister(int reg) { return reg + 1; }
static bool IsLowRegister(int reg) { return (reg & 1) == 0; }
static bool IsLowOfUnalignedPairInterval(LiveInterval* low) {
  return GetHighForLowRegister(low->GetRegister()) != low->GetHighInterval()->GetRegister();
}

class RegisterAllocatorLinearScan::SpillSlotData {
 public:
  SpillSlotData(LiveInterval* interval, size_t end)
      : end_(end),
        gap_start_(interval->GetFirstRange()->GetEnd()),
        interval_(interval),
        range_(interval->GetFirstRange()) {
    DCHECK_EQ(end, interval->GetLastSibling()->GetEnd());
  }

  size_t GetEnd() const {
    return end_;
  }

  // Determine if the spill slot can be used for another interval `parent`.
  // Returns `true` if the spill slot can be reused, `false` otherwise.
  // The `parent`'s `start` and `end` are passed as arguments as a performance optimization.
  //
  // This is a heuristic which does not find all spill slot reuse opportunities. For that,
  // we would need to keep track of all lifetime positions used for the spill slot, either as
  // a bit mask, or as a list of all intervals using it, and that could take a lot of memory.
  //
  // Instead, we keep only the `interval_` with the longest lifetime (ending at `end_`) and
  // the `gap_start_`, the earlier position that can be reused. When we reuse the slot for
  // another interval that falls within `[gap_start_, end_)` and does not overlap the current
  // `interval_`'s lifetime, we update `gap_start_` to the end of that interval.
  bool CanUseFor(LiveInterval* parent, size_t start, size_t end) {
    DCHECK(!parent->IsSplit());
    DCHECK_EQ(start, parent->GetStart());
    DCHECK_EQ(end, parent->GetLastSibling()->GetEnd());
    // Check if the spill slot use has ended at the `start` position.
    if (start >= end_) {
      return true;
    }
    // Check if the `parent` interval can fit in the gap.
    if (start < gap_start_ || end > end_) {
      return false;
    }
    // Update search start position based on `gap_start_`.
    while (gap_start_ >= interval_->GetEnd()) {
      if (interval_->GetNextSibling() == nullptr) {
        return true;  // The entire range `[gap_start_, end_)` can be used.
      }
      interval_ = interval_->GetNextSibling();
      range_ = interval_->GetFirstRange();
    }
    while (gap_start_ >= range_->GetEnd()) {
      range_ = range_->GetNext();
      DCHECK(range_ != nullptr);
    }
    // Check if there are any overlapping ranges.
    // This is similar to `LiveInterval::FirstIntersectionWith()` but covers sibling intervals.
    auto move_to_next_range = [](LiveInterval*& interval, LiveRange*& range) ALWAYS_INLINE {
      range = range->GetNext();
      if (range == nullptr) {
        if (interval->GetNextSibling() == nullptr) {
          return false;
        }
        interval = interval->GetNextSibling();
        range = interval->GetFirstRange();
      }
      return true;
    };
    LiveInterval* interval = interval_;
    LiveRange* range = range_;
    LiveInterval* other_interval = parent;
    LiveRange* other_range = parent->GetFirstRange();
    while (true) {
      if (range->IsBefore(*other_range)) {
        if (!move_to_next_range(interval, range)) {
          return true;  // No more ranges to check.
        }
      } else if (other_range->IsBefore(*range)) {
        if (!move_to_next_range(other_interval, other_range)) {
          return true;  // No more ranges to check.
        }
      } else {
        DCHECK(range->IntersectsWith(*other_range));
        return false;
      }
    }
  }

  void UseFor(LiveInterval* interval, size_t end) {
    DCHECK_EQ(end, interval->GetLastSibling()->GetEnd());
    if (end > end_) {
      DCHECK_GE(interval->GetParent()->GetStart(), end_);
      *this = SpillSlotData(interval, end);
    } else {
      DCHECK(CanUseFor(interval->GetParent(), interval->GetParent()->GetStart(), end));
      DCHECK_GT(end, gap_start_);
      gap_start_ = end;
    }
  }

 private:
  size_t end_;
  size_t gap_start_;
  LiveInterval* interval_;
  LiveRange* range_;
};

class RegisterAllocatorLinearScan::LinearScan {
 public:
  LinearScan(RegisterAllocatorLinearScan* register_allocator, RegisterType register_type)
      : LinearScan(register_allocator,
                   register_type,
                   register_allocator->codegen_,
                   register_allocator->allocator_) {}

  void Run();

 private:
  LinearScan(RegisterAllocatorLinearScan* register_allocator,
             RegisterType register_type,
             CodeGenerator* codegen,
             ScopedArenaAllocator* allocator);

  static size_t GetNumberOfRegisters(CodeGenerator* codegen, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? codegen->GetNumberOfCoreRegisters()
        : codegen->GetNumberOfFloatingPointRegisters();
  }

  static size_t GetRegistersBlockedForCall(
      RegisterAllocatorLinearScan* register_allocator, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? register_allocator->core_registers_blocked_for_call_
        : register_allocator->fp_registers_blocked_for_call_;
  }

  static const bool* GetBlockedRegisters(CodeGenerator* codegen, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? codegen->GetBlockedCoreRegisters()
        : codegen->GetBlockedFloatingPointRegisters();
  }

  static ScopedArenaVector<SpillSlotData>* GetSpillSlots(
      RegisterAllocatorLinearScan* register_allocator, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? &register_allocator->int_spill_slots_
        : &register_allocator->float_spill_slots_;
  }

  static ScopedArenaVector<SpillSlotData>* GetWideSpillSlots(
      RegisterAllocatorLinearScan* register_allocator, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? &register_allocator->long_spill_slots_
        : &register_allocator->double_spill_slots_;
  }

  static ScopedArenaVector<LiveInterval*> TakeUnhandledIntervals(
      RegisterAllocatorLinearScan* register_allocator, RegisterType register_type) {
    ScopedArenaVector<LiveInterval*>* source = register_type == RegisterType::kCoreRegister
        ? &register_allocator->unhandled_core_intervals_
        : &register_allocator->unhandled_fp_intervals_;
    ScopedArenaVector<LiveInterval*> result(source->get_allocator());
    result.swap(*source);
    return result;
  }

  static ScopedArenaVector<LiveInterval*>* GetPhysicalRegisterIntervals(
      RegisterAllocatorLinearScan* register_allocator, RegisterType register_type) {
    return register_type == RegisterType::kCoreRegister
        ? &register_allocator->physical_core_register_intervals_
        : &register_allocator->physical_fp_register_intervals_;
  }

  ALWAYS_INLINE ScopedArenaVector<SpillSlotData>* GetSpillSlotsForType(DataType::Type type) {
    switch (type) {
      case DataType::Type::kFloat64:
      case DataType::Type::kInt64:
        return wide_spill_slots_;
      case DataType::Type::kUint32:
      case DataType::Type::kUint64:
      case DataType::Type::kVoid:
        // Let the compiler optimize this away in release build.
        DCHECK(false) << "Unexpected type for interval " << type;
        FALLTHROUGH_INTENDED;
      case DataType::Type::kFloat32:
      case DataType::Type::kReference:
      case DataType::Type::kInt32:
      case DataType::Type::kUint16:
      case DataType::Type::kUint8:
      case DataType::Type::kInt8:
      case DataType::Type::kBool:
      case DataType::Type::kInt16:
        return spill_slots_;
    }
  }

  bool TryUsingSpillSlotHint(LiveInterval* interval);
  bool TryAllocateFreeReg(LiveInterval* interval);
  bool AllocateBlockedReg(LiveInterval* interval);
  int FindAvailableRegisterPair(ArrayRef<size_t> next_use, size_t starting_at) const;
  int FindAvailableRegister(ArrayRef<size_t> next_use, LiveInterval* current) const;

  // Allocate a spill slot for the given interval. Should be called in linear
  // order of interval starting positions.
  void AllocateSpillSlotFor(LiveInterval* interval);

  void DumpInterval(std::ostream& stream, LiveInterval* interval) const;
  void DumpAllIntervals(std::ostream& stream) const;

  // Try splitting an active non-pair or unaligned pair interval at the given `position`.
  // Returns whether it was successful at finding such an interval.
  bool TrySplitNonPairOrUnalignedPairIntervalAt(size_t position,
                                                size_t first_register_use,
                                                ArrayRef<size_t> next_use);

  LiveInterval* SplitBetween(LiveInterval* interval, size_t from, size_t to) const {
    return RegisterAllocator::SplitBetween(interval, from, to, instructions_from_positions_);
  }

  bool IsBlocked(int reg) const {
    DCHECK_LT(static_cast<size_t>(reg), number_of_registers_);
    return blocked_registers_[reg];
  }

  bool IsCallerSaveRegister(int reg) const {
    DCHECK_LT(static_cast<size_t>(reg), BitSizeOf<uint32_t>());
    return (registers_blocked_for_call_ & (1u << reg)) != 0u;
  }

  ArrayRef<size_t> GetRegistersArray() {
    return ArrayRef<size_t>(registers_array_, number_of_registers_);
  }

  uint32_t GetRegisterMask(LiveInterval* interval) {
    return interval->HasRegister()
        ? RegisterAllocator::GetSingleRegisterMask(interval, register_type_)
        : RegisterAllocator::GetBlockedRegistersMask(interval,
                                                     instructions_from_positions_,
                                                     number_of_registers_,
                                                     registers_blocked_for_call_);
  }

  CodeGenerator* const codegen_;

  // Number of registers for the current register kind (core or floating point).
  size_t number_of_registers_;

  // The register type processed by this `LinearScan` object.
  const RegisterType register_type_;

  // Mask of registers blocked for a call.
  uint32_t registers_blocked_for_call_;

  // Blocked registers, as decided by the code generator.
  const bool* const blocked_registers_;

  // Spill slots for normal and wide intervals, pointing to appropriately typed slots
  // in the `RegisterAllocatorLinearScan`.
  ScopedArenaVector<SpillSlotData>* const spill_slots_;
  ScopedArenaVector<SpillSlotData>* const wide_spill_slots_;

  // Currently processed list of unhandled intervals. Retrieved either from
  // `unhandled_core_intervals_` or `unhandled_fp_intervals_`.
  ScopedArenaVector<LiveInterval*> unhandled_;

  // List of intervals that have been processed.
  ScopedArenaVector<LiveInterval*> handled_;

  // List of intervals that are currently active when processing a new live interval.
  // That is, they have a live range that spans the start of the new interval.
  ScopedArenaVector<LiveInterval*> active_;

  // List of intervals that are currently inactive when processing a new live interval.
  // That is, they have a lifetime hole that spans the start of the new interval.
  ScopedArenaVector<LiveInterval*> inactive_;

  // Instructions indexed by lifetime positions, cached from `SsaLivenessAnalysis`.
  const ArrayRef<HInstruction* const> instructions_from_positions_;

  // Temporary array, allocated ahead of time for simplicity.
  size_t* registers_array_;
};

RegisterAllocatorLinearScan::RegisterAllocatorLinearScan(ScopedArenaAllocator* allocator,
                                                         CodeGenerator* codegen,
                                                         const SsaLivenessAnalysis& liveness)
      : RegisterAllocator(allocator, codegen, liveness),
        unhandled_core_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        unhandled_fp_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        physical_core_register_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        physical_fp_register_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        block_registers_for_call_interval_(
            LiveInterval::MakeFixedInterval(allocator, kNoRegister, DataType::Type::kVoid)),
        block_registers_special_interval_(
            LiveInterval::MakeFixedInterval(allocator, kNoRegister, DataType::Type::kVoid)),
        temp_intervals_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        int_spill_slots_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        long_spill_slots_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        float_spill_slots_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        double_spill_slots_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        catch_phi_spill_slots_(0),
        safepoints_(allocator->Adapter(kArenaAllocRegisterAllocator)),
        reserved_out_slots_(0) {
  temp_intervals_.reserve(4);
  int_spill_slots_.reserve(kDefaultNumberOfSpillSlots);
  long_spill_slots_.reserve(kDefaultNumberOfSpillSlots);
  float_spill_slots_.reserve(kDefaultNumberOfSpillSlots);
  double_spill_slots_.reserve(kDefaultNumberOfSpillSlots);

  codegen->SetupBlockedRegisters();
  physical_core_register_intervals_.resize(codegen->GetNumberOfCoreRegisters(), nullptr);
  physical_fp_register_intervals_.resize(codegen->GetNumberOfFloatingPointRegisters(), nullptr);
}

RegisterAllocatorLinearScan::~RegisterAllocatorLinearScan() {}

void RegisterAllocatorLinearScan::AllocateRegisters() {
  AllocateRegistersInternal();
  RegisterAllocationResolver(codegen_, liveness_)
      .Resolve(ArrayRef<HInstruction* const>(safepoints_),
               reserved_out_slots_,
               int_spill_slots_.size(),
               long_spill_slots_.size(),
               float_spill_slots_.size(),
               double_spill_slots_.size(),
               catch_phi_spill_slots_,
               ArrayRef<LiveInterval* const>(temp_intervals_));

  if (kIsDebugBuild) {
    ValidateInternal(RegisterType::kCoreRegister, /*log_fatal_on_failure=*/ true);
    ValidateInternal(RegisterType::kFpRegister, /*log_fatal_on_failure=*/ true);
    // Check that the linear order is still correct with regards to lifetime positions.
    // Since only parallel moves have been inserted during the register allocation,
    // these checks are mostly for making sure these moves have been added correctly.
    size_t current_liveness = 0;
    for (HBasicBlock* block : codegen_->GetGraph()->GetLinearOrder()) {
      for (HInstructionIteratorPrefetchNext inst_it(block->GetPhis()); !inst_it.Done();
           inst_it.Advance()) {
        HInstruction* instruction = inst_it.Current();
        DCHECK_LE(current_liveness, instruction->GetLifetimePosition());
        current_liveness = instruction->GetLifetimePosition();
      }
      for (HInstructionIteratorPrefetchNext inst_it(block->GetInstructions());
           !inst_it.Done();
           inst_it.Advance()) {
        HInstruction* instruction = inst_it.Current();
        DCHECK_LE(current_liveness, instruction->GetLifetimePosition()) << instruction->DebugName();
        current_liveness = instruction->GetLifetimePosition();
      }
    }
  }
}

bool RegisterAllocatorLinearScan::Validate(bool log_fatal_on_failure) {
  return ValidateInternal(RegisterType::kCoreRegister, log_fatal_on_failure) &&
         ValidateInternal(RegisterType::kFpRegister, log_fatal_on_failure);
}

void RegisterAllocatorLinearScan::BlockRegister(Location location,
                                                size_t position,
                                                bool will_call) {
  DCHECK(location.IsRegister() || location.IsFpuRegister());
  int reg = location.reg();
  if (will_call) {
    uint32_t registers_blocked_for_call =
        location.IsRegister() ? core_registers_blocked_for_call_ : fp_registers_blocked_for_call_;
    if ((registers_blocked_for_call & (1u << reg)) != 0u) {
      // Register is already marked as blocked by the `block_registers_for_call_interval_`.
      return;
    }
  }
  DCHECK(location.IsRegister() || location.IsFpuRegister());
  LiveInterval* interval = location.IsRegister()
      ? physical_core_register_intervals_[reg]
      : physical_fp_register_intervals_[reg];
  DataType::Type type = location.IsRegister()
      ? DataType::Type::kInt32
      : DataType::Type::kFloat32;
  if (interval == nullptr) {
    interval = LiveInterval::MakeFixedInterval(allocator_, reg, type);
    if (location.IsRegister()) {
      physical_core_register_intervals_[reg] = interval;
    } else {
      physical_fp_register_intervals_[reg] = interval;
    }
  }
  DCHECK(interval->GetRegister() == reg);
  interval->AddRange(position, position + kLivenessPositionsToBlock);
}

void RegisterAllocatorLinearScan::AllocateRegistersInternal() {
  // Iterate post-order, to ensure the list is sorted, and the last added interval
  // is the one with the lowest start position.
  for (HBasicBlock* block : codegen_->GetGraph()->GetLinearPostOrder()) {
    for (HBackwardInstructionIteratorPrefetchNext back_it(block->GetInstructions());
         !back_it.Done();
         back_it.Advance()) {
      ProcessInstruction(back_it.Current());
    }
    for (HInstructionIteratorPrefetchNext inst_it(block->GetPhis()); !inst_it.Done();
         inst_it.Advance()) {
      ProcessInstruction(inst_it.Current());
    }

    if (block->IsCatchBlock() ||
        (block->IsLoopHeader() && block->GetLoopInformation()->IsIrreducible())) {
      // By blocking all registers at the top of each catch block or irreducible loop, we force
      // intervals belonging to the live-in set of the catch/header block to be spilled.
      // TODO(ngeoffray): Phis in this block could be allocated in register.
      size_t position = block->GetLifetimeStart();
      DCHECK_EQ(liveness_.GetInstructionFromPosition(position / kLivenessPositionsPerInstruction),
                nullptr);
      block_registers_special_interval_->AddRange(position, position + kLivenessPositionsToBlock);
    }
  }

  // Add the current method to the `reserved_out_slots_`. ArtMethod* takes 2 vregs for 64 bits.
  PointerSize pointer_size = InstructionSetPointerSize(codegen_->GetInstructionSet());
  reserved_out_slots_ += static_cast<size_t>(pointer_size) / kVRegSize;

  // Most methods have some core register intervals, so run the core register pass unconditionally.
  LinearScan(this, RegisterType::kCoreRegister).Run();
  // Most methods do not have any FP register intervals, so try to avoid the overhead
  // of constructing the `LinearScan` object for the FP registers pass.
  if (!unhandled_fp_intervals_.empty()) {
    LinearScan(this, RegisterType::kFpRegister).Run();
  }
}

RegisterAllocatorLinearScan::LinearScan::LinearScan(
    RegisterAllocatorLinearScan* register_allocator,
    RegisterType register_type,
    CodeGenerator* codegen,
    ScopedArenaAllocator* allocator)
    : codegen_(codegen),
      number_of_registers_(GetNumberOfRegisters(codegen, register_type)),
      register_type_(register_type),
      registers_blocked_for_call_(GetRegistersBlockedForCall(register_allocator, register_type)),
      blocked_registers_(GetBlockedRegisters(codegen, register_type)),
      spill_slots_(GetSpillSlots(register_allocator, register_type)),
      wide_spill_slots_(GetWideSpillSlots(register_allocator, register_type)),
      unhandled_(TakeUnhandledIntervals(register_allocator, register_type)),
      handled_(allocator->Adapter(kArenaAllocRegisterAllocator)),
      active_(allocator->Adapter(kArenaAllocRegisterAllocator)),
      inactive_(allocator->Adapter(kArenaAllocRegisterAllocator)),
      instructions_from_positions_(register_allocator->liveness_.GetInstructionsFromPositions()),
      registers_array_(
          allocator->AllocArray<size_t>(number_of_registers_, kArenaAllocRegisterAllocator)) {
  // Add intervals representing groups of physical registers blocked for calls,
  // catch blocks and irreducible loop headers.
  LiveInterval* block_registers_intervals[] = {
      register_allocator->block_registers_for_call_interval_,
      register_allocator->block_registers_special_interval_
  };
  for (LiveInterval* block_registers_interval : block_registers_intervals) {
    if (block_registers_interval->GetFirstRange() != nullptr) {
      block_registers_interval->ResetSearchCache();
      inactive_.push_back(block_registers_interval);
    }
  }
  for (LiveInterval* fixed : *GetPhysicalRegisterIntervals(register_allocator, register_type)) {
    if (fixed != nullptr) {
      // Fixed interval is added to inactive_ instead of unhandled_.
      // It's also the only type of inactive interval whose start position
      // can be after the current interval during linear scan.
      // Fixed interval is never split and never moves to unhandled_.
      inactive_.push_back(fixed);
    }
  }
}

void RegisterAllocatorLinearScan::ProcessInstruction(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();

  // Check for early returns.
  if (locations == nullptr) {
    return;
  }
  if (TryRemoveSuspendCheckEntry(instruction)) {
    return;
  }

  if (locations->CanCall()) {
    // Update the `reserved_out_slots_` for invokes that make a call, including intrinsics
    // that make the call only on the slow-path. Same for the `HStringBuilderAppend`.
    if (instruction->IsInvoke()) {
      reserved_out_slots_ = std::max<size_t>(
          reserved_out_slots_, instruction->AsInvoke()->GetNumberOfOutVRegs());
    } else if (instruction->IsStringBuilderAppend()) {
      reserved_out_slots_ = std::max<size_t>(
          reserved_out_slots_, instruction->AsStringBuilderAppend()->GetNumberOfOutVRegs());
    }
  }
  bool will_call = locations->WillCall();
  if (will_call) {
    // If a call will happen, add the range to a fixed interval that represents all the
    // caller-save registers blocked at call sites.
    const size_t position = instruction->GetLifetimePosition();
    DCHECK_NE(liveness_.GetInstructionFromPosition(position / kLivenessPositionsPerInstruction),
              nullptr);
    block_registers_for_call_interval_->AddRange(position, position + kLivenessPositionsToBlock);
  }
  CheckForTempLiveIntervals(instruction, will_call);
  CheckForSafepoint(instruction);
  CheckForFixedInputs(instruction, will_call);

  LiveInterval* current = instruction->GetLiveInterval();
  if (current == nullptr)
    return;

  const bool core_register = !DataType::IsFloatingPointType(instruction->GetType());
  ScopedArenaVector<LiveInterval*>& unhandled =
      core_register ? unhandled_core_intervals_ : unhandled_fp_intervals_;

  DCHECK(unhandled.empty() || current->StartsBeforeOrAt(unhandled.back()));

  if (codegen_->NeedsTwoRegisters(current->GetType())) {
    current->AddHighInterval();
  }

  AddSafepointsFor(instruction);
  current->ResetSearchCache();
  CheckForFixedOutput(instruction, will_call);

  if (instruction->IsPhi() && instruction->AsPhi()->IsCatchPhi()) {
    AllocateSpillSlotForCatchPhi(instruction->AsPhi());
  }

  // If needed, add interval to the list of unhandled intervals.
  if (current->HasSpillSlot() || instruction->IsConstant()) {
    // Split just before first register use.
    size_t first_register_use = current->FirstRegisterUse();
    if (first_register_use != kNoLifetime) {
      LiveInterval* split = SplitBetween(current,
                                         current->GetStart(),
                                         first_register_use - 1,
                                         liveness_.GetInstructionsFromPositions());
      // Don't add directly to `unhandled`, it needs to be sorted and the start
      // of this new interval might be after intervals already in the list.
      AddSorted(&unhandled, split);
    } else {
      // Nothing to do, we won't allocate a register for this value.
    }
  } else {
    // Don't add directly to `unhandled`, temp or safepoint intervals
    // for this instruction may have been added, and those can be
    // processed first.
    AddSorted(&unhandled, current);
  }
}

bool RegisterAllocatorLinearScan::TryRemoveSuspendCheckEntry(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  if (instruction->IsSuspendCheckEntry() && !codegen_->NeedsSuspendCheckEntry()) {
    // TODO: We do this here because we do not want the suspend check to artificially
    // create live registers. We should find another place, but this is currently the
    // simplest.
    DCHECK_EQ(locations->GetTempCount(), 0u);
    instruction->GetBlock()->RemoveInstruction(instruction);
    return true;
  }
  return false;
}

void RegisterAllocatorLinearScan::CheckForTempLiveIntervals(HInstruction* instruction,
                                                            bool will_call) {
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();

  // Create synthesized intervals for temporaries.
  for (size_t i = 0; i < locations->GetTempCount(); ++i) {
    Location temp = locations->GetTemp(i);
    if (temp.IsRegister() || temp.IsFpuRegister()) {
      BlockRegister(temp, position, will_call);
      // Ensure that an explicit temporary register is marked as being allocated.
      codegen_->AddAllocatedRegister(temp);
    } else {
      DCHECK(temp.IsUnallocated());
      switch (temp.GetPolicy()) {
        case Location::kRequiresRegister: {
          LiveInterval* interval =
              LiveInterval::MakeTempInterval(allocator_, DataType::Type::kInt32, i, position);
          temp_intervals_.push_back(interval);
          unhandled_core_intervals_.push_back(interval);
          break;
        }

        case Location::kRequiresFpuRegister: {
          LiveInterval* interval =
              LiveInterval::MakeTempInterval(allocator_, DataType::Type::kFloat64, i, position);
          temp_intervals_.push_back(interval);
          if (codegen_->NeedsTwoRegisters(DataType::Type::kFloat64)) {
            interval->AddHighTempInterval();
            LiveInterval* high = interval->GetHighInterval();
            temp_intervals_.push_back(high);
            unhandled_fp_intervals_.push_back(high);
          }
          unhandled_fp_intervals_.push_back(interval);
          break;
        }

        default:
          LOG(FATAL) << "Unexpected policy for temporary location " << temp.GetPolicy();
      }
    }
  }
}

void RegisterAllocatorLinearScan::CheckForSafepoint(HInstruction* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  if (locations->NeedsSafepoint()) {
    safepoints_.push_back(instruction);
  }
}

void RegisterAllocatorLinearScan::CheckForFixedInputs(HInstruction* instruction, bool will_call) {
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();
  for (size_t i = 0; i < locations->GetInputCount(); ++i) {
    Location input = locations->InAt(i);
    if (input.IsRegister() || input.IsFpuRegister()) {
      BlockRegister(input, position, will_call);
      // Ensure that an explicit input register is marked as being allocated.
      codegen_->AddAllocatedRegister(input);
    } else if (input.IsPair()) {
      BlockRegister(input.ToLow(), position, will_call);
      BlockRegister(input.ToHigh(), position, will_call);
      // Ensure that an explicit input register pair is marked as being allocated.
      codegen_->AddAllocatedRegister(input.ToLow());
      codegen_->AddAllocatedRegister(input.ToHigh());
    }
  }
}

void RegisterAllocatorLinearScan::AddSafepointsFor(HInstruction* instruction) {
  LiveInterval* current = instruction->GetLiveInterval();
  SafepointPositionList list;
  auto before = list.before_begin();
  for (size_t safepoint_index = safepoints_.size(); safepoint_index > 0; --safepoint_index) {
    HInstruction* safepoint = safepoints_[safepoint_index - 1u];
    size_t safepoint_position = SafepointPosition::ComputePosition(safepoint);

    // Test that safepoints are ordered in the optimal way.
    DCHECK(safepoint_index == safepoints_.size() ||
           safepoints_[safepoint_index]->GetLifetimePosition() < safepoint_position);

    if (safepoint_position == current->GetStart()) {
      // The safepoint is for this instruction, so the location of the instruction
      // does not need to be saved.
      DCHECK_EQ(safepoint_index, safepoints_.size());
      DCHECK_EQ(safepoint, instruction);
      continue;
    } else if (current->IsDeadAt(safepoint_position)) {
      break;
    } else if (!current->Covers(safepoint_position)) {
      // Hole in the interval.
      continue;
    }
    before = list.insert_after(before, *current->CreateSafepointPosition(safepoint));
  }
  current->SetSafepointPositions(std::move(list));
}

void RegisterAllocatorLinearScan::CheckForFixedOutput(HInstruction* instruction, bool will_call) {
  LocationSummary* locations = instruction->GetLocations();
  size_t position = instruction->GetLifetimePosition();
  LiveInterval* current = instruction->GetLiveInterval();
  // Some instructions define their output in fixed register/stack slot. We need
  // to ensure we know these locations before doing register allocation. For a
  // given register, we create an interval that covers these locations. The register
  // will be unavailable at these locations when trying to allocate one for an
  // interval.
  //
  // The backwards walking ensures the ranges are ordered on increasing start positions.
  Location output = locations->Out();
  if (output.IsUnallocated() && output.GetPolicy() == Location::kSameAsFirstInput) {
    Location first = locations->InAt(0);
    if (first.IsRegister() || first.IsFpuRegister()) {
      current->SetFrom(position + kLivenessPositionOfFixedOutput);
      current->SetRegister(first.reg());
    } else if (first.IsPair()) {
      current->SetFrom(position + kLivenessPositionOfFixedOutput);
      current->SetRegister(first.low());
      LiveInterval* high = current->GetHighInterval();
      high->SetRegister(first.high());
      high->SetFrom(position + kLivenessPositionOfFixedOutput);
    }
  } else if (output.IsRegister() || output.IsFpuRegister()) {
    // Shift the interval's start by one to account for the blocked register.
    current->SetFrom(position + kLivenessPositionOfFixedOutput);
    current->SetRegister(output.reg());
    BlockRegister(output, position, will_call);
    // Ensure that an explicit output register is marked as being allocated.
    codegen_->AddAllocatedRegister(output);
  } else if (output.IsPair()) {
    current->SetFrom(position + kLivenessPositionOfFixedOutput);
    current->SetRegister(output.low());
    LiveInterval* high = current->GetHighInterval();
    high->SetRegister(output.high());
    high->SetFrom(position + kLivenessPositionOfFixedOutput);
    BlockRegister(output.ToLow(), position, will_call);
    BlockRegister(output.ToHigh(), position, will_call);
    // Ensure that an explicit output register pair is marked as being allocated.
    codegen_->AddAllocatedRegister(output.ToLow());
    codegen_->AddAllocatedRegister(output.ToHigh());
  } else if (output.IsStackSlot() || output.IsDoubleStackSlot()) {
    current->SetSpillSlot(output.GetStackIndex());
  } else {
    DCHECK(output.IsUnallocated() || output.IsConstant());
  }
}

class AllRangesIterator : public ValueObject {
 public:
  explicit AllRangesIterator(LiveInterval* interval)
      : current_interval_(interval),
        current_range_(interval->GetFirstRange()) {}

  bool Done() const { return current_interval_ == nullptr; }
  LiveRange* CurrentRange() const { return current_range_; }
  LiveInterval* CurrentInterval() const { return current_interval_; }

  void Advance() {
    current_range_ = current_range_->GetNext();
    if (current_range_ == nullptr) {
      current_interval_ = current_interval_->GetNextSibling();
      if (current_interval_ != nullptr) {
        current_range_ = current_interval_->GetFirstRange();
      }
    }
  }

 private:
  LiveInterval* current_interval_;
  LiveRange* current_range_;

  DISALLOW_COPY_AND_ASSIGN(AllRangesIterator);
};

inline size_t RegisterAllocatorLinearScan::GetNumberOfSpillSlots() const {
  return int_spill_slots_.size() +
         long_spill_slots_.size() +
         float_spill_slots_.size() +
         double_spill_slots_.size() +
         catch_phi_spill_slots_;
}

bool RegisterAllocatorLinearScan::ValidateInternal(RegisterType current_register_type,
                                                   bool log_fatal_on_failure) const {
  auto should_process = [current_register_type](LiveInterval* interval) {
    if (interval == nullptr) {
      return false;
    }
    RegisterType register_type = DataType::IsFloatingPointType(interval->GetType())
        ? RegisterType::kFpRegister
        : RegisterType::kCoreRegister;
    return register_type == current_register_type;
  };

  // To simplify unit testing, we eagerly create the array of intervals, and
  // call the helper method.
  ScopedArenaAllocator allocator(allocator_->GetArenaStack());
  ScopedArenaVector<LiveInterval*> intervals(
      allocator.Adapter(kArenaAllocRegisterAllocatorValidate));
  for (size_t i = 0; i < liveness_.GetNumberOfSsaValues(); ++i) {
    HInstruction* instruction = liveness_.GetInstructionFromSsaIndex(i);
    if (should_process(instruction->GetLiveInterval())) {
      intervals.push_back(instruction->GetLiveInterval());
    }
  }

  for (LiveInterval* block_registers_interval : { block_registers_for_call_interval_,
                                                  block_registers_special_interval_ }) {
    if (block_registers_interval->GetFirstRange() != nullptr) {
      intervals.push_back(block_registers_interval);
    }
  }
  const ScopedArenaVector<LiveInterval*>* physical_register_intervals =
      (current_register_type == RegisterType::kCoreRegister)
          ? &physical_core_register_intervals_
          : &physical_fp_register_intervals_;
  for (LiveInterval* fixed : *physical_register_intervals) {
    if (fixed != nullptr) {
      intervals.push_back(fixed);
    }
  }

  for (LiveInterval* temp : temp_intervals_) {
    if (should_process(temp)) {
      intervals.push_back(temp);
    }
  }

  return ValidateIntervals(ArrayRef<LiveInterval* const>(intervals),
                           GetNumberOfSpillSlots(),
                           reserved_out_slots_,
                           *codegen_,
                           &liveness_,
                           current_register_type,
                           log_fatal_on_failure);
}

void RegisterAllocatorLinearScan::LinearScan::DumpInterval(std::ostream& stream,
                                                           LiveInterval* interval) const {
  interval->Dump(stream);
  stream << ": ";
  if (interval->HasRegister()) {
    if (interval->IsFloatingPoint()) {
      codegen_->DumpFloatingPointRegister(stream, interval->GetRegister());
    } else {
      codegen_->DumpCoreRegister(stream, interval->GetRegister());
    }
  } else if (interval->IsFixed()) {
    DCHECK_EQ(interval->GetType(), DataType::Type::kVoid);
    size_t start = interval->GetFirstRange()->GetStart();
    bool blocked_for_call =
        instructions_from_positions_[start / kLivenessPositionsPerInstruction] != nullptr;
    stream << (blocked_for_call ? "block-for-call" : "block-special");
  } else {
    stream << "spilled";
  }
  stream << std::endl;
}

void RegisterAllocatorLinearScan::LinearScan::DumpAllIntervals(std::ostream& stream) const {
  stream << "inactive: " << std::endl;
  for (LiveInterval* inactive_interval : inactive_) {
    DumpInterval(stream, inactive_interval);
  }
  stream << "active: " << std::endl;
  for (LiveInterval* active_interval : active_) {
    DumpInterval(stream, active_interval);
  }
  stream << "unhandled: " << std::endl;
  for (LiveInterval* unhandled_interval : unhandled_) {
    DumpInterval(stream, unhandled_interval);
  }
  stream << "handled: " << std::endl;
  for (LiveInterval* handled_interval : handled_) {
    DumpInterval(stream, handled_interval);
  }
}

// By the book implementation of a linear scan register allocator.
void RegisterAllocatorLinearScan::LinearScan::Run() {
  size_t last_position = std::numeric_limits<size_t>::max();
  while (!unhandled_.empty()) {
    // Remove interval with the lowest start position from unhandled.
    LiveInterval* current = unhandled_.back();
    unhandled_.pop_back();

    // Make sure the interval is an expected state.
    DCHECK(!current->IsFixed() && !current->HasSpillSlot());
    // Make sure we are going in the right order.
    DCHECK(unhandled_.empty() || unhandled_.back()->GetStart() >= current->GetStart());
    // Make sure a low interval is always with a high.
    DCHECK_IMPLIES(current->IsLowInterval(), unhandled_.back()->IsHighInterval());
    // Make sure a high interval is always with a low.
    DCHECK(current->IsLowInterval() ||
           unhandled_.empty() ||
           !unhandled_.back()->IsHighInterval());

    size_t position = current->GetStart();
    if (position != last_position) {
      // Remember the inactive_ size here since the ones moved to inactive_ from
      // active_ below shouldn't need to be re-checked.
      size_t inactive_intervals_to_handle = inactive_.size();

      // Remove currently active intervals that are dead at this position.
      // Move active intervals that have a lifetime hole at this position to inactive.
      auto active_kept_end = std::remove_if(
          active_.begin(),
          active_.end(),
          [this, position](LiveInterval* interval) {
            if (interval->IsDeadAt(position)) {
              handled_.push_back(interval);
              return true;
            } else if (!interval->Covers(position)) {
              inactive_.push_back(interval);
              return true;
            } else {
              return false;  // Keep this interval.
            }
          });
      active_.erase(active_kept_end, active_.end());

      // Remove currently inactive intervals that are dead at this position.
      // Move inactive intervals that cover this position to active.
      auto inactive_to_handle_end = inactive_.begin() + inactive_intervals_to_handle;
      auto inactive_kept_end = std::remove_if(
          inactive_.begin(),
          inactive_to_handle_end,
          [this, position](LiveInterval* interval) {
            DCHECK(interval->GetStart() < position || interval->IsFixed());
            if (interval->IsDeadAt(position)) {
              handled_.push_back(interval);
              return true;
            } else if (interval->Covers(position)) {
              active_.push_back(interval);
              return true;
            } else {
              return false;  // Keep this interval.
            }
          });
      inactive_.erase(inactive_kept_end, inactive_to_handle_end);

      last_position = position;
    } else {
      // Active and inactive intervals should not change for the same position.
      DCHECK(std::none_of(active_.begin(),
                          active_.end(),
                          [position](LiveInterval* interval) {
                            return interval->IsDeadAt(position) || !interval->Covers(position);
                          }));
      DCHECK(std::none_of(inactive_.begin(),
                          inactive_.end(),
                          [position](LiveInterval* interval) {
                            return interval->IsDeadAt(position) || interval->Covers(position);
                          }));
    }

    if (current->IsHighInterval() && !current->GetLowInterval()->HasRegister()) {
      DCHECK(!current->HasRegister());
      // Allocating the low part was unsucessful. The splitted interval for the high part
      // will be handled next (it is in the `unhandled_` list).
      continue;
    }

    // For a Phi which has all inputs in the same spill slot as its spill slot hint, use that hint.
    if (com::android::art::flags::reg_alloc_spill_slot_reuse() &&
        current->HasSpillSlotHint() &&
        TryUsingSpillSlotHint(current)) {
      continue;
    }

    // Try to find an available register.
    bool success = TryAllocateFreeReg(current);

    // If no register could be found, we need to spill.
    if (!success) {
      success = AllocateBlockedReg(current);
    }

    // If the interval had a register allocated, add it to the list of active intervals.
    if (success) {
      codegen_->AddAllocatedRegister((register_type_ == RegisterType::kCoreRegister)
          ? Location::RegisterLocation(current->GetRegister())
          : Location::FpuRegisterLocation(current->GetRegister()));
      active_.push_back(current);
      if (current->HasHighInterval() && !current->GetHighInterval()->HasRegister()) {
        current->GetHighInterval()->SetRegister(GetHighForLowRegister(current->GetRegister()));
      }
    }
  }
}

static void FreeIfNotCoverAt(LiveInterval* interval, size_t position, ArrayRef<size_t> free_until) {
  DCHECK(!interval->IsHighInterval());
  // Note that the same instruction may occur multiple times in the input list,
  // so `free_until` may have changed already.
  // Since `position` is not the current scan position, we need to use CoversSlow.
  if (interval->IsDeadAt(position)) {
    // Set the register to be free. Note that inactive intervals might later
    // update this.
    free_until[interval->GetRegister()] = kMaxLifetimePosition;
    if (interval->HasHighInterval()) {
      DCHECK(interval->GetHighInterval()->IsDeadAt(position));
      free_until[interval->GetHighInterval()->GetRegister()] = kMaxLifetimePosition;
    }
  } else if (!interval->CoversSlow(position)) {
    // The interval becomes inactive at `defined_by`. We make its register
    // available only until the next use strictly after `defined_by`.
    free_until[interval->GetRegister()] = interval->FirstUseAfter(position);
    if (interval->HasHighInterval()) {
      DCHECK(!interval->GetHighInterval()->CoversSlow(position));
      free_until[interval->GetHighInterval()->GetRegister()] = free_until[interval->GetRegister()];
    }
  }
}

bool RegisterAllocatorLinearScan::LinearScan::TryUsingSpillSlotHint(LiveInterval* current) {
  DCHECK(current->HasSpillSlotHint());
  int hint = current->GetSpillSlotHint();
  DCHECK(current->GetDefinedBy() != nullptr);
  HBasicBlock* block = current->GetDefinedBy()->GetBlock();
  DCHECK(current->GetDefinedBy()->IsPhi());
  auto inputs = current->GetDefinedBy()->AsPhi()->GetInputs();
  DCHECK_EQ(inputs.size(), block->GetPredecessors().size());

  // Check if all inputs have the same spill slot as `hint` and that none of them
  // has a register allocated before the incoming edge.
  for (auto [predecessor, input_index] : ZipCount(block->GetPredecessors())) {
    DCHECK_EQ(predecessor->GetNormalSuccessors().size(), 1u);
    LiveInterval* input_li = inputs[input_index]->GetLiveInterval();
    if (input_li->GetSpillSlot() != hint ||
        input_li->GetSiblingAt(predecessor->GetLifetimeEnd() - 1)->HasRegister()) {
      return false;
    }
  }

  // Check that the required spill slots are available at the start of the `current` interval.
  ScopedArenaVector<SpillSlotData>* spill_slots = GetSpillSlotsForType(current->GetType());
  size_t number_of_spill_slots_needed = current->NumberOfSpillSlotsNeeded();
  DCHECK_LE(hint + number_of_spill_slots_needed, spill_slots->size());
  DCHECK(current->GetParent() == current);
  size_t start = current->GetStart();
  size_t end = current->GetLastSibling()->GetEnd();
  ArrayRef<SpillSlotData> range =
      ArrayRef<SpillSlotData>(*spill_slots).SubArray(hint, number_of_spill_slots_needed);
  if (std::any_of(range.begin(),
                  range.end(),
                  [=](const SpillSlotData& data) { return data.GetEnd() > start; })) {
    return false;
  }

  // Use the spill slots and split the `current` interval if there is any register use.
  SpillSlotData new_data(current, end);
  for (SpillSlotData& data : range) {
    DCHECK_LE(data.GetEnd(), start);
    data = new_data;
  }
  current->SetSpillSlot(hint);
  size_t first_register_use = current->FirstRegisterUse();
  if (first_register_use != kNoLifetime) {
    LiveInterval* split = SplitBetween(current, current->GetStart(), first_register_use - 1);
    DCHECK(current != split);
    AddSorted(&unhandled_, split);
  }
  handled_.push_back(current);
  return true;
}

// Find a free register. If multiple are found, pick the register that
// is free the longest.
bool RegisterAllocatorLinearScan::LinearScan::TryAllocateFreeReg(LiveInterval* current) {
  // First set all registers to be free.
  ArrayRef<size_t> free_until = GetRegistersArray();
  std::fill_n(free_until.begin(), free_until.size(), kMaxLifetimePosition);

  // For each active interval, set its register(s) to not free.
  for (LiveInterval* interval : active_) {
    DCHECK(interval->HasRegister() || interval->IsFixed());
    uint32_t register_mask = GetRegisterMask(interval);
    DCHECK_NE(register_mask, 0u);
    for (uint32_t reg : LowToHighBits(register_mask)) {
      free_until[reg] = 0;
    }
  }

  // An interval that starts an instruction (that is, it is not split), may
  // re-use the registers used by the inputs of that instruciton, based on the
  // location summary.
  HInstruction* defined_by = current->GetDefinedBy();
  if (defined_by != nullptr && !current->IsSplit()) {
    LocationSummary* locations = defined_by->GetLocations();
    if (!locations->OutputCanOverlapWithInputs() && locations->Out().IsUnallocated()) {
      HInputsRef inputs = defined_by->GetInputs();
      for (size_t i = 0; i < inputs.size(); ++i) {
        if (locations->InAt(i).IsValid()) {
          // Take the last interval of the input. It is the location of that interval
          // that will be used at `defined_by`.
          LiveInterval* interval = inputs[i]->GetLiveInterval()->GetLastSibling();
          // Note that interval may have not been processed yet.
          // TODO: Handle non-split intervals last in the work list.
          if (interval->HasRegister() && interval->SameRegisterKind(*current)) {
            // The input must be live until the end of `defined_by`, to comply to
            // the linear scan algorithm. So we use `defined_by`'s end lifetime
            // position to check whether the input is dead or is inactive after
            // `defined_by`.
            DCHECK(interval->CoversSlow(defined_by->GetLifetimePosition()));
            size_t position = defined_by->GetLifetimePosition() + kLivenessPositionOfNormalUse;
            FreeIfNotCoverAt(interval, position, free_until);
          }
        }
      }
    }
  }

  // For each inactive interval, set its register to be free until
  // the next intersection with `current`.
  for (LiveInterval* inactive : inactive_) {
    // Temp/Slow-path-safepoint interval has no holes.
    DCHECK(!inactive->IsTemp());
    if (!current->IsSplit() && !inactive->IsFixed()) {
      // Neither current nor inactive are fixed.
      // Thanks to SSA, a non-split interval starting in a hole of an
      // inactive interval should never intersect with that inactive interval.
      // Only if it's not fixed though, because fixed intervals don't come from SSA.
      DCHECK_EQ(inactive->FirstIntersectionWith(current), kNoLifetime);
      continue;
    }

    DCHECK(inactive->HasRegister() || inactive->IsFixed());
    uint32_t register_mask = GetRegisterMask(inactive);
    DCHECK_NE(register_mask, 0u);
    for (uint32_t reg : LowToHighBits(register_mask)) {
      if (free_until[reg] == 0) {
        // Already used by some active interval. Clear the register bit.
        register_mask &= ~(1u << reg);
      }
    }
    if (register_mask != 0u) {
      size_t next_intersection = inactive->FirstIntersectionWith(current);
      if (next_intersection != kNoLifetime) {
        for (uint32_t reg : LowToHighBits(register_mask)) {
          free_until[reg] = std::min(free_until[reg], next_intersection);
        }
      }
    }
  }

  int reg = kNoRegister;
  if (current->HasRegister()) {
    // Some instructions have a fixed register output.
    reg = current->GetRegister();
    if (free_until[reg] == 0) {
      DCHECK(current->IsHighInterval());
      // AllocateBlockedReg will spill the holder of the register.
      return false;
    }
  } else {
    DCHECK(!current->IsHighInterval());
    int hint = current->FindFirstRegisterHint(free_until, instructions_from_positions_);
    if ((hint != kNoRegister)
        // For simplicity, if the hint we are getting for a pair cannot be used,
        // we are just going to allocate a new pair.
        && !(current->IsLowInterval() && IsBlocked(GetHighForLowRegister(hint)))) {
      DCHECK(!IsBlocked(hint));
      reg = hint;
    } else if (current->IsLowInterval()) {
      reg = FindAvailableRegisterPair(free_until, current->GetStart());
    } else {
      reg = FindAvailableRegister(free_until, current);
    }
  }

  DCHECK_NE(reg, kNoRegister);
  // If we could not find a register, we need to spill.
  if (free_until[reg] == 0) {
    return false;
  }

  if (current->IsLowInterval()) {
    // If the high register of this interval is not available, we need to spill.
    int high_reg = current->GetHighInterval()->GetRegister();
    if (high_reg == kNoRegister) {
      high_reg = GetHighForLowRegister(reg);
    }
    if (free_until[high_reg] == 0) {
      return false;
    }
  }

  current->SetRegister(reg);
  if (!current->IsDeadAt(free_until[reg])) {
    // If the register is only available for a subset of live ranges
    // covered by `current`, split `current` before the position where
    // the register is not available anymore.
    LiveInterval* split = SplitBetween(current, current->GetStart(), free_until[reg]);
    DCHECK(split != nullptr);
    AddSorted(&unhandled_, split);
  }
  return true;
}

int RegisterAllocatorLinearScan::LinearScan::FindAvailableRegisterPair(ArrayRef<size_t> next_use,
                                                                       size_t starting_at) const {
  int reg = kNoRegister;
  // Pick the register pair that is used the last.
  for (size_t i : Range(number_of_registers_)) {
    if (IsBlocked(i)) continue;
    if (!IsLowRegister(i)) continue;
    int high_register = GetHighForLowRegister(i);
    if (IsBlocked(high_register)) continue;
    int existing_high_register = GetHighForLowRegister(reg);
    if ((reg == kNoRegister) || (next_use[i] >= next_use[reg]
                        && next_use[high_register] >= next_use[existing_high_register])) {
      reg = i;
      if (next_use[i] == kMaxLifetimePosition
          && next_use[high_register] == kMaxLifetimePosition) {
        break;
      }
    } else if (next_use[reg] <= starting_at || next_use[existing_high_register] <= starting_at) {
      // If one of the current register is known to be unavailable, just unconditionally
      // try a new one.
      reg = i;
    }
  }
  return reg;
}

int RegisterAllocatorLinearScan::LinearScan::FindAvailableRegister(ArrayRef<size_t> next_use,
                                                                   LiveInterval* current) const {
  // We special case intervals that do not span a safepoint to try to find a caller-save
  // register if one is available. We iterate from 0 to the number of registers,
  // so if there are caller-save registers available at the end, we continue the iteration.
  bool prefers_caller_save = !current->HasWillCallSafepoint();
  int reg = kNoRegister;
  for (size_t i : Range(number_of_registers_)) {
    if (IsBlocked(i)) {
      // Register cannot be used. Continue.
      continue;
    }

    // Best case: we found a register fully available.
    if (next_use[i] == kMaxLifetimePosition) {
      if (prefers_caller_save && !IsCallerSaveRegister(i)) {
        // We can get shorter encodings on some platforms by using
        // small register numbers. So only update the candidate if the previous
        // one was not available for the whole method.
        if (reg == kNoRegister || next_use[reg] != kMaxLifetimePosition) {
          reg = i;
        }
        // Continue the iteration in the hope of finding a caller save register.
        continue;
      } else {
        reg = i;
        // We know the register is good enough. Return it.
        break;
      }
    }

    // If we had no register before, take this one as a reference.
    if (reg == kNoRegister) {
      reg = i;
      continue;
    }

    // Pick the register that is used the last.
    if (next_use[i] > next_use[reg]) {
      reg = i;
      continue;
    }
  }
  return reg;
}

// Remove interval and its other half if any. Return iterator to the following element.
static ArenaVector<LiveInterval*>::iterator RemoveIntervalAndPotentialOtherHalf(
    ScopedArenaVector<LiveInterval*>* intervals, ScopedArenaVector<LiveInterval*>::iterator pos) {
  DCHECK(intervals->begin() <= pos && pos < intervals->end());
  LiveInterval* interval = *pos;
  if (interval->IsLowInterval()) {
    DCHECK(pos + 1 < intervals->end());
    DCHECK_EQ(*(pos + 1), interval->GetHighInterval());
    return intervals->erase(pos, pos + 2);
  } else if (interval->IsHighInterval()) {
    DCHECK(intervals->begin() < pos);
    DCHECK_EQ(*(pos - 1), interval->GetLowInterval());
    return intervals->erase(pos - 1, pos + 1);
  } else {
    return intervals->erase(pos);
  }
}

bool RegisterAllocatorLinearScan::LinearScan::TrySplitNonPairOrUnalignedPairIntervalAt(
    size_t position, size_t first_register_use, ArrayRef<size_t> next_use) {
  for (auto it = active_.begin(), end = active_.end(); it != end; ++it) {
    LiveInterval* active = *it;
    // Special fixed intervals that represent multiple registers do not report having a register.
    if (active->IsFixed()) continue;
    DCHECK(active->HasRegister());
    if (active->IsHighInterval()) continue;
    if (first_register_use > next_use[active->GetRegister()]) continue;

    // Split the first interval found that is either:
    // 1) A non-pair interval.
    // 2) A pair interval whose high is not low + 1.
    // 3) A pair interval whose low is not even.
    if (!active->IsLowInterval() ||
        IsLowOfUnalignedPairInterval(active) ||
        !IsLowRegister(active->GetRegister())) {
      LiveInterval* split = Split(active, position);
      if (split != active) {
        handled_.push_back(active);
      }
      RemoveIntervalAndPotentialOtherHalf(&active_, it);
      AddSorted(&unhandled_, split);
      return true;
    }
  }
  return false;
}

// Find the register that is used the last, and spill the interval
// that holds it. If the first use of `current` is after that register
// we spill `current` instead.
bool RegisterAllocatorLinearScan::LinearScan::AllocateBlockedReg(LiveInterval* current) {
  size_t first_register_use = current->FirstRegisterUse();
  if (current->HasRegister()) {
    DCHECK(current->IsHighInterval());
    // The low interval has allocated the register for the high interval. In
    // case the low interval had to split both intervals, we may end up in a
    // situation where the high interval does not have a register use anymore.
    // We must still proceed in order to split currently active and inactive
    // uses of the high interval's register, and put the high interval in the
    // active set.
    DCHECK_IMPLIES(first_register_use == kNoLifetime, current->GetNextSibling() != nullptr);
  } else if (first_register_use == kNoLifetime) {
    AllocateSpillSlotFor(current);
    return false;
  }

  // First set all registers as not being used.
  ArrayRef<size_t> next_use = GetRegistersArray();
  std::fill_n(next_use.begin(), next_use.size(), kMaxLifetimePosition);

  // For each active interval, find the next use of its register after the
  // start of current.
  for (LiveInterval* active : active_) {
    size_t use = current->GetStart();
    if (active->HasRegister()) {
      size_t reg = active->GetRegister();
      bool has_use_after = true;
      if (!active->IsFixed() && !active->IsTemp()) {
        use = active->FirstRegisterUseAfter(use);
        has_use_after = use != kNoLifetime;
      }
      if (has_use_after) {
        next_use[reg] = use;
      }
    } else {
      DCHECK(active->IsFixed());
      uint32_t register_mask = GetRegisterMask(active);
      DCHECK_NE(register_mask, 0u);
      for (uint32_t reg : LowToHighBits(register_mask)) {
        next_use[reg] = use;
      }
    }
  }

  // For each inactive interval, find the next use of its register after the
  // start of current.
  for (LiveInterval* inactive : inactive_) {
    // Temp/Slow-path-safepoint interval has no holes.
    DCHECK(!inactive->IsTemp());
    if (!current->IsSplit() && !inactive->IsFixed()) {
      // Neither current nor inactive are fixed.
      // Thanks to SSA, a non-split interval starting in a hole of an
      // inactive interval should never intersect with that inactive interval.
      // Only if it's not fixed though, because fixed intervals don't come from SSA.
      DCHECK_EQ(inactive->FirstIntersectionWith(current), kNoLifetime);
      continue;
    }
    DCHECK(inactive->HasRegister() || inactive->IsFixed());
    size_t next_intersection = inactive->FirstIntersectionWith(current);
    if (next_intersection != kNoLifetime) {
      if (inactive->IsFixed()) {
        uint32_t register_mask = GetRegisterMask(inactive);
        DCHECK_NE(register_mask, 0u);
        for (uint32_t reg : LowToHighBits(register_mask)) {
          next_use[reg] = std::min(next_intersection, next_use[reg]);
        }
      } else {
        size_t use = inactive->FirstUseAfter(current->GetStart());
        if (use != kNoLifetime) {
          next_use[inactive->GetRegister()] = std::min(use, next_use[inactive->GetRegister()]);
        }
      }
    }
  }

  int reg = kNoRegister;
  bool should_spill = false;
  if (current->HasRegister()) {
    DCHECK(current->IsHighInterval());
    reg = current->GetRegister();
    // When allocating the low part, we made sure the high register was available.
    DCHECK_LT(first_register_use, next_use[reg]);
  } else if (current->IsLowInterval()) {
    reg = FindAvailableRegisterPair(next_use, first_register_use);
    // We should spill if both registers are not available.
    should_spill = (first_register_use >= next_use[reg])
      || (first_register_use >= next_use[GetHighForLowRegister(reg)]);
  } else {
    DCHECK(!current->IsHighInterval());
    reg = FindAvailableRegister(next_use, current);
    should_spill = (first_register_use >= next_use[reg]);
  }

  DCHECK_NE(reg, kNoRegister);
  if (should_spill) {
    DCHECK(!current->IsHighInterval());
    bool is_allocation_at_use_site = (current->GetStart() >= (first_register_use - 1));
    if (is_allocation_at_use_site) {
      if (!current->IsLowInterval()) {
        DumpInterval(std::cerr, current);
        DumpAllIntervals(std::cerr);
        // This situation has the potential to infinite loop, so we make it a non-debug CHECK.
        HInstruction* at =
            instructions_from_positions_[first_register_use / kLivenessPositionsPerInstruction];
        CHECK(false) << "There is not enough registers available for "
          << current->GetParent()->GetDefinedBy()->DebugName() << " "
          << current->GetParent()->GetDefinedBy()->GetId()
          << " at " << first_register_use - 1 << " "
          << (at == nullptr ? "" : at->DebugName());
      }

      // If we're allocating a register for `current` because the instruction at
      // that position requires it, but we think we should spill, then there are
      // non-pair intervals or unaligned pair intervals blocking the allocation.
      // We split the first interval found, and put ourselves first in the
      // `unhandled_` list.
      bool success = TrySplitNonPairOrUnalignedPairIntervalAt(current->GetStart(),
                                                              first_register_use,
                                                              next_use);
      DCHECK(success);
      LiveInterval* existing = unhandled_.back();
      DCHECK(existing->IsHighInterval());
      DCHECK_EQ(existing->GetLowInterval(), current);
      unhandled_.push_back(current);
    } else {
      // If the first use of that instruction is after the last use of the found
      // register, we split this interval just before its first register use.
      AllocateSpillSlotFor(current);
      LiveInterval* split = SplitBetween(current, current->GetStart(), first_register_use - 1);
      DCHECK(current != split);
      AddSorted(&unhandled_, split);
    }
    return false;
  } else {
    // Use this register and spill the active and inactives interval that
    // have that register.
    current->SetRegister(reg);

    for (auto it = active_.begin(), end = active_.end(); it != end; ++it) {
      LiveInterval* active = *it;
      DCHECK_IMPLIES(active->IsFixed(), (GetRegisterMask(active) & (1u << reg)) == 0u);
      if (active->GetRegister() == reg) {
        DCHECK(!active->IsFixed());
        LiveInterval* split = Split(active, current->GetStart());
        if (split != active) {
          handled_.push_back(active);
        }
        RemoveIntervalAndPotentialOtherHalf(&active_, it);
        AddSorted(&unhandled_, split);
        break;
      }
    }

    // NOTE: Retrieve end() on each iteration because we're removing elements in the loop body.
    for (auto it = inactive_.begin(); it != inactive_.end(); ) {
      LiveInterval* inactive = *it;
      bool erased = false;
      if ((inactive->HasRegister() || inactive->IsFixed()) &&
          (GetRegisterMask(inactive) & (1u << reg)) != 0u) {
        if (!inactive->IsFixed() && !current->IsSplit()) {
          // Neither current nor inactive are fixed.
          // Thanks to SSA, a non-split interval starting in a hole of an
          // inactive interval should never intersect with that inactive interval.
          // Only if it's not fixed though, because fixed intervals don't come from SSA.
          DCHECK_EQ(inactive->FirstIntersectionWith(current), kNoLifetime);
        } else {
          size_t next_intersection = inactive->FirstIntersectionWith(current);
          if (next_intersection != kNoLifetime) {
            if (inactive->IsFixed()) {
              LiveInterval* split = Split(current, next_intersection);
              DCHECK_NE(split, current);
              AddSorted(&unhandled_, split);
            } else {
              // Split at the start of `current`, which will lead to splitting
              // at the end of the lifetime hole of `inactive`.
              LiveInterval* split = Split(inactive, current->GetStart());
              // If it's inactive, it must start before the current interval.
              DCHECK_NE(split, inactive);
              it = RemoveIntervalAndPotentialOtherHalf(&inactive_, it);
              erased = true;
              handled_.push_back(inactive);
              AddSorted(&unhandled_, split);
            }
          }
        }
      }
      // If we have erased the element, `it` already points to the next element.
      // Otherwise we need to move to the next element.
      if (!erased) {
        ++it;
      }
    }

    return true;
  }
}

void RegisterAllocatorLinearScan::AddSorted(ScopedArenaVector<LiveInterval*>* array,
                                            LiveInterval* interval) {
  DCHECK(!interval->IsFixed() && !interval->HasSpillSlot());
  size_t insert_at = 0;
  for (size_t i = array->size(); i > 0; --i) {
    LiveInterval* current = (*array)[i - 1u];
    // High intervals must be processed right after their low equivalent.
    if (current->StartsAfter(interval) && !current->IsHighInterval()) {
      insert_at = i;
      break;
    }
  }

  // Insert the high interval before the low, to ensure the low is processed before.
  auto insert_pos = array->begin() + insert_at;
  if (interval->HasHighInterval()) {
    array->insert(insert_pos, { interval->GetHighInterval(), interval });
  } else if (interval->HasLowInterval()) {
    array->insert(insert_pos, { interval, interval->GetLowInterval() });
  } else {
    array->insert(insert_pos, interval);
  }
}

void RegisterAllocatorLinearScan::LinearScan::AllocateSpillSlotFor(LiveInterval* interval) {
  if (interval->IsHighInterval()) {
    // The low interval already took care of allocating the spill slot.
    DCHECK(!interval->GetLowInterval()->HasRegister());
    DCHECK(interval->GetLowInterval()->GetParent()->HasSpillSlot());
    return;
  }

  LiveInterval* parent = interval->GetParent();

  // An instruction gets a spill slot for its entire lifetime. If the parent
  // of this interval already has a spill slot, there is nothing to do.
  if (parent->HasSpillSlot()) {
    return;
  }

  HInstruction* defined_by = parent->GetDefinedBy();
  DCHECK_IMPLIES(defined_by->IsPhi(), !defined_by->AsPhi()->IsCatchPhi());

  if (defined_by->IsParameterValue()) {
    // Parameters have their own stack slot.
    parent->SetSpillSlot(codegen_->GetStackSlotOfParameter(defined_by->AsParameterValue()));
    return;
  }

  if (defined_by->IsCurrentMethod()) {
    parent->SetSpillSlot(0);
    return;
  }

  if (defined_by->IsConstant()) {
    // Constants don't need a spill slot.
    return;
  }

  ScopedArenaVector<SpillSlotData>* spill_slots = GetSpillSlotsForType(interval->GetType());
  size_t number_of_spill_slots_needed = parent->NumberOfSpillSlotsNeeded();
  size_t start = parent->GetStart();
  size_t end = interval->GetLastSibling()->GetEnd();
  size_t slot = 0;
  bool used_hint = false;

  if (com::android::art::flags::reg_alloc_spill_slot_reuse()) {
    LiveInterval* hint_phi_interval = parent->GetHintPhiInterval();
    // If the immediate hint Phi does not have a spill hint, we could try to follow the
    // hint Phi chain to a Phi that does. However, we would need to make sure we don't go
    // over a Phi loop forever. And we would need to investigate if the additional spill
    // slot sharing we can find this way is worth the increase in compilation time.
    if (hint_phi_interval != nullptr && hint_phi_interval->HasSpillSlotOrHint()) {
      size_t hint = hint_phi_interval->GetSpillSlotHint();
      DCHECK_LE(hint + number_of_spill_slots_needed, spill_slots->size());
      ArrayRef<SpillSlotData> range =
          ArrayRef<SpillSlotData>(*spill_slots).SubArray(hint, number_of_spill_slots_needed);
      if (std::all_of(range.begin(),
                      range.end(),
                      [=](SpillSlotData& data) { return data.CanUseFor(parent, start, end); })) {
        // Update slots and use the hint.
        for (SpillSlotData& data : range) {
          data.UseFor(interval, end);
        }
        used_hint = true;
        slot = hint;
      }
    }
  }

  // Find first available spill slots.
  if (!used_hint) {
    for (size_t e = spill_slots->size(); slot < e; ++slot) {
      bool found = true;
      for (size_t s = slot, u = std::min(slot + number_of_spill_slots_needed, e); s < u; s++) {
        if ((*spill_slots)[s].GetEnd() > start) {
          found = false;  // failure
          break;
        }
      }
      if (found) {
        break;  // success
      }
    }

    // Need new spill slots?
    SpillSlotData new_data(interval, end);
    size_t num_old_slots = spill_slots->size() - slot;
    if (num_old_slots < number_of_spill_slots_needed) {
      spill_slots->resize(slot + number_of_spill_slots_needed, new_data);
      // Update only old slots below.
      number_of_spill_slots_needed = num_old_slots;
    }

    // Set slots to end.
    for (size_t s : Range(slot, slot + number_of_spill_slots_needed)) {
      SpillSlotData& data = (*spill_slots)[s];
      DCHECK_LE(data.GetEnd(), start);
      data = new_data;
    }
  }

  // Note that the exact spill slot location will be computed when we resolve,
  // that is when we know the number of spill slots for each type.
  parent->SetSpillSlot(slot);

  if (com::android::art::flags::reg_alloc_spill_slot_reuse()) {
    LiveInterval* hint_phi_interval = parent->GetHintPhiInterval();
    while (hint_phi_interval != nullptr && !hint_phi_interval->HasSpillSlotOrHint()) {
      hint_phi_interval->SetSpillSlotHint(slot);
      hint_phi_interval = hint_phi_interval->GetHintPhiInterval();
    }
  }
}

void RegisterAllocatorLinearScan::AllocateSpillSlotForCatchPhi(HPhi* phi) {
  LiveInterval* interval = phi->GetLiveInterval();

  HInstruction* previous_phi = phi->GetPrevious();
  DCHECK(previous_phi == nullptr || previous_phi->AsPhi()->GetRegNumber() <= phi->GetRegNumber())
      << "Phis expected to be sorted by vreg number, so that equivalent phis are adjacent.";

  if (phi->IsVRegEquivalentOf(previous_phi)) {
    // This is an equivalent of the previous phi. We need to assign the same
    // catch phi slot.
    DCHECK(previous_phi->GetLiveInterval()->HasSpillSlot());
    interval->SetSpillSlot(previous_phi->GetLiveInterval()->GetSpillSlot());
  } else {
    // Allocate a new spill slot for this catch phi.
    // TODO: Reuse spill slots when intervals of phis from different catch
    //       blocks do not overlap.
    interval->SetSpillSlot(catch_phi_spill_slots_);
    catch_phi_spill_slots_ += interval->NumberOfSpillSlotsNeeded();
  }
}

}  // namespace art
