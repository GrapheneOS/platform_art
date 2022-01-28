/*
 * Copyright (C) 2021 The Android Open Source Project
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

package com.android.tests.odsign;

import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.util.CommandResult;

import java.time.Duration;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;

public class OdsignTestUtils {
    public static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";

    public static final List<String> ZYGOTE_NAMES = List.of("zygote", "zygote64");

    public static final List<String> APP_ARTIFACT_EXTENSIONS = List.of(".art", ".odex", ".vdex");
    public static final List<String> BCP_ARTIFACT_EXTENSIONS = List.of(".art", ".oat", ".vdex");

    private static final String APEX_FILENAME = "test_com.android.art.apex";

    private static final String ODREFRESH_COMPILATION_LOG =
            "/data/misc/odrefresh/compilation-log.txt";

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(2);

    private static final String TAG = "OdsignTestUtils";
    private static final String WAS_ADB_ROOT_KEY = TAG + ":WAS_ADB_ROOT";
    private static final String ADB_ROOT_ENABLED_KEY = TAG + ":ADB_ROOT_ENABLED";

    private final InstallUtilsHost mInstallUtils;
    private final TestInformation mTestInfo;

    public OdsignTestUtils(TestInformation testInfo) throws Exception {
        assertNotNull(testInfo.getDevice());
        mInstallUtils = new InstallUtilsHost(testInfo);
        mTestInfo = testInfo;
    }

    public void installTestApex() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());
        mInstallUtils.installApexes(APEX_FILENAME);
        removeCompilationLogToAvoidBackoff();
    }

    public void uninstallTestApex() throws Exception {
        ApexInfo apex = mInstallUtils.getApexInfo(mInstallUtils.getTestFile(APEX_FILENAME));
        mTestInfo.getDevice().uninstallPackage(apex.name);
        removeCompilationLogToAvoidBackoff();
    }

    public Set<String> getMappedArtifacts(String pid, String grepPattern) throws Exception {
        final String grepCommand = String.format("grep \"%s\" /proc/%s/maps", grepPattern, pid);
        CommandResult result = mTestInfo.getDevice().executeShellV2Command(grepCommand);
        assertTrue(result.toString(), result.getExitCode() == 0);
        Set<String> mappedFiles = new HashSet<>();
        for (String line : result.getStdout().split("\\R")) {
            int start = line.indexOf(ART_APEX_DALVIK_CACHE_DIRNAME);
            if (line.contains("[")) {
                continue; // ignore anonymously mapped sections which are quoted in square braces.
            }
            mappedFiles.add(line.substring(start));
        }
        return mappedFiles;
    }

    /**
     * Returns the mapped artifacts of the Zygote process, or {@code Optional.empty()} if the
     * process does not exist.
     */
    public Optional<Set<String>> getZygoteLoadedArtifacts(String zygoteName) throws Exception {
        final CommandResult result =
                mTestInfo.getDevice().executeShellV2Command("pidof " + zygoteName);
        if (result.getExitCode() != 0) {
            return Optional.empty();
        }
        // There may be multiple Zygote processes when Zygote just forks and has not executed any
        // app binary. We can take any of the pids.
        // We can't use the "-s" flag when calling `pidof` because the Toybox's `pidof`
        // implementation is wrong and it outputs multiple pids regardless of the "-s" flag, so we
        // split the output and take the first pid ourselves.
        final String zygotePid = result.getStdout().trim().split("\\s+")[0];
        assertTrue(!zygotePid.isEmpty());

        final String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + ".*boot";
        return Optional.of(getMappedArtifacts(zygotePid, grepPattern));
    }

    public Set<String> getSystemServerLoadedArtifacts() throws Exception {
        final CommandResult result =
                mTestInfo.getDevice().executeShellV2Command("pidof system_server");
        assertTrue(result.toString(), result.getExitCode() == 0);
        final String systemServerPid = result.getStdout().trim();
        assertTrue(!systemServerPid.isEmpty());
        assertTrue(
                "There should be exactly one `system_server` process",
                systemServerPid.matches("\\d+"));

        // system_server artifacts are in the APEX data dalvik cache and names all contain
        // the word "@classes". Look for mapped files that match this pattern in the proc map for
        // system_server.
        final String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + ".*@classes";
        return getMappedArtifacts(systemServerPid, grepPattern);
    }

    public boolean haveCompilationLog() throws Exception {
        CommandResult result =
                mTestInfo.getDevice().executeShellV2Command("stat " + ODREFRESH_COMPILATION_LOG);
        return result.getExitCode() == 0;
    }

    public void removeCompilationLogToAvoidBackoff() throws Exception {
        mTestInfo.getDevice().executeShellCommand("rm -f " + ODREFRESH_COMPILATION_LOG);
    }

    public void reboot() throws Exception {
        mTestInfo.getDevice().reboot();
        boolean success =
                mTestInfo.getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }

    /**
     * Enables adb root or skips the test if adb root is not supported.
     */
    public void enableAdbRootOrSkipTest() throws Exception {
        setBoolean(WAS_ADB_ROOT_KEY, mTestInfo.getDevice().isAdbRoot());
        boolean adbRootEnabled = mTestInfo.getDevice().enableAdbRoot();
        assumeTrue("ADB root failed and required to get process maps", adbRootEnabled);
        setBoolean(ADB_ROOT_ENABLED_KEY, adbRootEnabled);
    }

    /**
     * Restores the device to the state before {@link enableAdbRootOrSkipTest} was called.
     */
    public void restoreAdbRoot() throws Exception {
        if (getBooleanOrDefault(ADB_ROOT_ENABLED_KEY) && !getBooleanOrDefault(WAS_ADB_ROOT_KEY)) {
            mTestInfo.getDevice().disableAdbRoot();
        }
    }

    /**
     * Returns the value of a boolean test property, or false if it does not exist.
     */
    private boolean getBooleanOrDefault(String key) {
        String value = mTestInfo.properties().get(key);
        if (value == null) {
            return false;
        }
        return Boolean.parseBoolean(value);
    }

    private void setBoolean(String key, boolean value) {
        mTestInfo.properties().put(key, Boolean.toString(value));
    }
}
