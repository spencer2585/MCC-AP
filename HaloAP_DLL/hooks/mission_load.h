#pragma once

#include "../pipe_client.h"

namespace haloap {
    // Diagnostic hook on BeginMissionLoad. Fires whenever the engine begins
    // setting up a mission load. Logs the path argument and the caller's address.
    // ALWAYS calls the original — this is observation only, not enforcement.
    bool InstallMissionLoadHook(PipeClient* pipe);
    void UninstallMissionLoadHook();
}