/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "fast_compiler.h"

#include "code_generation_data.h"
#include "code_generator_arm64.h"
#include "data_type-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_instruction-inl.h"
#include "driver/dex_compilation_unit.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "jit_patches_arm64.h"
#include "nodes.h"
#include "thread-inl.h"
#include "utils/arm64/assembler_arm64.h"

// TODO(VIXL): Make VIXL compile with -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "aarch64/disasm-aarch64.h"
#include "aarch64/macro-assembler-aarch64.h"
#pragma GCC diagnostic pop

using namespace vixl::aarch64;  // NOLINT(build/namespaces)
using vixl::ExactAssemblyScope;
using vixl::CodeBufferCheckScope;
using vixl::EmissionCheckScope;

#ifdef __
#error "ARM64 Codegen VIXL macro-assembler macro already defined."
#endif
#define __ GetVIXLAssembler()->

namespace art HIDDEN {
namespace arm64 {

using helpers::CPURegisterFrom;
using helpers::HeapOperand;
using helpers::LocationFrom;
using helpers::RegisterFrom;
using helpers::WRegisterFrom;
using helpers::DRegisterFrom;
using helpers::SRegisterFrom;

static const vixl::aarch64::Register kAvailableCalleeSaveRegisters[] = {
  vixl::aarch64::x22,
  vixl::aarch64::x23,
  vixl::aarch64::x24,
  vixl::aarch64::x25,
  vixl::aarch64::x26,
  vixl::aarch64::x27,
  vixl::aarch64::x28,
  vixl::aarch64::x29,
};

static const vixl::aarch64::Register kAvailableTempRegisters[] = {
  vixl::aarch64::x8,
  vixl::aarch64::x9,
  vixl::aarch64::x10,
  vixl::aarch64::x11,
  vixl::aarch64::x12,
  vixl::aarch64::x13,
  vixl::aarch64::x14,
  vixl::aarch64::x15,
};

static const vixl::aarch64::VRegister kAvailableCalleeSaveFpuRegisters[] = {
  vixl::aarch64::d8,
  vixl::aarch64::d9,
  vixl::aarch64::d10,
  vixl::aarch64::d11,
  vixl::aarch64::d12,
  vixl::aarch64::d13,
  vixl::aarch64::d14,
  vixl::aarch64::d15,
};

static const vixl::aarch64::VRegister kAvailableTempFpuRegisters[] = {
  vixl::aarch64::d0,
  vixl::aarch64::d1,
  vixl::aarch64::d2,
  vixl::aarch64::d3,
  vixl::aarch64::d4,
  vixl::aarch64::d5,
  vixl::aarch64::d6,
  vixl::aarch64::d7,
};

class FastCompilerARM64 : public FastCompiler {
 public:
  FastCompilerARM64(ArtMethod* method,
                    ArenaAllocator* allocator,
                    ArenaStack* arena_stack,
                    VariableSizedHandleScope* handles,
                    const CompilerOptions& compiler_options,
                    const DexCompilationUnit& dex_compilation_unit)
      : method_(method),
        allocator_(allocator),
        handles_(handles),
        assembler_(allocator,
                   compiler_options.GetInstructionSetFeatures()->AsArm64InstructionSetFeatures()),
        jit_patches_(&assembler_, allocator),
        compiler_options_(compiler_options),
        dex_compilation_unit_(dex_compilation_unit),
        code_generation_data_(CodeGenerationData::Create(arena_stack, InstructionSet::kArm64)),
        vreg_locations_(dex_compilation_unit.GetCodeItemAccessor().RegistersSize(),
                        allocator->Adapter()),
        has_frame_(false),
        core_spill_mask_(0u),
        fpu_spill_mask_(0u),
        object_register_mask_(0u),
        is_non_null_mask_(0u) {
    GetAssembler()->cfi().SetEnabled(compiler_options.GenerateAnyDebugInfo());
  }

  // Top-level method to generate code for `method_`.
  bool Compile();

  ArrayRef<const uint8_t> GetCode() const override {
    return ArrayRef<const uint8_t>(assembler_.CodeBufferBaseAddress(), assembler_.CodeSize());
  }

  ScopedArenaVector<uint8_t> GetStackMaps() const override {
    return code_generation_data_->GetStackMapStream()->Encode();
  }

  ArrayRef<const uint8_t> GetCfiData() const override {
    return ArrayRef<const uint8_t>(*assembler_.cfi().data());
  }

  int32_t GetFrameSize() const override {
    if (!has_frame_) {
      return 0;
    }
    size_t size = FrameEntrySpillSize() +
        /* method */ static_cast<size_t>(kArm64PointerSize) +
        /* out registers */ GetCodeItemAccessor().OutsSize() * kVRegSize;
    return RoundUp(size, kStackAlignment);
  }

  uint32_t GetNumberOfJitRoots() const override {
    return code_generation_data_->GetNumberOfJitRoots();
  }

  void EmitJitRoots(uint8_t* code,
                    const uint8_t* roots_data,
                    /*out*/std::vector<Handle<mirror::Object>>* roots) override
       REQUIRES_SHARED(Locks::mutator_lock_) {
    code_generation_data_->EmitJitRoots(roots);
    jit_patches_.EmitJitRootPatches(code, roots_data, *code_generation_data_);
  }

  ~FastCompilerARM64() override {
    GetVIXLAssembler()->Reset();
  }

  const char* GetUnimplementedReason() const {
    return unimplemented_reason_;
  }

 private:
  // Go over each instruction of the method, and generate code for them.
  bool ProcessInstructions();

  // Initialize the locations of parameters for this method.
  bool InitializeParameters();

  // Generate code for the frame entry. Only called when needed. If the frame
  // entry has already been generated, do nothing.
  void EnsureHasFrame();

  // Generate code for a frame exit.
  void DropFrameAndReturn();

  // Record a stack map at the given dex_pc.
  void RecordPcInfo(uint32_t dex_pc);

  // Generate code to move from one location to another.
  bool MoveLocation(Location destination, Location source, DataType::Type dst_type);

  // Get a register location for the dex register `reg`. Saves the location into
  // `vreg_locations_` for next uses of `reg`.
  // `next` should be the next dex instruction, to help choose the register.
  Location CreateNewRegisterLocation(uint32_t reg, DataType::Type type, const Instruction* next);

  // Return the existing register location for `reg`.
  Location GetExistingRegisterLocation(uint32_t reg, DataType::Type type);

  // Generate code for one instruction.
  bool ProcessDexInstruction(const Instruction& instruction,
                             uint32_t dex_pc,
                             const Instruction* next);

  // Setup the arguments for an invoke.
  bool SetupArguments(InvokeType invoke_type,
                      const InstructionOperands& operands,
                      const char* shorty,
                      /* out */ uint32_t* obj_reg);

  // Generate code for doing a Java invoke.
  bool HandleInvoke(const Instruction& instruction, uint32_t dex_pc, InvokeType invoke_type);

  // Generate code for doing a runtime invoke.
  void InvokeRuntime(QuickEntrypointEnum entrypoint, uint32_t dex_pc);

  void BuildLoadString(uint32_t vreg, dex::StringIndex string_index, const Instruction* next);
  void BuildNewInstance(
      uint32_t vreg, dex::TypeIndex string_index, uint32_t dex_pc, const Instruction* next);
  void BuildCheckCast(uint32_t vreg, dex::TypeIndex type_index, uint32_t dex_pc);
  bool LoadMethod(Register reg, ArtMethod* method);
  void DoReadBarrierOn(Register reg, vixl::aarch64::Label* exit = nullptr, bool do_mr_check = true);
  bool CanGenerateCodeFor(ArtField* field, bool can_receiver_be_null)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Mark whether dex register `vreg_index` is an object.
  void UpdateRegisterMask(uint32_t vreg_index, bool is_object) {
    // Note that the register mask is only useful when there is a frame, so we
    // use the callee save registers for the mask.
    if (is_object) {
      object_register_mask_ |= (1 << kAvailableCalleeSaveRegisters[vreg_index].GetCode());
    } else {
      object_register_mask_ &= ~(1 << kAvailableCalleeSaveRegisters[vreg_index].GetCode());
    }
  }

  // Mark whether dex register `vreg_index` can be null.
  void UpdateNonNullMask(uint32_t vreg_index, bool can_be_null) {
    if (can_be_null) {
      is_non_null_mask_ &= ~(1 << vreg_index);
    } else {
      is_non_null_mask_ |= (1 << vreg_index);
    }
  }

