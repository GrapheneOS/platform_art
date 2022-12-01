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
 * limitations under the License
 */

package com.android.server.art;

import static android.os.ParcelFileDescriptor.AutoCloseInputStream;

import static com.android.server.art.model.OptimizationStatus.DexContainerFileOptimizationStatus;
import static com.android.server.art.testing.TestingUtils.deepEq;
import static com.android.server.art.testing.TestingUtils.inAnyOrder;
import static com.android.server.art.testing.TestingUtils.inAnyOrderDeepEquals;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.AdditionalMatchers.not;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.same;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import android.apphibernation.AppHibernationManager;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;

import androidx.test.filters.SmallTest;

import com.android.server.art.model.Config;
import com.android.server.art.model.DeleteResult;
import com.android.server.art.model.OptimizationStatus;
import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.AndroidPackage;
import com.android.server.pm.pkg.AndroidPackageSplit;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.PackageUserState;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;
import org.mockito.Mock;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.ForkJoinPool;
import java.util.function.Consumer;
import java.util.stream.Collectors;

@SmallTest
@RunWith(Parameterized.class)
public class ArtManagerLocalTest {
    private static final String PKG_NAME = "com.example.foo";
    private static final String PKG_NAME_SYS_UI = "com.android.systemui";
    private static final String PKG_NAME_HIBERNATING = "com.example.hibernating";

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, Constants.class);

    @Mock private ArtManagerLocal.Injector mInjector;
    @Mock private PackageManagerLocal mPackageManagerLocal;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    @Mock private IArtd mArtd;
    @Mock private DexOptHelper mDexOptHelper;
    @Mock private AppHibernationManager mAppHibernationManager;
    @Mock private UserManager mUserManager;
    private PackageState mPkgState;
    private AndroidPackage mPkg;
    private Config mConfig;

    // True if the primary dex'es are in a readonly partition.
    @Parameter(0) public boolean mIsInReadonlyPartition;

    private ArtManagerLocal mArtManagerLocal;

    @Parameters(name = "isInReadonlyPartition={0}")
    public static Iterable<? extends Object> data() {
        return List.of(false, true);
    }

    @Before
    public void setUp() throws Exception {
        mConfig = new Config();

        // Use `lenient()` to suppress `UnnecessaryStubbingException` thrown by the strict stubs.
        // These are the default test setups. They may or may not be used depending on the code path
        // that each test case examines.
        lenient().when(mInjector.getPackageManagerLocal()).thenReturn(mPackageManagerLocal);
        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.getDexOptHelper()).thenReturn(mDexOptHelper);
        lenient().when(mInjector.getConfig()).thenReturn(mConfig);
        lenient().when(mInjector.getAppHibernationManager()).thenReturn(mAppHibernationManager);
        lenient().when(mInjector.getUserManager()).thenReturn(mUserManager);
        lenient().when(mInjector.isSystemUiPackage(any())).thenReturn(false);
        lenient().when(mInjector.isSystemUiPackage(PKG_NAME_SYS_UI)).thenReturn(true);

        lenient().when(SystemProperties.get(eq("pm.dexopt.install"))).thenReturn("speed-profile");
        lenient().when(SystemProperties.get(eq("pm.dexopt.bg-dexopt"))).thenReturn("speed-profile");
        lenient().when(SystemProperties.get(eq("pm.dexopt.first-boot"))).thenReturn("verify");
        lenient()
                .when(SystemProperties.get(eq("pm.dexopt.boot-after-mainline-update")))
                .thenReturn("verify");
        lenient()
                .when(SystemProperties.getInt(eq("pm.dexopt.bg-dexopt.concurrency"), anyInt()))
                .thenReturn(3);
        lenient()
                .when(SystemProperties.getInt(
                        eq("pm.dexopt.boot-after-mainline-update.concurrency"), anyInt()))
                .thenReturn(3);

        // No ISA translation.
        lenient()
                .when(SystemProperties.get(argThat(arg -> arg.startsWith("ro.dalvik.vm.isa."))))
                .thenReturn("");

        lenient().when(Constants.getPreferredAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("arm64-v8a");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("armeabi-v7a");

        lenient().when(mAppHibernationManager.isHibernatingGlobally(any())).thenReturn(false);
        lenient().when(mAppHibernationManager.isOatArtifactDeletionEnabled()).thenReturn(true);

        lenient()
                .when(mUserManager.getUserHandles(anyBoolean()))
                .thenReturn(List.of(UserHandle.of(0), UserHandle.of(1)));

        lenient().when(mPackageManagerLocal.withFilteredSnapshot()).thenReturn(mSnapshot);
        List<PackageState> pkgStates = createPackageStates();
        for (PackageState pkgState : pkgStates) {
            lenient()
                    .when(mSnapshot.getPackageState(pkgState.getPackageName()))
                    .thenReturn(pkgState);
        }
        var packageStateMap = pkgStates.stream().collect(
                Collectors.toMap(PackageState::getPackageName, it -> it));
        lenient().when(mSnapshot.getPackageStates()).thenReturn(packageStateMap);
        mPkgState = mSnapshot.getPackageState(PKG_NAME);
        mPkg = mPkgState.getAndroidPackage();

        mArtManagerLocal = new ArtManagerLocal(mInjector);
    }

    @Test
    public void testDeleteOptimizedArtifacts() throws Exception {
        when(mArtd.deleteArtifacts(any())).thenReturn(1l);

        DeleteResult result = mArtManagerLocal.deleteOptimizedArtifacts(mSnapshot, PKG_NAME);
        assertThat(result.getFreedBytes()).isEqualTo(4);

        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("arm64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("arm")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("arm64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("arm")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verifyNoMoreInteractions(mArtd);
    }

    @Test
    public void testDeleteOptimizedArtifactsTranslatedIsas() throws Exception {
        lenient().when(SystemProperties.get("ro.dalvik.vm.isa.arm64")).thenReturn("x86_64");
        lenient().when(SystemProperties.get("ro.dalvik.vm.isa.arm")).thenReturn("x86");
        lenient().when(Constants.getPreferredAbi()).thenReturn("x86_64");
        lenient().when(Constants.getNative64BitAbi()).thenReturn("x86_64");
        lenient().when(Constants.getNative32BitAbi()).thenReturn("x86");

        when(mArtd.deleteArtifacts(any())).thenReturn(1l);

        DeleteResult result = mArtManagerLocal.deleteOptimizedArtifacts(mSnapshot, PKG_NAME);
        assertThat(result.getFreedBytes()).isEqualTo(4);

        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("x86_64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/base.apk")
                        && artifactsPath.isa.equals("x86")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("x86_64")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verify(mArtd).deleteArtifacts(argThat(artifactsPath
                -> artifactsPath.dexPath.equals("/data/app/foo/split_0.apk")
                        && artifactsPath.isa.equals("x86")
                        && artifactsPath.isInDalvikCache == mIsInReadonlyPartition));
        verifyNoMoreInteractions(mArtd);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDeleteOptimizedArtifactsPackageNotFound() throws Exception {
        when(mSnapshot.getPackageState(anyString())).thenReturn(null);

        mArtManagerLocal.deleteOptimizedArtifacts(mSnapshot, PKG_NAME);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDeleteOptimizedArtifactsNoPackage() throws Exception {
        when(mPkgState.getAndroidPackage()).thenReturn(null);

        mArtManagerLocal.deleteOptimizedArtifacts(mSnapshot, PKG_NAME);
    }

    @Test
    public void testGetOptimizationStatus() throws Exception {
        when(mArtd.getOptimizationStatus(any(), any(), any()))
                .thenReturn(createGetOptimizationStatusResult(
                                    "speed", "compilation-reason-0", "location-debug-string-0"),
                        createGetOptimizationStatusResult(
                                "speed-profile", "compilation-reason-1", "location-debug-string-1"),
                        createGetOptimizationStatusResult(
                                "verify", "compilation-reason-2", "location-debug-string-2"),
                        createGetOptimizationStatusResult(
                                "extract", "compilation-reason-3", "location-debug-string-3"));

        OptimizationStatus result = mArtManagerLocal.getOptimizationStatus(mSnapshot, PKG_NAME);

        List<DexContainerFileOptimizationStatus> statuses =
                result.getDexContainerFileOptimizationStatuses();
        assertThat(statuses.size()).isEqualTo(4);

        assertThat(statuses.get(0).getDexContainerFile()).isEqualTo("/data/app/foo/base.apk");
        assertThat(statuses.get(0).isPrimaryAbi()).isEqualTo(true);
        assertThat(statuses.get(0).getAbi()).isEqualTo("arm64-v8a");
        assertThat(statuses.get(0).getCompilerFilter()).isEqualTo("speed");
        assertThat(statuses.get(0).getCompilationReason()).isEqualTo("compilation-reason-0");
        assertThat(statuses.get(0).getLocationDebugString()).isEqualTo("location-debug-string-0");

        assertThat(statuses.get(1).getDexContainerFile()).isEqualTo("/data/app/foo/base.apk");
        assertThat(statuses.get(1).isPrimaryAbi()).isEqualTo(false);
        assertThat(statuses.get(1).getAbi()).isEqualTo("armeabi-v7a");
        assertThat(statuses.get(1).getCompilerFilter()).isEqualTo("speed-profile");
        assertThat(statuses.get(1).getCompilationReason()).isEqualTo("compilation-reason-1");
        assertThat(statuses.get(1).getLocationDebugString()).isEqualTo("location-debug-string-1");

        assertThat(statuses.get(2).getDexContainerFile()).isEqualTo("/data/app/foo/split_0.apk");
        assertThat(statuses.get(2).isPrimaryAbi()).isEqualTo(true);
        assertThat(statuses.get(2).getAbi()).isEqualTo("arm64-v8a");
        assertThat(statuses.get(2).getCompilerFilter()).isEqualTo("verify");
        assertThat(statuses.get(2).getCompilationReason()).isEqualTo("compilation-reason-2");
        assertThat(statuses.get(2).getLocationDebugString()).isEqualTo("location-debug-string-2");

        assertThat(statuses.get(3).getDexContainerFile()).isEqualTo("/data/app/foo/split_0.apk");
        assertThat(statuses.get(3).isPrimaryAbi()).isEqualTo(false);
        assertThat(statuses.get(3).getAbi()).isEqualTo("armeabi-v7a");
        assertThat(statuses.get(3).getCompilerFilter()).isEqualTo("extract");
        assertThat(statuses.get(3).getCompilationReason()).isEqualTo("compilation-reason-3");
        assertThat(statuses.get(3).getLocationDebugString()).isEqualTo("location-debug-string-3");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testGetOptimizationStatusPackageNotFound() throws Exception {
        when(mSnapshot.getPackageState(anyString())).thenReturn(null);

        mArtManagerLocal.getOptimizationStatus(mSnapshot, PKG_NAME);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testGetOptimizationStatusNoPackage() throws Exception {
        when(mPkgState.getAndroidPackage()).thenReturn(null);

        mArtManagerLocal.getOptimizationStatus(mSnapshot, PKG_NAME);
    }

    @Test
    public void testGetOptimizationStatusNonFatalError() throws Exception {
        when(mArtd.getOptimizationStatus(any(), any(), any()))
                .thenThrow(new ServiceSpecificException(1 /* errorCode */, "some error message"));

        OptimizationStatus result = mArtManagerLocal.getOptimizationStatus(mSnapshot, PKG_NAME);

        List<DexContainerFileOptimizationStatus> statuses =
                result.getDexContainerFileOptimizationStatuses();
        assertThat(statuses.size()).isEqualTo(4);

        for (DexContainerFileOptimizationStatus status : statuses) {
            assertThat(status.getCompilerFilter()).isEqualTo("error");
            assertThat(status.getCompilationReason()).isEqualTo("error");
            assertThat(status.getLocationDebugString()).isEqualTo("some error message");
        }
    }

    @Test
    public void testOptimizePackage() throws Exception {
        var params = new OptimizeParams.Builder("install").build();
        var result = mock(OptimizeResult.class);
        var cancellationSignal = new CancellationSignal();

        when(mDexOptHelper.dexopt(any(), deepEq(List.of(PKG_NAME)), same(params),
                     same(cancellationSignal), any()))
                .thenReturn(result);

        assertThat(
                mArtManagerLocal.optimizePackage(mSnapshot, PKG_NAME, params, cancellationSignal))
                .isSameInstanceAs(result);
    }

    @Test
    public void testOptimizePackages() throws Exception {
        var result = mock(OptimizeResult.class);
        var cancellationSignal = new CancellationSignal();

        // It should use the default package list and params.
        when(mDexOptHelper.dexopt(any(), inAnyOrder(PKG_NAME, PKG_NAME_SYS_UI), any(),
                     same(cancellationSignal), any(), any(), any()))
                .thenReturn(result);

        assertThat(mArtManagerLocal.optimizePackages(mSnapshot, "bg-dexopt", cancellationSignal,
                           null /* processCallbackExecutor */, null /* processCallback */))
                .isSameInstanceAs(result);
    }

    @Test
    public void testOptimizePackagesBootAfterMainlineUpdate() throws Exception {
        var result = mock(OptimizeResult.class);
        var cancellationSignal = new CancellationSignal();

        // It should only optimize system UI.
        when(mDexOptHelper.dexopt(
                     any(), deepEq(List.of(PKG_NAME_SYS_UI)), any(), any(), any(), any(), any()))
                .thenReturn(result);

        assertThat(mArtManagerLocal.optimizePackages(mSnapshot, "boot-after-mainline-update",
                           cancellationSignal, null /* processCallbackExecutor */,
                           null /* processCallback */))
                .isSameInstanceAs(result);
    }

    @Test
    public void testOptimizePackagesOverride() throws Exception {
        var params = new OptimizeParams.Builder("bg-dexopt").build();
        var result = mock(OptimizeResult.class);
        var cancellationSignal = new CancellationSignal();

        mArtManagerLocal.setOptimizePackagesCallback(
                ForkJoinPool.commonPool(), (snapshot, reason, defaultPackages, builder) -> {
                    assertThat(reason).isEqualTo("bg-dexopt");
                    assertThat(defaultPackages).containsExactly(PKG_NAME, PKG_NAME_SYS_UI);
                    builder.setPackages(List.of(PKG_NAME)).setOptimizeParams(params);
                });

        // It should use the overridden package list and params.
        when(mDexOptHelper.dexopt(any(), deepEq(List.of(PKG_NAME)), same(params),
                     same(cancellationSignal), any(), any(), any()))
                .thenReturn(result);

        assertThat(mArtManagerLocal.optimizePackages(mSnapshot, "bg-dexopt", cancellationSignal,
                           null /* processCallbackExecutor */, null /* processCallback */))
                .isSameInstanceAs(result);
    }

    @Test
    public void testOptimizePackagesOverrideCleared() throws Exception {
        var params = new OptimizeParams.Builder("bg-dexopt").build();
        var result = mock(OptimizeResult.class);
        var cancellationSignal = new CancellationSignal();

        mArtManagerLocal.setOptimizePackagesCallback(
                ForkJoinPool.commonPool(), (snapshot, reason, defaultPackages, builder) -> {
                    builder.setPackages(List.of(PKG_NAME)).setOptimizeParams(params);
                });
        mArtManagerLocal.clearOptimizePackagesCallback();

        // It should use the default package list and params.
        when(mDexOptHelper.dexopt(any(), inAnyOrder(PKG_NAME, PKG_NAME_SYS_UI), not(same(params)),
                     same(cancellationSignal), any(), any(), any()))
                .thenReturn(result);

        assertThat(mArtManagerLocal.optimizePackages(mSnapshot, "bg-dexopt", cancellationSignal,
                           null /* processCallbackExecutor */, null /* processCallback */))
                .isSameInstanceAs(result);
    }

    @Test(expected = IllegalStateException.class)
    public void testOptimizePackagesOverrideReasonChanged() throws Exception {
        var params = new OptimizeParams.Builder("first-boot").build();
        var cancellationSignal = new CancellationSignal();

        mArtManagerLocal.setOptimizePackagesCallback(
                ForkJoinPool.commonPool(), (snapshot, reason, defaultPackages, builder) -> {
                    builder.setOptimizeParams(params);
                });

        mArtManagerLocal.optimizePackages(mSnapshot, "bg-dexopt", cancellationSignal,
                null /* processCallbackExecutor */, null /* processCallback */);
    }

    @Test
    public void testSnapshotAppProfile() throws Exception {
        var options = new MergeProfileOptions();
        options.forceMerge = true;
        options.forBootImage = false;

        File tempFile = File.createTempFile("primary", ".prof");
        tempFile.deleteOnExit();

        when(mArtd.mergeProfiles(
                     deepEq(List.of(AidlUtils.buildProfilePathForPrimaryRef(PKG_NAME, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, PKG_NAME, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, PKG_NAME, "primary"))),
                     isNull(),
                     deepEq(AidlUtils.buildOutputProfileForPrimary(PKG_NAME, "primary",
                             Process.SYSTEM_UID, Process.SYSTEM_UID, false /* isPublic */)),
                     deepEq(List.of("/data/app/foo/base.apk")), deepEq(options)))
                .thenAnswer(invocation -> {
                    try (var writer = new FileWriter(tempFile)) {
                        writer.write("snapshot");
                    } catch (IOException e) {
                        throw new RuntimeException(e);
                    }
                    var output = invocation.<OutputProfile>getArgument(2);
                    output.profilePath.tmpPath = tempFile.getPath();
                    return true;
                });

        ParcelFileDescriptor fd =
                mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, null /* splitName */);

        verify(mArtd).deleteProfile(
                argThat(profile -> profile.getTmpProfilePath().tmpPath.equals(tempFile.getPath())));

        try (InputStream inputStream = new AutoCloseInputStream(fd)) {
            String contents = new String(inputStream.readAllBytes(), StandardCharsets.UTF_8);
            assertThat(contents).isEqualTo("snapshot");
        }
    }

    @Test
    public void testSnapshotAppProfileSplit() throws Exception {
        when(mArtd.mergeProfiles(deepEq(List.of(AidlUtils.buildProfilePathForPrimaryRef(
                                                        PKG_NAME, "split_0.split"),
                                         AidlUtils.buildProfilePathForPrimaryCur(
                                                 0 /* userId */, PKG_NAME, "split_0.split"),
                                         AidlUtils.buildProfilePathForPrimaryCur(
                                                 1 /* userId */, PKG_NAME, "split_0.split"))),
                     isNull(),
                     deepEq(AidlUtils.buildOutputProfileForPrimary(PKG_NAME, "split_0.split",
                             Process.SYSTEM_UID, Process.SYSTEM_UID, false /* isPublic */)),
                     deepEq(List.of("/data/app/foo/split_0.apk")), any()))
                .thenReturn(false);

        mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, "split_0");
    }

    @Test
    public void testSnapshotAppProfileEmpty() throws Exception {
        when(mArtd.mergeProfiles(any(), any(), any(), any(), any())).thenReturn(false);

        ParcelFileDescriptor fd =
                mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, null /* splitName */);

        verify(mArtd, never()).deleteProfile(any());

        try (InputStream inputStream = new AutoCloseInputStream(fd)) {
            assertThat(inputStream.readAllBytes()).isEmpty();
        }
    }

    @Test(expected = IllegalArgumentException.class)
    public void testSnapshotAppProfilePackageNotFound() throws Exception {
        when(mSnapshot.getPackageState(anyString())).thenReturn(null);

        mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, null /* splitName */);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testSnapshotAppProfileNoPackage() throws Exception {
        when(mPkgState.getAndroidPackage()).thenReturn(null);

        mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, null /* splitName */);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testSnapshotAppProfileSplitNotFound() throws Exception {
        mArtManagerLocal.snapshotAppProfile(mSnapshot, PKG_NAME, "non-existent-split");
    }

    @Test
    public void testSnapshotBootImageProfile() throws Exception {
        // `lenient()` is required to allow mocking the same method multiple times.
        lenient().when(Constants.getenv("BOOTCLASSPATH")).thenReturn("bcp0:bcp1");
        lenient().when(Constants.getenv("SYSTEMSERVERCLASSPATH")).thenReturn("sscp0:sscp1");
        lenient().when(Constants.getenv("STANDALONE_SYSTEMSERVER_JARS")).thenReturn("sssj0:sssj1");

        var options = new MergeProfileOptions();
        options.forceMerge = true;
        options.forBootImage = true;

        when(mArtd.mergeProfiles(
                     inAnyOrderDeepEquals(
                             AidlUtils.buildProfilePathForPrimaryRef("android", "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, "android", "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, "android", "primary"),
                             AidlUtils.buildProfilePathForPrimaryRef(PKG_NAME, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, PKG_NAME, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, PKG_NAME, "primary"),
                             AidlUtils.buildProfilePathForPrimaryRef(PKG_NAME, "split_0.split"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, PKG_NAME, "split_0.split"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, PKG_NAME, "split_0.split"),
                             AidlUtils.buildProfilePathForPrimaryRef(PKG_NAME_SYS_UI, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, PKG_NAME_SYS_UI, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, PKG_NAME_SYS_UI, "primary"),
                             AidlUtils.buildProfilePathForPrimaryRef(
                                     PKG_NAME_HIBERNATING, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     0 /* userId */, PKG_NAME_HIBERNATING, "primary"),
                             AidlUtils.buildProfilePathForPrimaryCur(
                                     1 /* userId */, PKG_NAME_HIBERNATING, "primary")),
                     isNull(),
                     deepEq(AidlUtils.buildOutputProfileForPrimary("android", "primary",
                             Process.SYSTEM_UID, Process.SYSTEM_UID, false /* isPublic */)),
                     deepEq(List.of("bcp0", "bcp1", "sscp0", "sscp1", "sssj0", "sssj1")),
                     deepEq(options)))
                .thenReturn(false); // A non-empty merge is tested in `testSnapshotAppProfile`.

        mArtManagerLocal.snapshotBootImageProfile(mSnapshot);
    }

    private AndroidPackage createPackage(boolean multiSplit) {
        AndroidPackage pkg = mock(AndroidPackage.class);

        var baseSplit = mock(AndroidPackageSplit.class);
        lenient().when(baseSplit.getPath()).thenReturn("/data/app/foo/base.apk");
        lenient().when(baseSplit.isHasCode()).thenReturn(true);

        if (multiSplit) {
            // split_0 has code while split_1 doesn't.
            var split0 = mock(AndroidPackageSplit.class);
            lenient().when(split0.getName()).thenReturn("split_0");
            lenient().when(split0.getPath()).thenReturn("/data/app/foo/split_0.apk");
            lenient().when(split0.isHasCode()).thenReturn(true);
            var split1 = mock(AndroidPackageSplit.class);
            lenient().when(split1.getName()).thenReturn("split_1");
            lenient().when(split1.getPath()).thenReturn("/data/app/foo/split_1.apk");
            lenient().when(split1.isHasCode()).thenReturn(false);

            lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit, split0, split1));
        } else {
            lenient().when(pkg.getSplits()).thenReturn(List.of(baseSplit));
        }

        return pkg;
    }

    private PackageUserState createPackageUserState() {
        PackageUserState pkgUserState = mock(PackageUserState.class);
        lenient().when(pkgUserState.isInstalled()).thenReturn(true);
        return pkgUserState;
    }

    private PackageState createPackageState(
            String packageName, int appId, boolean hasPackage, boolean multiSplit) {
        PackageState pkgState = mock(PackageState.class);

        lenient().when(pkgState.getPackageName()).thenReturn(packageName);
        lenient().when(pkgState.getPrimaryCpuAbi()).thenReturn("arm64-v8a");
        lenient().when(pkgState.getSecondaryCpuAbi()).thenReturn("armeabi-v7a");
        lenient().when(pkgState.isSystem()).thenReturn(mIsInReadonlyPartition);
        lenient().when(pkgState.isUpdatedSystemApp()).thenReturn(false);
        lenient().when(pkgState.getAppId()).thenReturn(appId);

        if (hasPackage) {
            AndroidPackage pkg = createPackage(multiSplit);
            lenient().when(pkgState.getAndroidPackage()).thenReturn(pkg);
        } else {
            lenient().when(pkgState.getAndroidPackage()).thenReturn(null);
        }

        PackageUserState pkgUserState = createPackageUserState();
        lenient().when(pkgState.getStateForUser(any())).thenReturn(pkgUserState);

        return pkgState;
    }

    private List<PackageState> createPackageStates() {
        PackageState pkgState = createPackageState(
                PKG_NAME, 10001 /* appId */, true /* hasPackage */, true /* multiSplit */);

        PackageState sysUiPkgState = createPackageState(
                PKG_NAME_SYS_UI, 1234 /* appId */, true /* hasPackage */, false /* multiSplit */);

        // This should not be optimized because it's hibernating. However, it should be included
        // when snapshotting boot image profile.
        PackageState pkgHibernatingState = createPackageState(PKG_NAME_HIBERNATING,
                10002 /* appId */, true /* hasPackage */, false /* multiSplit */);
        lenient()
                .when(mAppHibernationManager.isHibernatingGlobally(PKG_NAME_HIBERNATING))
                .thenReturn(true);

        // This should not be optimized because it does't have AndroidPackage.
        PackageState nullPkgState = createPackageState("com.example.null", 10003 /* appId */,
                false /* hasPackage */, false /* multiSplit */);

        // This should not be optimized because it has a negative app id.
        PackageState apexPkgState = createPackageState(
                "com.android.art", -1 /* appId */, true /* hasPackage */, false /* multiSplit */);

        // This should not be optimized because it's "android".
        PackageState platformPkgState = createPackageState(Utils.PLATFORM_PACKAGE_NAME,
                1000 /* appId */, true /* hasPackage */, false /* multiSplit */);

        return List.of(pkgState, sysUiPkgState, pkgHibernatingState, nullPkgState, apexPkgState,
                platformPkgState);
    }

    private GetOptimizationStatusResult createGetOptimizationStatusResult(
            String compilerFilter, String compilationReason, String locationDebugString) {
        var getOptimizationStatusResult = new GetOptimizationStatusResult();
        getOptimizationStatusResult.compilerFilter = compilerFilter;
        getOptimizationStatusResult.compilationReason = compilationReason;
        getOptimizationStatusResult.locationDebugString = locationDebugString;
        return getOptimizationStatusResult;
    }
}
