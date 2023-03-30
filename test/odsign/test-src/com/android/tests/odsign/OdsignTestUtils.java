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

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.cts.install.lib.host.InstallUtilsHost;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.TestDeviceOptions;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.util.CommandResult;

import com.google.common.io.ByteStreams;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.time.Duration;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;

public class OdsignTestUtils {
    public static final String ART_APEX_DALVIK_CACHE_DIRNAME =
            "/data/misc/apexdata/com.android.art/dalvik-cache";
    public static final String CACHE_INFO_FILE = ART_APEX_DALVIK_CACHE_DIRNAME + "/cache-info.xml";
    public static final String APEX_INFO_FILE = "/apex/apex-info-list.xml";

    private static final String ODREFRESH_BIN = "odrefresh";

    public static final String ZYGOTE_32_NAME = "zygote";
    public static final String ZYGOTE_64_NAME = "zygote64";

    public static final List<String> APP_ARTIFACT_EXTENSIONS = List.of(".art", ".odex", ".vdex");
    public static final List<String> BCP_ARTIFACT_EXTENSIONS = List.of(".art", ".oat", ".vdex");

    private static final String ODREFRESH_COMPILATION_LOG =
            "/data/misc/odrefresh/compilation-log.txt";

    private static final Duration BOOT_COMPLETE_TIMEOUT = Duration.ofMinutes(5);
    private static final Duration RESTART_ZYGOTE_COMPLETE_TIMEOUT = Duration.ofMinutes(3);

    private static final String TAG = "OdsignTestUtils";
    private static final String PACKAGE_NAME_KEY = TAG + ":PACKAGE_NAME";

    // Keep in sync with `ABI_TO_INSTRUCTION_SET_MAP` in
    // libcore/libart/src/main/java/dalvik/system/VMRuntime.java.
    private static final Map<String, String> ABI_TO_INSTRUCTION_SET_MAP =
            Map.of("armeabi", "arm", "armeabi-v7a", "arm", "x86", "x86", "x86_64", "x86_64",
                    "arm64-v8a", "arm64", "arm64-v8a-hwasan", "arm64", "riscv64", "riscv64");

    private final InstallUtilsHost mInstallUtils;
    private final TestInformation mTestInfo;

    public OdsignTestUtils(TestInformation testInfo) throws Exception {
        assertThat(testInfo.getDevice()).isNotNull();
        mInstallUtils = new InstallUtilsHost(testInfo);
        mTestInfo = testInfo;
    }

    /**
     * Re-installs the current active ART module on device.
     */
    public void installTestApex() throws Exception {
        assumeTrue("Updating APEX is not supported", mInstallUtils.isApexUpdateSupported());

        String packagesOutput =
                mTestInfo.getDevice().executeShellCommand("pm list packages -f --apex-only");
        Pattern p = Pattern.compile(
                "^package:(.*)=(com(?:\\.google)?\\.android(?:\\.go)?\\.art)$", Pattern.MULTILINE);
        Matcher m = p.matcher(packagesOutput);
        assertTrue("ART module not found. Packages are:\n" + packagesOutput, m.find());
        String artApexPath = m.group(1);
        String artApexName = m.group(2);

        assertCommandSucceeds("pm install --apex " + artApexPath);

        mTestInfo.properties().put(PACKAGE_NAME_KEY, artApexName);

        removeCompilationLogToAvoidBackoff();
    }

    public void uninstallTestApex() throws Exception {
        String packageName = mTestInfo.properties().get(PACKAGE_NAME_KEY);
        if (packageName != null) {
            mTestInfo.getDevice().uninstallPackage(packageName);
            removeCompilationLogToAvoidBackoff();
        }
    }

    public Set<String> getMappedArtifacts(String pid, String grepPattern) throws Exception {
        String grepCommand = String.format("grep \"%s\" /proc/%s/maps", grepPattern, pid);
        Set<String> mappedFiles = new HashSet<>();
        for (String line : assertCommandSucceeds(grepCommand).split("\\R")) {
            int start = line.indexOf(ART_APEX_DALVIK_CACHE_DIRNAME);
            if (line.contains("[") || line.contains("(deleted)")) {
                // Ignore anonymously mapped sections, which are quoted in square braces, and
                // deleted mapped files.
                continue;
            }
            mappedFiles.add(line.substring(start));
        }
        return mappedFiles;
    }

