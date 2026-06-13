#include "skull_hook.h"
#include "../minhook/MinHook.h"
#include "shared/common.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <mutex>

// ---------------------------------------------------------------------------
// Skull bitmask constants (from skulls.py SKULL_BITS, shared MCC bitmask)
// ---------------------------------------------------------------------------
static constexpr uint64_t kBitAnger              = 0x1;
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
    kBitAnger,              // 0  Anger
    kBitBlackEye,           // 1  Black Eye
    kBitBlind,              // 2  Blind
    kBitBoom,               // 3  Boom
    kBitCatch,              // 4  Catch
    kBitEyePatch,           // 5  Eye Patch
    kBitFamine,             // 6  Famine
    kBitFog,                // 7  Fog
    kBitForeign,            // 8  Foreign
    kBitGhost,              // 9  Ghost
    kBitGruntBirthdayParty, // 10 Grunt Birthday Party
    kBitGruntFuneral,       // 11 Grunt Funeral
    kBitIron,               // 12 Iron
    kBitMalfunction,        // 13 Malfunction
    kBitMythic,             // 14 Mythic
    kBitPinata,             // 15 Pinata
    kBitRecession,          // 16 Recession
    kBitSputnik,            // 17 Sputnik
    kBitThatsJustWrong,     // 18 That's Just... Wrong
    kBitThunderstorm,       // 19 Thunderstorm
    kBitToughLuck,          // 20 Tough Luck
};

// Forced-skull bitmasks per skullsanity tier (matching _skulls_for_tier in
// items.py):
//   Tier 0 (off)         : no skulls forced
//   Tier 1 (non_scoring) : NON_SCORING CE skulls only
//   Tier 2/3/4 (hard+)   : all CE_SKULL_DISABLERS
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
// skull_id is the value returned by GetSkullIdx() in the ssl scripting system.
// AP location IDs are CE_SKULL_OFFSET (120000) + CE_SKULL_DATA index.
// Unused slots (Engineer=5, Deadeye=13) are 0.
// ---------------------------------------------------------------------------
static constexpr int kSkullIdCount = 16;
static constexpr int kSkullIdToLocationId[kSkullIdCount] = {
    0,       // 0 invalid
    120001,  // 1 Iron           -> Pillar of Autumn - Iron Skull
    120007,  // 2 Fog            -> Assault on the Control Room - Fog Skull
    120002,  // 3 Mythic         -> Halo (CE) - Mythic Skull
    120005,  // 4 Famine         -> Silent Cartographer - Famine Skull
    0,       // 5 Engineer (unused)
    120004,  // 6 Foreign        -> Truth and Reconciliation - Foreign Skull
    120011,  // 7 Eye Patch      -> Library - Eye Patch Skull
    120009,  // 8 Recession      -> 343 Guilty Spark - Recession Skull
    120008,  // 9 Malfunction    -> Assault on the Control Room - Malfunction Skull
    120010,  // 10 Black Eye     -> Library - Black Eye Skull
    120013,  // 11 Grunt BP      -> Maw - Grunt Birthday Party Skull
    120012,  // 12 Pinata        -> Two Betrayals - Pinata Skull
    0,       // 13 Deadeye (unused)
    120006,  // 14 Bandana       -> Silent Cartographer - Bandana Skull
    120003,  // 15 Boom          -> Halo (CE) - Boom Skull
};

// ---------------------------------------------------------------------------
// Skull bitmask pointer chain (from skulls.py):
//   MCC-Win64-Shipping.exe + 0x4004230 -> [+0x8] -> [+0xB8] -> [+0x20]
//   -> value at [+0x708] is the uint64_t skull bitmask
// ---------------------------------------------------------------------------
static uint64_t* ResolveSkullBitmask() {
    HMODULE exe = GetModuleHandleA("MCC-Win64-Shipping.exe");
    if (!exe) return nullptr;

    uintptr_t p = *reinterpret_cast<uintptr_t*>(
        reinterpret_cast<uintptr_t>(exe) + 0x4004230);
    if (!p) return nullptr;
    p = *reinterpret_cast<uintptr_t*>(p + 0x8);
    if (!p) return nullptr;
    p = *reinterpret_cast<uintptr_t*>(p + 0xB8);
    if (!p) return nullptr;
    p = *reinterpret_cast<uintptr_t*>(p + 0x20);
    if (!p) return nullptr;
    return reinterpret_cast<uint64_t*>(p + 0x708);
}

namespace haloap {
namespace {

