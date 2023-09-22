/*
 * Copyright (C) 2019 The Android Open Source Project
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

#if defined(ART_TARGET_ANDROID)

#include "library_namespaces.h"

#include <dirent.h>
#include <dlfcn.h>

#include <regex>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/result.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>
#include <nativehelper/scoped_utf_chars.h>

#include "nativeloader/dlext_namespaces.h"
#include "public_libraries.h"
#include "utils.h"

namespace android::nativeloader {

namespace {

constexpr const char* kApexPath = "/apex/";

// The device may be configured to have the vendor libraries loaded to a separate namespace.
// For historical reasons this namespace was named sphal but effectively it is intended
// to use to load vendor libraries to separate namespace with controlled interface between
// vendor and system namespaces.
constexpr const char* kVendorNamespaceName = "sphal";
// Similar to sphal namespace, product namespace provides some product libraries.
constexpr const char* kProductNamespaceName = "product";

// vndk namespace for unbundled vendor apps
constexpr const char* kVndkNamespaceName = "vndk";
// vndk_product namespace for unbundled product apps
constexpr const char* kVndkProductNamespaceName = "vndk_product";

// clns-XX is a linker namespace that is created for normal apps installed in
// the data partition. To be specific, it is created for the app classloader.
// When System.load() is called from a Java class that is loaded from the
// classloader, the clns namespace associated with that classloader is selected
// for dlopen. The namespace is configured so that its search path is set to the
// app-local JNI directory and it is linked to the system namespace with the
// names of libs listed in the public.libraries.txt and other public libraries.
// This way an app can only load its own JNI libraries along with the public
// libs.
constexpr const char* kClassloaderNamespaceName = "clns";
// Same thing for unbundled APKs in the vendor partition.
constexpr const char* kVendorClassloaderNamespaceName = "vendor-clns";
// Same thing for unbundled APKs in the product partition.
constexpr const char* kProductClassloaderNamespaceName = "product-clns";
// If the namespace is shared then add this suffix to help identify it in debug
// messages. A shared namespace (cf. ANDROID_NAMESPACE_TYPE_SHARED) has
// inherited all the libraries of the parent classloader namespace, or the
// system namespace for the main app classloader. It is used to give full access
// to the platform libraries for apps bundled in the system image, including
// their later updates installed in /data.
constexpr const char* kSharedNamespaceSuffix = "-shared";

// (http://b/27588281) This is a workaround for apps using custom classloaders and calling
// System.load() with an absolute path which is outside of the classloader library search path.
// This list includes all directories app is allowed to access this way.
constexpr const char* kAlwaysPermittedDirectories = "/data:/mnt/expand";

constexpr const char* kVendorLibPath = "/vendor/" LIB;
// TODO(mast): It's unlikely that both paths are necessary for kProductLibPath
// below, because they can't be two separate directories - either one has to be
// a symlink to the other.
constexpr const char* kProductLibPath = "/product/" LIB ":/system/product/" LIB;

const std::regex kVendorDexPathRegex("(^|:)(/system)?/vendor/");
const std::regex kProductDexPathRegex("(^|:)(/system)?/product/");

// Define origin partition of APK
using ApkOrigin = enum {
  APK_ORIGIN_DEFAULT = 0,
  APK_ORIGIN_VENDOR = 1,   // Includes both /vendor and /system/vendor
  APK_ORIGIN_PRODUCT = 2,  // Includes both /product and /system/product
};

jobject GetParentClassLoader(JNIEnv* env, jobject class_loader) {
  jclass class_loader_class = env->FindClass("java/lang/ClassLoader");
  jmethodID get_parent =
      env->GetMethodID(class_loader_class, "getParent", "()Ljava/lang/ClassLoader;");

  return env->CallObjectMethod(class_loader, get_parent);
}

ApkOrigin GetApkOriginFromDexPath(const std::string& dex_path) {
  ApkOrigin apk_origin = APK_ORIGIN_DEFAULT;
  if (std::regex_search(dex_path, kVendorDexPathRegex)) {
    apk_origin = APK_ORIGIN_VENDOR;
  }
  if (std::regex_search(dex_path, kProductDexPathRegex)) {
    LOG_ALWAYS_FATAL_IF(apk_origin == APK_ORIGIN_VENDOR,
                        "Dex path contains both vendor and product partition : %s",
                        dex_path.c_str());

    apk_origin = APK_ORIGIN_PRODUCT;
  }
  return apk_origin;
}

}  // namespace

void LibraryNamespaces::Initialize() {
  // Once public namespace is initialized there is no
  // point in running this code - it will have no effect
  // on the current list of public libraries.
  if (initialized_) {
    return;
  }

  // Load the preloadable public libraries. Since libnativeloader is in the
  // com_android_art namespace, use OpenSystemLibrary rather than dlopen to
  // ensure the libraries are loaded in the system namespace.
  //
  // TODO(dimitry): this is a bit misleading since we do not know
  // if the vendor public library is going to be opened from /vendor/lib
  // we might as well end up loading them from /system/lib or /product/lib
  // For now we rely on CTS test to catch things like this but
  // it should probably be addressed in the future.
  for (const std::string& soname : android::base::Split(preloadable_public_libraries(), ":")) {
    void* handle = OpenSystemLibrary(soname.c_str(), RTLD_NOW | RTLD_NODELETE);
    LOG_ALWAYS_FATAL_IF(handle == nullptr,
                        "Error preloading public library %s: %s", soname.c_str(), dlerror());
  }
}

// "ALL" is a magic name that allows all public libraries even when the
// target SDK is > 30. Currently this is used for (Java) shared libraries
// which don't use <uses-native-library>
// TODO(b/142191088) remove this hack
static constexpr const char LIBRARY_ALL[] = "ALL";

// Returns the colon-separated list of library names by filtering uses_libraries from
// public_libraries. The returned names will actually be available to the app. If the app is pre-S
// (<= 30), the filtering is not done; the entire public_libraries are provided.
static const std::string filter_public_libraries(
    uint32_t target_sdk_version, const std::vector<std::string>& uses_libraries,
    const std::string& public_libraries) {
  // Apps targeting Android 11 or earlier gets all public libraries
  if (target_sdk_version <= 30) {
    return public_libraries;
  }
  if (std::find(uses_libraries.begin(), uses_libraries.end(), LIBRARY_ALL) !=
      uses_libraries.end()) {
    return public_libraries;
  }
  std::vector<std::string> filtered;
  std::vector<std::string> orig = android::base::Split(public_libraries, ":");
  for (const auto& lib : uses_libraries) {
    if (std::find(orig.begin(), orig.end(), lib) != orig.end()) {
      filtered.emplace_back(lib);
    }
  }
  return android::base::Join(filtered, ":");
}

Result<NativeLoaderNamespace*> LibraryNamespaces::Create(JNIEnv* env, uint32_t target_sdk_version,
                                                         jobject class_loader, bool is_shared,
                                                         jstring dex_path_j,
                                                         jstring java_library_path,
                                                         jstring java_permitted_path,
                                                         jstring uses_library_list) {
  std::string library_path;  // empty string by default.
  std::string dex_path;

  if (java_library_path != nullptr) {
    ScopedUtfChars library_path_utf_chars(env, java_library_path);
    library_path = library_path_utf_chars.c_str();
  }

  if (dex_path_j != nullptr) {
    ScopedUtfChars dex_path_chars(env, dex_path_j);
    dex_path = dex_path_chars.c_str();
  }

  std::vector<std::string> uses_libraries;
  if (uses_library_list != nullptr) {
    ScopedUtfChars names(env, uses_library_list);
    uses_libraries = android::base::Split(names.c_str(), ":");
  } else {
    // uses_library_list could be nullptr when System.loadLibrary is called from a
    // custom classloader. In that case, we don't know the list of public
    // libraries because we don't know which apk the classloader is for. Only
    // choices we can have are 1) allowing all public libs (as before), or 2)
    // not allowing all but NDK libs. Here we take #1 because #2 would surprise
    // developers unnecessarily.
    // TODO(b/142191088) finalize the policy here. We could either 1) allow all
    // public libs, 2) disallow any lib, or 3) use the libs that were granted to
    // the first (i.e. app main) classloader.
    uses_libraries.emplace_back(LIBRARY_ALL);
  }

  ApkOrigin apk_origin = GetApkOriginFromDexPath(dex_path);

  // (http://b/27588281) This is a workaround for apps using custom
  // classloaders and calling System.load() with an absolute path which
  // is outside of the classloader library search path.
  //
  // This part effectively allows such a classloader to access anything
  // under /data and /mnt/expand
  std::string permitted_path = kAlwaysPermittedDirectories;

  if (java_permitted_path != nullptr) {
    ScopedUtfChars path(env, java_permitted_path);
    if (path.c_str() != nullptr && path.size() > 0) {
      permitted_path = permitted_path + ":" + path.c_str();
    }
  }

  LOG_ALWAYS_FATAL_IF(FindNamespaceByClassLoader(env, class_loader) != nullptr,
                      "There is already a namespace associated with this classloader");

  std::string system_exposed_libraries = default_public_libraries();
  std::string namespace_name = kClassloaderNamespaceName;
  ApkOrigin unbundled_app_origin = APK_ORIGIN_DEFAULT;
  const char* apk_origin_msg = "other apk";  // Only for debug logging.

  if (!is_shared) {
    if (apk_origin == APK_ORIGIN_VENDOR) {
      unbundled_app_origin = APK_ORIGIN_VENDOR;
      apk_origin_msg = "unbundled vendor apk";

      // For vendor apks, give access to the vendor libs even though they are
      // treated as unbundled; the libs and apks are still bundled together in the
      // vendor partition.
      library_path = library_path + ':' + kVendorLibPath;
      permitted_path = permitted_path + ':' + kVendorLibPath;

      // Also give access to LLNDK libraries since they are available to vendor.
      system_exposed_libraries = system_exposed_libraries + ':' + llndk_libraries_vendor();

      // Different name is useful for debugging
      namespace_name = kVendorClassloaderNamespaceName;
    } else if (apk_origin == APK_ORIGIN_PRODUCT) {
      unbundled_app_origin = APK_ORIGIN_PRODUCT;
      apk_origin_msg = "unbundled product apk";

      // Like for vendor apks, give access to the product libs since they are
      // bundled together in the same partition.
      library_path = library_path + ':' + kProductLibPath;
      permitted_path = permitted_path + ':' + kProductLibPath;

      // Also give access to LLNDK libraries since they are available to product.
      system_exposed_libraries = system_exposed_libraries + ':' + llndk_libraries_product();

      // Different name is useful for debugging
      namespace_name = kProductClassloaderNamespaceName;
    }
  }

  if (is_shared) {
    // Show in the name that the namespace was created as shared, for debugging
    // purposes.
    namespace_name = namespace_name + kSharedNamespaceSuffix;
  }

  // Append a unique number to the namespace name, to tell them apart when
  // debugging linker issues, e.g. with debug.ld.all set to "dlopen,dlerror".
  static int clns_count = 0;
  namespace_name = android::base::StringPrintf("%s-%d", namespace_name.c_str(), ++clns_count);

  ALOGD(
      "Configuring %s for %s %s. target_sdk_version=%u, uses_libraries=%s, library_path=%s, "
      "permitted_path=%s",
      namespace_name.c_str(),
      apk_origin_msg,
      dex_path.c_str(),
      static_cast<unsigned>(target_sdk_version),
      android::base::Join(uses_libraries, ':').c_str(),
      library_path.c_str(),
      permitted_path.c_str());

  if (unbundled_app_origin != APK_ORIGIN_VENDOR) {
    // Extended public libraries are NOT available to unbundled vendor apks, but
    // they are to other apps, including those in system, system_ext, and
    // product partitions. The reason is that when GSI is used, the system
    // partition may get replaced, and then vendor apps may fail. It's fine for
    // product apps, because that partition isn't mounted in GSI tests.
    auto libs =
        filter_public_libraries(target_sdk_version, uses_libraries, extended_public_libraries());
    if (!libs.empty()) {
      ALOGD("Extending system_exposed_libraries: %s", libs.c_str());
      system_exposed_libraries = system_exposed_libraries + ':' + libs;
    }
  }

  // Create the app namespace
  NativeLoaderNamespace* parent_ns = FindParentNamespaceByClassLoader(env, class_loader);
  // Heuristic: the first classloader with non-empty library_path is assumed to
  // be the main classloader for app
  // TODO(b/139178525) remove this heuristic by determining this in LoadedApk (or its
  // friends) and then passing it down to here.
  bool is_main_classloader = app_main_namespace_ == nullptr && !library_path.empty();
  // Policy: the namespace for the main classloader is also used as the
  // anonymous namespace.
  bool also_used_as_anonymous = is_main_classloader;
  // Note: this function is executed with g_namespaces_mutex held, thus no
  // racing here.
  auto app_ns = NativeLoaderNamespace::Create(
      namespace_name, library_path, permitted_path, parent_ns, is_shared,
      target_sdk_version < 24 /* is_exempt_list_enabled */, also_used_as_anonymous);
  if (!app_ns.ok()) {
    return app_ns.error();
  }
  // ... and link to other namespaces to allow access to some public libraries
  bool is_bridged = app_ns->IsBridged();

  auto system_ns = NativeLoaderNamespace::GetSystemNamespace(is_bridged);
  if (!system_ns.ok()) {
    return system_ns.error();
  }

  auto linked = app_ns->Link(&system_ns.value(), system_exposed_libraries);
  if (!linked.ok()) {
    return linked.error();
  }

  for (const auto&[apex_ns_name, public_libs] : apex_public_libraries()) {
    auto ns = NativeLoaderNamespace::GetExportedNamespace(apex_ns_name, is_bridged);
    // Even if APEX namespace is visible, it may not be available to bridged.
    if (ns.ok()) {
      linked = app_ns->Link(&ns.value(), public_libs);
      if (!linked.ok()) {
        return linked.error();
      }
    }
  }

  // Give access to VNDK-SP libraries from the 'vndk' namespace for unbundled vendor apps.
  if (unbundled_app_origin == APK_ORIGIN_VENDOR && !vndksp_libraries_vendor().empty()) {
    auto vndk_ns = NativeLoaderNamespace::GetExportedNamespace(kVndkNamespaceName, is_bridged);
    if (vndk_ns.ok()) {
      linked = app_ns->Link(&vndk_ns.value(), vndksp_libraries_vendor());
      if (!linked.ok()) {
        return linked.error();
      }
    }
  }

  // Give access to VNDK-SP libraries from the 'vndk_product' namespace for unbundled product apps.
  if (unbundled_app_origin == APK_ORIGIN_PRODUCT && !vndksp_libraries_product().empty()) {
    auto vndk_ns = NativeLoaderNamespace::GetExportedNamespace(kVndkProductNamespaceName, is_bridged);
    if (vndk_ns.ok()) {
      linked = app_ns->Link(&vndk_ns.value(), vndksp_libraries_product());
      if (!linked.ok()) {
        return linked.error();
      }
    }
  }

  for (const std::string& each_jar_path : android::base::Split(dex_path, ":")) {
    auto apex_ns_name = FindApexNamespaceName(each_jar_path);
    if (apex_ns_name.ok()) {
      const auto& jni_libs = apex_jni_libraries(*apex_ns_name);
      if (jni_libs != "") {
        auto apex_ns = NativeLoaderNamespace::GetExportedNamespace(*apex_ns_name, is_bridged);
        if (apex_ns.ok()) {
          linked = app_ns->Link(&apex_ns.value(), jni_libs);
          if (!linked.ok()) {
            return linked.error();
          }
        }
      }
    }
  }

  auto vendor_libs = filter_public_libraries(target_sdk_version, uses_libraries,
                                             vendor_public_libraries());
  if (!vendor_libs.empty()) {
    auto vendor_ns = NativeLoaderNamespace::GetExportedNamespace(kVendorNamespaceName, is_bridged);
    // when vendor_ns is not configured, link to the system namespace
    auto target_ns = vendor_ns.ok() ? vendor_ns : system_ns;
    if (target_ns.ok()) {
      linked = app_ns->Link(&target_ns.value(), vendor_libs);
      if (!linked.ok()) {
        return linked.error();
      }
    }
  }

  auto product_libs = filter_public_libraries(target_sdk_version, uses_libraries,
                                              product_public_libraries());
  if (!product_libs.empty()) {
    auto target_ns = NativeLoaderNamespace::GetExportedNamespace(kProductNamespaceName, is_bridged);
    if (target_ns.ok()) {
      linked = app_ns->Link(&target_ns.value(), product_libs);
      if (!linked.ok()) {
        return linked.error();
      }
    } else {
      // The linkerconfig must have a problem on defining the product namespace in the system
      // section. Skip linking product namespace. This will not affect most of the apps. Only the
      // apps that requires the product public libraries will fail.
      ALOGW("Namespace for product libs not found: %s", target_ns.error().message().c_str());
    }
  }

  auto& emplaced = namespaces_.emplace_back(
      std::make_pair(env->NewWeakGlobalRef(class_loader), *app_ns));
  if (is_main_classloader) {
    app_main_namespace_ = &emplaced.second;
  }
  return &emplaced.second;
}

