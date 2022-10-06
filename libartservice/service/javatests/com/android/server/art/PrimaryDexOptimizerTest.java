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

import static com.android.server.art.GetDexoptNeededResult.ArtifactsLocation;
import static com.android.server.art.model.OptimizeResult.DexContainerFileOptimizeResult;
import static com.android.server.art.testing.TestingUtils.deepEq;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.argThat;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.same;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.os.ServiceSpecificException;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.model.OptimizeParams;
import com.android.server.art.model.OptimizeResult;
import com.android.server.art.testing.TestingUtils;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class PrimaryDexOptimizerTest extends PrimaryDexOptimizerTestBase {
    private final OptimizeParams mOptimizeParams =
            new OptimizeParams.Builder("install").setCompilerFilter("speed-profile").build();

    private final String mDexPath = "/data/app/foo/base.apk";
    private final ProfilePath mRefProfile = AidlUtils.buildProfilePathForRef(PKG_NAME, "primary");
    private final ProfilePath mPrebuiltProfile = AidlUtils.buildProfilePathForPrebuilt(mDexPath);
    private final ProfilePath mDmProfile = AidlUtils.buildProfilePathForDm(mDexPath);
    private final OutputProfile mPublicOutputProfile = AidlUtils.buildOutputProfile(
            PKG_NAME, "primary", UID, SHARED_GID, true /* isOtherReadable */);
    private final OutputProfile mPrivateOutputProfile = AidlUtils.buildOutputProfile(
            PKG_NAME, "primary", UID, SHARED_GID, false /* isOtherReadable */);

    private final String mSplit0DexPath = "/data/app/foo/split_0.apk";
    private final ProfilePath mSplit0RefProfile =
            AidlUtils.buildProfilePathForRef(PKG_NAME, "split_0.split");

    private final int mDefaultDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
    private final int mBetterOrSameDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.COMPILER_FILTER_IS_SAME
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE;
    private final int mForceDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE
            | DexoptTrigger.COMPILER_FILTER_IS_SAME | DexoptTrigger.COMPILER_FILTER_IS_WORSE;

    private final DexoptResult mDexoptResult =
            createDexoptResult(false /* cancelled */, 200 /* wallTimeMs */, 200 /* cpuTimeMs */);

    private List<ProfilePath> mUsedProfiles;

    @Before
    public void setUp() throws Exception {
        super.setUp();

        // By default, none of the profiles are usable.
        lenient().when(mArtd.isProfileUsable(any(), any())).thenReturn(false);
        lenient().when(mArtd.copyAndRewriteProfile(any(), any(), any())).thenReturn(false);

        // Dexopt is by default needed and successful.
        lenient()
                .when(mArtd.getDexoptNeeded(any(), any(), any(), any(), anyInt()))
                .thenReturn(dexoptIsNeeded());
        lenient()
                .when(mArtd.dexopt(
                        any(), any(), any(), any(), any(), any(), any(), anyInt(), any(), any()))
                .thenReturn(mDexoptResult);

        lenient()
                .when(mArtd.createCancellationSignal())
                .thenReturn(mock(IArtdCancellationSignal.class));

        mUsedProfiles = new ArrayList<>();
    }

    @Test
    public void testDexoptInputVdex() throws Exception {
        // null.
        doReturn(dexoptIsNeeded(ArtifactsLocation.NONE_OR_ERROR))
                .when(mArtd)
                .getDexoptNeeded(eq(mDexPath), eq("arm64"), any(), any(), anyInt());
        doReturn(mDexoptResult)
                .when(mArtd)
                .dexopt(any(), eq(mDexPath), eq("arm64"), any(), any(), any(), isNull(), anyInt(),
                        any(), any());

        // ArtifactsPath, isInDalvikCache=true.
        doReturn(dexoptIsNeeded(ArtifactsLocation.DALVIK_CACHE))
                .when(mArtd)
                .getDexoptNeeded(eq(mDexPath), eq("arm"), any(), any(), anyInt());
        doReturn(mDexoptResult)
                .when(mArtd)
                .dexopt(any(), eq(mDexPath), eq("arm"), any(), any(), any(),
                        deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPath(
                                mDexPath, "arm", true /* isInDalvikCache */))),
                        anyInt(), any(), any());

        // ArtifactsPath, isInDalvikCache=false.
        doReturn(dexoptIsNeeded(ArtifactsLocation.NEXT_TO_DEX))
                .when(mArtd)
                .getDexoptNeeded(eq(mSplit0DexPath), eq("arm64"), any(), any(), anyInt());
        doReturn(mDexoptResult)
                .when(mArtd)
                .dexopt(any(), eq(mSplit0DexPath), eq("arm64"), any(), any(), any(),
                        deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPath(
                                mSplit0DexPath, "arm64", false /* isInDalvikCache */))),
                        anyInt(), any(), any());

        // DexMetadataPath.
        doReturn(dexoptIsNeeded(ArtifactsLocation.DM))
                .when(mArtd)
                .getDexoptNeeded(eq(mSplit0DexPath), eq("arm"), any(), any(), anyInt());
        doReturn(mDexoptResult)
                .when(mArtd)
                .dexopt(any(), eq(mSplit0DexPath), eq("arm"), any(), any(), any(),
                        deepEq(VdexPath.dexMetadataPath(
                                AidlUtils.buildDexMetadataPath(mSplit0DexPath))),
                        anyInt(), any(), any());

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);
    }

    @Test
    public void testDexoptUsesRefProfile() throws Exception {
        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        // Other profiles are also usable, but they shouldn't be used.
        makeProfileUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithPrivateProfile(verify(mArtd), mDexPath, "arm64", mRefProfile);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithPrivateProfile(verify(mArtd), mDexPath, "arm", mRefProfile);

        // There is no profile for split 0, so it should fall back to "verify".
        verify(mArtd).getDexoptNeeded(
                eq(mSplit0DexPath), eq("arm64"), any(), eq("verify"), eq(mDefaultDexoptTrigger));
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm64", "verify");

        verify(mArtd).getDexoptNeeded(
                eq(mSplit0DexPath), eq("arm"), any(), eq("verify"), eq(mDefaultDexoptTrigger));
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm", "verify");

        verifyProfileNotUsed(mPrebuiltProfile);
        verifyProfileNotUsed(mDmProfile);
    }

    @Test
    public void testDexoptUsesPublicRefProfile() throws Exception {
        // The ref profile is usable and public.
        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        // Other profiles are also usable, but they shouldn't be used.
        makeProfileUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm64", mRefProfile);
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm", mRefProfile);

        verifyProfileNotUsed(mPrebuiltProfile);
        verifyProfileNotUsed(mDmProfile);
    }

    @Test
    public void testDexoptUsesPrebuiltProfile() throws Exception {
        makeProfileNotUsable(mRefProfile);
        makeProfileUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        InOrder inOrder = inOrder(mArtd);

        inOrder.verify(mArtd).copyAndRewriteProfile(
                deepEq(mPrebuiltProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        checkDexoptWithPublicProfile(inOrder.verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));
        checkDexoptWithPublicProfile(inOrder.verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));

        inOrder.verify(mArtd).commitTmpProfile(deepEq(mPublicOutputProfile.profilePath));

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mDmProfile);
    }

    @Test
    public void testDexoptMergesProfiles() throws Exception {
        when(mPkgState.getUserStateOrDefault(0 /* userId */)).thenReturn(mPkgUserStateInstalled);
        when(mPkgState.getUserStateOrDefault(2 /* userId */)).thenReturn(mPkgUserStateInstalled);

        when(mArtd.mergeProfiles(any(), any(), any(), any())).thenReturn(true);

        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        InOrder inOrder = inOrder(mArtd);

        inOrder.verify(mArtd).mergeProfiles(
                deepEq(List.of(
                        AidlUtils.buildProfilePathForCur(0 /* userId */, PKG_NAME, "primary"),
                        AidlUtils.buildProfilePathForCur(2 /* userId */, PKG_NAME, "primary"))),
                deepEq(mRefProfile), deepEq(mPrivateOutputProfile), eq(mDexPath));

        // It should use `mBetterOrSameDexoptTrigger` and the merged profile for both ISAs.
        inOrder.verify(mArtd).getDexoptNeeded(eq(mDexPath), eq("arm64"), any(), eq("speed-profile"),
                eq(mBetterOrSameDexoptTrigger));
        checkDexoptWithPrivateProfile(inOrder.verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpRefProfilePath(mPrivateOutputProfile.profilePath));

        inOrder.verify(mArtd).getDexoptNeeded(eq(mDexPath), eq("arm"), any(), eq("speed-profile"),
                eq(mBetterOrSameDexoptTrigger));
        checkDexoptWithPrivateProfile(inOrder.verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpRefProfilePath(mPrivateOutputProfile.profilePath));

        inOrder.verify(mArtd).commitTmpProfile(deepEq(mPrivateOutputProfile.profilePath));

        inOrder.verify(mArtd).deleteProfile(
                deepEq(AidlUtils.buildProfilePathForCur(0 /* userId */, PKG_NAME, "primary")));
        inOrder.verify(mArtd).deleteProfile(
                deepEq(AidlUtils.buildProfilePathForCur(2 /* userId */, PKG_NAME, "primary")));
    }

    @Test
    public void testDexoptMergesProfilesMergeFailed() throws Exception {
        when(mPkgState.getUserStateOrDefault(0 /* userId */)).thenReturn(mPkgUserStateInstalled);
        when(mPkgState.getUserStateOrDefault(2 /* userId */)).thenReturn(mPkgUserStateInstalled);

        when(mArtd.mergeProfiles(any(), any(), any(), any())).thenReturn(false);

        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        // It should still use "speed-profile", but with the existing reference profile only.
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm64", mRefProfile);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm", mRefProfile);

        verify(mArtd, never()).deleteProfile(any());
        verify(mArtd, never()).commitTmpProfile(any());
    }

    @Test
    public void testDexoptUsesDmProfile() throws Exception {
        makeProfileNotUsable(mRefProfile);
        makeProfileNotUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        verify(mArtd).copyAndRewriteProfile(
                deepEq(mDmProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mPrebuiltProfile);
    }

    @Test
    public void testDexoptDeletesProfileOnFailure() throws Exception {
        makeProfileNotUsable(mRefProfile);
        makeProfileNotUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        when(mArtd.dexopt(any(), eq(mDexPath), any(), any(), any(), any(), any(), anyInt(), any(),
                     any()))
                .thenThrow(ServiceSpecificException.class);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        verify(mArtd).deleteProfile(
                deepEq(ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath)));
        verify(mArtd, never()).commitTmpProfile(deepEq(mPublicOutputProfile.profilePath));
    }

    @Test
    public void testDexoptNeedsToBeShared() throws Exception {
        when(mInjector.isUsedByOtherApps(PKG_NAME)).thenReturn(true);

        // The ref profile is usable but shouldn't be used.
        makeProfileUsable(mRefProfile);

        makeProfileNotUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);

        // The existing artifacts are private.
        when(mArtd.getArtifactsVisibility(
                     argThat(artifactsPath -> artifactsPath.dexPath == mDexPath)))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        verify(mArtd).copyAndRewriteProfile(
                deepEq(mDmProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        // It should re-compile anyway.
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mForceDexoptTrigger));
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mForceDexoptTrigger));
        checkDexoptWithPublicProfile(verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpRefProfilePath(mPublicOutputProfile.profilePath));

        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm64", "speed");
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm", "speed");

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mPrebuiltProfile);
    }

    @Test
    public void testDexoptNeedsToBeSharedArtifactsArePublic() throws Exception {
        // Same setup as above, but the existing artifacts are public.
        when(mInjector.isUsedByOtherApps(PKG_NAME)).thenReturn(true);
        makeProfileUsable(mRefProfile);
        makeProfileNotUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);
        when(mArtd.getArtifactsVisibility(
                     argThat(artifactsPath -> artifactsPath.dexPath == mDexPath)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        // It should use the default dexopt trigger.
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
    }

    @Test
    public void testDexoptUsesProfileForSplit() throws Exception {
        makeProfileUsable(mSplit0RefProfile);
        when(mArtd.getProfileVisibility(deepEq(mSplit0RefProfile)))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal);

        verify(mArtd).getDexoptNeeded(eq(mSplit0DexPath), eq("arm64"), any(), eq("speed-profile"),
                eq(mDefaultDexoptTrigger));
        checkDexoptWithPrivateProfile(verify(mArtd), mSplit0DexPath, "arm64", mSplit0RefProfile);

        verify(mArtd).getDexoptNeeded(eq(mSplit0DexPath), eq("arm"), any(), eq("speed-profile"),
                eq(mDefaultDexoptTrigger));
        checkDexoptWithPrivateProfile(verify(mArtd), mSplit0DexPath, "arm", mSplit0RefProfile);
    }

    @Test
    public void testDexoptCancelledBeforeDexopt() throws Exception {
        mCancellationSignal.cancel();

        var artdCancellationSignal = mock(IArtdCancellationSignal.class);
        when(mArtd.createCancellationSignal()).thenReturn(artdCancellationSignal);

        doAnswer(invocation -> {
            verify(artdCancellationSignal).cancel();
            return createDexoptResult(
                    true /* cancelled */, 200 /* wallTimeMs */, 200 /* cpuTimeMs */);
        })
                .when(mArtd)
                .dexopt(any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        same(artdCancellationSignal));

        // The result should only contain one element: the result of the first file with
        // OPTIMIZE_CANCELLED.
        assertThat(
                mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams, mCancellationSignal)
                        .stream()
                        .map(DexContainerFileOptimizeResult::getStatus)
                        .collect(Collectors.toList()))
                .containsExactly(OptimizeResult.OPTIMIZE_CANCELLED);

        // It shouldn't continue after being cancelled on the first file.
        verify(mArtd, times(1)).createCancellationSignal();
        verify(mArtd, times(1))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), anyInt(), any(), any());
    }

    @Test
    public void testDexoptCancelledDuringDexopt() throws Exception {
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        final long TIMEOUT_SEC = 1;

        var artdCancellationSignal = mock(IArtdCancellationSignal.class);
        when(mArtd.createCancellationSignal()).thenReturn(artdCancellationSignal);

        doAnswer(invocation -> {
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return createDexoptResult(
                    true /* cancelled */, 200 /* wallTimeMs */, 200 /* cpuTimeMs */);
        })
                .when(mArtd)
                .dexopt(any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        same(artdCancellationSignal));
        doAnswer(invocation -> {
            dexoptCancelled.release();
            return null;
        })
                .when(artdCancellationSignal)
                .cancel();

        Future<List<DexContainerFileOptimizeResult>> results =
                Executors.newSingleThreadExecutor().submit(() -> {
                    return mPrimaryDexOptimizer.dexopt(
                            mPkgState, mPkg, mOptimizeParams, mCancellationSignal);
                });

        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        mCancellationSignal.cancel();

        assertThat(results.get()
                           .stream()
                           .map(DexContainerFileOptimizeResult::getStatus)
                           .collect(Collectors.toList()))
                .containsExactly(OptimizeResult.OPTIMIZE_CANCELLED);

        // It shouldn't continue after being cancelled on the first file.
        verify(mArtd, times(1)).createCancellationSignal();
        verify(mArtd, times(1))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), anyInt(), any(), any());
    }

    private void checkDexoptWithPublicProfile(
            IArtd artd, String dexPath, String isa, ProfilePath profile) throws Exception {
        artd.dexopt(
                argThat(artifacts
                        -> artifacts.permissionSettings.fileFsPermission.isOtherReadable == true),
                eq(dexPath), eq(isa), any(), eq("speed-profile"), deepEq(profile), any(), anyInt(),
                argThat(dexoptOptions -> dexoptOptions.generateAppImage == true), any());
    }

    private void checkDexoptWithPrivateProfile(
            IArtd artd, String dexPath, String isa, ProfilePath profile) throws Exception {
        artd.dexopt(
                argThat(artifacts
                        -> artifacts.permissionSettings.fileFsPermission.isOtherReadable == false),
                eq(dexPath), eq(isa), any(), eq("speed-profile"), deepEq(profile), any(), anyInt(),
                argThat(dexoptOptions -> dexoptOptions.generateAppImage == true), any());
    }

    private void checkDexoptWithNoProfile(
            IArtd artd, String dexPath, String isa, String compilerFilter) throws Exception {
        artd.dexopt(
                argThat(artifacts
                        -> artifacts.permissionSettings.fileFsPermission.isOtherReadable == true),
                eq(dexPath), eq(isa), any(), eq(compilerFilter), isNull(), any(), anyInt(),
                argThat(dexoptOptions -> dexoptOptions.generateAppImage == false), any());
    }

    private void verifyProfileNotUsed(ProfilePath profile) throws Exception {
        assertThat(mUsedProfiles)
                .comparingElementsUsing(TestingUtils.<ProfilePath>deepEquality())
                .doesNotContain(profile);
    }

    private void makeProfileUsable(ProfilePath profile) throws Exception {
        lenient().when(mArtd.isProfileUsable(deepEq(profile), any())).thenAnswer(invocation -> {
            mUsedProfiles.add(invocation.<ProfilePath>getArgument(0));
            return true;
        });
        lenient()
                .when(mArtd.copyAndRewriteProfile(deepEq(profile), any(), any()))
                .thenAnswer(invocation -> {
                    mUsedProfiles.add(invocation.<ProfilePath>getArgument(0));
                    return true;
                });
    }

    private void makeProfileNotUsable(ProfilePath profile) throws Exception {
        lenient().when(mArtd.isProfileUsable(deepEq(profile), any())).thenReturn(false);
        lenient()
                .when(mArtd.copyAndRewriteProfile(deepEq(profile), any(), any()))
                .thenReturn(false);
    }
}
