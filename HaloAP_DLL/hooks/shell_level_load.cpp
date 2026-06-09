#include "shell_level_load.h"
#include "../minhook/MinHook.h"
#include "shared/common.h"
#include <cstdio>
#include <intrin.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace haloap {

    namespace {
        // Vtable slot 9 signature: FUN_1800955c0
        // void FUN_1800955c0(longlong param_1, char* param_2)
        //
        // param_1 = engine object ("this" pointer)
        // param_2 = level path string (e.g. "levels\\a10\\a10")
        //
        // The function pops a message from the free pool (SList at +0x440),
        // fills it with type 0xFFFFFFFF and a _strdup'd copy of the path,
        // then pushes it onto the work queue (SList at +0x430).

        typedef void (*ShellLevelLoadFn)(void* engineObj, const char* levelPath);
        ShellLevelLoadFn g_original = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        // Cached pointer to the global that holds the engine object pointer.
        // Set during install, used by GetEngineObject().
        void** g_engineGlobal = nullptr;

        // Identify module containing an address.
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
                    const char* base = strrchr(fullPath, '\\');
                    base = base ? base + 1 : fullPath;
                    strncpy_s(info.name, sizeof(info.name), base, _TRUNCATE);
                    info.offset = (size_t)address - (size_t)hMod;
                }
            }
            return info;
        }

        void DetourShellLevelLoad(void* engineObj, const char* levelPath) {
            // Log the level load command.
            printf("[hook] SHELL_LEVEL_LOAD: path='%s'\n",
                levelPath ? levelPath : "(null)");

            if (g_pipe && g_pipe->IsConnected()) {
                char buf[512];
                snprintf(buf, sizeof(buf), "SHELL_LEVEL_LOAD: path='%s'",
                    levelPath ? levelPath : "(null)");
                g_pipe->SendAsync(buf);
            }

            // Log the caller — this tells us WHERE in the shell the load
            // request originates. Should be in MCC-Win64-Shipping.exe.
            void* returnAddress = _ReturnAddress();
            ModuleInfo caller = IdentifyAddress(returnAddress);
            printf("[hook]   caller: %s+0x%zx\n", caller.name, caller.offset);

            if (g_pipe && g_pipe->IsConnected()) {
                char buf[256];
                snprintf(buf, sizeof(buf), "SHELL_LEVEL_LOAD_CALLER: %s+0x%zx",
                    caller.name, caller.offset);
                g_pipe->SendAsync(buf);
            }

            // ALWAYS call original.
            if (g_original) {
                g_original(engineObj, levelPath);
            }
        }
    }

    void* GetEngineObject() {
        if (!g_engineGlobal) return nullptr;
        return *g_engineGlobal;
    }

    bool InstallShellLevelLoadHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] (shell_level_load) halo1.dll not loaded yet\n");
            return false;
        }

        // Step 1: Resolve the engine global pointer (only once per halo1.dll load).
        if (!g_engineGlobal) {
            auto createEngine = (uint8_t*)GetProcAddress(halo1, "CreateGameEngine");
            if (!createEngine) {
                printf("[hook] (shell_level_load) CreateGameEngine export not found\n");
                return false;
            }
            printf("[hook] (shell_level_load) CreateGameEngine at %p\n", createEngine);

            // Scan for the last MOV [rip+disp32], reg64 in CreateGameEngine.
            void** found = nullptr;
            for (int i = 0; i < 1024; i++) {
                if (createEngine[i] == 0x48 &&
                    createEngine[i + 1] == 0x89 &&
                    (createEngine[i + 2] & 0x07) == 0x05 &&
                    (createEngine[i + 2] & 0xC0) == 0x00) {
                    int32_t disp = *(int32_t*)(&createEngine[i + 3]);
                    uint8_t* nextInstr = &createEngine[i + 7];
                    found = (void**)(nextInstr + disp);
                }
            }

            if (!found) {
                printf("[hook] (shell_level_load) couldn't find engine global pointer\n");
                return false;
            }
            g_engineGlobal = found;
            printf("[hook] (shell_level_load) engine global at %p\n", g_engineGlobal);
        }

        // Step 2: Read the engine object pointer.
        void* engineObj = *g_engineGlobal;
        if (!engineObj) {
            // Silently return false — the monitor loop will retry.
            return false;
        }
        printf("[hook] (shell_level_load) engine object at %p\n", engineObj);

        // Step 4: Read the vtable pointer (first 8 bytes of the object).
        void** vtable = *(void***)engineObj;
        printf("[hook] (shell_level_load) vtable at %p\n", vtable);

        // Step 5: Vtable slot 9 = offset 9 pointers from base.
        void* slot9 = vtable[9];
        printf("[hook] (shell_level_load) vtable[9] (level load) at %p\n", slot9);

        // Sanity check: make sure it's inside halo1.dll.
        MODULEINFO modInfo;
        if (GetModuleInformation(GetCurrentProcess(), halo1, &modInfo, sizeof(modInfo))) {
            size_t funcAddr = (size_t)slot9;
            size_t modBase = (size_t)halo1;
            if (funcAddr < modBase || funcAddr >= modBase + modInfo.SizeOfImage) {
                printf("[hook] (shell_level_load) vtable[9] is outside halo1.dll! Aborting.\n");
                return false;
            }
        }

        g_hookTarget = slot9;

        // Step 6: Hook it.
        MH_STATUS status = MH_CreateHook(slot9, (void*)DetourShellLevelLoad,
            (void**)&g_original);
        if (status != MH_OK) {
            printf("[hook] (shell_level_load) MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(slot9);
        if (status != MH_OK) {
            printf("[hook] (shell_level_load) MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] (shell_level_load) installed.\n");
        return true;
    }

    void UninstallShellLevelLoadHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_original = nullptr;
        g_pipe = nullptr;
        g_engineGlobal = nullptr;
    }

}  // namespace haloap