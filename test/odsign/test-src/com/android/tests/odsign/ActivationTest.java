/*
 * Copyright (C) 2022 The Android Open Source Project
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

import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.DeviceTestRunOptions;

import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Arrays;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Test to check if odrefresh, odsign, fs-verity, and ART runtime work together properly.
 */
@Ignore("See derived classes, each produces the file for running the following verification")
abstract class ActivationTest extends BaseHostJUnit4Test {
    private static final String TEST_APP_PACKAGE_NAME = "com.android.tests.odsign";
    private static final String TEST_APP_APK = "odsign_e2e_test_app.apk";

    private OdsignTestUtils mTestUtils;

    @Before
    public void setUp() throws Exception {
        mTestUtils = new OdsignTestUtils(getTestInformation());
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
    public void verifyCompilationLogGenerated() throws Exception {
        mTestUtils.enableAdbRootOrSkipTest();

        // Check there is a compilation log, we expect compilation to have occurred.
        assertTrue("Compilation log not found", mTestUtils.haveCompilationLog());
    }

    @Test
    public void verifyGeneratedArtifactsLoaded() throws Exception {
        // Checking zygote and system_server need the device have adb root to walk process maps.
        mTestUtils.enableAdbRootOrSkipTest();

        // Check both zygote and system_server processes to see that they have loaded the
        // artifacts compiled and signed by odrefresh and odsign. We check both here rather than
        // having a separate test because the device reboots between each @Test method and
        // that is an expensive use of time.
        verifyZygotesLoadedArtifacts();
        verifySystemServerLoadedArtifacts();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedAfterReboot() throws Exception {
        mTestUtils.enableAdbRootOrSkipTest();

        mTestUtils.reboot();
        verifyGeneratedArtifactsLoaded();
    }

    @Test
    public void verifyGeneratedArtifactsLoadedAfterPartialCompilation() throws Exception {
        mTestUtils.enableAdbRootOrSkipTest();

        Set<String> mappedArtifacts = mTestUtils.getSystemServerLoadedArtifacts();
        // Delete an arbitrary artifact.
        getDevice().deleteFile(mappedArtifacts.iterator().next());
        mTestUtils.removeCompilationLogToAvoidBackoff();
        mTestUtils.reboot();
        verifyGeneratedArtifactsLoaded();
    }

    private String[] getListFromEnvironmentVariable(String name) throws Exception {
        String systemServerClasspath = getDevice().executeShellCommand("echo $" + name).trim();
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

    private void verifySystemServerLoadedArtifacts() throws Exception {
        String[] classpathElements = getListFromEnvironmentVariable("SYSTEMSERVERCLASSPATH");
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);
        String[] standaloneJars = getListFromEnvironmentVariable("STANDALONE_SYSTEMSERVER_JARS");
        String[] allSystemServerJars = Stream
                .concat(Arrays.stream(classpathElements), Arrays.stream(standaloneJars))
                .toArray(String[]::new);

        final Set<String> mappedArtifacts = mTestUtils.getSystemServerLoadedArtifacts();
        assertTrue(
                "No mapped artifacts under " + OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME,
                mappedArtifacts.size() > 0);
        final String isa = getSystemServerIsa(mappedArtifacts.iterator().next());
        final String isaCacheDirectory =
                String.format("%s/%s", OdsignTestUtils.ART_APEX_DALVIK_CACHE_DIRNAME, isa);

        // Check components in the system_server classpath have mapped artifacts.
        for (String element : allSystemServerJars) {
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
        final String bootImageStem = "boot";

        assertTrue("Expect 3 bootclasspath artifacts", mappedArtifacts.size() == 3);

        String allArtifacts = mappedArtifacts.stream().collect(Collectors.joining(","));
        for (String extension : OdsignTestUtils.BCP_ARTIFACT_EXTENSIONS) {
            final String artifact = bootImageStem + extension;
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
                    mTestUtils.getZygoteLoadedArtifacts(zygoteName);
            if (!mappedArtifacts.isPresent()) {
                continue;
            }
            verifyZygoteLoadedArtifacts(zygoteName, mappedArtifacts.get());
            zygoteCount += 1;
        }
        assertTrue("No zygote processes found", zygoteCount > 0);
    }
}