  // Update information about dex register `vreg_index`.
  void UpdateLocal(uint32_t vreg_index, bool is_object, bool can_be_null = true) {
    UpdateRegisterMask(vreg_index, is_object);
    UpdateNonNullMask(vreg_index, can_be_null);
  }

  // Whether dex register `vreg_index` can be null.
  bool CanBeNull(uint32_t vreg_index) const {
    return !(is_non_null_mask_ & (1 << vreg_index));
  }

  // Compiler utilities.
  //
  Arm64Assembler* GetAssembler() { return &assembler_; }
  vixl::aarch64::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->GetVIXLAssembler(); }
  const DexFile& GetDexFile() const { return *dex_compilation_unit_.GetDexFile(); }
  const CodeItemDataAccessor& GetCodeItemAccessor() const {
    return dex_compilation_unit_.GetCodeItemAccessor();
  }
  bool HitUnimplemented() const {
    return unimplemented_reason_[0] != '\0';
  }

  // Frame related utilities.
  //
  uint32_t GetCoreSpillSize() const {
    return GetFramePreservedCoreRegisters().GetTotalSizeInBytes();
  }
  uint32_t FrameEntrySpillSize() const {
    return GetFramePreservedFPRegisters().GetTotalSizeInBytes() + GetCoreSpillSize();
  }
  CPURegList GetFramePreservedCoreRegisters() const {
    return CPURegList(CPURegister::kRegister, kXRegSize, core_spill_mask_);
  }
  CPURegList GetFramePreservedFPRegisters() const {
    return CPURegList(CPURegister::kVRegister, kDRegSize, fpu_spill_mask_);
  }

  // Method being compiled.
  ArtMethod* method_;

  // Allocator for any allocation happening in the compiler.
  ArenaAllocator* allocator_;

  VariableSizedHandleScope* handles_;

  // Compilation utilities.
  Arm64Assembler assembler_;
  JitPatchesARM64 jit_patches_;
  const CompilerOptions& compiler_options_;
  const DexCompilationUnit& dex_compilation_unit_;
  std::unique_ptr<CodeGenerationData> code_generation_data_;

  // The current location of each dex register.
  ArenaVector<Location> vreg_locations_;

  // Whether we've created a frame for this compiled method.
  bool has_frame_;

  // CPU registers that have been spilled in the frame.
  uint32_t core_spill_mask_;

  // FPU registers that have been spilled in the frame.
  uint32_t fpu_spill_mask_;

  // The current mask to know which physical register holds an object.
  uint64_t object_register_mask_;

  // The current mask to know if a dex register is known non-null.
  uint64_t is_non_null_mask_;

  // The return type of the compiled method. Saved to avoid re-computing it on
  // the return instruction.
  DataType::Type return_type_;

  // The type of the last invoke instruction.
  DataType::Type previous_invoke_type_;

