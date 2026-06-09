#include "mission_complete.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include "shell_command.h"
#include <cstdio>
#include <cstring>

namespace haloap {

    namespace {
        // Pattern for the start of FUN_180ac3854 (the game_won victory handler).
        const char* kGameWonPattern =
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 81 EC 40 01 00 00";

        // Offset of the current mission name string inside halo1.dll.
        // Verified via Cheat Engine.
        constexpr size_t kCurrentMissionNameOffset = 0x1BE9C80;

        // Type of the original function: takes no arguments, returns nothing.
        typedef void (*GameWonFn)(void);

        // Pointer populated by MinHook so we can call the original.
        GameWonFn g_originalGameWon = nullptr;

        // Cached hook target address.
        void* g_hookTarget = nullptr;

        // Weak reference to the pipe for sending events. Set at install time.
        PipeClient* g_pipe = nullptr;

        // Reads the current mission name from halo1.dll's memory.
        // Returns a pointer to a static buffer (valid until next call).
        // Returns nullptr if halo1.dll isn't loaded or the name looks invalid.
        const char* ReadCurrentMissionName() {
            HMODULE halo1 = GetModuleHandleA("halo1.dll");
            if (!halo1) return nullptr;

            const char* namePtr = (const char*)((uint8_t*)halo1 + kCurrentMissionNameOffset);

            // Sanity check: mission names are short ASCII strings like "a10".
            // If the first byte isn't printable, assume the field is empty/invalid.
            if (namePtr[0] < 0x20 || namePtr[0] > 0x7E) return nullptr;

            // Copy into a static buffer to avoid lifetime issues.
            static char buffer[64];
            strncpy_s(buffer, namePtr, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = 0;
            return buffer;
        }

        // The detour function. Runs in place of the original game_won handler.
        void DetourGameWon(void) {
            // Read mission context.
            const char* missionName = ReadCurrentMissionName();

            // Report to the injector.
            if (g_pipe && g_pipe->IsConnected()) {
                std::string msg = "MISSION_COMPLETE: ";
                msg += missionName ? missionName : "unknown";
                g_pipe->SendAsync(msg);
            }

            printf("[hook] MISSION_COMPLETE: %s\n", missionName ? missionName : "unknown");
            haloap::g_quitAfterComplete.store(true);
            // Call the original so the game proceeds normally.
            if (g_originalGameWon) {
                g_originalGameWon();
            }
        }
    }

    bool InstallMissionCompleteHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] halo1.dll not loaded yet\n");
            return false;
        }

        // Find the target via pattern scan.
        void* target = FindPatternInModule(halo1, kGameWonPattern);
        if (!target) {
            printf("[hook] pattern not found in halo1.dll\n");
            return false;
        }

        printf("[hook] game_won handler found at %p\n", target);
        g_hookTarget = target;

        // Install the hook.
        MH_STATUS status = MH_CreateHook(
            target,
            (void*)DetourGameWon,
            (void**)&g_originalGameWon
        );
        if (status != MH_OK) {
            printf("[hook] MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(target);
        if (status != MH_OK) {
            printf("[hook] MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] mission-complete hook installed.\n");
        return true;
    }

    void UninstallMissionCompleteHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_originalGameWon = nullptr;
        g_pipe = nullptr;
    }

}  // namespace haloap