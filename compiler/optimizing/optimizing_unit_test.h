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

#ifndef ART_COMPILER_OPTIMIZING_OPTIMIZING_UNIT_TEST_H_
#define ART_COMPILER_OPTIMIZING_OPTIMIZING_UNIT_TEST_H_

#include <memory>
#include <ostream>
#include <string_view>
#include <string>
#include <tuple>
#include <vector>
#include <variant>

#include "base/macros.h"
#include "base/indenter.h"
#include "base/malloc_arena_pool.h"
#include "base/scoped_arena_allocator.h"
#include "builder.h"
#include "common_compiler_test.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file.h"
#include "dex/dex_instruction.h"
#include "dex/standard_dex_file.h"
#include "driver/dex_compilation_unit.h"
#include "graph_checker.h"
#include "gtest/gtest.h"
#include "handle_scope-inl.h"
#include "handle_scope.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"
#include "nodes.h"
#include "scoped_thread_state_change.h"
#include "ssa_builder.h"
#include "ssa_liveness_analysis.h"

namespace art HIDDEN {

#define NUM_INSTRUCTIONS(...)  \
  (sizeof((uint16_t[]) {__VA_ARGS__}) /sizeof(uint16_t))

#define N_REGISTERS_CODE_ITEM(NUM_REGS, ...)                            \
    { NUM_REGS, 0, 0, 0, 0, 0, NUM_INSTRUCTIONS(__VA_ARGS__), 0, __VA_ARGS__ }

#define ZERO_REGISTER_CODE_ITEM(...)   N_REGISTERS_CODE_ITEM(0, __VA_ARGS__)
#define ONE_REGISTER_CODE_ITEM(...)    N_REGISTERS_CODE_ITEM(1, __VA_ARGS__)
#define TWO_REGISTERS_CODE_ITEM(...)   N_REGISTERS_CODE_ITEM(2, __VA_ARGS__)
#define THREE_REGISTERS_CODE_ITEM(...) N_REGISTERS_CODE_ITEM(3, __VA_ARGS__)
#define FOUR_REGISTERS_CODE_ITEM(...)  N_REGISTERS_CODE_ITEM(4, __VA_ARGS__)
#define FIVE_REGISTERS_CODE_ITEM(...)  N_REGISTERS_CODE_ITEM(5, __VA_ARGS__)
#define SIX_REGISTERS_CODE_ITEM(...)   N_REGISTERS_CODE_ITEM(6, __VA_ARGS__)

struct InstructionDumper {
 public:
  HInstruction* ins_;
};

inline bool operator==(const InstructionDumper& a, const InstructionDumper& b) {
  return a.ins_ == b.ins_;
}
inline bool operator!=(const InstructionDumper& a, const InstructionDumper& b) {
  return !(a == b);
}

inline std::ostream& operator<<(std::ostream& os, const InstructionDumper& id) {
  if (id.ins_ == nullptr) {
    return os << "NULL";
  } else {
    return os << "(" << id.ins_ << "): " << id.ins_->DumpWithArgs();
  }
}

#define EXPECT_INS_EQ(a, b) EXPECT_EQ(InstructionDumper{a}, InstructionDumper{b})
#define EXPECT_INS_REMOVED(a) EXPECT_TRUE(IsRemoved(a)) << "Not removed: " << (InstructionDumper{a})
#define EXPECT_INS_RETAINED(a) EXPECT_FALSE(IsRemoved(a)) << "Removed: " << (InstructionDumper{a})
#define ASSERT_INS_EQ(a, b) ASSERT_EQ(InstructionDumper{a}, InstructionDumper{b})
#define ASSERT_INS_REMOVED(a) ASSERT_TRUE(IsRemoved(a)) << "Not removed: " << (InstructionDumper{a})
#define ASSERT_INS_RETAINED(a) ASSERT_FALSE(IsRemoved(a)) << "Removed: " << (InstructionDumper{a})

#define EXPECT_BLOCK_REMOVED(b) EXPECT_TRUE(IsRemoved(b)) << "Not removed: B" << b->GetBlockId()
#define EXPECT_BLOCK_RETAINED(b) EXPECT_FALSE(IsRemoved(b)) << "Removed: B" << b->GetBlockId()
#define ASSERT_BLOCK_REMOVED(b) ASSERT_TRUE(IsRemoved(b)) << "Not removed: B" << b->GetBlockId()
#define ASSERT_BLOCK_RETAINED(b) ASSERT_FALSE(IsRemoved(b)) << "Removed: B" << b->GetBlockId()

inline LiveInterval* BuildInterval(const size_t ranges[][2],
                                   size_t number_of_ranges,
                                   ScopedArenaAllocator* allocator,
                                   int reg = -1,
                                   HInstruction* defined_by = nullptr) {
  LiveInterval* interval =
      LiveInterval::MakeInterval(allocator, DataType::Type::kInt32, defined_by);
  if (defined_by != nullptr) {
    defined_by->SetLiveInterval(interval);
  }
  for (size_t i = number_of_ranges; i > 0; --i) {
    interval->AddRange(ranges[i - 1][0], ranges[i - 1][1]);
  }
  interval->SetRegister(reg);
  return interval;
}

inline void RemoveSuspendChecks(HGraph* graph) {
  for (HBasicBlock* block : graph->GetBlocks()) {
    if (block != nullptr) {
      if (block->GetLoopInformation() != nullptr) {
        block->GetLoopInformation()->SetSuspendCheck(nullptr);
      }
      for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
        HInstruction* current = it.Current();
        if (current->IsSuspendCheck()) {
          current->GetBlock()->RemoveInstruction(current);
        }
      }
    }
  }
}

