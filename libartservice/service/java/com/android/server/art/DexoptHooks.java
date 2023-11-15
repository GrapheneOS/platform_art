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

        switch (params.getReason()) {
            case ReasonMapping.REASON_BOOT_AFTER_OTA:
            case ReasonMapping.REASON_BG_DEXOPT:
                break;
            default:
                return orig;
        }

        var pm = Objects.requireNonNull(LocalManagerRegistry.getManager(PackageManagerLocal.class));

        Consumer<OperationProgress> res = progress -> {
            if (orig != null) {
                orig.accept(progress);
            }

            String reason = params.getReason();

            Slog.d(TAG, "onDexoptProgress: reason " + reason + ", " + progress);

            switch (reason) {
                case ReasonMapping.REASON_BOOT_AFTER_OTA ->
                    pm.showDexoptProgressBootMessage(progress.getPercentage(), progress.getCurrent(), progress.getTotal());
                case ReasonMapping.REASON_BG_DEXOPT ->
                    pm.onBgDexoptProgressUpdate(progress.getPercentage(), progress.getCurrent(), progress.getTotal());
            }
        };

        return res;
    }
}
