/*
 * Copyright (C) 2024 The Android Open Source Project
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
 * limitations under the License
 */

package com.android.server.art;

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.lenient;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.testing.StaticMockitoRule;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class ArtManagedInstallFileHelperTest {
    @Rule public StaticMockitoRule mockitoRule = new StaticMockitoRule(Constants.class);

    @Before
    public void setUp() throws Exception {
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");
    }

    @Test
    public void testIsArtManaged() throws Exception {
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.dm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.prof")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.sdm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.arm.sdm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.arm64.sdm")).isTrue();
        assertThat(ArtManagedInstallFileHelper.isArtManaged("/foo/bar.abc")).isFalse();
    }

    @Test
    public void testFilterPathsForApk() throws Exception {
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.sdm",
                                   "/foo/bar.x86_64.sdm", "/foo/bar.arm.sdm", "/foo/bar.arm64.sdm",
                                   "/foo/bar.abc", "/foo/baz.dm"),
                           "/foo/bar.apk"))
                .containsExactly(
                        "/foo/bar.dm", "/foo/bar.prof", "/foo/bar.arm.sdm", "/foo/bar.arm64.sdm");

        // Filenames don't match.
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.arm64.sdm",
                                   "/foo/bar.abc", "/foo/baz.dm"),
                           "/foo/qux.apk"))
                .isEmpty();

        // Directories don't match.
        assertThat(ArtManagedInstallFileHelper.filterPathsForApk(
                           List.of("/foo/bar.dm", "/foo/bar.prof", "/foo/bar.arm64.sdm",
                                   "/foo/bar.abc", "/foo/baz.dm"),
                           "/quz/bar.apk"))
                .isEmpty();
    }

    @Test
    public void testGetTargetPathForApk() throws Exception {
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.dm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.dm");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.prof", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.prof");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.arm.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.arm.sdm");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.arm64.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/base.arm64.sdm");

        // None or invalid ISA.
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/bar.sdm");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.x86_64.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/bar.x86_64.sdm");
        assertThat(ArtManagedInstallFileHelper.getTargetPathForApk(
                           "/foo/bar.invalid-isa.sdm", "/somewhere/base.apk"))
                .isEqualTo("/somewhere/bar.invalid-isa.sdm");

        assertThrows(IllegalArgumentException.class, () -> {
            ArtManagedInstallFileHelper.getTargetPathForApk("/foo/bar.abc", "/somewhere/base.apk");
        });
    }
}
