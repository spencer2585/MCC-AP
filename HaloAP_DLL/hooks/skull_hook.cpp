#include "skull_hook.h"
#include "../minhook/MinHook.h"
#include "shared/common.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <Psapi.h>

// ---------------------------------------------------------------------------
// Skull bitmask constants (from skulls.py SKULL_BITS, shared MCC bitmask)
// ---------------------------------------------------------------------------
static constexpr uint64_t kBitAnger = 0x1;
static constexpr uint64_t kBitBandana     = 0x4;
static constexpr uint64_t kBitBlackEye = 0x8;
static constexpr uint64_t kBitBlind = 0x10;
static constexpr uint64_t kBitBoom = 0x40;
static constexpr uint64_t kBitCatch = 0x80;
static constexpr uint64_t kBitEyePatch = 0x400;
static constexpr uint64_t kBitFamine = 0x800;
static constexpr uint64_t kBitFog = 0x2000;
static constexpr uint64_t kBitForeign = 0x4000;
static constexpr uint64_t kBitGhost = 0x8000;
static constexpr uint64_t kBitGruntBirthdayParty = 0x10000;
static constexpr uint64_t kBitGruntFuneral = 0x20000;
static constexpr uint64_t kBitIron = 0x40000;
static constexpr uint64_t kBitMalfunction = 0x200000;
static constexpr uint64_t kBitMythic = 0x800000;
static constexpr uint64_t kBitPinata = 0x1000000;
static constexpr uint64_t kBitRecession = 0x4000000;
static constexpr uint64_t kBitSputnik = 0x20000000;
static constexpr uint64_t kBitThatsJustWrong = 0x100000000;
static constexpr uint64_t kBitThunderstorm = 0x400000000;
static constexpr uint64_t kBitToughLuck = 0x1000000000;
static constexpr uint64_t kBitAcrophobia  = 0x2000000000;

// ---------------------------------------------------------------------------
// Skull disabler index -> bitmask lookup
// Ordered to match CE_SKULL_DISABLERS in items.py (GAME_SKULLS["ce"] minus
// PERM_DISABLED, preserving _CE list order):
//   0=Anger  1=BlackEye  2=Blind  3=Boom  4=Catch  5=EyePatch  6=Famine
//   7=Fog  8=Foreign  9=Ghost  10=GruntBP  11=GruntFuneral  12=Iron
//   13=Malfunction  14=Mythic  15=Pinata  16=Recession  17=Sputnik
//   18=ThatsJustWrong  19=Thunderstorm  20=ToughLuck
// ---------------------------------------------------------------------------
static constexpr int kDisablerCount = 23;
static constexpr uint64_t kDisablerBits[kDisablerCount] = {
    // Scoring (CE)
    kBitAnger,              // 0  Anger
    kBitBlackEye,           // 1  Black Eye
    kBitBlind,              // 2  Blind
    kBitCatch,              // 3  Catch
    kBitEyePatch,           // 4  Eye Patch
    kBitFamine,             // 5  Famine
    kBitFog,                // 6  Fog
    kBitForeign,            // 7  Foreign
    kBitIron,               // 8  Iron
    kBitMythic,             // 9  Mythic
    kBitRecession,          // 10 Recession
    kBitThatsJustWrong,     // 11 That's Just... Wrong
    kBitThunderstorm,       // 12 Thunderstorm
    kBitToughLuck,          // 13 Tough Luck
    // Non-scoring
    kBitBandana,            // 14 Bandana
    kBitBoom,               // 15 Boom
    kBitGhost,              // 16 Ghost
    kBitGruntBirthdayParty, // 17 Grunt Birthday Party
    kBitGruntFuneral,       // 18 Grunt Funeral
    kBitMalfunction,        // 19 Malfunction
    kBitPinata,             // 20 Pinata
    kBitSputnik,            // 21 Sputnik
    kBitAcrophobia,         // 22 Acrophobia
};

// Forced-skull bitmasks per skullsanity tier
static constexpr uint64_t kForcedNonScoring =
    kBitAcrophobia | kBitBandana |kBitBoom | kBitGhost | kBitGruntBirthdayParty | kBitGruntFuneral |
    kBitMalfunction | kBitPinata | kBitSputnik;

