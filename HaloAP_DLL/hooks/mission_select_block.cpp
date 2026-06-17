#include "mission_select_block.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "../item_handler.h"
#include "shared/common.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <atomic>

namespace haloap {

    namespace
    {
        // =================================================================
        // UE4 pattern scan constants
        // =================================================================
        const char* kFNameToStringPattern =
            "40 57 48 83 ec ? 48 c7 44 24 ? fe ff ff ff 48 89 5c 24 ? "
            "48 8b da 48 8b f9 83 79 04 00";

        const char* kGUObjectArrayRefPattern =
            "84 c0 0f 84 ? ? ? ? 40 38 3d ? ? ? ? 74 ? "
            "48 8d 0d ? ? ? ? e8 ? ? ? ?";

        // =================================================================
        // Resolved UE4 addresses (populated at init)
        // =================================================================
        void* g_fnameToString = nullptr;
        uint8_t* g_guobjectArray = nullptr;

        // =================================================================
        // UE4 FName resolver
        // =================================================================
        
        struct FString {
            wchar_t* Data;
            int32_t Count;
            int32_t Max;
        };
        
        bool ResolveFName(uint8_t* exe, void* obj, int nameOffset, char* outBuf, int bufSize) {
            if (!g_fnameToString) { outBuf[0] = '?'; outBuf[1] = 0; return false; }
            typedef void (*FNameToStringFn)(void* fname, FString* out);
            auto ToString = (FNameToStringFn)g_fnameToString;
            FString result = {};
            __try {
                ToString((uint8_t*)obj + nameOffset, &result);
                if (result.Data && result.Count > 0) {
                    int len = (result.Count < bufSize - 1) ? result.Count : bufSize - 1;
                    for (int i = 0; i < len; i++) outBuf[i] = (char)result.Data[i];
                    outBuf[len] = 0;
                    return true;
                }
            } __except (1) {}
            outBuf[0] = '?'; outBuf[1] = 0;
            return false;
        }

        // =================================================================
        // Resolve UE4 addresses via pattern scan
        // =================================================================
        bool ResolveUE4Addresses() {
            HMODULE exe = GetModuleHandleA(nullptr);
            if (!exe) return false;

            // Resolve FName::ToString
            g_fnameToString = FindPatternInModule(exe, kFNameToStringPattern);
            if (!g_fnameToString) {
                printf("[hook] FName::ToString pattern not found\n");
                return false;
            }
            printf("[hook] FName::ToString at %p\n", g_fnameToString);

            // Resolve GUObjectArray via reference function
            uint8_t* guobjectRef = (uint8_t*)FindPatternInModule(exe, kGUObjectArrayRefPattern);
            if (!guobjectRef) {
                printf("[hook] GUObjectArray reference pattern not found\n");
                return false;
            }
            // Pattern: 84 c0 (2) | 0f 84 ???? (6) | 40 38 3d ???? (7) | 74 ? (2) | 48 8d 0d ???? (7) | e8 ???? (5)
            // LEA RCX,[rip+offset] starts at byte 17, offset bytes at byte 20
            int32_t ripOffset = *(int32_t*)(guobjectRef + 20);
            uint8_t* nextInstr = guobjectRef + 24; // LEA is 7 bytes, ends at 17+7=24
            g_guobjectArray = nextInstr + ripOffset;
            printf("[hook] GUObjectArray at %p\n", g_guobjectArray);

            return true;
        }

        // =================================================================
        // Helper: iterate UObjects using direct chunk access
        // =================================================================
        // Callback receives (exe, obj) and returns true to stop iteration
        typedef bool (*UObjectVisitorFn)(uint8_t* exe, void* obj, void* userData);

        void ForEachUObject(uint8_t* exe, UObjectVisitorFn visitor, void* userData) {
            if (!g_guobjectArray) return;
            void** Objects = *(void***)(g_guobjectArray + 0x10);
            int numElements = *(int*)(g_guobjectArray + 0x24);
            int numChunks = *(int*)(g_guobjectArray + 0x2C);
            if (!Objects || numElements <= 0) return;

            int remaining = numElements;
            for (int c = 0; c < numChunks && remaining > 0; c++) {
                if (!Objects[c]) { remaining -= 0x10000; continue; }
                uint8_t* chunk = (uint8_t*)Objects[c];
                int itemsInChunk = (remaining > 0x10000) ? 0x10000 : remaining;

                for (int n = 0; n < itemsInChunk; n++) {
                    void* obj = *(void**)(chunk + (n * 0x18));
                    if (!obj) continue;
                    __try {
                        if (visitor(exe, obj, userData)) return;
                    } __except (1) {}
                }
                remaining -= itemsInChunk;
            }
        }

