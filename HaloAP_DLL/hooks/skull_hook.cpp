#include "skull_hook.h"
#include "../minhook/MinHook.h"
#include "../pattern_scan.h"
#include "shared/common.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <Psapi.h>

// ---------------------------------------------------------------------------
// Skull bitmask constants
// ---------------------------------------------------------------------------
static constexpr uint64_t kBitAnger              = 0x1;
static constexpr uint64_t kBitBandana            = 0x4;
static constexpr uint64_t kBitBlackEye           = 0x8;
static constexpr uint64_t kBitBlind              = 0x10;
static constexpr uint64_t kBitBoom               = 0x40;
static constexpr uint64_t kBitCatch              = 0x80;
static constexpr uint64_t kBitEyePatch           = 0x400;
static constexpr uint64_t kBitFamine             = 0x800;
static constexpr uint64_t kBitFog                = 0x2000;
static constexpr uint64_t kBitForeign            = 0x4000;
static constexpr uint64_t kBitGhost              = 0x8000;
static constexpr uint64_t kBitGruntBirthdayParty = 0x10000;
static constexpr uint64_t kBitGruntFuneral       = 0x20000;
static constexpr uint64_t kBitIron               = 0x40000;
static constexpr uint64_t kBitMalfunction        = 0x200000;
static constexpr uint64_t kBitMythic             = 0x800000;
static constexpr uint64_t kBitPinata             = 0x1000000;
static constexpr uint64_t kBitRecession          = 0x4000000;
static constexpr uint64_t kBitSputnik            = 0x20000000;
static constexpr uint64_t kBitThatsJustWrong     = 0x100000000;
static constexpr uint64_t kBitThunderstorm       = 0x400000000;
static constexpr uint64_t kBitToughLuck          = 0x1000000000;
static constexpr uint64_t kBitAcrophobia         = 0x2000000000;

// ---------------------------------------------------------------------------
// Skull disabler index -> bitmask lookup
// Must match CE_SKULL_DISABLERS order in APWorld items.py:
//   Scoring first, then non-scoring
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
    kBitAcrophobia | kBitBandana | kBitBoom | kBitGhost |
    kBitGruntBirthdayParty | kBitGruntFuneral |
    kBitMalfunction | kBitPinata | kBitSputnik;

static constexpr uint64_t kForcedAll =
    kBitAnger | kBitBlackEye | kBitBlind | kBitBoom | kBitCatch |
    kBitEyePatch | kBitFamine | kBitFog | kBitForeign | kBitGhost |
    kBitGruntBirthdayParty | kBitGruntFuneral | kBitIron | kBitMalfunction |
    kBitMythic | kBitPinata | kBitRecession | kBitSputnik |
    kBitThatsJustWrong | kBitThunderstorm | kBitToughLuck |
    kBitAcrophobia | kBitBandana;

static constexpr uint64_t kScoringOnly = kForcedAll & ~kForcedNonScoring;

// ---------------------------------------------------------------------------
// Skull ID -> AP location ID (for skull pickup detection)
// ---------------------------------------------------------------------------
static constexpr int kSkullIdCount = 16;
static constexpr int kSkullIdToLocationId[kSkullIdCount] = {
    0,       // 0  invalid
    101021,  // 1  Iron
    105021,  // 2  Fog
    102021,  // 3  Mythic
    104021,  // 4  Famine
    0,       // 5  Engineer (unused)
    103022,  // 6  Foreign
    107022,  // 7  Eye Patch
    106021,  // 8  Recession
    105022,  // 9  Malfunction
    107021,  // 10 Black Eye
    110021,  // 11 Grunt BP
    108021,  // 12 Pinata
    0,       // 13 Deadeye (unused)
    104022,  // 14 Bandana
    102022,  // 15 Boom
};

