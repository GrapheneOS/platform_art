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

#ifndef ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_LINEAR_SCAN_H_
#define ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_LINEAR_SCAN_H_

#include "arch/instruction_set.h"
#include "base/macros.h"
#include "base/scoped_arena_containers.h"
#include "register_allocator.h"

namespace art HIDDEN {

class CodeGenerator;
class HBasicBlock;
class HGraph;
class HInstruction;
class HParallelMove;
class HPhi;
class LiveInterval;
class Location;
class SsaLivenessAnalysis;

/**
 * An implementation of a linear scan register allocator on an `HGraph` with SSA form.
 */
class RegisterAllocatorLinearScan : public RegisterAllocator {
 public:
  RegisterAllocatorLinearScan(ScopedArenaAllocator* allocator,
                              CodeGenerator* codegen,
                              const SsaLivenessAnalysis& analysis);
  ~RegisterAllocatorLinearScan() override;

  void AllocateRegisters() override;

  bool Validate(bool log_fatal_on_failure) override;

  size_t GetNumberOfSpillSlots() const {
    return int_spill_slots_.size()
        + long_spill_slots_.size()
        + float_spill_slots_.size()
        + double_spill_slots_.size()
        + catch_phi_spill_slots_;
  }

 private:
  class LinearScan;

  // Add `interval` in the given sorted list.
  static void AddSorted(ScopedArenaVector<LiveInterval*>* array, LiveInterval* interval);

  // Update the interval for the register in `location` to cover [start, end).
  void BlockRegister(Location location, size_t position, bool will_call);

  // Allocate a spill slot for the given catch phi. Will allocate the same slot
  // for phis which share the same vreg. Must be called in reverse linear order
  // of lifetime positions and ascending vreg numbers for correctness.
  void AllocateSpillSlotForCatchPhi(HPhi* phi);

  // Helper methods.
  void AllocateRegistersInternal();
  void ProcessInstruction(HInstruction* instruction);
  bool ValidateInternal(RegisterType current_register_type, bool log_fatal_on_failure) const;

  // If any inputs require specific registers, block those registers
  // at the position of this instruction.
  void CheckForFixedInputs(HInstruction* instruction, bool will_call);

  // If the output of an instruction requires a specific register, split
  // the interval and assign the register to the first part.
  void CheckForFixedOutput(HInstruction* instruction, bool will_call);

  // Add all applicable safepoints to a live interval.
  // Currently depends on instruction processing order.
  void AddSafepointsFor(HInstruction* instruction);

  // Collect all live intervals associated with the temporary locations
  // needed by an instruction.
  void CheckForTempLiveIntervals(HInstruction* instruction, bool will_call);

  // If a safe point is needed, add a synthesized interval to later record
  // the number of live registers at this point.
  void CheckForSafepoint(HInstruction* instruction);

  // Try to remove the SuspendCheck at function entry. Returns true if it was successful.
  bool TryRemoveSuspendCheckEntry(HInstruction* instruction);

  // List of intervals for core registers that must be processed, ordered by start
  // position. Last entry is the interval that has the lowest start position.
  // This list is initially populated before doing the linear scan.
  ScopedArenaVector<LiveInterval*> unhandled_core_intervals_;

  // List of intervals for floating-point registers. Same comments as above.
  ScopedArenaVector<LiveInterval*> unhandled_fp_intervals_;

  // Fixed intervals for physical registers. Such intervals cover the positions
  // where an instruction requires a specific register.
  ScopedArenaVector<LiveInterval*> physical_core_register_intervals_;
  ScopedArenaVector<LiveInterval*> physical_fp_register_intervals_;
  LiveInterval* block_registers_for_call_interval_;
  LiveInterval* block_registers_special_interval_;  // For catch block or irreducible loop header.

  // Intervals for temporaries. Such intervals cover the positions
  // where an instruction requires a temporary.
  ScopedArenaVector<LiveInterval*> temp_intervals_;

  // The spill slots allocated for live intervals. We ensure spill slots
  // are typed to avoid (1) doing moves and swaps between two different kinds
  // of registers, and (2) swapping between a single stack slot and a double
  // stack slot. This simplifies the parallel move resolver.
  ScopedArenaVector<size_t> int_spill_slots_;
  ScopedArenaVector<size_t> long_spill_slots_;
  ScopedArenaVector<size_t> float_spill_slots_;
  ScopedArenaVector<size_t> double_spill_slots_;

  // Spill slots allocated to catch phis. This category is special-cased because
  // (1) slots are allocated prior to linear scan and in reverse linear order,
  // (2) equivalent phis need to share slots despite having different types.
  size_t catch_phi_spill_slots_;

  // Instructions that need a safepoint.
  ScopedArenaVector<HInstruction*> safepoints_;

  // Slots reserved for out arguments.
  size_t reserved_out_slots_;

  friend class RegisterAllocatorTest;

  DISALLOW_COPY_AND_ASSIGN(RegisterAllocatorLinearScan);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_REGISTER_ALLOCATOR_LINEAR_SCAN_H_
