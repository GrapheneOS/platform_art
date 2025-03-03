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

.class public LFillArrayData;

.super Ljava/lang/Object;

.method public static emptyIntArray([I)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 4
    .end array-data

.end method

.method public static intArray([I)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 4
        1 2 3 4 5
    .end array-data

.end method

.method public static intArrayFillInstructionAfterData([I)V
   .registers 1
   goto :FillInstruction

:ArrayData
    .array-data 4
        1 2 3 4 5
    .end array-data

:FillInstruction
   fill-array-data v0, :ArrayData
   return-void

.end method

.method public static shortArray([S)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 2
        1 2 3 4 5
    .end array-data

.end method

.method public static charArray([C)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 2
        1 2 3 4 5
    .end array-data

.end method

.method public static byteArray([B)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 1
        1 2 3 4 5
    .end array-data

.end method

.method public static booleanArray([Z)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 1
        0 1 1
    .end array-data

.end method

.method public static longArray([J)V
   .registers 1

   fill-array-data v0, :ArrayData
   return-void

:ArrayData
    .array-data 8
        1 2 3 4 5
    .end array-data

.end method