    /**
     * Returns the mapped artifacts of the Zygote process.
     */
    public Set<String> getZygoteLoadedArtifacts(String zygoteName) throws Exception {
        // There may be multiple Zygote processes when Zygote just forks and has not executed any
        // app binary. We can take any of the pids.
        // We can't use the "-s" flag when calling `pidof` because the Toybox's `pidof`
        // implementation is wrong and it outputs multiple pids regardless of the "-s" flag, so we
        // split the output and take the first pid ourselves.
        String zygotePid = assertCommandSucceeds("pidof " + zygoteName).split("\\s+")[0];
        assertTrue(!zygotePid.isEmpty());

        String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + "/.*/boot";
        return getMappedArtifacts(zygotePid, grepPattern);
    }

    public Set<String> getSystemServerLoadedArtifacts() throws Exception {
        String systemServerPid = assertCommandSucceeds("pidof system_server");
        assertTrue(!systemServerPid.isEmpty());
        assertTrue("There should be exactly one `system_server` process",
                systemServerPid.matches("\\d+"));

        // system_server artifacts are in the APEX data dalvik cache and names all contain
        // the word "@classes". Look for mapped files that match this pattern in the proc map for
        // system_server.
        String grepPattern = ART_APEX_DALVIK_CACHE_DIRNAME + "/.*@classes";
        return getMappedArtifacts(systemServerPid, grepPattern);
    }

    public Set<String> getZygoteExpectedArtifacts(String bootImageStem, String isa)
            throws Exception {
        Set<String> artifacts = new HashSet<>();
        for (String extension : BCP_ARTIFACT_EXTENSIONS) {
            artifacts.add(String.format(
                    "%s/%s/%s%s", ART_APEX_DALVIK_CACHE_DIRNAME, isa, bootImageStem, extension));
        }
        return artifacts;
    }

    public Set<String> getZygotesExpectedArtifacts(String bootImageStem) throws Exception {
        Set<String> artifacts = new HashSet<>();
        for (String isa : getZygoteNamesAndIsas().values()) {
            artifacts.addAll(getZygoteExpectedArtifacts(bootImageStem, isa));
        }
        return artifacts;
    }

    public Set<String> getZygotesExpectedArtifacts() throws Exception {
        return getZygotesExpectedArtifacts("boot");
    }

    public Set<String> getSystemServerExpectedArtifacts() throws Exception {
        String[] classpathElements = getListFromEnvironmentVariable("SYSTEMSERVERCLASSPATH");
        assertTrue("SYSTEMSERVERCLASSPATH is empty", classpathElements.length > 0);
        String[] standaloneJars = getListFromEnvironmentVariable("STANDALONE_SYSTEMSERVER_JARS");
        String[] allSystemServerJars =
                Stream.concat(Arrays.stream(classpathElements), Arrays.stream(standaloneJars))
                        .toArray(String[] ::new);
        String isa = getSystemServerIsa();

        Set<String> artifacts = new HashSet<>();
        for (String jar : allSystemServerJars) {
            artifacts.addAll(getApexDataDalvikCacheFilenames(jar, isa));
        }

        return artifacts;
    }

    // Verifies that boot image files with the given stem are loaded by Zygote for each instruction
    // set.
    public void verifyZygotesLoadedArtifacts(String bootImageStem) throws Exception {
        for (var entry : getZygoteNamesAndIsas().entrySet()) {
            assertThat(getZygoteLoadedArtifacts(entry.getKey()))
                    .containsAtLeastElementsIn(
                            getZygoteExpectedArtifacts(bootImageStem, entry.getValue()));
        }
    }

    public void verifySystemServerLoadedArtifacts() throws Exception {
        assertThat(getSystemServerLoadedArtifacts())
                .containsAtLeastElementsIn(getSystemServerExpectedArtifacts());
    }

    public boolean haveCompilationLog() throws Exception {
        CommandResult result =
                mTestInfo.getDevice().executeShellV2Command("stat " + ODREFRESH_COMPILATION_LOG);
        return result.getExitCode() == 0;
    }

    public void removeCompilationLogToAvoidBackoff() throws Exception {
        mTestInfo.getDevice().executeShellCommand("rm -f " + ODREFRESH_COMPILATION_LOG);
    }

    public void reboot() throws Exception {
        TestDeviceOptions options = mTestInfo.getDevice().getOptions();
        // store default value and increase time-out for reboot
        int rebootTimeout = options.getRebootTimeout();
        long onlineTimeout = options.getOnlineTimeout();
        options.setRebootTimeout((int) BOOT_COMPLETE_TIMEOUT.toMillis());
        options.setOnlineTimeout(BOOT_COMPLETE_TIMEOUT.toMillis());
        mTestInfo.getDevice().setOptions(options);

        mTestInfo.getDevice().reboot();
        boolean success =
                mTestInfo.getDevice().waitForBootComplete(BOOT_COMPLETE_TIMEOUT.toMillis());

        // restore default values
        options.setRebootTimeout(rebootTimeout);
        options.setOnlineTimeout(onlineTimeout);
        mTestInfo.getDevice().setOptions(options);

        assertWithMessage("Device didn't boot in %s", BOOT_COMPLETE_TIMEOUT).that(success).isTrue();
    }

