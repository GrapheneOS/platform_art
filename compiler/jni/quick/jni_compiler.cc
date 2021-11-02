/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "jni_compiler.h"

#include <algorithm>
#include <fstream>
#include <ios>
#include <memory>
#include <vector>

#include "art_method.h"
#include "base/arena_allocator.h"
#include "base/arena_containers.h"
#include "base/enums.h"
#include "base/logging.h"  // For VLOG.
#include "base/macros.h"
#include "base/malloc_arena_pool.h"
#include "base/memory_region.h"
#include "base/utils.h"
#include "calling_convention.h"
#include "class_linker.h"
#include "dwarf/debug_frame_opcode_writer.h"
#include "dex/dex_file-inl.h"
#include "driver/compiler_options.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "jni/jni_env_ext.h"
#include "thread.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/arm64/managed_register_arm64.h"
#include "utils/assembler.h"
#include "utils/jni_macro_assembler.h"
#include "utils/managed_register.h"
#include "utils/x86/managed_register_x86.h"

#define __ jni_asm->

namespace art {

constexpr size_t kIRTCookieSize = JniCallingConvention::SavedLocalReferenceCookieSize();

template <PointerSize kPointerSize>
static void PushLocalReferenceFrame(JNIMacroAssembler<kPointerSize>* jni_asm,
                                    ManagedRegister jni_env_reg,
                                    ManagedRegister saved_cookie_reg,
                                    ManagedRegister temp_reg);
template <PointerSize kPointerSize>
static void PopLocalReferenceFrame(JNIMacroAssembler<kPointerSize>* jni_asm,
                                   ManagedRegister jni_env_reg,
                                   ManagedRegister saved_cookie_reg,
                                   ManagedRegister temp_reg);

template <PointerSize kPointerSize>
static void CopyParameter(JNIMacroAssembler<kPointerSize>* jni_asm,
                          ManagedRuntimeCallingConvention* mr_conv,
                          JniCallingConvention* jni_conv);
template <PointerSize kPointerSize>
static void SetNativeParameter(JNIMacroAssembler<kPointerSize>* jni_asm,
                               JniCallingConvention* jni_conv,
                               ManagedRegister in_reg);

template <PointerSize kPointerSize>
static std::unique_ptr<JNIMacroAssembler<kPointerSize>> GetMacroAssembler(
    ArenaAllocator* allocator, InstructionSet isa, const InstructionSetFeatures* features) {
  return JNIMacroAssembler<kPointerSize>::Create(allocator, isa, features);
}

enum class JniEntrypoint {
  kStart,
  kEnd
};

template <PointerSize kPointerSize>
static ThreadOffset<kPointerSize> GetJniEntrypointThreadOffset(JniEntrypoint which,
                                                               bool reference_return,
                                                               bool is_synchronized) {
  if (which == JniEntrypoint::kStart) {  // JniMethodStart
    ThreadOffset<kPointerSize> jni_start =
        is_synchronized
            ? QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodStartSynchronized)
            : QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodStart);

    return jni_start;
  } else {  // JniMethodEnd
    ThreadOffset<kPointerSize> jni_end(-1);
    if (reference_return) {
      // Pass result.
      jni_end = is_synchronized
                    ? QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodEndWithReferenceSynchronized)
                    : QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodEndWithReference);
    } else {
      jni_end = is_synchronized
                    ? QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodEndSynchronized)
                    : QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniMethodEnd);
    }

    return jni_end;
  }
}


