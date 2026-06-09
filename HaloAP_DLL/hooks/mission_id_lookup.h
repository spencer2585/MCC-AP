#pragma once

#include "../pipe_client.h"

namespace haloap {
    // Diagnostic hook on GetMissionIdFromPath.
    // Logs every mission ID lookup with caller info, to help us identify
    // the auto-progression code path.
    bool InstallMissionIdLookupHook(PipeClient* pipe);
    void UninstallMissionIdLookupHook();
}