// ---------------------------------------------------------------------------
// Skull bitmask resolution
// Dual-offset approach: tries Steam offset first, then Windows Store.
// Cached after first successful resolution.
// ---------------------------------------------------------------------------
static uint64_t* WalkPointerChain(uintptr_t rootAddr)
{
    __try
    {
        uintptr_t p = *reinterpret_cast<uintptr_t*>(rootAddr);
        if (!p || p < 0x10000 || p > 0x7FFFFFFFFFFF) return nullptr;

        uintptr_t p1 = *reinterpret_cast<uintptr_t*>(p + 0x8);
        if (!p1 || p1 < 0x10000 || p1 > 0x7FFFFFFFFFFF) return nullptr;

        uintptr_t p2 = *reinterpret_cast<uintptr_t*>(p1 + 0xB8);
        if (!p2 || p2 < 0x10000 || p2 > 0x7FFFFFFFFFFF) return nullptr;

        uintptr_t p3 = *reinterpret_cast<uintptr_t*>(p2 + 0x20);
        if (!p3 || p3 < 0x10000 || p3 > 0x7FFFFFFFFFFF) return nullptr;

        uint64_t val = *reinterpret_cast<uint64_t*>(p3 + 0x708);
        if (val > 0x3FFFFFFFFFF) return nullptr;

        return reinterpret_cast<uint64_t*>(p3 + 0x708);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        printf("[skull] chain walk exception\n");
        return nullptr;
    }
}

static uintptr_t g_resolvedGlobalAddr = 0;

static uint64_t* ResolveSkullBitmask()
{
    if (!g_resolvedGlobalAddr)
    {
        HMODULE exe = GetModuleHandleA(nullptr);
        if (!exe) return nullptr;

        MODULEINFO mi = {};
        GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof(mi));
        uint8_t* base = reinterpret_cast<uint8_t*>(exe);
        size_t size = mi.SizeOfImage;

        // Signature: the manager allocation site
        // MOV EDX, 0x5e8 / LEA RCX,[rip+??] / CALL ?? / MOV [RSP+0x30],RAX /
        // TEST RAX,RAX / JZ +0xD / MOV RDX,RBX / MOV RCX,RAX / CALL ?? /
        // JMP +3 / MOV RAX,R14 / MOV [rip+??],RAX
        static const uint8_t sig[] = {
            0xBA, 0xE8, 0x05, 0x00, 0x00,             // MOV EDX, 0x5e8
            0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // LEA RCX, [rip+??]
            0xE8, 0x00, 0x00, 0x00, 0x00,             // CALL alloc
            0x48, 0x89, 0x44, 0x24, 0x00,             // MOV [RSP+??], RAX
            0x48, 0x85, 0xC0,                         // TEST RAX, RAX
            0x74, 0x0D,                               // JZ +0xD
            0x48, 0x8B, 0xD3,                         // MOV RDX, RBX
            0x48, 0x8B, 0xC8,                         // MOV RCX, RAX
            0xE8, 0x00, 0x00, 0x00, 0x00,             // CALL ctor
            0xEB, 0x03,                               // JMP +3
            0x49, 0x8B, 0xC6,                         // MOV RAX, R14
            0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00  // MOV [rip+??], RAX
        };
        static const char mask[] =
            "xxxxx"    // MOV EDX
            "xxx????"  // LEA RCX
            "x????"    // CALL
            "xxxx?"    // MOV [RSP+??], RAX
            "xxx"      // TEST
            "xx"       // JZ
            "xxx"      // MOV RDX,RBX
            "xxx"      // MOV RCX,RAX
            "x????"    // CALL
            "xx"       // JMP
            "xxx"      // MOV RAX,R14
            "xxx????"; // MOV [rip+??]

        static_assert(sizeof(sig) == 50, "sig size mismatch");
        static_assert(sizeof(mask) - 1 == 50, "mask size mismatch");

        uint8_t* match = nullptr;
        for (size_t i = 0; i + sizeof(sig) <= size; i++)
        {
            bool found = true;
            for (size_t j = 0; j < sizeof(sig); j++)
            {
                if (mask[j] == 'x' && base[i + j] != sig[j])
                {
                    found = false;
                    break;
                }
            }
            if (found) { match = base + i; break; }
        }

        if (!match)
        {
            printf("[skull] manager alloc signature not found\n");
            return nullptr;
        }

        // disp32 is at offset 46 (the ?? in the final MOV [rip+disp32], RAX)
        // instruction ends at offset 50
        int32_t disp = *reinterpret_cast<int32_t*>(match + 46);
        g_resolvedGlobalAddr = reinterpret_cast<uintptr_t>(match + 50 + disp);

        printf("[skull] global pointer at %p (via sig scan)\n",
            reinterpret_cast<void*>(g_resolvedGlobalAddr));
    }

    // The global might be null if the manager hasn't been created yet
    uintptr_t root = *reinterpret_cast<uintptr_t*>(g_resolvedGlobalAddr);
    if (!root) return nullptr;

   return WalkPointerChain(g_resolvedGlobalAddr);
}

namespace haloap
{
    namespace
    {
        typedef void (*OnSkullClaimed_t)(void* a, int skull_id, void* c, void* d, void* e);

