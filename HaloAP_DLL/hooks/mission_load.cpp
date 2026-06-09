#include "mission_load.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include <cstdio>
#include <intrin.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace haloap {

    namespace {
        const char* kPattern =
            "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 B0 EE FF FF B8 50 12 00 00 E8 ? ? ? ?";

        typedef void (*BeginMissionLoadFn)(void* p1, void* p2, const char* path,
            unsigned int p4, char p5);

        BeginMissionLoadFn g_originalBeginMissionLoad = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        // Identify the module containing an address. Returns module name and offset.
        // If not found, returns ("?", 0).
        struct ModuleInfo {
            char name[64];
            size_t offset;
        };

        ModuleInfo IdentifyAddress(void* address) {
            ModuleInfo info{};
            info.name[0] = '?';
            info.name[1] = '\0';
            info.offset = (size_t)address;

            HMODULE hMod = nullptr;
            if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)address, &hMod)) {
                char fullPath[MAX_PATH];
                if (GetModuleFileNameA(hMod, fullPath, sizeof(fullPath))) {
                    // Take just the filename, not the full path.
                    const char* base = strrchr(fullPath, '\\');
                    base = base ? base + 1 : fullPath;
                    strncpy_s(info.name, sizeof(info.name), base, _TRUNCATE);
                    info.offset = (size_t)address - (size_t)hMod;
                }
            }
            return info;
        }

        void DetourBeginMissionLoad(void* p1, void* p2, const char* path,
            unsigned int p4, char p5) {
            // Walk up the stack. Skip 1 frame (this detour function itself).
            // Capture up to 10 frames after that.
            constexpr ULONG kFramesToSkip = 1;
            constexpr ULONG kFramesToCapture = 32;
            void* stackFrames[kFramesToCapture] = {};
            USHORT framesCaptured = RtlCaptureStackBackTrace(
                kFramesToSkip, kFramesToCapture, stackFrames, nullptr);

            // Log the load itself.
            char buf[1024];
            snprintf(buf, sizeof(buf),
                "MISSION_LOAD: path='%s'", path ? path : "(null)");
            printf("[hook] %s\n", buf);
            if (g_pipe && g_pipe->IsConnected()) {
                g_pipe->SendAsync(buf);
            }
            
            // Log each captured stack frame.
            for (USHORT i = 0; i < framesCaptured; i++) {
                ModuleInfo info = IdentifyAddress(stackFrames[i]);
                char frameBuf[256];
                snprintf(frameBuf, sizeof(frameBuf),
                    "  STACK[%u] %s+0x%zx",
                    i, info.name, info.offset);
                printf("[hook] %s\n", frameBuf);
                if (g_pipe && g_pipe->IsConnected()) {
                    char pipeMsg[300];
                    snprintf(pipeMsg, sizeof(pipeMsg),
                        "MISSION_LOAD_FRAME: %u %s+0x%zx",
                        i, info.name, info.offset);
                    g_pipe->SendAsync(pipeMsg);
                }
            }

            // ALWAYS call original.
            if (g_originalBeginMissionLoad) {
                g_originalBeginMissionLoad(p1, p2, path, p4, p5);
            }
        }
    }

    bool InstallMissionLoadHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] (mission_load) halo1.dll not loaded yet\n");
            return false;
        }

        void* target = FindPatternInModule(halo1, kPattern);
        if (!target) {
            printf("[hook] (mission_load) pattern not found\n");
            return false;
        }

        g_hookTarget = target;
        printf("[hook] (mission_load) BeginMissionLoad found at %p\n", target);

        MH_STATUS status = MH_CreateHook(target, (void*)DetourBeginMissionLoad,
            (void**)&g_originalBeginMissionLoad);
        if (status != MH_OK) {
            printf("[hook] (mission_load) MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(target);
        if (status != MH_OK) {
            printf("[hook] (mission_load) MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] (mission_load) installed.\n");
        return true;
    }

    void UninstallMissionLoadHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_originalBeginMissionLoad = nullptr;
        g_pipe = nullptr;
    }

}  // namespace haloap