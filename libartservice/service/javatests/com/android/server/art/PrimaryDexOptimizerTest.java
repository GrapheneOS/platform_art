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
import static com.android.server.art.testing.TestingUtils.deepEq;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.isNull;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.model.OptimizeParams;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class PrimaryDexOptimizerTest extends PrimaryDexOptimizerTestBase {
    private OptimizeParams mOptimizeParams;

    @Before
    public void setUp() throws Exception {
        super.setUp();

        mOptimizeParams = new OptimizeParams.Builder("install").setCompilerFilter("verify").build();
    }

    @Test
    public void testDexoptInputVdex() throws Exception {
        // null.
        doReturn(dexoptIsNeeded(ArtifactsLocation.NONE_OR_ERROR))
                .when(mArtd)
                .getDexoptNeeded(eq("/data/app/foo/base.apk"), eq("arm64"), any(), any(), anyInt());
        doReturn(true).when(mArtd).dexopt(any(), eq("/data/app/foo/base.apk"), eq("arm64"), any(),
                any(), any(), isNull(), anyInt(), any());

        // ArtifactsPath, isInDalvikCache=true.
        doReturn(dexoptIsNeeded(ArtifactsLocation.DALVIK_CACHE))
                .when(mArtd)
                .getDexoptNeeded(eq("/data/app/foo/base.apk"), eq("arm"), any(), any(), anyInt());
        doReturn(true).when(mArtd).dexopt(any(), eq("/data/app/foo/base.apk"), eq("arm"), any(),
                any(), any(),
                deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPath(
                        "/data/app/foo/base.apk", "arm", true /* isInDalvikCache */))),
                anyInt(), any());

        // ArtifactsPath, isInDalvikCache=false.
        doReturn(dexoptIsNeeded(ArtifactsLocation.NEXT_TO_DEX))
                .when(mArtd)
                .getDexoptNeeded(
                        eq("/data/app/foo/split_0.apk"), eq("arm64"), any(), any(), anyInt());
        doReturn(true).when(mArtd).dexopt(any(), eq("/data/app/foo/split_0.apk"), eq("arm64"),
                any(), any(), any(),
                deepEq(VdexPath.artifactsPath(AidlUtils.buildArtifactsPath(
                        "/data/app/foo/split_0.apk", "arm64", false /* isInDalvikCache */))),
                anyInt(), any());

        // DexMetadataPath.
        doReturn(dexoptIsNeeded(ArtifactsLocation.DM))
                .when(mArtd)
                .getDexoptNeeded(
                        eq("/data/app/foo/split_0.apk"), eq("arm"), any(), any(), anyInt());
        doReturn(true).when(mArtd).dexopt(any(), eq("/data/app/foo/split_0.apk"), eq("arm"), any(),
                any(), any(),
                deepEq(VdexPath.dexMetadataPath(
                        AidlUtils.buildDexMetadataPath("/data/app/foo/split_0.apk"))),
                anyInt(), any());

        mPrimaryDexOptimizer.dexopt(mPkgState, mPkg, mOptimizeParams);
    }
}
