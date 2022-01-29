/*
 * Copyright 2022 The Android Open Source Project
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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.junit.Assume.assumeTrue;
import static org.junit.Assume.assumeFalse;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;
import com.android.tradefed.util.CommandResult;

import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Arrays;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Test to check if CompOS works properly.
 *
 * @see ActivationTest for actual tests
 * @see OnDeviceSigningHostTest for the same test with compilation happens in early boot
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CompOsSigningHostTest extends ActivationTest {

    private static final String COMPOSD_CMD_BIN = "/apex/com.android.compos/bin/composd_cmd";
    private static final String PENDING_ARTIFACTS_DIR =
            "/data/misc/apexdata/com.android.art/compos-pending";

    /** odrefresh is currently hard-coded to fail if it does not complete in 300 seconds. */
    private static final int ODREFRESH_MAX_SECONDS = 300;

    /** Waiting time before starting to check odrefresh progress. */
    private static final int SECONDS_BEFORE_PROGRESS_CHECK = 30;

    /** Job ID of the pending compilation with staged APEXes. */
    private static final String JOB_ID = "5132251";

    private static final String ORIGINAL_CHECKSUMS_KEY = "compos_test_orig_checksums";
    private static final String PENDING_CHECKSUMS_KEY = "compos_test_pending_checksums";

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        ITestDevice device = testInfo.getDevice();

        // TODO(216321149): enable when the bug is fixed.
        assumeFalse("VM fails to boot on user build", device.getBuildFlavor().endsWith("-user"));

        assumeCompOsPresent(device);

        testInfo.properties().put(ORIGINAL_CHECKSUMS_KEY,
                checksumDirectoryContentPartial(device,
                    "/data/misc/apexdata/com.android.art/dalvik-cache/"));

        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.installTestApex();

        // Once the test APK is installed, a CompilationJob is scheduled to run when certain
        // criteria are met, e.g. the device is charging and idle. Since we don't want to wait in
        // the test, here we start the job by ID immediately.
        assertCommandSucceeds(device, "cmd jobscheduler run android " + JOB_ID);
        // It takes time. Just don't spam.
        TimeUnit.SECONDS.sleep(SECONDS_BEFORE_PROGRESS_CHECK);
        // The job runs asynchronously. To wait until it completes.
        waitForJobExit(device, ODREFRESH_MAX_SECONDS - SECONDS_BEFORE_PROGRESS_CHECK);

        // Checks the output validity, then store the hashes of pending files.
        assertThat(device.getChildren(PENDING_ARTIFACTS_DIR)).asList().containsAtLeast(
                "cache-info.xml", "compos.info", "compos.info.signature");
        testInfo.properties().put(PENDING_CHECKSUMS_KEY,
                checksumDirectoryContentPartial(device,
                    "/data/misc/apexdata/com.android.art/compos-pending/"));

        testUtils.reboot();
    }

    @AfterClassWithInfo
    public static void afterClassWithDevice(TestInformation testInfo) throws Exception {
        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.uninstallTestApex();
        testUtils.reboot();
        testUtils.restoreAdbRoot();
    }

    @Test
    public void checkFileChecksums() throws Exception {
        String actualChecksums = checksumDirectoryContentPartial(getDevice(),
                "/data/misc/apexdata/com.android.art/dalvik-cache/");

        String pendingChecksums = getTestInformation().properties().get(PENDING_CHECKSUMS_KEY);
        assertThat(actualChecksums).isEqualTo(pendingChecksums);

        // With test apex, the output should be different.
        String originalChecksums = getTestInformation().properties().get(ORIGINAL_CHECKSUMS_KEY);
        assertThat(actualChecksums).isNotEqualTo(originalChecksums);
    }

    @Ignore("Implement timestamp check when possible. b/215589015")
    public void verifyFileTimestamps() {}

    @Ignore("Override base class. Due to b/211458160 and b/210998761.")
    public void verifyGeneratedArtifactsLoaded() {}

    @Ignore("Override base class. Due to b/211458160 and b/210998761.")
    public void verifyGeneratedArtifactsLoadedAfterPartialCompilation() {}

    @Ignore("Override base class. Due to b/211458160 and b/210998761.")
    public void verifyGeneratedArtifactsLoadedAfterReboot() {}

    private static String checksumDirectoryContentPartial(ITestDevice device, String path)
            throws Exception {
        // Sort by filename (second column) to make comparison easier.
        // Filter out compos.info* (which will be deleted at boot) and cache-info.xm
        // compos.info.signature since it's only generated by CompOS.
        // TODO(b/210473615): Remove irrelevant APEXes (i.e. those aren't contributing to the
        // classpaths, thus not in the VM) from cache-info.xml.
        return assertCommandSucceeds(device, "cd " + path + "; find -type f -exec sha256sum {} \\;"
                + "| grep -v cache-info.xml | grep -v compos.info"
                + "| sort -k2");
    }

    private static String assertCommandSucceeds(ITestDevice device, String command)
            throws DeviceNotAvailableException {
        CommandResult result = device.executeShellV2Command(command);
        assertWithMessage(result.toString()).that(result.getExitCode()).isEqualTo(0);
        return result.getStdout();
    }

    private static void waitForJobExit(ITestDevice device, int timeout)
            throws Exception {
        boolean started = false;
        for (int i = 0; i < timeout; i++) {
            CommandResult result = device.executeShellV2Command(
                    "cmd jobscheduler get-job-state android " + JOB_ID);
            String state = result.getStdout().toString();
            if (state.contains("ready") || state.contains("active")) {
                started = true;
                TimeUnit.SECONDS.sleep(1);
            } else if (state.startsWith("unknown")) {
                if (!started) {
                    // It's likely that the job hasn't been scheduled. So try again.
                    TimeUnit.SECONDS.sleep(1);
                    continue;
                } else {
                    // Job has completed
                    return;
                }
            } else {
                fail("Failing due to unexpected job state: " + result);
            }
        }
        fail("Timed out waiting for the job to complete");
    }

    public static void assumeCompOsPresent(ITestDevice device) throws Exception {
        // We have to have kernel support for a VM.
        assumeTrue(device.doesFileExist("/dev/kvm"));

        // And the CompOS APEX must be present.
        assumeTrue(device.doesFileExist("/apex/com.android.compos/"));
    }
}
