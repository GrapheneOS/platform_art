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

from art_build_rules import build_run_test, rm
import os

# Build the jars twice. First with applying hiddenapi, creating a boot jar, then
# a second time without to create a normal jar. We need to do this because we
# want to load the jar once as an app module and once as a member of the boot
# class path. The DexFileVerifier would fail on the former as it does not allow
# hidden API access flags in dex files. DexFileVerifier is not invoked on boot
# class path dex files, so the boot jar loads fine in the latter case.

build_run_test(use_hiddenapi=True)

# Move the jar file into the resource folder to be bundled with the test.
os.mkdir("res")
os.rename("674-hiddenapi.jar", "res/boot.jar")

# Clear all intermediate files otherwise default-build would either skip
# compilation or fail rebuilding.
rm("classes*")

build_run_test(use_hiddenapi=False)
