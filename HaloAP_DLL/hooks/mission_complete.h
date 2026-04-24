#pragma once

#include "../pipe_client.h"

namespace haloap {

    // Installs the mission-complete hook. Call after MinHook is initialized
    // and the pipe client is connected (or at least constructed).
    // Returns true on success.
    bool InstallMissionCompleteHook(PipeClient* pipe);

    // Uninstalls the hook. Safe to call during shutdown.
    void UninstallMissionCompleteHook();

}