static constexpr uint64_t kForcedAll =
    kBitAnger | kBitBlackEye | kBitBlind | kBitBoom | kBitCatch |
    kBitEyePatch | kBitFamine | kBitFog | kBitForeign | kBitGhost |
    kBitGruntBirthdayParty | kBitGruntFuneral | kBitIron | kBitMalfunction |
    kBitMythic | kBitPinata | kBitRecession | kBitSputnik |
    kBitThatsJustWrong | kBitThunderstorm | kBitToughLuck | kBitAcrophobia| kBitBandana;

// ---------------------------------------------------------------------------
// skull_id -> AP location ID
// ---------------------------------------------------------------------------
static constexpr int kSkullIdCount = 16;
static constexpr int kSkullIdToLocationId[kSkullIdCount] = {
    0, // 0 invalid
    101021, // 1 Iron
    105021, // 2 Fog
    102021, // 3 Mythic
    104021, // 4 Famine
    0, // 5 Engineer (unused)
    103022, // 6 Foreign
    107022, // 7 Eye Patch
    106021, // 8 Recession
    105022, // 9 Malfunction
    107021, // 10 Black Eye
    110021, // 11 Grunt BP
    108021, // 12 Pinata
    0, // 13 Deadeye (unused)
    104022, // 14 Bandana
    102022, // 15 Boom
};

// ---------------------------------------------------------------------------
// Skull bitmask pointer chain:
//   exe + 0x4004230 -> [+0x8] -> [+0xB8] -> [+0x20] -> value at [+0x708]
// NOTE: offset 0x4004230 may differ on Windows Store build. The __try block
// handles graceful failure, and diagnostics are logged on first attempt.
// ---------------------------------------------------------------------------
static int g_bitmaskAttempts = 0;

static uint64_t* ResolveSkullBitmask() {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) return nullptr;

    bool shouldLog = (g_bitmaskAttempts < 5 || g_bitmaskAttempts % 50 == 0);
    g_bitmaskAttempts++;

    __try {
        uintptr_t p = *reinterpret_cast<uintptr_t*>(
            reinterpret_cast<uintptr_t>(exe) + 0x4004230);
        if (!p) return nullptr;
        p = *reinterpret_cast<uintptr_t*>(p + 0x8);
        if (!p) return nullptr;
        p = *reinterpret_cast<uintptr_t*>(p + 0xB8);
        if (!p) return nullptr;
        p = *reinterpret_cast<uintptr_t*>(p + 0x20);
        if (!p) return nullptr;

        uint64_t* result = reinterpret_cast<uint64_t*>(p + 0x708);
        if (shouldLog) {
            printf("[skull] bitmask resolved at %p = 0x%llX (attempt %d)\n",
                result, static_cast<unsigned long long>(*result), g_bitmaskAttempts);
        }
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (shouldLog) {
            printf("[skull] bitmask chain failed (attempt %d)\n", g_bitmaskAttempts);
        }
        return nullptr;
    }
}

namespace haloap
{
    namespace
    {
        typedef void (*OnSkullClaimed_t)(void* a, int skull_id, void* c, void* d, void* e);

        OnSkullClaimed_t g_origOnSkullClaimed = nullptr;
        void* g_hookTarget = nullptr;
        PipeClient* g_pipe = nullptr;

        std::mutex g_skullMutex;
        uint64_t g_forcedOnMask = 0;
        uint64_t g_forcedOffMask = 0;
        uint64_t g_unlockedMask = 0;
        static constexpr uint64_t kScoringOnly = kForcedAll & ~kForcedNonScoring;
        std::atomic<bool> g_inMission{false};

