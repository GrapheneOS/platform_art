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

import android.os.Binder;
import android.os.Process;

import com.android.modules.utils.BasicShellCommandHandler;

import java.io.PrintWriter;

/**
 * This class handles ART shell commands.
 *
 * @hide
 */
public final class ArtShellCommand extends BasicShellCommandHandler {
    private static final String TAG = "ArtShellCommand";

    private final ArtManagerLocal mArtManagerLocal;

    public ArtShellCommand(ArtManagerLocal artManagerLocal) {
        mArtManagerLocal = artManagerLocal;
    }

    @Override
    public int onCommand(String cmd) {
        enforceRoot();
        // Handles empty, help, and invalid commands.
        return handleDefaultCommands(cmd);
    }

    @Override
    public void onHelp() {
        final PrintWriter pw = getOutPrintWriter();
        pw.println("ART service commands.");
        pw.println("Note: The commands are used for internal debugging purposes only. There are no "
                + "stability guarantees for them.");
        pw.println("");
        pw.println("Usage: cmd package art [ARGS]...");
        pw.println("");
        pw.println("Supported commands:");
        pw.println("  help or -h");
        pw.println("    Print this help text.");
    }

    private void enforceRoot() {
        final int uid = Binder.getCallingUid();
        if (uid != Process.ROOT_UID) {
            throw new SecurityException("ART service shell commands need root access");
        }
    }
}
