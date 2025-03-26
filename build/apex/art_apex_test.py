#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (C) 2019 The Android Open Source Project
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
#

import argparse
import fnmatch
import logging
import os
import os.path
import shutil
import subprocess
import sys

logging.basicConfig(format='%(message)s')

# Flavors of ART APEX package.
FLAVOR_RELEASE = 'release'
FLAVOR_DEBUG = 'debug'
FLAVOR_TESTING = 'testing'
FLAVOR_AUTO = 'auto'
FLAVORS_ALL = [FLAVOR_RELEASE, FLAVOR_DEBUG, FLAVOR_TESTING, FLAVOR_AUTO]

# Bitness options for APEX package
BITNESS_32 = '32'
BITNESS_64 = '64'
BITNESS_MULTILIB = 'multilib'
BITNESS_AUTO = 'auto'
BITNESS_ALL = [BITNESS_32, BITNESS_64, BITNESS_MULTILIB, BITNESS_AUTO]

# Architectures supported by APEX packages.
ARCHS_32 = ['arm', 'x86']
ARCHS_64 = ['arm64', 'riscv64', 'x86_64']

# Multilib options
MULTILIB_32 = '32'
MULTILIB_64 = '64'
MULTILIB_BOTH = 'both'
MULTILIB_FIRST = 'first'

# Directory containing ART tests within an ART APEX (if the package includes
# any). ART test executables are installed in `bin/art/<arch>`. Segregating
# tests by architecture is useful on devices supporting more than one
# architecture, as it permits testing all of them using a single ART APEX
# package.
ART_TEST_DIR = 'bin/art'


# Test if a given variable is set to a string "true".
def isEnvTrue(var):
  return var in os.environ and os.environ[var] == 'true'


def extract_apex(apex_path, deapexer_path, debugfs_path, fsckerofs_path,
                 tmpdir):
  _, apex_name = os.path.split(apex_path)
  extract_path = os.path.join(tmpdir, apex_name)
  if os.path.exists(extract_path):
    shutil.rmtree(extract_path)
  subprocess.check_call([deapexer_path, '--debugfs', debugfs_path,
                         '--fsckerofs', fsckerofs_path,
                         'extract', apex_path, extract_path],
                        stdout=subprocess.DEVNULL)
  return extract_path


class FSObject:
  def __init__(self, name, is_dir, is_exec, is_symlink, size):
    self.name = name
    self.is_dir = is_dir
    self.is_exec = is_exec
    self.is_symlink = is_symlink
    self.size = size

  def __str__(self):
    return '%s(dir=%r,exec=%r,symlink=%r,size=%d)' \
             % (self.name, self.is_dir, self.is_exec, self.is_symlink, self.size)

  def type_descr(self):
    if self.is_dir:
      return 'directory'
    if self.is_symlink:
      return 'symlink'
    return 'file'


class TargetApexProvider:
  def __init__(self, apex):
    self._folder_cache = {}
    self._apex = apex

  def get(self, path):
    apex_dir, name = os.path.split(path)
    if not apex_dir:
      apex_dir = '.'
    apex_map = self.read_dir(apex_dir)
    return apex_map[name] if name in apex_map else None

  def read_dir(self, apex_dir):
    if apex_dir in self._folder_cache:
      return self._folder_cache[apex_dir]
    apex_map = {}
    dirname = os.path.join(self._apex, apex_dir)
    if os.path.exists(dirname):
      for basename in os.listdir(dirname):
        filepath = os.path.join(dirname, basename)
        is_dir = os.path.isdir(filepath)
        is_exec = os.access(filepath, os.X_OK)
        is_symlink = os.path.islink(filepath)
        if is_symlink:
          # Report the length of the symlink's target's path as file size, like `ls`.
          size = len(os.readlink(filepath))
        else:
          size = os.path.getsize(filepath)
        apex_map[basename] = FSObject(basename, is_dir, is_exec, is_symlink, size)
    self._folder_cache[apex_dir] = apex_map
    return apex_map