// Generate the JNI bridge for the given method, general contract:
// - Arguments are in the managed runtime format, either on stack or in
//   registers, a reference to the method object is supplied as part of this
//   convention.
//
template <PointerSize kPointerSize>
static JniCompiledMethod ArtJniCompileMethodInternal(const CompilerOptions& compiler_options,
                                                     uint32_t access_flags,
                                                     uint32_t method_idx,
                                                     const DexFile& dex_file) {
  constexpr size_t kRawPointerSize = static_cast<size_t>(kPointerSize);
  const bool is_native = (access_flags & kAccNative) != 0;
  CHECK(is_native);
  const bool is_static = (access_flags & kAccStatic) != 0;
  const bool is_synchronized = (access_flags & kAccSynchronized) != 0;
  const char* shorty = dex_file.GetMethodShorty(dex_file.GetMethodId(method_idx));
  InstructionSet instruction_set = compiler_options.GetInstructionSet();
  const InstructionSetFeatures* instruction_set_features =
      compiler_options.GetInstructionSetFeatures();

  // i.e. if the method was annotated with @FastNative
  const bool is_fast_native = (access_flags & kAccFastNative) != 0u;

  // i.e. if the method was annotated with @CriticalNative
  const bool is_critical_native = (access_flags & kAccCriticalNative) != 0u;

  VLOG(jni) << "JniCompile: Method :: "
              << dex_file.PrettyMethod(method_idx, /* with signature */ true)
              << " :: access_flags = " << std::hex << access_flags << std::dec;

  if (UNLIKELY(is_fast_native)) {
    VLOG(jni) << "JniCompile: Fast native method detected :: "
              << dex_file.PrettyMethod(method_idx, /* with signature */ true);
  }

  if (UNLIKELY(is_critical_native)) {
    VLOG(jni) << "JniCompile: Critical native method detected :: "
              << dex_file.PrettyMethod(method_idx, /* with signature */ true);
  }

  if (kIsDebugBuild) {
    // Don't allow both @FastNative and @CriticalNative. They are mutually exclusive.
    if (UNLIKELY(is_fast_native && is_critical_native)) {
      LOG(FATAL) << "JniCompile: Method cannot be both @CriticalNative and @FastNative"
                 << dex_file.PrettyMethod(method_idx, /* with_signature= */ true);
    }

    // @CriticalNative - extra checks:
    // -- Don't allow virtual criticals
    // -- Don't allow synchronized criticals
    // -- Don't allow any objects as parameter or return value
    if (UNLIKELY(is_critical_native)) {
      CHECK(is_static)
          << "@CriticalNative functions cannot be virtual since that would"
          << "require passing a reference parameter (this), which is illegal "
          << dex_file.PrettyMethod(method_idx, /* with_signature= */ true);
      CHECK(!is_synchronized)
          << "@CriticalNative functions cannot be synchronized since that would"
          << "require passing a (class and/or this) reference parameter, which is illegal "
          << dex_file.PrettyMethod(method_idx, /* with_signature= */ true);
      for (size_t i = 0; i < strlen(shorty); ++i) {
        CHECK_NE(Primitive::kPrimNot, Primitive::GetType(shorty[i]))
            << "@CriticalNative methods' shorty types must not have illegal references "
            << dex_file.PrettyMethod(method_idx, /* with_signature= */ true);
      }
    }
  }

  MallocArenaPool pool;
  ArenaAllocator allocator(&pool);

  // Calling conventions used to iterate over parameters to method
  std::unique_ptr<JniCallingConvention> main_jni_conv =
      JniCallingConvention::Create(&allocator,
                                   is_static,
                                   is_synchronized,
                                   is_fast_native,
                                   is_critical_native,
                                   shorty,
                                   instruction_set);
  bool reference_return = main_jni_conv->IsReturnAReference();

  std::unique_ptr<ManagedRuntimeCallingConvention> mr_conv(
      ManagedRuntimeCallingConvention::Create(
          &allocator, is_static, is_synchronized, shorty, instruction_set));

  // Calling conventions to call into JNI method "end" possibly passing a returned reference, the
  //     method and the current thread.
  const char* jni_end_shorty;
  if (reference_return && is_synchronized) {
    jni_end_shorty = "IL";
  } else if (reference_return) {
    jni_end_shorty = "I";
  } else {
    jni_end_shorty = "V";
  }

  std::unique_ptr<JniCallingConvention> end_jni_conv(
      JniCallingConvention::Create(&allocator,
                                   is_static,
                                   is_synchronized,
                                   is_fast_native,
                                   is_critical_native,
                                   jni_end_shorty,
                                   instruction_set));

  // Assembler that holds generated instructions
  std::unique_ptr<JNIMacroAssembler<kPointerSize>> jni_asm =
      GetMacroAssembler<kPointerSize>(&allocator, instruction_set, instruction_set_features);
  jni_asm->cfi().SetEnabled(compiler_options.GenerateAnyDebugInfo());
  jni_asm->SetEmitRunTimeChecksInDebugMode(compiler_options.EmitRunTimeChecksInDebugMode());

  // 1. Build the frame saving all callee saves, Method*, and PC return address.
  //    For @CriticalNative, this includes space for out args, otherwise just the managed frame.
  const size_t managed_frame_size = main_jni_conv->FrameSize();
  const size_t main_out_arg_size = main_jni_conv->OutFrameSize();
  size_t current_frame_size = is_critical_native ? main_out_arg_size : managed_frame_size;
  ManagedRegister method_register =
      is_critical_native ? ManagedRegister::NoRegister() : mr_conv->MethodRegister();
  ArrayRef<const ManagedRegister> callee_save_regs = main_jni_conv->CalleeSaveRegisters();
  __ BuildFrame(current_frame_size, method_register, callee_save_regs);
  DCHECK_EQ(jni_asm->cfi().GetCurrentCFAOffset(), static_cast<int>(current_frame_size));

  if (LIKELY(!is_critical_native)) {
    // Spill all register arguments.
    // TODO: Pass these in a single call to let the assembler use multi-register stores.
    // TODO: Spill native stack args straight to their stack locations (adjust SP earlier).
    // TODO: For @FastNative, move args in registers, spill only references.
    mr_conv->ResetIterator(FrameOffset(current_frame_size));
    for (; mr_conv->HasNext(); mr_conv->Next()) {
      if (mr_conv->IsCurrentParamInRegister()) {
        size_t size = mr_conv->IsCurrentParamALongOrDouble() ? 8u : 4u;
        __ Store(mr_conv->CurrentParamStackOffset(), mr_conv->CurrentParamRegister(), size);
      }
    }

    // 2. Write out the end of the quick frames.
    __ StoreStackPointerToThread(Thread::TopOfManagedStackOffset<kPointerSize>());

    // NOTE: @CriticalNative does not need to store the stack pointer to the thread
    //       because garbage collections are disabled within the execution of a
    //       @CriticalNative method.
  }  // if (!is_critical_native)

  // 3. Move frame down to allow space for out going args.
  size_t current_out_arg_size = main_out_arg_size;
  if (UNLIKELY(is_critical_native)) {
    DCHECK_EQ(main_out_arg_size, current_frame_size);
  } else {
    __ IncreaseFrameSize(main_out_arg_size);
    current_frame_size += main_out_arg_size;
  }

  // 4. Check if we need to go to the slow path to emit the read barrier for the declaring class
  //    in the method for a static call.
  //    Skip this for @CriticalNative because we're not passing a `jclass` to the native method.
  std::unique_ptr<JNIMacroLabel> jclass_read_barrier_slow_path;
  std::unique_ptr<JNIMacroLabel> jclass_read_barrier_return;
  if (kUseReadBarrier && is_static && !is_critical_native) {
    jclass_read_barrier_slow_path = __ CreateLabel();
    jclass_read_barrier_return = __ CreateLabel();

    // Check if gc_is_marking is set -- if it's not, we don't need a read barrier.
    __ TestGcMarking(jclass_read_barrier_slow_path.get(), JNIMacroUnaryCondition::kNotZero);

    // If marking, the slow path returns after the check.
    __ Bind(jclass_read_barrier_return.get());
  }

  // 5. Call into appropriate JniMethodStart passing Thread* so that transition out of Runnable
  //    can occur. We abuse the JNI calling convention here, that is guaranteed to support passing
  //    two pointer arguments.
  std::unique_ptr<JNIMacroLabel> monitor_enter_exception_slow_path =
      UNLIKELY(is_synchronized) ? __ CreateLabel() : nullptr;
  if (LIKELY(!is_critical_native && !is_fast_native)) {
    // Skip this for @CriticalNative and @FastNative methods. They do not call JniMethodStart.
    ThreadOffset<kPointerSize> jni_start =
        GetJniEntrypointThreadOffset<kPointerSize>(JniEntrypoint::kStart,
                                                   reference_return,
                                                   is_synchronized);
    main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));
    if (is_synchronized) {
      // Pass object for locking.
      if (is_static) {
        // Pass the pointer to the method's declaring class as the first argument.
        DCHECK_EQ(ArtMethod::DeclaringClassOffset().SizeValue(), 0u);
        SetNativeParameter(jni_asm.get(), main_jni_conv.get(), method_register);
      } else {
        // TODO: Use the register that still holds the `this` reference.
        mr_conv->ResetIterator(FrameOffset(current_frame_size));
        FrameOffset this_offset = mr_conv->CurrentParamStackOffset();
        if (main_jni_conv->IsCurrentParamOnStack()) {
          FrameOffset out_off = main_jni_conv->CurrentParamStackOffset();
          __ CreateJObject(out_off, this_offset, /*null_allowed=*/ false);
        } else {
          ManagedRegister out_reg = main_jni_conv->CurrentParamRegister();
          __ CreateJObject(out_reg,
                           this_offset,
                           ManagedRegister::NoRegister(),
                           /*null_allowed=*/ false);
        }
      }
      main_jni_conv->Next();
    }
    if (main_jni_conv->IsCurrentParamInRegister()) {
      __ GetCurrentThread(main_jni_conv->CurrentParamRegister());
      __ Call(main_jni_conv->CurrentParamRegister(), Offset(jni_start));
    } else {
      __ GetCurrentThread(main_jni_conv->CurrentParamStackOffset());
      __ CallFromThread(jni_start);
    }
    method_register = ManagedRegister::NoRegister();  // Method register is clobbered.
    if (is_synchronized) {  // Check for exceptions from monitor enter.
      __ ExceptionPoll(monitor_enter_exception_slow_path.get());
    }
  }

  // 6. Push local reference frame.
  // Skip this for @CriticalNative methods, they cannot use any references.
  ManagedRegister jni_env_reg = ManagedRegister::NoRegister();
  ManagedRegister saved_cookie_reg = ManagedRegister::NoRegister();
  ManagedRegister callee_save_temp = ManagedRegister::NoRegister();
  if (LIKELY(!is_critical_native)) {
    // To pop the local reference frame later, we shall need the JNI environment pointer
    // as well as the cookie, so we preserve them across calls in callee-save registers.
    // Managed callee-saves were already saved, so these registers are now available.
    ArrayRef<const ManagedRegister> callee_save_scratch_regs =
        main_jni_conv->CalleeSaveScratchRegisters();
    CHECK_GE(callee_save_scratch_regs.size(), 3u);  // At least 3 for each supported architecture.
    jni_env_reg = callee_save_scratch_regs[0];
    saved_cookie_reg = __ CoreRegisterWithSize(callee_save_scratch_regs[1], kIRTCookieSize);
    callee_save_temp = __ CoreRegisterWithSize(callee_save_scratch_regs[2], kIRTCookieSize);

    // Load the JNI environment pointer.
    __ LoadRawPtrFromThread(jni_env_reg, Thread::JniEnvOffset<kPointerSize>());

    // Push the local reference frame.
    PushLocalReferenceFrame<kPointerSize>(
        jni_asm.get(), jni_env_reg, saved_cookie_reg, callee_save_temp);
  }

  // 7. Fill arguments.
  if (UNLIKELY(is_critical_native)) {
    ArenaVector<ArgumentLocation> src_args(allocator.Adapter());
    ArenaVector<ArgumentLocation> dest_args(allocator.Adapter());
    // Move the method pointer to the hidden argument register.
    dest_args.push_back(ArgumentLocation(main_jni_conv->HiddenArgumentRegister(), kRawPointerSize));
    src_args.push_back(ArgumentLocation(mr_conv->MethodRegister(), kRawPointerSize));
    // Move normal arguments to their locations.
    mr_conv->ResetIterator(FrameOffset(current_frame_size));
    main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));
    for (; mr_conv->HasNext(); mr_conv->Next(), main_jni_conv->Next()) {
      DCHECK(main_jni_conv->HasNext());
      size_t size = mr_conv->IsCurrentParamALongOrDouble() ? 8u : 4u;
      src_args.push_back(mr_conv->IsCurrentParamInRegister()
          ? ArgumentLocation(mr_conv->CurrentParamRegister(), size)
          : ArgumentLocation(mr_conv->CurrentParamStackOffset(), size));
      dest_args.push_back(main_jni_conv->IsCurrentParamInRegister()
          ? ArgumentLocation(main_jni_conv->CurrentParamRegister(), size)
          : ArgumentLocation(main_jni_conv->CurrentParamStackOffset(), size));
    }
    DCHECK(!main_jni_conv->HasNext());
    __ MoveArguments(ArrayRef<ArgumentLocation>(dest_args), ArrayRef<ArgumentLocation>(src_args));
  } else {
    if (UNLIKELY(!method_register.IsNoRegister())) {
      DCHECK(is_fast_native);
      // In general, we do not know if the method register shall be clobbered by initializing
      // some argument below. However, for most supported architectures (arm, arm64, x86_64),
      // the `method_register` is the same as the `JNIEnv*` argument register which is
      // initialized last, so we can quickly check that case and use the original method
      // register to initialize the `jclass` for static methods. Otherwise, move the method
      // to the `callee_save_temp` as we shall need it for the call.
      main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));
      if (main_jni_conv->IsCurrentParamInRegister() &&
          main_jni_conv->CurrentParamRegister().Equals(method_register) &&
          is_static) {
        // Keep the current `method_register`.
      } else {
        ManagedRegister new_method_reg = __ CoreRegisterWithSize(callee_save_temp, kRawPointerSize);
        __ Move(new_method_reg, method_register, kRawPointerSize);
        method_register = new_method_reg;
      }
    }

    // Iterate over arguments placing values from managed calling convention in
    // to the convention required for a native call (shuffling). For references
    // place an index/pointer to the reference after checking whether it is
    // null (which must be encoded as null).
    // Note: we do this prior to materializing the JNIEnv* and static's jclass to
    // give as many free registers for the shuffle as possible.
    mr_conv->ResetIterator(FrameOffset(current_frame_size));
    uint32_t args_count = 0;
    while (mr_conv->HasNext()) {
      args_count++;
      mr_conv->Next();
    }

    // Do a backward pass over arguments, so that the generated code will be "mov
    // R2, R3; mov R1, R2" instead of "mov R1, R2; mov R2, R3."
    // TODO: A reverse iterator to improve readability.
    // TODO: This is currently useless as all archs spill args when building the frame.
    //       To avoid the full spilling, we would have to do one pass before the BuildFrame()
    //       to determine which arg registers are clobbered before they are needed.
    for (uint32_t i = 0; i < args_count; ++i) {
      mr_conv->ResetIterator(FrameOffset(current_frame_size));
      main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));

      // Skip the extra JNI parameters for now.
      main_jni_conv->Next();    // Skip JNIEnv*.
      if (is_static) {
        main_jni_conv->Next();  // Skip Class for now.
      }
      // Skip to the argument we're interested in.
      for (uint32_t j = 0; j < args_count - i - 1; ++j) {
        mr_conv->Next();
        main_jni_conv->Next();
      }
      CopyParameter(jni_asm.get(), mr_conv.get(), main_jni_conv.get());
    }

    // 8. For static method, create jclass argument as a pointer to the method's declaring class.
    //    Make sure the method is in a register even for non-static methods.
    DCHECK_EQ(ArtMethod::DeclaringClassOffset().SizeValue(), 0u);
    FrameOffset method_offset =
        FrameOffset(current_out_arg_size + mr_conv->MethodStackOffset().SizeValue());
    if (is_static) {
      main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));
      main_jni_conv->Next();  // Skip JNIEnv*
      // Load reference to the method's declaring class. For normal native, the method register
      // has been clobbered by the above call, so we need to load the method from the stack.
      if (method_register.IsNoRegister()) {
        // Use the `callee_save_temp` if the parameter goes on the stack.
        method_register = main_jni_conv->IsCurrentParamOnStack()
            ? __ CoreRegisterWithSize(callee_save_temp, kRawPointerSize)
            : main_jni_conv->CurrentParamRegister();
        __ Load(method_register, method_offset, kRawPointerSize);
      }
      DCHECK(!method_register.IsNoRegister());
      if (main_jni_conv->IsCurrentParamOnStack()) {
        // Store the method argument.
        FrameOffset out_off = main_jni_conv->CurrentParamStackOffset();
        __ Store(out_off, method_register, kRawPointerSize);
      } else {
        ManagedRegister out_reg = main_jni_conv->CurrentParamRegister();
        __ Move(out_reg, method_register, kRawPointerSize);  // No-op if equal.
        method_register = out_reg;
      }
    } else if (LIKELY(method_register.IsNoRegister())) {
      // Load the method for non-static methods to `callee_save_temp` as we need it for the call.
      method_register = __ CoreRegisterWithSize(callee_save_temp, kRawPointerSize);
      __ Load(method_register, method_offset, kRawPointerSize);
    }

    // Set the iterator back to the incoming Method*.
    main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));

    // 9. Create 1st argument, the JNI environment ptr.
    if (main_jni_conv->IsCurrentParamInRegister()) {
      ManagedRegister jni_env_arg = main_jni_conv->CurrentParamRegister();
      __ Move(jni_env_arg, jni_env_reg, kRawPointerSize);
    } else {
      FrameOffset jni_env_arg_offset = main_jni_conv->CurrentParamStackOffset();
      __ Store(jni_env_arg_offset, jni_env_reg, kRawPointerSize);
    }
  }

  // 10. Plant call to native code associated with method.
  MemberOffset jni_entrypoint_offset =
      ArtMethod::EntryPointFromJniOffset(InstructionSetPointerSize(instruction_set));
  if (UNLIKELY(is_critical_native)) {
    if (main_jni_conv->UseTailCall()) {
      __ Jump(main_jni_conv->HiddenArgumentRegister(), jni_entrypoint_offset);
    } else {
      __ Call(main_jni_conv->HiddenArgumentRegister(), jni_entrypoint_offset);
    }
  } else {
    DCHECK(method_register.IsRegister());
    __ Call(method_register, jni_entrypoint_offset);
    // We shall not need the method register anymore. And we may clobber it below
    // if it's the `callee_save_temp`, so clear it here to make sure it's not used.
    method_register = ManagedRegister::NoRegister();
  }

  // 11. Fix differences in result widths.
  if (main_jni_conv->RequiresSmallResultTypeExtension()) {
    DCHECK(main_jni_conv->HasSmallReturnType());
    CHECK(!is_critical_native || !main_jni_conv->UseTailCall());
    if (main_jni_conv->GetReturnType() == Primitive::kPrimByte ||
        main_jni_conv->GetReturnType() == Primitive::kPrimShort) {
      __ SignExtend(main_jni_conv->ReturnRegister(),
                    Primitive::ComponentSize(main_jni_conv->GetReturnType()));
    } else {
      CHECK(main_jni_conv->GetReturnType() == Primitive::kPrimBoolean ||
            main_jni_conv->GetReturnType() == Primitive::kPrimChar);
      __ ZeroExtend(main_jni_conv->ReturnRegister(),
                    Primitive::ComponentSize(main_jni_conv->GetReturnType()));
    }
  }

  // 12. Process return value
  bool spill_return_value = main_jni_conv->SpillsReturnValue();
  FrameOffset return_save_location =
      spill_return_value ? main_jni_conv->ReturnValueSaveLocation() : FrameOffset(0);
  if (spill_return_value) {
    DCHECK(!is_critical_native);
    // For normal JNI, store the return value on the stack because the call to
    // JniMethodEnd will clobber the return value. It will be restored in (13).
    CHECK_LT(return_save_location.Uint32Value(), current_frame_size);
    __ Store(return_save_location,
             main_jni_conv->ReturnRegister(),
             main_jni_conv->SizeOfReturnValue());
  } else if (UNLIKELY(is_fast_native || is_critical_native) &&
             main_jni_conv->SizeOfReturnValue() != 0) {
    // For @FastNative and @CriticalNative only,
    // move the JNI return register into the managed return register (if they don't match).
    ManagedRegister jni_return_reg = main_jni_conv->ReturnRegister();
    ManagedRegister mr_return_reg = mr_conv->ReturnRegister();

    // Check if the JNI return register matches the managed return register.
    // If they differ, only then do we have to do anything about it.
    // Otherwise the return value is already in the right place when we return.
    if (!jni_return_reg.Equals(mr_return_reg)) {
      CHECK(!is_critical_native || !main_jni_conv->UseTailCall());
      // This is typically only necessary on ARM32 due to native being softfloat
      // while managed is hardfloat.
      // -- For example VMOV {r0, r1} -> D0; VMOV r0 -> S0.
      __ Move(mr_return_reg, jni_return_reg, main_jni_conv->SizeOfReturnValue());
    } else if (jni_return_reg.IsNoRegister() && mr_return_reg.IsNoRegister()) {
      // Check that if the return value is passed on the stack for some reason,
      // that the size matches.
      CHECK_EQ(main_jni_conv->SizeOfReturnValue(), mr_conv->SizeOfReturnValue());
    }
  }

  // 13. For @FastNative that returns a reference, do an early exception check so that the
  //     `JniDecodeReferenceResult()` in the main path does not need to check for exceptions.
  std::unique_ptr<JNIMacroLabel> exception_slow_path =
      LIKELY(!is_critical_native) ? __ CreateLabel() : nullptr;
  if (UNLIKELY(is_fast_native) && reference_return) {
    __ ExceptionPoll(exception_slow_path.get());
  }

  // 14. For @FastNative that returns a reference, do an early suspend check so that we
  //     do not need to encode the decoded reference in a stack map.
  std::unique_ptr<JNIMacroLabel> suspend_check_slow_path =
      UNLIKELY(is_fast_native) ? __ CreateLabel() : nullptr;
  std::unique_ptr<JNIMacroLabel> suspend_check_resume =
      UNLIKELY(is_fast_native) ? __ CreateLabel() : nullptr;
  if (UNLIKELY(is_fast_native) && reference_return) {
    __ SuspendCheck(suspend_check_slow_path.get());
    __ Bind(suspend_check_resume.get());
  }

  if (LIKELY(!is_critical_native)) {
    // Increase frame size for out args if needed by the end_jni_conv.
    const size_t end_out_arg_size = end_jni_conv->OutFrameSize();
    if (end_out_arg_size > current_out_arg_size) {
      DCHECK(!is_fast_native);
      size_t out_arg_size_diff = end_out_arg_size - current_out_arg_size;
      current_out_arg_size = end_out_arg_size;
      __ IncreaseFrameSize(out_arg_size_diff);
      current_frame_size += out_arg_size_diff;
      return_save_location = FrameOffset(return_save_location.SizeValue() + out_arg_size_diff);
    }
    end_jni_conv->ResetIterator(FrameOffset(end_out_arg_size));

    // 15. Call JniMethodEnd for normal native.
    //     For @FastNative with reference return, decode the `jobject`.
    if (LIKELY(!is_fast_native) || reference_return) {
      ThreadOffset<kPointerSize> jni_end = is_fast_native
          ? QUICK_ENTRYPOINT_OFFSET(kPointerSize, pJniDecodeReferenceResult)
          : GetJniEntrypointThreadOffset<kPointerSize>(JniEntrypoint::kEnd,
                                                       reference_return,
                                                       is_synchronized);
      if (reference_return) {
        // Pass result.
        SetNativeParameter(jni_asm.get(), end_jni_conv.get(), end_jni_conv->ReturnRegister());
        end_jni_conv->Next();
      }
      if (is_synchronized) {
        // Pass object for unlocking.
        if (is_static) {
          // Load reference to the method's declaring class. The method register has been
          // clobbered by the above call, so we need to load the method from the stack.
          FrameOffset method_offset =
              FrameOffset(current_out_arg_size + mr_conv->MethodStackOffset().SizeValue());
          DCHECK_EQ(ArtMethod::DeclaringClassOffset().SizeValue(), 0u);
          if (end_jni_conv->IsCurrentParamOnStack()) {
            FrameOffset out_off = end_jni_conv->CurrentParamStackOffset();
            __ Copy(out_off, method_offset, kRawPointerSize);
          } else {
            ManagedRegister out_reg = end_jni_conv->CurrentParamRegister();
            __ Load(out_reg, method_offset, kRawPointerSize);
          }
        } else {
          mr_conv->ResetIterator(FrameOffset(current_frame_size));
          FrameOffset this_offset = mr_conv->CurrentParamStackOffset();
          if (end_jni_conv->IsCurrentParamOnStack()) {
            FrameOffset out_off = end_jni_conv->CurrentParamStackOffset();
            __ CreateJObject(out_off, this_offset, /*null_allowed=*/ false);
          } else {
            ManagedRegister out_reg = end_jni_conv->CurrentParamRegister();
            __ CreateJObject(out_reg,
                             this_offset,
                             ManagedRegister::NoRegister(),
                             /*null_allowed=*/ false);
          }
        }
        end_jni_conv->Next();
      }
      if (end_jni_conv->IsCurrentParamInRegister()) {
        __ GetCurrentThread(end_jni_conv->CurrentParamRegister());
        __ Call(end_jni_conv->CurrentParamRegister(), Offset(jni_end));
      } else {
        __ GetCurrentThread(end_jni_conv->CurrentParamStackOffset());
        __ CallFromThread(jni_end);
      }
    }

    // 16. Reload return value
    if (spill_return_value) {
      __ Load(mr_conv->ReturnRegister(), return_save_location, mr_conv->SizeOfReturnValue());
    }
  }  // if (!is_critical_native)

  // 17. Pop local reference frame.
  if (LIKELY(!is_critical_native)) {
    PopLocalReferenceFrame<kPointerSize>(
        jni_asm.get(), jni_env_reg, saved_cookie_reg, callee_save_temp);
  }

  // 18. Move frame up now we're done with the out arg space.
  //     @CriticalNative remove out args together with the frame in RemoveFrame().
  if (LIKELY(!is_critical_native)) {
    __ DecreaseFrameSize(current_out_arg_size);
    current_frame_size -= current_out_arg_size;
  }

  // 19. Process pending exceptions from JNI call or monitor exit.
  //     @CriticalNative methods do not need exception poll in the stub.
  //     @FastNative methods with reference return emit the exception poll earlier.
  if (LIKELY(!is_critical_native) && (LIKELY(!is_fast_native) || !reference_return)) {
    __ ExceptionPoll(exception_slow_path.get());
  }

  // 20. For @FastNative, we never transitioned out of runnable, so there is no transition back.
  //     Perform a suspend check if there is a flag raised, unless we have done that above
  //     for reference return.
  if (UNLIKELY(is_fast_native) && !reference_return) {
    __ SuspendCheck(suspend_check_slow_path.get());
    __ Bind(suspend_check_resume.get());
  }

  // 21. Remove activation - need to restore callee save registers since the GC may have changed
  //     them.
  DCHECK_EQ(jni_asm->cfi().GetCurrentCFAOffset(), static_cast<int>(current_frame_size));
  if (LIKELY(!is_critical_native) || !main_jni_conv->UseTailCall()) {
    // We expect the compiled method to possibly be suspended during its
    // execution, except in the case of a CriticalNative method.
    bool may_suspend = !is_critical_native;
    __ RemoveFrame(current_frame_size, callee_save_regs, may_suspend);
    DCHECK_EQ(jni_asm->cfi().GetCurrentCFAOffset(), static_cast<int>(current_frame_size));
  }

  // 22. Read barrier slow path for the declaring class in the method for a static call.
  //     Skip this for @CriticalNative because we're not passing a `jclass` to the native method.
  if (kUseReadBarrier && is_static && !is_critical_native) {
    __ Bind(jclass_read_barrier_slow_path.get());

    // We do the marking check after adjusting for outgoing arguments. That ensures that
    // we have space available for at least two params in case we need to pass the read
    // barrier parameters on stack (only x86). But that means we must adjust the CFI
    // offset accordingly as it does not include the outgoing args after `RemoveFrame().
    if (main_out_arg_size != 0) {
      // Note: The DW_CFA_def_cfa_offset emitted by `RemoveFrame()` above
      // is useless when it is immediatelly overridden here but avoiding
      // it adds a lot of code complexity for minimal gain.
      jni_asm->cfi().AdjustCFAOffset(main_out_arg_size);
    }

    // We enter the slow path with the method register unclobbered.
    method_register = mr_conv->MethodRegister();

    // Construct slow path for read barrier:
    //
    // Call into the runtime's ReadBarrierJni and have it fix up
    // the object address if it was moved.

    ThreadOffset<kPointerSize> read_barrier = QUICK_ENTRYPOINT_OFFSET(kPointerSize,
                                                                      pReadBarrierJni);
    main_jni_conv->ResetIterator(FrameOffset(main_out_arg_size));
    // Pass the pointer to the method's declaring class as the first argument.
    DCHECK_EQ(ArtMethod::DeclaringClassOffset().SizeValue(), 0u);
    SetNativeParameter(jni_asm.get(), main_jni_conv.get(), method_register);
    main_jni_conv->Next();
    // Pass the current thread as the second argument and call.
    if (main_jni_conv->IsCurrentParamInRegister()) {
      __ GetCurrentThread(main_jni_conv->CurrentParamRegister());
      __ Call(main_jni_conv->CurrentParamRegister(), Offset(read_barrier));
    } else {
      __ GetCurrentThread(main_jni_conv->CurrentParamStackOffset());
      __ CallFromThread(read_barrier);
    }
    if (UNLIKELY(is_synchronized || is_fast_native)) {
      // Reload the method pointer in the slow path because it is needed
      // as an argument for the `JniMethodStartSynchronized`, or for @FastNative.
      __ Load(method_register,
              FrameOffset(main_out_arg_size + mr_conv->MethodStackOffset().SizeValue()),
              kRawPointerSize);
    }

    // Return to main path.
    __ Jump(jclass_read_barrier_return.get());

    // Undo the CFI offset adjustment at the start of the slow path.
    if (main_out_arg_size != 0) {
      jni_asm->cfi().AdjustCFAOffset(-main_out_arg_size);
    }
  }

  // 23. Emit suspend check slow path.
  if (UNLIKELY(is_fast_native)) {
    __ Bind(suspend_check_slow_path.get());
    if (reference_return && main_out_arg_size != 0) {
      jni_asm->cfi().AdjustCFAOffset(main_out_arg_size);
      __ DecreaseFrameSize(main_out_arg_size);
    }
    __ CallFromThread(QUICK_ENTRYPOINT_OFFSET(kPointerSize, pTestSuspend));
    if (reference_return) {
      // Suspend check entry point overwrites top of managed stack and leaves it clobbered.
      // We need to restore the top for subsequent runtime call to `JniDecodeReferenceResult()`.
      __ StoreStackPointerToThread(Thread::TopOfManagedStackOffset<kPointerSize>());
    }
    if (reference_return && main_out_arg_size != 0) {
      __ IncreaseFrameSize(main_out_arg_size);
      jni_asm->cfi().AdjustCFAOffset(-main_out_arg_size);
    }
    __ Jump(suspend_check_resume.get());
  }

  // 24. Emit exception poll slow paths.
  if (LIKELY(!is_critical_native)) {
    if (UNLIKELY(is_synchronized)) {
      DCHECK(!is_fast_native);
      __ Bind(monitor_enter_exception_slow_path.get());
      if (main_out_arg_size != 0) {
        jni_asm->cfi().AdjustCFAOffset(main_out_arg_size);
        __ DecreaseFrameSize(main_out_arg_size);
      }
    }
    __ Bind(exception_slow_path.get());
    if (UNLIKELY(is_fast_native) && reference_return) {
      // We performed the exception check early, so we need to adjust SP and pop IRT frame.
      if (main_out_arg_size != 0) {
        jni_asm->cfi().AdjustCFAOffset(main_out_arg_size);
        __ DecreaseFrameSize(main_out_arg_size);
      }
      PopLocalReferenceFrame<kPointerSize>(
          jni_asm.get(), jni_env_reg, saved_cookie_reg, callee_save_temp);
    }
    DCHECK_EQ(jni_asm->cfi().GetCurrentCFAOffset(), static_cast<int>(current_frame_size));
    __ DeliverPendingException();
  }

  // 21. Finalize code generation
  __ FinalizeCode();
  size_t cs = __ CodeSize();
  std::vector<uint8_t> managed_code(cs);
  MemoryRegion code(&managed_code[0], managed_code.size());
  __ FinalizeInstructions(code);

  return JniCompiledMethod(instruction_set,
                           std::move(managed_code),
                           managed_frame_size,
                           main_jni_conv->CoreSpillMask(),
                           main_jni_conv->FpSpillMask(),
                           ArrayRef<const uint8_t>(*jni_asm->cfi().data()));
}

