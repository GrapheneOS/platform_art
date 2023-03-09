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

import static com.google.common.truth.Truth.assertThat;

import com.android.tradefed.invoker.TestInformation;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

import java.io.File;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.transform.Transformer;
import javax.xml.transform.TransformerFactory;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;

/** A helper class that can mutate the device state and restore it afterwards. */
public class DeviceState {
    private static final String APEX_INFO_FILE = "/apex/apex-info-list.xml";

    private final TestInformation mTestInfo;
    private final OdsignTestUtils mTestUtils;

    private Set<String> mTempFiles = new HashSet<>();
    private Set<String> mMountPoints = new HashSet<>();

    public DeviceState(TestInformation testInfo) throws Exception {
        mTestInfo = testInfo;
        mTestUtils = new OdsignTestUtils(testInfo);
    }

    /** Restores the device state. */
    public void restore() throws Exception {
        for (String mountPoint : mMountPoints) {
            mTestInfo.getDevice().executeShellV2Command(String.format("umount '%s'", mountPoint));
        }

        for (String tempFile : mTempFiles) {
            mTestInfo.getDevice().deleteFile(tempFile);
        }
    }

    /** Simulates that the ART APEX has been upgraded. */
    public void simulateArtApexUpgrade() throws Exception {
        try (var xmlMutator = new XmlMutator(APEX_INFO_FILE)) {
            NodeList list = xmlMutator.getDocument().getElementsByTagName("apex-info");
            for (int i = 0; i < list.getLength(); i++) {
                Element node = (Element) list.item(i);
                if (node.getAttribute("moduleName").equals("com.android.art")
                        && node.getAttribute("isActive").equals("true")) {
                    node.setAttribute("isFactory", "false");
                    node.setAttribute("lastUpdateMillis", "1");
                }
            }
        }
    }

    /**
     * Simulates that an APEX has been upgraded. We could install a real APEX, but that would
     * introduce an extra dependency to this test, which we want to avoid.
     */
    public void simulateApexUpgrade() throws Exception {
        try (var xmlMutator = new XmlMutator(APEX_INFO_FILE)) {
            NodeList list = xmlMutator.getDocument().getElementsByTagName("apex-info");
            for (int i = 0; i < list.getLength(); i++) {
                Element node = (Element) list.item(i);
                if (node.getAttribute("moduleName").equals("com.android.wifi")
                        && node.getAttribute("isActive").equals("true")) {
                    node.setAttribute("isFactory", "false");
                    node.setAttribute("lastUpdateMillis", "1");
                }
            }
        }
    }

    /**
     * Pushes the file to a temporary location and bind-mount it at the given path. This is useful
     * when the path is readonly.
     */
    private void pushAndBindMount(File localFile, String remotePath) throws Exception {
        String tempFile = "/data/local/tmp/odsign_e2e_tests_" + UUID.randomUUID() + ".tmp";
        assertThat(mTestInfo.getDevice().pushFile(localFile, tempFile)).isTrue();
        mTempFiles.add(tempFile);

        mTestUtils.assertCommandSucceeds(
                String.format("mount --bind '%s' '%s'", tempFile, remotePath));
        mMountPoints.add(remotePath);
        mTestUtils.assertCommandSucceeds(String.format("restorecon '%s'", remotePath));
    }

    /** A helper class for mutating an XML file. */
    private class XmlMutator implements AutoCloseable {
        private final Document mDocument;
        private final String mRemoteXmlFile;
        private final File mLocalFile;

        public XmlMutator(String remoteXmlFile) throws Exception {
            // Load the XML file.
            mRemoteXmlFile = remoteXmlFile;
            mLocalFile = mTestInfo.getDevice().pullFile(remoteXmlFile);
            assertThat(mLocalFile).isNotNull();
            DocumentBuilder builder = DocumentBuilderFactory.newInstance().newDocumentBuilder();
            mDocument = builder.parse(mLocalFile);
        }

        @Override
        public void close() throws Exception {
            // Save the XML file.
            Transformer transformer = TransformerFactory.newInstance().newTransformer();
            transformer.transform(new DOMSource(mDocument), new StreamResult(mLocalFile));
            pushAndBindMount(mLocalFile, mRemoteXmlFile);
        }

        /** Returns a mutable XML document. */
        public Document getDocument() {
            return mDocument;
        }
    }
}
