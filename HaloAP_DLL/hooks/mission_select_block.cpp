#include "mission_select_block.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include "../item_handler.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <atomic>
#include <cstring>

namespace haloap {

    namespace {
        // === STRATEGY ===
        // Three hooks working together:
        //
        // 1. FUN_14050bdc0 (chapter tab setup) — sets a flag during mission population
        // 2. FUN_14088dc6c (item allocator) — tracks mission index and sets lock flag
        //    (does NOT return null — that crashes)
        // 3. Widget vtable[0x78/8] (add item) — skips adding locked items to the list
        //
        // Flow per mission:
        //   Allocator called → sets g_nextItemLocked based on mission index
        //   Item gets created normally (non-null)
        //   Add function called → if g_nextItemLocked, skip (item exists but isn't displayed)

        // === SHARED STATE ===
        std::atomic<bool> g_populatingMissions{ false };
        std::atomic<int> g_missionCounter{ 0 };
        std::atomic<bool> g_nextItemLocked{ false };

        // === HOOK 1: Chapter tab setup (FUN_14050bdc0) ===
        typedef void (*ChapterTabSetupFn)(void* controller);
        ChapterTabSetupFn g_chapterTabOriginal = nullptr;
        void* g_chapterTabTarget = nullptr;

        // === HOOK 2: Item allocator (FUN_14088dc6c) ===
        typedef void* (*AllocItemFn)(void* pool, uint32_t size, const char* type, int flags);
        AllocItemFn g_allocOriginal = nullptr;
        void* g_allocTarget = nullptr;

        // === HOOK 3: Widget add item (discovered at runtime from vtable) ===
        typedef void (*AddItemFn)(void* widget, void* item, int param);
        AddItemFn g_addItemOriginal = nullptr;
        void* g_addItemTarget = nullptr;
        bool g_addItemHooked = false;

        // === SHARED ===
        PipeClient* g_pipe = nullptr;
        LockCheckFn g_lockCheck = nullptr;

        bool IsMissionAllowed(int index) {
            int missionId;
            switch (index)
            {
            case 0:
            case 1:
                missionId = 0;
                break;
            case 2:
            case 3:
                missionId = 1;
                break;
            case 4:
            case 5:
                missionId = 2;
                break;
            case 6:
            case 7:
                missionId = 3;
                break;
            case 8:
            case 9:
                missionId = 4;
                break;
            case 10:
            case 11:
                missionId = 5;
                break;
            case 12:
            case 13:
                missionId = 6;
                break;
            case 14:
            case 15:
                missionId = 7;
                break;
            case 16:
            case 17:
                missionId = 8;
                break;
            case 18:
            case 19:
                missionId = 9;
                break;
                
            }
            return haloap::GetItemHandler().isMissionAllowed(missionId);
        }

        // --- Add item detour ---
        void DetourAddItem(void* widget, void* item, int param) {
            if (g_populatingMissions.load() && g_nextItemLocked.load()) {
                printf("[hook] ADD_ITEM: SKIPPING locked mission item\n");
                g_nextItemLocked.store(false);
                // Don't call original — item exists in memory but not in the widget
                return;
            }

            // Reset flag (in case it was set but not for a locked item)
            g_nextItemLocked.store(false);

            // Call original for unlocked items and non-mission items
            if (g_addItemOriginal) {
                g_addItemOriginal(widget, item, param);
            }
        }

        // --- Allocator detour ---
        void* DetourAllocItem(void* pool, uint32_t size, const char* type, int flags) {
            if (g_populatingMissions.load() && type != nullptr) {
                if (strcmp(type, "handleArrayMessage") == 0) {
                    int currentMission = g_missionCounter.fetch_add(1);
                    
                    if ((currentMission % 2) == 1) {
                        if (!IsMissionAllowed(currentMission))
                        {
                            g_nextItemLocked.store(true);
                        }
                        else
                        {
                            g_nextItemLocked.store(false);
                        }
                    }
                    else {
                        g_nextItemLocked.store(false);
                    }
                }
            }

            // Always call original — never return null
            if (g_allocOriginal) {
                return g_allocOriginal(pool, size, type, flags);
            }
            return nullptr;
        }

