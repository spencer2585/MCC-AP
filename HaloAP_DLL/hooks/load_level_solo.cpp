#include "load_level_solo.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include <cstdio>
#include <intrin.h>

namespace haloap {

    namespace {
        // Pattern for the start of FUN_18005fc20 (LoadLevelSolo handler).
        // 48 89 4C 24 08    MOV [RSP+8], RCX
        // 57                PUSH RDI
        // 48 83 EC 40       SUB RSP, 0x40
        // 48 C7 44 24 30 FE FF FF FF  MOV [RSP+0x30], -2
        const char* kPattern =
            "48 89 4C 24 08 57 48 83 EC 40 48 C7 44 24 30 FE FF FF FF";

        // LoadLevelSolo signature from Scaleform bindings:
        //   LoadLevelSolo(map: string, difficulty: int, isNew: bool = false, fillStat: bool = true)
        //
        // Calling convention (Scaleform internal):
        //   RCX = param_1: pointer to Scaleform args object
        //          *(*(param_1) + 0x18) = map name string object
        //          *(map_string_obj + 0xC) = actual char* data
        //   EDX = param_2: difficulty int
        //   R8  = param_3: additional args
        //   R9  = param_4: additional args
        //
        // We use the raw x64 calling convention since this is an internal function.

        typedef void (*LoadLevelSoloFn)(void* scaleformArgs, int difficulty,
            void* param3, void* param4);
        LoadLevelSoloFn g_original = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        
        void DetourLoadLevelSolo(void* param1, int param2, void* param3, void* param4) {
            printf("[hook] LOAD_LEVEL_SOLO fired\n");
            printf("[hook]   p1=%p p2=%d p3=%p p4=%p\n", param1, param2, param3, param4);
            if (param3) {
                uint64_t* args = (uint64_t*)param3;
                for (int i = 0; i < 8; i++) {
                    printf("[hook]   p3[%d] = 0x%llx\n", i, args[i]);
                    if (args[i] > 0x10000 && args[i] < 0x00007FFFFFFFFFFF) {
                        char* maybe = (char*)(args[i]);
                        if (maybe[0] >= 0x20 && maybe[0] < 0x7F) {
                            printf("[hook]     -> str: '%.32s'\n", maybe);
                        }
                        char* maybeC = maybe + 0xC;
                        if (maybeC[0] >= 0x20 && maybeC[0] < 0x7F) {
                            printf("[hook]     -> str@C: '%.32s'\n", maybeC);
                        }
                    }
                }
            }
            if (param4) {
                uint64_t* args = (uint64_t*)param4;
                for (int i = 0; i < 4; i++) {
                    printf("[hook]   p4[%d] = 0x%llx\n", i, args[i]);
                }
            }

            g_pipe->SendAsync("LOAD_LEVEL_SOLO fired");

            if (param3) {
                uint64_t firstArg = ((uint64_t*)param3)[0];
                if (firstArg > 0x10000) {
                    printf("[hook] MAP: '%s'\n", (char*)(firstArg + 0xC));
                }
            }

            if (g_original) {
                g_original(param1, param2, param3, param4);
            }
        }
    }

    bool InstallLoadLevelSoloHook(PipeClient* pipe) {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1) {
            printf("[hook] (load_level_solo) halo1.dll not loaded yet\n");
            return false;
        }

        void* target = FindPatternInModule(halo1, kPattern);
        if (!target) {
            printf("[hook] (load_level_solo) pattern not found\n");
            return false;
        }

        g_hookTarget = target;
        printf("[hook] (load_level_solo) LoadLevelSolo found at %p\n", target);

        MH_STATUS status = MH_CreateHook(target, (void*)DetourLoadLevelSolo,
            (void**)&g_original);
        if (status != MH_OK) {
            printf("[hook] (load_level_solo) MH_CreateHook failed: %d\n", status);
            return false;
        }

        status = MH_EnableHook(target);
        if (status != MH_OK) {
            printf("[hook] (load_level_solo) MH_EnableHook failed: %d\n", status);
            return false;
        }

        printf("[hook] (load_level_solo) installed.\n");
        return true;
    }

    void UninstallLoadLevelSoloHook() {
        if (g_hookTarget) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_original = nullptr;
        g_pipe = nullptr;
    }

}  // namespace haloap