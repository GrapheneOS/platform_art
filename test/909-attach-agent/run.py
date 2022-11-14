#!/bin/bash
#
# Copyright 2016 The Android Open Source Project
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


def run(ctx, args):
  agent = "libtiagent.so" if args.O else "libtiagentd.so"
  plugin = "libopenjdkjvmti.so" if args.O else "libopenjdkjvmtid.so"

  if args.interpreter:
    # On interpreter we are fully capable of providing the full jvmti api so we
    # have a slightly different expected output.
    ctx.expected_stdout = ctx.expected_stdout.with_suffix(".interpreter.txt")

  # Provide additional runtime options when running on device.
  if not args.host:
    for i, opt in enumerate(args.runtime_option):
      if opt.startswith("-Djava.library.path="):
        libpath = opt.split("=")[-1]
        assert libpath.startswith("/data/nativetest"), libpath

        # The linker configuration used for dalvikvm(64) in the ART APEX requires us
        # to pass the full path to the agent to the runtime when running on device.
        agent = f"{libpath}/{agent}"

        # The above agent path is an absolute one; append the root directory to the
        # library path so that the agent can be found via the `java.library.path`
        # system property (see method `Main.find` in
        # test/909-attach-agent/src-art/Main.java).
        args.runtime_option[i] += ":/"
        break

  ctx.default_run(
      args,
      android_runtime_option=[
          f"-Xplugin:{plugin}", "-Xcompiler-option", "--debuggable"
      ],
      test_args=[f"agent:{agent}=909-attach-agent"])

  ctx.default_run(args, test_args=[f"agent:{agent}=909-attach-agent"])

  ctx.default_run(
      args,
      test_args=[f"agent:{agent}=909-attach-agent"],
      android_log_tags="*:f")

  ctx.default_run(
      args,
      test_args=[f"agent:{agent}=909-attach-agent", "disallow-debugging"],
      android_log_tags="*:f")
