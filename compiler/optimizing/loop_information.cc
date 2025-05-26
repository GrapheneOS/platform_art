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

#include "loop_information.h"

#include "base/bit_vector-inl.h"
#include "nodes.h"

namespace art HIDDEN {

HLoopInformation::HLoopInformation(HBasicBlock* header, HGraph* graph)
    : header_(header),
      suspend_check_(nullptr),
      irreducible_(false),
      contains_irreducible_loop_(false),
      back_edges_(graph->GetAllocator()->Adapter(kArenaAllocLoopInfoBackEdges)),
      // Make bit vector growable, as the number of blocks may change.
      block_mask_(graph->GetAllocator(),
                  graph->GetBlocks().size(),
                  /*expandable=*/ true,
                  kArenaAllocLoopInfoBackEdges) {
  back_edges_.reserve(kDefaultNumberOfBackEdges);
}

void HLoopInformation::Dump(std::ostream& os) {
  os << "header: " << header_->GetBlockId() << std::endl;
  os << "pre header: " << GetPreHeader()->GetBlockId() << std::endl;
  for (HBasicBlock* block : back_edges_) {
    os << "back edge: " << block->GetBlockId() << std::endl;
  }
  for (HBasicBlock* block : header_->GetPredecessors()) {
    os << "predecessor: " << block->GetBlockId() << std::endl;
  }
  for (uint32_t idx : block_mask_.Indexes()) {
    os << "  in loop: " << idx << std::endl;
  }
}

void HLoopInformation::Add(HBasicBlock* block) {
  block_mask_.SetBit(block->GetBlockId());
}

void HLoopInformation::Remove(HBasicBlock* block) {
  block_mask_.ClearBit(block->GetBlockId());
}

void HLoopInformation::PopulateRecursive(HBasicBlock* block) {
  if (block_mask_.IsBitSet(block->GetBlockId())) {
    return;
  }

  block_mask_.SetBit(block->GetBlockId());
  MarkInLoop(block);
  if (block->IsLoopHeader()) {
    // We're visiting loops in post-order, so inner loops must have been
    // populated already.
    DCHECK(block->GetLoopInformation()->IsPopulated());
    if (block->GetLoopInformation()->IsIrreducible()) {
      contains_irreducible_loop_ = true;
    }
  }
  for (HBasicBlock* predecessor : block->GetPredecessors()) {
    PopulateRecursive(predecessor);
  }
}

void HLoopInformation::PopulateIrreducibleRecursive(HBasicBlock* block, ArenaBitVector* finalized) {
  size_t block_id = block->GetBlockId();

  // If `block` is in `finalized`, we know its membership in the loop has been
  // decided and it does not need to be revisited.
  if (finalized->IsBitSet(block_id)) {
    return;
  }

  bool is_finalized = false;
  if (block->IsLoopHeader()) {
    // If we hit a loop header in an irreducible loop, we first check if the
    // pre header of that loop belongs to the currently analyzed loop. If it does,
    // then we visit the back edges.
    // Note that we cannot use GetPreHeader, as the loop may have not been populated
    // yet.
    HBasicBlock* pre_header = block->GetPredecessors()[0];
    PopulateIrreducibleRecursive(pre_header, finalized);
    if (block_mask_.IsBitSet(pre_header->GetBlockId())) {
      MarkInLoop(block);
      block_mask_.SetBit(block_id);
      finalized->SetBit(block_id);
      is_finalized = true;

      HLoopInformation* info = block->GetLoopInformation();
      for (HBasicBlock* back_edge : info->GetBackEdges()) {
        PopulateIrreducibleRecursive(back_edge, finalized);
      }
    }
  } else {
    // Visit all predecessors. If one predecessor is part of the loop, this
    // block is also part of this loop.
    for (HBasicBlock* predecessor : block->GetPredecessors()) {
      PopulateIrreducibleRecursive(predecessor, finalized);
      if (!is_finalized && block_mask_.IsBitSet(predecessor->GetBlockId())) {
        MarkInLoop(block);
        block_mask_.SetBit(block_id);
        finalized->SetBit(block_id);
        is_finalized = true;
      }
    }
  }

  // All predecessors have been recursively visited. Mark finalized if not marked yet.
  if (!is_finalized) {
    finalized->SetBit(block_id);
  }
}

void HLoopInformation::Populate() {
  DCHECK_EQ(block_mask_.NumSetBits(), 0u) << "Loop information has already been populated";
  // Populate this loop: starting with the back edge, recursively add predecessors
  // that are not already part of that loop. Set the header as part of the loop
  // to end the recursion.
  // This is a recursive implementation of the algorithm described in
  // "Advanced Compiler Design & Implementation" (Muchnick) p192.
  HGraph* graph = header_->GetGraph();
  block_mask_.SetBit(header_->GetBlockId());
  MarkInLoop(header_);

  bool is_irreducible_loop = HasBackEdgeNotDominatedByHeader();

  if (is_irreducible_loop) {
    // Allocate memory from local ScopedArenaAllocator.
    ScopedArenaAllocator allocator(graph->GetArenaStack());
    ArenaBitVector visited(&allocator,
                           graph->GetBlocks().size(),
                           /* expandable= */ false,
                           kArenaAllocGraphBuilder);
    // Stop marking blocks at the loop header.
    visited.SetBit(header_->GetBlockId());

    for (HBasicBlock* back_edge : GetBackEdges()) {
      PopulateIrreducibleRecursive(back_edge, &visited);
    }
  } else {
    for (HBasicBlock* back_edge : GetBackEdges()) {
      PopulateRecursive(back_edge);
    }
  }

  if (!is_irreducible_loop && graph->IsCompilingOsr()) {
    // When compiling in OSR mode, all loops in the compiled method may be entered
    // from the interpreter. We treat this OSR entry point just like an extra entry
    // to an irreducible loop, so we need to mark the method's loops as irreducible.
    // This does not apply to inlined loops which do not act as OSR entry points.
    if (suspend_check_ == nullptr) {
      // Just building the graph in OSR mode, this loop is not inlined. We never build an
      // inner graph in OSR mode as we can do OSR transition only from the outer method.
      is_irreducible_loop = true;
    } else {
      // Look at the suspend check's environment to determine if the loop was inlined.
      DCHECK(suspend_check_->HasEnvironment());
      if (!suspend_check_->GetEnvironment()->IsFromInlinedInvoke()) {
        is_irreducible_loop = true;
      }
    }
  }
  if (is_irreducible_loop) {
    irreducible_ = true;
    contains_irreducible_loop_ = true;
    graph->SetHasIrreducibleLoops(true);
  }
  graph->SetHasLoops(true);
}

void HLoopInformation::PopulateInnerLoopUpwards(HLoopInformation* inner_loop) {
  DCHECK(inner_loop->GetPreHeader()->GetLoopInformation() == this);
  block_mask_.Union(&inner_loop->block_mask_);
  HLoopInformation* outer_loop = GetPreHeader()->GetLoopInformation();
  if (outer_loop != nullptr) {
    outer_loop->PopulateInnerLoopUpwards(this);
  }
}

HBasicBlock* HLoopInformation::GetPreHeader() const {
  HBasicBlock* block = header_->GetPredecessors()[0];
  DCHECK(irreducible_ || (block == header_->GetDominator()));
  return block;
}

bool HLoopInformation::Contains(const HBasicBlock& block) const {
  return block_mask_.IsBitSet(block.GetBlockId());
}

bool HLoopInformation::IsIn(const HLoopInformation& other) const {
  return other.block_mask_.IsBitSet(header_->GetBlockId());
}

bool HLoopInformation::IsDefinedOutOfTheLoop(HInstruction* instruction) const {
  return !block_mask_.IsBitSet(instruction->GetBlock()->GetBlockId());
}

size_t HLoopInformation::GetLifetimeEnd() const {
  size_t last_position = 0;
  for (HBasicBlock* back_edge : GetBackEdges()) {
    last_position = std::max(back_edge->GetLifetimeEnd(), last_position);
  }
  return last_position;
}

bool HLoopInformation::HasBackEdgeNotDominatedByHeader() const {
  for (HBasicBlock* back_edge : GetBackEdges()) {
    DCHECK(back_edge->GetDominator() != nullptr);
    if (!header_->Dominates(back_edge)) {
      return true;
    }
  }
  return false;
}

bool HLoopInformation::DominatesAllBackEdges(HBasicBlock* block) {
  for (HBasicBlock* back_edge : GetBackEdges()) {
    if (!block->Dominates(back_edge)) {
      return false;
    }
  }
  return true;
}

bool HLoopInformation::HasExitEdge() const {
  // Determine if this loop has at least one exit edge.
  HBlocksInLoopReversePostOrderIterator it_loop(*this);
  for (; !it_loop.Done(); it_loop.Advance()) {
    for (HBasicBlock* successor : it_loop.Current()->GetSuccessors()) {
      if (!Contains(*successor)) {
        return true;
      }
    }
  }
  return false;
}

inline void HLoopInformation::MarkInLoop(HBasicBlock* block) {
  if (!block->IsInLoop()) {
    block->SetLoopInformation(this);
  } else if (block->IsLoopHeader()) {
    // Nothing to do. This just means `*this` is an outer loop.
  } else if (block->GetLoopInformation()->Contains(*GetHeader())) {
    // The `block` is currently part of an outer loop. Make it part of this inner loop.
    // Note that a non loop header having a loop information means this loop information
    // has already been populated
    block->SetLoopInformation(this);
  } else {
    // The `block` is part of an inner loop. Do not update the loop information.
    // Note that we cannot do the check `Contains(block->GetLoopInformation()->GetHeader())`
    // at this point, because this function is being called while populating `*this`.
  }
}

}  // namespace art