  // If non-empty, the reason the compilation could not be finished.
  const char* unimplemented_reason_ = "";
};

bool FastCompilerARM64::InitializeParameters() {
  if (GetCodeItemAccessor().TriesSize() != 0) {
    // TODO: Support try/catch.
    unimplemented_reason_ = "TryCatch";
    return false;
  }
  const char* shorty = dex_compilation_unit_.GetShorty();
  uint16_t number_of_vregs = GetCodeItemAccessor().RegistersSize();
  uint16_t number_of_parameters = GetCodeItemAccessor().InsSize();
  uint16_t vreg_parameter_index = number_of_vregs - number_of_parameters;

  if (number_of_vregs > arraysize(kAvailableTempRegisters) ||
      number_of_vregs > arraysize(kAvailableCalleeSaveRegisters) ||
      number_of_vregs > arraysize(kAvailableTempFpuRegisters) ||
      number_of_vregs > arraysize(kAvailableCalleeSaveFpuRegisters)) {
    // Too many registers for this compiler.
    unimplemented_reason_ = "TooManyRegisters";
    return false;
  }

  InvokeDexCallingConventionVisitorARM64 convention;
  if (!dex_compilation_unit_.IsStatic()) {
    // Add the implicit 'this' argument, not expressed in the signature.
    vreg_locations_[vreg_parameter_index] = convention.GetNextLocation(DataType::Type::kReference);
    UpdateLocal(vreg_parameter_index, /* is_object= */ true, /* can_be_null= */ false);
    ++vreg_parameter_index;
    --number_of_parameters;
  }

  for (int i = 0, shorty_pos = 1;
       i < number_of_parameters;
       i++, shorty_pos++, vreg_parameter_index++) {
    DataType::Type type = DataType::FromShorty(shorty[shorty_pos]);
    vreg_locations_[vreg_parameter_index] = convention.GetNextLocation(type);
    UpdateLocal(vreg_parameter_index,
                /* is_object= */ (type == DataType::Type::kReference),
                /* can_be_null= */ true);
    if (DataType::Is64BitType(type)) {
      ++i;
      ++vreg_parameter_index;
    }
  }
  return_type_ = DataType::FromShorty(shorty[0]);
  return true;
}

bool FastCompilerARM64::ProcessInstructions() {
  DCHECK(GetCodeItemAccessor().HasCodeItem());

  DexInstructionIterator it = GetCodeItemAccessor().begin();
  DexInstructionIterator end = GetCodeItemAccessor().end();
  DCHECK(it != end);
  do {
    DexInstructionPcPair pair = *it;
    ++it;

    // Fetch the next instruction as a micro-optimization currently only used
    // for optimizing returns.
    const Instruction* next = nullptr;
    if (it != end) {
      const DexInstructionPcPair& next_pair = *it;
      next = &next_pair.Inst();
    }

    if (!ProcessDexInstruction(pair.Inst(), pair.DexPc(), next)) {
      DCHECK(HitUnimplemented());
      return false;
    }
    // Note: There may be no Thread for gtests.
    DCHECK(Thread::Current() == nullptr || !Thread::Current()->IsExceptionPending())
        << GetDexFile().PrettyMethod(dex_compilation_unit_.GetDexMethodIndex())
        << " " << pair.Inst().Name() << "@" << pair.DexPc();

    if (HitUnimplemented()) {
      return false;
    }
  } while (it != end);
  return true;
}

bool FastCompilerARM64::MoveLocation(Location destination,
                                     Location source,
                                     DataType::Type dst_type) {
  if (source.Equals(destination)) {
    return true;
  }
  if (source.IsRegister() && destination.IsRegister()) {
    CPURegister dst = CPURegisterFrom(destination, dst_type);
    __ Mov(Register(dst), RegisterFrom(source, dst_type));
    return true;
  }
  if (source.IsConstant() && destination.IsRegister()) {
    if (source.GetConstant()->IsIntConstant()) {
      __ Mov(RegisterFrom(destination, DataType::Type::kInt32),
             source.GetConstant()->AsIntConstant()->GetValue());
      return true;
    }
  }
  unimplemented_reason_ = "MoveLocation";
  return false;
}

Location FastCompilerARM64::CreateNewRegisterLocation(uint32_t reg,
                                                      DataType::Type type,
                                                      const Instruction* next) {
  if (next != nullptr &&
      (next->Opcode() == Instruction::RETURN_OBJECT || next->Opcode() == Instruction::RETURN)) {
    // If the next instruction is a return, use the return register from the calling
    // convention.
    InvokeDexCallingConventionVisitorARM64 convention;
    vreg_locations_[reg] = convention.GetReturnLocation(return_type_);
    return vreg_locations_[reg];
  } else if (vreg_locations_[reg].IsStackSlot() ||
             vreg_locations_[reg].IsDoubleStackSlot()) {
    unimplemented_reason_ = "MoveStackSlot";
    // Return a phony location.
    return DataType::IsFloatingPointType(type)
        ? Location::FpuRegisterLocation(1)
        : Location::RegisterLocation(1);
  } else if (DataType::IsFloatingPointType(type)) {
    if (vreg_locations_[reg].IsFpuRegister()) {
      // Re-use existing register.
      return vreg_locations_[reg];
    } else if (has_frame_) {
      // TODO: Regenerate the method with floating point support.
      unimplemented_reason_ = "FpuRegisterAllocation";
      vreg_locations_[reg] = Location::FpuRegisterLocation(1);
      return vreg_locations_[reg];
    } else {
      vreg_locations_[reg] =
          Location::FpuRegisterLocation(kAvailableTempFpuRegisters[reg].GetCode());
      return vreg_locations_[reg];
    }
  } else if (vreg_locations_[reg].IsRegister()) {
    // Re-use existing register.
    return vreg_locations_[reg];
  } else {
    // Get the associated register with `reg`.
    uint32_t register_code = has_frame_
        ? kAvailableCalleeSaveRegisters[reg].GetCode()
        : kAvailableTempRegisters[reg].GetCode();
    vreg_locations_[reg] = Location::RegisterLocation(register_code);
    return vreg_locations_[reg];
  }
}

Location FastCompilerARM64::GetExistingRegisterLocation(uint32_t reg, DataType::Type type) {
  if (vreg_locations_[reg].IsStackSlot() || vreg_locations_[reg].IsDoubleStackSlot()) {
    unimplemented_reason_ = "MoveStackSlot";
    // Return a phony location.
    return DataType::IsFloatingPointType(type)
        ? Location::FpuRegisterLocation(1)
        : Location::RegisterLocation(1);
  } else if (DataType::IsFloatingPointType(type)) {
    if (vreg_locations_[reg].IsFpuRegister()) {
      return vreg_locations_[reg];
    } else {
      // TODO: Regenerate the method with floating point support.
      unimplemented_reason_ = "FpuRegisterAllocation";
      vreg_locations_[reg] = Location::FpuRegisterLocation(1);
      return vreg_locations_[reg];
    }
  } else if (vreg_locations_[reg].IsRegister()) {
    return vreg_locations_[reg];
  } else {
    unimplemented_reason_ = "UnknownLocation";
    vreg_locations_[reg] = Location::RegisterLocation(1);
    return Location::RegisterLocation(1);
  }
}

void FastCompilerARM64::RecordPcInfo(uint32_t dex_pc) {
  DCHECK(has_frame_);
  uint32_t native_pc = GetAssembler()->CodePosition();
  StackMapStream* stack_map_stream = code_generation_data_->GetStackMapStream();
  CHECK_EQ(object_register_mask_ & callee_saved_core_registers.GetList(), object_register_mask_);
  stack_map_stream->BeginStackMapEntry(dex_pc, native_pc, object_register_mask_);
  stack_map_stream->EndStackMapEntry();
}

void FastCompilerARM64::DropFrameAndReturn() {
  if (has_frame_) {
    CodeGeneratorARM64::DropFrameAndReturn(GetAssembler(),
                                           GetVIXLAssembler(),
                                           GetFrameSize(),
                                           GetCoreSpillSize(),
                                           GetFramePreservedCoreRegisters(),
                                           FrameEntrySpillSize(),
                                           GetFramePreservedFPRegisters());
  } else {
    DCHECK_EQ(GetFrameSize(), 0);
    __ Ret();
  }
}

void FastCompilerARM64::EnsureHasFrame() {
  if (has_frame_) {
    // Frame entry has already been generated.
    return;
  }
  has_frame_ = true;
  uint16_t number_of_vregs = GetCodeItemAccessor().RegistersSize();
  for (int i = 0; i < number_of_vregs; ++i) {
    // Assume any vreg will be held in a callee-save register.
    core_spill_mask_ |= (1 << kAvailableCalleeSaveRegisters[i].GetCode());
    if (vreg_locations_[i].IsFpuRegister()) {
      // TODO: Re-generate method with floating points.
      unimplemented_reason_ = "FloatingPoint";
    }
  }
  core_spill_mask_ |= (1 << lr.GetCode());

  code_generation_data_->GetStackMapStream()->BeginMethod(GetFrameSize(),
                                                          core_spill_mask_,
                                                          fpu_spill_mask_,
                                                          GetCodeItemAccessor().RegistersSize(),
                                                          /* is_compiling_baseline= */ true,
                                                          /* is_debuggable= */ false);
  MacroAssembler* masm = GetVIXLAssembler();
  {
    UseScratchRegisterScope temps(masm);
    Register temp = temps.AcquireX();
    __ Sub(temp, sp, static_cast<int32_t>(GetStackOverflowReservedBytes(InstructionSet::kArm64)));
    // Ensure that between load and RecordPcInfo there are no pools emitted.
    ExactAssemblyScope eas(GetVIXLAssembler(),
                           kInstructionSize,
                           CodeBufferCheckScope::kExactSize);
    __ ldr(wzr, MemOperand(temp, 0));
    RecordPcInfo(0);
  }

  // Stack layout:
  //      sp[frame_size - 8]        : lr.
  //      ...                       : other preserved core registers.
  //      ...                       : other preserved fp registers.
  //      ...                       : reserved frame space.
  //      sp[0]                     : current method.
  int32_t frame_size = GetFrameSize();
  uint32_t core_spills_offset = frame_size - GetCoreSpillSize();
  CPURegList preserved_core_registers = GetFramePreservedCoreRegisters();
  DCHECK(!preserved_core_registers.IsEmpty());
  uint32_t fp_spills_offset = frame_size - FrameEntrySpillSize();
  CPURegList preserved_fp_registers = GetFramePreservedFPRegisters();

  // Save the current method if we need it, or if using STP reduces code
  // size. Note that we do not do this in HCurrentMethod, as the
  // instruction might have been removed in the SSA graph.
  CPURegister lowest_spill;
  if (core_spills_offset == kXRegSizeInBytes) {
    // If there is no gap between the method and the lowest core spill, use
    // aligned STP pre-index to store both. Max difference is 512. We do
    // that to reduce code size even if we do not have to save the method.
    DCHECK_LE(frame_size, 512);  // 32 core registers are only 256 bytes.
    lowest_spill = preserved_core_registers.PopLowestIndex();
    __ Stp(kArtMethodRegister, lowest_spill, MemOperand(sp, -frame_size, PreIndex));
  } else {
    __ Str(kArtMethodRegister, MemOperand(sp, -frame_size, PreIndex));
  }
  GetAssembler()->cfi().AdjustCFAOffset(frame_size);
  if (lowest_spill.IsValid()) {
    GetAssembler()->cfi().RelOffset(DWARFReg(lowest_spill), core_spills_offset);
    core_spills_offset += kXRegSizeInBytes;
  }
  GetAssembler()->SpillRegisters(preserved_core_registers, core_spills_offset);
  GetAssembler()->SpillRegisters(preserved_fp_registers, fp_spills_offset);

  // Move registers which are currently allocated from caller-saves to callee-saves.
  for (int i = 0; i < number_of_vregs; ++i) {
    if (vreg_locations_[i].IsRegister()) {
      Location new_location =
          Location::RegisterLocation(kAvailableCalleeSaveRegisters[i].GetCode());
      MoveLocation(new_location, vreg_locations_[i], DataType::Type::kInt64);
      vreg_locations_[i] = new_location;
    } else if (vreg_locations_[i].IsFpuRegister()) {
      Location new_location =
          Location::FpuRegisterLocation(kAvailableCalleeSaveFpuRegisters[i].GetCode());
      MoveLocation(new_location, vreg_locations_[i], DataType::Type::kFloat64);
      vreg_locations_[i] = new_location;
    }
  }

  // Increment hotness.
  if (!Runtime::Current()->IsAotCompiler()) {
    uint64_t address = reinterpret_cast64<uint64_t>(method_);
    UseScratchRegisterScope temps(masm);
    Register counter = temps.AcquireW();
    vixl::aarch64::Label increment, done;
    uint32_t entrypoint_offset =
        GetThreadOffset<kArm64PointerSize>(kQuickCompileOptimized).Int32Value();

    __ Ldrh(counter, MemOperand(kArtMethodRegister, ArtMethod::HotnessCountOffset().Int32Value()));
    __ Cbnz(counter, &increment);
    __ Ldr(lr, MemOperand(tr, entrypoint_offset));
    // Note: we don't record the call here (and therefore don't generate a stack
    // map), as the entrypoint should never be suspended.
    __ Blr(lr);
    __ Bind(&increment);
    __ Add(counter, counter, -1);
    __ Strh(counter, MemOperand(kArtMethodRegister, ArtMethod::HotnessCountOffset().Int32Value()));
    __ Bind(&done);
  }

  // Do the suspend check.
  if (compiler_options_.GetImplicitSuspendChecks()) {
    ExactAssemblyScope eas(GetVIXLAssembler(),
                           kInstructionSize,
                           CodeBufferCheckScope::kExactSize);
    __ ldr(kImplicitSuspendCheckRegister, MemOperand(kImplicitSuspendCheckRegister));
    RecordPcInfo(0);
  } else {
    UseScratchRegisterScope temps(masm);
    Register temp = temps.AcquireW();
    vixl::aarch64::Label continue_label;
    __ Ldr(temp, MemOperand(tr, Thread::ThreadFlagsOffset<kArm64PointerSize>().SizeValue()));
    __ Tst(temp, Thread::SuspendOrCheckpointRequestFlags());
    __ B(eq, &continue_label);
    uint32_t entrypoint_offset =
        GetThreadOffset<kArm64PointerSize>(kQuickTestSuspend).Int32Value();
    __ Ldr(lr, MemOperand(tr, entrypoint_offset));
    {
      ExactAssemblyScope eas(GetVIXLAssembler(),
                             kInstructionSize,
                             CodeBufferCheckScope::kExactSize);
      __ blr(lr);
      RecordPcInfo(0);
    }
    __ Bind(&continue_label);
  }
}


bool FastCompilerARM64::SetupArguments(InvokeType invoke_type,
                                       const InstructionOperands& operands,
                                       const char* shorty,
                                       /* out */ uint32_t* obj_reg) {
  const size_t number_of_operands = operands.GetNumberOfOperands();

  size_t start_index = 0u;
  size_t argument_index = 0u;
  InvokeDexCallingConventionVisitorARM64 convention;

  // Handle 'this' parameter.
  if (invoke_type != kStatic) {
    if (number_of_operands == 0u) {
      unimplemented_reason_ = "BogusSignature";
      return false;
    }
    start_index = 1u;
    *obj_reg = operands.GetOperand(0u);
    MoveLocation(convention.GetNextLocation(DataType::Type::kReference),
                 vreg_locations_[*obj_reg],
                 DataType::Type::kReference);
  }

  uint32_t shorty_index = 1;  // Skip the return type.
  // Handle all parameters except 'this'.
  for (size_t i = start_index; i < number_of_operands; ++i, ++argument_index, ++shorty_index) {
    // Make sure we don't go over the expected arguments or over the number of
    // dex registers given. If the instruction was seen as dead by the verifier,
    // it hasn't been properly checked.
    char c = shorty[shorty_index];
    if (UNLIKELY(c == 0)) {
      unimplemented_reason_ = "BogusSignature";
      return false;
    }
    DataType::Type type = DataType::FromShorty(c);
    bool is_wide = (type == DataType::Type::kInt64) || (type == DataType::Type::kFloat64);
    if (is_wide && ((i + 1 == number_of_operands) ||
                    (operands.GetOperand(i) + 1 != operands.GetOperand(i + 1)))) {
      unimplemented_reason_ = "BogusSignature";
      return false;
    }
    MoveLocation(convention.GetNextLocation(type), vreg_locations_[operands.GetOperand(i)], type);
    if (is_wide) {
      ++i;
    }
  }
  return true;
}

bool FastCompilerARM64::LoadMethod(Register reg, ArtMethod* method) {
  if (Runtime::Current()->IsAotCompiler()) {
    unimplemented_reason_ = "AOTLoadMethod";
    return false;
  }
  __ Ldr(reg, jit_patches_.DeduplicateUint64Literal(reinterpret_cast<uint64_t>(method)));
  return true;
}

bool FastCompilerARM64::HandleInvoke(const Instruction& instruction,
                                     uint32_t dex_pc,
                                     InvokeType invoke_type) {
  Instruction::Code opcode = instruction.Opcode();
  uint16_t method_index = (opcode >= Instruction::INVOKE_VIRTUAL_RANGE)
      ? instruction.VRegB_3rc()
      : instruction.VRegB_35c();
  ArtMethod* resolved_method = nullptr;
  size_t offset = 0u;
  {
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    ClassLinker* const class_linker = dex_compilation_unit_.GetClassLinker();
    resolved_method = method_->SkipAccessChecks()
        ? class_linker->ResolveMethodId(method_index, method_)
        : class_linker->ResolveMethodWithChecks(
              method_index, method_, invoke_type);
    if (resolved_method == nullptr) {
      DCHECK(self->IsExceptionPending());
      self->ClearException();
      unimplemented_reason_ = "UnresolvedInvoke";
      return false;
    }

    if (resolved_method->IsConstructor() && resolved_method->GetDeclaringClass()->IsObjectClass()) {
      // Object.<init> is always empty. Return early to not generate a frame.
      if (kIsDebugBuild) {
        CHECK(resolved_method->GetDeclaringClass()->IsVerified());
        CodeItemDataAccessor accessor(*resolved_method->GetDexFile(),
                                      resolved_method->GetCodeItem());
        CHECK_EQ(accessor.InsnsSizeInCodeUnits(), 1u);
        CHECK_EQ(accessor.begin().Inst().Opcode(), Instruction::RETURN_VOID);
      }
      // No need to update `previous_invoke_type_`, we know it is not going the
      // be used.
      return true;
    }

    if (invoke_type == kSuper) {
      resolved_method = method_->SkipAccessChecks()
          ? FindSuperMethodToCall</*access_check=*/false>(method_index,
                                                          resolved_method,
                                                          method_,
                                                          self)
          : FindSuperMethodToCall</*access_check=*/true>(method_index,
                                                         resolved_method,
                                                         method_,
                                                         self);
      if (resolved_method == nullptr) {
        DCHECK(self->IsExceptionPending()) << method_->PrettyMethod();
        self->ClearException();
        unimplemented_reason_ = "UnresolvedInvokeSuper";
        return false;
      }
    } else if (invoke_type == kVirtual) {
      offset = resolved_method->GetVtableIndex();
    } else if (invoke_type == kInterface) {
      offset = resolved_method->GetImtIndex();
    }
  }

  // Given we are calling a method, generate a frame.
  EnsureHasFrame();

  // Setup the arguments for the call.
  uint32_t obj_reg = -1;
  const char* shorty = dex_compilation_unit_.GetDexFile()->GetMethodShorty(method_index);
  if (opcode >= Instruction::INVOKE_VIRTUAL_RANGE) {
    RangeInstructionOperands operands(instruction.VRegC(), instruction.VRegA_3rc());
    if (!SetupArguments(invoke_type, operands, shorty, &obj_reg)) {
      return false;
    }
  } else {
    uint32_t args[5];
    uint32_t number_of_vreg_arguments = instruction.GetVarArgs(args);
    VarArgsInstructionOperands operands(args, number_of_vreg_arguments);
    if (!SetupArguments(invoke_type, operands, shorty, &obj_reg)) {
      return false;
    }
  }
  // Save the invoke type for the next move-result instruction.
  previous_invoke_type_ = DataType::FromShorty(shorty[0]);

  if (invoke_type != kStatic) {
    bool can_be_null = CanBeNull(obj_reg);
    // Load the class of the instance. For kDirect and kSuper, this acts as a
    // null check.
    if (can_be_null || invoke_type == kVirtual || invoke_type == kInterface) {
      InvokeDexCallingConvention calling_convention;
      Register receiver = calling_convention.GetRegisterAt(0);
      Offset class_offset = mirror::Object::ClassOffset();
      EmissionCheckScope guard(GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
      __ Ldr(kArtMethodRegister.W(), HeapOperand(receiver.W(), class_offset));
      if (can_be_null) {
        RecordPcInfo(dex_pc);
      }
    }
  }

  if (invoke_type == kVirtual) {
    size_t method_offset =
        mirror::Class::EmbeddedVTableEntryOffset(offset, kArm64PointerSize).SizeValue();
    __ Ldr(kArtMethodRegister, MemOperand(kArtMethodRegister, method_offset));
  } else if (invoke_type == kInterface) {
    __ Ldr(kArtMethodRegister,
           MemOperand(kArtMethodRegister,
                      mirror::Class::ImtPtrOffset(kArm64PointerSize).Uint32Value()));
    uint32_t method_offset =
        static_cast<uint32_t>(ImTable::OffsetOfElement(offset, kArm64PointerSize));
    __ Ldr(kArtMethodRegister, MemOperand(kArtMethodRegister, method_offset));
    LoadMethod(ip1, resolved_method);
  } else {
    DCHECK(invoke_type == kDirect || invoke_type == kSuper || invoke_type == kStatic);
    LoadMethod(kArtMethodRegister, resolved_method);
  }

  Offset entry_point = ArtMethod::EntryPointFromQuickCompiledCodeOffset(kArm64PointerSize);
  __ Ldr(lr, MemOperand(kArtMethodRegister, entry_point.SizeValue()));
  {
    // Use a scope to help guarantee that `RecordPcInfo()` records the correct pc.
    ExactAssemblyScope eas(GetVIXLAssembler(), kInstructionSize, CodeBufferCheckScope::kExactSize);
    __ blr(lr);
    RecordPcInfo(dex_pc);
  }
  return true;
}

void FastCompilerARM64::InvokeRuntime(QuickEntrypointEnum entrypoint, uint32_t dex_pc) {
  ThreadOffset64 entrypoint_offset = GetThreadOffset<kArm64PointerSize>(entrypoint);
  __ Ldr(lr, MemOperand(tr, entrypoint_offset.Int32Value()));
  // Ensure the pc position is recorded immediately after the `blr` instruction.
  ExactAssemblyScope eas(GetVIXLAssembler(), kInstructionSize, CodeBufferCheckScope::kExactSize);
  __ blr(lr);
  if (EntrypointRequiresStackMap(entrypoint)) {
    RecordPcInfo(dex_pc);
  }
}

void FastCompilerARM64::BuildLoadString(uint32_t vreg,
                                        dex::StringIndex string_index,
                                        const Instruction* next) {
  // Generate a frame because of the read barrier.
  EnsureHasFrame();
  Location loc = CreateNewRegisterLocation(vreg, DataType::Type::kReference, next);
  if (Runtime::Current()->IsAotCompiler()) {
    unimplemented_reason_ = "AOTLoadString";
    return;
  }

  ScopedObjectAccess soa(Thread::Current());
  ClassLinker* const class_linker = dex_compilation_unit_.GetClassLinker();
  ObjPtr<mirror::String> str = class_linker->ResolveString(string_index, method_);
  if (str == nullptr) {
    unimplemented_reason_ = "NullString";
    return;
  }

  Handle<mirror::String> h_str = handles_->NewHandle(str);
  Register dst = RegisterFrom(loc, DataType::Type::kReference);
  __ Ldr(dst.W(), jit_patches_.DeduplicateJitStringLiteral(GetDexFile(),
                                                           string_index,
                                                           h_str,
                                                           code_generation_data_.get()));
  __ Ldr(dst.W(), MemOperand(dst.X()));
  DoReadBarrierOn(dst);
  UpdateLocal(vreg, /* is_object= */ true, /* can_be_null= */ false);
}

void FastCompilerARM64::BuildNewInstance(uint32_t vreg,
                                         dex::TypeIndex type_index,
                                         uint32_t dex_pc,
                                         const Instruction* next) {
  EnsureHasFrame();
  if (Runtime::Current()->IsAotCompiler()) {
    unimplemented_reason_ = "AOTNewInstance";
    return;
  }

  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = dex_compilation_unit_.GetClassLinker()->ResolveType(
      type_index, dex_compilation_unit_.GetDexCache(), dex_compilation_unit_.GetClassLoader());
  if (klass == nullptr ||
      !method_->GetDeclaringClass()->CanAccess(klass) ||
      klass->IsStringClass()) {
    soa.Self()->ClearException();
    unimplemented_reason_ = "UnsupportedClassForNewInstance";
    return;
  }

  InvokeRuntimeCallingConvention calling_convention;
  Register cls_reg = calling_convention.GetRegisterAt(0);
  Handle<mirror::Class> h_klass = handles_->NewHandle(klass);
  __ Ldr(cls_reg.W(), jit_patches_.DeduplicateJitClassLiteral(GetDexFile(),
                                                              type_index,
                                                              h_klass ,
                                                              code_generation_data_.get()));
  __ Ldr(cls_reg.W(), MemOperand(cls_reg.X()));
  DoReadBarrierOn(cls_reg);

  QuickEntrypointEnum entrypoint = kQuickAllocObjectInitialized;
  if (h_klass->IsFinalizable() ||
      !h_klass->IsVisiblyInitialized() ||
      h_klass.Get() == h_klass->GetClass() ||  // Classes cannot be allocated in code
      !klass->IsInstantiable()) {
    entrypoint = kQuickAllocObjectWithChecks;
  }
  InvokeRuntime(entrypoint, dex_pc);
  MoveLocation(CreateNewRegisterLocation(vreg, DataType::Type::kReference, next),
               calling_convention.GetReturnLocation(DataType::Type::kReference),
               DataType::Type::kReference);
  UpdateLocal(vreg, /* is_object= */ true, /* can_be_null= */ false);
}

void FastCompilerARM64::BuildCheckCast(uint32_t vreg, dex::TypeIndex type_index, uint32_t dex_pc) {
  EnsureHasFrame();

  InvokeRuntimeCallingConvention calling_convention;
  UseScratchRegisterScope temps(GetVIXLAssembler());
  Register cls = calling_convention.GetRegisterAt(1);
  Register obj_cls = calling_convention.GetRegisterAt(2);
  Register obj = WRegisterFrom(GetExistingRegisterLocation(vreg, DataType::Type::kReference));

  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = dex_compilation_unit_.GetClassLinker()->ResolveType(
      type_index, dex_compilation_unit_.GetDexCache(), dex_compilation_unit_.GetClassLoader());
  if (klass == nullptr || !method_->GetDeclaringClass()->CanAccess(klass)) {
    soa.Self()->ClearException();
    unimplemented_reason_ = "UnsupportedCheckCast";
    return;
  }
  Handle<mirror::Class> h_klass = handles_->NewHandle(klass);

  vixl::aarch64::Label exit, read_barrier_exit;
  __ Cbz(obj, &exit);
  __ Ldr(cls.W(), jit_patches_.DeduplicateJitClassLiteral(GetDexFile(),
                                                          type_index,
                                                          h_klass ,
                                                          code_generation_data_.get()));
  __ Ldr(cls.W(), MemOperand(cls.X()));
  __ Ldr(obj_cls.W(), MemOperand(obj.X()));
  __ Cmp(cls.W(), obj_cls.W());
  __ B(eq, &exit);

  // Read barrier on the GC Root.
  DoReadBarrierOn(cls, &read_barrier_exit);
  // Read barrier on the object's class.
  DoReadBarrierOn(obj_cls, &read_barrier_exit, /* do_mr_check= */ false);

  __ Bind(&read_barrier_exit);
  __ Cmp(cls.W(), obj_cls.W());
  __ B(eq, &exit);
  MoveLocation(LocationFrom(calling_convention.GetRegisterAt(0)),
               LocationFrom(obj),
               DataType::Type::kReference);
  InvokeRuntime(kQuickCheckInstanceOf, dex_pc);

  __ Bind(&exit);
}


void FastCompilerARM64::DoReadBarrierOn(Register reg,
                                        vixl::aarch64::Label* exit,
                                        bool do_mr_check) {
  DCHECK(has_frame_);
  vixl::aarch64::Label local_exit;
  if (do_mr_check) {
    __ Cbz(mr, (exit != nullptr) ? exit : &local_exit);
  }
  int32_t entry_point_offset =
      Thread::ReadBarrierMarkEntryPointsOffset<kArm64PointerSize>(reg.GetCode());
  __ Ldr(lr, MemOperand(tr, entry_point_offset));
  __ Blr(lr);
  if (exit == nullptr && do_mr_check) {
    __ Bind(&local_exit);
  }
}

bool FastCompilerARM64::CanGenerateCodeFor(ArtField* field, bool can_receiver_be_null) {
  if (field == nullptr) {
    // Clear potential resolution exception.
    Thread::Current()->ClearException();
    unimplemented_reason_ = "UnresolvedField";
    return false;
  }
  if (field->IsVolatile()) {
    unimplemented_reason_ = "VolatileField";
    return false;
  }

  if (can_receiver_be_null) {
    if (!CanDoImplicitNullCheckOn(field->GetOffset().Uint32Value())) {
      unimplemented_reason_ = "TooLargeFieldOffset";
      return false;
    }
  }
  return true;
}

bool FastCompilerARM64::ProcessDexInstruction(const Instruction& instruction,
                                              uint32_t dex_pc,
                                              const Instruction* next) {
  bool is_object = false;
  switch (instruction.Opcode()) {
    case Instruction::CONST_4: {
      int32_t register_index = instruction.VRegA_11n();
      int32_t constant = instruction.VRegB_11n();
      vreg_locations_[register_index] =
          Location::ConstantLocation(new (allocator_) HIntConstant(constant));
      UpdateLocal(register_index, /* is_object= */ false);
      return true;
    }

    case Instruction::CONST_16: {
      int32_t register_index = instruction.VRegA_21s();
      int32_t constant = instruction.VRegB_21s();
      vreg_locations_[register_index] =
          Location::ConstantLocation(new (allocator_) HIntConstant(constant));
      UpdateLocal(register_index, /* is_object= */ false);
      return true;
    }

    case Instruction::CONST: {
      break;
    }

    case Instruction::CONST_HIGH16: {
      break;
    }

    case Instruction::CONST_WIDE_16: {
      break;
    }

    case Instruction::CONST_WIDE_32: {
      break;
    }

    case Instruction::CONST_WIDE: {
      break;
    }

    case Instruction::CONST_WIDE_HIGH16: {
      break;
    }

    case Instruction::MOVE:
    case Instruction::MOVE_FROM16:
    case Instruction::MOVE_16: {
      break;
    }

    case Instruction::MOVE_WIDE:
    case Instruction::MOVE_WIDE_FROM16:
    case Instruction::MOVE_WIDE_16: {
      break;
    }

    case Instruction::MOVE_OBJECT:
    case Instruction::MOVE_OBJECT_16:
    case Instruction::MOVE_OBJECT_FROM16: {
      break;
    }

    case Instruction::RETURN_VOID: {
      if (method_->IsConstructor() &&
          !method_->IsStatic() &&
          dex_compilation_unit_.RequiresConstructorBarrier()) {
        __ Dmb(InnerShareable, BarrierWrites);
      }
      DropFrameAndReturn();
      return true;
    }

#define IF_XX(comparison, cond) \
    case Instruction::IF_##cond: break; \
    case Instruction::IF_##cond##Z: break

    IF_XX(HEqual, EQ);
    IF_XX(HNotEqual, NE);
    IF_XX(HLessThan, LT);
    IF_XX(HLessThanOrEqual, LE);
    IF_XX(HGreaterThan, GT);
    IF_XX(HGreaterThanOrEqual, GE);

    case Instruction::GOTO:
    case Instruction::GOTO_16:
    case Instruction::GOTO_32: {
      break;
    }

    case Instruction::RETURN:
    case Instruction::RETURN_OBJECT: {
      int32_t register_index = instruction.VRegA_11x();
      InvokeDexCallingConventionVisitorARM64 convention;
      MoveLocation(convention.GetReturnLocation(return_type_),
                   vreg_locations_[register_index],
                   return_type_);
      DropFrameAndReturn();
      return true;
    }

    case Instruction::RETURN_WIDE: {
      break;
    }

    case Instruction::INVOKE_DIRECT:
    case Instruction::INVOKE_DIRECT_RANGE:
      return HandleInvoke(instruction, dex_pc, kDirect);
    case Instruction::INVOKE_INTERFACE:
    case Instruction::INVOKE_INTERFACE_RANGE:
      return HandleInvoke(instruction, dex_pc, kInterface);
    case Instruction::INVOKE_STATIC:
    case Instruction::INVOKE_STATIC_RANGE:
      return HandleInvoke(instruction, dex_pc, kStatic);
    case Instruction::INVOKE_SUPER:
    case Instruction::INVOKE_SUPER_RANGE:
      return HandleInvoke(instruction, dex_pc, kSuper);
    case Instruction::INVOKE_VIRTUAL:
    case Instruction::INVOKE_VIRTUAL_RANGE: {
      return HandleInvoke(instruction, dex_pc, kVirtual);
    }

    case Instruction::INVOKE_POLYMORPHIC: {
      break;
    }

    case Instruction::INVOKE_POLYMORPHIC_RANGE: {
      break;
    }

    case Instruction::INVOKE_CUSTOM: {
      break;
    }

    case Instruction::INVOKE_CUSTOM_RANGE: {
      break;
    }

    case Instruction::NEG_INT: {
      break;
    }

    case Instruction::NEG_LONG: {
      break;
    }

    case Instruction::NEG_FLOAT: {
      break;
    }

    case Instruction::NEG_DOUBLE: {
      break;
    }

    case Instruction::NOT_INT: {
      break;
    }

    case Instruction::NOT_LONG: {
      break;
    }

    case Instruction::INT_TO_LONG: {
      break;
    }

    case Instruction::INT_TO_FLOAT: {
      break;
    }

    case Instruction::INT_TO_DOUBLE: {
      break;
    }

    case Instruction::LONG_TO_INT: {
      break;
    }

    case Instruction::LONG_TO_FLOAT: {
      break;
    }

    case Instruction::LONG_TO_DOUBLE: {
      break;
    }

    case Instruction::FLOAT_TO_INT: {
      break;
    }

    case Instruction::FLOAT_TO_LONG: {
      break;
    }

    case Instruction::FLOAT_TO_DOUBLE: {
      break;
    }

    case Instruction::DOUBLE_TO_INT: {
      break;
    }

    case Instruction::DOUBLE_TO_LONG: {
      break;
    }

    case Instruction::DOUBLE_TO_FLOAT: {
      break;
    }

    case Instruction::INT_TO_BYTE: {
      break;
    }

    case Instruction::INT_TO_SHORT: {
      break;
    }

    case Instruction::INT_TO_CHAR: {
      break;
    }

    case Instruction::ADD_INT: {
      break;
    }

    case Instruction::ADD_LONG: {
      break;
    }

    case Instruction::ADD_DOUBLE: {
      break;
    }

    case Instruction::ADD_FLOAT: {
      break;
    }

    case Instruction::SUB_INT: {
      break;
    }

    case Instruction::SUB_LONG: {
      break;
    }

    case Instruction::SUB_FLOAT: {
      break;
    }

    case Instruction::SUB_DOUBLE: {
      break;
    }

    case Instruction::ADD_INT_2ADDR: {
      break;
    }

    case Instruction::MUL_INT: {
      break;
    }

    case Instruction::MUL_LONG: {
      break;
    }

    case Instruction::MUL_FLOAT: {
      break;
    }

    case Instruction::MUL_DOUBLE: {
      break;
    }

    case Instruction::DIV_INT: {
      break;
    }

    case Instruction::DIV_LONG: {
      break;
    }

    case Instruction::DIV_FLOAT: {
      break;
    }

    case Instruction::DIV_DOUBLE: {
      break;
    }

    case Instruction::REM_INT: {
      break;
    }

    case Instruction::REM_LONG: {
      break;
    }

    case Instruction::REM_FLOAT: {
      break;
    }

    case Instruction::REM_DOUBLE: {
      break;
    }

    case Instruction::AND_INT: {
      break;
    }

    case Instruction::AND_LONG: {
      break;
    }

    case Instruction::SHL_INT: {
      break;
    }

    case Instruction::SHL_LONG: {
      break;
    }

    case Instruction::SHR_INT: {
      break;
    }

    case Instruction::SHR_LONG: {
      break;
    }

    case Instruction::USHR_INT: {
      break;
    }

    case Instruction::USHR_LONG: {
      break;
    }

    case Instruction::OR_INT: {
      break;
    }

    case Instruction::OR_LONG: {
      break;
    }

    case Instruction::XOR_INT: {
      break;
    }

    case Instruction::XOR_LONG: {
      break;
    }

    case Instruction::ADD_LONG_2ADDR: {
      break;
    }

    case Instruction::ADD_DOUBLE_2ADDR: {
      break;
    }

    case Instruction::ADD_FLOAT_2ADDR: {
      break;
    }

    case Instruction::SUB_INT_2ADDR: {
      break;
    }

    case Instruction::SUB_LONG_2ADDR: {
      break;
    }

    case Instruction::SUB_FLOAT_2ADDR: {
      break;
    }

    case Instruction::SUB_DOUBLE_2ADDR: {
      break;
    }

    case Instruction::MUL_INT_2ADDR: {
      break;
    }

    case Instruction::MUL_LONG_2ADDR: {
      break;
    }

    case Instruction::MUL_FLOAT_2ADDR: {
      break;
    }

    case Instruction::MUL_DOUBLE_2ADDR: {
      break;
    }

    case Instruction::DIV_INT_2ADDR: {
      break;
    }

    case Instruction::DIV_LONG_2ADDR: {
      break;
    }

    case Instruction::REM_INT_2ADDR: {
      break;
    }

    case Instruction::REM_LONG_2ADDR: {
      break;
    }

    case Instruction::REM_FLOAT_2ADDR: {
      break;
    }

    case Instruction::REM_DOUBLE_2ADDR: {
      break;
    }

    case Instruction::SHL_INT_2ADDR: {
      break;
    }

    case Instruction::SHL_LONG_2ADDR: {
      break;
    }

    case Instruction::SHR_INT_2ADDR: {
      break;
    }

    case Instruction::SHR_LONG_2ADDR: {
      break;
    }

    case Instruction::USHR_INT_2ADDR: {
      break;
    }

    case Instruction::USHR_LONG_2ADDR: {
      break;
    }

    case Instruction::DIV_FLOAT_2ADDR: {
      break;
    }

    case Instruction::DIV_DOUBLE_2ADDR: {
      break;
    }

    case Instruction::AND_INT_2ADDR: {
      break;
    }

    case Instruction::AND_LONG_2ADDR: {
      break;
    }

    case Instruction::OR_INT_2ADDR: {
      break;
    }

    case Instruction::OR_LONG_2ADDR: {
      break;
    }

    case Instruction::XOR_INT_2ADDR: {
      break;
    }

    case Instruction::XOR_LONG_2ADDR: {
      break;
    }

    case Instruction::ADD_INT_LIT16: {
      break;
    }

    case Instruction::AND_INT_LIT16: {
      break;
    }

    case Instruction::OR_INT_LIT16: {
      break;
    }

    case Instruction::XOR_INT_LIT16: {
      break;
    }

    case Instruction::RSUB_INT: {
      break;
    }

    case Instruction::MUL_INT_LIT16: {
      break;
    }

    case Instruction::ADD_INT_LIT8: {
      break;
    }

    case Instruction::AND_INT_LIT8: {
      break;
    }

    case Instruction::OR_INT_LIT8: {
      break;
    }

    case Instruction::XOR_INT_LIT8: {
      break;
    }

    case Instruction::RSUB_INT_LIT8: {
      break;
    }

    case Instruction::MUL_INT_LIT8: {
      break;
    }

    case Instruction::DIV_INT_LIT16:
    case Instruction::DIV_INT_LIT8: {
      break;
    }

    case Instruction::REM_INT_LIT16:
    case Instruction::REM_INT_LIT8: {
      break;
    }

    case Instruction::SHL_INT_LIT8: {
      break;
    }

    case Instruction::SHR_INT_LIT8: {
      break;
    }

    case Instruction::USHR_INT_LIT8: {
      break;
    }

    case Instruction::NEW_INSTANCE: {
      dex::TypeIndex type_index(instruction.VRegB_21c());
      BuildNewInstance(instruction.VRegA_21c(), type_index, dex_pc, next);
      return true;
    }

    case Instruction::NEW_ARRAY: {
      break;
    }

    case Instruction::FILLED_NEW_ARRAY: {
      break;
    }

    case Instruction::FILLED_NEW_ARRAY_RANGE: {
      break;
    }

    case Instruction::FILL_ARRAY_DATA: {
      break;
    }

    case Instruction::MOVE_RESULT_OBJECT:
      is_object = true;
      FALLTHROUGH_INTENDED;
    case Instruction::MOVE_RESULT: {
      int32_t register_index = instruction.VRegA_11x();
      InvokeDexCallingConventionVisitorARM64 convention;
      MoveLocation(CreateNewRegisterLocation(register_index, previous_invoke_type_, next),
                   convention.GetReturnLocation(previous_invoke_type_),
                   previous_invoke_type_);
      UpdateLocal(register_index, is_object);
      return true;
    }

    case Instruction::MOVE_RESULT_WIDE: {
      break;
    }

    case Instruction::CMP_LONG: {
      break;
    }

    case Instruction::CMPG_FLOAT: {
      break;
    }

    case Instruction::CMPG_DOUBLE: {
      break;
    }

    case Instruction::CMPL_FLOAT: {
      break;
    }

    case Instruction::CMPL_DOUBLE: {
      break;
    }

    case Instruction::NOP:
      return true;

    case Instruction::IGET_OBJECT:
    case Instruction::IGET:
    case Instruction::IGET_WIDE:
    case Instruction::IGET_BOOLEAN:
    case Instruction::IGET_BYTE:
    case Instruction::IGET_CHAR:
    case Instruction::IGET_SHORT: {
      uint32_t source_or_dest_reg = instruction.VRegA_22c();
      uint32_t obj_reg = instruction.VRegB_22c();
      uint16_t field_index = instruction.VRegC_22c();
      bool can_receiver_be_null = CanBeNull(obj_reg);
      ArtField* field = nullptr;
      {
        ScopedObjectAccess soa(Thread::Current());
        field = ResolveFieldWithAccessChecks(soa.Self(),
                                             dex_compilation_unit_.GetClassLinker(),
                                             field_index,
                                             method_,
                                             /* is_static= */ false,
                                             /* is_put= */ false,
                                             /* resolve_field_type= */ 0u);
        if (!CanGenerateCodeFor(field, can_receiver_be_null)) {
          return false;
        }
      }

      if (can_receiver_be_null) {
        // We need a frame in case the null check throws.
        EnsureHasFrame();
      }

      MemOperand mem = HeapOperand(
          RegisterFrom(GetExistingRegisterLocation(obj_reg, DataType::Type::kReference),
                       DataType::Type::kReference),
          field->GetOffset());
      if (instruction.Opcode() == Instruction::IGET_OBJECT) {
        // Generate a frame because of the read barrier.
        EnsureHasFrame();
        Register dst = WRegisterFrom(
            CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kReference, next));
        {
          // Ensure the pc position is recorded immediately after the load instruction.
          EmissionCheckScope guard(GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
          __ Ldr(dst, mem);
          if (can_receiver_be_null) {
            RecordPcInfo(dex_pc);
          }
        }
        UpdateLocal(source_or_dest_reg, /* is_object= */ true);
        DoReadBarrierOn(dst);
        return true;
      }
      // Ensure the pc position is recorded immediately after the load instruction.
      EmissionCheckScope guard(GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
      switch (instruction.Opcode()) {
        case Instruction::IGET_BOOLEAN: {
          Register dst = WRegisterFrom(
              CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kInt32, next));
          __ Ldrb(Register(dst), mem);
          break;
        }
        case Instruction::IGET_BYTE: {
          Register dst = WRegisterFrom(
              CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kInt32, next));
          __ Ldrsb(Register(dst), mem);
          break;
        }
        case Instruction::IGET_CHAR: {
          Register dst = WRegisterFrom(
              CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kInt32, next));
          __ Ldrh(Register(dst), mem);
          break;
        }
        case Instruction::IGET_SHORT: {
          Register dst = WRegisterFrom(
              CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kInt32, next));
          __ Ldrsh(Register(dst), mem);
          break;
        }
        case Instruction::IGET: {
          const dex::FieldId& field_id = GetDexFile().GetFieldId(field_index);
          const char* type = GetDexFile().GetFieldTypeDescriptor(field_id);
          DataType::Type field_type = DataType::FromShorty(type[0]);
          if (DataType::IsFloatingPointType(field_type)) {
            VRegister dst = SRegisterFrom(
                CreateNewRegisterLocation(source_or_dest_reg, field_type, next));
            __ Ldr(dst, mem);
          } else {
            Register dst = WRegisterFrom(
                CreateNewRegisterLocation(source_or_dest_reg, DataType::Type::kInt32, next));
            __ Ldr(dst, mem);
          }
          break;
        }
        default:
          unimplemented_reason_ = "UnimplementedIGet";
          return false;
      }
      UpdateLocal(source_or_dest_reg, /* is_object= */ false);
      if (can_receiver_be_null) {
        RecordPcInfo(dex_pc);
      }
      return true;
    }

