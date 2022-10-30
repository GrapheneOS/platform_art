#!/usr/bin/env python3
#
# Copyright (C) 2021 The Android Open Source Project
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
This scripts compiles Java files which are needed to execute run-tests.
It is intended to be used only from soong genrule.
"""

from argparse import ArgumentParser
from art_build_rules import BuildTestContext, default_build
from fcntl import lockf, LOCK_EX, LOCK_NB
from importlib.machinery import SourceFileLoader
from multiprocessing import Pool
from multiprocessing.pool import ApplyResult
from os import environ, getcwd, chdir, cpu_count
from os.path import join, basename
from pathlib import Path
from re import match
from shutil import copytree
from subprocess import run
from typing import Dict

ZIP = "prebuilts/build-tools/linux-x86/bin/soong_zip"

lock_file = None  # Keep alive as long as this process is alive.


def copy_sources(args, ziproot: Path, mode: str, srcdir: Path) -> Path:
  """Copy test files from Android tree into the build sandbox and return its path."""

  dstdir = ziproot / mode / srcdir.name
  copytree(srcdir, dstdir)
  return dstdir


def build_test(ctx: BuildTestContext) -> None:
  """Run the build script for single run-test"""

  chdir(ctx.test_dir)
  script = ctx.test_dir / "build.py"
  if script.exists():
    module = SourceFileLoader("build_" + ctx.test_name,
                              str(script)).load_module()
    module.build(ctx)
  else:
    default_build(ctx)


# If we build just individual shard, we want to split the work among all the cores,
# but if the build system builds all shards, we don't want to overload the machine.
# We don't know which situation we are in, so as simple work-around, we use a lock
# file to allow only one shard to use multiprocessing at the same time.
def use_multiprocessing(mode: str) -> bool:
  global lock_file
  lock_path = join(environ["TMPDIR"], "art-test-run-test-build-py-" + mode)
  lock_file = open(lock_path, "w")
  try:
    lockf(lock_file, LOCK_EX | LOCK_NB)
    return True  # We are the only instance of this script in the build system.
  except BlockingIOError:
    return False  # Some other instance is already running.


def main() -> None:
  parser = ArgumentParser(description=__doc__)
  parser.add_argument(
      "--out", help="Path of the generated ZIP file with the build data")
  parser.add_argument("--mode", choices=["host", "jvm", "target"])
  parser.add_argument(
      "--shard", help="Identifies subset of tests to build (00..99)")
  parser.add_argument(
      "--bootclasspath", help="JAR files used for javac compilation")
  args = parser.parse_args()

  build_top = Path(getcwd())
  sbox = Path(__file__).absolute().parent.parent.parent.parent.parent
  assert sbox.parent.name == "sbox" and len(sbox.name) == 40

  ziproot = sbox / "zip"
  srcdirs = sorted(build_top.glob("art/test/*"))
  srcdirs = [s for s in srcdirs if match("\d*{}-.*".format(args.shard), s.name)]
  dstdirs = [copy_sources(args, ziproot, args.mode, s) for s in srcdirs]

  # Use multiprocessing (i.e. forking) since tests modify their current working directory.
  with Pool(cpu_count() if use_multiprocessing(args.mode) else 1) as pool:
    jobs: Dict[Path, ApplyResult] = {}
    for dstdir in dstdirs:
      ctx = BuildTestContext(args, build_top, sbox, dstdir.name, dstdir)
      jobs[dstdir] = pool.apply_async(build_test, (ctx,))
    for dstdir, job in jobs.items():
      try:
        job.get()
      except Exception as e:
        raise Exception("Failed to build " + dstdir.name) from e.__cause__

  # Create the final zip file which contains the content of the temporary directory.
  proc = run([ZIP, "-o", args.out, "-C", ziproot, "-D", ziproot], check=True)


if __name__ == "__main__":
  main()