class ArenaPoolAndAllocator {
 public:
  ArenaPoolAndAllocator()
      : pool_(), allocator_(&pool_), arena_stack_(&pool_), scoped_allocator_(&arena_stack_) { }

  ArenaAllocator* GetAllocator() { return &allocator_; }
  ArenaStack* GetArenaStack() { return &arena_stack_; }
  ScopedArenaAllocator* GetScopedAllocator() { return &scoped_allocator_; }

 private:
  MallocArenaPool pool_;
  ArenaAllocator allocator_;
  ArenaStack arena_stack_;
  ScopedArenaAllocator scoped_allocator_;
};

class AdjacencyListGraph {
 public:
  using Edge = std::pair<const std::string_view, const std::string_view>;
  AdjacencyListGraph(
      HGraph* graph,
      ArenaAllocator* alloc,
      const std::string_view entry_name,
      const std::string_view exit_name,
      const std::vector<Edge>& adj) : graph_(graph) {
    auto create_block = [&]() {
      HBasicBlock* blk = new (alloc) HBasicBlock(graph_);
      graph_->AddBlock(blk);
      return blk;
    };
    HBasicBlock* entry = create_block();
    HBasicBlock* exit = create_block();
    graph_->SetEntryBlock(entry);
    graph_->SetExitBlock(exit);
    name_to_block_.Put(entry_name, entry);
    name_to_block_.Put(exit_name, exit);
    for (const auto& [src, dest] : adj) {
      HBasicBlock* src_blk = name_to_block_.GetOrCreate(src, create_block);
      HBasicBlock* dest_blk = name_to_block_.GetOrCreate(dest, create_block);
      src_blk->AddSuccessor(dest_blk);
    }
    graph_->ComputeDominanceInformation();
    for (auto [name, blk] : name_to_block_) {
      block_to_name_.Put(blk, name);
    }
  }

  bool HasBlock(const HBasicBlock* blk) const {
    return block_to_name_.find(blk) != block_to_name_.end();
  }

  std::string_view GetName(const HBasicBlock* blk) const {
    return block_to_name_.Get(blk);
  }

  HBasicBlock* Get(const std::string_view& sv) const {
    return name_to_block_.Get(sv);
  }

  AdjacencyListGraph(AdjacencyListGraph&&) = default;
  AdjacencyListGraph(const AdjacencyListGraph&) = default;
  AdjacencyListGraph& operator=(AdjacencyListGraph&&) = default;
  AdjacencyListGraph& operator=(const AdjacencyListGraph&) = default;

  std::ostream& Dump(std::ostream& os) const {
    struct Namer : public BlockNamer {
     public:
      explicit Namer(const AdjacencyListGraph& alg) : BlockNamer(), alg_(alg) {}
      std::ostream& PrintName(std::ostream& os, HBasicBlock* blk) const override {
        if (alg_.HasBlock(blk)) {
          return os << alg_.GetName(blk) << " (" << blk->GetBlockId() << ")";
        } else {
          return os << "<Unnamed B" << blk->GetBlockId() << ">";
        }
      }

      const AdjacencyListGraph& alg_;
    };
    Namer namer(*this);
    return graph_->Dump(os, /* codegen_= */ nullptr, namer);
  }

 private:
  HGraph* graph_;
  SafeMap<const std::string_view, HBasicBlock*> name_to_block_;
  SafeMap<const HBasicBlock*, const std::string_view> block_to_name_;
};

// Have a separate helper so the OptimizingCFITest can inherit it without causing
// multiple inheritance errors from having two gtest as a parent twice.
class OptimizingUnitTestHelper {
 public:
  OptimizingUnitTestHelper()
      : pool_and_allocator_(new ArenaPoolAndAllocator()),
        graph_(nullptr),
        entry_block_(nullptr),
        exit_block_(nullptr) { }

  ArenaAllocator* GetAllocator() { return pool_and_allocator_->GetAllocator(); }
  ArenaStack* GetArenaStack() { return pool_and_allocator_->GetArenaStack(); }
  ScopedArenaAllocator* GetScopedAllocator() { return pool_and_allocator_->GetScopedAllocator(); }

  void ResetPoolAndAllocator() {
    pool_and_allocator_.reset(new ArenaPoolAndAllocator());
  }

  HGraph* CreateGraph(VariableSizedHandleScope* handles = nullptr) {
    ArenaAllocator* const allocator = pool_and_allocator_->GetAllocator();

    // Reserve a big array of 0s so the dex file constructor can offsets from the header.
    static constexpr size_t kDexDataSize = 4 * KB;
    const uint8_t* dex_data = reinterpret_cast<uint8_t*>(allocator->Alloc(kDexDataSize));

    // Create the dex file based on the fake data. Call the constructor so that we can use virtual
    // functions. Don't use the arena for the StandardDexFile otherwise the dex location leaks.
    auto container =
        std::make_shared<MemoryDexFileContainer>(dex_data, sizeof(StandardDexFile::Header));
    dex_files_.emplace_back(new StandardDexFile(dex_data,
                                                "no_location",
                                                /*location_checksum*/ 0,
                                                /*oat_dex_file*/ nullptr,
                                                std::move(container)));

    graph_ = new (allocator) HGraph(
        allocator,
        pool_and_allocator_->GetArenaStack(),
        handles,
        *dex_files_.back(),
        /*method_idx*/-1,
        kRuntimeISA);
    return graph_;
  }

