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

// ---------------------------------------------------------------------------
// Skull disabler index -> bitmask lookup
// Ordered to match CE_SKULL_DISABLERS in items.py (GAME_SKULLS["ce"] minus
// PERM_DISABLED, preserving _CE list order):
//   0=Anger  1=BlackEye  2=Blind  3=Boom  4=Catch  5=EyePatch  6=Famine
//   7=Fog  8=Foreign  9=Ghost  10=GruntBP  11=GruntFuneral  12=Iron
//   13=Malfunction  14=Mythic  15=Pinata  16=Recession  17=Sputnik
//   18=ThatsJustWrong  19=Thunderstorm  20=ToughLuck
// ---------------------------------------------------------------------------
static constexpr int kDisablerCount = 21;
static constexpr uint64_t kDisablerBits[kDisablerCount] = {
    kBitAnger, // 0  Anger
    kBitBlackEye, // 1  Black Eye
    kBitBlind, // 2  Blind
    kBitBoom, // 3  Boom
    kBitCatch, // 4  Catch
    kBitEyePatch, // 5  Eye Patch
    kBitFamine, // 6  Famine
    kBitFog, // 7  Fog
    kBitForeign, // 8  Foreign
    kBitGhost, // 9  Ghost
    kBitGruntBirthdayParty, // 10 Grunt Birthday Party
    kBitGruntFuneral, // 11 Grunt Funeral
    kBitIron, // 12 Iron
    kBitMalfunction, // 13 Malfunction
    kBitMythic, // 14 Mythic
    kBitPinata, // 15 Pinata
    kBitRecession, // 16 Recession
    kBitSputnik, // 17 Sputnik
    kBitThatsJustWrong, // 18 That's Just... Wrong
    kBitThunderstorm, // 19 Thunderstorm
    kBitToughLuck, // 20 Tough Luck
};

// Forced-skull bitmasks per skullsanity tier
static constexpr uint64_t kForcedNonScoring =
    kBitBoom | kBitGhost | kBitGruntBirthdayParty | kBitGruntFuneral |
    kBitMalfunction | kBitPinata | kBitSputnik;

static constexpr uint64_t kForcedAll =
    kBitAnger | kBitBlackEye | kBitBlind | kBitBoom | kBitCatch |
    kBitEyePatch | kBitFamine | kBitFog | kBitForeign | kBitGhost |
    kBitGruntBirthdayParty | kBitGruntFuneral | kBitIron | kBitMalfunction |
    kBitMythic | kBitPinata | kBitRecession | kBitSputnik |
    kBitThatsJustWrong | kBitThunderstorm | kBitToughLuck;

// ---------------------------------------------------------------------------
// skull_id -> AP location ID
// ---------------------------------------------------------------------------
static constexpr int kSkullIdCount = 16;
static constexpr int kSkullIdToLocationId[kSkullIdCount] = {
    0, // 0 invalid
    120001, // 1 Iron
    120007, // 2 Fog
    120002, // 3 Mythic
    120005, // 4 Famine
    0, // 5 Engineer (unused)
    120004, // 6 Foreign
    120011, // 7 Eye Patch
    120009, // 8 Recession
    120008, // 9 Malfunction
    120010, // 10 Black Eye
    120013, // 11 Grunt BP
    120012, // 12 Pinata
    0, // 13 Deadeye (unused)
    120006, // 14 Bandana
    120003, // 15 Boom
};

// ---------------------------------------------------------------------------
// Skull bitmask pointer chain:
//   exe + 0x4004230 -> [+0x8] -> [+0xB8] -> [+0x20] -> value at [+0x708]
// NOTE: offset 0x4004230 may differ on Windows Store build. The __try block
// handles graceful failure, and diagnostics are logged on first attempt.
// ---------------------------------------------------------------------------
static bool g_bitmaskDiagLogged = false;

