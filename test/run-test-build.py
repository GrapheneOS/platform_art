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

import argparse, os, shutil, subprocess, glob, re, json, multiprocessing, pathlib
import art_build_rules
from importlib.machinery import SourceFileLoader

ZIP = "prebuilts/build-tools/linux-x86/bin/soong_zip"
BUILDFAILURES = json.loads(open(os.path.join("art", "test", "buildfailures.json"), "rt").read())

def copy_sources(args, tmp, mode, srcdir):
  """Copy test files from Android tree into the build sandbox and return its path."""

  join = os.path.join
  test = os.path.basename(srcdir)
  dstdir = join(tmp, mode, test)

  # Don't build tests that are disabled since they might not compile (e.g. on jvm).
  def is_buildfailure(kf):
    return test in kf.get("tests", []) and mode == kf.get("variant") and not kf.get("env_vars")
  if any(is_buildfailure(kf) for kf in BUILDFAILURES):
    return None

  # Copy all source files to the temporary directory.
  shutil.copytree(srcdir, dstdir)

  # Copy the default scripts if the test does not have a custom ones.
  for name in ["build.py", "run", "check"]:
    src, dst = f"art/test/etc/default-{name}", join(dstdir, name)
    if os.path.exists(dst):
      shutil.copy2(src, dstdir)  # Copy default script next to the custom script.
    else:
      shutil.copy2(src, dst)  # Use just the default script.
    os.chmod(dst, 0o755)

  return dstdir

def build_test(args, mode, build_top, sbox, dstdir):
  """Run the build script for single run-test"""

  join = os.path.join
  java_home = os.environ.get("JAVA_HOME")
  tools_dir = os.path.abspath(join(os.path.dirname(__file__), "../../../out/bin"))
  test_name = os.path.basename(dstdir)
  env = dict(os.environ)
  env.update({
    "BUILD_MODE": mode,
    "ANDROID_BUILD_TOP": build_top,
    "SBOX_PATH":   sbox,
    "ART_TEST_RUN_TEST_BOOTCLASSPATH": join(build_top, args.bootclasspath),
    "TEST_NAME":   test_name,
    "SOONG_ZIP":   join(build_top, "prebuilts/build-tools/linux-x86/bin/soong_zip"),
    "ZIPALIGN":    join(build_top, "prebuilts/build-tools/linux-x86/bin/zipalign"),
    "JAVA":        join(java_home, "bin/java"),
    "JAVAC":       join(java_home, "bin/javac"),
    "JAVAC_ARGS":  "-g -Xlint:-options -source 1.8 -target 1.8",
    "D8":          join(tools_dir, "d8"),
    "HIDDENAPI":   join(tools_dir, "hiddenapi"),
    "JASMIN":      join(tools_dir, "jasmin"),
    "SMALI":       join(tools_dir, "smali"),
    "NEED_DEX":    {"host": "true", "target": "true", "jvm": "false"}[mode],
  })

  generate_sources = join(dstdir, "generate-sources")
  if os.path.exists(generate_sources):
    proc = subprocess.run([generate_sources, "--" + mode],
                          cwd=dstdir,
                          env=env,
                          encoding=os.sys.stdout.encoding,
                          stderr=subprocess.STDOUT,
                          stdout=subprocess.PIPE)
    if proc.returncode:
      raise Exception("Failed to generate sources for " + test_name + ":\n" + proc.stdout)

  os.chdir(dstdir)
  for name, value in env.items():
    os.environ[name] = str(value)
  SourceFileLoader("build_" + test_name, join(dstdir, "build.py")).load_module()

def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--out", help="Path of the generated ZIP file with the build data")
  parser.add_argument('--mode', choices=['host', 'jvm', 'target'])
  parser.add_argument("--shard", help="Identifies subset of tests to build (00..99)")
  parser.add_argument("--bootclasspath", help="JAR files used for javac compilation")
  args = parser.parse_args()

  build_top = os.getcwd()
  sbox = pathlib.Path(__file__).absolute().parent.parent.parent.parent.parent
  assert sbox.parent.name == "sbox" and len(sbox.name) == 40

  ziproot = os.path.join(sbox, "zip")
  srcdirs = sorted(glob.glob(os.path.join("art", "test", "*")))
  srcdirs = filter(lambda srcdir: re.match(".*/\d*{}-.*".format(args.shard), srcdir), srcdirs)
  dstdirs = [copy_sources(args, ziproot, args.mode, srcdir) for srcdir in srcdirs]
  dstdirs = filter(lambda dstdir: dstdir, dstdirs)  # Remove None (skipped tests).
  # Use multiprocess (i.e. forking) since tests modify their current working directory.
  with multiprocessing.Pool() as pool:
    jobs = [(d, pool.apply_async(build_test, (args, args.mode, build_top, sbox, d))) for d in dstdirs]
    for dstdir, job in jobs:
      try:
        job.get()
      except Exception as e:
        raise Exception("Failed to build " + os.path.basename(dstdir)) from e.__cause__

  # Create the final zip file which contains the content of the temporary directory.
  proc = subprocess.run([ZIP, "-o", args.out, "-C", ziproot, "-D", ziproot], check=True)

if __name__ == "__main__":
  main()
