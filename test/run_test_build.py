#!/usr/bin/env python3
#
# Copyright (C) 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
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

import argparse
import functools
import glob
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import zipfile

from argparse import ArgumentParser
from fcntl import lockf, LOCK_EX, LOCK_NB
from importlib.machinery import SourceFileLoader
from concurrent.futures import ThreadPoolExecutor
from os import environ, getcwd, chdir, cpu_count, chmod
from os.path import relpath
from pathlib import Path
from pprint import pprint
from re import match
from shutil import copytree, rmtree
from subprocess import run
from tempfile import TemporaryDirectory, NamedTemporaryFile
from typing import Dict, List, Union, Set

USE_RBE_FOR_JAVAC = 100    # Percentage of tests that can use RBE (between 0 and 100)
USE_RBE_FOR_D8 = 100       # Percentage of tests that can use RBE (between 0 and 100)

lock_file = None  # Keep alive as long as this process is alive.


class BuildTestContext:
  def __init__(self, args, android_build_top, test_dir):
    self.test_name = test_dir.name
    self.test_dir = test_dir.absolute()
    self.mode = args.mode
    self.jvm = (self.mode == "jvm")
    self.host = (self.mode == "host")
    self.target = (self.mode == "target")
    assert self.jvm or self.host or self.target

    self.android_build_top = android_build_top.absolute()

    self.java_home = Path(os.environ.get("JAVA_HOME")).absolute()
    self.java = self.java_home / "bin/java"
    self.javac = self.java_home / "bin/javac"
    self.javac_args = "-g -Xlint:-options -source 1.8 -target 1.8"

    self.bootclasspath = args.bootclasspath.absolute()
    self.d8 = args.d8.absolute()
    self.hiddenapi = args.hiddenapi.absolute()
    self.jasmin = args.jasmin.absolute()
    self.smali = args.smali.absolute()
    self.soong_zip = args.soong_zip.absolute()
    self.zipalign = args.zipalign.absolute()

    self.need_dex = (self.host or self.target)

    # Minimal environment needed for bash commands that we execute.
    self.bash_env = {
      "ANDROID_BUILD_TOP": self.android_build_top,
      "D8": self.d8,
      "JAVA": self.java,
      "JAVAC": self.javac,
      "JAVAC_ARGS": self.javac_args,
      "JAVA_HOME": self.java_home,
      "PATH": os.environ["PATH"],
      "PYTHONDONTWRITEBYTECODE": "1",
      "SMALI": self.smali,
      "SOONG_ZIP": self.soong_zip,
      "TEST_NAME": self.test_name,
    }

  def bash(self, cmd):
    return subprocess.run(cmd,
                          shell=True,
                          cwd=self.test_dir,
                          env=self.bash_env,
                          check=True)

  def default_build(self, **kwargs):
    globals()['default_build'](self, **kwargs)

def rm(*patterns):
  for pattern in patterns:
    for path in glob.glob(pattern):
      if os.path.isdir(path):
        shutil.rmtree(path)
      else:
        os.remove(path)