  // Create a control-flow graph from Dex instructions.
  HGraph* CreateCFG(const std::vector<uint16_t>& data,
                    DataType::Type return_type = DataType::Type::kInt32) {
    ScopedObjectAccess soa(Thread::Current());
    VariableSizedHandleScope handles(soa.Self());
    HGraph* graph = CreateGraph(&handles);

    // The code item data might not aligned to 4 bytes, copy it to ensure that.
    const size_t code_item_size = data.size() * sizeof(data.front());
    void* aligned_data = GetAllocator()->Alloc(code_item_size);
    memcpy(aligned_data, &data[0], code_item_size);
    CHECK_ALIGNED(aligned_data, StandardDexFile::CodeItem::kAlignment);
    const dex::CodeItem* code_item = reinterpret_cast<const dex::CodeItem*>(aligned_data);

    {
      const DexCompilationUnit* dex_compilation_unit =
          new (graph->GetAllocator()) DexCompilationUnit(
              /* class_loader= */ Handle<mirror::ClassLoader>(),  // Invalid handle.
              /* class_linker= */ nullptr,
              graph->GetDexFile(),
              code_item,
              /* class_def_idx= */ DexFile::kDexNoIndex16,
              /* method_idx= */ dex::kDexNoIndex,
              /* access_flags= */ 0u,
              /* verified_method= */ nullptr,
              /* dex_cache= */ Handle<mirror::DexCache>());  // Invalid handle.
      CodeItemDebugInfoAccessor accessor(graph->GetDexFile(), code_item, /*dex_method_idx*/ 0u);
      HGraphBuilder builder(graph, dex_compilation_unit, accessor, return_type);
      bool graph_built = (builder.BuildGraph() == kAnalysisSuccess);
      return graph_built ? graph : nullptr;
    }
  }

  // Create simple graph with "entry", "main" and "exit" blocks, return the "main" block.
  // Adds `HGoto` to the "entry" block and `HExit` to the "exit block. Leaves "main" block empty.
  HBasicBlock* InitEntryMainExitGraph(VariableSizedHandleScope* handles = nullptr) {
    CreateGraph(handles);
    entry_block_ = AddNewBlock();
    HBasicBlock* main_block = AddNewBlock();
    exit_block_ = AddNewBlock();

    graph_->SetEntryBlock(entry_block_);
    graph_->SetExitBlock(exit_block_);

    entry_block_->AddSuccessor(main_block);
    main_block->AddSuccessor(exit_block_);

    MakeGoto(entry_block_);
    MakeExit(exit_block_);

    return main_block;
  }

  // Creates a graph identical to `InitEntryMainExitGraph()` and adds `HReturnVoid`.
  HBasicBlock* InitEntryMainExitGraphWithReturnVoid(VariableSizedHandleScope* handles = nullptr) {
    HBasicBlock* return_block = InitEntryMainExitGraph(handles);
    MakeReturnVoid(return_block);
    return return_block;
  }