        // =================================================================
        // HOOK 1: Chapter tab setup
        // =================================================================
        const char* kChapterTabPattern = 
            "48 8b c4 55 41 54 41 55 41 56 41 57 48 8d a8 ? ? ? ? "
            "48 81 ec ? ? ? ? 48 c7 45 ? fe ff ff ff 48 89 58 10 "
            "48 89 70 18 48 89 78 20 48 8b 05 ? ? ? ? 48 33 c4 "
            "48 89 85 ? ? ? ? 48 8b f1 45 33 e4 45 8b f4 44 89 "
            "64 24 ? 48 8d b9 10 09 00 00";
        
        typedef void (*ChapterTabSetupFn)(void* controller);
        ChapterTabSetupFn g_chapterTabOriginal = nullptr;
        void* g_chapterTabTarget = nullptr;

        // =================================================================
        // HOOK 2: Item allocator
        // =================================================================
        const char* kAllocatorPattern = 
            "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 "
            "48 83 ec ? 49 8b e9 41 bf 01 00 00 00 48 c1 ed 08 49 8b d9 "
            "45 84 cf 4d 8b f0 48 8b fa 48 8b f1";

        typedef void* (*AllocItemFn)(void* pool, uint32_t size, const char* type, int flags);
        AllocItemFn g_allocOriginal = nullptr;
        void* g_allocTarget = nullptr;

        // =================================================================
        // HOOK 3: Widget add item (discovered at runtime from vtable)
        // =================================================================
        typedef void (*AddItemFn)(void* widget, void* item, int param);
        AddItemFn g_addItemOriginal = nullptr;
        void* g_addItemTarget = nullptr;
        bool g_addItemHooked = false;

        // =================================================================
        // HOOK 4: Menu navigation handler
        // =================================================================
        const char* kMenuNavPattern = 
            "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 ? "
            "48 81 ec ? ? ? ? 48 c7 45 ? fe ff ff ff 48 8b d9 "
            "45 33 ed 41 8b fd 44 89 6d ? e8 ? ? ? ? 48 89 83 08 04 00 00";

        typedef void (*MenuNavFn)(void* controller);
        MenuNavFn g_menuNavOriginal = nullptr;
        void* g_menuNavTarget = nullptr;

        // =================================================================
        // Button collapse state
        // =================================================================
        void* g_setVisFunc = nullptr;
        void* g_menuBtnClass = nullptr;
        bool g_ue4CacheReady = false;

        // =================================================================
        // Shared state
        // =================================================================
        std::atomic<bool> g_populatingMissions{ false };
        std::atomic<int> g_missionCounter{ 0 };
        std::atomic<bool> g_nextItemLocked{ false };
        std::atomic<uint32_t> g_collapseGeneration{ 0 };
        PipeClient* g_pipe = nullptr;
        LockCheckFn g_lockCheck = nullptr;

        // ----- UE4 object cache visitor -----
        bool CacheVisitor(uint8_t* exe, void* obj, void* /*userData*/) {
            char name[128] = {};
            if (!ResolveFName(exe, obj, 0x18, name, sizeof(name))) return false;

            if (!g_setVisFunc && strcmp(name, "SetVisibility") == 0) {
                void* outer = *(void**)((uint8_t*)obj + 0x20);
                if (outer) {
                    char outerName[64] = {};
                    ResolveFName(exe, outer, 0x18, outerName, sizeof(outerName));
                    if (strcmp(outerName, "Widget") == 0)
                        g_setVisFunc = obj;
                }
            }

            if (!g_menuBtnClass) {
                void* cls = *(void**)((uint8_t*)obj + 0x10);
                char cn[128] = {};
                if (ResolveFName(exe, cls, 0x18, cn, sizeof(cn)) &&
                    strcmp(cn, "WBP_MCCMenuButton_C") == 0) {
                    g_menuBtnClass = cls;
                }
            }

            return (g_setVisFunc && g_menuBtnClass); // stop when both found
        }