    // RVA of OnSkullClaimed in halo1.dll, confirmed across two ASLR sessions
    // (retail MCC 2025). A twin function exists at RVA 0x60820 with an identical
    // prologue; using the RVA directly avoids ambiguous pattern matches.
    constexpr size_t kOnSkullClaimedRVA = 0x63640;

    // The ssl dispatcher passes 5 arguments. r8 and r9 are saved early in the
    // function body (mov r15,r8 / mov r14,r9) and a 5th is read from [rsp+0x130]
    // as a refcounted context pointer. All five must be forwarded to avoid crashes.
    typedef void(*OnSkullClaimed_t)(void* a, int skull_id, void* c, void* d, void* e);

    OnSkullClaimed_t g_origOnSkullClaimed   = nullptr;
    void*            g_hookTarget           = nullptr;
    PipeClient*      g_pipe                 = nullptr;

    std::mutex       g_skullMutex;
    uint64_t         g_forcedMask  = 0;  // skulls forced on by skullsanity tier
    uint64_t         g_disabledMask = 0; // skulls cleared by received disabler items

    void DetourOnSkullClaimed(void* a, int skull_id, void* c, void* d, void* e) {
        if (skull_id >= 1 && skull_id < kSkullIdCount) {
            int locationId = kSkullIdToLocationId[skull_id];
            if (locationId != 0 && g_pipe && g_pipe->IsConnected()) {
                std::string msg = "LOCATION_CHECKED: " + std::to_string(locationId);
                g_pipe->SendAsync(msg);
                printf("[skull] skull_id=%d -> location %d\n", skull_id, locationId);
            } else if (locationId == 0) {
                printf("[skull] skull_id=%d (unused skull, no location)\n", skull_id);
            }
        }
        if (g_origOnSkullClaimed) g_origOnSkullClaimed(a, skull_id, c, d, e);
    }

}  // namespace

bool InstallSkullHook(PipeClient* pipe) {
    g_pipe = pipe;

    HMODULE halo1 = GetModuleHandleA("halo1.dll");
    if (!halo1) {
        printf("[skull] halo1.dll not loaded\n");
        return false;
    }

    void* target = reinterpret_cast<uint8_t*>(halo1) + kOnSkullClaimedRVA;
    g_hookTarget = target;

    MH_STATUS s = MH_CreateHook(target,
        reinterpret_cast<void*>(DetourOnSkullClaimed),
        reinterpret_cast<void**>(&g_origOnSkullClaimed));
    if (s != MH_OK) {
        printf("[skull] MH_CreateHook failed: %d\n", s);
        return false;
    }

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        printf("[skull] MH_EnableHook failed: %d\n", s);
        return false;
    }

    printf("[skull] OnSkullClaimed hook installed at %p (RVA 0x%zx)\n",
        target, kOnSkullClaimedRVA);
    return true;
}

void UninstallSkullHook() {
    if (g_hookTarget) {
        MH_DisableHook(g_hookTarget);
        MH_RemoveHook(g_hookTarget);
        g_hookTarget = nullptr;
    }
    g_origOnSkullClaimed = nullptr;
    g_pipe = nullptr;
}

void SetSkullsanityTier(int tier) {
    std::lock_guard<std::mutex> lock(g_skullMutex);
    switch (tier) {
    case 1: g_forcedMask = kForcedNonScoring; break;
    case 2:
    case 3:
    case 4: g_forcedMask = kForcedAll;        break;
    default: g_forcedMask = 0;                break;
    }
    printf("[skull] skullsanity tier %d -> forced mask 0x%llx\n",
        tier, static_cast<unsigned long long>(g_forcedMask));
}

void DisableSkull(int disablerIdx) {
    if (disablerIdx < 0 || disablerIdx >= kDisablerCount) return;
    uint64_t bit = kDisablerBits[disablerIdx];

    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        g_disabledMask |= bit;
    }

    uint64_t* bitmask = ResolveSkullBitmask();
    if (bitmask) {
        *bitmask &= ~bit;
        printf("[skull] disabled skull idx %d (bit 0x%llx), bitmask now 0x%llx\n",
            disablerIdx,
            static_cast<unsigned long long>(bit),
            static_cast<unsigned long long>(*bitmask));
    }
}

void ApplyForcedSkulls() {
    uint64_t forced;
    uint64_t disabled;
    {
        std::lock_guard<std::mutex> lock(g_skullMutex);
        forced   = g_forcedMask;
        disabled = g_disabledMask;
    }

    if (forced == 0) return;

    uint64_t* bitmask = ResolveSkullBitmask();
    if (!bitmask) return;

    // Set bits for forced skulls that have not been disabled by a received item.
    *bitmask |= (forced & ~disabled);
}

}  // namespace haloap
