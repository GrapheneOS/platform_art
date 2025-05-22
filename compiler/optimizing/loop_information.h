/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_H_
#define ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_H_

#include <iosfwd>

#include "base/macros.h"

#include "base/arena_bit_vector.h"
#include "base/arena_containers.h"
#include "base/arena_object.h"
#include "base/stl_util.h"

namespace art HIDDEN {

class HBasicBlock;
class HGraph;
class HInstruction;
class HSuspendCheck;

class HLoopInformation final : public ArenaObject<kArenaAllocLoopInfo> {
 public:
  HLoopInformation(HBasicBlock* header, HGraph* graph);

  bool IsIrreducible() const { return irreducible_; }
  bool ContainsIrreducibleLoop() const { return contains_irreducible_loop_; }

  void Dump(std::ostream& os);

  HBasicBlock* GetHeader() const {
    return header_;
  }

  void SetHeader(HBasicBlock* block) {
    header_ = block;
  }

  HSuspendCheck* GetSuspendCheck() const { return suspend_check_; }
  void SetSuspendCheck(HSuspendCheck* check) { suspend_check_ = check; }
  bool HasSuspendCheck() const { return suspend_check_ != nullptr; }

  void AddBackEdge(HBasicBlock* back_edge) {
    back_edges_.push_back(back_edge);
  }

  void RemoveBackEdge(HBasicBlock* back_edge) {
    RemoveElement(back_edges_, back_edge);
  }

  bool IsBackEdge(const HBasicBlock& block) const {
    return ContainsElement(back_edges_, &block);
  }

  size_t NumberOfBackEdges() const {
    return back_edges_.size();
  }

  HBasicBlock* GetPreHeader() const;

  const ArenaVector<HBasicBlock*>& GetBackEdges() const {
    return back_edges_;
  }

  // Returns the lifetime position of the back edge that has the
  // greatest lifetime position.
  size_t GetLifetimeEnd() const;

  void ReplaceBackEdge(HBasicBlock* existing, HBasicBlock* new_back_edge) {
    ReplaceElement(back_edges_, existing, new_back_edge);
  }

  // Finds blocks that are part of this loop.
  void Populate();

  // Updates blocks population of the loop and all of its outer' ones recursively after the
  // population of the inner loop is updated.
  void PopulateInnerLoopUpwards(HLoopInformation* inner_loop);

  // Returns whether this loop information contains `block`.
  // Note that this loop information *must* be populated before entering this function.
  bool Contains(const HBasicBlock& block) const;

  // Returns whether this loop information is an inner loop of `other`.
  // Note that `other` *must* be populated before entering this function.
  bool IsIn(const HLoopInformation& other) const;

  // Returns true if instruction is not defined within this loop.
  bool IsDefinedOutOfTheLoop(HInstruction* instruction) const;

  const ArenaBitVector& GetBlocks() const { return blocks_; }

  void Add(HBasicBlock* block);
  void Remove(HBasicBlock* block);

  void ClearAllBlocks() {
    blocks_.ClearAllBits();
  }

  bool HasBackEdgeNotDominatedByHeader() const;

  bool IsPopulated() const {
    return blocks_.GetHighestBitSet() != -1;
  }

  bool DominatesAllBackEdges(HBasicBlock* block);

  bool HasExitEdge() const;

  // Resets back edge and blocks-in-loop data.
  void ResetBasicBlockData() {
    back_edges_.clear();
    ClearAllBlocks();
  }

 private:
  // Internal recursive implementation of `Populate`.
  void PopulateRecursive(HBasicBlock* block);
  void PopulateIrreducibleRecursive(HBasicBlock* block, ArenaBitVector* finalized);

  // Set the loop information in the `block`. Overrides the `block`'s current
  // loop information if it is an outer loop of the loop information `*this`.
  void MarkInLoop(HBasicBlock* block);

  HBasicBlock* header_;
  HSuspendCheck* suspend_check_;
  bool irreducible_;
  bool contains_irreducible_loop_;
  ArenaVector<HBasicBlock*> back_edges_;
  ArenaBitVector blocks_;

  DISALLOW_COPY_AND_ASSIGN(HLoopInformation);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOOP_INFORMATION_H_
