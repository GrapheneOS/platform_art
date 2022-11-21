/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_WELL_KNOWN_CLASSES_H_
#define ART_RUNTIME_WELL_KNOWN_CLASSES_H_

#include "base/locks.h"
#include "jni.h"
#include "obj_ptr.h"

namespace art {

class ArtField;
class ArtMethod;

namespace mirror {
class Class;
}  // namespace mirror

// Various classes used in JNI. We cache them so we don't have to keep looking them up.

struct WellKnownClasses {
 public:
  // Run before native methods are registered.
  static void Init(JNIEnv* env);
  // Run after native methods are registered.
  static void LateInit(JNIEnv* env);

  static void Clear();

  static void HandleJniIdTypeChange(JNIEnv* env);

  static void InitStringInit(ObjPtr<mirror::Class> string_class,
                             ObjPtr<mirror::Class> string_builder_class)
      REQUIRES_SHARED(Locks::mutator_lock_);
  static ArtMethod* StringInitToStringFactory(ArtMethod* method);
  static uint32_t StringInitToEntryPoint(ArtMethod* method);

  static ObjPtr<mirror::Class> ToClass(jclass global_jclass) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  static void InitFieldsAndMethodsOnly(JNIEnv* env);

 public:
  static jclass dalvik_annotation_optimization_CriticalNative;
  static jclass dalvik_annotation_optimization_FastNative;
  static jclass dalvik_annotation_optimization_NeverCompile;
  static jclass dalvik_annotation_optimization_NeverInline;
  static jclass dalvik_system_BaseDexClassLoader;
  static jclass dalvik_system_DelegateLastClassLoader;
  static jclass dalvik_system_DexClassLoader;
  static jclass dalvik_system_DexFile;
  static jclass dalvik_system_DexPathList;
  static jclass dalvik_system_DexPathList__Element;
  static jclass dalvik_system_EmulatedStackFrame;
  static jclass dalvik_system_InMemoryDexClassLoader;
  static jclass dalvik_system_PathClassLoader;
  static jclass java_lang_annotation_Annotation__array;
  static jclass java_lang_BootClassLoader;
  static jclass java_lang_ClassLoader;
  static jclass java_lang_ClassNotFoundException;
  static jclass java_lang_Daemons;
  static jclass java_lang_Error;
  static jclass java_lang_IllegalAccessError;
  static jclass java_lang_NoClassDefFoundError;
  static jclass java_lang_Object;
  static jclass java_lang_OutOfMemoryError;
  static jclass java_lang_reflect_InvocationTargetException;
  static jclass java_lang_reflect_Parameter;
  static jclass java_lang_reflect_Parameter__array;
  static jclass java_lang_reflect_Proxy;
  static jclass java_lang_RuntimeException;
  static jclass java_lang_StackOverflowError;
  static jclass java_lang_String;
  static jclass java_lang_StringFactory;
  static jclass java_lang_System;
  static jclass java_lang_Throwable;
  static jclass java_lang_Void;
  static jclass libcore_reflect_AnnotationMember__array;

