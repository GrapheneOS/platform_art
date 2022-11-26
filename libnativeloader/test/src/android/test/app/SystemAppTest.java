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

package android.test.app;

import android.test.lib.TestUtils;
import android.test.systemsharedlib.SystemSharedLib;
import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

// These tests are run from /system/app, /system/priv-app, and /system_ext/app.
@SmallTest
@RunWith(AndroidJUnit4.class)
public class SystemAppTest {
    @Test
    public void testLoadExtendedPublicLibraries() {
        System.loadLibrary("foo.oem1");
        System.loadLibrary("bar.oem1");
        System.loadLibrary("foo.oem2");
        System.loadLibrary("bar.oem2");
        System.loadLibrary("foo.product1");
        System.loadLibrary("bar.product1");
    }

    @Test
    public void testLoadPrivateLibraries() {
        System.loadLibrary("system_private1");
        TestUtils.assertLibraryNotFound(() -> System.loadLibrary("product_private1"));
        TestUtils.assertLibraryNotFound(() -> System.loadLibrary("vendor_private1"));
    }

    @Test
    public void testLoadPrivateLibrariesViaSystemSharedLib() {
        SystemSharedLib.loadLibrary("system_private2");
        TestUtils.assertLibraryNotFound(() -> SystemSharedLib.loadLibrary("product_private2"));
        TestUtils.assertLibraryNotFound(() -> SystemSharedLib.loadLibrary("vendor_private2"));
    }
}