# DO NOT USE DIRECTLY! This is an "abstract" base class.
class Checker:
  def __init__(self, provider):
    self._provider = provider
    self._errors = 0
    self._expected_file_globs = set()

  def fail(self, msg, *fail_args):
    self._errors += 1
    logging.error(msg, *fail_args)

  def error_count(self):
    return self._errors

  def reset_errors(self):
    self._errors = 0

  def is_file(self, path):
    fs_object = self._provider.get(path)
    if fs_object is None:
      return False, 'Could not find %s'
    if fs_object.is_dir:
      return False, '%s is a directory'
    if fs_object.is_symlink:
      return False, '%s is a symlink'
    return True, ''

  def is_dir(self, path):
    fs_object = self._provider.get(path)
    if fs_object is None:
      return False, 'Could not find %s'
    if not fs_object.is_dir:
      return False, '%s is not a directory'
    return True, ''

  def check_file(self, path):
    ok, msg = self.is_file(path)
    if not ok:
      self.fail(msg, path)
    self._expected_file_globs.add(path)
    return ok

  def check_dir(self, path):
    ok, msg = self.is_dir(path)
    if not ok:
      self.fail(msg, path)
    self._expected_file_globs.add(path)
    return ok

  def check_optional_file(self, path):
    if not self._provider.get(path):
      return True
    return self.check_file(path)

  def check_executable(self, filename):
    path = 'bin/%s' % filename
    if not self.check_file(path):
      return
    if not self._provider.get(path).is_exec:
      self.fail('%s is not executable', path)

  def check_executable_symlink(self, filename):
    path = 'bin/%s' % filename
    fs_object = self._provider.get(path)
    if fs_object is None:
      self.fail('Could not find %s', path)
      return
    if fs_object.is_dir:
      self.fail('%s is a directory', path)
      return
    if not fs_object.is_symlink:
      self.fail('%s is not a symlink', path)
    self._expected_file_globs.add(path)

  def arch_dirs_for_path(self, path, multilib=None):
    # Look for target-specific subdirectories for the given directory path.
    # This is needed because the list of build targets is not propagated
    # to this script.
    #
    # TODO(b/123602136): Pass build target information to this script and fix
    # all places where this function in used (or similar workarounds).
    dirs = []
    for archs_per_bitness in self.possible_archs_per_bitness(multilib):
      found_dir = False
      for arch in archs_per_bitness:
        dir = '%s/%s' % (path, arch)
        found, _ = self.is_dir(dir)
        if found:
          found_dir = True
          dirs.append(dir)
      # At least one arch directory per bitness must exist.
      if not found_dir:
        self.fail('Arch directories missing in %s - expected at least one of %s',
                  path, ', '.join(archs_per_bitness))
    return dirs

  def check_art_test_executable(self, filename, multilib=None):
    for dir in self.arch_dirs_for_path(ART_TEST_DIR, multilib):
      test_path = '%s/%s' % (dir, filename)
      self._expected_file_globs.add(test_path)
      file_obj = self._provider.get(test_path)
      if not file_obj:
        self.fail('ART test binary missing: %s', test_path)
      elif not file_obj.is_exec:
        self.fail('%s is not executable', test_path)

  def check_art_test_data(self, filename):
    for dir in self.arch_dirs_for_path(ART_TEST_DIR):
      if not self.check_file('%s/%s' % (dir, filename)):
        return

  def check_single_library(self, filename):
    lib_path = 'lib/%s' % filename
    lib64_path = 'lib64/%s' % filename
    lib_is_file, _ = self.is_file(lib_path)
    if lib_is_file:
      self._expected_file_globs.add(lib_path)
    lib64_is_file, _ = self.is_file(lib64_path)
    if lib64_is_file:
      self._expected_file_globs.add(lib64_path)
    if not lib_is_file and not lib64_is_file:
      self.fail('Library missing: %s', filename)

  def check_java_library(self, basename):
    return self.check_file('javalib/%s.jar' % basename)

  def ignore_path(self, path_glob):
    self._expected_file_globs.add(path_glob)

  def check_optional_art_test_executable(self, filename):
    for archs_per_bitness in self.possible_archs_per_bitness():
      for arch in archs_per_bitness:
        self.ignore_path('%s/%s/%s' % (ART_TEST_DIR, arch, filename))

  def check_no_superfluous_files(self):
    def recurse(dir_path):
      paths = []
      for name, fsobj in sorted(self._provider.read_dir(dir_path).items(), key=lambda p: p[0]):
        if name in ('.', '..'):
          continue
        path = os.path.join(dir_path, name)
        paths.append(path)
        if fsobj.is_dir:
          recurse(path)
      for path_glob in self._expected_file_globs:
        paths = [path for path in paths if not fnmatch.fnmatch(path, path_glob)]
      for unexpected_path in paths:
        fs_object = self._provider.get(unexpected_path)
        self.fail('Unexpected %s: %s', fs_object.type_descr(), unexpected_path)
    recurse('')

  # Just here for docs purposes, even if it isn't good Python style.

  def check_symlinked_multilib_executable(self, filename):
    """Check bin/filename32, and/or bin/filename64, with symlink bin/filename."""
    raise NotImplementedError

  def check_symlinked_first_executable(self, filename):
    """Check bin/filename32, and/or bin/filename64, with symlink bin/filename."""
    raise NotImplementedError

  def check_native_library(self, basename):
    """Check lib/basename.so, and/or lib64/basename.so."""
    raise NotImplementedError

  def check_optional_native_library(self, basename_glob):
    """Allow lib/basename.so and/or lib64/basename.so to exist."""
    raise NotImplementedError

  def check_prefer64_library(self, basename):
    """Check lib64/basename.so, or lib/basename.so on 32 bit only."""
    raise NotImplementedError

  def possible_archs_per_bitness(self, multilib=None):
    """Returns a list of lists of possible architectures per bitness."""
    raise NotImplementedError