    case Instruction::IPUT_OBJECT:
      is_object = true;
      FALLTHROUGH_INTENDED;
    case Instruction::IPUT:
    case Instruction::IPUT_WIDE:
    case Instruction::IPUT_BOOLEAN:
    case Instruction::IPUT_BYTE:
    case Instruction::IPUT_CHAR:
    case Instruction::IPUT_SHORT: {
      uint32_t source_reg = instruction.VRegA_22c();
      uint32_t obj_reg = instruction.VRegB_22c();
      uint16_t field_index = instruction.VRegC_22c();
      bool can_receiver_be_null = CanBeNull(obj_reg);
      ArtField* field = nullptr;
      {
        ScopedObjectAccess soa(Thread::Current());
        field = ResolveFieldWithAccessChecks(soa.Self(),
                                             dex_compilation_unit_.GetClassLinker(),
                                             field_index,
                                             method_,
                                             /* is_static= */ false,
                                             /* is_put= */ true,
                                             /* resolve_field_type= */ is_object);
        if (!CanGenerateCodeFor(field, can_receiver_be_null)) {
          return false;
        }
      }

      if (can_receiver_be_null) {
        // We need a frame in case the null check throws.
        EnsureHasFrame();
      }

      Register holder = RegisterFrom(
          GetExistingRegisterLocation(obj_reg, DataType::Type::kReference),
          DataType::Type::kReference);
      MemOperand mem = HeapOperand(holder, field->GetOffset());

      // Need one temp if the stored value is a constant.
      UseScratchRegisterScope temps(GetVIXLAssembler());
      Location src = vreg_locations_[source_reg];
      bool assigning_constant = false;
      if (src.IsConstant()) {
        assigning_constant = true;
        if (src.GetConstant()->IsIntConstant() &&
            src.GetConstant()->AsIntConstant()->GetValue() == 0) {
          src = Location::RegisterLocation(XZR);
        } else {
          src = Location::RegisterLocation(temps.AcquireW().GetCode());
          MoveLocation(src, vreg_locations_[source_reg], DataType::Type::kInt32);
        }
      } else if (src.IsStackSlot() || src.IsDoubleStackSlot()) {
        unimplemented_reason_ = "IPUTOnStackSlot";
        return false;
      }
      if (instruction.Opcode() == Instruction::IPUT_OBJECT) {
        Register reg = WRegisterFrom(src);
        {
          // Ensure the pc position is recorded immediately after the store instruction.
          EmissionCheckScope guard(GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
          __ Str(reg, mem);
          if (can_receiver_be_null) {
            RecordPcInfo(dex_pc);
          }
        }
        // If we assign a constant (only null for iput-object), no need for the write
        // barrier.
        if (!assigning_constant) {
          vixl::aarch64::Label exit;
          __ Cbz(reg, &exit);
          Register card = temps.AcquireX();
          Register temp = temps.AcquireW();
          __ Ldr(card, MemOperand(tr, Thread::CardTableOffset<kArm64PointerSize>().Int32Value()));
          __ Lsr(temp, holder, gc::accounting::CardTable::kCardShift);
          __ Strb(card, MemOperand(card, temp.X()));
          __ Bind(&exit);
        }
        return true;
      }
      // Ensure the pc position is recorded immediately after the store instruction.
      EmissionCheckScope guard(GetVIXLAssembler(), kMaxMacroInstructionSizeInBytes);
      switch (instruction.Opcode()) {
        case Instruction::IPUT_BOOLEAN:
        case Instruction::IPUT_BYTE: {
          __ Strb(WRegisterFrom(src), mem);
          break;
        }
        case Instruction::IPUT_CHAR:
        case Instruction::IPUT_SHORT: {
          __ Strh(WRegisterFrom(src), mem);
          break;
        }
        case Instruction::IPUT: {
          if (src.IsFpuRegister()) {
            __ Str(SRegisterFrom(src), mem);
          } else {
            __ Str(WRegisterFrom(src), mem);
          }
          break;
        }
        default:
          unimplemented_reason_ = "UnimplementedIPut";
          return false;
      }
      if (can_receiver_be_null) {
        RecordPcInfo(dex_pc);
      }
      return true;
    }

    case Instruction::SGET:
    case Instruction::SGET_WIDE:
    case Instruction::SGET_OBJECT:
    case Instruction::SGET_BOOLEAN:
    case Instruction::SGET_BYTE:
    case Instruction::SGET_CHAR:
    case Instruction::SGET_SHORT: {
      break;
    }

    case Instruction::SPUT:
    case Instruction::SPUT_WIDE:
    case Instruction::SPUT_OBJECT:
    case Instruction::SPUT_BOOLEAN:
    case Instruction::SPUT_BYTE:
    case Instruction::SPUT_CHAR:
    case Instruction::SPUT_SHORT: {
      break;
    }

#define ARRAY_XX(kind, anticipated_type)                                          \
    case Instruction::AGET##kind: {                                               \
      break;                                                                      \
    }                                                                             \
    case Instruction::APUT##kind: {                                               \
      break;                                                                      \
    }

    ARRAY_XX(, DataType::Type::kInt32);
    ARRAY_XX(_WIDE, DataType::Type::kInt64);
    ARRAY_XX(_OBJECT, DataType::Type::kReference);
    ARRAY_XX(_BOOLEAN, DataType::Type::kBool);
    ARRAY_XX(_BYTE, DataType::Type::kInt8);
    ARRAY_XX(_CHAR, DataType::Type::kUint16);
    ARRAY_XX(_SHORT, DataType::Type::kInt16);

    case Instruction::ARRAY_LENGTH: {
      break;
    }

    case Instruction::CONST_STRING: {
      dex::StringIndex string_index(instruction.VRegB_21c());
      BuildLoadString(instruction.VRegA_21c(), string_index, next);
      return true;
    }

    case Instruction::CONST_STRING_JUMBO: {
      dex::StringIndex string_index(instruction.VRegB_31c());
      BuildLoadString(instruction.VRegA_31c(), string_index, next);
      return true;
    }

    case Instruction::CONST_CLASS: {
      break;
    }

    case Instruction::CONST_METHOD_HANDLE: {
      break;
    }

    case Instruction::CONST_METHOD_TYPE: {
      break;
    }

    case Instruction::MOVE_EXCEPTION: {
      break;
    }

    case Instruction::THROW: {
      EnsureHasFrame();
      int32_t reg = instruction.VRegA_11x();
      InvokeRuntimeCallingConvention calling_convention;
      MoveLocation(LocationFrom(calling_convention.GetRegisterAt(0)),
                   vreg_locations_[reg],
                   DataType::Type::kReference);
      InvokeRuntime(kQuickDeliverException, dex_pc);
      return true;
    }

    case Instruction::INSTANCE_OF: {
      break;
    }

    case Instruction::CHECK_CAST: {
      uint8_t reference = instruction.VRegA_21c();
      dex::TypeIndex type_index(instruction.VRegB_21c());
      BuildCheckCast(reference, type_index, dex_pc);
      return true;
    }

    case Instruction::MONITOR_ENTER: {
      break;
    }

    case Instruction::MONITOR_EXIT: {
      break;
    }

    case Instruction::SPARSE_SWITCH:
    case Instruction::PACKED_SWITCH: {
      break;
    }

    case Instruction::UNUSED_3E ... Instruction::UNUSED_43:
    case Instruction::UNUSED_73:
    case Instruction::UNUSED_79:
    case Instruction::UNUSED_7A:
    case Instruction::UNUSED_E3 ... Instruction::UNUSED_F9: {
      break;
    }
  }
  unimplemented_reason_ = instruction.Name();
  return false;
}  // NOLINT(readability/fn_size)

