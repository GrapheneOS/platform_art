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

package android.test.hostside;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.AfterClassWithInfo;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.testtype.junit4.BeforeClassWithInfo;
import com.android.tradefed.util.CommandResult;
import com.google.common.io.ByteStreams;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Test libnativeloader behavior for apps and libs in various partitions by overlaying them over
 * the system partitions. Requires root.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class LibnativeloaderTest extends BaseHostJUnit4Test {
    private static final String TAG = "LibnativeloaderTest";
    private static final String CLEANUP_PATHS_KEY = TAG + ":CLEANUP_PATHS";
    private static final String LOG_FILE_NAME = "TestActivity.log";

    @BeforeClassWithInfo
    public static void beforeClassWithDevice(TestInformation testInfo) throws Exception {
        DeviceContext ctx = new DeviceContext(testInfo.getDevice());

        // A soft reboot is slow, so do setup for all tests and reboot once.

        ctx.mDevice.remountSystemWritable();
        try (ZipFile libApk = openLibContainerApk()) {
            ctx.pushSystemOemLibs(libApk);
            ctx.pushProductLibs(libApk);
        }

        // "Install" apps in various partitions through plain adb push. We need them in these
        // locations to test library loading restrictions, so we cannot use
        // ITestDevice.installPackage for it since it only installs in /data.

        // For testSystemApp
        ctx.pushResource("/loadlibrarytest_system_app.apk",
                "/system/app/loadlibrarytest_system_app/loadlibrarytest_system_app.apk");

        // For testVendorApp
        ctx.pushResource("/loadlibrarytest_vendor_app.apk",
                "/vendor/app/loadlibrarytest_vendor_app/loadlibrarytest_vendor_app.apk");

        ctx.softReboot();

        testInfo.properties().put(CLEANUP_PATHS_KEY, ctx.mCleanup.getPathList());
    }

    @AfterClassWithInfo
    public static void afterClassWithDevice(TestInformation testInfo) throws Exception {
        String cleanupPathList = testInfo.properties().get(CLEANUP_PATHS_KEY);
        CleanupPaths cleanup = new CleanupPaths(testInfo.getDevice(), cleanupPathList);
        cleanup.cleanup();
    }

    @Test
    public void testSystemApp() throws Exception {
        runDeviceTests("android.test.app.system", "android.test.app.SystemAppTest");
    }

    @Test
    public void testVendorApp() throws Exception {
        runDeviceTests("android.test.app.vendor", "android.test.app.VendorAppTest");
    }

    // Utility class that keeps track of a set of paths the need to be deleted after testing.
    private static class CleanupPaths {
        private ITestDevice mDevice;
        private List<String> mCleanupPaths;

        CleanupPaths(ITestDevice device) {
            mDevice = device;
            mCleanupPaths = new ArrayList<String>();
        }

        CleanupPaths(ITestDevice device, String pathList) {
            mDevice = device;
            mCleanupPaths = Arrays.asList(pathList.split(":"));
        }

        String getPathList() { return String.join(":", mCleanupPaths); }

        // Adds the given path, or its topmost nonexisting parent directory, to the list of paths to
        // clean up.
        void addPath(String devicePath) throws DeviceNotAvailableException {
            File path = new File(devicePath);
            while (true) {
                File parentPath = path.getParentFile();
                if (parentPath == null || mDevice.doesFileExist(parentPath.toString())) {
                    break;
                }
                path = parentPath;
            }
            String nonExistingPath = path.toString();
            if (!mCleanupPaths.contains(nonExistingPath)) {
                mCleanupPaths.add(nonExistingPath);
            }
        }

        void cleanup() throws DeviceNotAvailableException {
            // Clean up in reverse order in case several pushed files were in the same nonexisting
            // directory.
            for (int i = mCleanupPaths.size() - 1; i >= 0; --i) {
                mDevice.deleteFile(mCleanupPaths.get(i));
            }
        }
    }

    // Class for code that needs an ITestDevice. It is instantiated both in tests and in
    // (Before|After)ClassWithInfo.
    private static class DeviceContext implements AutoCloseable {
        ITestDevice mDevice;
        CleanupPaths mCleanup;
        private String mPrimaryArch;

        DeviceContext(ITestDevice device) {
            mDevice = device;
            mCleanup = new CleanupPaths(mDevice);
        }

        public void close() throws DeviceNotAvailableException { mCleanup.cleanup(); }

        void pushSystemOemLibs(ZipFile libApk) throws Exception {
            pushNativeTestLib(libApk, "/system/${LIB}/libfoo.oem1.so");
            pushNativeTestLib(libApk, "/system/${LIB}/libbar.oem1.so");
            pushString("libfoo.oem1.so\n"
                            + "libbar.oem1.so\n",
                    "/system/etc/public.libraries-oem1.txt");

            pushNativeTestLib(libApk, "/system/${LIB}/libfoo.oem2.so");
            pushNativeTestLib(libApk, "/system/${LIB}/libbar.oem2.so");
            pushString("libfoo.oem2.so\n"
                            + "libbar.oem2.so\n",
                    "/system/etc/public.libraries-oem2.txt");
        }

        void pushProductLibs(ZipFile libApk) throws Exception {
            pushNativeTestLib(libApk, "/product/${LIB}/libfoo.product1.so");
            pushNativeTestLib(libApk, "/product/${LIB}/libbar.product1.so");
            pushString("libfoo.product1.so\n"
                            + "libbar.product1.so\n",
                    "/product/etc/public.libraries-product1.txt");
        }

        void softReboot() throws DeviceNotAvailableException {
            assertCommandSucceeds("setprop dev.bootcomplete 0");
            assertCommandSucceeds("stop");
            assertCommandSucceeds("start");
            mDevice.waitForDeviceAvailable();
        }

        String getPrimaryArch() throws DeviceNotAvailableException {
            if (mPrimaryArch == null) {
                mPrimaryArch = assertCommandSucceeds("getprop ro.bionic.arch");
            }
            return mPrimaryArch;
        }

        // Pushes the given file contents to the device at the given destination path. destPath is
        // assumed to have no risk of overlapping with existing files, and is deleted in tearDown(),
        // along with any directory levels that had to be created.
        void pushString(String fileContents, String destPath) throws DeviceNotAvailableException {
            mCleanup.addPath(destPath);
            assertThat(mDevice.pushString(fileContents, destPath)).isTrue();
        }

        // Like pushString, but extracts a Java resource and pushes that.
        void pushResource(String resourceName, String destPath) throws Exception {
            File hostTempFile = extractResourceToTempFile(resourceName);
            mCleanup.addPath(destPath);
            assertThat(mDevice.pushFile(hostTempFile, destPath)).isTrue();
        }

        // Like pushString, but extracts libnativeloader_testlib.so from the library_container_app
        // APK and pushes it to destPath. "${LIB}" is replaced with "lib" or "lib64" as appropriate.
        void pushNativeTestLib(ZipFile libApk, String destPath) throws Exception {
            String libApkPath = "lib/" + getPrimaryArch() + "/libnativeloader_testlib.so";
            ZipEntry entry = libApk.getEntry(libApkPath);
            assertWithMessage("Failed to find " + libApkPath + " in library_container_app.apk")
                    .that(entry)
                    .isNotNull();

            File libraryTempFile;
            try (InputStream inStream = libApk.getInputStream(entry)) {
                libraryTempFile = writeStreamToTempFile("libnativeloader_testlib.so", inStream);
            }

            String libDir = getPrimaryArch().contains("64") ? "lib64" : "lib";
            destPath = destPath.replace("${LIB}", libDir);

            mCleanup.addPath(destPath);
            assertThat(mDevice.pushFile(libraryTempFile, destPath)).isTrue();
        }

        String assertCommandSucceeds(String command) throws DeviceNotAvailableException {
            CommandResult result = mDevice.executeShellV2Command(command);
            assertWithMessage(result.toString()).that(result.getExitCode()).isEqualTo(0);
            // Remove trailing \n's.
            return result.getStdout().trim();
        }
    }

    static private ZipFile openLibContainerApk() throws Exception {
        return new ZipFile(extractResourceToTempFile("/library_container_app.apk"));
    }

    static private File extractResourceToTempFile(String resourceName) throws Exception {
        assertThat(resourceName).startsWith("/");
        try (InputStream inStream = LibnativeloaderTest.class.getResourceAsStream(resourceName)) {
            assertWithMessage("Failed to extract resource " + resourceName)
                    .that(inStream)
                    .isNotNull();
            return writeStreamToTempFile(resourceName.substring(1), inStream);
        }
    }

    static private File writeStreamToTempFile(String tempFileBaseName, InputStream inStream)
            throws Exception {
        File hostTempFile = File.createTempFile(tempFileBaseName, null);
        try (FileOutputStream outStream = new FileOutputStream(hostTempFile)) {
            ByteStreams.copy(inStream, outStream);
        }
        return hostTempFile;
    }
}
