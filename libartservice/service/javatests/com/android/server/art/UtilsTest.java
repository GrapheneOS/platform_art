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
 * limitations under the License
 */

package com.android.server.art;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.os.SystemProperties;
import android.util.SparseArray;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.Utils;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.art.wrapper.PackageState;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class UtilsTest {
    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, Constants.class);

    @Before
    public void setUp() throws Exception {
        lenient().when(SystemProperties.get(eq("ro.dalvik.vm.isa.x86_64"))).thenReturn("arm64");
        lenient().when(SystemProperties.get(eq("ro.dalvik.vm.isa.x86"))).thenReturn("arm");

        // In reality, the preferred ABI should be either the native 64 bit ABI or the native 32 bit
        // ABI, but we use a different value here to make sure the value is used only if expected.
        lenient().when(Constants.getPreferredAbi()).thenReturn("x86");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");
    }

    @Test
    public void testCollectionIsEmptyTrue() {
        assertThat(Utils.isEmpty(List.of())).isTrue();
    }

    @Test
    public void testCollectionIsEmptyFalse() {
        assertThat(Utils.isEmpty(List.of(1))).isFalse();
    }

    @Test
    public void testSparseArrayIsEmptyTrue() {
        assertThat(Utils.isEmpty(new SparseArray<Integer>())).isTrue();
    }

    @Test
    public void testSparseArrayIsEmptyFalse() {
        SparseArray<Integer> array = new SparseArray<>();
        array.put(1, 1);
        assertThat(Utils.isEmpty(array)).isFalse();
    }

    @Test
    public void testArrayIsEmptyTrue() {
        assertThat(Utils.isEmpty(new int[0])).isTrue();
    }

    @Test
    public void testArrayIsEmptyFalse() {
        assertThat(Utils.isEmpty(new int[] {1})).isFalse();
    }

    @Test
    public void testGetAllAbis() {
        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn("armeabi-v7a");
        when(pkgState.getSecondaryCpuAbi()).thenReturn("arm64-v8a");

        assertThat(Utils.getAllAbis(pkgState))
                .containsExactly(Utils.Abi.create("armeabi-v7a", "arm", true /* isPrimaryAbi */),
                        Utils.Abi.create("arm64-v8a", "arm64", false /* isPrimaryAbi */));
    }

    @Test
    public void testGetAllAbisTranslated() {
        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn("x86_64");
        when(pkgState.getSecondaryCpuAbi()).thenReturn("x86");

        assertThat(Utils.getAllAbis(pkgState))
                .containsExactly(Utils.Abi.create("arm64-v8a", "arm64", true /* isPrimaryAbi */),
                        Utils.Abi.create("armeabi-v7a", "arm", false /* isPrimaryAbi */));
    }

    @Test
    public void testGetAllAbisPrimaryOnly() {
        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn("armeabi-v7a");
        when(pkgState.getSecondaryCpuAbi()).thenReturn(null);

        assertThat(Utils.getAllAbis(pkgState))
                .containsExactly(Utils.Abi.create("armeabi-v7a", "arm", true /* isPrimaryAbi */));
    }

    @Test
    public void testGetAllAbisNone() {
        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn(null);
        when(pkgState.getSecondaryCpuAbi()).thenReturn(null);

        assertThat(Utils.getAllAbis(pkgState))
                .containsExactly(Utils.Abi.create("x86", "x86", true /* isPrimaryAbi */));
    }

    @Test(expected = IllegalStateException.class)
    public void testGetAllAbisInvalidNativeIsa() {
        lenient().when(SystemProperties.get(eq("ro.dalvik.vm.isa.x86_64"))).thenReturn("x86");

        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn("x86_64");
        when(pkgState.getSecondaryCpuAbi()).thenReturn(null);

        Utils.getAllAbis(pkgState);
    }

    @Test(expected = IllegalStateException.class)
    public void testGetAllAbisUnsupportedTranslation() {
        lenient().when(SystemProperties.get(eq("ro.dalvik.vm.isa.x86_64"))).thenReturn("");

        var pkgState = mock(PackageState.class);
        when(pkgState.getPrimaryCpuAbi()).thenReturn("x86_64");
        when(pkgState.getSecondaryCpuAbi()).thenReturn(null);

        Utils.getAllAbis(pkgState);
    }

    @Test
    public void testImplies() {
        assertThat(Utils.implies(false, false)).isTrue();
        assertThat(Utils.implies(false, true)).isTrue();
        assertThat(Utils.implies(true, false)).isFalse();
        assertThat(Utils.implies(true, true)).isTrue();
    }

    @Test
    public void testCheck() {
        Utils.check(true);
    }

    @Test(expected = IllegalStateException.class)
    public void testCheckFailed() throws Exception {
        Utils.check(false);
    }
}
