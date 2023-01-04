#!/bin/bash
#
# Copyright (C) 2017 The Android Open Source Project
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


def run(ctx, args):
  # The expected output is different in debuggable and non debuggable. Just
  # enable debuggable for now.
  ctx.default_run(args)

  print(args);
  if ("--debuggable" in args.Xcompiler_option):
    # On debuggable runtimes we disable oat code in boot images right at the start
    # so we get events for all methods including methods optimized in boot images.
    ctx.expected_stdout = ctx.expected_stdout.with_suffix(".debuggable.txt")
  elif ("--interpreter" in args.Xcompiler_option) or args.interpreter:
    # On forced interpreter runtimes we don't get method events for optimized
    # methods in boot images but get events for a few more methods that would
    # have otherwise used nterp.
    ctx.expected_stdout = ctx.expected_stdout.with_suffix(".interpreter.txt")