  // Insert "if_block", "then_block" and "else_block" before a given `merge_block`. Return the
  // new blocks. Adds `HGoto` to "then_block" and "else_block". Adds `HIf` to the "if_block"
  // if the caller provides a `condition`.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateDiamondPattern(
      HBasicBlock* merge_block, HInstruction* condition = nullptr) {
    HBasicBlock* if_block = AddNewBlock();
    HBasicBlock* then_block = AddNewBlock();
    HBasicBlock* else_block = AddNewBlock();

    HBasicBlock* predecessor = merge_block->GetSinglePredecessor();
    predecessor->ReplaceSuccessor(merge_block, if_block);

    if_block->AddSuccessor(then_block);
    if_block->AddSuccessor(else_block);
    then_block->AddSuccessor(merge_block);
    else_block->AddSuccessor(merge_block);

    if (condition != nullptr) {
      MakeIf(if_block, condition);
    }
    MakeGoto(then_block);
    MakeGoto(else_block);

    return {if_block, then_block, else_block};
  }

  // Insert "pre-header", "loop-header" and "loop-body" blocks before a given `loop_exit` block
  // and connect them in a `while (...) { ... }` loop pattern. Return the new blocks.
  // Adds `HGoto` to the "pre-header" and "loop-body" blocks but leaves the "loop-header" block
  // empty, leaving the construction of an appropriate condition and `HIf` to the caller.
  // Note: The `loop_exit` shall be the "then" successor of the "loop-header". If the `loop_exit`
  // is needed as the "else" successor, use `HBlock::SwapSuccessors()` to adjust the order.
  // Note: A `do { ... } while (...);` loop pattern has the same block structure, except that
  // the `loop_body` is a single-goto block that exists purely to avoid a critical edge.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateWhileLoop(HBasicBlock* loop_exit) {
    HBasicBlock* pre_header = AddNewBlock();
    HBasicBlock* loop_header = AddNewBlock();
    HBasicBlock* loop_body = AddNewBlock();

    HBasicBlock* predecessor = loop_exit->GetSinglePredecessor();
    predecessor->ReplaceSuccessor(loop_exit, pre_header);

    pre_header->AddSuccessor(loop_header);
    loop_header->AddSuccessor(loop_exit);  // true successor
    loop_header->AddSuccessor(loop_body);  // false successor
    loop_body->AddSuccessor(loop_header);

    MakeGoto(pre_header);
    MakeGoto(loop_body);

    return {pre_header, loop_header, loop_body};
  }

  // Insert blocks for an irreducible loop before the `loop_exit`:
  //
  //      <loop_exit's old predecessor>
  //                    |
  //                  split
  //                 /     \
  //   left_preheader       right_preheader
  //         |                     |
  //    left_header <------- right_header <-+
  //     |  |                               |
  //     |  +------------> body ------------+
  //     |
  //    loop_exit
  //
  // Note that `left_preheader`, `right_preheader` and `body` are needed to avoid critical edges.
  //
  // `HGoto` instructions are added to `left_preheader`, `right_preheader`, `body`
  // and `right_header`. To complete the control flow, the caller should add `HIf`
  // to `split` and `left_header`.
  //
  // Returns `{split, left_header, right_header, body}`.
  std::tuple<HBasicBlock*, HBasicBlock*, HBasicBlock*, HBasicBlock*> CreateIrreducibleLoop(
      HBasicBlock* loop_exit) {
    HBasicBlock* split = AddNewBlock();
    HBasicBlock* left_preheader = AddNewBlock();
    HBasicBlock* right_preheader = AddNewBlock();
    HBasicBlock* left_header = AddNewBlock();
    HBasicBlock* right_header = AddNewBlock();
    HBasicBlock* body = AddNewBlock();

    HBasicBlock* predecessor = loop_exit->GetSinglePredecessor();
    predecessor->ReplaceSuccessor(loop_exit, split);

    split->AddSuccessor(left_preheader);  // true successor
    split->AddSuccessor(right_preheader);  // false successor
    left_preheader->AddSuccessor(left_header);
    right_preheader->AddSuccessor(right_header);
    left_header->AddSuccessor(loop_exit);  // true successor
    left_header->AddSuccessor(body);  // false successor
    body->AddSuccessor(right_header);
    right_header->AddSuccessor(left_header);

    MakeGoto(left_preheader);
    MakeGoto(right_preheader);
    MakeGoto(body);
    MakeGoto(right_header);

    return {split, left_header, right_header, body};
  }

  HBasicBlock* AddNewBlock() {
    HBasicBlock* block = new (GetAllocator()) HBasicBlock(graph_);
    graph_->AddBlock(block);
    return block;
  }

  // Run GraphChecker with all checks.
  //
  // Return: the status whether the run is successful.
  bool CheckGraph(std::ostream& oss = std::cerr) {
    return CheckGraph(graph_, oss);
  }

  HEnvironment* ManuallyBuildEnvFor(HInstruction* instruction,
                                    ArenaVector<HInstruction*>* current_locals) {
    HEnvironment* environment = HEnvironment::Create(
        GetAllocator(),
        current_locals->size(),
        graph_->GetArtMethod(),
        instruction->GetDexPc(),
        instruction);

    environment->CopyFrom(GetAllocator(), ArrayRef<HInstruction* const>(*current_locals));
    instruction->SetRawEnvironment(environment);
    return environment;
  }

  void EnsurePredecessorOrder(HBasicBlock* target, std::initializer_list<HBasicBlock*> preds) {
    // Make sure the given preds and block predecessors have the same blocks.
    BitVector bv(preds.size(), false, Allocator::GetCallocAllocator());
    auto preds_and_idx = ZipCount(MakeIterationRange(target->GetPredecessors()));
    bool correct_preds = preds.size() == target->GetPredecessors().size() &&
                         std::all_of(preds.begin(), preds.end(), [&](HBasicBlock* pred) {
                           return std::any_of(preds_and_idx.begin(),
                                              preds_and_idx.end(),
                                              // Make sure every target predecessor is used only
                                              // once.
                                              [&](std::pair<HBasicBlock*, uint32_t> cur) {
                                                if (cur.first == pred && !bv.IsBitSet(cur.second)) {
                                                  bv.SetBit(cur.second);
                                                  return true;
                                                } else {
                                                  return false;
                                                }
                                              });
                         }) &&
                         bv.NumSetBits() == preds.size();
    auto dump_list = [](auto it) {
      std::ostringstream oss;
      oss << "[";
      bool first = true;
      for (HBasicBlock* b : it) {
        if (!first) {
          oss << ", ";
        }
        first = false;
        oss << b->GetBlockId();
      }
      oss << "]";
      return oss.str();
    };
    ASSERT_TRUE(correct_preds) << "Predecessors of " << target->GetBlockId() << " are "
                               << dump_list(target->GetPredecessors()) << " not "
                               << dump_list(preds);
    if (correct_preds) {
      std::copy(preds.begin(), preds.end(), target->predecessors_.begin());
    }
  }

  AdjacencyListGraph SetupFromAdjacencyList(const std::string_view entry_name,
                                            const std::string_view exit_name,
                                            const std::vector<AdjacencyListGraph::Edge>& adj) {
    return AdjacencyListGraph(graph_, GetAllocator(), entry_name, exit_name, adj);
  }

  void ManuallyBuildEnvFor(HInstruction* ins, const std::initializer_list<HInstruction*>& env) {
    ArenaVector<HInstruction*> current_locals(env, GetAllocator()->Adapter(kArenaAllocInstruction));
    OptimizingUnitTestHelper::ManuallyBuildEnvFor(ins, &current_locals);
  }

  HLoadClass* MakeLoadClass(HBasicBlock* block,
                            std::optional<dex::TypeIndex> ti = std::nullopt,
                            std::optional<Handle<mirror::Class>> klass = std::nullopt,
                            std::initializer_list<HInstruction*> env = {},
                            uint32_t dex_pc = kNoDexPc) {
    HLoadClass* load_class = new (GetAllocator()) HLoadClass(
        graph_->GetCurrentMethod(),
        ti ? *ti : dex::TypeIndex(class_idx_++),
        graph_->GetDexFile(),
        /* klass= */ klass ? *klass : null_klass_,
        /* is_referrers_class= */ false,
        dex_pc,
        /* needs_access_check= */ false);
    AddOrInsertInstruction(block, load_class);
    ManuallyBuildEnvFor(load_class, env);
    return load_class;
  }

  HNewInstance* MakeNewInstance(HBasicBlock* block,
                                HInstruction* cls,
                                std::initializer_list<HInstruction*> env = {},
                                uint32_t dex_pc = kNoDexPc) {
    EXPECT_TRUE(cls->IsLoadClass() || cls->IsClinitCheck()) << *cls;
    HLoadClass* load =
        cls->IsLoadClass() ? cls->AsLoadClass() : cls->AsClinitCheck()->GetLoadClass();
    HNewInstance* new_instance = new (GetAllocator()) HNewInstance(
        cls,
        dex_pc,
        load->GetTypeIndex(),
        graph_->GetDexFile(),
        /* finalizable= */ false,
        QuickEntrypointEnum::kQuickAllocObjectInitialized);
    AddOrInsertInstruction(block, new_instance);
    ManuallyBuildEnvFor(new_instance, env);
    return new_instance;
  }

  HInstanceFieldSet* MakeIFieldSet(HBasicBlock* block,
                                   HInstruction* object,
                                   HInstruction* data,
                                   MemberOffset off,
                                   uint32_t dex_pc = kNoDexPc) {
    CHECK(data != nullptr);
    return MakeIFieldSet(block, object, data, data->GetType(), off, dex_pc);
  }

  HInstanceFieldSet* MakeIFieldSet(HBasicBlock* block,
                                   HInstruction* object,
                                   HInstruction* data,
                                   DataType::Type field_type,
                                   MemberOffset off,
                                   uint32_t dex_pc = kNoDexPc) {
    HInstanceFieldSet* ifield_set = new (GetAllocator()) HInstanceFieldSet(
        object,
        data,
        /* field= */ nullptr,
        field_type,
        /* field_offset= */ off,
        /* is_volatile= */ false,
        kUnknownFieldIndex,
        kUnknownClassDefIndex,
        graph_->GetDexFile(),
        dex_pc);
    AddOrInsertInstruction(block, ifield_set);
    return ifield_set;
  }

  HInstanceFieldGet* MakeIFieldGet(HBasicBlock* block,
                                   HInstruction* object,
                                   DataType::Type type,
                                   MemberOffset off,
                                   uint32_t dex_pc = kNoDexPc) {
    HInstanceFieldGet* ifield_get = new (GetAllocator()) HInstanceFieldGet(
        object,
        /* field= */ nullptr,
        /* field_type= */ type,
        /* field_offset= */ off,
        /* is_volatile= */ false,
        kUnknownFieldIndex,
        kUnknownClassDefIndex,
        graph_->GetDexFile(),
        dex_pc);
    AddOrInsertInstruction(block, ifield_get);
    return ifield_get;
  }

  HNewArray* MakeNewArray(HBasicBlock* block,
                          HInstruction* cls,
                          HInstruction* length,
                          size_t component_size_shift = DataType::SizeShift(DataType::Type::kInt32),
                          std::initializer_list<HInstruction*> env = {},
                          uint32_t dex_pc = kNoDexPc) {
    HNewArray* new_array =
        new (GetAllocator()) HNewArray(cls, length, dex_pc, component_size_shift);
    AddOrInsertInstruction(block, new_array);
    ManuallyBuildEnvFor(new_array, env);
    return new_array;
  }

  HArraySet* MakeArraySet(HBasicBlock* block,
                          HInstruction* array,
                          HInstruction* index,
                          HInstruction* value,
                          uint32_t dex_pc = kNoDexPc) {
    CHECK(value != nullptr);
    return MakeArraySet(block, array, index, value, value->GetType(), dex_pc);
  }

  HArraySet* MakeArraySet(HBasicBlock* block,
                          HInstruction* array,
                          HInstruction* index,
                          HInstruction* value,
                          DataType::Type type,
                          uint32_t dex_pc = kNoDexPc) {
    HArraySet* array_set = new (GetAllocator()) HArraySet(array, index, value, type, dex_pc);
    AddOrInsertInstruction(block, array_set);
    return array_set;
  }

  HArrayGet* MakeArrayGet(HBasicBlock* block,
                          HInstruction* array,
                          HInstruction* index,
                          DataType::Type type,
                          uint32_t dex_pc = kNoDexPc) {
    HArrayGet* array_get = new (GetAllocator()) HArrayGet(array, index, type, dex_pc);
    AddOrInsertInstruction(block, array_get);
    return array_get;
  }

  HArrayLength* MakeArrayLength(HBasicBlock* block,
                                HInstruction* array,
                                uint32_t dex_pc = kNoDexPc) {
    HArrayLength* array_length = new (GetAllocator()) HArrayLength(array, dex_pc);
    AddOrInsertInstruction(block, array_length);
    return array_length;
  }

  HNullCheck* MakeNullCheck(HBasicBlock* block,
                            HInstruction* value,
                            std::initializer_list<HInstruction*> env = {},
                            uint32_t dex_pc = kNoDexPc) {
    HNullCheck* null_check = new (GetAllocator()) HNullCheck(value, dex_pc);
    AddOrInsertInstruction(block, null_check);
    ManuallyBuildEnvFor(null_check, env);
    return null_check;
  }

  HBoundsCheck* MakeBoundsCheck(HBasicBlock* block,
                                HInstruction* index,
                                HInstruction* length,
                                std::initializer_list<HInstruction*> env = {},
                                uint32_t dex_pc = kNoDexPc) {
    HBoundsCheck* bounds_check = new (GetAllocator()) HBoundsCheck(index, length, dex_pc);
    AddOrInsertInstruction(block, bounds_check);
    ManuallyBuildEnvFor(bounds_check, env);
    return bounds_check;
  }

  HVecStore* MakeVecStore(HBasicBlock* block,
                          HInstruction* base,
                          HInstruction* index,
                          HInstruction* value,
                          DataType::Type packed_type,
                          size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
                          uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    SideEffects side_effects = SideEffects::ArrayWriteOfType(packed_type);
    HVecStore* vec_store = new (GetAllocator()) HVecStore(
        GetAllocator(), base, index, value, packed_type, side_effects, num_of_elements, dex_pc);
    AddOrInsertInstruction(block, vec_store);
    return vec_store;
  }

  HVecPredSetAll* MakeVecPredSetAll(HBasicBlock* block,
                                    HInstruction* input,
                                    DataType::Type packed_type,
                                    size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
                                    uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    HVecPredSetAll* predicate = new (GetAllocator()) HVecPredSetAll(
        GetAllocator(), input, packed_type, num_of_elements, dex_pc);
    AddOrInsertInstruction(block, predicate);
    return predicate;
  }

  HVecReplicateScalar* MakeVecReplicateScalar(
      HBasicBlock* block,
      HInstruction* scalar,
      DataType::Type packed_type,
      size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
      HVecPredSetOperation* predicate = nullptr,
      uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    HVecReplicateScalar* vec_replicate_scalar = new (GetAllocator()) HVecReplicateScalar(
        GetAllocator(), scalar, packed_type, num_of_elements, dex_pc);
    AddOrInsertInstruction(block, vec_replicate_scalar);
    if (predicate != nullptr) {
      vec_replicate_scalar->SetMergingGoverningPredicate(predicate);
    }
    return vec_replicate_scalar;
  }

  HVecPredToBoolean* MakeVecPredToBoolean(
      HBasicBlock* block,
      HInstruction* input,
      HVecPredToBoolean::PCondKind pred_cond,
      DataType::Type packed_type,
      size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
      uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    HVecPredToBoolean* vec_pred_to_boolean = new (GetAllocator()) HVecPredToBoolean(
        GetAllocator(),
        input,
        pred_cond,
        packed_type,
        num_of_elements,
        dex_pc);
    AddOrInsertInstruction(block, vec_pred_to_boolean);
    return vec_pred_to_boolean;
  }

  HVecPredWhile* MakeVecPredWhile(HBasicBlock* block,
                                  HInstruction* left,
                                  HInstruction* right,
                                  HVecPredWhile::CondKind cond,
                                  DataType::Type packed_type,
                                  size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
                                  uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    HVecPredWhile* vec_pred_while = new (GetAllocator()) HVecPredWhile(
        GetAllocator(),
        left,
        right,
        cond,
        packed_type,
        num_of_elements,
        dex_pc);
    AddOrInsertInstruction(block, vec_pred_while);
    return vec_pred_while;
  }

  HInvokeStaticOrDirect* MakeInvokeStatic(HBasicBlock* block,
                                          DataType::Type return_type,
                                          const std::vector<HInstruction*>& args,
                                          std::initializer_list<HInstruction*> env = {},
                                          uint32_t dex_pc = kNoDexPc) {
    MethodReference method_reference{/* file= */ &graph_->GetDexFile(), /* index= */ method_idx_++};
    size_t num_64bit_args = std::count_if(args.begin(), args.end(), [](HInstruction* insn) {
      return DataType::Is64BitType(insn->GetType());
    });
    HInvokeStaticOrDirect* invoke = new (GetAllocator())
        HInvokeStaticOrDirect(GetAllocator(),
                              args.size(),
                              /* number_of_out_vregs= */ args.size() + num_64bit_args,
                              return_type,
                              dex_pc,
                              method_reference,
                              /* resolved_method= */ nullptr,
                              HInvokeStaticOrDirect::DispatchInfo{},
                              InvokeType::kStatic,
                              /* resolved_method_reference= */ method_reference,
                              HInvokeStaticOrDirect::ClinitCheckRequirement::kNone,
                              !graph_->IsDebuggable());
    for (auto [ins, idx] : ZipCount(MakeIterationRange(args))) {
      invoke->SetRawInputAt(idx, ins);
    }
    AddOrInsertInstruction(block, invoke);
    ManuallyBuildEnvFor(invoke, env);
    return invoke;
  }

  template <typename Type>
  Type* MakeBinOp(HBasicBlock* block,
                  DataType::Type result_type,
                  HInstruction* left,
                  HInstruction* right,
                  uint32_t dex_pc = kNoDexPc) {
    static_assert(std::is_base_of_v<HBinaryOperation, Type>);
    Type* insn = new (GetAllocator()) Type(result_type, left, right, dex_pc);
    AddOrInsertInstruction(block, insn);
    return insn;
  }

  HCondition* MakeCondition(HBasicBlock* block,
                            IfCondition cond,
                            HInstruction* first,
                            HInstruction* second,
                            uint32_t dex_pc = kNoDexPc) {
    HCondition* condition = HCondition::Create(graph_, cond, first, second, dex_pc);
    AddOrInsertInstruction(block, condition);
    return condition;
  }

  HVecCondition* MakeVecCondition(HBasicBlock* block,
                                  IfCondition cond,
                                  HInstruction* first,
                                  HInstruction* second,
                                  DataType::Type packed_type,
                                  size_t vector_size_in_bytes = kDefaultTestVectorSizeInBytes,
                                  HVecPredSetOperation* predicate = nullptr,
                                  uint32_t dex_pc = kNoDexPc) {
    size_t num_of_elements = GetNumberOfElementsInVector(vector_size_in_bytes, packed_type);
    HVecCondition* condition = HVecCondition::Create(graph_,
                                                     cond,
                                                     first,
                                                     second,
                                                     packed_type,
                                                     num_of_elements,
                                                     dex_pc);
    AddOrInsertInstruction(block, condition);
    if (predicate != nullptr) {
      condition->SetMergingGoverningPredicate(predicate);
    }
    return condition;
  }

  HSelect* MakeSelect(HBasicBlock* block,
                      HInstruction* condition,
                      HInstruction* true_value,
                      HInstruction* false_value,
                      uint32_t dex_pc = kNoDexPc) {
    HSelect* select = new (GetAllocator()) HSelect(condition, true_value, false_value, dex_pc);
    AddOrInsertInstruction(block, select);
    return select;
  }

  HSuspendCheck* MakeSuspendCheck(HBasicBlock* block,
                                  std::initializer_list<HInstruction*> env = {},
                                  uint32_t dex_pc = kNoDexPc) {
    HSuspendCheck* suspend_check = new (GetAllocator()) HSuspendCheck(dex_pc);
    AddOrInsertInstruction(block, suspend_check);
    ManuallyBuildEnvFor(suspend_check, env);
    return suspend_check;
  }

  void AddOrInsertInstruction(HBasicBlock* block, HInstruction* instruction) {
    CHECK(!instruction->IsControlFlow());
    if (block->GetLastInstruction() != nullptr && block->GetLastInstruction()->IsControlFlow()) {
      block->InsertInstructionBefore(instruction, block->GetLastInstruction());
    } else {
      block->AddInstruction(instruction);
    }
  }

  HIf* MakeIf(HBasicBlock* block, HInstruction* cond, uint32_t dex_pc = kNoDexPc) {
    HIf* if_insn = new (GetAllocator()) HIf(cond, dex_pc);
    block->AddInstruction(if_insn);
    return if_insn;
  }

  HGoto* MakeGoto(HBasicBlock* block, uint32_t dex_pc = kNoDexPc) {
    HGoto* goto_insn = new (GetAllocator()) HGoto(dex_pc);
    block->AddInstruction(goto_insn);
    return goto_insn;
  }

  HReturnVoid* MakeReturnVoid(HBasicBlock* block, uint32_t dex_pc = kNoDexPc) {
    HReturnVoid* return_void = new (GetAllocator()) HReturnVoid(dex_pc);
    block->AddInstruction(return_void);
    return return_void;
  }

  HReturn* MakeReturn(HBasicBlock* block, HInstruction* value, uint32_t dex_pc = kNoDexPc) {
    HReturn* return_insn = new (GetAllocator()) HReturn(value, dex_pc);
    block->AddInstruction(return_insn);
    return return_insn;
  }

  HExit* MakeExit(HBasicBlock* exit_block) {
    HExit* exit = new (GetAllocator()) HExit();
    exit_block->AddInstruction(exit);
    return exit;
  }

  HPhi* MakePhi(HBasicBlock* block, const std::vector<HInstruction*>& ins) {
    EXPECT_GE(ins.size(), 2u) << "Phi requires at least 2 inputs";
    DataType::Type type = DataType::Kind(ins[0]->GetType());
    HPhi* phi = new (GetAllocator()) HPhi(GetAllocator(), kNoRegNumber, ins.size(), type);
    for (auto [i, idx] : ZipCount(MakeIterationRange(ins))) {
      phi->SetRawInputAt(idx, i);
    }
    block->AddPhi(phi);
    return phi;
  }

  std::tuple<HPhi*, HAdd*> MakeLinearLoopVar(HBasicBlock* header,
                                             HBasicBlock* body,
                                             int32_t initial,
                                             int32_t increment) {
    HInstruction* initial_const = graph_->GetIntConstant(initial);
    HInstruction* increment_const = graph_->GetIntConstant(increment);
    return MakeLinearLoopVar(header, body, initial_const, increment_const);
  }

  std::tuple<HPhi*, HAdd*> MakeLinearLoopVar(HBasicBlock* header,
                                             HBasicBlock* body,
                                             HInstruction* initial,
                                             HInstruction* increment) {
    HPhi* phi = MakePhi(header, {initial, /* placeholder */ initial});
    HAdd* add = MakeBinOp<HAdd>(body, phi->GetType(), phi, increment);
    phi->ReplaceInput(add, 1u);  // Update back-edge input.
    return {phi, add};
  }

  std::tuple<HPhi*, HPhi*, HAdd*> MakeLinearIrreducibleLoopVar(HBasicBlock* left_header,
                                                               HBasicBlock* right_header,
                                                               HBasicBlock* body,
                                                               HInstruction* left_initial,
                                                               HInstruction* right_initial,
                                                               HInstruction* increment) {
    HPhi* left_phi = MakePhi(left_header, {left_initial, /* placeholder */ left_initial});
    HAdd* add = MakeBinOp<HAdd>(body, left_phi->GetType(), left_phi, increment);
    HPhi* right_phi = MakePhi(right_header, {right_initial, add});
    left_phi->ReplaceInput(right_phi, 1u);  // Update back-edge input.
    return {left_phi, right_phi, add};
  }

  dex::TypeIndex DefaultTypeIndexForType(DataType::Type type) {
    switch (type) {
      case DataType::Type::kBool:
        return dex::TypeIndex(1);
      case DataType::Type::kUint8:
      case DataType::Type::kInt8:
        return dex::TypeIndex(2);
      case DataType::Type::kUint16:
      case DataType::Type::kInt16:
        return dex::TypeIndex(3);
      case DataType::Type::kUint32:
      case DataType::Type::kInt32:
        return dex::TypeIndex(4);
      case DataType::Type::kUint64:
      case DataType::Type::kInt64:
        return dex::TypeIndex(5);
      case DataType::Type::kReference:
        return dex::TypeIndex(6);
      case DataType::Type::kFloat32:
        return dex::TypeIndex(7);
      case DataType::Type::kFloat64:
        return dex::TypeIndex(8);
      case DataType::Type::kVoid:
        EXPECT_TRUE(false) << "No type for void!";
        return dex::TypeIndex(1000);
    }
  }

  // Creates a parameter. The instruction is automatically added to the entry-block.
  HParameterValue* MakeParam(DataType::Type type, std::optional<dex::TypeIndex> ti = std::nullopt) {
    HParameterValue* val = new (GetAllocator()) HParameterValue(
        graph_->GetDexFile(), ti ? *ti : DefaultTypeIndexForType(type), param_count_++, type);
    AddOrInsertInstruction(graph_->GetEntryBlock(), val);
    return val;
  }

  static bool PredecessorsEqual(HBasicBlock* block,
                                std::initializer_list<HBasicBlock*> expected) {
    return RangeEquals(block->GetPredecessors(), expected);
  }

  static bool InputsEqual(HInstruction* instruction,
                          std::initializer_list<HInstruction*> expected) {
    return RangeEquals(instruction->GetInputs(), expected);
  }

  // Returns if the `instruction` is removed from the graph.
  static inline bool IsRemoved(HInstruction* instruction) {
    return instruction->GetBlock() == nullptr;
  }

  // Returns if the `block` is removed from the graph.
  static inline bool IsRemoved(HBasicBlock* block) {
    return block->GetGraph() == nullptr;
  }

 protected:
  bool CheckGraph(HGraph* graph, std::ostream& oss) {
    GraphChecker checker(graph);
    checker.Run();
    checker.Dump(oss);
    return checker.IsValid();
  }

  template <typename Range, typename ElementType>
  static bool RangeEquals(Range&& range, std::initializer_list<ElementType> expected) {
    return std::distance(range.begin(), range.end()) == expected.size() &&
           std::equal(range.begin(), range.end(), expected.begin());
  }

  std::vector<std::unique_ptr<const StandardDexFile>> dex_files_;
  std::unique_ptr<ArenaPoolAndAllocator> pool_and_allocator_;

  HGraph* graph_;
  HBasicBlock* entry_block_;
  HBasicBlock* exit_block_;

  size_t param_count_ = 0;
  size_t class_idx_ = 42;
  uint32_t method_idx_ = 100;

  // The default size of vectors to use for tests, in bytes. 16 bytes (128 bits) is used as it is
  // commonly the smallest size of vector used in vector extensions.
  static constexpr size_t kDefaultTestVectorSizeInBytes = 16;

  ScopedNullHandle<mirror::Class> null_klass_;
};

class OptimizingUnitTest : public CommonArtTest, public OptimizingUnitTestHelper {};

// Naive string diff data type.
using diff_t = std::list<std::pair<std::string, std::string>>;

// An alias for the empty string used to make it clear that a line is
// removed in a diff.
static const std::string removed = "";  // NOLINT [runtime/string] [4]

// Naive patch command: apply a diff to a string.
inline std::string Patch(const std::string& original, const diff_t& diff) {
  std::string result = original;
  for (const auto& p : diff) {
    std::string::size_type pos = result.find(p.first);
    DCHECK_NE(pos, std::string::npos)
        << "Could not find: \"" << p.first << "\" in \"" << result << "\"";
    result.replace(pos, p.first.size(), p.second);
  }
  return result;
}

inline std::ostream& operator<<(std::ostream& oss, const AdjacencyListGraph& alg) {
  return alg.Dump(oss);
}

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_OPTIMIZING_UNIT_TEST_H_
