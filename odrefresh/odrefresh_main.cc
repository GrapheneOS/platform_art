/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <sys/stat.h>

#include <string>
#include <string_view>

#include "android-base/properties.h"
#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "arch/instruction_set.h"
#include "base/file_utils.h"
#include "base/globals.h"
#include "odr_common.h"
#include "odr_compilation_log.h"
#include "odr_config.h"
#include "odr_metrics.h"
#include "odrefresh.h"
#include "odrefresh/odrefresh.h"

namespace {

using ::art::InstructionSet;
using ::art::odrefresh::Concatenate;
using ::art::odrefresh::ExitCode;
using ::art::odrefresh::OdrCompilationLog;
using ::art::odrefresh::OdrConfig;
using ::art::odrefresh::OdrMetrics;
using ::art::odrefresh::OnDeviceRefresh;
using ::art::odrefresh::QuotePath;
using ::art::odrefresh::ZygoteKind;

void UsageErrorV(const char* fmt, va_list ap) {
  std::string error;
  android::base::StringAppendV(&error, fmt, ap);
  if (isatty(fileno(stderr))) {
    std::cerr << error << std::endl;
  } else {
    LOG(ERROR) << error;
  }
}

void UsageError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
}

NO_RETURN void ArgumentError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UsageErrorV(fmt, ap);
  va_end(ap);
  UsageError("Try '--help' for more information.");
  exit(EX_USAGE);
}

NO_RETURN void UsageHelp(const char* argv0) {
  std::string name(android::base::Basename(argv0));
  UsageError("Usage: %s ACTION", name.c_str());
  UsageError("On-device refresh tool for boot class path extensions and system server");
  UsageError("following an update of the ART APEX.");
  UsageError("");
  UsageError("Valid ACTION choices are:");
  UsageError("");
  UsageError(
      "--check          Check compilation artifacts are up-to-date based on metadata (fast).");
  UsageError("--compile        Compile boot class path extensions and system_server jars");
  UsageError("                 when necessary.");
  UsageError("--force-compile  Unconditionally compile the boot class path extensions and");
  UsageError("                 system_server jars.");
  UsageError("--verify         Verify artifacts are up-to-date with dexoptanalyzer (slow).");
  UsageError("--help           Display this help information.");
  exit(EX_USAGE);
}

bool ParseZygoteKind(const char* input, ZygoteKind* zygote_kind) {
  std::string_view z(input);
  if (z == "zygote32") {
    *zygote_kind = ZygoteKind::kZygote32;
    return true;
  } else if (z == "zygote32_64") {
    *zygote_kind = ZygoteKind::kZygote32_64;
    return true;
  } else if (z == "zygote64_32") {
    *zygote_kind = ZygoteKind::kZygote64_32;
    return true;
  } else if (z == "zygote64") {
    *zygote_kind = ZygoteKind::kZygote64;
    return true;
  }
  return false;
}

std::string GetEnvironmentVariableOrDie(const char* name) {
  const char* value = getenv(name);
  LOG_ALWAYS_FATAL_IF(value == nullptr, "%s is not defined.", name);
  return value;
}

bool ArgumentMatches(std::string_view argument, std::string_view prefix, std::string* value) {
  if (android::base::StartsWith(argument, prefix)) {
    *value = std::string(argument.substr(prefix.size()));
    return true;
  }
  return false;
}

bool ArgumentEquals(std::string_view argument, std::string_view expected) {
  return argument == expected;
}

bool InitializeCommonConfig(std::string_view argument, OdrConfig* config) {
  static constexpr std::string_view kDryRunArgument{"--dry-run"};
  if (ArgumentEquals(argument, kDryRunArgument)) {
    config->SetDryRun();
    return true;
  }
  return false;
}

int InitializeHostConfig(int argc, const char** argv, OdrConfig* config) {
  __android_log_set_logger(__android_log_stderr_logger);

  std::string current_binary;
  if (argv[0][0] == '/') {
    current_binary = argv[0];
  } else {
    std::vector<char> buf(PATH_MAX);
    if (getcwd(buf.data(), buf.size()) == nullptr) {
      PLOG(FATAL) << "Failed getwd()";
    }
    current_binary = Concatenate({buf.data(), "/", argv[0]});
  }
  config->SetArtBinDir(android::base::Dirname(current_binary));

  int n = 1;
  for (; n < argc - 1; ++n) {
    const char* arg = argv[n];
    std::string value;
    if (ArgumentMatches(arg, "--android-root=", &value)) {
      setenv("ANDROID_ROOT", value.c_str(), 1);
    } else if (ArgumentMatches(arg, "--android-art-root=", &value)) {
      setenv("ANDROID_ART_ROOT", value.c_str(), 1);
    } else if (ArgumentMatches(arg, "--apex-info-list=", &value)) {
      config->SetApexInfoListFile(value);
    } else if (ArgumentMatches(arg, "--art-apex-data=", &value)) {
      setenv("ART_APEX_DATA", value.c_str(), 1);
    } else if (ArgumentMatches(arg, "--dex2oat-bootclasspath=", &value)) {
      config->SetDex2oatBootclasspath(value);
    } else if (ArgumentMatches(arg, "--isa=", &value)) {
      config->SetIsa(art::GetInstructionSetFromString(value.c_str()));
    } else if (ArgumentMatches(arg, "--system-server-classpath=", &value)) {
      config->SetSystemServerClasspath(arg);
    } else if (ArgumentMatches(arg, "--updatable-bcp-packages-file=", &value)) {
      config->SetUpdatableBcpPackagesFile(value);
    } else if (ArgumentMatches(arg, "--zygote-arch=", &value)) {
      ZygoteKind zygote_kind;
      if (!ParseZygoteKind(value.c_str(), &zygote_kind)) {
        ArgumentError("Unrecognized zygote kind: '%s'", value.c_str());
      }
      config->SetZygoteKind(zygote_kind);
    } else if (!InitializeCommonConfig(arg, config)) {
      UsageError("Unrecognized argument: '%s'", arg);
    }
  }
  return n;
}

