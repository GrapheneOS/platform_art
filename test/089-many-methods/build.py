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

from art_build_rules import build_run_test

# Specify old API level as d8 automagically produces a multidex file
# when the API level is above 20. Failing the build here is deliberate.
# Force DEX generation so test also passes with --jvm.
try:
  build_run_test(api_level=20, need_dex=True)
  assert False, "Test was not expected to build successfully"
except Exception as e:
  # Check that a build failure happened (the test is not expected to run).
  assert "Cannot fit requested classes in a single dex" in str(e), e