class Arch32Checker(Checker):
  def __init__(self, provider):
    super().__init__(provider)
    self.lib_dirs = ['lib']

  def check_symlinked_multilib_executable(self, filename):
    self.check_executable('%s32' % filename)
    self.check_executable_symlink(filename)

  def check_symlinked_first_executable(self, filename):
    self.check_executable('%s32' % filename)
    self.check_executable_symlink(filename)

  def check_native_library(self, basename):
    # TODO: Use $TARGET_ARCH (e.g. check whether it is "arm" or "arm64") to improve
    # the precision of this test?
    self.check_file('lib/%s.so' % basename)

  def check_optional_native_library(self, basename_glob):
    self.ignore_path('lib/%s.so' % basename_glob)

  def check_prefer64_library(self, basename):
    self.check_native_library(basename)

  def possible_archs_per_bitness(self, multilib=None):
    return [ARCHS_32]

class Arch64Checker(Checker):
  def __init__(self, provider):
    super().__init__(provider)
    self.lib_dirs = ['lib64']

  def check_symlinked_multilib_executable(self, filename):
    self.check_executable('%s64' % filename)
    self.check_executable_symlink(filename)

  def check_symlinked_first_executable(self, filename):
    self.check_executable('%s64' % filename)
    self.check_executable_symlink(filename)

  def check_native_library(self, basename):
    # TODO: Use $TARGET_ARCH (e.g. check whether it is "arm" or "arm64") to improve
    # the precision of this test?
    self.check_file('lib64/%s.so' % basename)

  def check_optional_native_library(self, basename_glob):
    self.ignore_path('lib64/%s.so' % basename_glob)

  def check_prefer64_library(self, basename):
    self.check_native_library(basename)

  def possible_archs_per_bitness(self, multilib=None):
    return [ARCHS_64]


class MultilibChecker(Checker):
  def __init__(self, provider):
    super().__init__(provider)
    self.lib_dirs = ['lib', 'lib64']

  def check_symlinked_multilib_executable(self, filename):
    self.check_executable('%s32' % filename)
    self.check_executable('%s64' % filename)
    self.check_executable_symlink(filename)

  def check_symlinked_first_executable(self, filename):
    self.check_executable('%s64' % filename)
    self.check_executable_symlink(filename)

  def check_native_library(self, basename):
    # TODO: Use $TARGET_ARCH (e.g. check whether it is "arm" or "arm64") to improve
    # the precision of this test?
    self.check_file('lib/%s.so' % basename)
    self.check_file('lib64/%s.so' % basename)

  def check_optional_native_library(self, basename_glob):
    self.ignore_path('lib/%s.so' % basename_glob)
    self.ignore_path('lib64/%s.so' % basename_glob)

  def check_prefer64_library(self, basename):
    self.check_file('lib64/%s.so' % basename)

  def possible_archs_per_bitness(self, multilib=None):
    if multilib is None or multilib == MULTILIB_BOTH:
      return [ARCHS_32, ARCHS_64]
    if multilib == MULTILIB_FIRST or multilib == MULTILIB_64:
      return [ARCHS_64]
    elif multilib == MULTILIB_32:
      return [ARCHS_32]
    self.fail('Unrecognized multilib option "%s"', multilib)