        OnSkullClaimed_t g_origOnSkullClaimed = nullptr;
        void*            g_hookTarget         = nullptr;
        PipeClient*      g_pipe               = nullptr;

        std::mutex        g_skullMutex;
        uint64_t          g_forcedOnMask   = 0;
        uint64_t          g_forcedOffMask  = 0;
        uint64_t          g_unlockedMask   = 0;
        std::atomic<bool> g_inMission{ false };

        // -----------------------------------------------------------------
        // Find OnSkullClaimed handler via SSL function registration table
        // -----------------------------------------------------------------
        void* FindOnSkullClaimed(HMODULE halo1)
        {
            MODULEINFO mi = {};
            GetModuleInformation(GetCurrentProcess(), halo1, &mi, sizeof(mi));
            uint8_t* base = (uint8_t*)halo1;
            size_t size = mi.SizeOfImage;

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

                        // Search forward for the handler function LEA
                        for (int j = 0; j < 120; j++)
                        {
                            size_t pos = i + j;
                            if (pos + 7 >= size) break;
                            if ((base[pos] == 0x48 || base[pos] == 0x4C) && base[pos + 1] == 0x8D)
                            {
                                uint8_t m = base[pos + 2];
                                if ((m & 0x07) != 0x05) continue;
                                int32_t handlerOffset = *(int32_t*)(base + pos + 3);
                                void* handler = base + pos + 7 + handlerOffset;

                                if ((uint8_t*)handler < base || (uint8_t*)handler >= base + size)
                                    continue;

                                // Skip the string reference itself
                                if ((uint8_t*)handler == stringAddr)
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
        static int ReadSkullIdFromParam(void* c)
        {
            __try
            {
                return *(int*)((uint8_t*)c + 0x10);
            }
            __except (1)
            {
                return -1;
            }
        }

        void DetourOnSkullClaimed(void* a, int skull_id, void* c, void* d, void* e)
        {
            int actualSkullId = ReadSkullIdFromParam(c);

            printf("[skull] OnSkullClaimed: skull_id=%d\n", actualSkullId);

            if (actualSkullId >= 1 && actualSkullId < kSkullIdCount)
            {
                int locationId = kSkullIdToLocationId[actualSkullId];
                if (locationId != 0 && g_pipe && g_pipe->IsConnected())
                {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "LOCATION_CHECKED: %d", locationId);
                    g_pipe->SendAsync(msg);
                    printf("[skull] -> location %d sent\n", locationId);
                }
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
        g_resolvedGlobalAddr = 0;
    }

    void SetSkullsanityTier(int tier)
    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        switch (tier)
        {
        case 1:  // non_scoring: non-scoring skulls locked off until received
            g_forcedOnMask  = 0;
            g_forcedOffMask = kForcedNonScoring;
            break;
        case 2:  // all_on: scoring locked on, non-scoring locked off
            g_forcedOnMask  = kScoringOnly;
            g_forcedOffMask = kForcedNonScoring;
            break;
        case 3:  // inverted: all locked off until received
            g_forcedOnMask  = 0;
            g_forcedOffMask = kForcedAll;
            break;
        default: // off
            g_forcedOnMask  = 0;
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
            forcedOn  = g_forcedOnMask;
            forcedOff = g_forcedOffMask;
            unlocked  = g_unlockedMask;
        }

        if (forcedOn == 0 && forcedOff == 0) return;
        if (g_inMission.load()) return;

        uint64_t* bitmask = ResolveSkullBitmask();
        if (!bitmask) return;

        uint64_t current = *bitmask;
        uint64_t updated = current;

        uint64_t toForceOn = forcedOn & ~unlocked;
        updated |= toForceOn;

        uint64_t toForceOff = forcedOff & ~unlocked;
        updated &= ~toForceOff;

        if (updated != current)
        {
            *bitmask = updated;
            printf("[skull] bitmask at %p: 0x%llx -> 0x%llx\n",
                bitmask,
                static_cast<unsigned long long>(current),
                static_cast<unsigned long long>(updated));
        }
    }

    void SetInMission(bool inMission)
    {
        g_inMission.store(inMission);
        printf("[skull] in-mission: %s\n", inMission ? "true" : "false");
    }

    uint64_t GetUnlockedMask()
    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        return g_unlockedMask;
    }
    
    uint64_t GetForcedMask()
    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        return g_forcedOnMask | g_forcedOffMask;
    }

} // namespace haloap