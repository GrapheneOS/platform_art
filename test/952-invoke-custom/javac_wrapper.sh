#!/bin/bash
#
# Copyright (C) 2022 The Android Open Source Project
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

set -e # Stop on error - the caller script may not have this set.

# Update arguments to add transformer and ASM to the compiler classpath.
classpath="./transformer.jar:$ASM_JAR"
args=(-cp $classpath)
while [ $# -ne 0 ] ; do
  case $1 in
    -cp|-classpath|--class-path)
      shift
      shift
      ;;
    *)
      args+=("$1")
      shift
      ;;
  esac
done

# Compile.
$JAVAC "${args[@]}"

# Move original classes to intermediate location.
mv classes intermediate-classes
mkdir classes

# Transform intermediate classes.
transformer_args="-cp ${ASM_JAR}:transformer.jar transformer.IndyTransformer"
for class in intermediate-classes/*.class ; do
  transformed_class=classes/$(basename ${class})
  ${JAVA:-java} ${transformer_args} $PWD/${class} ${transformed_class}
done