    public void restartZygote() throws Exception {
        // `waitForBootComplete` relies on `dev.bootcomplete`.
        mTestInfo.getDevice().executeShellCommand("setprop dev.bootcomplete 0");
        mTestInfo.getDevice().executeShellCommand("setprop ctl.restart zygote");
        boolean success = mTestInfo.getDevice().waitForBootComplete(
                RESTART_ZYGOTE_COMPLETE_TIMEOUT.toMillis());
        assertWithMessage("Zygote didn't start in %s", BOOT_COMPLETE_TIMEOUT)
                .that(success)
                .isTrue();
    }

    /**
     * Returns the value of a boolean test property, or false if it does not exist.
     */
    private boolean getBooleanOrDefault(String key) {
        String value = mTestInfo.properties().get(key);
        if (value == null) {
            return false;
        }
        return Boolean.parseBoolean(value);
    }

    private void setBoolean(String key, boolean value) {
        mTestInfo.properties().put(key, Boolean.toString(value));
    }

    private String[] getListFromEnvironmentVariable(String name) throws Exception {
        String systemServerClasspath =
                mTestInfo.getDevice().executeShellCommand("echo $" + name).trim();
        if (!systemServerClasspath.isEmpty()) {
            return systemServerClasspath.split(":");
        }
        return new String[0];
    }

    private static String getInstructionSet(String abi) {
        String instructionSet = ABI_TO_INSTRUCTION_SET_MAP.get(abi);
        assertThat(instructionSet).isNotNull();
        return instructionSet;
    }

    public Map<String, String> getZygoteNamesAndIsas() throws Exception {
        Map<String, String> namesAndIsas = new HashMap<>();
        String abiList64 = mTestInfo.getDevice().getProperty("ro.product.cpu.abilist64");
        if (abiList64 != null && !abiList64.isEmpty()) {
            namesAndIsas.put(ZYGOTE_64_NAME, getInstructionSet(abiList64.split(",")[0]));
        }
        String abiList32 = mTestInfo.getDevice().getProperty("ro.product.cpu.abilist32");
        if (abiList32 != null && !abiList32.isEmpty()) {
            namesAndIsas.put(ZYGOTE_32_NAME, getInstructionSet(abiList32.split(",")[0]));
        }
        return namesAndIsas;
    }

    public String getSystemServerIsa() throws Exception {
        return getInstructionSet(
                mTestInfo.getDevice().getProperty("ro.product.cpu.abilist").split(",")[0]);
    }

    // Keep in sync with `GetApexDataDalvikCacheFilename` in art/libartbase/base/file_utils.cc.
    public static Set<String> getApexDataDalvikCacheFilenames(String dexLocation, String isa)
            throws Exception {
        Set<String> filenames = new HashSet<>();
        String escapedPath = dexLocation.substring(1).replace('/', '@');
        for (String extension : APP_ARTIFACT_EXTENSIONS) {
            filenames.add(String.format("%s/%s/%s@classes%s", ART_APEX_DALVIK_CACHE_DIRNAME, isa,
                    escapedPath, extension));
        }
        return filenames;
    }

    private long parseFormattedDateTime(String dateTimeStr) throws Exception {
        DateTimeFormatter formatter =
                DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss.nnnnnnnnn Z");
        ZonedDateTime zonedDateTime = ZonedDateTime.parse(dateTimeStr, formatter);
        return zonedDateTime.toInstant().toEpochMilli();
    }

    public long getModifiedTimeMs(String filename) throws Exception {
        // We can't use the "-c '%.3Y'" flag when to get the timestamp because the Toybox's `stat`
        // implementation truncates the timestamp to seconds, which is not accurate enough, so we
        // use "-c '%%y'" and parse the time ourselves.
        String dateTimeStr = assertCommandSucceeds(String.format("stat -c '%%y' '%s'", filename));
        return parseFormattedDateTime(dateTimeStr);
    }

    public long getCurrentTimeMs() throws Exception {
        // We can't use getDevice().getDeviceDate() because it truncates the timestamp to seconds,
        // which is not accurate enough.
        String dateTimeStr = assertCommandSucceeds("date +'%Y-%m-%d %H:%M:%S.%N %z'");
        return parseFormattedDateTime(dateTimeStr);
    }

