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

import static org.junit.Assert.fail;
import static org.junit.Assume.assumeTrue;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;
import com.android.tradefed.util.CommandResult;

import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.TimeUnit;

/**
 * Test to check if CompOS works properly.
 *
 * @see ActivationTest for actual tests
 * @see OnDeviceSigningHostTest for the same test with compilation happens in early boot
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CompOsSigningHostTest extends ActivationTest {

    private static final String PENDING_ARTIFACTS_DIR =
            "/data/misc/apexdata/com.android.art/compos-pending";

    /** odrefresh is currently hard-coded to fail if it does not complete in 300 seconds. */
    private static final int ODREFRESH_MAX_SECONDS = 300;

    /** Waiting time for the job to be scheduled after staging an APEX */
    private static final int JOB_CREATION_MAX_SECONDS = 5;

    /** Waiting time before starting to check odrefresh progress. */
    private static final int SECONDS_BEFORE_PROGRESS_CHECK = 30;

    /** Job ID of the pending compilation with staged APEXes. */
    private static final String JOB_ID = "5132251";

    private static final String ORIGINAL_CHECKSUMS_KEY = "compos_test_orig_checksums";
    private static final String PENDING_CHECKSUMS_KEY = "compos_test_pending_checksums";

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        ITestDevice device = testInfo.getDevice();

        assumeCompOsPresent(device);

        testInfo.properties().put(ORIGINAL_CHECKSUMS_KEY,
                checksumDirectoryContentPartial(device,
                    "/data/misc/apexdata/com.android.art/dalvik-cache/"));

        OdsignTestUtils testUtils = new OdsignTestUtils(testInfo);
        testUtils.installTestApex();

        // Once the test APK is installed, a CompilationJob is (asynchronously) scheduled to run
        // when certain criteria are met, e.g. the device is charging and idle. Since we don't
        // want to wait in the test, here we start the job by ID as soon as it is scheduled.
        waitForJobToBeScheduled(device, JOB_CREATION_MAX_SECONDS);
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

    @Ignore("Compilation log in CompOS isn't useful, and doesn't need to be generated")
    public void verifyCompilationLogGenerated() {}

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

    private static void waitForJobToBeScheduled(ITestDevice device, int timeout)
            throws Exception {
        for (int i = 0; i < timeout; i++) {
            CommandResult result = device.executeShellV2Command(
                    "cmd jobscheduler get-job-state android " + JOB_ID);
            String state = result.getStdout().toString();
            if (state.startsWith("unknown")) {
                // The job hasn't been scheduled yet. So try again.
                TimeUnit.SECONDS.sleep(1);
            } else if (result.getExitCode() != 0) {
                fail("Failing due to unexpected job state: " + result);
            } else {
                // The job exists, which is all we care about here
                return;
            }
        }
        fail("Timed out waiting for the job to be scheduled");
    }

    private static void waitForJobExit(ITestDevice device, int timeout)
            throws Exception {
        for (int i = 0; i < timeout; i++) {
            CommandResult result = device.executeShellV2Command(
                    "cmd jobscheduler get-job-state android " + JOB_ID);
            String state = result.getStdout().toString();
            if (state.contains("ready") || state.contains("active")) {
                TimeUnit.SECONDS.sleep(1);
            } else if (state.startsWith("unknown")) {
                // Job has completed
                return;
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
