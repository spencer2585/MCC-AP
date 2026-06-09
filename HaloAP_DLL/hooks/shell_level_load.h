#pragma once

#include "../pipe_client.h"
#include <atomic>

namespace haloap {
    // Diagnostic hook on the shell's level-load producer (vtable slot 9).
    // This is the function MCC's Unreal shell calls to tell halo1.dll
    // to load a mission. It posts a message with type 0xFFFFFFFF and a
    // level path string onto the engine's SList work queue.
    //
    // Logs every level load command the shell issues.
    // ALWAYS calls the original - observation only.
    bool InstallShellLevelLoadHook(PipeClient* pipe);
    void UninstallShellLevelLoadHook();

    // Returns the current engine object pointer by reading the cached global.
    // Returns nullptr if the global hasn't been resolved yet or the engine
    // object hasn't been created.
    void* GetEngineObject();
}