        void CacheUE4Objects()
        {
            if (g_ue4CacheReady) return;
            if (!g_guobjectArray || !g_fnameToString) return;

            uint8_t* exe = (uint8_t*)GetModuleHandleA(nullptr);
            printf("[cache] Scanning UObjects...\n");

            ForEachUObject(exe, CacheVisitor, nullptr);

            g_ue4CacheReady = (g_setVisFunc && g_menuBtnClass);
            printf("[cache] SetVis=%p BtnClass=%p ready=%d\n",
                   g_setVisFunc, g_menuBtnClass, g_ue4CacheReady);
        }

        // ----- Collapse Quickstart/Playlists -----
        bool CollapseGameModeButtons() {
            if (!g_ue4CacheReady) CacheUE4Objects();
            if (!g_ue4CacheReady || !g_setVisFunc || !g_menuBtnClass || !g_guobjectArray) return false;

            uint8_t* exe = (uint8_t*)GetModuleHandleA(nullptr);
            void** Objects = *(void***)(g_guobjectArray + 0x10);
            int numElements = *(int*)(g_guobjectArray + 0x24);
            int numChunks = *(int*)(g_guobjectArray + 0x2C);
            if (!Objects || numElements <= 0) return false;

            int32_t classNameIndex = *(int32_t*)((uint8_t*)g_menuBtnClass + 0x18);

            typedef void (__fastcall *ProcessEventFn)(void* obj, void* func, void* parms);
            int collapsed = 0;
            int scanned = 0;
            int noLabel = 0;

            int remaining = numElements;
            for (int c = 0; c < numChunks && remaining > 0; c++) {
                if (!Objects[c]) { remaining -= 0x10000; continue; }
                uint8_t* chunk = (uint8_t*)Objects[c];
                int itemsInChunk = (remaining > 0x10000) ? 0x10000 : remaining;

                for (int n = 0; n < itemsInChunk; n++) {
                    void* obj = *(void**)(chunk + (n * 0x18));
                    if (!obj) continue;
                    __try {
                        if (*(void**)((uint8_t*)obj + 0x10) != g_menuBtnClass) continue;
                        int32_t objNameIndex = *(int32_t*)((uint8_t*)obj + 0x18);
                        int32_t objNameNum = *(int32_t*)((uint8_t*)obj + 0x1C);
                        if (objNameIndex != classNameIndex || objNameNum <= 0) continue;

                        scanned++;
                        char label[32] = {};
                        void* textBlock = *(void**)((uint8_t*)obj + 0x4B8);
                        if (!textBlock) { noLabel++; continue; }
                        void* textData = *(void**)((uint8_t*)textBlock + 0x180);
                        if (!textData) { noLabel++; continue; }
                        wchar_t* ws = *(wchar_t**)((uint8_t*)textData + 0x28);
                        if (!ws) { noLabel++; continue; }

                        for (int j = 0; j < 28 && ws[j] && ws[j] < 0x7F; j++)
                            label[j] = (char)ws[j];

                        if (strcmp(label, "QUICKSTART") == 0 || strcmp(label, "PLAYLISTS") == 0) {
                            uint8_t params[16] = {};
                            params[0] = 1;
                            uint64_t* vtable = *(uint64_t**)obj;
                            ProcessEventFn pe = (ProcessEventFn)vtable[0x40];
                            pe(obj, g_setVisFunc, params);
                            collapsed++;
                            printf("[collapse] Hidden '%s' (num=%d)\n", label, objNameNum);
                        }
                    } __except (1) {}
                }
                remaining -= itemsInChunk;
            }

            printf("[collapse] scanned=%d noLabel=%d collapsed=%d\n", scanned, noLabel, collapsed);
            return collapsed > 0;
        }

        // ----- Deferred collapse thread -----
        static volatile LONG g_collapseRunning = 0;

        DWORD WINAPI CollapseThread(LPVOID param) {
            if (InterlockedCompareExchange(&g_collapseRunning, 1, 0) != 0) return 0;
            uint32_t myGen = (uint32_t)(uintptr_t)param;

            int delays[] = { 100, 200, 500 };
            for (int i = 0; i < 3; i++) {
                Sleep(delays[i]);
                if (g_collapseGeneration.load() != myGen) break;
                CollapseGameModeButtons();
            }

            g_collapseRunning = 0;
            return 0;
        }