static uint64_t* ResolveSkullBitmask()
{
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) return nullptr;

    __try
    {
        uintptr_t p = *reinterpret_cast<uintptr_t*>(
            reinterpret_cast<uintptr_t>(exe) + 0x4004230);
        if (!g_bitmaskDiagLogged) printf("[skull-diag] Step 1: %p\n", (void*)p);
        if (!p)
        {
            g_bitmaskDiagLogged = true;
            return nullptr;
        }

        p = *reinterpret_cast<uintptr_t*>(p + 0x8);
        if (!g_bitmaskDiagLogged) printf("[skull-diag] Step 2: %p\n", (void*)p);
        if (!p)
        {
            g_bitmaskDiagLogged = true;
            return nullptr;
        }

        p = *reinterpret_cast<uintptr_t*>(p + 0xB8);
        if (!g_bitmaskDiagLogged) printf("[skull-diag] Step 3: %p\n", (void*)p);
        if (!p)
        {
            g_bitmaskDiagLogged = true;
            return nullptr;
        }

        p = *reinterpret_cast<uintptr_t*>(p + 0x20);
        if (!g_bitmaskDiagLogged) printf("[skull-diag] Step 4: %p\n", (void*)p);
        if (!p)
        {
            g_bitmaskDiagLogged = true;
            return nullptr;
        }

        uint64_t* result = reinterpret_cast<uint64_t*>(p + 0x708);
        if (!g_bitmaskDiagLogged)
        {
            printf("[skull-diag] Bitmask at %p = 0x%llX\n", result,
                   static_cast<unsigned long long>(*result));
            g_bitmaskDiagLogged = true;
        }
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (!g_bitmaskDiagLogged)
        {
            printf("[skull-diag] Pointer chain failed (access violation)\n");
            g_bitmaskDiagLogged = true;
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
        uint64_t g_forcedMask = 0;
        uint64_t g_disabledMask = 0;
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

                        // Step 3: Search backward for the handler function LEA
                        for (int j = 60; j > 0; j--)
                        {
                            size_t pos = i - j;
                            if ((base[pos] == 0x48 || base[pos] == 0x4C) && base[pos + 1] == 0x8D)
                            {
                                uint8_t m = base[pos + 2];
                                if ((m & 0x07) != 0x05) continue;
                                int32_t handlerOffset = *(int32_t*)(base + pos + 3);
                                void* handler = base + pos + 7 + handlerOffset;

                                if ((uint8_t*)handler >= base && (uint8_t*)handler < base + size)
                                {
                                    printf("[skull] OnSkullClaimed handler at %p\n", handler);
                                    return handler;
                                }
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
        case 1: g_forcedMask = kForcedNonScoring;
            break;
        case 2:
        case 3:
        case 4: g_forcedMask = kForcedAll;
            break;
        default: g_forcedMask = 0;
            break;
        }
        printf("[skull] skullsanity tier %d -> forced mask 0x%llx\n",
               tier, static_cast<unsigned long long>(g_forcedMask));
    }

    void DisableSkull(int disablerIdx)
    {
        if (disablerIdx < 0 || disablerIdx >= kDisablerCount) return;
        uint64_t bit = kDisablerBits[disablerIdx];

        {
            std::lock_guard<std::mutex> lock(g_skullMutex);
            g_disabledMask |= bit;
        }

        uint64_t* bitmask = ResolveSkullBitmask();
        if (bitmask)
        {
            *bitmask &= ~bit;
            printf("[skull] disabled skull idx %d (bit 0x%llx), bitmask now 0x%llx\n",
                   disablerIdx,
                   static_cast<unsigned long long>(bit),
                   static_cast<unsigned long long>(*bitmask));
        }
    }

    void ApplyForcedSkulls()
    {
        uint64_t forced;
        uint64_t disabled;
        {
            std::lock_guard<std::mutex> lock(g_skullMutex);
            forced = g_forcedMask;
            disabled = g_disabledMask;
        }

        if (forced == 0) return;
        if (g_inMission.load()) return;

        uint64_t* bitmask = ResolveSkullBitmask();
        if (!bitmask) return;

        static uint64_t* s_lastBitmaskPtr = nullptr;
        static uint64_t s_lastValue = ~0ULL;
        if (bitmask != s_lastBitmaskPtr)
        {
            printf("[skull] bitmask ptr resolved: %p (value 0x%llx)\n",
                   bitmask, static_cast<unsigned long long>(*bitmask));
            s_lastBitmaskPtr = bitmask;
            s_lastValue = ~0ULL;
        }

        uint64_t toSet = forced & ~disabled;
        uint64_t current = *bitmask;
        if ((current & toSet) != toSet)
        {
            *bitmask = current | toSet;
            printf("[skull] forced skulls applied: 0x%llx -> 0x%llx\n",
                   static_cast<unsigned long long>(current),
                   static_cast<unsigned long long>(*bitmask));
            s_lastValue = *bitmask;
        }
    }

    void SetInMission(bool inMission)
    {
        g_inMission.store(inMission);
        printf("[skull] in-mission: %s\n", inMission ? "true" : "false");
    }
} // namespace haloap