template <PointerSize kPointerSize>
static void PushLocalReferenceFrame(JNIMacroAssembler<kPointerSize>* jni_asm,
                                    ManagedRegister jni_env_reg,
                                    ManagedRegister saved_cookie_reg,
                                    ManagedRegister temp_reg) {
  const size_t kRawPointerSize = static_cast<size_t>(kPointerSize);
  const MemberOffset jni_env_cookie_offset = JNIEnvExt::LocalRefCookieOffset(kRawPointerSize);
  const MemberOffset jni_env_segment_state_offset = JNIEnvExt::SegmentStateOffset(kRawPointerSize);

  // Load the old cookie that we shall need to restore.
  __ Load(saved_cookie_reg, jni_env_reg, jni_env_cookie_offset, kIRTCookieSize);

  // Set the cookie in JNI environment to the current segment state.
  __ Load(temp_reg, jni_env_reg, jni_env_segment_state_offset, kIRTCookieSize);
  __ Store(jni_env_reg, jni_env_cookie_offset, temp_reg, kIRTCookieSize);
}

template <PointerSize kPointerSize>
static void PopLocalReferenceFrame(JNIMacroAssembler<kPointerSize>* jni_asm,
                                   ManagedRegister jni_env_reg,
                                   ManagedRegister saved_cookie_reg,
                                   ManagedRegister temp_reg) {
  const size_t kRawPointerSize = static_cast<size_t>(kPointerSize);
  const MemberOffset jni_env_cookie_offset = JNIEnvExt::LocalRefCookieOffset(kRawPointerSize);
  const MemberOffset jni_env_segment_state_offset = JNIEnvExt::SegmentStateOffset(kRawPointerSize);

  // Set the current segment state to the current cookie in JNI environment.
  __ Load(temp_reg, jni_env_reg, jni_env_cookie_offset, kIRTCookieSize);
  __ Store(jni_env_reg, jni_env_segment_state_offset, temp_reg, kIRTCookieSize);

  // Restore the cookie in JNI environment to the saved value.
  __ Store(jni_env_reg, jni_env_cookie_offset, saved_cookie_reg, kIRTCookieSize);
}

