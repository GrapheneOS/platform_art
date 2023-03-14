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
import static com.google.common.truth.Truth.assertWithMessage;

import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.HashSet;
import java.util.Set;

/**
 * Test to check end-to-end odrefresh invocations, but without odsign, fs-verity, and ART runtime
 * involved.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class OdrefreshHostTest extends BaseHostJUnit4Test {
    private OdsignTestUtils mTestUtils;
    private DeviceState mDeviceState;

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.installTestApex();
        testUtils.reboot();
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
        mDeviceState = new DeviceState(getTestInformation());
        mDeviceState.backupArtifacts();
    }

    @After
    public void tearDown() throws Exception {
        mDeviceState.restore();
    }

    @Test
    public void verifyArtSamegradeUpdateTriggersCompilation() throws Exception {
        mDeviceState.simulateArtApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyOtherApexSamegradeUpdateTriggersCompilation() throws Exception {
        mDeviceState.simulateApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyBootClasspathOtaTriggersCompilation() throws Exception {
        mDeviceState.simulateBootClasspathOta();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifySystemServerOtaTriggersCompilation() throws Exception {
        mDeviceState.simulateSystemServerOta();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyMissingArtifactTriggersCompilation() throws Exception {
        Set<String> missingArtifacts = simulateMissingArtifacts();
        Set<String> remainingArtifacts = new HashSet<>();
        remainingArtifacts.addAll(mTestUtils.getZygotesExpectedArtifacts());
        remainingArtifacts.addAll(mTestUtils.getSystemServerExpectedArtifacts());
        remainingArtifacts.removeAll(missingArtifacts);

        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertNotModifiedAfter(remainingArtifacts, timeMs);
        mTestUtils.assertModifiedAfter(missingArtifacts, timeMs);
    }

    @Test
    public void verifyEnableUffdGcChangeTriggersCompilation() throws Exception {
        mDeviceState.setPhenotypeFlag("enable_uffd_gc", "false");

        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.setPhenotypeFlag("enable_uffd_gc", "true");

        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run odrefresh again with the flag unchanged.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.setPhenotypeFlag("enable_uffd_gc", null);

        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifySystemServerCompilerFilterOverrideChangeTriggersCompilation()
            throws Exception {
        mDeviceState.setPhenotypeFlag("systemservercompilerfilter_override", null);

        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.setPhenotypeFlag("systemservercompilerfilter_override", "speed");

        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run odrefresh again with the flag unchanged.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.setPhenotypeFlag("systemservercompilerfilter_override", "verify");

        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifySystemPropertyMismatchTriggersCompilation() throws Exception {
        // Change a system property from empty to a value.
        mDeviceState.setProperty("dalvik.vm.foo", "1");
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Change the system property to another value.
        mDeviceState.setProperty("dalvik.vm.foo", "2");
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Change the system property to empty.
        mDeviceState.setProperty("dalvik.vm.foo", "");
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should be re-compiled.
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run again with the same value.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts should not be re-compiled.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyNoCompilationWhenCacheIsGood() throws Exception {
        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyUnexpectedFilesAreCleanedUp() throws Exception {
        String unexpected = OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME + "/unexpected";
        getDevice().pushString("" /* contents */, unexpected);
        mTestUtils.runOdrefresh();

        assertThat(getDevice().doesFileExist(unexpected)).isFalse();
    }

    @Test
    public void verifyCacheInfoOmitsIrrelevantApexes() throws Exception {
        String cacheInfo = getDevice().pullFileContents(OdsignTestUtils.CACHE_INFO_FILE);

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
        mDeviceState.simulateApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh("--compilation-os-mode");

        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        String cacheInfo = getDevice().pullFileContents(OdsignTestUtils.CACHE_INFO_FILE);
        assertThat(cacheInfo).contains("compilationOsMode=\"true\"");

        // Compilation OS does not write the compilation log to the host.
        mTestUtils.removeCompilationLogToAvoidBackoff();

        // Simulate the odrefresh invocation on the next boot.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // odrefresh should not re-compile anything.
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyMinimalCompilation() throws Exception {
        mTestUtils.removeCompilationLogToAvoidBackoff();
        getDevice().executeShellV2Command(
                "rm -rf " + OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME);
        mTestUtils.runOdrefresh("--minimal");

        mTestUtils.restartZygote();

        // The minimal boot image should be loaded.
        mTestUtils.verifyZygotesLoadedArtifacts("boot_minimal");

        // Running the command again should not overwrite the minimal boot image.
        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh("--minimal");

        Set<String> minimalZygoteArtifacts = mTestUtils.getZygotesExpectedArtifacts("boot_minimal");
        mTestUtils.assertNotModifiedAfter(minimalZygoteArtifacts, timeMs);

        // A normal odrefresh invocation should replace the minimal boot image with a full one.
        mTestUtils.removeCompilationLogToAvoidBackoff();
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        for (String artifact : minimalZygoteArtifacts) {
            assertWithMessage(
                    String.format(
                            "Artifact %s should be cleaned up while it still exists", artifact))
                    .that(getDevice().doesFileExist(artifact))
                    .isFalse();
        }

        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
    }

    @Test
    public void verifyCompilationFailureBackoff() throws Exception {
        mDeviceState.makeDex2oatFail();
        mDeviceState.simulateArtApexUpgrade();

        // Run odrefresh. It should encounter dex2oat failures.
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Artifacts don't exist because the compilation failed.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(mTestUtils.getZygotesExpectedArtifacts());
        mTestUtils.assertFilesNotExist(mTestUtils.getSystemServerExpectedArtifacts());

        // Run odrefresh again.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should not retry.
        mTestUtils.assertNotModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);

        // Simulate that the backoff time has passed.
        mTestUtils.removeCompilationLogToAvoidBackoff();

        // Run odrefresh again.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Now it should retry.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
    }

    private Set<String> simulateMissingArtifacts() throws Exception {
        Set<String> missingArtifacts = new HashSet<>();
        String sample = mTestUtils.getSystemServerExpectedArtifacts().iterator().next();
        for (String extension : OdsignTestUtils.APP_ARTIFACT_EXTENSIONS) {
            String artifact = OdsignTestUtils.replaceExtension(sample, extension);
            getDevice().deleteFile(artifact);
            missingArtifacts.add(artifact);
        }
        return missingArtifacts;
    }
}