class ReleaseChecker:
  def __init__(self, checker):
    self._checker = checker

  def __str__(self):
    return 'Release Checker'

  def run(self):
    # Check the root directory.
    self._checker.check_dir('bin')
    self._checker.check_dir('etc')
    self._checker.check_dir('javalib')
    for lib_dir in self._checker.lib_dirs:
      self._checker.check_dir(lib_dir)
    self._checker.check_file('apex_manifest.pb')

    # Check etc.
    self._checker.check_file('etc/boot-image.prof')
    self._checker.check_dir('etc/classpaths')
    self._checker.check_file('etc/classpaths/bootclasspath.pb')
    self._checker.check_file('etc/classpaths/systemserverclasspath.pb')
    self._checker.check_dir('etc/compatconfig')
    self._checker.check_file('etc/compatconfig/libcore-platform-compat-config.xml')
    self._checker.check_file('etc/init.rc')
    self._checker.check_file('etc/linker.config.pb')
    self._checker.check_file('etc/sdkinfo.pb')
    self._checker.check_file('etc/dirty-image-objects')

    # Check flagging files that don't get added in builds on master-art.
    # TODO(b/345713436): Make flags work on master-art.
    self._checker.check_optional_file('etc/aconfig_flags.pb')
    self._checker.check_optional_file('etc/flag.info')
    self._checker.check_optional_file('etc/flag.map')
    self._checker.check_optional_file('etc/flag.val')
    self._checker.check_optional_file('etc/package.map')

    # Check binaries for ART.
    self._checker.check_executable('art_boot')
    self._checker.check_executable('art_exec')
    self._checker.check_executable('artd')
    self._checker.check_executable('dexdump')
    self._checker.check_executable('dexlist')
    self._checker.check_executable('dexopt_chroot_setup')
    self._checker.check_executable('dexoptanalyzer')
    self._checker.check_executable('oatdump')
    self._checker.check_executable('odrefresh')
    self._checker.check_executable('profman')
    self._checker.check_symlinked_multilib_executable('dalvikvm')
    self._checker.check_symlinked_multilib_executable('dex2oat')

    # Check exported libraries for ART.
    self._checker.check_native_library('libdexfile')
    self._checker.check_native_library('libnativebridge')
    self._checker.check_native_library('libnativehelper')
    self._checker.check_native_library('libnativeloader')

    # Check internal libraries for ART.
    self._checker.check_native_library('libadbconnection')
    self._checker.check_native_library('libart')
    self._checker.check_native_library('libart-disassembler')
    self._checker.check_native_library('libartbase')
    self._checker.check_native_library('libartpalette')
    self._checker.check_native_library('libdt_fd_forward')
    self._checker.check_native_library('libopenjdkjvm')
    self._checker.check_native_library('libopenjdkjvmti')
    self._checker.check_native_library('libperfetto_hprof')
    self._checker.check_native_library('libprofile')
    self._checker.check_native_library('libsigchain')
    self._checker.check_prefer64_library('libartservice')
    self._checker.check_prefer64_library('libarttools')

    # Check internal Java libraries for ART.
    self._checker.check_java_library('service-art')
    self._checker.check_file('javalib/service-art.jar.prof')

    # Check exported native libraries for Managed Core Library.
    self._checker.check_native_library('libandroidio')

    # Check Java libraries for Managed Core Library.
    self._checker.check_java_library('apache-xml')
    self._checker.check_java_library('bouncycastle')
    self._checker.check_java_library('core-libart')
    self._checker.check_java_library('core-oj')
    self._checker.check_java_library('okhttp')
    if isEnvTrue('EMMA_INSTRUMENT_FRAMEWORK'):
      # In coverage builds jacoco is added to the list of ART apex jars.
      self._checker.check_java_library('jacocoagent')

    # Check internal native libraries for Managed Core Library.
    self._checker.check_native_library('libjavacore')
    self._checker.check_native_library('libopenjdk')

    # Check internal native library dependencies.
    #
    # Any internal dependency not listed here will cause a failure in
    # NoSuperfluousFilesChecker. Internal dependencies are generally just
    # implementation details, but in the release package we want to track them
    # because a) they add to the package size and the RAM usage (in particular
    # if the library is also present in /system or another APEX and hence might
    # get loaded twice through linker namespace separation), and b) we need to
    # catch invalid dependencies on /system or other APEXes that should go
    # through an exported library with stubs (b/128708192 tracks implementing a
    # better approach for that).
    self._checker.check_native_library('libbase')
    self._checker.check_native_library('libc++')
    self._checker.check_native_library('libdt_socket')
    self._checker.check_native_library('libexpat')
    self._checker.check_native_library('libjdwp')
    self._checker.check_native_library('liblz4')
    self._checker.check_native_library('liblzma')
    self._checker.check_native_library('libnpt')
    self._checker.check_native_library('libunwindstack')

    # Allow extra dependencies that appear in ASAN builds.
    self._checker.check_optional_native_library('libclang_rt.asan*')
    self._checker.check_optional_native_library('libclang_rt.hwasan*')
    self._checker.check_optional_native_library('libclang_rt.ubsan*')