// Copy a single parameter from the managed to the JNI calling convention.
template <PointerSize kPointerSize>
static void CopyParameter(JNIMacroAssembler<kPointerSize>* jni_asm,
                          ManagedRuntimeCallingConvention* mr_conv,
                          JniCallingConvention* jni_conv) {
  // We spilled all registers, so use stack locations.
  // TODO: Move args in registers for @CriticalNative.
  bool input_in_reg = false;  // mr_conv->IsCurrentParamInRegister();
  bool output_in_reg = jni_conv->IsCurrentParamInRegister();
  FrameOffset spilled_reference_offset(0);
  bool null_allowed = false;
  bool ref_param = jni_conv->IsCurrentParamAReference();
  CHECK(!ref_param || mr_conv->IsCurrentParamAReference());
  if (output_in_reg) {  // output shouldn't straddle registers and stack
    CHECK(!jni_conv->IsCurrentParamOnStack());
  } else {
    CHECK(jni_conv->IsCurrentParamOnStack());
  }
  // References are spilled to caller's reserved out vreg area.
  if (ref_param) {
    null_allowed = mr_conv->IsCurrentArgPossiblyNull();
    // Compute spilled reference offset. Note that null is spilled but the jobject
    // passed to the native code must be null (not a pointer into the spilled value
    // as with regular references).
    spilled_reference_offset = mr_conv->CurrentParamStackOffset();
    // Check that spilled reference offset is in the spill area in the caller's frame.
    CHECK_GT(spilled_reference_offset.Uint32Value(), mr_conv->GetDisplacement().Uint32Value());
  }
  if (input_in_reg && output_in_reg) {
    ManagedRegister in_reg = mr_conv->CurrentParamRegister();
    ManagedRegister out_reg = jni_conv->CurrentParamRegister();
    if (ref_param) {
      __ CreateJObject(out_reg, spilled_reference_offset, in_reg, null_allowed);
    } else {
      if (!mr_conv->IsCurrentParamOnStack()) {
        // regular non-straddling move
        __ Move(out_reg, in_reg, mr_conv->CurrentParamSize());
      } else {
        UNIMPLEMENTED(FATAL);  // we currently don't expect to see this case
      }
    }
  } else if (!input_in_reg && !output_in_reg) {
    FrameOffset out_off = jni_conv->CurrentParamStackOffset();
    if (ref_param) {
      __ CreateJObject(out_off, spilled_reference_offset, null_allowed);
    } else {
      FrameOffset in_off = mr_conv->CurrentParamStackOffset();
      size_t param_size = mr_conv->CurrentParamSize();
      CHECK_EQ(param_size, jni_conv->CurrentParamSize());
      __ Copy(out_off, in_off, param_size);
    }
  } else if (!input_in_reg && output_in_reg) {
    FrameOffset in_off = mr_conv->CurrentParamStackOffset();
    ManagedRegister out_reg = jni_conv->CurrentParamRegister();
    // Check that incoming stack arguments are above the current stack frame.
    CHECK_GT(in_off.Uint32Value(), mr_conv->GetDisplacement().Uint32Value());
    if (ref_param) {
      __ CreateJObject(out_reg,
                       spilled_reference_offset,
                       ManagedRegister::NoRegister(),
                       null_allowed);
    } else {
      size_t param_size = mr_conv->CurrentParamSize();
      CHECK_EQ(param_size, jni_conv->CurrentParamSize());
      __ Load(out_reg, in_off, param_size);
    }
  } else {
    CHECK(input_in_reg && !output_in_reg);
    ManagedRegister in_reg = mr_conv->CurrentParamRegister();
    FrameOffset out_off = jni_conv->CurrentParamStackOffset();
    // Check outgoing argument is within frame part dedicated to out args.
    CHECK_LT(out_off.Uint32Value(), jni_conv->GetDisplacement().Uint32Value());
    if (ref_param) {
      // TODO: recycle value in in_reg rather than reload from spill slot.
      __ CreateJObject(out_off, spilled_reference_offset, null_allowed);
    } else {
      size_t param_size = mr_conv->CurrentParamSize();
      CHECK_EQ(param_size, jni_conv->CurrentParamSize());
      if (!mr_conv->IsCurrentParamOnStack()) {
        // regular non-straddling store
        __ Store(out_off, in_reg, param_size);
      } else {
        // store where input straddles registers and stack
        CHECK_EQ(param_size, 8u);
        FrameOffset in_off = mr_conv->CurrentParamStackOffset();
        __ StoreSpanning(out_off, in_reg, in_off);
      }
    }
  }
}

template <PointerSize kPointerSize>
static void SetNativeParameter(JNIMacroAssembler<kPointerSize>* jni_asm,
                               JniCallingConvention* jni_conv,
                               ManagedRegister in_reg) {
  if (jni_conv->IsCurrentParamOnStack()) {
    FrameOffset dest = jni_conv->CurrentParamStackOffset();
    __ StoreRawPtr(dest, in_reg);
  } else {
    if (!jni_conv->CurrentParamRegister().Equals(in_reg)) {
      __ Move(jni_conv->CurrentParamRegister(), in_reg, jni_conv->CurrentParamSize());
    }
  }
}

JniCompiledMethod ArtQuickJniCompileMethod(const CompilerOptions& compiler_options,
                                           uint32_t access_flags,
                                           uint32_t method_idx,
                                           const DexFile& dex_file) {
  if (Is64BitInstructionSet(compiler_options.GetInstructionSet())) {
    return ArtJniCompileMethodInternal<PointerSize::k64>(
        compiler_options, access_flags, method_idx, dex_file);
  } else {
    return ArtJniCompileMethodInternal<PointerSize::k32>(
        compiler_options, access_flags, method_idx, dex_file);
  }
}

}  // namespace art