        // -----------------------------------------------------------------
        // Find OnSkullClaimed handler via SSL function registration table
        // Searches for the "OnSkullClaimed(" string, finds the LEA that
        // references it, then walks backward to find the handler LEA.
        // -----------------------------------------------------------------
        void* FindOnSkullClaimed(HMODULE halo1)
        {
            MODULEINFO mi = {};
            GetModuleInformation(GetCurrentProcess(), halo1, &mi, sizeof(mi));
            uint8_t* base = (uint8_t*)halo1;
            size_t size = mi.SizeOfImage;

            // Step 1: Find "OnSkullClaimed(" string
            const char* needle = "OnSkullClaimed(";
            size_t needleLen = strlen(needle);
            uint8_t* stringAddr = nullptr;

            for (size_t i = 0; i < size - needleLen; i++)
            {
                if (memcmp(base + i, needle, needleLen) == 0)
                {
                    stringAddr = base + i;
                    break;
                }
            }
            if (!stringAddr)
            {
                printf("[skull] 'OnSkullClaimed' string not found\n");
                return nullptr;
            }
            printf("[skull] Found string at %p\n", stringAddr);

            // Step 2: Find the LEA instruction that references this string
            for (size_t i = 0; i < size - 7; i++)
            {
                if ((base[i] == 0x48 || base[i] == 0x4C) && base[i + 1] == 0x8D)
                {
                    uint8_t modrm = base[i + 2];
                    if ((modrm & 0x07) != 0x05) continue;
                    int32_t offset = *(int32_t*)(base + i + 3);
                    uint8_t* target = base + i + 7 + offset;
                    if (target == stringAddr)
                    {
                        printf("[skull] Found string reference at %p\n", base + i);

                        // Step 3: Search FORWARD for the handler function LEA
                        // Handler is assigned a few instructions after the string reference
                        for (int j = 0; j < 120; j++) {
                            size_t pos = i + j;
                            if (pos + 7 >= size) break;
                            if ((base[pos] == 0x48 || base[pos] == 0x4C) && base[pos + 1] == 0x8D) {
                                uint8_t m = base[pos + 2];
                                if ((m & 0x07) != 0x05) continue;
                                int32_t handlerOffset = *(int32_t*)(base + pos + 3);
                                void* handler = base + pos + 7 + handlerOffset;

                                if ((uint8_t*)handler < base || (uint8_t*)handler >= base + size)
                                    continue;

                                MEMORY_BASIC_INFORMATION mbi = {};
                                if (!VirtualQuery(handler, &mbi, sizeof(mbi)))
                                    continue;
                                if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                                    continue;

                                printf("[skull] OnSkullClaimed handler at %p\n", handler);
                                return handler;
                            }
                        }
                        break;
                    }
                }
            }

            printf("[skull] Could not resolve OnSkullClaimed handler\n");
            return nullptr;
        }

        // -----------------------------------------------------------------
        // Detour
        // -----------------------------------------------------------------
        void DetourOnSkullClaimed(void* a, int skull_id, void* c, void* d, void* e)
        {
            printf("[skull] OnSkullClaimed: skull_id=%d\n", skull_id);

            if (skull_id >= 1 && skull_id < kSkullIdCount)
            {
                int locationId = kSkullIdToLocationId[skull_id];
                if (locationId != 0 && g_pipe && g_pipe->IsConnected())
                {
                    std::string msg = "LOCATION_CHECKED: " + std::to_string(locationId);
                    g_pipe->SendAsync(msg);
                    printf("[skull] -> location %d sent\n", locationId);
                }
                else if (locationId == 0)
                {
                    printf("[skull] skull_id=%d (unused, no location)\n", skull_id);
                }
                else
                {
                    printf("[skull] skull_id=%d -> location %d (pipe not connected)\n",
                           skull_id, locationId);
                }
            }
            else
            {
                printf("[skull] skull_id=%d out of range [1,%d)\n", skull_id, kSkullIdCount);
            }

            if (g_origOnSkullClaimed)
                g_origOnSkullClaimed(a, skull_id, c, d, e);
        }
    } // anonymous namespace

    // =====================================================================
    // Public API
    // =====================================================================