class DebugChecker:
  def __init__(self, checker):
    self._checker = checker

  def __str__(self):
    return 'Debug Checker'

  def run(self):
    # Check binaries for ART.
    self._checker.check_executable('dexanalyze')
    self._checker.check_symlinked_multilib_executable('imgdiag')

    # Check debug binaries for ART.
    self._checker.check_executable('dexoptanalyzerd')
    self._checker.check_executable('oatdumpd')
    self._checker.check_executable('profmand')
    self._checker.check_symlinked_multilib_executable('dex2oatd')
    self._checker.check_symlinked_multilib_executable('imgdiagd')

    # Check exported libraries for ART.
    self._checker.check_native_library('libdexfiled')

    # Check internal libraries for ART.
    self._checker.check_native_library('libadbconnectiond')
    self._checker.check_native_library('libartbased')
    self._checker.check_native_library('libartd')
    self._checker.check_native_library('libartd-disassembler')
    self._checker.check_native_library('libopenjdkjvmd')
    self._checker.check_native_library('libopenjdkjvmtid')
    self._checker.check_native_library('libperfetto_hprofd')
    self._checker.check_native_library('libprofiled')
    self._checker.check_prefer64_library('libartserviced')

    # Check internal libraries for Managed Core Library.
    self._checker.check_native_library('libopenjdkd')

    # Check internal native library dependencies.
    #
    # Like in the release package, we check that we don't get other dependencies
    # besides those listed here. In this case the concern is not bloat, but
    # rather that we don't get behavioural differences between user (release)
    # and userdebug/eng builds, which could happen if the debug package has
    # duplicate library instances where releases don't. In other words, it's
    # uncontroversial to add debug-only dependencies, as long as they don't make
    # assumptions on having a single global state (ideally they should have
    # double_loadable:true, cf. go/double_loadable). Also, like in the release
    # package we need to look out for dependencies that should go through
    # exported library stubs (until b/128708192 is fixed).
    #
    # (There are currently no debug-only native libraries.)