def default_build(
    ctx,
    use_desugar=True,
    use_hiddenapi=True,
    need_dex=None,
    experimental="no-experiment",
    zip_compression_method="deflate",
    zip_align_bytes=None,
    api_level=None,
    javac_args=[],
    d8_flags=[],
    smali_args=[],
    has_smali=None,
    has_jasmin=None,
  ):

  # Wrap "pathlib.Path" with our own version that ensures all paths are absolute.
  # Plain filenames are assumed to be relative to ctx.test_dir and made absolute.
  class Path(pathlib.Path):
    def __new__(cls, filename: str):
      path = pathlib.Path(filename)
      return path if path.is_absolute() else (ctx.test_dir / path)

  ANDROID_BUILD_TOP = ctx.android_build_top
  TEST_NAME = ctx.test_name
  NEED_DEX = ctx.need_dex if need_dex is None else need_dex

  RBE_exec_root = os.environ.get("RBE_exec_root")
  RBE_rewrapper = ctx.android_build_top / "prebuilts/remoteexecution-client/live/rewrapper"

  # Set default values for directories.
  HAS_SMALI = Path("smali").exists() if has_smali is None else has_smali
  HAS_JASMIN = Path("jasmin").exists() if has_jasmin is None else has_jasmin
  HAS_SRC = Path("src").exists()
  HAS_SRC_ART = Path("src-art").exists()
  HAS_SRC2 = Path("src2").exists()
  HAS_SRC_MULTIDEX = Path("src-multidex").exists()
  HAS_SMALI_MULTIDEX = Path("smali-multidex").exists()
  HAS_JASMIN_MULTIDEX = Path("jasmin-multidex").exists()
  HAS_SMALI_EX = Path("smali-ex").exists()
  HAS_SRC_EX = Path("src-ex").exists()
  HAS_SRC_EX2 = Path("src-ex2").exists()
  HAS_SRC_AOTEX = Path("src-aotex").exists()
  HAS_SRC_BCPEX = Path("src-bcpex").exists()
  HAS_HIDDENAPI_SPEC = Path("hiddenapi-flags.csv").exists()

  JAVAC_ARGS = shlex.split(ctx.javac_args) + javac_args
  SMALI_ARGS = smali_args.copy()
  D8_FLAGS = d8_flags.copy()

  BUILD_MODE = ctx.mode

  # Setup experimental API level mappings in a bash associative array.
  EXPERIMENTAL_API_LEVEL = {}
  EXPERIMENTAL_API_LEVEL["no-experiment"] = "26"
  EXPERIMENTAL_API_LEVEL["default-methods"] = "24"
  EXPERIMENTAL_API_LEVEL["parameter-annotations"] = "25"
  EXPERIMENTAL_API_LEVEL["agents"] = "26"
  EXPERIMENTAL_API_LEVEL["method-handles"] = "26"
  EXPERIMENTAL_API_LEVEL["var-handles"] = "28"

  if BUILD_MODE == "jvm":
    # No desugaring on jvm because it supports the latest functionality.
    use_desugar = False
    # Do not attempt to build src-art directories on jvm,
    # since it would fail without libcore.
    HAS_SRC_ART = False

  # Set API level for smali and d8.
  if not api_level:
    api_level = EXPERIMENTAL_API_LEVEL[experimental]

  # Add API level arguments to smali and dx
  SMALI_ARGS.extend(["--api", str(api_level)])
  D8_FLAGS.extend(["--min-api", str(api_level)])

  def run(executable: pathlib.Path, args: List[str]):
    assert isinstance(executable, pathlib.Path), executable
    cmd: List[Union[pathlib.Path, str]] = []
    if executable.suffix == ".sh":
      cmd += ["/bin/bash"]
    cmd += [executable]
    cmd += args
    env = ctx.bash_env
    env.update({k: v for k, v in os.environ.items() if k.startswith("RBE_")})
    for i, arg in enumerate(cmd):
      if isinstance(arg, pathlib.Path):
        assert arg.absolute(), arg
        cmd[i] = relpath(arg, ctx.test_dir)
    p = subprocess.run(cmd,
                       encoding=sys.stdout.encoding,
                       cwd=ctx.test_dir,
                       env=ctx.bash_env,
                       stderr=subprocess.STDOUT,
                       stdout=subprocess.PIPE)
    if p.returncode != 0:
      raise Exception("Command failed with exit code {}\n$ {}\n{}".format(
                      p.returncode, " ".join(map(str, cmd)), p.stdout))
    return p


  # Helper functions to execute tools.
  soong_zip = functools.partial(run, ctx.soong_zip)
  zipalign = functools.partial(run, ctx.zipalign)
  javac = functools.partial(run, ctx.javac)
  jasmin = functools.partial(run, ctx.jasmin)
  smali = functools.partial(run, ctx.smali)
  d8 = functools.partial(run, ctx.d8)
  hiddenapi = functools.partial(run, ctx.hiddenapi)

  if "RBE_server_address" in os.environ:
    version = match(r"Version: (\d*)\.(\d*)\.(\d*)", run(RBE_rewrapper, ["--version"]).stdout)
    assert version, "Could not parse RBE version"
    assert tuple(map(int, version.groups())) >= (0, 76, 0), "Please update " + RBE_rewrapper

    def rbe_wrap(args, inputs: Set[pathlib.Path]=None):
      with NamedTemporaryFile(mode="w+t") as input_list:
        inputs = inputs or set()
        inputs.update(arg for arg in args if isinstance(arg, pathlib.Path))
        for i, arg in enumerate(args):
          if isinstance(arg, str):
            for part in arg.split(":"):
              if (ctx.test_dir / part).exists():
                inputs.add(Path(ctx.test_dir / part))
        input_list.writelines([relpath(i, RBE_exec_root)+"\n" for i in inputs])
        input_list.flush()
        return run(RBE_rewrapper, [
          "--platform=" + os.environ["RBE_platform"],
          "--input_list_paths=" + input_list.name,
        ] + args)

    if USE_RBE_FOR_JAVAC > (hash(TEST_NAME) % 100):  # Use for given percentage of tests.
      def javac(args):
        output = relpath(Path(args[args.index("-d") + 1]), RBE_exec_root)
        return rbe_wrap(["--output_directories", output, ctx.javac] + args)

    if USE_RBE_FOR_D8 > (hash(TEST_NAME) % 100):  # Use for given percentage of tests.
      def d8(args):
        inputs = set([ctx.d8.parent.parent / "framework/d8.jar"])
        output = relpath(Path(args[args.index("--output") + 1]), RBE_exec_root)
        return rbe_wrap([
          "--output_files" if output.endswith(".jar") else "--output_directories", output,
          "--toolchain_inputs=prebuilts/jdk/jdk11/linux-x86/bin/java",
          ctx.d8] + args, inputs)

  # If wrapper script exists, use it instead of the default javac.
  javac_wrapper = Path("javac_wrapper.sh")
  if javac_wrapper.exists():
    javac = functools.partial(run, javac_wrapper)

  def zip(zip_target: Path, *files: Path):
    zip_args = ["-o", zip_target, "-C", zip_target.parent]
    if zip_compression_method == "store":
      zip_args.extend(["-L", "0"])
    for f in files:
      zip_args.extend(["-f", f])
    soong_zip(zip_args)

    if zip_align_bytes:
      # zipalign does not operate in-place, so write results to a temp file.
      with TemporaryDirectory() as tmp_dir:
        tmp_file = Path(tmp_dir) / "aligned.zip"
        zipalign(["-f", str(zip_align_bytes), zip_target, tmp_file])
        # replace original zip target with our temp file.
        tmp_file.rename(zip_target)


  def make_jasmin(out_directory: Path, jasmin_sources: List[Path]):
    out_directory.mkdir()
    jasmin(["-d", out_directory] + sorted(jasmin_sources))


  # Like regular javac but may include libcore on the bootclasspath.
  def javac_with_bootclasspath(args):
    flags = JAVAC_ARGS + ["-encoding", "utf8"]
    if BUILD_MODE != "jvm":
      flags.extend(["-bootclasspath", ctx.bootclasspath])
    javac(flags + args)


  # Make a "dex" file given a directory of classes. This will be
  # packaged in a jar file.
  def make_dex(name: str):
    d8_inputs = sorted(Path(name).glob("**/*.class"))
    d8_output = Path(name + ".jar")
    dex_output = Path(name + ".dex")
    if use_desugar:
      flags = ["--lib", ctx.bootclasspath]
    else:
      flags = ["--no-desugaring"]
    assert d8_inputs
    d8(D8_FLAGS + flags + ["--output", d8_output] + d8_inputs)

    # D8 outputs to JAR files today rather than DEX files as DX used
    # to. To compensate, we extract the DEX from d8's output to meet the
    # expectations of make_dex callers.
    with TemporaryDirectory() as tmp_dir:
      zipfile.ZipFile(d8_output, "r").extractall(tmp_dir)
      (Path(tmp_dir) / "classes.dex").rename(dex_output)

  # Merge all the dex files.
  # Skip non-existing files, but at least 1 file must exist.
  def make_dexmerge(*dex_files_to_merge: Path):
    # Dex file that acts as the destination.
    dst_file = dex_files_to_merge[0]

    # Skip any non-existing files.
    src_files = [f for f in dex_files_to_merge if f.exists()]

    # NB: We merge even if there is just single input.
    # It is useful to normalize non-deterministic smali output.

    tmp_dir = ctx.test_dir / "dexmerge"
    tmp_dir.mkdir()
    d8(["--min-api", api_level, "--output", tmp_dir] + src_files)
    assert not (tmp_dir / "classes2.dex").exists()
    for src_file in src_files:
      src_file.unlink()
    (tmp_dir / "classes.dex").rename(dst_file)
    tmp_dir.rmdir()


  def make_hiddenapi(*dex_files: Path):
    args: List[Union[str, Path]] = ["encode"]
    for dex_file in dex_files:
      args.extend(["--input-dex=" + str(dex_file), "--output-dex=" + str(dex_file)])
    args.append("--api-flags=hiddenapi-flags.csv")
    args.append("--no-force-assign-all")
    hiddenapi(args)


  if Path("classes.dex").exists():
    zip(Path(TEST_NAME + ".jar"), Path("classes.dex"))
    return

  if Path("classes.dm").exists():
    zip(Path(TEST_NAME + ".jar"), Path("classes.dm"))
    return


  def has_multidex():
    return HAS_SRC_MULTIDEX or HAS_JASMIN_MULTIDEX or HAS_SMALI_MULTIDEX


  def add_to_cp_args(old_cp_args, path):
    if len(old_cp_args) == 0:
      return ["-cp", path]
    else:
      return ["-cp", old_cp_args[1] + ":" + path]


  src_tmp_all = []

  if HAS_JASMIN:
    make_jasmin(Path("jasmin_classes"),
                sorted(Path("jasmin").glob("**/*.j")))
    src_tmp_all = add_to_cp_args(src_tmp_all, "jasmin_classes")

  if HAS_JASMIN_MULTIDEX:
    make_jasmin(Path("jasmin_classes2"),
                sorted(Path("jasmin-multidex").glob("**/*.j")))
    src_tmp_all = add_to_cp_args(src_tmp_all, "jasmin_classes2")

  if HAS_SRC and (HAS_SRC_MULTIDEX or HAS_SRC_AOTEX or HAS_SRC_BCPEX or
                  HAS_SRC_EX or HAS_SRC_ART or HAS_SRC2 or HAS_SRC_EX2):
    # To allow circular references, compile src/, src-multidex/, src-aotex/,
    # src-bcpex/, src-ex/ together and pass the output as class path argument.
    # Replacement sources in src-art/, src2/ and src-ex2/ can replace symbols
    # used by the other src-* sources we compile here but everything needed to
    # compile the other src-* sources should be present in src/ (and jasmin*/).
    Path("classes-tmp-all").mkdir()
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes-tmp-all")] +
                             sorted(Path("src").glob("**/*.java")) +
                             sorted(Path("src-multidex").glob("**/*.java")) +
                             sorted(Path("src-aotex").glob("**/*.java")) +
                             sorted(Path("src-bcpex").glob("**/*.java")) +
                             sorted(Path("src-ex").glob("**/*.java")))
    src_tmp_all = add_to_cp_args(src_tmp_all, "classes-tmp-all")

  if HAS_SRC_AOTEX:
    Path("classes-aotex").mkdir()
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes-aotex")] +
                             sorted(Path("src-aotex").glob("**/*.java")))
    if NEED_DEX:
      make_dex("classes-aotex")
      # rename it so it shows up as "classes.dex" in the zip file.
      Path("classes-aotex.dex").rename(Path("classes.dex"))
      zip(Path(TEST_NAME + "-aotex.jar"), Path("classes.dex"))

  if HAS_SRC_BCPEX:
    Path("classes-bcpex").mkdir()
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes-bcpex")] +
                             sorted(Path("src-bcpex").glob("**/*.java")))
    if NEED_DEX:
      make_dex("classes-bcpex")
      # rename it so it shows up as "classes.dex" in the zip file.
      Path("classes-bcpex.dex").rename(Path("classes.dex"))
      zip(Path(TEST_NAME + "-bcpex.jar"), Path("classes.dex"))

  if HAS_SRC:
    Path("classes").mkdir(exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes")] +
                             sorted(Path("src").glob("**/*.java")))

  if HAS_SRC_ART:
    Path("classes").mkdir(exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes")] +
                             sorted(Path("src-art").glob("**/*.java")))

  if HAS_SRC_MULTIDEX:
    Path("classes2").mkdir()
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes2")] +
                             sorted(Path("src-multidex").glob("**/*.java")))
    if NEED_DEX:
      make_dex("classes2")

  if HAS_SRC2:
    Path("classes").mkdir(exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes")] +
                             sorted(Path("src2").glob("**/*.java")))

  # If the classes directory is not-empty, package classes in a DEX file.
  # NB: some tests provide classes rather than java files.
  if any(Path("classes").glob("*")):
    if NEED_DEX:
      make_dex("classes")

  if HAS_JASMIN:
    # Compile Jasmin classes as if they were part of the classes.dex file.
    if NEED_DEX:
      make_dex("jasmin_classes")
      make_dexmerge(Path("classes.dex"), Path("jasmin_classes.dex"))
    else:
      # Move jasmin classes into classes directory so that they are picked up
      # with -cp classes.
      Path("classes").mkdir(exist_ok=True)
      copytree(Path("jasmin_classes"), Path("classes"), dirs_exist_ok=True)

  if HAS_SMALI and NEED_DEX:
    # Compile Smali classes
    smali_output = Path("smali_classes.dex")
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", smali_output] + sorted(Path("smali").glob("**/*.smali")))
    assert smali_output.exists()
    # Merge smali files into classes.dex,
    # this takes priority over any jasmin files.
    make_dexmerge(Path("classes.dex"), Path("smali_classes.dex"))

  # Compile Jasmin classes in jasmin-multidex as if they were part of
  # the classes2.jar
  if HAS_JASMIN_MULTIDEX:
    if NEED_DEX:
      make_dex("jasmin_classes2")
      make_dexmerge(Path("classes2.dex"), Path("jasmin_classes2.dex"))
    else:
      # Move jasmin classes into classes2 directory so that
      # they are picked up with -cp classes2.
      Path("classes2").mkdir()
      copytree(Path("jasmin_classes2"), Path("classes2"), dirs_exist_ok=True)
      rmtree(Path("jasmin_classes2"))

  if HAS_SMALI_MULTIDEX and NEED_DEX:
    # Compile Smali classes
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", "smali_classes2.dex"] + sorted(Path("smali-multidex").glob("**/*.smali")))

    # Merge smali_classes2.dex into classes2.dex
    make_dexmerge(Path("classes2.dex"), Path("smali_classes2.dex"))

  if HAS_SRC_EX:
    Path("classes-ex").mkdir()
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes-ex")] +
                             sorted(Path("src-ex").glob("**/*.java")))

  if HAS_SRC_EX2:
    Path("classes-ex").mkdir(exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", Path("classes-ex")] +
                             sorted(Path("src-ex2").glob("**/*.java")))

  if Path("classes-ex").exists() and NEED_DEX:
    make_dex("classes-ex")

  if HAS_SMALI_EX and NEED_DEX:
    # Compile Smali classes
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", "smali_classes-ex.dex"] + sorted(Path("smali-ex").glob("**/*.smali")))
    assert Path("smali_classes-ex.dex").exists()
    # Merge smali files into classes-ex.dex.
    make_dexmerge(Path("classes-ex.dex"), Path("smali_classes-ex.dex"))

  if Path("classes-ex.dex").exists():
    # Apply hiddenapi on the dex files if the test has API list file(s).
    if use_hiddenapi and HAS_HIDDENAPI_SPEC:
      make_hiddenapi(Path("classes-ex.dex"))

    # quick shuffle so that the stored name is "classes.dex"
    Path("classes.dex").rename(Path("classes-1.dex"))
    Path("classes-ex.dex").rename(Path("classes.dex"))
    zip(Path(TEST_NAME + "-ex.jar"), Path("classes.dex"))
    Path("classes.dex").rename(Path("classes-ex.dex"))
    Path("classes-1.dex").rename(Path("classes.dex"))

  # Apply hiddenapi on the dex files if the test has API list file(s).
  if NEED_DEX and use_hiddenapi and HAS_HIDDENAPI_SPEC:
    if has_multidex():
      make_hiddenapi(Path("classes.dex"), Path("classes2.dex"))
    else:
      make_hiddenapi(Path("classes.dex"))

  # Create a single dex jar with two dex files for multidex.
  if NEED_DEX:
    if Path("classes2.dex").exists():
      zip(Path(TEST_NAME + ".jar"), Path("classes.dex"), Path("classes2.dex"))
    else:
      zip(Path(TEST_NAME + ".jar"), Path("classes.dex"))


def build_test(ctx: BuildTestContext) -> None:
  """Run the build script for single run-test"""

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
  lock_path = Path(environ["TMPDIR"]) / ("art-test-run-test-build-py-" + mode)
  lock_file = open(lock_path, "w")
  try:
    lockf(lock_file, LOCK_EX | LOCK_NB)
    return True  # We are the only instance of this script in the build system.
  except BlockingIOError:
    return False  # Some other instance is already running.


def main() -> None:
  parser = ArgumentParser(description=__doc__)
  parser.add_argument("--out", type=Path, help="Final zip file")
  parser.add_argument("--mode", choices=["host", "jvm", "target"])
  parser.add_argument("--bootclasspath", type=Path)
  parser.add_argument("--d8", type=Path)
  parser.add_argument("--hiddenapi", type=Path)
  parser.add_argument("--jasmin", type=Path)
  parser.add_argument("--smali", type=Path)
  parser.add_argument("--soong_zip", type=Path)
  parser.add_argument("--zipalign", type=Path)
  parser.add_argument("srcs", nargs="+", type=Path)
  args = parser.parse_args()

  android_build_top = Path(getcwd()).absolute()
  ziproot = args.out.absolute().parent / "zip"
  srcdirs = set(s.parents[-4].absolute() for s in args.srcs)

  # Initialize the test objects.
  # We need to do this before we change the working directory below.
  tests: List[BuildTestContext] = []
  for srcdir in srcdirs:
    dstdir = ziproot / args.mode / srcdir.name
    copytree(srcdir, dstdir)
    tests.append(BuildTestContext(args, android_build_top, dstdir))

  # We can not change the working directory per each thread since they all run in parallel.
  # Create invalid read-only directory to catch accidental use of current working directory.
  with TemporaryDirectory("-do-not-use-cwd") as invalid_tmpdir:
    os.chdir(invalid_tmpdir)
    os.chmod(invalid_tmpdir, 0)
    with ThreadPoolExecutor(cpu_count() if use_multiprocessing(args.mode) else 1) as pool:
      jobs = {}
      for ctx in tests:
        jobs[ctx.test_name] = pool.submit(build_test, ctx)
      for test_name, job in jobs.items():
        try:
          job.result()
        except Exception as e:
          raise Exception("Failed to build " + test_name) from e

  # Create the final zip file which contains the content of the temporary directory.
  proc = run([android_build_top / args.soong_zip, "-o", android_build_top / args.out,
              "-C", ziproot, "-D", ziproot], check=True)


if __name__ == "__main__":
  main()
