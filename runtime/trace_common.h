/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef ART_RUNTIME_TRACE_COMMON_H_
#define ART_RUNTIME_TRACE_COMMON_H_

#include "android-base/stringprintf.h"
#include "art_method-inl.h"
#include "dex/descriptors_names.h"
#include "oat/oat_quick_method_header.h"

using android::base::StringPrintf;

namespace art HIDDEN {

static std::string GetMethodInfoLine(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
  method = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);
  return StringPrintf("%s\t%s\t%s\t%s\n",
                      PrettyDescriptor(method->GetDeclaringClassDescriptor()).c_str(),
                      method->GetName(),
                      method->GetSignature().ToString().c_str(),
                      method->GetDeclaringClassSourceFile());
}

class TimestampCounter {
 public:
  static uint64_t GetTimestamp() {
    uint64_t t = 0;
#if defined(__arm__)
    // On ARM 32 bit, we don't always have access to the timestamp counters from user space. There
    // is no easy way to check if it is safe to read the timestamp counters. There is HWCAP_EVTSTRM
    // which is set when generic timer is available but not necessarily from the user space. Kernel
    // disables access to generic timer when there are known problems on the target CPUs. Sometimes
    // access is disabled only for 32-bit processes even when 64-bit processes can accesses the
    // timer from user space. These are not reflected in the HWCAP_EVTSTRM capability.So just
    // fallback to clock_gettime on these processes. See b/289178149 for more discussion.
    t = MicroTime();
#elif defined(__aarch64__)
    // See Arm Architecture Registers  Armv8 section System Registers
    asm volatile("mrs %0, cntvct_el0" : "=r"(t));
#elif defined(__i386__) || defined(__x86_64__)
    // rdtsc returns two 32-bit values in rax and rdx even on 64-bit architectures.
    unsigned int lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    t = (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__riscv)
    asm volatile("rdtime %0" : "=r"(t));
#else
    t = MicroTime();
#endif
    return t;
  }

  static void InitializeTimestampCounters() {
    // It is sufficient to initialize this once for the entire execution. Just return if it is
    // already initialized.
    if (tsc_to_microsec_scaling_factor > 0.0) {
      return;
    }

#if defined(__arm__)
    // On ARM 32 bit, we don't always have access to the timestamp counters from
    // user space. Seem comment in GetTimestamp for more details.
    tsc_to_microsec_scaling_factor = 1.0;
#elif defined(__aarch64__)
    double seconds_to_microseconds = 1000 * 1000;
    uint64_t freq = 0;
    // See Arm Architecture Registers  Armv8 section System Registers
    asm volatile("mrs %0,  cntfrq_el0" : "=r"(freq));
    if (freq == 0) {
      // It is expected that cntfrq_el0 is correctly setup during system initialization but some
      // devices don't do this. In such cases fall back to computing the frequency. See b/315139000.
      tsc_to_microsec_scaling_factor = computeScalingFactor();
    } else {
      tsc_to_microsec_scaling_factor = seconds_to_microseconds / static_cast<double>(freq);
    }
#elif defined(__i386__) || defined(__x86_64__)
    tsc_to_microsec_scaling_factor = GetScalingFactorForX86();
#else
    tsc_to_microsec_scaling_factor = 1.0;
#endif
  }

  static ALWAYS_INLINE uint64_t GetMicroTime(uint64_t counter) {
    DCHECK(tsc_to_microsec_scaling_factor > 0.0) << tsc_to_microsec_scaling_factor;
    return tsc_to_microsec_scaling_factor * counter;
  }

 private:
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
  // Here we compute the scaling factor by sleeping for a millisecond. Alternatively, we could
  // generate raw timestamp counter and also time using clock_gettime at the start and the end of
  // the trace. We can compute the frequency of timestamp counter upadtes in the post processing
  // step using these two samples. However, that would require a change in Android Studio which is
  // the main consumer of these profiles. For now, just compute the frequency of tsc updates here.
  static double computeScalingFactor() {
    uint64_t start = MicroTime();
    uint64_t start_tsc = GetTimestamp();
    // Sleep for one millisecond.
    usleep(1000);
    uint64_t diff_tsc = GetTimestamp() - start_tsc;
    uint64_t diff_time = MicroTime() - start;
    double scaling_factor = static_cast<double>(diff_time) / diff_tsc;
    DCHECK(scaling_factor > 0.0) << scaling_factor;
    return scaling_factor;
  }
#endif

#if defined(__i386__) || defined(__x86_64__)
  static double GetScalingFactorForX86() {
    uint32_t eax, ebx, ecx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(0x0), "c"(0));
    if (eax < 0x15) {
      // There is no 15H - Timestamp counter and core crystal clock information
      // leaf. Just compute the frequency.
      return computeScalingFactor();
    }

    // From Intel architecture-instruction-set-extensions-programming-reference:
    // EBX[31:0]/EAX[31:0] indicates the ratio of the TSC frequency and the
    // core crystal clock frequency.
    // If EBX[31:0] is 0, the TSC and "core crystal clock" ratio is not enumerated.
    // If ECX is 0, the nominal core crystal clock frequency is not enumerated.
    // "TSC frequency" = "core crystal clock frequency" * EBX/EAX.
    // The core crystal clock may differ from the reference clock, bus clock, or core clock
    // frequencies.
    // EAX Bits 31 - 00: An unsigned integer which is the denominator of the
    //                   TSC/"core crystal clock" ratio.
    // EBX Bits 31 - 00: An unsigned integer which is the numerator of the
    //                   TSC/"core crystal clock" ratio.
    // ECX Bits 31 - 00: An unsigned integer which is the nominal frequency of the core
    //                   crystal clock in Hz.
    // EDX Bits 31 - 00: Reserved = 0.
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(0x15), "c"(0));
    if (ebx == 0 || ecx == 0) {
      return computeScalingFactor();
    }
    double coreCrystalFreq = ecx;
    // frequency = coreCrystalFreq * (ebx / eax)
    // scaling_factor = seconds_to_microseconds / frequency
    //                = seconds_to_microseconds * eax / (coreCrystalFreq * ebx)
    double seconds_to_microseconds = 1000 * 1000;
    double scaling_factor = (seconds_to_microseconds * eax) / (coreCrystalFreq * ebx);
    return scaling_factor;
  }
#endif

  // Scaling factor to convert timestamp counter into wall clock time reported in micro seconds.
  // This is initialized at the start of tracing using the timestamp counter update frequency.
  // See InitializeTimestampCounters for more details.
  static double tsc_to_microsec_scaling_factor;
};

}  // namespace art

#endif  // ART_RUNTIME_TRACE_COMMON_H_