  static jmethodID dalvik_system_BaseDexClassLoader_getLdLibraryPath;
  static ArtMethod* dalvik_system_VMRuntime_hiddenApiUsed;
  static ArtMethod* java_lang_Boolean_valueOf;
  static ArtMethod* java_lang_Byte_valueOf;
  static ArtMethod* java_lang_Character_valueOf;
  static jmethodID java_lang_ClassLoader_loadClass;
  static jmethodID java_lang_ClassNotFoundException_init;
  static jmethodID java_lang_Daemons_start;
  static jmethodID java_lang_Daemons_stop;
  static jmethodID java_lang_Daemons_waitForDaemonStart;
  static ArtMethod* java_lang_Double_doubleToRawLongBits;
  static ArtMethod* java_lang_Double_valueOf;
  static ArtMethod* java_lang_Float_floatToRawIntBits;
  static ArtMethod* java_lang_Float_valueOf;
  static ArtMethod* java_lang_Integer_valueOf;
  static jmethodID java_lang_invoke_MethodHandle_asType;
  static jmethodID java_lang_invoke_MethodHandle_invokeExact;
  static jmethodID java_lang_invoke_MethodHandles_lookup;
  static jmethodID java_lang_invoke_MethodHandles_Lookup_findConstructor;
  static ArtMethod* java_lang_Long_valueOf;
  static jmethodID java_lang_ref_FinalizerReference_add;
  static jmethodID java_lang_ref_ReferenceQueue_add;
  static jmethodID java_lang_reflect_InvocationTargetException_init;
  static jmethodID java_lang_reflect_Parameter_init;
  static jmethodID java_lang_reflect_Proxy_init;
  static jmethodID java_lang_reflect_Proxy_invoke;
  static jmethodID java_lang_Runtime_nativeLoad;
  static ArtMethod* java_lang_Short_valueOf;
  static jmethodID java_lang_String_charAt;
  static ArtMethod* java_lang_Thread_dispatchUncaughtException;
  static ArtMethod* java_lang_Thread_init;
  static ArtMethod* java_lang_Thread_run;
  static ArtMethod* java_lang_ThreadGroup_add;
  static ArtMethod* java_lang_ThreadGroup_threadTerminated;
  static ArtMethod* java_nio_Buffer_isDirect;
  static ArtMethod* java_nio_DirectByteBuffer_init;
  static ArtMethod* java_util_function_Consumer_accept;
  static ArtMethod* libcore_reflect_AnnotationFactory_createAnnotation;
  static ArtMethod* libcore_reflect_AnnotationMember_init;
  static ArtMethod* org_apache_harmony_dalvik_ddmc_DdmServer_broadcast;
  static ArtMethod* org_apache_harmony_dalvik_ddmc_DdmServer_dispatch;

  static ArtField* dalvik_system_BaseDexClassLoader_pathList;
  static ArtField* dalvik_system_BaseDexClassLoader_sharedLibraryLoaders;
  static ArtField* dalvik_system_BaseDexClassLoader_sharedLibraryLoadersAfter;
  static ArtField* dalvik_system_DexFile_cookie;
  static ArtField* dalvik_system_DexFile_fileName;
  static ArtField* dalvik_system_DexPathList_dexElements;
  static ArtField* dalvik_system_DexPathList__Element_dexFile;
  static ArtField* dalvik_system_VMRuntime_nonSdkApiUsageConsumer;
  static ArtField* java_io_FileDescriptor_descriptor;
  static ArtField* java_lang_ClassLoader_parent;
  static ArtField* java_lang_Thread_parkBlocker;
  static ArtField* java_lang_Thread_daemon;
  static ArtField* java_lang_Thread_group;
  static ArtField* java_lang_Thread_lock;
  static ArtField* java_lang_Thread_name;
  static ArtField* java_lang_Thread_priority;
  static ArtField* java_lang_Thread_nativePeer;
  static ArtField* java_lang_Thread_systemDaemon;
  static ArtField* java_lang_Thread_unparkedBeforeStart;
  static ArtField* java_lang_ThreadGroup_groups;
  static ArtField* java_lang_ThreadGroup_ngroups;
  static ArtField* java_lang_ThreadGroup_mainThreadGroup;
  static ArtField* java_lang_ThreadGroup_name;
  static ArtField* java_lang_ThreadGroup_parent;
  static ArtField* java_lang_ThreadGroup_systemThreadGroup;
  static ArtField* java_lang_Throwable_cause;
  static ArtField* java_lang_Throwable_detailMessage;
  static ArtField* java_lang_Throwable_stackTrace;
  static ArtField* java_lang_Throwable_stackState;
  static ArtField* java_lang_Throwable_suppressedExceptions;
  static ArtField* java_nio_Buffer_address;
  static ArtField* java_nio_Buffer_capacity;
  static ArtField* java_nio_Buffer_elementSizeShift;
  static ArtField* java_nio_Buffer_limit;
  static ArtField* java_nio_Buffer_position;
  static ArtField* java_nio_ByteBuffer_hb;
  static ArtField* java_nio_ByteBuffer_isReadOnly;
  static ArtField* java_nio_ByteBuffer_offset;

  static ArtField* java_util_Collections_EMPTY_LIST;
  static ArtField* libcore_util_EmptyArray_STACK_TRACE_ELEMENT;
  static ArtField* org_apache_harmony_dalvik_ddmc_Chunk_data;
  static ArtField* org_apache_harmony_dalvik_ddmc_Chunk_length;
  static ArtField* org_apache_harmony_dalvik_ddmc_Chunk_offset;
  static ArtField* org_apache_harmony_dalvik_ddmc_Chunk_type;
};

}  // namespace art

#endif  // ART_RUNTIME_WELL_KNOWN_CLASSES_H_