        // --- Chapter tab setup detour ---
        void DetourChapterTabSetup(void* controller) {
            printf("[hook] CHAPTER_TAB_SETUP fired\n");
            printf("[hook]   controller=%p\n", controller);

            // Discover and hook the widget's add function (once)
            if (!g_addItemHooked) {
                __try {
                    // Widget is embedded at controller+0x930
                    void* widget = (void*)((char*)controller + 0x910);
                    uint64_t* vt = *(uint64_t**)widget;
                    void* addFunc = (void*)vt[0x78 / 8];
                    
                    printf("[hook]   widget=%p vtable=%p addFunc=%p\n", widget, vt, addFunc);

                    MH_STATUS status = MH_CreateHook(addFunc, (void*)DetourAddItem,
                        (void**)&g_addItemOriginal);
                    if (status == MH_OK) {
                        status = MH_EnableHook(addFunc);
                        if (status == MH_OK) {
                            g_addItemTarget = addFunc;
                            g_addItemHooked = true;
                            printf("[hook]   add item hook installed at %p\n", addFunc);
                        } else {
                            printf("[hook]   add item MH_EnableHook failed: %d\n", status);
                        }
                    } else {
                        printf("[hook]   add item MH_CreateHook failed: %d\n", status);
                    }
                }
                __except (1) {
                    printf("[hook]   failed to discover add function\n");
                }
            }

            // Reset counter and set flag
            g_missionCounter.store(0);
            g_nextItemLocked.store(false);
            g_populatingMissions.store(true);

            // Call original — loop runs, allocator sets flags, add hook skips locked items
            if (g_chapterTabOriginal) {
                g_chapterTabOriginal(controller);
            }

            // Clear flag
            g_populatingMissions.store(false);
            g_nextItemLocked.store(false);

            printf("[hook] CHAPTER_TAB_SETUP complete, %d items processed\n",
                g_missionCounter.load());
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
        void* chapterTarget = (void*)((uint8_t*)exe + 0x50bdc0);
        printf("[hook] (mission_select) chapter tab at %p (byte: 0x%02x)\n",
            chapterTarget, *(uint8_t*)chapterTarget);

        g_chapterTabTarget = chapterTarget;
        MH_STATUS status = MH_CreateHook(chapterTarget, (void*)DetourChapterTabSetup,
            (void**)&g_chapterTabOriginal);
        if (status != MH_OK) {
            printf("[hook] (mission_select) chapter tab MH_CreateHook failed: %d\n", status);
            return false;
        }
        status = MH_EnableHook(chapterTarget);
        if (status != MH_OK) {
            printf("[hook] (mission_select) chapter tab MH_EnableHook failed: %d\n", status);
            return false;
        }
        printf("[hook] (mission_select) chapter tab hook installed.\n");

        // --- Hook 2: Allocator at exe+0x88dc6c ---
        void* allocTarget = (void*)((uint8_t*)exe + 0x88dc6c);
        printf("[hook] (mission_select) allocator at %p (byte: 0x%02x)\n",
            allocTarget, *(uint8_t*)allocTarget);

        g_allocTarget = allocTarget;
        status = MH_CreateHook(allocTarget, (void*)DetourAllocItem,
            (void**)&g_allocOriginal);
        if (status != MH_OK) {
            printf("[hook] (mission_select) allocator MH_CreateHook failed: %d\n", status);
            return true;
        }
        status = MH_EnableHook(allocTarget);
        if (status != MH_OK) {
            printf("[hook] (mission_select) allocator MH_EnableHook failed: %d\n", status);
            return true;
        }
        printf("[hook] (mission_select) allocator hook installed.\n");

        // Hook 3 (add item) is installed dynamically on first chapter tab setup call
        // because we need the widget's vtable to find the function address.

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
        g_chapterTabOriginal = nullptr;
        g_allocOriginal = nullptr;
        g_addItemOriginal = nullptr;
        g_pipe = nullptr;
        g_lockCheck = nullptr;
        g_populatingMissions.store(false);
        g_nextItemLocked.store(false);
    }

    void SetMissionLockCheck(LockCheckFn lockCheck) {
        g_lockCheck = lockCheck;
    }

}  // namespace haloap