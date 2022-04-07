#!/usr/bin/env python3
#
# Copyright (C) 2022 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Checks dwarf CFI (unwinding) information by comparing it to disassembly.
It is only a simple heuristic check of stack pointer adjustments.
Fully inferring CFI from disassembly is not possible in general.
"""

import os, re, subprocess, collections, pathlib, bisect, collections
from typing import List, Optional, Set, Tuple

Source = collections.namedtuple("Source", ["addr", "file", "line", "flag"])

def get_source(lib: pathlib.Path) -> List[Source]:
  """ Get source-file and line-number for all hand-written assembly code. """

  proc = subprocess.run(["llvm-dwarfdump", "--debug-line", lib],
                        encoding='utf-8',
                        capture_output=True,
                        check=True)

  section_re = re.compile("^debug_line\[0x[0-9a-f]+\]$", re.MULTILINE)
  filename_re = re.compile('file_names\[ *(\d)+\]:\n\s*name: "(.*)"', re.MULTILINE)
  line_re = re.compile('0x([0-9a-f]{16}) +(\d+) +\d+ +(\d+)'  # addr, line, column, file
                       ' +\d+ +\d +(.*)')                     # isa, discriminator, flag

  results = []
  for section in section_re.split(proc.stdout):
    files = {m[1]: m[2] for m in filename_re.finditer(section)}
    if not any(f.endswith(".S") for f in files.values()):
      continue
    lines = line_re.findall(section)
    results.extend([Source(int(a, 16), files[fn], l, fg) for a, l, fn, fg in lines])
  return sorted(filter(lambda line: "end_sequence" not in line.flag, results))

Fde = collections.namedtuple("Fde", ["addr", "end", "data"])

def get_fde(lib: pathlib.Path) -> List[Fde]:
  """ Get all unwinding FDE blocks (in dumped text-based format) """

  proc = subprocess.run(["llvm-dwarfdump", "--debug-frame", lib],
                        encoding='utf-8',
                        capture_output=True,
                        check=True)

  section_re = re.compile("\n(?! |\n)", re.MULTILINE)  # New-line not followed by indent.
  fda_re = re.compile(".* FDE .* pc=([0-9a-f]+)...([0-9a-f]+)")

  results = []
  for section in section_re.split(proc.stdout):
    m = fda_re.match(section)
    if m:
      fde = Fde(int(m[1], 16), int(m[2], 16), section)
      if fde.addr != 0:
        results.append(fde)
  return sorted(results)

Asm = collections.namedtuple("Asm", ["addr", "name", "data"])

def get_asm(lib: pathlib.Path) -> List[Asm]:
  """ Get disassembly for all methods (in dumped text-based format) """

  proc = subprocess.run(["llvm-objdump", "--disassemble", lib],
                        encoding='utf-8',
                        capture_output=True,
                        check=True)

  section_re = re.compile("\n(?! |\n)", re.MULTILINE)  # New-line not followed by indent.
  sym_re = re.compile("([0-9a-f]+) <(.+)>:")

  results = []
  for section in section_re.split(proc.stdout):
    sym = sym_re.match(section)
    if sym:
      results.append(Asm(int(sym[1], 16), sym[2], section))
  return sorted(results)

Cfa = collections.namedtuple("Cfa", ["addr", "cfa"])

def get_cfa(fde: Fde) -> List[Cfa]:
  """ Extract individual CFA (SP+offset) entries from the FDE block """

  cfa_re = re.compile("0x([0-9a-f]+): CFA=([^\s:]+)")
  return [Cfa(int(addr, 16), cfa) for addr, cfa in cfa_re.findall(fde.data)]

Inst = collections.namedtuple("Inst", ["addr", "inst", "symbol"])

def get_instructions(asm: Asm) -> List[Inst]:
  """ Extract individual instructions from disassembled code block """

  data = re.sub(r"[ \t]+", " ", asm.data)
  inst_re = re.compile(r"([0-9a-f]+): +(?:[0-9a-f]{2} +)*(.*)")
  return [Inst(int(addr, 16), inst, asm.name) for addr, inst in inst_re.findall(data)]

CfaOffset = collections.namedtuple("CfaOffset", ["addr", "offset"])

def get_dwarf_cfa_offsets(cfas: List[Cfa]) -> List[CfaOffset]:
  """ Parse textual CFA entries into integer stack offsets """

  result = []
  for addr, cfa in cfas:
    if cfa == "WSP" or cfa == "SP":
      result.append(CfaOffset(addr, 0))
    elif cfa.startswith("WSP+") or cfa.startswith("SP+"):
      result.append(CfaOffset(addr, int(cfa.split("+")[1])))
    else:
      result.append(CfaOffset(addr, None))
  return result

def get_infered_cfa_offsets(insts: List[Inst]) -> List[CfaOffset]:
  """ Heuristic to convert disassembly into stack offsets """

  # Regular expressions which find instructions that adjust stack pointer.
  rexprs = []
  def add(rexpr, adjust_offset):
    rexprs.append((re.compile(rexpr), adjust_offset))
  add(r"sub sp,(?: sp,)? #(\d+)", lambda m: int(m[1]))
  add(r"add sp,(?: sp,)? #(\d+)", lambda m: -int(m[1]))
  add(r"str \w+, \[sp, #-(\d+)\]!", lambda m: int(m[1]))
  add(r"ldr \w+, \[sp\], #(\d+)", lambda m: -int(m[1]))
  add(r"stp \w+, \w+, \[sp, #-(\d+)\]!", lambda m: int(m[1]))
  add(r"ldp \w+, \w+, \[sp\], #(\d+)", lambda m: -int(m[1]))
  add(r"vpush \{([d0-9, ]*)\}", lambda m: 8 * len(m[1].split(",")))
  add(r"vpop \{([d0-9, ]*)\}", lambda m: -8 * len(m[1].split(",")))
  add(r"v?push(?:\.w)? \{([\w+, ]*)\}", lambda m: 4 * len(m[1].split(",")))
  add(r"v?pop(?:\.w)? \{([\w+, ]*)\}", lambda m: -4 * len(m[1].split(",")))

  # Regular expression which identifies branches.
  jmp_re = re.compile(r"cb\w* \w+, 0x(\w+)|(?:b|bl|b\w\w) 0x(\w+)")

  offset, future_offset = 0, {}
  result = [CfaOffset(insts[0].addr, offset)]
  for addr, inst, symbol in insts:
    # Previous code branched here, so us that offset instead.
    # This likely identifies slow-path which is after return.
    if addr in future_offset:
      offset = future_offset[addr]

    # Add entry to output (only if the offset changed).
    if result[-1].offset != offset:
      result.append(CfaOffset(addr, offset))

    # Adjust offset if the instruction modifies stack pointer.
    for rexpr, adjust_offset in rexprs:
      m = rexpr.match(inst)
      if m:
        offset += adjust_offset(m)
        break  # First matched pattern wins.

    # Record branches.  We only support forward edges for now.
    m = jmp_re.match(inst)
    if m:
      future_offset[int(m[m.lastindex], 16)] = offset
  return result

def check_fde(fde: Fde, insts: List[Inst], srcs, verbose: bool = False) -> Tuple[str, Set[int]]:
  """ Compare DWARF offsets to assembly-inferred offsets. Report differences. """

  error, seen_addrs = None, set()
  cfas = get_cfa(fde)
  i, dwarf_cfa = 0, get_dwarf_cfa_offsets(cfas)
  j, infered_cfa = 0, get_infered_cfa_offsets(insts)
  for inst in insts:
    seen_addrs.add(inst.addr)
    while i+1 < len(dwarf_cfa) and dwarf_cfa[i+1].addr <= inst.addr:
      i += 1
    while j+1 < len(infered_cfa) and infered_cfa[j+1].addr <= inst.addr:
      j += 1
    if verbose:
      print("{:08x}: dwarf={:4} infered={:4} {:40} // {}".format(
                inst.addr, str(dwarf_cfa[i].offset), str(infered_cfa[j].offset),
                inst.inst.strip(), srcs.get(inst.addr, "")))
    if dwarf_cfa[i].offset is not None and dwarf_cfa[i].offset != infered_cfa[j].offset:
      if inst.addr in srcs:  # Only report if it maps to source code (not padding or literals).
        error = error or "{:08x} {}".format(inst.addr, srcs.get(inst.addr, ""))
  return error, seen_addrs

def check_lib(lib: pathlib.Path):
  assert lib.exists()
  IGNORE = [
      "art_quick_throw_null_pointer_exception_from_signal",  # Starts with non-zero offset.
      "art_quick_generic_jni_trampoline",  # Saves/restores SP in other register.
      "nterp_op_",  # Uses calculated CFA due to dynamic stack size.
      "$d.",  # Data (literals) interleaved within code.
  ]
  fdes = get_fde(lib)
  asms = collections.deque(get_asm(lib))
  srcs = {src.addr: src.file + ":" + src.line for src in get_source(lib)}
  seen = set()  # Used to verify the we have covered all assembly source lines.

  for fde in fdes:
    if fde.addr not in srcs:
      continue  # Ignore if it is not hand-written assembly.

    # Assembly instructions (one FDE can cover several assembly chunks).
    all_insts, name = [], None
    while asms and asms[0].addr < fde.end:
      asm = asms.popleft()
      if asm.addr < fde.addr:
        continue
      insts = get_instructions(asm)
      if any(asm.name.startswith(i) for i in IGNORE):
        seen.update([inst.addr for inst in insts])
        continue
      all_insts.extend(insts)
      name = name or asm.name
    if not all_insts:
      continue  # No assembly

    # Compare DWARF data to assembly instructions
    error, seen_addrs = check_fde(fde, all_insts, srcs)
    if error:
      print("ERROR at " + name + " " + error)
      check_fde(fde, all_insts, srcs, True)
      print("")
    seen.update(seen_addrs)
  for addr in sorted(set(srcs.keys()) - seen):
    print("Missing CFI for {:08x}: {}".format(addr, srcs[addr]))


def main(argv):
  """ Check libraries provided on the command line, or use the default build output """

  libs = argv[1:]
  if not libs:
    out = os.environ["OUT"]
    libs.append(out + "/symbols/apex/com.android.art/lib/libart.so")
    libs.append(out + "/symbols/apex/com.android.art/lib64/libart.so")
  for lib in libs:
    check_lib(pathlib.Path(lib))

if __name__ == "__main__":
  main(os.sys.argv)
