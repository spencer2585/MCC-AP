#include "mission_id_lookup.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include <cstdio>
#include <intrin.h>

namespace haloap {

    namespace {
        const char* kPattern =
            "40 53 48 83 EC 20 4C 8B 05 ? ? ? ? 48 8B D9 4C 2B C1 48 8B C1 41 BA 01 00 00 00";

        typedef int (*GetMissionIdFn)(const char* path);
        GetMissionIdFn g_originalGetMissionId = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        int DetourGetMissionId(const char* path) {
            // Capture the return address (the caller's address inside its function).
            // _ReturnAddress() is an MSVC intrinsic that returns the RA from this frame.
            void* returnAddress = _ReturnAddress();

            int result = -1;
            if (g_originalGetMissionId) {
                result = g_originalGetMissionId(path);
            }

            // Compute caller offset relative to halo1.dll for stable logging
            // across runs (since halo1.dll is ASLR'd).
            //HMODULE halo1 = GetModuleHandleA("halo1.dll");
            //size_t callerOffset = 0;
            //if (halo1) {
            //    callerOffset = (size_t)returnAddress - (size_t)halo1;
            //}

            // Build log message.
            //char buf[512];
            //snprintf(buf, sizeof(buf),
            //    "MISSION_ID_LOOKUP: path='%s' result=%d caller=halo1.dll+0x%zx",
            //    path ? path : "(null)", result, callerOffset);
            //printf("[hook] %s\n", buf);
            //if (g_pipe && g_pipe->IsConnected()) {
            //    g_pipe->SendAsync(buf);
            //}

            return result;
        }
    }

    bool InstallMissionIdLookupHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] (mission_id) halo1.dll not loaded yet\n");
            return false;
        }

        void* target = FindPatternInModule(halo1, kPattern);
        if (!target) {
            printf("[hook] (mission_id) pattern not found\n");
            return false;
        }

        g_hookTarget = target;
        printf("[hook] (mission_id) GetMissionIdFromPath found at %p\n", target);

        MH_STATUS status = MH_CreateHook(target, (void*)DetourGetMissionId,
            (void**)&g_originalGetMissionId);
        if (status != MH_OK) {
            printf("[hook] (mission_id) MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(target);
        if (status != MH_OK) {
            printf("[hook] (mission_id) MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] (mission_id) installed.\n");
        return true;
    }

    void UninstallMissionIdLookupHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_originalGetMissionId = nullptr;
        g_pipe = nullptr;
    }

}  // namespace haloap