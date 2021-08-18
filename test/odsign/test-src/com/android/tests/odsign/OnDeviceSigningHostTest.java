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

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.ITestDevice.ApexInfo;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;
import com.android.tradefed.util.CommandResult;

import org.junit.After;
import org.junit.Before;
import org.junit.FixMethodOrder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.MethodSorters;

import java.time.Duration;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.stream.Collectors;

@RunWith(DeviceJUnit4ClassRunner.class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
public class OnDeviceSigningHostTest extends BaseHostJUnit4Test {

    private static final String APEX_FILENAME = "test_com.android.art.apex";

    private static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";

    private static final String ODREFRESH_COMPILATION_LOG =
            "/data/misc/odrefresh/compilation-log.txt";

    private static final String CACHE_INFO_FILE = ART_APEX_DALVIK_CACHE_DIRNAME + "/cache-info.xml";

    private final String[] APP_ARTIFACT_EXTENSIONS = new String[] {".art", ".odex", ".vdex"};

    private final String[] BCP_ARTIFACT_EXTENSIONS = new String[] {".art", ".oat", ".vdex"};

    private static final String TEST_APP_PACKAGE_NAME = "com.android.tests.odsign";
    private static final String TEST_APP_APK = "odsign_e2e_test_app.apk";

    private final InstallUtilsHost mInstallUtils = new InstallUtilsHost(this);

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(2);

    private final String[] ZYGOTE_NAMES = new String[] {"zygote", "zygote64"};

    private Set<String> mZygoteArtifacts;
    private Set<String> mSystemServerArtifacts;
    private long mBootTimeMs;

    @Before
    public void setUp() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());
        installPackage(TEST_APP_APK);
        mInstallUtils.installApexes(APEX_FILENAME);
        removeCompilationLogToAvoidBackoff();
        reboot();

        mZygoteArtifacts = new HashSet<>();
        for (String zygoteName : ZYGOTE_NAMES) {
            mZygoteArtifacts.addAll(getZygoteLoadedArtifacts(zygoteName).orElse(new HashSet<>()));
        }
        mSystemServerArtifacts = getSystemServerLoadedArtifacts();
        mBootTimeMs = getCurrentTimeMs();
    }

    @After
    public void cleanup() throws Exception {
        ApexInfo apex = mInstallUtils.getApexInfo(mInstallUtils.getTestFile(APEX_FILENAME));
        getDevice().uninstallPackage(apex.name);
        removeCompilationLogToAvoidBackoff();
        reboot();
    }

    @Test
    public void verifyArtUpgradeSignsFiles() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testArtArtifactsHaveFsverity");
        runDeviceTests(options);
    }

    @Test
    public void verifyArtUpgradeGeneratesRequiredArtifacts() throws Exception {
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testGeneratesRequiredArtArtifacts");
        runDeviceTests(options);
    }

    private Set<String> getMappedArtifacts(String pid, String grepPattern) throws Exception {
        final String grepCommand = String.format("grep \"%s\" /proc/%s/maps", grepPattern, pid);
        CommandResult result = getDevice().executeShellV2Command(grepCommand);
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
    private Optional<Set<String>> getZygoteLoadedArtifacts(String zygoteName) throws Exception {
        final CommandResult result =
                getDevice().executeShellV2Command("pidof " + zygoteName);
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

        final String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + ".*boot-framework";
        return Optional.of(getMappedArtifacts(zygotePid, grepPattern));
    }

    private Set<String> getSystemServerLoadedArtifacts() throws Exception {
        final CommandResult result =
                getDevice().executeShellV2Command("pidof system_server");
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

    private String[] getSystemServerClasspath() throws Exception {
        String systemServerClasspath =
                getDevice().executeShellCommand("echo $SYSTEMSERVERCLASSPATH");
        return systemServerClasspath.trim().split(":");
    }

    private String getSystemServerIsa(String mappedArtifact) {
        // Artifact path for system server artifacts has the form:
        //    ART_APEX_DALVIK_CACHE_DIRNAME + "/<arch>/system@framework@some.jar@classes.odex"
        // `mappedArtifacts` may include other artifacts, such as boot-framework.oat that are not
        // prefixed by the architecture.
        String[] pathComponents = mappedArtifact.split("/");
        return pathComponents[pathComponents.length - 2];
    }

    private void verifySystemServerLoadedArtifacts() throws Exception {
        String[] classpathElements = getSystemServerClasspath();
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);

        final Set<String> mappedArtifacts = getSystemServerLoadedArtifacts();
        assertTrue(
                "No mapped artifacts under " + ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String isaCacheDirectory = String.format("%s/%s", ART_APEX_DALVIK_CACHE_DIRNAME, isa);

        // Check components in the system_server classpath have mapped artifacts.
        for (String element : classpathElements) {
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
              Arrays.stream(APP_ARTIFACT_EXTENSIONS).anyMatch(e -> mappedArtifact.endsWith(e));
          assertTrue("Unknown artifact kind: " + mappedArtifact, knownArtifactKind);
        }
    }

    private void verifyZygoteLoadedArtifacts(String zygoteName, Set<String> mappedArtifacts)
            throws Exception {
        final String bootExtensionName = "boot-framework";

        assertTrue("Expect 3 boot-framework artifacts", mappedArtifacts.size() == 3);

        String allArtifacts = mappedArtifacts.stream().collect(Collectors.joining(","));
        for (String extension : BCP_ARTIFACT_EXTENSIONS) {
            final String artifact = bootExtensionName + extension;
            final boolean found = mappedArtifacts.stream().anyMatch(a -> a.endsWith(artifact));
            assertTrue(zygoteName + " " + artifact + " not found: '" + allArtifacts + "'", found);
        }
    }

    private void verifyZygotesLoadedArtifacts() throws Exception {
        // There are potentially two zygote processes "zygote" and "zygote64". These are
        // instances 32-bit and 64-bit unspecialized app_process processes.
        // (frameworks/base/cmds/app_process).
        int zygoteCount = 0;
        for (String zygoteName : ZYGOTE_NAMES) {
            final Optional<Set<String>> mappedArtifacts = getZygoteLoadedArtifacts(zygoteName);
            if (!mappedArtifacts.isPresent()) {
                continue;
            }
            verifyZygoteLoadedArtifacts(zygoteName, mappedArtifacts.get());
            zygoteCount += 1;
        }
        assertTrue("No zygote processes found", zygoteCount > 0);
    }

    @Test
    public void verifyGeneratedArtifactsLoaded() throws Exception {
        // Checking zygote and system_server need the device have adb root to walk process maps.
        final boolean adbEnabled = getDevice().enableAdbRoot();
        assertTrue("ADB root failed and required to get process maps", adbEnabled);

        // Check there is a compilation log, we expect compilation to have occurred.
        assertTrue("Compilation log not found", haveCompilationLog());

        // Check both zygote and system_server processes to see that they have loaded the
        // artifacts compiled and signed by odrefresh and odsign. We check both here rather than
        // having a separate test because the device reboots between each @Test method and
        // that is an expensive use of time.
        verifyZygotesLoadedArtifacts();
        verifySystemServerLoadedArtifacts();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedAfterReboot() throws Exception {
        reboot();
        verifyGeneratedArtifactsLoaded();
    }

    /**
     * Checks the input line by line and replaces all lines that match the regex with the given
     * replacement.
     */
    String replaceLine(String input, String regex, String replacement) {
        StringBuffer output = new StringBuffer();
        Pattern p = Pattern.compile(regex);
        for (String line : input.split("\n")) {
            Matcher m = p.matcher(line);
            if (m.matches()) {
                m.appendReplacement(output, replacement);
                output.append("\n");
            } else {
                output.append(line + "\n");
            }
        }
        return output.toString();
    }

    /**
     * Simulates that there is an OTA that updates a boot classpath jar.
     */
    void simulateBootClasspathOta() throws Exception {
        String cacheInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        // Replace the cached checksum of /system/framework/framework.jar with "aaaaaaaa".
        cacheInfo = replaceLine(
                cacheInfo,
                "(.*/system/framework/framework\\.jar.*checksums=\").*?(\".*)",
                "$1aaaaaaaa$2");
        getDevice().pushString(cacheInfo, CACHE_INFO_FILE);
    }

    /**
     * Simulates that there is an OTA that updates a system server jar.
     */
    void simulateSystemServerOta() throws Exception {
        String cacheInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        // Replace the cached checksum of /system/framework/services.jar with "aaaaaaaa".
        cacheInfo = replaceLine(
                cacheInfo,
                "(.*/system/framework/services\\.jar.*checksums=\").*?(\".*)",
                "$1aaaaaaaa$2");
        getDevice().pushString(cacheInfo, CACHE_INFO_FILE);
    }

    /**
     * Simulates that an ART APEX has been upgraded.
     */
    void simulateArtApexUpgrade() throws Exception {
        String apexInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        // Replace the lastUpdateMillis of com.android.art with "1".
        apexInfo = replaceLine(
                apexInfo,
                "(.*com\\.android\\.art.*lastUpdateMillis=\").*?(\".*)",
                "$11$2");
        getDevice().pushString(apexInfo, CACHE_INFO_FILE);
    }

    /**
     * Simulates that an APEX has been upgraded. We could install a real APEX, but that would
     * introduce an extra dependency to this test, which we want to avoid.
     */
    void simulateApexUpgrade() throws Exception {
        String apexInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        // Replace the lastUpdateMillis of com.android.wifi with "1".
        apexInfo = replaceLine(
                apexInfo,
                "(.*com\\.android\\.wifi.*lastUpdateMillis=\").*?(\".*)",
                "$11$2");
        getDevice().pushString(apexInfo, CACHE_INFO_FILE);
    }

    long parseFormattedDateTime(String dateTimeStr) throws Exception {
        DateTimeFormatter formatter = DateTimeFormatter.ofPattern(
                "yyyy-MM-dd HH:mm:ss.nnnnnnnnn Z");
        ZonedDateTime zonedDateTime = ZonedDateTime.parse(dateTimeStr, formatter);
        return zonedDateTime.toInstant().toEpochMilli();
    }

    long getModifiedTimeMs(String filename) throws Exception {
        // We can't use the "-c '%.3Y'" flag when to get the timestamp because the Toybox's `stat`
        // implementation truncates the timestamp to seconds, which is not accurate enough, so we
        // use "-c '%%y'" and parse the time ourselves.
        String dateTimeStr = getDevice()
                .executeShellCommand(String.format("stat -c '%%y' '%s'", filename))
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    long getCurrentTimeMs() throws Exception {
        // We can't use getDevice().getDeviceDate() because it truncates the timestamp to seconds,
        // which is not accurate enough.
        String dateTimeStr = getDevice()
                .executeShellCommand("date +'%Y-%m-%d %H:%M:%S.%N %z'")
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    void assertArtifactsModifiedAfterBoot(Set<String> artifacts) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(
                    String.format(
                            "Artifact %s is not re-compiled. Modified time: %d, Boot time: %d",
                            artifact,
                            modifiedTime,
                            mBootTimeMs),
                    modifiedTime > mBootTimeMs);
        }
    }

    void assertArtifactsNotModifiedAfterBoot(Set<String> artifacts) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(
                    String.format(
                            "Artifact %s is unexpectedly re-compiled. " +
                                    "Modified time: %d, Boot time: %d",
                            artifact,
                            modifiedTime,
                            mBootTimeMs),
                    modifiedTime < mBootTimeMs);
        }
    }

    @Test
    public void verifyArtSamegradeUpdateTriggersCompilation() throws Exception {
        simulateArtApexUpgrade();
        removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command("odrefresh --compile");

        assertArtifactsModifiedAfterBoot(mZygoteArtifacts);
        assertArtifactsModifiedAfterBoot(mSystemServerArtifacts);
    }

    @Test
    public void verifyOtherApexSamegradeUpdateTriggersCompilation() throws Exception {
        simulateApexUpgrade();
        removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command("odrefresh --compile");

        assertArtifactsNotModifiedAfterBoot(mZygoteArtifacts);
        assertArtifactsModifiedAfterBoot(mSystemServerArtifacts);
    }

    @Test
    public void verifyBootClasspathOtaTriggersCompilation() throws Exception {
        simulateBootClasspathOta();
        removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command("odrefresh --compile");

        assertArtifactsModifiedAfterBoot(mZygoteArtifacts);
        assertArtifactsModifiedAfterBoot(mSystemServerArtifacts);
    }

    @Test
    public void verifySystemServerOtaTriggersCompilation() throws Exception {
        simulateSystemServerOta();
        removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command("odrefresh --compile");

        assertArtifactsNotModifiedAfterBoot(mZygoteArtifacts);
        assertArtifactsModifiedAfterBoot(mSystemServerArtifacts);
    }

    @Test
    public void verifyNoCompilationWhenCacheIsGood() throws Exception {
        removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command("odrefresh --compile");

        assertArtifactsNotModifiedAfterBoot(mZygoteArtifacts);
        assertArtifactsNotModifiedAfterBoot(mSystemServerArtifacts);
    }

    private boolean haveCompilationLog() throws Exception {
        CommandResult result =
                getDevice().executeShellV2Command("stat " + ODREFRESH_COMPILATION_LOG);
        return result.getExitCode() == 0;
    }

    private void removeCompilationLogToAvoidBackoff() throws Exception {
        getDevice().executeShellCommand("rm -f " + ODREFRESH_COMPILATION_LOG);
    }

    private void reboot() throws Exception {
        getDevice().reboot();
        boolean success = getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }
}