class TestingChecker:
  def __init__(self, checker):
    self._checker = checker

  def __str__(self):
    return 'Testing Checker'

  def run(self):
    # Check test directories.
    self._checker.check_dir(ART_TEST_DIR)
    for arch_dir in self._checker.arch_dirs_for_path(ART_TEST_DIR):
      self._checker.check_dir(arch_dir)

    # Check ART test binaries.
    self._checker.check_art_test_executable('art_cmdline_tests')
    self._checker.check_art_test_executable('art_compiler_tests')
    self._checker.check_art_test_executable('art_dex2oat_tests')
    self._checker.check_art_test_executable('art_dexanalyze_tests')
    self._checker.check_art_test_executable('art_dexdump_tests')
    self._checker.check_art_test_executable('art_dexlist_tests')
    self._checker.check_art_test_executable('art_dexoptanalyzer_tests')
    self._checker.check_art_test_executable('art_disassembler_tests')
    self._checker.check_art_test_executable('art_imgdiag_tests')
    self._checker.check_art_test_executable('art_libartbase_tests')
    self._checker.check_art_test_executable('art_libdexfile_support_tests')
    self._checker.check_art_test_executable('art_libdexfile_tests')
    self._checker.check_art_test_executable('art_libprofile_tests')
    self._checker.check_art_test_executable('art_oatdump_tests')
    self._checker.check_art_test_executable('art_odrefresh_tests', MULTILIB_FIRST)
    self._checker.check_art_test_executable('art_profman_tests')
    self._checker.check_art_test_executable('art_runtime_tests')
    self._checker.check_art_test_executable('art_sigchain_tests')

    # Some libraries are in odd location (libarttest(d) and libtiagent(d)).
    # We intend to remove the whole testing apex, so just ignore those for now.
    self._checker.ignore_path('lib*/com.android.art')
    self._checker.ignore_path('lib*/com.android.art/lib*')

    # Check ART test tools.
    self._checker.check_executable('signal_dumper')

    # Check ART jar files which are needed for gtests.
    self._checker.check_art_test_data('art-gtest-jars-AbstractMethod.jar')
    self._checker.check_art_test_data('art-gtest-jars-ArrayClassWithUnresolvedComponent.dex')
    self._checker.check_art_test_data('art-gtest-jars-MyClassNatives.jar')
    self._checker.check_art_test_data('art-gtest-jars-Main.jar')
    self._checker.check_art_test_data('art-gtest-jars-ProtoCompare.jar')
    self._checker.check_art_test_data('art-gtest-jars-Transaction.jar')
    self._checker.check_art_test_data('art-gtest-jars-VerifierDepsMulti.dex')
    self._checker.check_art_test_data('art-gtest-jars-Nested.jar')
    self._checker.check_art_test_data('art-gtest-jars-MyClass.jar')
    self._checker.check_art_test_data('art-gtest-jars-ManyMethods.jar')
    self._checker.check_art_test_data('art-gtest-jars-GetMethodSignature.jar')
    self._checker.check_art_test_data('art-gtest-jars-Lookup.jar')
    self._checker.check_art_test_data('art-gtest-jars-Instrumentation.jar')
    self._checker.check_art_test_data('art-gtest-jars-MainUncompressedAligned.jar')
    self._checker.check_art_test_data('art-gtest-jars-ForClassLoaderD.jar')
    self._checker.check_art_test_data('art-gtest-jars-ForClassLoaderC.jar')
    self._checker.check_art_test_data('art-gtest-jars-ErroneousA.jar')
    self._checker.check_art_test_data('art-gtest-jars-HiddenApiSignatures.jar')
    self._checker.check_art_test_data('art-gtest-jars-ForClassLoaderB.jar')
    self._checker.check_art_test_data('art-gtest-jars-LinkageTest.dex')
    self._checker.check_art_test_data('art-gtest-jars-MethodTypes.jar')
    self._checker.check_art_test_data('art-gtest-jars-ErroneousInit.jar')
    self._checker.check_art_test_data('art-gtest-jars-VerifierDeps.dex')
    self._checker.check_art_test_data('art-gtest-jars-StringLiterals.jar')
    self._checker.check_art_test_data('art-gtest-jars-XandY.jar')
    self._checker.check_art_test_data('art-gtest-jars-ExceptionHandle.jar')
    self._checker.check_art_test_data('art-gtest-jars-ImageLayoutB.jar')
    self._checker.check_art_test_data('art-gtest-jars-Interfaces.jar')
    self._checker.check_art_test_data('art-gtest-jars-IMTB.jar')
    self._checker.check_art_test_data('art-gtest-jars-Extension2.jar')
    self._checker.check_art_test_data('art-gtest-jars-Extension1.jar')
    self._checker.check_art_test_data('art-gtest-jars-MainEmptyUncompressedAligned.jar')
    self._checker.check_art_test_data('art-gtest-jars-ErroneousB.jar')
    self._checker.check_art_test_data('art-gtest-jars-MultiDexModifiedSecondary.jar')
    self._checker.check_art_test_data('art-gtest-jars-NonStaticLeafMethods.jar')
    self._checker.check_art_test_data('art-gtest-jars-DefaultMethods.jar')
    self._checker.check_art_test_data('art-gtest-jars-MultiDexUncompressedAligned.jar')
    self._checker.check_art_test_data('art-gtest-jars-StaticsFromCode.jar')
    self._checker.check_art_test_data('art-gtest-jars-ProfileTestMultiDex.jar')
    self._checker.check_art_test_data('art-gtest-jars-VerifySoftFailDuringClinit.dex')
    self._checker.check_art_test_data('art-gtest-jars-MainStripped.jar')
    self._checker.check_art_test_data('art-gtest-jars-ForClassLoaderA.jar')
    self._checker.check_art_test_data('art-gtest-jars-StaticLeafMethods.jar')
    self._checker.check_art_test_data('art-gtest-jars-MultiDex.jar')
    self._checker.check_art_test_data('art-gtest-jars-Packages.jar')
    self._checker.check_art_test_data('art-gtest-jars-ProtoCompare2.jar')
    self._checker.check_art_test_data('art-gtest-jars-Statics.jar')
    self._checker.check_art_test_data('art-gtest-jars-AllFields.jar')
    self._checker.check_art_test_data('art-gtest-jars-IMTA.jar')
    self._checker.check_art_test_data('art-gtest-jars-ImageLayoutA.jar')
    self._checker.check_art_test_data('art-gtest-jars-MainEmptyUncompressed.jar')
    self._checker.check_art_test_data('art-gtest-jars-Dex2oatVdexTestDex.jar')
    self._checker.check_art_test_data('art-gtest-jars-Dex2oatVdexPublicSdkDex.dex')
    self._checker.check_art_test_data('art-gtest-jars-SuperWithAccessChecks.dex')

    # Fuzzer cases
    self._checker.check_art_test_data('dex_verification_fuzzer_corpus.zip')
    self._checker.check_art_test_data('class_verification_fuzzer_corpus.zip')


class NoSuperfluousFilesChecker:
  def __init__(self, checker):
    self._checker = checker

  def __str__(self):
    return 'No superfluous files checker'

  def run(self):
    self._checker.check_no_superfluous_files()