    bool InstallSkullHook(PipeClient* pipe)
    {
        g_pipe = pipe;

        HMODULE halo1 = GetModuleHandleA("halo1.dll");
        if (!halo1)
        {
            printf("[skull] halo1.dll not loaded\n");
            return false;
        }

        void* target = FindOnSkullClaimed(halo1);
        if (!target)
        {
            printf("[skull] OnSkullClaimed not found\n");
            return false;
        }
        g_hookTarget = target;

        MH_STATUS s = MH_CreateHook(target,
                                    reinterpret_cast<void*>(DetourOnSkullClaimed),
                                    reinterpret_cast<void**>(&g_origOnSkullClaimed));
        if (s != MH_OK)
        {
            printf("[skull] MH_CreateHook failed: %d\n", s);
            return false;
        }

        s = MH_EnableHook(target);
        if (s != MH_OK)
        {
            printf("[skull] MH_EnableHook failed: %d\n", s);
            return false;
        }

        printf("[skull] OnSkullClaimed hook installed at %p\n", target);
        return true;
    }

    void UninstallSkullHook()
    {
        if (g_hookTarget)
        {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
        }
        g_origOnSkullClaimed = nullptr;
        g_pipe = nullptr;
    }

    void SetSkullsanityTier(int tier)
    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        switch (tier)
        {
        case 1: // non_scoring: non-scoring skulls locked off until received
            g_forcedOnMask = 0;
            g_forcedOffMask = kForcedNonScoring;
            break;
        case 2: // all_on: scoring locked on, non-scoring locked off
            g_forcedOnMask = kScoringOnly;
            g_forcedOffMask = kForcedNonScoring;
            break;
        case 3: // inverted: all locked off until received
            g_forcedOnMask = 0;
            g_forcedOffMask = kForcedAll;
            break;
        default: // off
            g_forcedOnMask = 0;
            g_forcedOffMask = 0;
            break;
        }
        printf("[skull] skullsanity tier %d -> forcedOn=0x%llx forcedOff=0x%llx\n",
               tier,
               static_cast<unsigned long long>(g_forcedOnMask),
               static_cast<unsigned long long>(g_forcedOffMask));
    }

    void UnlockSkull(int disablerIdx)
    {
        if (disablerIdx < 0 || disablerIdx >= kDisablerCount) return;
        uint64_t bit = kDisablerBits[disablerIdx];

        {
            std::lock_guard<std::mutex> lock(g_skullMutex);
            g_unlockedMask |= bit;
        }

        printf("[skull] unlocked skull idx %d (bit 0x%llx), unlocked mask now 0x%llx\n",
               disablerIdx,
               static_cast<unsigned long long>(bit),
               static_cast<unsigned long long>(g_unlockedMask));
    }

    void ApplyForcedSkulls()
    {
        uint64_t forcedOn, forcedOff, unlocked;
        {
            std::lock_guard<std::mutex> lock(g_skullMutex);
            forcedOn = g_forcedOnMask;
            forcedOff = g_forcedOffMask;
            unlocked = g_unlockedMask;
        }

        if (forcedOn == 0 && forcedOff == 0) return;
        if (g_inMission.load()) return;

        uint64_t* bitmask = ResolveSkullBitmask();
        if (!bitmask) return;

        uint64_t current = *bitmask;
        uint64_t updated = current;

        // Force ON skulls that aren't unlocked yet
        uint64_t toForceOn = forcedOn & ~unlocked;
        updated |= toForceOn;

        // Force OFF skulls that aren't unlocked yet
        uint64_t toForceOff = forcedOff & ~unlocked;
        updated &= ~toForceOff;

        if (updated != current) {
            *bitmask = updated;
            printf("[skull] bitmask: 0x%llx -> 0x%llx (forceOn=0x%llx forceOff=0x%llx unlocked=0x%llx)\n",
                static_cast<unsigned long long>(current),
                static_cast<unsigned long long>(updated),
                static_cast<unsigned long long>(toForceOn),
                static_cast<unsigned long long>(toForceOff),
                static_cast<unsigned long long>(unlocked));
        }
    }

    void SetInMission(bool inMission)
    {
        g_inMission.store(inMission);
        printf("[skull] in-mission: %s\n", inMission ? "true" : "false");
    }
    
    uint64_t GetUnlockedMask() {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        return g_unlockedMask;
    }
} // namespace haloap
