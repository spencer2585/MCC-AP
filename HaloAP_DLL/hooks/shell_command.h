#pragma once

#include "../pipe_client.h"

namespace haloap {
    // Diagnostic hook on the shell's general command producer (vtable slot 3).
    // This is the function MCC's Unreal shell calls to post typed messages
    // to halo1.dll's SList work queue. Message types include game state
    // transitions, pause/unpause, and other shell commands.
    //
    // Logs every command type and context data the shell sends.
    // ALWAYS calls the original - observation only.
    bool InstallShellCommandHook(PipeClient* pipe);
    void UninstallShellCommandHook();
    extern std::atomic<bool> g_quitAfterComplete;
}