class List:
  def __init__(self, provider, print_size=False):
    self._provider = provider
    self._print_size = print_size

  def print_list(self):

    def print_list_rec(path):
      apex_map = self._provider.read_dir(path)
      if apex_map is None:
        return
      apex_map = dict(apex_map)
      if '.' in apex_map:
        del apex_map['.']
      if '..' in apex_map:
        del apex_map['..']
      for (_, val) in sorted(apex_map.items()):
        val_path = os.path.join(path, val.name)
        if self._print_size:
          if val.size < 0:
            print('[    n/a    ]  %s' % val_path)
          else:
            print('[%11d]  %s' % (val.size, val_path))
        else:
          print(val_path)
        if val.is_dir:
          print_list_rec(val_path)

    print_list_rec('')


class Tree:
  def __init__(self, provider, title, print_size=False):
    print('%s' % title)
    self._provider = provider
    self._has_next_list = []
    self._print_size = print_size

  @staticmethod
  def get_vertical(has_next_list):
    string = ''
    for v in has_next_list:
      string += '%s   ' % ('│' if v else ' ')
    return string

  @staticmethod
  def get_last_vertical(last):
    return '└── ' if last else '├── '

  def print_tree(self):

    def print_tree_rec(path):
      apex_map = self._provider.read_dir(path)
      if apex_map is None:
        return
      apex_map = dict(apex_map)
      if '.' in apex_map:
        del apex_map['.']
      if '..' in apex_map:
        del apex_map['..']
      key_list = list(sorted(apex_map.keys()))
      for i, key in enumerate(key_list):
        prev = self.get_vertical(self._has_next_list)
        last = self.get_last_vertical(i == len(key_list) - 1)
        val = apex_map[key]
        if self._print_size:
          if val.size < 0:
            print('%s%s[    n/a    ]  %s' % (prev, last, val.name))
          else:
            print('%s%s[%11d]  %s' % (prev, last, val.size, val.name))
        else:
          print('%s%s%s' % (prev, last, val.name))
        if val.is_dir:
          self._has_next_list.append(i < len(key_list) - 1)
          val_path = os.path.join(path, val.name)
          print_tree_rec(val_path)
          self._has_next_list.pop()

    print_tree_rec('')


# Note: do not sys.exit early, for __del__ cleanup.
def art_apex_test_main(test_args):
  if test_args.list and test_args.tree:
    logging.error('Both of --list and --tree set')
    return 1
  if test_args.size and not (test_args.list or test_args.tree):
    logging.error('--size set but neither --list nor --tree set')
    return 1
  if not test_args.flattened and not test_args.tmpdir:
    logging.error('Need a tmpdir.')
    return 1
  if not test_args.flattened:
    if not test_args.deapexer:
      logging.error('Need deapexer.')
      return 1
    if not test_args.debugfs:
      logging.error('Need debugfs.')
      return 1
    if not test_args.fsckerofs:
      logging.error('Need fsck.erofs.')
      return 1

  if test_args.flavor == FLAVOR_AUTO:
    logging.warning('--flavor=auto, trying to autodetect. This may be incorrect!')
    # The order of flavors in the list below matters, as the release tag (empty string) will
    # match any package name.
    for flavor in [ FLAVOR_DEBUG, FLAVOR_TESTING, FLAVOR_RELEASE ]:
      flavor_tag = flavor
      # Special handling for the release flavor, whose name is no longer part of the Release ART
      # APEX file name (`com.android.art.capex` / `com.android.art`).
      if flavor == FLAVOR_RELEASE:
        flavor_tag = ''
      flavor_pattern = '*.%s*' % flavor_tag
      if fnmatch.fnmatch(test_args.apex, flavor_pattern):
        test_args.flavor = flavor
        logging.warning('  Detected %s flavor', flavor)
        break
    if test_args.flavor == FLAVOR_AUTO:
      logging.error('  Could not detect APEX flavor, neither %s, %s nor %s for \'%s\'',
                  FLAVOR_RELEASE, FLAVOR_DEBUG, FLAVOR_TESTING, test_args.apex)
      return 1

  apex_dir = test_args.apex
  if not test_args.flattened:
    # Extract the apex. It would be nice to use the output from "deapexer list"
    # to avoid this work, but it doesn't provide info about executable bits.
    apex_dir = extract_apex(test_args.apex, test_args.deapexer, test_args.debugfs,
                            test_args.fsckerofs, test_args.tmpdir)
  apex_provider = TargetApexProvider(apex_dir)

  if test_args.tree:
    Tree(apex_provider, test_args.apex, test_args.size).print_tree()
    return 0
  if test_args.list:
    List(apex_provider, test_args.size).print_list()
    return 0

  checkers = []
  if test_args.bitness == BITNESS_AUTO:
    logging.warning('--bitness=auto, trying to autodetect. This may be incorrect!')
    has_32 = apex_provider.get('lib') is not None
    has_64 = apex_provider.get('lib64') is not None
    if has_32 and has_64:
      logging.warning('  Detected multilib')
      test_args.bitness = BITNESS_MULTILIB
    elif has_32:
      logging.warning('  Detected 32-only')
      test_args.bitness = BITNESS_32
    elif has_64:
      logging.warning('  Detected 64-only')
      test_args.bitness = BITNESS_64
    else:
      logging.error('  Could not detect bitness, neither lib nor lib64 contained.')
      List(apex_provider).print_list()
      return 1

  if test_args.bitness == BITNESS_32:
    base_checker = Arch32Checker(apex_provider)
  elif test_args.bitness == BITNESS_64:
    base_checker = Arch64Checker(apex_provider)
  else:
    assert test_args.bitness == BITNESS_MULTILIB
    base_checker = MultilibChecker(apex_provider)

  checkers.append(ReleaseChecker(base_checker))
  if test_args.flavor == FLAVOR_DEBUG or test_args.flavor == FLAVOR_TESTING:
    checkers.append(DebugChecker(base_checker))
  if test_args.flavor == FLAVOR_TESTING:
    checkers.append(TestingChecker(base_checker))

  # This checker must be last.
  checkers.append(NoSuperfluousFilesChecker(base_checker))

  failed = False
  for checker in checkers:
    logging.info('%s...', checker)
    checker.run()
    if base_checker.error_count() > 0:
      logging.error('%s FAILED', checker)
      failed = True
    else:
      logging.info('%s SUCCEEDED', checker)
    base_checker.reset_errors()

  return 1 if failed else 0


