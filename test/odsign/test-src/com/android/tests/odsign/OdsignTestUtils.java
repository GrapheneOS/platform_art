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

import static com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;

import static com.google.common.truth.Truth.assertWithMessage;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.device.TestDeviceOptions;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.result.FileInputStreamSource;
import com.android.tradefed.result.LogDataType;
import com.android.tradefed.util.CommandResult;

import java.io.File;
import java.time.Duration;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class OdsignTestUtils {
    public static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";

    public static final List<String> ZYGOTE_NAMES = List.of("zygote", "zygote64");

    public static final List<String> APP_ARTIFACT_EXTENSIONS = List.of(".art", ".odex", ".vdex");
    public static final List<String> BCP_ARTIFACT_EXTENSIONS = List.of(".art", ".oat", ".vdex");

    private static final String ODREFRESH_COMPILATION_LOG =
            "/data/misc/odrefresh/compilation-log.txt";

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(5);
    private static final Duration RESTART_ZYGOTE_COMPLETE_TIMEOUT = Duration.ofMinutes(3);

    private static final String TAG = "OdsignTestUtils";
    private static final String PACKAGE_NAME_KEY = TAG + ":PACKAGE_NAME";

    private final InstallUtilsHost mInstallUtils;
    private final TestInformation mTestInfo;

    public OdsignTestUtils(TestInformation testInfo) throws Exception {
        assertNotNull(testInfo.getDevice());
        mInstallUtils = new InstallUtilsHost(testInfo);
        mTestInfo = testInfo;
    }

    /**
     * Re-installs the current active ART module on device.
     */
    public void installTestApex() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());

        String packagesOutput =
                mTestInfo.getDevice().executeShellCommand("pm list packages -f --apex-only");
        Pattern p = Pattern.compile(
                "^package:(.*)=(com(?:\\.google)?\\.android(?:\\.go)?\\.art)$",
                Pattern.MULTILINE);
        Matcher m = p.matcher(packagesOutput);
        assertTrue("ART module not found. Packages are:\n" + packagesOutput, m.find());
        String artApexPath = m.group(1);
        String artApexName = m.group(2);

        CommandResult result = mTestInfo.getDevice().executeShellV2Command(
                "pm install --apex " + artApexPath);
        assertWithMessage("Failed to install APEX. Reason: " + result.toString())
            .that(result.getExitCode()).isEqualTo(0);

        mTestInfo.properties().put(PACKAGE_NAME_KEY, artApexName);

        removeCompilationLogToAvoidBackoff();
    }

    public void uninstallTestApex() throws Exception {
        String packageName = mTestInfo.properties().get(PACKAGE_NAME_KEY);
        if (packageName != null) {
            mTestInfo.getDevice().uninstallPackage(packageName);
            removeCompilationLogToAvoidBackoff();
        }
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

    public void verifyZygoteLoadedArtifacts(String zygoteName, Set<String> mappedArtifacts,
            String bootImageStem) throws Exception {
        assertTrue("Expect 3 bootclasspath artifacts", mappedArtifacts.size() == 3);

        String allArtifacts = mappedArtifacts.stream().collect(Collectors.joining(","));
        for (String extension : BCP_ARTIFACT_EXTENSIONS) {
            final String artifact = bootImageStem + extension;
            final boolean found = mappedArtifacts.stream().anyMatch(a -> a.endsWith(artifact));
            assertTrue(zygoteName + " " + artifact + " not found: '" + allArtifacts + "'", found);
        }
    }

    // Verifies that boot image files with the given stem are loaded by Zygote for each instruction
    // set. Returns the verified files.
    public HashSet<String> verifyZygotesLoadedArtifacts(String bootImageStem) throws Exception {
        // There are potentially two zygote processes "zygote" and "zygote64". These are
        // instances 32-bit and 64-bit unspecialized app_process processes.
        // (frameworks/base/cmds/app_process).
        int zygoteCount = 0;
        HashSet<String> verifiedArtifacts = new HashSet<>();
        for (String zygoteName : ZYGOTE_NAMES) {
            final Optional<Set<String>> mappedArtifacts = getZygoteLoadedArtifacts(zygoteName);
            if (!mappedArtifacts.isPresent()) {
                continue;
            }
            verifyZygoteLoadedArtifacts(zygoteName, mappedArtifacts.get(), bootImageStem);
            zygoteCount += 1;
            verifiedArtifacts.addAll(mappedArtifacts.get());
        }
        assertTrue("No zygote processes found", zygoteCount > 0);
        return verifiedArtifacts;
    }

    public void verifySystemServerLoadedArtifacts() throws Exception {
        String[] classpathElements = getListFromEnvironmentVariable("SYSTEMSERVERCLASSPATH");
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);
        String[] standaloneJars = getListFromEnvironmentVariable("STANDALONE_SYSTEMSERVER_JARS");
        String[] allSystemServerJars = Stream
                .concat(Arrays.stream(classpathElements), Arrays.stream(standaloneJars))
                .toArray(String[]::new);

        final Set<String> mappedArtifacts = getSystemServerLoadedArtifacts();
        assertTrue(
                "No mapped artifacts under " + ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String isaCacheDirectory = String.format("%s/%s", ART_APEX_DALVIK_CACHE_DIRNAME, isa);

        // Check components in the system_server classpath have mapped artifacts.
        for (String element : allSystemServerJars) {
          String escapedPath = element.substring(1).replace('/', '@');
          for (String extension : APP_ARTIFACT_EXTENSIONS) {
            final String fullArtifactPath =
                    String.format("%s/%s@classes%s", isaCacheDirectory, escapedPath, extension);
            assertTrue("Missing " + fullArtifactPath, mappedArtifacts.contains(fullArtifactPath));
          }
        }

        for (String mappedArtifact : mappedArtifacts) {
          // Check the mapped artifact has a .art, .odex or .vdex extension.
          final boolean knownArtifactKind =
                    APP_ARTIFACT_EXTENSIONS.stream().anyMatch(e -> mappedArtifact.endsWith(e));
          assertTrue("Unknown artifact kind: " + mappedArtifact, knownArtifactKind);
        }
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
        TestDeviceOptions options = mTestInfo.getDevice().getOptions();
        // store default value and increase time-out for reboot
        int rebootTimeout = options.getRebootTimeout();
        long onlineTimeout = options.getOnlineTimeout();
        options.setRebootTimeout((int)BOOT_COMPLETE_TIMEOUT.toMillis());
        options.setOnlineTimeout(BOOT_COMPLETE_TIMEOUT.toMillis());
        mTestInfo.getDevice().setOptions(options);

        mTestInfo.getDevice().reboot();
        boolean success =
                mTestInfo.getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());

        // restore default values
        options.setRebootTimeout(rebootTimeout);
        options.setOnlineTimeout(onlineTimeout);
        mTestInfo.getDevice().setOptions(options);

        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }

    public void restartZygote() throws Exception {
        // `waitForBootComplete` relies on `dev.bootcomplete`.
        mTestInfo.getDevice().executeShellCommand("setprop dev.bootcomplete 0");
        mTestInfo.getDevice().executeShellCommand("setprop ctl.restart zygote");
        boolean success = mTestInfo.getDevice()
                .waitForBootComplete(RESTART_ZYGOTE_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Zygote didn't start in %s", BOOT_COMPLETE_TIMEOUT).that(success)
                .isTrue();
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

    private String[] getListFromEnvironmentVariable(String name) throws Exception {
        String systemServerClasspath =
                mTestInfo.getDevice().executeShellCommand("echo $" + name).trim();
        if (!systemServerClasspath.isEmpty()) {
            return systemServerClasspath.split(":");
        }
        return new String[0];
    }

    private String getSystemServerIsa(String mappedArtifact) {
        // Artifact path for system server artifacts has the form:
        //    ART_APEX_DALVIK_CACHE_DIRNAME + "/<arch>/system@framework@some.jar@classes.odex"
        String[] pathComponents = mappedArtifact.split("/");
        return pathComponents[pathComponents.length - 2];
    }

    private long parseFormattedDateTime(String dateTimeStr) throws Exception {
        DateTimeFormatter formatter = DateTimeFormatter.ofPattern(
                "yyyy-MM-dd HH:mm:ss.nnnnnnnnn Z");
        ZonedDateTime zonedDateTime = ZonedDateTime.parse(dateTimeStr, formatter);
        return zonedDateTime.toInstant().toEpochMilli();
    }

    public long getModifiedTimeMs(String filename) throws Exception {
        // We can't use the "-c '%.3Y'" flag when to get the timestamp because the Toybox's `stat`
        // implementation truncates the timestamp to seconds, which is not accurate enough, so we
        // use "-c '%%y'" and parse the time ourselves.
        String dateTimeStr = mTestInfo.getDevice()
                .executeShellCommand(String.format("stat -c '%%y' '%s'", filename))
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    public long getCurrentTimeMs() throws Exception {
        // We can't use getDevice().getDeviceDate() because it truncates the timestamp to seconds,
        // which is not accurate enough.
        String dateTimeStr = mTestInfo.getDevice()
                .executeShellCommand("date +'%Y-%m-%d %H:%M:%S.%N %z'")
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    public int countFilesCreatedBeforeTime(String directory, long timestampMs)
            throws DeviceNotAvailableException {
        // Drop the precision to second, mainly because we need to use `find -newerct` to query
        // files by timestamp, but toybox can't parse `date +'%s.%N'` currently.
        String timestamp = String.valueOf(timestampMs / 1000);
        // For simplicity, directory must be a simple path that doesn't require escaping.
        String output = assertCommandSucceeds(
                "find " + directory + " -type f ! -newerct '@" + timestamp + "' | wc -l");
        return Integer.parseInt(output);
    }

    public int countFilesCreatedAfterTime(String directory, long timestampMs)
            throws DeviceNotAvailableException {
        // Drop the precision to second, mainly because we need to use `find -newerct` to query
        // files by timestamp, but toybox can't parse `date +'%s.%N'` currently.
        String timestamp = String.valueOf(timestampMs / 1000);
        // For simplicity, directory must be a simple path that doesn't require escaping.
        String output = assertCommandSucceeds(
                "find " + directory + " -type f -newerct '@" + timestamp + "' | wc -l");
        return Integer.parseInt(output);
    }

    public String assertCommandSucceeds(String command) throws DeviceNotAvailableException {
        CommandResult result = mTestInfo.getDevice().executeShellV2Command(command);
        assertWithMessage(result.toString()).that(result.getExitCode()).isEqualTo(0);
        return result.getStdout().trim();
    }

    public void archiveLogThenDelete(TestLogData logs, String remotePath, String localName)
            throws DeviceNotAvailableException {
        ITestDevice device = mTestInfo.getDevice();
        File logFile = device.pullFile(remotePath);
        if (logFile != null) {
            logs.addTestLog(localName, LogDataType.TEXT, new FileInputStreamSource(logFile));
            // Delete to avoid confusing logs from a previous run, just in case.
            device.deleteFile(remotePath);
        }
    }

}