int InitializeTargetConfig(int argc, const char** argv, OdrConfig* config) {
  config->SetApexInfoListFile("/apex/apex-info-list.xml");
  config->SetArtBinDir(art::GetArtBinDir());
  config->SetBootClasspath(GetEnvironmentVariableOrDie("BOOTCLASSPATH"));
  config->SetDex2oatBootclasspath(GetEnvironmentVariableOrDie("DEX2OATBOOTCLASSPATH"));
  config->SetSystemServerClasspath(GetEnvironmentVariableOrDie("SYSTEMSERVERCLASSPATH"));
  config->SetIsa(art::kRuntimeISA);

  const std::string zygote = android::base::GetProperty("ro.zygote", {});
  ZygoteKind zygote_kind;
  if (!ParseZygoteKind(zygote.c_str(), &zygote_kind)) {
    LOG(FATAL) << "Unknown zygote: " << QuotePath(zygote);
  }
  config->SetZygoteKind(zygote_kind);

  const std::string updatable_packages =
      android::base::GetProperty("dalvik.vm.dex2oat-updatable-bcp-packages-file", {});
  config->SetUpdatableBcpPackagesFile(updatable_packages);

  int n = 1;
  for (; n < argc - 1; ++n) {
    const char* arg = argv[n];
    std::string value;
    if (ArgumentMatches(arg, "--use-compilation-os=", &value)) {
      config->SetCompilationOsAddress(value);
    } else if (!InitializeCommonConfig(arg, config)) {
      UsageError("Unrecognized argument: '%s'", arg);
    }
  }
  return n;
}

int InitializeConfig(int argc, const char** argv, OdrConfig* config) {
  if (art::kIsTargetBuild) {
    return InitializeTargetConfig(argc, argv, config);
  } else {
    return InitializeHostConfig(argc, argv, config);
  }
}

}  // namespace

int main(int argc, const char** argv) {
  // odrefresh is launched by `init` which sets the umask of forked processed to
  // 077 (S_IRWXG | S_IRWXO). This blocks the ability to make files and directories readable
  // by others and prevents system_server from loading generated artifacts.
  umask(S_IWGRP | S_IWOTH);

  OdrConfig config(argv[0]);
  int n = InitializeConfig(argc, argv, &config);
  argv += n;
  argc -= n;
  if (argc != 1) {
    UsageError("Expected 1 argument, but have %d.", argc);
  }

  OdrMetrics metrics(art::odrefresh::kOdrefreshArtifactDirectory);
  OnDeviceRefresh odr(config);
  for (int i = 0; i < argc; ++i) {
    std::string_view action(argv[i]);
    std::vector<InstructionSet> compile_boot_extensions;
    bool compile_system_server;
    if (action == "--check") {
      // Fast determination of whether artifacts are up to date.
      return odr.CheckArtifactsAreUpToDate(
          metrics, &compile_boot_extensions, &compile_system_server);
    } else if (action == "--compile") {
      const ExitCode exit_code =
          odr.CheckArtifactsAreUpToDate(metrics, &compile_boot_extensions, &compile_system_server);
      if (exit_code != ExitCode::kCompilationRequired) {
        return exit_code;
      }
      OdrCompilationLog compilation_log;
      if (!compilation_log.ShouldAttemptCompile(metrics.GetArtApexVersion(),
                                                metrics.GetArtApexLastUpdateMillis(),
                                                metrics.GetTrigger())) {
        return ExitCode::kOkay;
      }
      ExitCode compile_result =
          odr.Compile(metrics, compile_boot_extensions, compile_system_server);
      compilation_log.Log(metrics.GetArtApexVersion(),
                          metrics.GetArtApexLastUpdateMillis(),
                          metrics.GetTrigger(),
                          compile_result);
      return compile_result;
    } else if (action == "--force-compile") {
      // Clean-up existing files.
      if (!odr.RemoveArtifactsDirectory()) {
        metrics.SetStatus(OdrMetrics::Status::kIoError);
        return ExitCode::kCleanupFailed;
      }
      return odr.Compile(metrics,
                         /*compile_boot_extensions=*/config.GetBootExtensionIsas(),
                         /*compile_system_server=*/true);
    } else if (action == "--verify") {
      // Slow determination of whether artifacts are up to date. These are too slow for checking
      // during boot (b/181689036).
      return odr.VerifyArtifactsAreUpToDate();
    } else if (action == "--help") {
      UsageHelp(argv[0]);
    } else {
      UsageError("Unknown argument: ", argv[i]);
    }
  }
  return ExitCode::kOkay;
}
