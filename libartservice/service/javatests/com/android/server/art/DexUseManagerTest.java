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

package com.android.server.art;

import static com.android.server.art.DexUseManagerLocal.DetailedSecondaryDexInfo;
import static com.android.server.art.DexUseManagerLocal.DexLoader;
import static com.android.server.art.DexUseManagerLocal.SecondaryDexInfo;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.os.Binder;
import android.os.Environment;
import android.os.Process;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.storage.StorageManager;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.testing.MockClock;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;

import dalvik.system.PathClassLoader;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class DexUseManagerTest {
    private static final String LOADING_PKG_NAME = "com.example.loadingpackage";
    private static final String OWNING_PKG_NAME = "com.example.owningpackage";
    private static final String BASE_APK = "/data/app/" + OWNING_PKG_NAME + "/base.apk";
    private static final String SPLIT_APK = "/data/app/" + OWNING_PKG_NAME + "/split_0.apk";

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, Constants.class, Process.class);

    private final UserHandle mUserHandle = Binder.getCallingUserHandle();

    /**
     * The default value of `isDexFilePublic` returned by `getSecondaryDexInfo`. The value doesn't
     * matter because it's undefined, but it's needed for deep equality check, to make the test
     * simpler.
     */
    private final boolean mDefaultIsDexFilePublic = true;

    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    @Mock private DexUseManagerLocal.Injector mInjector;
    @Mock private IArtd mArtd;
    private DexUseManagerLocal mDexUseManager;
    private String mCeDir;
    private String mDeDir;
    private MockClock mMockClock;

    @Before
    public void setUp() throws Exception {
        // No ISA translation.
        lenient()
                .when(SystemProperties.get(argThat(arg -> arg.startsWith("ro.dalvik.vm.isa."))))
                .thenReturn("");

        lenient().when(Constants.getPreferredAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");

        lenient().when(Process.isIsolatedUid(anyInt())).thenReturn(false);

        PackageState loadingPkgState = createPackageState(LOADING_PKG_NAME, "armeabi-v7a");
        lenient().when(mSnapshot.getPackageState(eq(LOADING_PKG_NAME))).thenReturn(loadingPkgState);
        PackageState owningPkgState = createPackageState(OWNING_PKG_NAME, "arm64-v8a");
        lenient().when(mSnapshot.getPackageState(eq(OWNING_PKG_NAME))).thenReturn(owningPkgState);

        lenient()
                .when(mSnapshot.getPackageStates())
                .thenReturn(
                        Map.of(LOADING_PKG_NAME, loadingPkgState, OWNING_PKG_NAME, owningPkgState));

        mCeDir = Environment
                         .getDataCePackageDirectoryForUser(StorageManager.UUID_DEFAULT,
                                 Binder.getCallingUserHandle(), OWNING_PKG_NAME)
                         .toString();
        mDeDir = Environment
                         .getDataDePackageDirectoryForUser(StorageManager.UUID_DEFAULT,
                                 Binder.getCallingUserHandle(), OWNING_PKG_NAME)
                         .toString();
        mMockClock = new MockClock();

        File tempFile = File.createTempFile("package-dex-usage", ".pb");
        tempFile.deleteOnExit();

        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.getCurrentTimeMillis()).thenReturn(0l);
        lenient().when(mInjector.getFilename()).thenReturn(tempFile.getPath());
        lenient()
                .when(mInjector.createScheduledExecutor())
                .thenAnswer(invocation -> mMockClock.createScheduledExecutor());

        mDexUseManager = new DexUseManagerLocal(mInjector);
    }

    @Test
    public void testPrimaryDexOwned() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isFalse();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    @Test
    public void testPrimaryDexOwnedIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isTrue();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    @Test
    public void testPrimaryDexOwnedSplitIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(SPLIT_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isFalse();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK)).isTrue();
    }

    @Test
    public void testPrimaryDexOthers() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */));
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, BASE_APK)).isTrue();

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK)).isEmpty();
        assertThat(mDexUseManager.isPrimaryDexUsedByOtherApps(OWNING_PKG_NAME, SPLIT_APK))
                .isFalse();
    }

    /** Checks that it ignores and dedups things correctly. */
    @Test
    public void testPrimaryDexMultipleEntries() throws Exception {
        verifyPrimaryDexMultipleEntries(false /* saveAndLoad */);
    }

    /** Checks that it saves and loads data correctly. */
    @Test
    public void testPrimaryDexMultipleEntriesPersisted() throws Exception {
        verifyPrimaryDexMultipleEntries(true /*saveAndLoad */);
    }

    private void verifyPrimaryDexMultipleEntries(boolean saveAndLoad) throws Exception {
        when(mInjector.getCurrentTimeMillis()).thenReturn(1000l);

        // These should be ignored.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, Utils.PLATFORM_PACKAGE_NAME, Map.of(BASE_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of("/data/app/" + OWNING_PKG_NAME + "/non-existing.apk", "CLC"));

        // Some of these should be deduped.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC", SPLIT_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC", SPLIT_APK, "CLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));
        when(mInjector.getCurrentTimeMillis()).thenReturn(2000l);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(BASE_APK, "CLC"));

        if (saveAndLoad) {
            // MockClock runs tasks synchronously.
            mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS);
            mDexUseManager = new DexUseManagerLocal(mInjector);
        }

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, BASE_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                        DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */),
                        DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */));

        assertThat(mDexUseManager.getPrimaryDexLoaders(OWNING_PKG_NAME, SPLIT_APK))
                .containsExactly(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */));

        assertThat(mDexUseManager.getPackageLastUsedAtMs(OWNING_PKG_NAME)).isEqualTo(2000l);
    }

    @Test
    public void testSecondaryDexOwned() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */)),
                        false /* isUsedByOtherApps */, mDefaultIsDexFilePublic));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexOwnedIsolated() {
        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mDeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mDeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, true /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, mDefaultIsDexFilePublic));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexOthers() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("armeabi-v7a"),
                        Set.of(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, mDefaultIsDexFilePublic));
        assertThat(dexInfoList.get(0).classLoaderContext()).isEqualTo("CLC");
    }

    @Test
    public void testSecondaryDexUnsupportedClc() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, LOADING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT, Set.of("armeabi-v7a"),
                        Set.of(DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, mDefaultIsDexFilePublic));
        assertThat(dexInfoList.get(0).classLoaderContext()).isNull();
    }

    @Test
    public void testSecondaryDexVariableClc() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC2"));

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS,
                        Set.of("arm64-v8a", "armeabi-v7a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, mDefaultIsDexFilePublic));
        assertThat(dexInfoList.get(0).classLoaderContext()).isNull();
    }

    /** Checks that it ignores and dedups things correctly. */
    @Test
    public void testSecondaryDexMultipleEntries() throws Exception {
        verifySecondaryDexMultipleEntries(false /*saveAndLoad */);
    }

    /** Checks that it saves and loads data correctly. */
    @Test
    public void testSecondaryDexMultipleEntriesPersisted() throws Exception {
        verifySecondaryDexMultipleEntries(true /*saveAndLoad */);
    }

    private void verifySecondaryDexMultipleEntries(boolean saveAndLoad) throws Exception {
        when(mInjector.getCurrentTimeMillis()).thenReturn(1000l);

        // These should be ignored.
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, Utils.PLATFORM_PACKAGE_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of("/some/non-existing.apk", "CLC"));

        // Some of these should be deduped.
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", "CLC", mCeDir + "/bar.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", "UpdatedCLC", mCeDir + "/bar.apk", "UpdatedCLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "UpdatedCLC"));

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/bar.apk", "DifferentCLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/bar.apk", "UpdatedDifferentCLC"));

        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/baz.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        when(mInjector.getCurrentTimeMillis()).thenReturn(2000l);
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME,
                Map.of(mCeDir + "/foo.apk", SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT));

        if (saveAndLoad) {
            // MockClock runs tasks synchronously.
            mMockClock.advanceTime(DexUseManagerLocal.INTERVAL_MS);
            mDexUseManager = new DexUseManagerLocal(mInjector);
        }

        List<? extends SecondaryDexInfo> dexInfoList =
                mDexUseManager.getSecondaryDexInfo(OWNING_PKG_NAME);
        assertThat(dexInfoList)
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                                         "UpdatedCLC", Set.of("arm64-v8a", "armeabi-v7a"),
                                         Set.of(DexLoader.create(OWNING_PKG_NAME,
                                                        false /* isolatedProcess */),
                                                 DexLoader.create(OWNING_PKG_NAME,
                                                         true /* isolatedProcess */),
                                                 DexLoader.create(LOADING_PKG_NAME,
                                                         false /* isolatedProcess */)),
                                         true /* isUsedByOtherApps */, mDefaultIsDexFilePublic),
                        DetailedSecondaryDexInfo.create(mCeDir + "/bar.apk", mUserHandle,
                                SecondaryDexInfo.VARYING_CLASS_LOADER_CONTEXTS,
                                Set.of("arm64-v8a", "armeabi-v7a"),
                                Set.of(DexLoader.create(
                                               OWNING_PKG_NAME, false /* isolatedProcess */),
                                        DexLoader.create(
                                                LOADING_PKG_NAME, false /* isolatedProcess */)),
                                true /* isUsedByOtherApps */, mDefaultIsDexFilePublic),
                        DetailedSecondaryDexInfo.create(mCeDir + "/baz.apk", mUserHandle,
                                SecondaryDexInfo.UNSUPPORTED_CLASS_LOADER_CONTEXT,
                                Set.of("arm64-v8a"),
                                Set.of(DexLoader.create(
                                        OWNING_PKG_NAME, false /* isolatedProcess */)),
                                false /* isUsedByOtherApps */, mDefaultIsDexFilePublic));

        assertThat(mDexUseManager.getPackageLastUsedAtMs(OWNING_PKG_NAME)).isEqualTo(2000l);
    }

    @Test
    public void testFilteredDetailedSecondaryDexPublic() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getFilteredDetailedSecondaryDexInfo(OWNING_PKG_NAME))
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a", "armeabi-v7a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */),
                                DexLoader.create(LOADING_PKG_NAME, false /* isolatedProcess */)),
                        true /* isUsedByOtherApps */, true /* isDexFilePublic */));
    }

    @Test
    public void testFilteredDetailedSecondaryDexPrivate() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getFilteredDetailedSecondaryDexInfo(OWNING_PKG_NAME))
                .containsExactly(DetailedSecondaryDexInfo.create(mCeDir + "/foo.apk", mUserHandle,
                        "CLC", Set.of("arm64-v8a"),
                        Set.of(DexLoader.create(OWNING_PKG_NAME, false /* isolatedProcess */)),
                        false /* isUsedByOtherApps */, false /* isDexFilePublic */));
    }

    @Test
    public void testFilteredDetailedSecondaryDexFilteredDueToVisibility() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk"))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, LOADING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        when(Process.isIsolatedUid(anyInt())).thenReturn(true);
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getFilteredDetailedSecondaryDexInfo(OWNING_PKG_NAME)).isEmpty();
    }

    @Test
    public void testFilteredDetailedSecondaryDexFilteredDueToNotFound() throws Exception {
        when(mArtd.getDexFileVisibility(mCeDir + "/foo.apk")).thenReturn(FileVisibility.NOT_FOUND);

        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of(mCeDir + "/foo.apk", "CLC"));

        assertThat(mDexUseManager.getFilteredDetailedSecondaryDexInfo(OWNING_PKG_NAME)).isEmpty();
    }

    @Test(expected = IllegalArgumentException.class)
    public void testUnknownPackage() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, "bogus", Map.of(BASE_APK, "CLC"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testEmptyMap() {
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, Map.of());
    }

    @Test(expected = IllegalArgumentException.class)
    public void testNullKey() {
        var map = new HashMap<String, String>();
        map.put(null, "CLC");
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, map);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testNonAbsoluteKey() {
        mDexUseManager.notifyDexContainersLoaded(
                mSnapshot, OWNING_PKG_NAME, Map.of("a/b.jar", "CLC"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testNullValue() {
        var map = new HashMap<String, String>();
        map.put(mCeDir + "/foo.apk", null);
        mDexUseManager.notifyDexContainersLoaded(mSnapshot, OWNING_PKG_NAME, map);
    }

    private AndroidPackage createPackage(String packageName) {
        AndroidPackage pkg = mock(AndroidPackage.class);
        lenient().when(pkg.getStorageUuid()).thenReturn(StorageManager.UUID_DEFAULT);

        var baseSplit = mock(AndroidPackageSplit.class);
        lenient().when(baseSplit.getPath()).thenReturn("/data/app/" + packageName + "/base.apk");
        lenient().when(baseSplit.isHasCode()).thenReturn(true);
        lenient().when(baseSplit.getClassLoaderName()).thenReturn(PathClassLoader.class.getName());

        var split0 = mock(AndroidPackageSplit.class);
        lenient().when(split0.getName()).thenReturn("split_0");
        lenient().when(split0.getPath()).thenReturn("/data/app/" + packageName + "/split_0.apk");
        lenient().when(split0.isHasCode()).thenReturn(true);

        lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit, split0));

        return pkg;
    }

    private PackageState createPackageState(String packageName, String primaryAbi) {
        PackageState pkgState = mock(PackageState.class);
        lenient().when(pkgState.getPackageName()).thenReturn(packageName);
        AndroidPackage pkg = createPackage(packageName);
        lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);
        lenient().when(pkgState.getPrimaryCpuAbi()).thenReturn(primaryAbi);
        return pkgState;
    }
}
