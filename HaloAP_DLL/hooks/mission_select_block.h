#pragma once

#include "../pipe_client.h"
#include <functional>

namespace haloap {
    using LockCheckFn = std::function<bool(const char* missionId)>;
    
    bool InstallMissionSelectBlockHook(PipeClient* pipe, LockCheckFn lockCheck = nullptr);
    void UninstallMissionSelectBlockHook();
    void SetMissionLockCheck(LockCheckFn lockCheck);
}