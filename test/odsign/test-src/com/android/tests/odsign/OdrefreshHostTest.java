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

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Test to check end-to-end odrefresh invocations, but without odsign, fs-verity, and ART runtime
 * involved.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class OdrefreshHostTest extends BaseHostJUnit4Test {
    private static final String CACHE_INFO_FILE =
            OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME + "/cache-info.xml";
    private static final String ODREFRESH_BIN = "odrefresh";

    private static final String TAG = "OdrefreshHostTest";
    private static final String ZYGOTE_ARTIFACTS_KEY = TAG + ":ZYGOTE_ARTIFACTS";
    private static final String SYSTEM_SERVER_ARTIFACTS_KEY = TAG + ":SYSTEM_SERVER_ARTIFACTS";

    private OdsignTestUtils mTestUtils;

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.installTestApex();
        testUtils.reboot();

        HashSet<String> zygoteArtifacts = new HashSet<>();
        for (String zygoteName : testUtils.ZYGOTE_NAMES) {
            zygoteArtifacts.addAll(
                    testUtils.getZygoteLoadedArtifacts(zygoteName).orElse(new HashSet<>()));
        }
        Set<String> systemServerArtifacts = testUtils.getSystemServerLoadedArtifacts();

        testInfo.properties().put(ZYGOTE_ARTIFACTS_KEY, String.join(":", zygoteArtifacts));
        testInfo.properties()
                .put(SYSTEM_SERVER_ARTIFACTS_KEY, String.join(":", systemServerArtifacts));
    }

    @AfterClassWithInfo
    public static void afterClassWithDevice(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.uninstallTestApex();
        testUtils.reboot();
    }

    @Before
    public void setUp() throws Exception {
        mTestUtils = new OdsignTestUtils(getTestInformation());
    }

    @After
    public void tearDown() throws Exception {
        Set<String> artifacts = new HashSet<>();
        artifacts.addAll(getZygoteArtifacts());
        artifacts.addAll(getSystemServerArtifacts());

        for (String artifact : artifacts) {
            if (!getDevice().doesFileExist(artifact)) {
                // Things went wrong during the test. Run odrefresh to revert to a normal state.
                mTestUtils.removeCompilationLogToAvoidBackoff();
                runOdrefresh();
                break;
            }
        }
    }

    @Test
    public void verifyArtSamegradeUpdateTriggersCompilation() throws Exception {
        simulateArtApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyOtherApexSamegradeUpdateTriggersCompilation() throws Exception {
        simulateApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyBootClasspathOtaTriggersCompilation() throws Exception {
        simulateBootClasspathOta();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifySystemServerOtaTriggersCompilation() throws Exception {
        simulateSystemServerOta();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyMissingArtifactTriggersCompilation() throws Exception {
        Set<String> missingArtifacts = simulateMissingArtifacts();
        Set<String> remainingArtifacts = new HashSet<>();
        remainingArtifacts.addAll(getZygoteArtifacts());
        remainingArtifacts.addAll(getSystemServerArtifacts());
        remainingArtifacts.removeAll(missingArtifacts);

        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsNotModifiedAfter(remainingArtifacts, timeMs);
        assertArtifactsModifiedAfter(missingArtifacts, timeMs);
    }

    @Test
    public void verifyEnableUffdGcChangeTriggersCompilation() throws Exception {
        try {
            // Disable phenotype flag syncing. Potentially, we can set
            // `set_sync_disabled_for_tests` to `until_reboot`, but setting it to
            // `persistent` prevents unrelated system crashes/restarts from affecting the
            // test. `set_sync_disabled_for_tests` is reset in the `finally` block anyway.
            getDevice().executeShellV2Command(
                    "device_config set_sync_disabled_for_tests persistent");

            // Simulate that the phenotype flag is set to the default value.
            getDevice().executeShellV2Command(
                    "device_config put runtime_native_boot enable_uffd_gc false");

            long timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should not be re-compiled.
            assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Simulate that the phenotype flag is set to true.
            getDevice().executeShellV2Command(
                    "device_config put runtime_native_boot enable_uffd_gc true");

            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should be re-compiled.
            assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Run odrefresh again with the flag unchanged.
            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should not be re-compiled.
            assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Simulate that the phenotype flag is set to false.
            getDevice().executeShellV2Command(
                    "device_config put runtime_native_boot enable_uffd_gc false");

            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should be re-compiled.
            assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
        } finally {
            getDevice().executeShellV2Command("device_config set_sync_disabled_for_tests none");
            getDevice().executeShellV2Command(
                    "device_config delete runtime_native_boot enable_uffd_gc");
        }
    }

    @Test
    public void verifySystemServerCompilerFilterOverrideChangeTriggersCompilation()
            throws Exception {
        try {
            // Disable phenotype flag syncing. Potentially, we can set
            // `set_sync_disabled_for_tests` to `until_reboot`, but setting it to
            // `persistent` prevents unrelated system crashes/restarts from affecting the
            // test. `set_sync_disabled_for_tests` is reset in the `finally` block anyway.
            getDevice()
                    .executeShellV2Command("device_config set_sync_disabled_for_tests persistent");

            // Simulate that the phenotype flag is set to the default value.
            getDevice()
                    .executeShellV2Command(
                            "device_config put runtime_native_boot"
                                + " systemservercompilerfilter_override");

            long timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should not be re-compiled.
            assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Simulate that the phenotype flag is set to "speed".
            getDevice()
                    .executeShellV2Command(
                            "device_config put runtime_native_boot"
                                + " systemservercompilerfilter_override speed");

            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should be re-compiled.
            assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Run odrefresh again with the flag unchanged.
            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should not be re-compiled.
            assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

            // Simulate that the phenotype flag is set to "verify".
            getDevice()
                    .executeShellV2Command(
                            "device_config put runtime_native_boot"
                                + " systemservercompilerfilter_override verify");

            timeMs = mTestUtils.getCurrentTimeMs();
            runOdrefresh();

            // Artifacts should be re-compiled.
            assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
            assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);
        } finally {
            getDevice().executeShellV2Command("device_config set_sync_disabled_for_tests none");
            getDevice()
                    .executeShellV2Command(
                            "device_config delete runtime_native_boot"
                                + " systemservercompilerfilter_override");
        }
    }

    @Test
    public void verifySystemPropertyMismatchTriggersCompilation() throws Exception {
        // Change a system property from empty to a value.
        getDevice().setProperty("dalvik.vm.foo", "1");
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should be re-compiled.
        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should not be re-compiled.
        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

        // Change the system property to another value.
        getDevice().setProperty("dalvik.vm.foo", "2");
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should be re-compiled.
        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should not be re-compiled.
        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);

        // Change the system property to empty.
        getDevice().setProperty("dalvik.vm.foo", "");
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should be re-compiled.
        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // Artifacts should not be re-compiled.
        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyNoCompilationWhenCacheIsGood() throws Exception {
        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyUnexpectedFilesAreCleanedUp() throws Exception {
        String unexpected = OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME + "/unexpected";
        getDevice().pushString("" /* contents */, unexpected);
        runOdrefresh();

        assertFalse(getDevice().doesFileExist(unexpected));
    }

    @Test
    public void verifyCacheInfoOmitsIrrelevantApexes() throws Exception {
        String cacheInfo = getDevice().pullFileContents(CACHE_INFO_FILE);

        // cacheInfo should list all APEXes that have compilable JARs and
        // none that do not.

        // This should always contain classpath JARs, that's the reason it exists.
        assertThat(cacheInfo).contains("name=\"com.android.sdkext\"");

        // This should never contain classpath JARs, it's the native runtime.
        assertThat(cacheInfo).doesNotContain("name=\"com.android.runtime\"");
    }

    @Test
    public void verifyCompilationOsMode() throws Exception {
        mTestUtils.removeCompilationLogToAvoidBackoff();
        simulateApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh("--compilation-os-mode");

        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsModifiedAfter(getSystemServerArtifacts(), timeMs);

        String cacheInfo = getDevice().pullFileContents(CACHE_INFO_FILE);
        assertThat(cacheInfo).contains("compilationOsMode=\"true\"");

        // Compilation OS does not write the compilation log to the host.
        mTestUtils.removeCompilationLogToAvoidBackoff();

        // Simulate the odrefresh invocation on the next boot.
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        // odrefresh should not re-compile anything.
        assertArtifactsNotModifiedAfter(getZygoteArtifacts(), timeMs);
        assertArtifactsNotModifiedAfter(getSystemServerArtifacts(), timeMs);
    }

    @Test
    public void verifyMinimalCompilation() throws Exception {
        mTestUtils.removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command(
            "rm -rf " + OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME);
        runOdrefresh("--minimal");

        mTestUtils.restartZygote();

        // The minimal boot image should be loaded.
        Set<String> minimalZygoteArtifacts =
                mTestUtils.verifyZygotesLoadedArtifacts("boot_minimal");

        // Running the command again should not overwrite the minimal boot image.
        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh("--minimal");

        assertArtifactsNotModifiedAfter(minimalZygoteArtifacts, timeMs);

        // A normal odrefresh invocation should replace the minimal boot image with a full one.
        mTestUtils.removeCompilationLogToAvoidBackoff();
        timeMs = mTestUtils.getCurrentTimeMs();
        runOdrefresh();

        for (String artifact : minimalZygoteArtifacts) {
            assertFalse(
                    String.format(
                            "Artifact %s should be cleaned up while it still exists", artifact),
                    getDevice().doesFileExist(artifact));
        }

        assertArtifactsModifiedAfter(getZygoteArtifacts(), timeMs);
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
        String sample = getSystemServerArtifacts().iterator().next();
        for (String extension : OdsignTestUtils.APP_ARTIFACT_EXTENSIONS) {
            String artifact = replaceExtension(sample, extension);
            getDevice().deleteFile(artifact);
            missingArtifacts.add(artifact);
        }
        return missingArtifacts;
    }

    private void assertArtifactsModifiedAfter(Set<String> artifacts, long timeMs) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = mTestUtils.getModifiedTimeMs(artifact);
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
            long modifiedTime = mTestUtils.getModifiedTimeMs(artifact);
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

    private Set<String> getColonSeparatedSet(String key) {
        String value = getTestInformation().properties().get(key);
        if (value == null || value.isEmpty()) {
            return new HashSet<>();
        }
        return new HashSet<>(Arrays.asList(value.split(":")));
    }

    private Set<String> getZygoteArtifacts() {
        return getColonSeparatedSet(ZYGOTE_ARTIFACTS_KEY);
    }

    private Set<String> getSystemServerArtifacts() {
        return getColonSeparatedSet(SYSTEM_SERVER_ARTIFACTS_KEY);
    }

    private void runOdrefresh() throws Exception {
        runOdrefresh("" /* extraArgs */);
    }

    private void runOdrefresh(String extraArgs) throws Exception {
        getDevice().executeShellV2Command(ODREFRESH_BIN + " --check");
        getDevice().executeShellV2Command(
                ODREFRESH_BIN + " --partial-compilation --no-refresh " + extraArgs + " --compile");
    }
}
