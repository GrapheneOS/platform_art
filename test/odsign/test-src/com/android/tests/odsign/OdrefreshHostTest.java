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

import static org.junit.Assert.assertTrue;

import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

/**
 * Test to check end-to-end odrefresh invocations, but without odsign, fs-verity, and ART runtime
 * involved.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class OdrefreshHostTest extends BaseHostJUnit4Test {
    private static final String CACHE_INFO_FILE =
            OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME + "/cache-info.xml";
    private static final String ODREFRESH_COMMAND =
            "odrefresh --partial-compilation --no-refresh --compile";

    private static OdsignTestUtils sTestUtils;

    private static Set<String> sZygoteArtifacts;
    private static Set<String> sSystemServerArtifacts;

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        sTestUtils = new OdsignTestUtils(testInfo);
        sTestUtils.installTestApex();

        sZygoteArtifacts = new HashSet<>();
        for (String zygoteName : sTestUtils.ZYGOTE_NAMES) {
            sZygoteArtifacts.addAll(
                    sTestUtils.getZygoteLoadedArtifacts(zygoteName).orElse(new HashSet<>()));
        }
        sSystemServerArtifacts = sTestUtils.getSystemServerLoadedArtifacts();
    }

    @AfterClassWithInfo
    public static void afterClassWithDevice(TestInformation testInfo) throws Exception {
        sTestUtils.uninstallTestApex();
    }

    @Test
    public void verifyArtSamegradeUpdateTriggersCompilation() throws Exception {
        simulateArtApexUpgrade();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsModifiedAfter(sZygoteArtifacts, timeMs);
        assertArtifactsModifiedAfter(sSystemServerArtifacts, timeMs);
    }

    @Test
    public void verifyOtherApexSamegradeUpdateTriggersCompilation() throws Exception {
        simulateApexUpgrade();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsNotModifiedAfter(sZygoteArtifacts, timeMs);
        assertArtifactsModifiedAfter(sSystemServerArtifacts, timeMs);
    }

    @Test
    public void verifyBootClasspathOtaTriggersCompilation() throws Exception {
        simulateBootClasspathOta();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsModifiedAfter(sZygoteArtifacts, timeMs);
        assertArtifactsModifiedAfter(sSystemServerArtifacts, timeMs);
    }

    @Test
    public void verifySystemServerOtaTriggersCompilation() throws Exception {
        simulateSystemServerOta();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsNotModifiedAfter(sZygoteArtifacts, timeMs);
        assertArtifactsModifiedAfter(sSystemServerArtifacts, timeMs);
    }

    @Test
    public void verifyMissingArtifactTriggersCompilation() throws Exception {
        Set<String> missingArtifacts = simulateMissingArtifacts();
        Set<String> remainingArtifacts = new HashSet<>();
        remainingArtifacts.addAll(sZygoteArtifacts);
        remainingArtifacts.addAll(sSystemServerArtifacts);
        remainingArtifacts.removeAll(missingArtifacts);

        sTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsNotModifiedAfter(remainingArtifacts, timeMs);
        assertArtifactsModifiedAfter(missingArtifacts, timeMs);
    }

    @Test
    public void verifyNoCompilationWhenCacheIsGood() throws Exception {
        sTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = getCurrentTimeMs();
        getDevice().executeShellV2Command(ODREFRESH_COMMAND);

        assertArtifactsNotModifiedAfter(sZygoteArtifacts, timeMs);
        assertArtifactsNotModifiedAfter(sSystemServerArtifacts, timeMs);
    }

    /**
     * Checks the input line by line and replaces all lines that match the regex with the given
     * replacement.
     */
    private String replaceLine(String input, String regex, String replacement) {
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
    private void simulateBootClasspathOta() throws Exception {
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
    private void simulateSystemServerOta() throws Exception {
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
    private void simulateArtApexUpgrade() throws Exception {
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
    private void simulateApexUpgrade() throws Exception {
        String apexInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        // Replace the lastUpdateMillis of com.android.wifi with "1".
        apexInfo = replaceLine(
                apexInfo,
                "(.*com\\.android\\.wifi.*lastUpdateMillis=\").*?(\".*)",
                "$11$2");
        getDevice().pushString(apexInfo, CACHE_INFO_FILE);
    }

    private Set<String> simulateMissingArtifacts() throws Exception {
        Set<String> missingArtifacts = new HashSet<>();
        String sample = sSystemServerArtifacts.iterator().next();
        for (String extension : OdsignTestUtils.APP_ARTIFACT_EXTENSIONS) {
            String artifact = replaceExtension(sample, extension);
            getDevice().deleteFile(artifact);
            missingArtifacts.add(artifact);
        }
        return missingArtifacts;
    }

    private long parseFormattedDateTime(String dateTimeStr) throws Exception {
        DateTimeFormatter formatter = DateTimeFormatter.ofPattern(
                "yyyy-MM-dd HH:mm:ss.nnnnnnnnn Z");
        ZonedDateTime zonedDateTime = ZonedDateTime.parse(dateTimeStr, formatter);
        return zonedDateTime.toInstant().toEpochMilli();
    }

    private long getModifiedTimeMs(String filename) throws Exception {
        // We can't use the "-c '%.3Y'" flag when to get the timestamp because the Toybox's `stat`
        // implementation truncates the timestamp to seconds, which is not accurate enough, so we
        // use "-c '%%y'" and parse the time ourselves.
        String dateTimeStr = getDevice()
                .executeShellCommand(String.format("stat -c '%%y' '%s'", filename))
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    private long getCurrentTimeMs() throws Exception {
        // We can't use getDevice().getDeviceDate() because it truncates the timestamp to seconds,
        // which is not accurate enough.
        String dateTimeStr = getDevice()
                .executeShellCommand("date +'%Y-%m-%d %H:%M:%S.%N %z'")
                .trim();
        return parseFormattedDateTime(dateTimeStr);
    }

    private void assertArtifactsModifiedAfter(Set<String> artifacts, long timeMs) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(
                    String.format(
                            "Artifact %s is not re-compiled. Modified time: %d, Reference time: %d",
                            artifact,
                            modifiedTime,
                            timeMs),
                    modifiedTime > timeMs);
        }
    }

    private void assertArtifactsNotModifiedAfter(Set<String> artifacts, long timeMs)
            throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(
                    String.format(
                            "Artifact %s is unexpectedly re-compiled. " +
                                    "Modified time: %d, Reference time: %d",
                            artifact,
                            modifiedTime,
                            timeMs),
                    modifiedTime < timeMs);
        }
    }

    private String replaceExtension(String filename, String extension) throws Exception {
        int index = filename.lastIndexOf(".");
        assertTrue("Extension not found in filename: " + filename, index != -1);
        return filename.substring(0, index) + extension;
    }
}
