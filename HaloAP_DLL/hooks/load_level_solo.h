#pragma once

#include "../pipe_client.h"

namespace haloap {
    // Diagnostic hook on the LoadLevelSolo Scaleform handler (FUN_18005fc20).
    // This is the function called when the shell's UI triggers a solo mission
    // load via the Scaleform scripting bridge. The map name (e.g. "a30") and
    // difficulty are passed through the Scaleform argument structure.
    //
    // Logs every solo mission load request from the shell UI.
    // ALWAYS calls the original - observation only.
    bool InstallLoadLevelSoloHook(PipeClient* pipe);
    void UninstallLoadLevelSoloHook();
}