bool FastCompilerARM64::Compile() {
  if (!InitializeParameters()) {
    DCHECK(HitUnimplemented());
    return false;
  }
  if (!ProcessInstructions()) {
    DCHECK(HitUnimplemented());
    return false;
  }
  if (HitUnimplemented()) {
    return false;
  }
  if (!has_frame_) {
    code_generation_data_->GetStackMapStream()->BeginMethod(/* frame_size= */ 0u,
                                                            /* core_spill_mask= */ 0u,
                                                            /* fp_spill_mask= */ 0u,
                                                            GetCodeItemAccessor().RegistersSize(),
                                                            /* is_compiling_baseline= */ true,
                                                            /* is_debuggable= */ false);
  }
  code_generation_data_->GetStackMapStream()->EndMethod(assembler_.CodeSize());
  GetVIXLAssembler()->FinalizeCode();
  return true;
}

}  // namespace arm64

std::unique_ptr<FastCompiler> FastCompiler::CompileARM64(
    ArtMethod* method,
    ArenaAllocator* allocator,
    ArenaStack* arena_stack,
    VariableSizedHandleScope* handles,
    const CompilerOptions& compiler_options,
    const DexCompilationUnit& dex_compilation_unit) {
  if (!compiler_options.GetImplicitNullChecks() ||
      !compiler_options.GetImplicitStackOverflowChecks() ||
      kUseTableLookupReadBarrier ||
      !kReserveMarkingRegister ||
      kPoisonHeapReferences) {
    // Configurations we don't support.
    return nullptr;
  }
  std::unique_ptr<arm64::FastCompilerARM64> compiler(new arm64::FastCompilerARM64(
      method,
      allocator,
      arena_stack,
      handles,
      compiler_options,
      dex_compilation_unit));
  if (compiler->Compile()) {
    return compiler;
  }
  VLOG(jit) << "Did not fast compile because of " << compiler->GetUnimplementedReason();
  return nullptr;
}

}  // namespace art
