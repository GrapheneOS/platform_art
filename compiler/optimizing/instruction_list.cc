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

#include "instruction_list.h"

#include "nodes.h"

namespace art HIDDEN {

void HInstructionList::AddInstruction(HInstruction* instruction) {
  if (first_instruction_ == nullptr) {
    DCHECK(last_instruction_ == nullptr);
    first_instruction_ = last_instruction_ = instruction;
  } else {
    DCHECK(last_instruction_ != nullptr);
    last_instruction_->next_ = instruction;
    instruction->previous_ = last_instruction_;
    last_instruction_ = instruction;
  }
}

void HInstructionList::InsertInstructionBefore(HInstruction* instruction, HInstruction* cursor) {
  DCHECK(Contains(cursor));
  if (cursor == first_instruction_) {
    cursor->previous_ = instruction;
    instruction->next_ = cursor;
    first_instruction_ = instruction;
  } else {
    instruction->previous_ = cursor->previous_;
    instruction->next_ = cursor;
    cursor->previous_ = instruction;
    instruction->previous_->next_ = instruction;
  }
}

void HInstructionList::InsertInstructionAfter(HInstruction* instruction, HInstruction* cursor) {
  DCHECK(Contains(cursor));
  if (cursor == last_instruction_) {
    cursor->next_ = instruction;
    instruction->previous_ = cursor;
    last_instruction_ = instruction;
  } else {
    instruction->next_ = cursor->next_;
    instruction->previous_ = cursor;
    cursor->next_ = instruction;
    instruction->next_->previous_ = instruction;
  }
}

void HInstructionList::RemoveInstruction(HInstruction* instruction) {
  DCHECK_EQ(instruction->previous_ == nullptr, instruction == first_instruction_);
  DCHECK_EQ(instruction->next_ == nullptr, instruction == last_instruction_);

  if (instruction == first_instruction_) {
    first_instruction_ = instruction->next_;
  } else {
    instruction->previous_->next_ = instruction->next_;
  }

  if (instruction == last_instruction_) {
    last_instruction_ = instruction->previous_;
  } else {
    instruction->next_->previous_ = instruction->previous_;
  }
}

bool HInstructionList::Contains(HInstruction* instruction) const {
  for (HInstructionIteratorPrefetchNext it(*this); !it.Done(); it.Advance()) {
    if (it.Current() == instruction) {
      return true;
    }
  }
  return false;
}

bool HInstructionList::FoundBefore(const HInstruction* instruction1,
                                   const HInstruction* instruction2) const {
  DCHECK_EQ(instruction1->GetBlock(), instruction2->GetBlock());
  for (HInstructionIteratorPrefetchNext it(*this); !it.Done(); it.Advance()) {
    if (it.Current() == instruction2) {
      return false;
    }
    if (it.Current() == instruction1) {
      return true;
    }
  }
  LOG(FATAL) << "Did not find an order between two instructions of the same block.";
  UNREACHABLE();
}

size_t HInstructionList::CountSize() const {
  size_t size = 0;
  HInstruction* current = first_instruction_;
  for (; current != nullptr; current = current->GetNext()) {
    size++;
  }
  return size;
}

void HInstructionList::SetBlockOfInstructions(HBasicBlock* block) const {
  for (HInstruction* current = first_instruction_;
       current != nullptr;
       current = current->GetNext()) {
    current->SetBlock(block);
  }
}

void HInstructionList::AddAfter(HInstruction* cursor, const HInstructionList& instruction_list) {
  DCHECK(Contains(cursor));
  if (!instruction_list.IsEmpty()) {
    if (cursor == last_instruction_) {
      last_instruction_ = instruction_list.last_instruction_;
    } else {
      cursor->next_->previous_ = instruction_list.last_instruction_;
    }
    instruction_list.last_instruction_->next_ = cursor->next_;
    cursor->next_ = instruction_list.first_instruction_;
    instruction_list.first_instruction_->previous_ = cursor;
  }
}

void HInstructionList::AddBefore(HInstruction* cursor, const HInstructionList& instruction_list) {
  DCHECK(Contains(cursor));
  if (!instruction_list.IsEmpty()) {
    if (cursor == first_instruction_) {
      first_instruction_ = instruction_list.first_instruction_;
    } else {
      cursor->previous_->next_ = instruction_list.first_instruction_;
    }
    instruction_list.last_instruction_->next_ = cursor;
    instruction_list.first_instruction_->previous_ = cursor->previous_;
    cursor->previous_ = instruction_list.last_instruction_;
  }
}

void HInstructionList::Add(const HInstructionList& instruction_list) {
  if (IsEmpty()) {
    first_instruction_ = instruction_list.first_instruction_;
    last_instruction_ = instruction_list.last_instruction_;
  } else {
    AddAfter(last_instruction_, instruction_list);
  }
}

}  // namespace art