def art_apex_test_default(test_parser):
  if 'ANDROID_PRODUCT_OUT' not in os.environ:
    logging.error('No-argument use requires ANDROID_PRODUCT_OUT')
    sys.exit(1)
  product_out = os.environ['ANDROID_PRODUCT_OUT']
  if 'ANDROID_HOST_OUT' not in os.environ:
    logging.error('No-argument use requires ANDROID_HOST_OUT')
    sys.exit(1)
  host_out = os.environ['ANDROID_HOST_OUT']

  test_args = test_parser.parse_args(['unused'])  # For consistency.
  test_args.debugfs = '%s/bin/debugfs' % host_out
  test_args.fsckerofs = '%s/bin/fsck.erofs' % host_out
  test_args.tmpdir = '.'
  test_args.tree = False
  test_args.list = False
  test_args.bitness = BITNESS_AUTO
  failed = False

  if not os.path.exists(test_args.debugfs):
    logging.error('Cannot find debugfs (default path %s). Please build it, e.g., m debugfs',
                  test_args.debugfs)
    sys.exit(1)

  # TODO: Add support for flattened APEX packages.
  configs = [
    {'name': 'com.android.art.capex',         'flavor': FLAVOR_RELEASE},
    {'name': 'com.android.art.debug.capex',   'flavor': FLAVOR_DEBUG},
    # Note: The Testing ART APEX is not a Compressed APEX.
    {'name': 'com.android.art.testing.apex',  'flavor': FLAVOR_TESTING},
  ]

  for config in configs:
    logging.info(config['name'])
    test_args.apex = '%s/system/apex/%s' % (product_out, config['name'])
    if not os.path.exists(test_args.apex):
      failed = True
      logging.error('Cannot find APEX %s. Please build it first.', test_args.apex)
      continue
    test_args.flavor = config['flavor']
    failed = art_apex_test_main(test_args) != 0

  if failed:
    sys.exit(1)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Check integrity of an ART APEX.')

  parser.add_argument('apex', help='APEX file input')

  parser.add_argument('--flattened', help='Check as flattened (target) APEX', action='store_true')

  parser.add_argument('--flavor', help='Check as FLAVOR APEX', choices=FLAVORS_ALL,
                      default=FLAVOR_AUTO)

  parser.add_argument('--list', help='List all files', action='store_true')
  parser.add_argument('--tree', help='Print directory tree', action='store_true')
  parser.add_argument('--size', help='Print file sizes', action='store_true')

  parser.add_argument('--tmpdir', help='Directory for temp files')
  parser.add_argument('--deapexer', help='Path to deapexer')
  parser.add_argument('--debugfs', help='Path to debugfs')
  parser.add_argument('--fsckerofs', help='Path to fsck.erofs')

  parser.add_argument('--bitness', help='Bitness to check', choices=BITNESS_ALL,
                      default=BITNESS_AUTO)

  if len(sys.argv) == 1:
    art_apex_test_default(parser)
  else:
    args = parser.parse_args()

    if args is None:
      sys.exit(1)

    exit_code = art_apex_test_main(args)
    sys.exit(exit_code)
