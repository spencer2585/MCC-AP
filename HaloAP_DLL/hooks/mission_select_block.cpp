#include "mission_select_block.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "../item_handler.h"
#include "shared/common.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <atomic>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace haloap {

    namespace {
        // =================================================================
        // HOOK 1: Chapter tab setup (FUN_14050bdc0)
        // Sets a flag during mission population so the allocator can track items.
        // =================================================================
        typedef void (*ChapterTabSetupFn)(void* controller);
        ChapterTabSetupFn g_chapterTabOriginal = nullptr;
        void* g_chapterTabTarget = nullptr;

        // =================================================================
        // HOOK 2: Item allocator (FUN_14088dc6c)
        // Tracks "handleArrayMessage" allocations during mission population.
        // Sets g_nextItemLocked for locked missions.
        // =================================================================
        typedef void* (*AllocItemFn)(void* pool, uint32_t size, const char* type, int flags);
        AllocItemFn g_allocOriginal = nullptr;
        void* g_allocTarget = nullptr;

        // =================================================================
        // HOOK 3: Widget add item (discovered at runtime from vtable)
        // Skips adding locked mission items to the list.
        // =================================================================
        typedef void (*AddItemFn)(void* widget, void* item, int param);
        AddItemFn g_addItemOriginal = nullptr;
        void* g_addItemTarget = nullptr;
        bool g_addItemHooked = false;

        // =================================================================
        // HOOK 4: Lobby init (FUN_14050aa44)
        // Skips the game mode selection screen (Resume/Quickstart/Missions/Playlists)
        // and goes directly to mission select.
        // =================================================================
        typedef void (*LobbyInitFn)(void* controller, int param2);
        LobbyInitFn g_lobbyInitOriginal = nullptr;
        void* g_lobbyInitTarget = nullptr;
        
        typedef bool (*ScaleformInvokeFn)(void* controller, const char* funcName, void* args, int numArgs);
        ScaleformInvokeFn g_scaleformOriginal = nullptr;

        bool DetourScaleformInvoke(void* controller, const char* funcName, void* args, int numArgs) {
            if (funcName) {
                printf("[scaleform] %s (args=%d)\n", funcName, numArgs);
            }
            return g_scaleformOriginal(controller, funcName, args, numArgs);
        }
        
        // =================================================================
    // HOOK 5: Tick function (FUN_140511e3c)
    // Runs every frame. Used to detect game mode screen and manipulate it.
    // =================================================================
    typedef void (*TickFn)(void* controller, float deltaTime);
    TickFn g_tickOriginal = nullptr;
    void* g_tickTarget = nullptr;
    bool g_dumpedGameMode = false;

        void DetourTick(void* controller, float deltaTime) {
            static int lastState = -1;
            static int lastCounter = -1;
    
            __try {
                int state = *(int*)((char*)controller + 0x2464);
                int counter = *(int*)((char*)controller + 0x2500);
        
                if (state != lastState || counter != lastCounter) {
                    printf("[tick] state=0x%x counter=%d\n", (uint32_t)state, counter);
                    lastState = state;
                    lastCounter = counter;
                }
            } __except(1) {}
    
            if (g_tickOriginal) {
                g_tickOriginal(controller, deltaTime);
            }
        }

        // =================================================================
        // Shared state
        // =================================================================
        std::atomic<bool> g_populatingMissions{ false };
        std::atomic<int> g_missionCounter{ 0 };
        std::atomic<bool> g_nextItemLocked{ false };
        PipeClient* g_pipe = nullptr;
        LockCheckFn g_lockCheck = nullptr;

        // ----- Add item detour -----
        void DetourAddItem(void* widget, void* item, int param) {
            if (g_populatingMissions.load() && g_nextItemLocked.load()) {
                printf("[hook] ADD_ITEM: SKIPPING locked mission item\n");
                g_nextItemLocked.store(false);
                return;
            }
            g_nextItemLocked.store(false);
            if (g_addItemOriginal) {
                g_addItemOriginal(widget, item, param);
            }
        }

        // ----- Allocator detour -----
        void* DetourAllocItem(void* pool, uint32_t size, const char* type, int flags) {
            // Diagnostic: log all UI item allocations
            if (type != nullptr && (
                strcmp(type, "populateArray") == 0 ||
                strcmp(type, "populateContent") == 0 ||
                strcmp(type, "populateMenuBar") == 0 ||
                strcmp(type, "handleArrayMessage") == 0)) {
                printf("[alloc] type='%s' size=0x%x\n", type, size);
                }
            if (g_populatingMissions.load() && type != nullptr) {
                if (strcmp(type, "handleArrayMessage") == 0) {
                    int currentItem = g_missionCounter.fetch_add(1);
                    int missionId = currentItem / 2;
                    bool isVisualItem = (currentItem % 2 == 1);

                    if (isVisualItem && !GetItemHandler().isMissionAllowed(missionId)) {
                        printf("[hook] ALLOC: mission %d NOT ALLOWED, will skip\n", missionId);
                        g_nextItemLocked.store(true);
                    } else {
                        g_nextItemLocked.store(false);
                    }
                }
            }
            if (g_allocOriginal) {
                return g_allocOriginal(pool, size, type, flags);
            }
            return nullptr;
        }

        // ----- Chapter tab setup detour -----
        void DetourChapterTabSetup(void* controller) {
            printf("[hook] CHAPTER_TAB_SETUP fired\n");

            // Discover and hook the widget's add function (once)
            if (!g_addItemHooked) {
                __try {
                    void* widget = (void*)((char*)controller + 0x910);
                    uint64_t* vt = *(uint64_t**)widget;
                    void* addFunc = (void*)vt[0x78 / 8];

                    MH_STATUS status = MH_CreateHook(addFunc, (void*)DetourAddItem,
                        (void**)&g_addItemOriginal);
                    if (status == MH_OK) {
                        status = MH_EnableHook(addFunc);
                        if (status == MH_OK) {
                            g_addItemTarget = addFunc;
                            g_addItemHooked = true;
                            printf("[hook] add item hook installed at %p\n", addFunc);
                        }
                    }
                }
                __except (1) {
                    printf("[hook] failed to discover add function\n");
                }
            }

            g_missionCounter.store(0);
            g_nextItemLocked.store(false);
            g_populatingMissions.store(true);

            if (g_chapterTabOriginal) {
                g_chapterTabOriginal(controller);
            }

            g_populatingMissions.store(false);
            g_nextItemLocked.store(false);

            printf("[hook] CHAPTER_TAB_SETUP complete, %d items processed\n",
                g_missionCounter.load());
        }

        // ----- Lobby init detour -----
        void DetourLobbyInit(void* controller, int param2) {
            printf("[hook] LOBBY_INIT fired, param2=%d\n", param2);
    
            if (param2 == 1) {
                // Capture stack trace to find the UE4 caller
                void* frames[20] = {};
                USHORT count = RtlCaptureStackBackTrace(1, 20, frames, nullptr);
                for (USHORT i = 0; i < count; i++) {
                    HMODULE hMod = nullptr;
                    char name[64] = "?";
                    size_t offset = 0;
                    if (GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (LPCSTR)frames[i], &hMod)) {
                        char path[MAX_PATH];
                        if (GetModuleFileNameA(hMod, path, sizeof(path))) {
                            const char* base = strrchr(path, '\\');
                            base = base ? base + 1 : path;
                            strncpy_s(name, sizeof(name), base, _TRUNCATE);
                            offset = (size_t)frames[i] - (size_t)hMod;
                        }
                        }
                    printf("[hook]   STACK[%u] %s+0x%zx\n", i, name, offset);
                }
            }
    
            if (g_lobbyInitOriginal) {
                g_lobbyInitOriginal(controller, param2);
            }
        }
    }

    bool InstallMissionSelectBlockHook(PipeClient* pipe, LockCheckFn lockCheck) {
        g_pipe = pipe;
        g_lockCheck = lockCheck;

        HMODULE exe = GetModuleHandleA(NULL);
        if (!exe) {
            printf("[hook] (mission_select) couldn't get exe module handle\n");
            return false;
        }

        printf("[hook] (mission_select) exe base at %p\n", exe);

        // --- Hook 1: Chapter tab setup at exe+0x50bdc0 ---
        g_chapterTabTarget = (void*)((uint8_t*)exe + 0x50bdc0);
        MH_STATUS status = MH_CreateHook(g_chapterTabTarget, (void*)DetourChapterTabSetup,
            (void**)&g_chapterTabOriginal);
        if (status != MH_OK) {
            printf("[hook] (mission_select) chapter tab MH_CreateHook failed: %d\n", status);
            return false;
        }
        MH_EnableHook(g_chapterTabTarget);
        printf("[hook] (mission_select) chapter tab hook installed.\n");

        // --- Hook 2: Allocator at exe+0x88dc6c ---
        g_allocTarget = (void*)((uint8_t*)exe + 0x88dc6c);
        status = MH_CreateHook(g_allocTarget, (void*)DetourAllocItem,
            (void**)&g_allocOriginal);
        if (status == MH_OK) {
            MH_EnableHook(g_allocTarget);
            printf("[hook] (mission_select) allocator hook installed.\n");
        }

        // --- Hook 4: Lobby init at exe+0x50aa44 ---
        g_lobbyInitTarget = (void*)((uint8_t*)exe + 0x50aa44);
        status = MH_CreateHook(g_lobbyInitTarget, (void*)DetourLobbyInit,
            (void**)&g_lobbyInitOriginal);
        if (status == MH_OK) {
            MH_EnableHook(g_lobbyInitTarget);
            printf("[hook] (mission_select) lobby init hook installed.\n");
        } else {
            printf("[hook] (mission_select) lobby init MH_CreateHook failed: %d\n", status);
        }
        
        // --- Hook 5: Tick at exe+0x511e3c ---
        g_tickTarget = (void*)((uint8_t*)exe + 0x511e3c);
        status = MH_CreateHook(g_tickTarget, (void*)DetourTick, (void**)&g_tickOriginal);
        if (status == MH_OK) {
            MH_EnableHook(g_tickTarget);
            printf("[hook] (mission_select) tick hook installed.\n");
        } else
        {
            printf("[hook] (mission_select) tick hook failed: %d\n", status);
        }
        
        void* sfTarget = (void*)((uint8_t*)exe + 0x8aaee0);
        MH_CreateHook(sfTarget, (void*)DetourScaleformInvoke, (void**)&g_scaleformOriginal);
        MH_EnableHook(sfTarget);

        // Hook 3 (add item) is installed dynamically on first chapter tab setup call

        return true;
    }

    void UninstallMissionSelectBlockHook() {
        if (g_addItemTarget) {
            MH_DisableHook(g_addItemTarget);
            MH_RemoveHook(g_addItemTarget);
            g_addItemTarget = nullptr;
            g_addItemHooked = false;
        }
        if (g_chapterTabTarget) {
            MH_DisableHook(g_chapterTabTarget);
            MH_RemoveHook(g_chapterTabTarget);
            g_chapterTabTarget = nullptr;
        }
        if (g_allocTarget) {
            MH_DisableHook(g_allocTarget);
            MH_RemoveHook(g_allocTarget);
            g_allocTarget = nullptr;
        }
        if (g_lobbyInitTarget) {
            MH_DisableHook(g_lobbyInitTarget);
            MH_RemoveHook(g_lobbyInitTarget);
            g_lobbyInitTarget = nullptr;
        }
        g_chapterTabOriginal = nullptr;
        g_allocOriginal = nullptr;
        g_addItemOriginal = nullptr;
        g_lobbyInitOriginal = nullptr;
        g_pipe = nullptr;
        g_lockCheck = nullptr;
        g_populatingMissions.store(false);
        g_nextItemLocked.store(false);
    }

    void SetMissionLockCheck(LockCheckFn lockCheck) {
        g_lockCheck = lockCheck;
    }

}  // namespace haloap