        // ----- Menu navigation detour -----

        static bool IsGameTitle(const char* label) {
            return (strstr(label, "HALO") != nullptr || 
                    strstr(label, "CROSS-GAME") != nullptr);
        }

        static bool g_shouldCollapse = false;

        void DetourMenuNav(void* controller) {
            uint32_t thisGen = g_collapseGeneration.fetch_add(1) + 1;
            char label[64] = {};
            bool hasLabel = false;
            
            void* viewModel = *(void**)((uint8_t*)controller + 0x2d8);
            if (viewModel) {
                __try {
                    void* textData = *(void**)((uint8_t*)viewModel + 0x28);
                    if (textData) {
                        wchar_t* ws = *(wchar_t**)((uint8_t*)textData + 0x28);
                        if (ws && ws[0]) {
                            for (int i = 0; i < 60 && ws[i]; i++)
                                label[i] = (char)ws[i];
                            hasLabel = true;
                        }
                    }
                } __except (1) {}
            }

            // Block Quickstart
            if (hasLabel && strcmp(label, "QUICKSTART") == 0) {
                printf("[hook] Blocked click on '%s'\n", label);
                return;
            }

            // Check if navigating to a game mode screen
            g_shouldCollapse = hasLabel && IsGameTitle(label);
            if (g_shouldCollapse)
                printf("[hook] Navigating to game: '%s', will collapse buttons\n", label);

            g_menuNavOriginal(controller);

            if (g_shouldCollapse)
                CreateThread(nullptr, 0, CollapseThread, (LPVOID)(uintptr_t)thisGen, 0, nullptr);
        }

        // ----- Add item detour -----
        void DetourAddItem(void* widget, void* item, int param) {
            if (g_populatingMissions.load() && g_nextItemLocked.load()) {
                printf("[hook] Skipping locked mission item\n");
                g_nextItemLocked.store(false);
                return;
            }
            g_nextItemLocked.store(false);
            if (g_addItemOriginal)
                g_addItemOriginal(widget, item, param);
        }

        // ----- Allocator detour -----
        void* DetourAllocItem(void* pool, uint32_t size, const char* type, int flags) {
            if (g_populatingMissions.load() && type != nullptr) {
                if (strcmp(type, "handleArrayMessage") == 0) {
                    int currentItem = g_missionCounter.fetch_add(1);
                    int missionId = currentItem / 2;
                    bool isVisualItem = (currentItem % 2 == 1);
                    if (isVisualItem && !GetItemHandler().isMissionAllowed(missionId)) {
                        printf("[hook] Mission %d locked, will skip\n", missionId);
                        g_nextItemLocked.store(true);
                    } else {
                        g_nextItemLocked.store(false);
                    }
                }
            }
            return g_allocOriginal ? g_allocOriginal(pool, size, type, flags) : nullptr;
        }

        // ----- Chapter tab setup detour -----
        void DetourChapterTabSetup(void* controller) {
            int32_t screenId = 0;
            __try {
                screenId = *(int32_t*)((uint8_t*)controller + 0x230);
            } __except(1) {}
            
            bool isMissionScreen = (screenId > 18);
            printf("[hook] Chapter tab setup (screenId=%d, isMission=%d)\n", screenId, isMissionScreen);

            if (!g_addItemHooked) {
                __try {
                    void* widget = (void*)((char*)controller + 0x910);
                    uint64_t* vt = *(uint64_t**)widget;
                    void* addFunc = (void*)vt[0x78 / 8];
                    MH_STATUS status = MH_CreateHook(addFunc, (void*)DetourAddItem,
                        (void**)&g_addItemOriginal);
                    if (status == MH_OK && MH_EnableHook(addFunc) == MH_OK) {
                        g_addItemTarget = addFunc;
                        g_addItemHooked = true;
                        printf("[hook] Add item hook installed at %p\n", addFunc);
                    }
                } __except (1) {
                    printf("[hook] Failed to discover add function\n");
                }
            }

            g_missionCounter.store(0);
            g_nextItemLocked.store(false);
            g_populatingMissions.store(isMissionScreen);

            if (g_chapterTabOriginal)
                g_chapterTabOriginal(controller);

            g_populatingMissions.store(false);
            g_nextItemLocked.store(false);
            printf("[hook] Chapter tab setup complete, %d items processed\n",
                g_missionCounter.load());
        }

    } // end anonymous namespace

