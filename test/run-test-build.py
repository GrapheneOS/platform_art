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

import argparse, os, tempfile, shutil, subprocess, glob, textwrap, re, json, concurrent.futures

ZIP = "prebuilts/build-tools/linux-x86/bin/soong_zip"
KNOWNFAILURES = json.loads(open(os.path.join("art", "test", "knownfailures.json"), "rt").read())

def build(args, tmp, mode, srcdir):
  join = os.path.join
  test = os.path.basename(srcdir)
  dstdir = join(tmp, mode, test)

  # Don't build tests that are disabled since they might not compile (e.g. on jvm).
  def is_knownfailure(kf):
    return test in kf.get("tests", []) and mode == kf.get("variant") and not kf.get("env_vars")
  if any(is_knownfailure(kf) for kf in KNOWNFAILURES):
    return None, 0  # (stdout, exitcode)

  # Copy all source files to the temporary directory.
  shutil.copytree(srcdir, dstdir)

  # Copy the default scripts if the test does not have a custom ones.
  for name in ["build", "run", "check"]:
    src, dst = f"art/test/etc/default-{name}", join(dstdir, name)
    if os.path.exists(dst):
      shutil.copy2(src, dstdir)  # Copy default script next to the custom script.
    else:
      shutil.copy2(src, dst)  # Use just the default script.
    os.chmod(dst, 0o755)
  os.sync()  # Ensure the chmod changes are applied and file descriptor is closed.

  # Execute the build script.
  build_top = os.getcwd()
  java_home = os.environ.get("JAVA_HOME")
  tools_dir = os.path.abspath(join(os.path.dirname(__file__), "../../../out/bin"))
  env = {
    "PATH": os.environ.get("PATH"),
    "ANDROID_BUILD_TOP": build_top,
    "ART_TEST_RUN_TEST_BOOTCLASSPATH": join(build_top, args.bootclasspath),
    "TEST_NAME":   test,
    "SOONG_ZIP":   join(build_top, "prebuilts/build-tools/linux-x86/bin/soong_zip"),
    "ZIPALIGN":    join(build_top, "prebuilts/build-tools/linux-x86/bin/zipalign"),
    "JAVA":        join(java_home, "bin/java"),
    "JAVAC":       join(java_home, "bin/javac") + " -g -Xlint:-options -source 1.8 -target 1.8",
    "D8":          join(tools_dir, "d8"),
    "HIDDENAPI":   join(tools_dir, "hiddenapi"),
    "JASMIN":      join(tools_dir, "jasmin"),
    "SMALI":       join(tools_dir, "smali"),
    "NEED_DEX":    {"host": "true", "target": "true", "jvm": "false"}[mode],
    "USE_DESUGAR": "true",
  }
  proc = subprocess.run([join(dstdir, "build"), "--" + mode],
                        cwd=dstdir,
                        env=env,
                        encoding=os.sys.stdout.encoding,
                        stderr=subprocess.STDOUT,
                        stdout=subprocess.PIPE)
  return proc.stdout, proc.returncode

def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--out", help="Path of the generated ZIP file with the build data")
  parser.add_argument('--mode', choices=['host', 'jvm', 'target'])
  parser.add_argument("--shard", help="Identifies subset of tests to build (00..99)")
  parser.add_argument("--bootclasspath", help="JAR files used for javac compilation")
  args = parser.parse_args()

  with tempfile.TemporaryDirectory(prefix=os.path.basename(__file__)) as tmp:
    srcdirs = sorted(glob.glob(os.path.join("art", "test", "*")))
    srcdirs = filter(lambda srcdir: re.match(".*/\d*{}-.*".format(args.shard), srcdir), srcdirs)
    with concurrent.futures.ThreadPoolExecutor() as pool:
      for stdout, exitcode in pool.map(lambda srcdir: build(args, tmp, args.mode, srcdir), srcdirs):
        if stdout:
          print(stdout.strip())
        assert(exitcode == 0) # Build failed.

    # Create the final zip file which contains the content of the temporary directory.
    proc = subprocess.run([ZIP, "-o", args.out, "-C", tmp, "-D", tmp])
    assert(proc.returncode == 0) # Zip failed.

if __name__ == "__main__":
  main()
