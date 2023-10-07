package com.android.server.art;

import android.annotation.Nullable;
import android.util.Slog;

import com.android.server.LocalManagerRegistry;
import com.android.server.art.model.DexoptParams;
import com.android.server.art.model.OperationProgress;
import com.android.server.pm.PackageManagerLocal;

import java.util.Objects;
import java.util.function.Consumer;

class DexoptHooks {
    static final String TAG = DexoptHooks.class.getSimpleName();

    @Nullable
    static Consumer<OperationProgress> maybeWrapDexoptProgressCallback(
            DexoptParams params, @Nullable Consumer<OperationProgress> orig) {

        if (!ReasonMapping.REASON_BOOT_AFTER_OTA.equals(params.getReason())) {
            return orig;
        }

        var pm = Objects.requireNonNull(LocalManagerRegistry.getManager(PackageManagerLocal.class));

        Consumer<OperationProgress> res = progress -> {
            if (orig != null) {
                orig.accept(progress);
            }

            Slog.d(TAG, progress.toString());

            pm.showDexoptProgressBootMessage(progress.getPercentage(), progress.getCurrent(), progress.getTotal());
        };

        return res;
    }
}