    // =================================================================
    // Public API
    // =================================================================

    void InitGameModeButtonCollapse() {
        if (!g_fnameToString || !g_guobjectArray) {
            ResolveUE4Addresses();
        }
        // Don't cache here — UObjects may not be loaded yet
        // Cache will be attempted on first collapse trigger
    }

    bool InstallMissionSelectBlockHook(PipeClient* pipe, LockCheckFn lockCheck) {
        g_pipe = pipe;
        g_lockCheck = lockCheck;

        HMODULE exe = GetModuleHandleA(NULL);
        if (!exe) {
            printf("[hook] (mission_select) Couldn't get exe module handle\n");
            return false;
        }
        printf("[hook] (mission_select) exe base at %p\n", exe);

        // Hook 1: Chapter tab setup
        g_chapterTabTarget = FindPatternInModule(exe, kChapterTabPattern);
        if (!g_chapterTabTarget) {
            printf("[hook] (mission_select) Chapter tab pattern not found\n");
            return false;
        }
        printf("[hook] (mission_select) Chapter tab found at %p\n", g_chapterTabTarget);
        MH_STATUS status = MH_CreateHook(g_chapterTabTarget, (void*)DetourChapterTabSetup,
            (void**)&g_chapterTabOriginal);
        if (status != MH_OK) {
            printf("[hook] (mission_select) Chapter tab hook failed: %d\n", status);
            return false;
        }
        MH_EnableHook(g_chapterTabTarget);
        printf("[hook] (mission_select) Chapter tab hook installed\n");

        // Hook 2: Allocator
        g_allocTarget = FindPatternInModule(exe, kAllocatorPattern);
        if (!g_allocTarget) {
            printf("[hook] (mission_select) Allocator pattern not found\n");
            return false;
        }
        printf("[hook] (mission_select) Allocator found at %p\n", g_allocTarget);
        status = MH_CreateHook(g_allocTarget, (void*)DetourAllocItem, (void**)&g_allocOriginal);
        if (status == MH_OK) {
            MH_EnableHook(g_allocTarget);
            printf("[hook] (mission_select) Allocator hook installed\n");
        }

        // Hook 3: Add item — installed dynamically

        // Hook 4: Menu navigation
        g_menuNavTarget = FindPatternInModule(exe, kMenuNavPattern);
        if (!g_menuNavTarget) {
            printf("[hook] (mission_select) Menu nav pattern not found\n");
            return false;
        }
        printf("[hook] (mission_select) Menu nav found at %p\n", g_menuNavTarget);
        status = MH_CreateHook(g_menuNavTarget, (void*)DetourMenuNav, (void**)&g_menuNavOriginal);
        if (status == MH_OK) {
            MH_EnableHook(g_menuNavTarget);
            printf("[hook] (mission_select) Menu nav hook installed\n");
        } else {
            printf("[hook] (mission_select) Menu nav hook failed: %d\n", status);
        }

        return true;
    }

    void UninstallMissionSelectBlockHook() {
        if (g_addItemTarget) { MH_DisableHook(g_addItemTarget); MH_RemoveHook(g_addItemTarget); g_addItemTarget = nullptr; g_addItemHooked = false; }
        if (g_chapterTabTarget) { MH_DisableHook(g_chapterTabTarget); MH_RemoveHook(g_chapterTabTarget); g_chapterTabTarget = nullptr; }
        if (g_allocTarget) { MH_DisableHook(g_allocTarget); MH_RemoveHook(g_allocTarget); g_allocTarget = nullptr; }
        if (g_menuNavTarget) { MH_DisableHook(g_menuNavTarget); MH_RemoveHook(g_menuNavTarget); g_menuNavTarget = nullptr; }
        g_chapterTabOriginal = nullptr;
        g_allocOriginal = nullptr;
        g_addItemOriginal = nullptr;
        g_menuNavOriginal = nullptr;
        g_pipe = nullptr;
        g_lockCheck = nullptr;
        g_populatingMissions.store(false);
        g_nextItemLocked.store(false);
    }

    void SetMissionLockCheck(LockCheckFn lockCheck) {
        g_lockCheck = lockCheck;
    }

} // namespace haloap