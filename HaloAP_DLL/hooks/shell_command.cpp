#include "shell_command.h"
#include "../minhook/MinHook.h"
#include "shared/common.h"
#include <cstdio>
#include <intrin.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace haloap {

    std::atomic<bool> g_quitAfterComplete{false};
    std::atomic<bool> g_quitLockedMission{false};
    
    namespace {
        // Vtable slot 3 signature: FUN_180093ce0
        // void FUN_180093ce0(longlong param_1, int param_2, undefined8* param_3)
        //
        // param_1 = engine object ("this" pointer)
        // param_2 = message type (integer enum)
        // param_3 = context/argument data (pointer to payload, may be null)
        //
        // Known message types from static analysis:
        //   6    — initialization/reset (special-cased, calls FUN_1800935a0)
        //   7    — state transition (special-cased, sets flags)
        //   0x10 — copies single pointer from param_3
        //   0x11 — copies single pointer from param_3
        //   0x12 — copies 16 bytes of context from param_3
        //   0x13 — copies single pointer from param_3
        //   others — posted with type only, no extra data

        typedef void (*ShellCommandFn)(void* engineObj, int msgType, void* context);
        ShellCommandFn g_original = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        void DetourShellCommand(void* engineObj, int msgType, void* context) {
            printf("[hook] SHELL_CMD: type=0x%x\n", msgType);

            if (g_quitAfterComplete.load()) {
                g_quitAfterComplete.store(false);
                printf("[hook] SHELL_CMD: intercepting — sending quit sequence\n");

                // Send quit sequence: pause → teardown → resume
                if (g_original) {
                    g_original(engineObj, 0x0, nullptr);  // pause
                    g_original(engineObj, 0xD, nullptr);  // teardown
                    g_original(engineObj, 0x1, nullptr);  // resume
                }
                return;  // Don't process the original command (which was "load next mission")
            }
            
            if (g_quitLockedMission.load())
            {
                g_quitLockedMission.store(false);
                printf("[hook] SHELL_CMD: intercepting — sending quit sequence (locked mission)\n");
                if (g_original) {
                    g_original(engineObj, 0x0, nullptr);
                    g_original(engineObj, 0xD, nullptr);
                    g_original(engineObj, 0x1, nullptr);
                }
                return;
            }

            // Normal processing
            if (g_original) {
                g_original(engineObj, msgType, context);
            }
        }
    }

    bool InstallShellCommandHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] (shell_cmd) halo1.dll not loaded yet\n");
            return false;
        }

        // Same strategy as the level load hook: find engine object via
        // CreateGameEngine export, read vtable, hook slot 3.

        auto createEngine = (uint8_t*)GetProcAddress(halo1, "CreateGameEngine");
        if (!createEngine) {
            printf("[hook] (shell_cmd) CreateGameEngine export not found\n");
            return false;
        }

        // Find the global engine object pointer — scan for the last
        // MOV [rip+disp32], reg64 in CreateGameEngine (up to 1024 bytes).
        // Cannot use 0xC3 as RET marker since it appears inside other
        // instructions (e.g. FF C3 = INC EBX).
        void** engineGlobal = nullptr;
        for (int i = 0; i < 1024; i++) {
            if (createEngine[i] == 0x48 &&
                createEngine[i + 1] == 0x89 &&
                (createEngine[i + 2] & 0x07) == 0x05 &&
                (createEngine[i + 2] & 0xC0) == 0x00) {
                int32_t disp = *(int32_t*)(&createEngine[i + 3]);
                uint8_t* nextInstr = &createEngine[i + 7];
                engineGlobal = (void**)(nextInstr + disp);
            }
        }

        if (!engineGlobal) {
            printf("[hook] (shell_cmd) couldn't find engine global pointer\n");
            return false;
        }

        void* engineObj = *engineGlobal;
        if (!engineObj) {
            printf("[hook] (shell_cmd) engine object not created yet\n");
            return false;
        }

        void** vtable = *(void***)engineObj;

        // Vtable slot 3 = offset 3.
        void* slot3 = vtable[3];
        printf("[hook] (shell_cmd) vtable[3] (shell command) at %p\n", slot3);

        // Sanity check.
        MODULEINFO modInfo;
        if (GetModuleInformation(GetCurrentProcess(), halo1, &modInfo, sizeof(modInfo))) {
            size_t funcAddr = (size_t)slot3;
            size_t modBase = (size_t)halo1;
            if (funcAddr < modBase || funcAddr >= modBase + modInfo.SizeOfImage) {
                printf("[hook] (shell_cmd) vtable[3] is outside halo1.dll! Aborting.\n");
                return false;
            }
        }

        g_hookTarget = slot3;

        MH_STATUS status = MH_CreateHook(slot3, (void*)DetourShellCommand,
            (void**)&g_original);
        if (status != MH_OK) {
            printf("[hook] (shell_cmd) MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(slot3);
        if (status != MH_OK) {
            printf("[hook] (shell_cmd) MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] (shell_cmd) installed.\n");
        return true;
    }

    void UninstallShellCommandHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_original = nullptr;
        g_pipe = nullptr;
    }

}  // namespace haloap