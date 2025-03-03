# /*
#  * Copyright 2014 The Android Open Source Project
#  *
#  * Licensed under the Apache License, Version 2.0 (the "License");
#  * you may not use this file except in compliance with the License.
#  * You may obtain a copy of the License at
#  *
#  *      http://www.apache.org/licenses/LICENSE-2.0
#  *
#  * Unless required by applicable law or agreed to in writing, software
#  * distributed under the License is distributed on an "AS IS" BASIS,
#  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  * See the License for the specific language governing permissions and
#  * limitations under the License.
#  */

.class public LFilledNewArray;

.super Ljava/lang/Object;

.method public static newInt(III)[I
   .registers 4
   filled-new-array {v1, v2, v3}, [I
   move-result-object v0
   return-object v0
.end method

.method public static newRef(Ljava/lang/Object;Ljava/lang/Object;)[Ljava/lang/Object;
   .registers 3
   filled-new-array {v1, v2}, [Ljava/lang/Object;
   move-result-object v0
   return-object v0
.end method

.method public static newArray([I[I)[[I
   .registers 3
   filled-new-array {v1, v2}, [[I
   move-result-object v0
   return-object v0
.end method

.method public static newIntRange(III)[I
   .registers 4
   filled-new-array/range {v1 .. v3}, [I
   move-result-object v0
   return-object v0
.end method

.method public static newRefRange(Ljava/lang/Object;Ljava/lang/Object;)[Ljava/lang/Object;
   .registers 3
   filled-new-array/range {v1 .. v2}, [Ljava/lang/Object;
   move-result-object v0
   return-object v0
.end method

.method public static newArrayRange([I[I)[[I
   .registers 3
   filled-new-array/range {v1 .. v2}, [[I
   move-result-object v0
   return-object v0
.end method