    public int countFilesCreatedBeforeTime(String directory, long timestampMs)
            throws DeviceNotAvailableException {
        // Drop the precision to second, mainly because we need to use `find -newerct` to query
        // files by timestamp, but toybox can't parse `date +'%s.%N'` currently.
        String timestamp = String.valueOf(timestampMs / 1000);
        // For simplicity, directory must be a simple path that doesn't require escaping.
        String output = assertCommandSucceeds(
                "find " + directory + " -type f ! -newerct '@" + timestamp + "' | wc -l");
        return Integer.parseInt(output);
    }

    public int countFilesCreatedAfterTime(String directory, long timestampMs)
            throws DeviceNotAvailableException {
        // Drop the precision to second, mainly because we need to use `find -newerct` to query
        // files by timestamp, but toybox can't parse `date +'%s.%N'` currently.
        String timestamp = String.valueOf(timestampMs / 1000);
        // For simplicity, directory must be a simple path that doesn't require escaping.
        String output = assertCommandSucceeds(
                "find " + directory + " -type f -newerct '@" + timestamp + "' | wc -l");
        return Integer.parseInt(output);
    }

    public String assertCommandSucceeds(String command) throws DeviceNotAvailableException {
        CommandResult result = mTestInfo.getDevice().executeShellV2Command(command);
        assertWithMessage(result.toString()).that(result.getExitCode()).isEqualTo(0);
        return result.getStdout().trim();
    }

    public File copyResourceToFile(String resourceName) throws Exception {
        File file = File.createTempFile("odsign_e2e_tests", ".tmp");
        file.deleteOnExit();
        try (OutputStream outputStream = new FileOutputStream(file);
                InputStream inputStream = getClass().getResourceAsStream(resourceName)) {
            assertThat(ByteStreams.copy(inputStream, outputStream)).isGreaterThan(0);
        }
        return file;
    }

    public void assertModifiedAfter(Set<String> artifacts, long timeMs) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(
                    String.format(
                            "Artifact %s is not re-compiled. Modified time: %d, Reference time: %d",
                            artifact, modifiedTime, timeMs),
                    modifiedTime > timeMs);
        }
    }

    public void assertNotModifiedAfter(Set<String> artifacts, long timeMs) throws Exception {
        for (String artifact : artifacts) {
            long modifiedTime = getModifiedTimeMs(artifact);
            assertTrue(String.format("Artifact %s is unexpectedly re-compiled. "
                                       + "Modified time: %d, Reference time: %d",
                               artifact, modifiedTime, timeMs),
                    modifiedTime < timeMs);
        }
    }

    public void assertFilesExist(Set<String> files) throws Exception {
        assertThat(getExistingFiles(files)).containsExactlyElementsIn(files);
    }

    public void assertFilesNotExist(Set<String> files) throws Exception {
        assertThat(getExistingFiles(files)).isEmpty();
    }

    private Set<String> getExistingFiles(Set<String> files) throws Exception {
        Set<String> existingFiles = new HashSet<>();
        for (String file : files) {
            if (mTestInfo.getDevice().doesFileExist(file)) {
                existingFiles.add(file);
            }
        }
        return existingFiles;
    }

    public static String replaceExtension(String filename, String extension) throws Exception {
        int index = filename.lastIndexOf(".");
        assertTrue("Extension not found in filename: " + filename, index != -1);
        return filename.substring(0, index) + extension;
    }

    public void runOdrefresh() throws Exception {
        runOdrefresh("" /* extraArgs */);
    }

    public void runOdrefresh(String extraArgs) throws Exception {
        mTestInfo.getDevice().executeShellV2Command(ODREFRESH_BIN + " --check");
        mTestInfo.getDevice().executeShellV2Command(
                ODREFRESH_BIN + " --partial-compilation --no-refresh " + extraArgs + " --compile");
    }

    public boolean areAllApexesFactoryInstalled() throws Exception {
        Document doc = loadXml(APEX_INFO_FILE);
        NodeList list = doc.getElementsByTagName("apex-info");
        for (int i = 0; i < list.getLength(); i++) {
            Element node = (Element) list.item(i);
            if (node.getAttribute("isActive").equals("true")
                    && node.getAttribute("isFactory").equals("false")) {
                return false;
            }
        }
        return true;
    }

    private Document loadXml(String remoteXmlFile) throws Exception {
        File localFile = mTestInfo.getDevice().pullFile(remoteXmlFile);
        assertThat(localFile).isNotNull();
        DocumentBuilder builder = DocumentBuilderFactory.newInstance().newDocumentBuilder();
        return builder.parse(localFile);
    }
}
