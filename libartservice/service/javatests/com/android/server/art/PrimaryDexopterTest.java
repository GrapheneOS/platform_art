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

import static com.android.server.art.OutputArtifacts.PermissionSettings;
import static com.android.server.art.model.DexoptResult.DexContainerFileDexoptResult;
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

import android.os.Process;
import android.os.ServiceSpecificException;
import android.os.UserHandle;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.model.ArtFlags;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.DexoptResult;
import com.android.server.art.proto.DexMetadataConfig;
import com.android.server.art.testing.TestingUtils;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;

import java.nio.file.NoSuchFileException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.Future;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.zip.ZipFile;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class PrimaryDexopterTest extends PrimaryDexopterTestBase {
    private final String mDexPath = "/somewhere/app/foo/base.apk";
    private final String mDmPath = "/somewhere/app/foo/base.dm";
    private final ProfilePath mRefProfile =
            AidlUtils.buildProfilePathForPrimaryRefAsInput(PKG_NAME, "primary");
    private final ProfilePath mPrebuiltProfile = AidlUtils.buildProfilePathForPrebuilt(mDexPath);
    private final ProfilePath mDmProfile = AidlUtils.buildProfilePathForDm(mDexPath);
    private final DexMetadataPath mDmFile = AidlUtils.buildDexMetadataPath(mDexPath);
    private final OutputProfile mPublicOutputProfile =
            AidlUtils.buildOutputProfileForPrimary(PKG_NAME, "primary", Process.SYSTEM_UID,
                    SHARED_GID, true /* isOtherReadable */, false /* isPreReboot */);
    private final OutputProfile mPrivateOutputProfile =
            AidlUtils.buildOutputProfileForPrimary(PKG_NAME, "primary", Process.SYSTEM_UID,
                    SHARED_GID, false /* isOtherReadable */, false /* isPreReboot */);

    private final String mSplit0DexPath = "/somewhere/app/foo/split_0.apk";
    private final ProfilePath mSplit0RefProfile =
            AidlUtils.buildProfilePathForPrimaryRefAsInput(PKG_NAME, "split_0.split");

    private final int mDefaultDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
    private final int mBetterOrSameDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.COMPILER_FILTER_IS_SAME
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE | DexoptTrigger.NEED_EXTRACTION;
    private final int mForceDexoptTrigger = DexoptTrigger.COMPILER_FILTER_IS_BETTER
            | DexoptTrigger.PRIMARY_BOOT_IMAGE_BECOMES_USABLE
            | DexoptTrigger.COMPILER_FILTER_IS_SAME | DexoptTrigger.COMPILER_FILTER_IS_WORSE
            | DexoptTrigger.NEED_EXTRACTION;

    private final MergeProfileOptions mMergeProfileOptions = new MergeProfileOptions();

    private final ArtdDexoptResult mArtdDexoptResult =
            createArtdDexoptResult(false /* cancelled */);

    private DexoptParams mDexoptParams =
            new DexoptParams.Builder("install").setCompilerFilter("speed-profile").build();

    private PrimaryDexopter mPrimaryDexopter;

    private List<ProfilePath> mUsedProfiles;
    private List<String> mUsedEmbeddedProfiles;

    @Before
    public void setUp() throws Exception {
        super.setUp();

        // By default, none of the profiles are usable.
        lenient().when(mArtd.isProfileUsable(any(), any())).thenReturn(false);
        lenient()
                .when(mArtd.copyAndRewriteProfile(any(), any(), any()))
                .thenReturn(TestingUtils.createCopyAndRewriteProfileNoProfile());
        lenient()
                .when(mArtd.copyAndRewriteEmbeddedProfile(any(), any()))
                .thenReturn(TestingUtils.createCopyAndRewriteProfileNoProfile());

        // By default, no DM file exists.
        lenient()
                .when(mDexMetadataHelperInjector.openZipFile(any()))
                .thenThrow(NoSuchFileException.class);

        // By default, no artifacts exist.
        lenient().when(mArtd.getArtifactsVisibility(any())).thenReturn(FileVisibility.NOT_FOUND);

        // Dexopt is by default needed and successful.
        lenient()
                .when(mArtd.getDexoptNeeded(any(), any(), any(), any(), anyInt()))
                .thenReturn(dexoptIsNeeded());
        lenient()
                .when(mArtd.dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(),
                        any(), any()))
                .thenReturn(mArtdDexoptResult);

        lenient()
                .when(mArtd.createCancellationSignal())
                .thenReturn(mock(IArtdCancellationSignal.class));

        lenient()
                .when(mArtd.getDexFileVisibility(mDexPath))
                .thenReturn(FileVisibility.OTHER_READABLE);
        lenient()
                .when(mArtd.getDexFileVisibility(mSplit0DexPath))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        mUsedProfiles = new ArrayList<>();
        mUsedEmbeddedProfiles = new ArrayList<>();
    }

    private void checkDexoptInputVdex(
            @ArtifactsLocation int location, Supplier<VdexPath> inputVdexMatcher) throws Exception {
        doReturn(dexoptIsNeeded(location))
                .when(mArtd)
                .getDexoptNeeded(eq(mDexPath), eq("arm64"), any(), any(), anyInt());

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);
        verify(mArtd).dexopt(any(), eq(mDexPath), eq("arm64"), any(), any(), any(),
                inputVdexMatcher.get(), any(), anyInt(), any(), any());
    }

    @Test
    public void testDexoptInputVdexNoneOrError() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.NONE_OR_ERROR, () -> isNull());
    }

    @Test
    public void testDexoptInputVdexDalvikCache() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.DALVIK_CACHE, () -> {
            return deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPathAsInput(
                    mDexPath, "arm64", true /* isInDalvikCache */)));
        });
    }

    @Test
    public void testDexoptInputVdexNextToDex() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.NEXT_TO_DEX, () -> {
            return deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPathAsInput(
                    mDexPath, "arm64", false /* isInDalvikCache */)));
        });
    }

    @Test
    public void testDexoptInputVdexDm() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.DM, () -> isNull());
    }

    @Test
    public void testDexoptInputVdexSdmDalvikCache() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.SDM_DALVIK_CACHE, () -> isNull());
    }

    @Test
    public void testDexoptInputVdexSdmNextToDex() throws Exception {
        checkDexoptInputVdex(ArtifactsLocation.SDM_NEXT_TO_DEX, () -> isNull());
    }

    @Test
    public void testDexoptDm() throws Exception {
        String dmPath = TestingUtils.createTempZipWithEntry("primary.vdex", new byte[0] /* data */);
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd, times(2))
                .dexopt(any(), eq(mDexPath), any(), any(), any(), any(), any(), deepEq(mDmFile),
                        anyInt(),
                        argThat(dexoptOptions
                                -> dexoptOptions.compilationReason.equals("install-dm")),
                        any());
        verify(mArtd, times(2))
                .dexopt(any(), eq(mSplit0DexPath), any(), any(), any(), any(), any(), isNull(),
                        anyInt(),
                        argThat(dexoptOptions -> dexoptOptions.compilationReason.equals("install")),
                        any());
    }

    @Test
    public void testDexoptUsesRefProfile() throws Exception {
        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        // Other profiles are also usable, but they shouldn't be used.
        makeProfileUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);
        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm64", mRefProfile, false /* isOtherReadable */);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm", mRefProfile, false /* isOtherReadable */);

        // There is no profile for split 0, so it should fall back to "verify".
        verify(mArtd).getDexoptNeeded(
                eq(mSplit0DexPath), eq("arm64"), any(), eq("verify"), eq(mDefaultDexoptTrigger));
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm64", "verify");

        verify(mArtd).getDexoptNeeded(
                eq(mSplit0DexPath), eq("arm"), any(), eq("verify"), eq(mDefaultDexoptTrigger));
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm", "verify");

        verifyProfileNotUsed(mPrebuiltProfile);
        verifyProfileNotUsed(mDmProfile);
        verifyEmbeddedProfileNotUsed(mDexPath);
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
        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm64", mRefProfile, true /* isOtherReadable */);
        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm", mRefProfile, true /* isOtherReadable */);

        verifyProfileNotUsed(mPrebuiltProfile);
        verifyProfileNotUsed(mDmProfile);
        verifyEmbeddedProfileNotUsed(mDexPath);
    }

    @Test
    public void testDexoptUsesPrebuiltProfile() throws Exception {
        makeProfileUsable(mPrebuiltProfile);
        makeProfileUsable(mDmProfile);
        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        InOrder inOrder = inOrder(mArtd);

        inOrder.verify(mArtd).copyAndRewriteProfile(
                deepEq(mPrebuiltProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        checkDexoptWithProfile(inOrder.verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);
        checkDexoptWithProfile(inOrder.verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);

        inOrder.verify(mArtd).commitTmpProfile(deepEq(mPublicOutputProfile.profilePath));

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mDmProfile);
        verifyEmbeddedProfileNotUsed(mDexPath);
    }

    private void checkDexoptMergesProfiles() throws Exception {
        setPackageInstalledForUserIds(0, 2);

        when(mArtd.mergeProfiles(any(), any(), any(), any(), any())).thenReturn(true);

        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        InOrder inOrder = inOrder(mArtd);

        inOrder.verify(mArtd).mergeProfiles(
                deepEq(List.of(AidlUtils.buildProfilePathForPrimaryCur(
                                       0 /* userId */, PKG_NAME, "primary"),
                        AidlUtils.buildProfilePathForPrimaryCur(
                                2 /* userId */, PKG_NAME, "primary"))),
                deepEq(mRefProfile), deepEq(mPrivateOutputProfile), deepEq(List.of(mDexPath)),
                deepEq(mMergeProfileOptions));

        // It should use `mBetterOrSameDexoptTrigger` and the merged profile for both ISAs.
        inOrder.verify(mArtd).getDexoptNeeded(eq(mDexPath), eq("arm64"), any(), eq("speed-profile"),
                eq(mBetterOrSameDexoptTrigger));
        checkDexoptWithProfile(inOrder.verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpProfilePath(mPrivateOutputProfile.profilePath),
                false /* isOtherReadable */);

        inOrder.verify(mArtd).getDexoptNeeded(eq(mDexPath), eq("arm"), any(), eq("speed-profile"),
                eq(mBetterOrSameDexoptTrigger));
        checkDexoptWithProfile(inOrder.verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpProfilePath(mPrivateOutputProfile.profilePath),
                false /* isOtherReadable */);

        inOrder.verify(mArtd).commitTmpProfile(deepEq(mPrivateOutputProfile.profilePath));
    }

    @Test
    public void testDexoptMergesProfiles() throws Exception {
        checkDexoptMergesProfiles();

        verify(mArtd).deleteProfile(deepEq(
                AidlUtils.buildProfilePathForPrimaryCur(0 /* userId */, PKG_NAME, "primary")));
        verify(mArtd).deleteProfile(deepEq(
                AidlUtils.buildProfilePathForPrimaryCur(2 /* userId */, PKG_NAME, "primary")));
    }

    @Test
    public void testDexoptMergesProfilesPreReboot() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);
        mPublicOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;
        mPrivateOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;

        checkDexoptMergesProfiles();

        verify(mArtd, never()).deleteProfile(any());
    }

    @Test
    public void testDexoptMergesProfilesMergeFailed() throws Exception {
        setPackageInstalledForUserIds(0, 2);

        when(mArtd.mergeProfiles(any(), any(), any(), any(), any())).thenReturn(false);

        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        // It should still use "speed-profile", but with the existing reference profile only.
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm64", mRefProfile, true /* isOtherReadable */);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(
                verify(mArtd), mDexPath, "arm", mRefProfile, true /* isOtherReadable */);

        verify(mArtd, never()).deleteProfile(any());
        verify(mArtd, never()).commitTmpProfile(any());
    }

    @Test
    public void testDexoptMergesProfilesForceMerge() throws Exception {
        mDexoptParams = mDexoptParams.toBuilder()
                                .setFlags(ArtFlags.FLAG_FORCE_MERGE_PROFILE,
                                        ArtFlags.FLAG_FORCE_MERGE_PROFILE)
                                .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        setPackageInstalledForUserIds(0, 2);

        mMergeProfileOptions.forceMerge = true;
        when(mArtd.mergeProfiles(any(), any(), any(), any(), deepEq(mMergeProfileOptions)))
                .thenReturn(true);

        makeProfileUsable(mRefProfile);
        when(mArtd.getProfileVisibility(deepEq(mRefProfile)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexopter.dexopt();
    }

    private void checkDexoptUsesDmProfile() throws Exception {
        makeProfileUsable(mDmProfile);
        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd).copyAndRewriteProfile(
                deepEq(mDmProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);
        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mPrebuiltProfile);
        verifyEmbeddedProfileNotUsed(mDexPath);
    }

    @Test
    public void testDexoptUsesDmProfile() throws Exception {
        checkDexoptUsesDmProfile();
    }

    @Test
    public void testDexoptUsesDmProfilePreReboot() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);
        mPublicOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;
        mPrivateOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;

        checkDexoptUsesDmProfile();
    }

    private void checkDexoptUsesEmbeddedProfile() throws Exception {
        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd).copyAndRewriteProfile(
                deepEq(mDmProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);
        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mPrebuiltProfile);
        verifyProfileNotUsed(mDmProfile);
    }

    @Test
    public void testDexoptUsesEmbeddedProfileNoDm() throws Exception {
        checkDexoptUsesEmbeddedProfile();
    }

    @Test
    public void testDexoptUsesEmbeddedProfileDmNoConfig() throws Exception {
        String dmPath = TestingUtils.createTempZipWithEntry("primary.vdex", new byte[0] /* data */);
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);
        checkDexoptUsesEmbeddedProfile();
    }

    @Test
    public void testDexoptUsesEmbeddedProfileDmEmptyConfig() throws Exception {
        String dmPath = TestingUtils.createTempZipWithEntry("config.pb", new byte[0] /* data */);
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);
        checkDexoptUsesEmbeddedProfile();
    }

    @Test
    public void testDexoptUsesEmbeddedProfileDmBadConfig() throws Exception {
        String dmPath = TestingUtils.createTempZipWithEntry(
                "config.pb", new byte[] {0x42, 0x43, 0x44} /* data */);
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);
        checkDexoptUsesEmbeddedProfile();
    }

    @Test
    public void testDexoptUsesEmbeddedProfileDmDisableEmbeddedProfile() throws Exception {
        var config = DexMetadataConfig.newBuilder().setEnableEmbeddedProfile(false).build();
        String dmPath = TestingUtils.createTempZipWithEntry("config.pb", config.toByteArray());
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);

        makeEmbeddedProfileUsable(mDexPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        checkDexoptWithNoProfile(verify(mArtd), mDexPath, "arm64", "verify");
        checkDexoptWithNoProfile(verify(mArtd), mDexPath, "arm", "verify");

        verifyEmbeddedProfileNotUsed(mDexPath);
    }

    @Test
    public void testDexoptUsesEmbeddedProfileNoEmbeddedProfile() throws Exception {
        var config = DexMetadataConfig.newBuilder().setEnableEmbeddedProfile(true).build();
        String dmPath = TestingUtils.createTempZipWithEntry("config.pb", config.toByteArray());
        doReturn(new ZipFile(dmPath)).when(mDexMetadataHelperInjector).openZipFile(mDmPath);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        checkDexoptWithNoProfile(verify(mArtd), mDexPath, "arm64", "verify");
        checkDexoptWithNoProfile(verify(mArtd), mDexPath, "arm", "verify");

        verifyEmbeddedProfileNotUsed(mDexPath);
    }

    @Test
    public void testDexoptUsesEmbeddedProfilePreReboot() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);
        mPublicOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;
        mPrivateOutputProfile.profilePath.finalPath.getForPrimary().isPreReboot = true;

        checkDexoptUsesEmbeddedProfile();
    }

    @Test
    public void testDexoptExternalProfileErrors() throws Exception {
        // Having no profile should not be reported.
        // Having a bad profile should be reported.
        lenient()
                .when(mArtd.copyAndRewriteProfile(deepEq(mDmProfile), any(), any()))
                .thenReturn(TestingUtils.createCopyAndRewriteProfileBadProfile("error_msg"));

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();

        assertThat(results.get(0).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(results.get(0).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_BAD_EXTERNAL_PROFILE)
                .isNotEqualTo(0);
        assertThat(results.get(0).getExternalProfileErrors()).containsExactly("error_msg");
        assertThat(results.get(1).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(results.get(1).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_BAD_EXTERNAL_PROFILE)
                .isNotEqualTo(0);
        assertThat(results.get(1).getExternalProfileErrors()).containsExactly("error_msg");
    }

    @Test
    public void testDexoptDeletesProfileOnFailure() throws Exception {
        makeProfileUsable(mDmProfile);

        when(mArtd.dexopt(any(), eq(mDexPath), any(), any(), any(), any(), any(), any(), anyInt(),
                     any(), any()))
                .thenThrow(new ServiceSpecificException(42, "This is an error message"));

        mPrimaryDexopter.dexopt();

        verify(mArtd).deleteProfile(
                deepEq(ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath)));
        verify(mArtd, never()).commitTmpProfile(deepEq(mPublicOutputProfile.profilePath));
    }

    @Test
    public void testDexoptNeedsToBeShared() throws Exception {
        when(mDexUseManager.isPrimaryDexUsedByOtherApps(eq(PKG_NAME), eq(mDexPath)))
                .thenReturn(true);
        when(mDexUseManager.isPrimaryDexUsedByOtherApps(eq(PKG_NAME), eq(mSplit0DexPath)))
                .thenReturn(true);

        // The ref profile is usable but shouldn't be used.
        makeProfileUsable(mRefProfile);

        makeProfileUsable(mDmProfile);

        // The existing artifacts are private.
        when(mArtd.getArtifactsVisibility(
                     argThat(artifactsPath -> artifactsPath.dexPath == mDexPath)))
                .thenReturn(FileVisibility.NOT_OTHER_READABLE);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd).copyAndRewriteProfile(
                deepEq(mDmProfile), deepEq(mPublicOutputProfile), eq(mDexPath));

        // It should re-compile anyway.
        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm64"), any(), eq("speed-profile"), eq(mForceDexoptTrigger));
        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm64",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);

        verify(mArtd).getDexoptNeeded(
                eq(mDexPath), eq("arm"), any(), eq("speed-profile"), eq(mForceDexoptTrigger));
        checkDexoptWithProfile(verify(mArtd), mDexPath, "arm",
                ProfilePath.tmpProfilePath(mPublicOutputProfile.profilePath),
                true /* isOtherReadable */);

        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm64", "speed");
        checkDexoptWithNoProfile(verify(mArtd), mSplit0DexPath, "arm", "speed");

        verifyProfileNotUsed(mRefProfile);
        verifyProfileNotUsed(mPrebuiltProfile);
    }

    @Test
    public void testDexoptNeedsToBeSharedArtifactsArePublic() throws Exception {
        // Same setup as above, but the existing artifacts are public.
        when(mDexUseManager.isPrimaryDexUsedByOtherApps(eq(PKG_NAME), eq(mDexPath)))
                .thenReturn(true);
        when(mDexUseManager.isPrimaryDexUsedByOtherApps(eq(PKG_NAME), eq(mSplit0DexPath)))
                .thenReturn(true);

        makeProfileUsable(mRefProfile);
        makeProfileUsable(mDmProfile);
        when(mArtd.getArtifactsVisibility(
                     argThat(artifactsPath -> artifactsPath.dexPath == mDexPath)))
                .thenReturn(FileVisibility.OTHER_READABLE);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

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

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd).getDexoptNeeded(eq(mSplit0DexPath), eq("arm64"), any(), eq("speed-profile"),
                eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(verify(mArtd), mSplit0DexPath, "arm64", mSplit0RefProfile,
                false /* isOtherReadable */);

        verify(mArtd).getDexoptNeeded(eq(mSplit0DexPath), eq("arm"), any(), eq("speed-profile"),
                eq(mDefaultDexoptTrigger));
        checkDexoptWithProfile(verify(mArtd), mSplit0DexPath, "arm", mSplit0RefProfile,
                false /* isOtherReadable */);
    }

    @Test
    public void testDexoptCancelledBeforeDexopt() throws Exception {
        mCancellationSignal.cancel();

        var artdCancellationSignal = mock(IArtdCancellationSignal.class);
        when(mArtd.createCancellationSignal()).thenReturn(artdCancellationSignal);

        doAnswer(invocation -> {
            verify(artdCancellationSignal).cancel();
            return createArtdDexoptResult(true /* cancelled */);
        })
                .when(mArtd)
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        same(artdCancellationSignal));

        // The result should only contain one element: the result of the first file with
        // DEXOPT_CANCELLED.
        assertThat(mPrimaryDexopter.dexopt()
                           .stream()
                           .map(DexContainerFileDexoptResult::getStatus)
                           .toList())
                .containsExactly(DexoptResult.DEXOPT_CANCELLED);

        // It shouldn't continue after being cancelled on the first file.
        verify(mArtd, times(1)).createCancellationSignal();
        verify(mArtd, times(1))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        any());
    }

    @Test
    public void testDexoptCancelledDuringDexopt() throws Exception {
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        final long TIMEOUT_SEC = 10;

        var artdCancellationSignal = mock(IArtdCancellationSignal.class);
        when(mArtd.createCancellationSignal()).thenReturn(artdCancellationSignal);

        doAnswer(invocation -> {
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return createArtdDexoptResult(true /* cancelled */);
        })
                .when(mArtd)
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        same(artdCancellationSignal));
        doAnswer(invocation -> {
            dexoptCancelled.release();
            return null;
        })
                .when(artdCancellationSignal)
                .cancel();

        Future<List<DexContainerFileDexoptResult>> results =
                ForkJoinPool.commonPool().submit(() -> { return mPrimaryDexopter.dexopt(); });

        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        mCancellationSignal.cancel();

        assertThat(results.get().stream().map(DexContainerFileDexoptResult::getStatus).toList())
                .containsExactly(DexoptResult.DEXOPT_CANCELLED);

        // It shouldn't continue after being cancelled on the first file.
        verify(mArtd, times(1)).createCancellationSignal();
        verify(mArtd, times(1))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        any());
    }

    @Test
    public void testDexoptBaseApk() throws Exception {
        mDexoptParams =
                new DexoptParams.Builder("install")
                        .setCompilerFilter("speed-profile")
                        .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                        .setSplitName(null)
                        .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd, times(2))
                .dexopt(any(), eq(mDexPath), any(), any(), any(), any(), any(), any(), anyInt(),
                        any(), any());
        verify(mArtd, never())
                .dexopt(any(), eq(mSplit0DexPath), any(), any(), any(), any(), any(), any(),
                        anyInt(), any(), any());
    }

    @Test
    public void testDexoptSplitApk() throws Exception {
        mDexoptParams =
                new DexoptParams.Builder("install")
                        .setCompilerFilter("speed-profile")
                        .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_FOR_SINGLE_SPLIT)
                        .setSplitName("split_0")
                        .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        verifyStatusAllOk(results);

        verify(mArtd, never())
                .dexopt(any(), eq(mDexPath), any(), any(), any(), any(), any(), any(), anyInt(),
                        any(), any());
        verify(mArtd, times(2))
                .dexopt(any(), eq(mSplit0DexPath), any(), any(), any(), any(), any(), any(),
                        anyInt(), any(), any());
    }

    @Test
    public void testDexoptStorageLow() throws Exception {
        when(mStorageManager.getAllocatableBytes(any())).thenReturn(1l, 0l, 0l, 1l);

        mDexoptParams =
                new DexoptParams.Builder("install")
                        .setCompilerFilter("speed-profile")
                        .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX | ArtFlags.FLAG_SKIP_IF_STORAGE_LOW)
                        .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        assertThat(results.get(0).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(
                results.get(0).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW)
                .isEqualTo(0);
        assertThat(results.get(1).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(
                results.get(1).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW)
                .isNotEqualTo(0);
        assertThat(results.get(2).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(
                results.get(2).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW)
                .isNotEqualTo(0);
        assertThat(results.get(3).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(
                results.get(3).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_STORAGE_LOW)
                .isEqualTo(0);

        verify(mArtd, times(2))
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        any());
    }

    @Test
    public void testDexoptDexStatus() throws Exception {
        lenient()
                .when(mArtd.getDexoptNeeded(any(), any(), any(), any(), anyInt()))
                .thenReturn(dexoptIsNotNeeded(false /* hasDexCode */),
                        dexoptIsNotNeeded(false /* hasDexCode */),
                        dexoptIsNotNeeded(true /* hasDexCode */), dexoptIsNeeded());

        mDexoptParams = new DexoptParams.Builder("install")
                                .setCompilerFilter("speed-profile")
                                .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX)
                                .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        assertThat(results.get(0).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(
                results.get(0).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE)
                .isNotEqualTo(0);
        assertThat(results.get(1).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(
                results.get(1).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE)
                .isNotEqualTo(0);
        assertThat(results.get(2).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(
                results.get(2).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE)
                .isEqualTo(0);
        assertThat(results.get(3).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(
                results.get(3).getExtendedStatusFlags() & DexoptResult.EXTENDED_SKIPPED_NO_DEX_CODE)
                .isEqualTo(0);
    }

    @Test
    public void testDexoptPreRebootDexNotFound() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);
        doReturn(FileVisibility.NOT_FOUND).when(mArtd).getDexFileVisibility(mDexPath);
        doReturn(FileVisibility.NOT_FOUND).when(mArtd).getDexFileVisibility(mSplit0DexPath);

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        assertThat(results).hasSize(0);
    }

    @Test
    public void testDexoptPreRebootSomeDexNotFound() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);
        doReturn(FileVisibility.OTHER_READABLE).when(mArtd).getDexFileVisibility(mDexPath);
        doReturn(FileVisibility.NOT_FOUND).when(mArtd).getDexFileVisibility(mSplit0DexPath);

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        assertThat(results).hasSize(2);
        assertThat(results.get(0).getDexContainerFile()).isEqualTo(mDexPath);
        assertThat(results.get(0).getAbi()).isEqualTo("arm64-v8a");
        assertThat(results.get(1).getDexContainerFile()).isEqualTo(mDexPath);
        assertThat(results.get(1).getAbi()).isEqualTo("armeabi-v7a");
    }

    @Test
    public void testDexoptPreRebootArtifactsExist() throws Exception {
        when(mInjector.isPreReboot()).thenReturn(true);

        when(mArtd.getArtifactsVisibility(deepEq(AidlUtils.buildArtifactsPathAsInputPreReboot(
                     mDexPath, "arm", false /* isInDalvikCache */))))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        // Only the one at index 1 is skipped.
        assertThat(results).hasSize(4);
        assertThat(results.get(0).getDexContainerFile()).isEqualTo(mDexPath);
        assertThat(results.get(0).getAbi()).isEqualTo("arm64-v8a");
        assertThat(results.get(0).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(results.get(0).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST)
                .isEqualTo(0);
        assertThat(results.get(1).getDexContainerFile()).isEqualTo(mDexPath);
        assertThat(results.get(1).getAbi()).isEqualTo("armeabi-v7a");
        assertThat(results.get(1).getStatus()).isEqualTo(DexoptResult.DEXOPT_SKIPPED);
        assertThat(results.get(1).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST)
                .isNotEqualTo(0);
        assertThat(results.get(2).getDexContainerFile()).isEqualTo(mSplit0DexPath);
        assertThat(results.get(2).getAbi()).isEqualTo("arm64-v8a");
        assertThat(results.get(2).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(results.get(2).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST)
                .isEqualTo(0);
        assertThat(results.get(3).getDexContainerFile()).isEqualTo(mSplit0DexPath);
        assertThat(results.get(3).getAbi()).isEqualTo("armeabi-v7a");
        assertThat(results.get(3).getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        assertThat(results.get(3).getExtendedStatusFlags()
                & DexoptResult.EXTENDED_SKIPPED_PRE_REBOOT_ALREADY_EXIST)
                .isEqualTo(0);
    }

    @Test
    public void testDexoptNotAffectedByPreRebootArtifacts() throws Exception {
        // Same setup as above, but `isPreReboot` is false.
        lenient()
                .when(mArtd.getArtifactsVisibility(
                        deepEq(AidlUtils.buildArtifactsPathAsInputPreReboot(
                                mDexPath, "arm", false /* isInDalvikCache */))))
                .thenReturn(FileVisibility.OTHER_READABLE);

        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        List<DexContainerFileDexoptResult> results = mPrimaryDexopter.dexopt();
        assertThat(results).hasSize(4);
        for (DexContainerFileDexoptResult result : results) {
            assertThat(result.getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
        }
    }

    @Test
    public void testMaybeCreateSdc() throws Exception {
        mDexoptParams = new DexoptParams.Builder("install")
                                .setCompilerFilter("speed-profile")
                                .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX)
                                .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        mPrimaryDexopter.dexopt();

        FsPermission dirFsPermission = AidlUtils.buildFsPermission(Process.SYSTEM_UID /* uid */,
                Process.SYSTEM_UID /* gid */, false /* isOtherReadable */,
                true /* isOtherExecutable */);
        FsPermission fileFsPermission = AidlUtils.buildFsPermission(
                Process.SYSTEM_UID /* uid */, SHARED_GID /* gid */, true /* isOtherReadable */);
        PermissionSettings permissionSettings = AidlUtils.buildPermissionSettings(
                dirFsPermission, fileFsPermission, null /* seContext */);

        verify(mArtd).maybeCreateSdc(deepEq(AidlUtils.buildOutputSecureDexMetadataCompanion(
                mDexPath, "arm64", false /* isInDalvikCache */, permissionSettings)));
        verify(mArtd).maybeCreateSdc(deepEq(AidlUtils.buildOutputSecureDexMetadataCompanion(
                mDexPath, "arm", false /* isInDalvikCache */, permissionSettings)));
        verify(mArtd).maybeCreateSdc(deepEq(AidlUtils.buildOutputSecureDexMetadataCompanion(
                mSplit0DexPath, "arm64", false /* isInDalvikCache */, permissionSettings)));
        verify(mArtd).maybeCreateSdc(deepEq(AidlUtils.buildOutputSecureDexMetadataCompanion(
                mSplit0DexPath, "arm", false /* isInDalvikCache */, permissionSettings)));
    }

    @Test
    public void testMaybeCreateSdcCompilerFilterSkip() throws Exception {
        mDexoptParams = new DexoptParams.Builder("install")
                                .setCompilerFilter(DexoptParams.COMPILER_FILTER_NOOP)
                                .setFlags(ArtFlags.FLAG_FOR_PRIMARY_DEX)
                                .build();
        mPrimaryDexopter =
                new PrimaryDexopter(mInjector, mPkgState, mPkg, mDexoptParams, mCancellationSignal);

        mPrimaryDexopter.dexopt();

        verify(mArtd, times(4)).maybeCreateSdc(any());
        verify(mArtd, never())
                .dexopt(any(), any(), any(), any(), any(), any(), any(), any(), anyInt(), any(),
                        any());
    }

    private void checkDexoptWithProfile(IArtd artd, String dexPath, String isa, ProfilePath profile,
            boolean isOtherReadable) throws Exception {
        artd.dexopt(argThat(artifacts
                            -> artifacts.permissionSettings.fileFsPermission.isOtherReadable
                                    == isOtherReadable),
                eq(dexPath), eq(isa), any(), eq("speed-profile"), deepEq(profile), any(), any(),
                anyInt(), argThat(dexoptOptions -> dexoptOptions.generateAppImage == true), any());
    }

    private void checkDexoptWithNoProfile(
            IArtd artd, String dexPath, String isa, String compilerFilter) throws Exception {
        artd.dexopt(
                argThat(artifacts
                        -> artifacts.permissionSettings.fileFsPermission.isOtherReadable == true),
                eq(dexPath), eq(isa), any(), eq(compilerFilter), isNull(), any(), any(), anyInt(),
                argThat(dexoptOptions -> dexoptOptions.generateAppImage == false), any());
    }

    private void verifyProfileNotUsed(ProfilePath profile) throws Exception {
        assertThat(mUsedProfiles)
                .comparingElementsUsing(TestingUtils.<ProfilePath>deepEquality())
                .doesNotContain(profile);
    }

    private void verifyEmbeddedProfileNotUsed(String dexPath) throws Exception {
        assertThat(mUsedEmbeddedProfiles).doesNotContain(dexPath);
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
                    return TestingUtils.createCopyAndRewriteProfileSuccess();
                });
    }

    private void makeEmbeddedProfileUsable(String dexPath) throws Exception {
        lenient()
                .when(mArtd.copyAndRewriteEmbeddedProfile(any(), eq(dexPath)))
                .thenAnswer(invocation -> {
                    mUsedEmbeddedProfiles.add(invocation.<String>getArgument(1));
                    return TestingUtils.createCopyAndRewriteProfileSuccess();
                });
    }

    private void verifyStatusAllOk(List<DexContainerFileDexoptResult> results) {
        for (DexContainerFileDexoptResult result : results) {
            assertThat(result.getStatus()).isEqualTo(DexoptResult.DEXOPT_PERFORMED);
            assertThat(result.getExtendedStatusFlags()).isEqualTo(0);
            assertThat(result.getExternalProfileErrors()).isEmpty();
        }
    }

    /** Dexopter relies on this information to determine which current profiles to check. */
    private void setPackageInstalledForUserIds(int... userIds) {
        for (int userId : userIds) {
            when(mPkgState.getStateForUser(eq(UserHandle.of(userId))))
                    .thenReturn(mPkgUserStateInstalled);
        }
    }
}
