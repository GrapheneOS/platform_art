/*
 * Copyright (C) 2023 The Android Open Source Project
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

import static org.junit.Assume.assumeTrue;

import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;

import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;

import java.util.HashSet;
import java.util.Set;

/**
 * This class tests odrefresh for the cases where all the APEXes are initially factory-installed.
 * Similar to OdrefreshHostTest, it does not involve odsign, fs-verity, and ART runtime.
 *
 * The tests are run by derived classes with different conditions: with and without the cache info.
 */
@Ignore("See derived classes")
abstract public class OdrefreshFactoryHostTestBase extends BaseHostJUnit4Test {
    protected OdsignTestUtils mTestUtils;
    protected DeviceState mDeviceState;

    @BeforeClassWithInfo
    public static void beforeClassWithDeviceBase(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        assumeTrue(testUtils.areAllApexesFactoryInstalled());
        testUtils.assertCommandSucceeds("disable-verity");
        testUtils.removeCompilationLogToAvoidBackoff();
        testUtils.reboot();
        testUtils.assertCommandSucceeds("remount");
    }

    @AfterClassWithInfo
    public static void afterClassWithDeviceBase(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.assertCommandSucceeds("enable-verity");
        testUtils.removeCompilationLogToAvoidBackoff();
        testUtils.reboot();
    }

    @Before
    public void setUpBase() throws Exception {
        mTestUtils = new OdsignTestUtils(getTestInformation());
        mDeviceState = new DeviceState(getTestInformation());
        mDeviceState.backupArtifacts();
    }

    @After
    public void tearDownBase() throws Exception {
        mDeviceState.restore();
    }

    @Test
    public void verifyArtSamegradeUpdateTriggersCompilation() throws Exception {
        mDeviceState.simulateArtApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should recompile everything.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.simulateArtApexUninstall();
        mTestUtils.runOdrefresh();

        // It should delete all compilation artifacts and update the cache info.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(mTestUtils.getZygotesExpectedArtifacts());
        mTestUtils.assertFilesNotExist(mTestUtils.getSystemServerExpectedArtifacts());
    }

    @Test
    public void verifyOtherApexSamegradeUpdateTriggersCompilation() throws Exception {
        mDeviceState.simulateApexUpgrade();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should only recompile system server.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(mTestUtils.getZygotesExpectedArtifacts());
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.simulateApexUninstall();
        mTestUtils.runOdrefresh();

        // It should delete all compilation artifacts and update the cache info.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(mTestUtils.getZygotesExpectedArtifacts());
        mTestUtils.assertFilesNotExist(mTestUtils.getSystemServerExpectedArtifacts());
    }

    @Test
    public void verifyMissingArtifactTriggersCompilation() throws Exception {
        // Simulate that an artifact is missing from /system.
        mDeviceState.backupAndDeleteFile(
                "/system/framework/oat/" + mTestUtils.getSystemServerIsa() + "/services.odex");

        mTestUtils.removeCompilationLogToAvoidBackoff();
        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        Set<String> expectedArtifacts = OdsignTestUtils.getApexDataDalvikCacheFilenames(
                "/system/framework/services.jar", mTestUtils.getSystemServerIsa());

        Set<String> nonExpectedArtifacts = new HashSet<>();
        nonExpectedArtifacts.addAll(mTestUtils.getZygotesExpectedArtifacts());
        nonExpectedArtifacts.addAll(mTestUtils.getSystemServerExpectedArtifacts());
        nonExpectedArtifacts.removeAll(expectedArtifacts);

        // It should only generate artifacts that are missing from /system.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(nonExpectedArtifacts);
        mTestUtils.assertModifiedAfter(expectedArtifacts, timeMs);

        mDeviceState.simulateArtApexUpgrade();
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should recompile everything.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.simulateArtApexUninstall();
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should only re-generate artifacts that are missing from /system.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(nonExpectedArtifacts);
        mTestUtils.assertModifiedAfter(expectedArtifacts, timeMs);
    }

    @Test
    public void verifyEnableUffdGcChangeTriggersCompilation() throws Exception {
        mDeviceState.setPhenotypeFlag("enable_uffd_gc", "true");

        long timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // It should recompile everything.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        // Run odrefresh again with the flag unchanged.
        timeMs = mTestUtils.getCurrentTimeMs();
        mTestUtils.runOdrefresh();

        // Nothing should change.
        mTestUtils.assertNotModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getZygotesExpectedArtifacts(), timeMs);
        mTestUtils.assertNotModifiedAfter(mTestUtils.getSystemServerExpectedArtifacts(), timeMs);

        mDeviceState.setPhenotypeFlag("enable_uffd_gc", null);

        mTestUtils.runOdrefresh();

        // It should delete all compilation artifacts and update the cache info.
        mTestUtils.assertModifiedAfter(Set.of(OdsignTestUtils.CACHE_INFO_FILE), timeMs);
        mTestUtils.assertFilesNotExist(mTestUtils.getZygotesExpectedArtifacts());
        mTestUtils.assertFilesNotExist(mTestUtils.getSystemServerExpectedArtifacts());
    }
}