NativeLoaderNamespace* LibraryNamespaces::FindNamespaceByClassLoader(JNIEnv* env,
                                                                     jobject class_loader) {
  auto it = std::find_if(namespaces_.begin(), namespaces_.end(),
                         [&](const std::pair<jweak, NativeLoaderNamespace>& value) {
                           return env->IsSameObject(value.first, class_loader);
                         });
  if (it != namespaces_.end()) {
    return &it->second;
  }

  return nullptr;
}

NativeLoaderNamespace* LibraryNamespaces::FindParentNamespaceByClassLoader(JNIEnv* env,
                                                                           jobject class_loader) {
  jobject parent_class_loader = GetParentClassLoader(env, class_loader);

  while (parent_class_loader != nullptr) {
    NativeLoaderNamespace* ns;
    if ((ns = FindNamespaceByClassLoader(env, parent_class_loader)) != nullptr) {
      return ns;
    }

    parent_class_loader = GetParentClassLoader(env, parent_class_loader);
  }

  return nullptr;
}

base::Result<std::string> FindApexNamespaceName(const std::string& location) {
  // Lots of implicit assumptions here: we expect `location` to be of the form:
  // /apex/modulename/...
  //
  // And we extract from it 'modulename', and then apply mangling rule to get namespace name for it.
  if (android::base::StartsWith(location, kApexPath)) {
    size_t start_index = strlen(kApexPath);
    size_t slash_index = location.find_first_of('/', start_index);
    LOG_ALWAYS_FATAL_IF((slash_index == std::string::npos),
                        "Error finding namespace of apex: no slash in path %s", location.c_str());
    std::string name = location.substr(start_index, slash_index - start_index);
    std::replace(name.begin(), name.end(), '.', '_');
    return name;
  }
  return base::Error();
}

}  // namespace android::nativeloader

#endif  // defined(ART_TARGET_ANDROID)
