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
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * Test to check if odrefresh, odsign, fs-verity, and ART runtime work together properly.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class OnDeviceSigningHostTest extends BaseHostJUnit4Test {
    private static final String TEST_APP_PACKAGE_NAME = "com.android.tests.odsign";
    private static final String TEST_APP_APK = "odsign_e2e_test_app.apk";

    private static OdsignTestUtils sTestUtils;

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        sTestUtils = new OdsignTestUtils(testInfo);
        sTestUtils.installTestApex();;
    }

    @AfterClassWithInfo
    public static void afterClassWithDevice(TestInformation testInfo) throws Exception {
        sTestUtils.uninstallTestApex();
    }

    @Test
    public void verifyArtUpgradeSignsFiles() throws Exception {
        installPackage(TEST_APP_APK);
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testArtArtifactsHaveFsverity");
        runDeviceTests(options);
    }

    @Test
    public void verifyArtUpgradeGeneratesRequiredArtifacts() throws Exception {
        installPackage(TEST_APP_APK);
        DeviceTestRunOptions options = new DeviceTestRunOptions(TEST_APP_PACKAGE_NAME);
        options.setTestClassName(TEST_APP_PACKAGE_NAME + ".ArtifactsSignedTest");
        options.setTestMethodName("testGeneratesRequiredArtArtifacts");
        runDeviceTests(options);
    }

    @Test
    public void verifyGeneratedArtifactsLoaded() throws Exception {
        // Checking zygote and system_server need the device have adb root to walk process maps.
        final boolean adbEnabled = getDevice().enableAdbRoot();
        assertTrue("ADB root failed and required to get process maps", adbEnabled);

        // Check there is a compilation log, we expect compilation to have occurred.
        assertTrue("Compilation log not found", sTestUtils.haveCompilationLog());

        // Check both zygote and system_server processes to see that they have loaded the
        // artifacts compiled and signed by odrefresh and odsign. We check both here rather than
        // having a separate test because the device reboots between each @Test method and
        // that is an expensive use of time.
        verifyZygotesLoadedArtifacts();
        verifySystemServerLoadedArtifacts();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedAfterReboot() throws Exception {
        sTestUtils.reboot();
        verifyGeneratedArtifactsLoaded();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedAfterPartialCompilation() throws Exception {
        Set<String> mappedArtifacts = sTestUtils.getSystemServerLoadedArtifacts();
        // Delete an arbitrary artifact.
        getDevice().deleteFile(mappedArtifacts.iterator().next());
        sTestUtils.removeCompilationLogToAvoidBackoff();
        sTestUtils.reboot();
        verifyGeneratedArtifactsLoaded();
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

        final Set<String> mappedArtifacts = sTestUtils.getSystemServerLoadedArtifacts();
        assertTrue(
                "No mapped artifacts under " + OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String isaCacheDirectory =
                String.format("%s/%s", OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME, isa);

        // Check components in the system_server classpath have mapped artifacts.
        for (String element : classpathElements) {
          String escapedPath = element.substring(1).replace('/', '@');
          for (String extension : OdsignTestUtils.APP_ARTIFACT_EXTENSIONS) {
            final String fullArtifactPath =
                    String.format("%s/%s@classes%s", isaCacheDirectory, escapedPath, extension);
            assertTrue("Missing " + fullArtifactPath, mappedArtifacts.contains(fullArtifactPath));
          }
        }

        for (String mappedArtifact : mappedArtifacts) {
          // Check the mapped artifact has a .art, .odex or .vdex extension.
          final boolean knownArtifactKind =
                    OdsignTestUtils.APP_ARTIFACT_EXTENSIONS.stream().anyMatch(
                            e -> mappedArtifact.endsWith(e));
          assertTrue("Unknown artifact kind: " + mappedArtifact, knownArtifactKind);
        }
    }

    private void verifyZygoteLoadedArtifacts(String zygoteName, Set<String> mappedArtifacts)
            throws Exception {
        final String bootExtensionName = "boot-framework";

        assertTrue("Expect 3 boot-framework artifacts", mappedArtifacts.size() == 3);

        String allArtifacts = mappedArtifacts.stream().collect(Collectors.joining(","));
        for (String extension : OdsignTestUtils.BCP_ARTIFACT_EXTENSIONS) {
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
        for (String zygoteName : OdsignTestUtils.ZYGOTE_NAMES) {
            final Optional<Set<String>> mappedArtifacts =
                    sTestUtils.getZygoteLoadedArtifacts(zygoteName);
            if (!mappedArtifacts.isPresent()) {
                continue;
            }
            verifyZygoteLoadedArtifacts(zygoteName, mappedArtifacts.get());
            zygoteCount += 1;
        }
        assertTrue("No zygote processes found", zygoteCount > 0);
    }
}
