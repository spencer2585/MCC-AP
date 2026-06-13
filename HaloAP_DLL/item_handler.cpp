#include "item_handler.h"
#include "hooks/skull_hook.h"
#include <cstdio>

namespace haloap {

    namespace {
        // CE mission unlock item IDs:
        //   Pillar of Autumn = 101000
        //   Halo             = 102000
        //   Truth & Recon    = 103000
        //   Silent Carto     = 104000
        //   Assault on CR    = 105000
        //   343 Guilty Spark = 106000
        //   The Library      = 107000
        //   Two Betrayals    = 108000
        //   Keyes            = 109000
        //   The Maw          = 110000
        constexpr int CE_OFFSET       = 100000;
        constexpr int CE_SKULL_OFFSET = 120000;
        constexpr int MISSION_COUNT   = 10;
        constexpr int SKULL_DISABLER_COUNT = 21;
    }

    void ItemHandler::addItem(int itemID) {
        int missionId = translateItemToMission(itemID);
        if (missionId >= 0) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_unlockedMissions.insert(missionId).second) {
                printf("[items] Mission %d unlocked (AP item %d)\n", missionId, itemID);
            }
            return;
        }

        int disablerIdx = translateItemToSkullDisabler(itemID);
        if (disablerIdx >= 0) {
            printf("[items] Skull disabler idx %d received (AP item %d)\n", disablerIdx, itemID);
            haloap::DisableSkull(disablerIdx);
            return;
        }

        printf("[items] Unknown item received: %d\n", itemID);
    }

    bool ItemHandler::isMissionAllowed(int missionId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_unlockedMissions.count(missionId) > 0;
    }

    int ItemHandler::getUnlockedCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_unlockedMissions.size());
    }

    ItemHandler& GetItemHandler() {
        static ItemHandler instance;
        return instance;
    }
    
    int ItemHandler::translateItemToMission(int itemID) const {
        int offset = itemID - CE_OFFSET;
        if (offset < 1000 || offset > 10000) return -1;
        if (offset % 1000 != 0) return -1;
        return (offset / 1000) - 1;
    }

    int ItemHandler::translateItemToSkullDisabler(int itemID) const {
        int idx = itemID - CE_SKULL_OFFSET - 1;
        if (idx < 0 || idx >= SKULL_DISABLER_COUNT) return -1;
        return idx;
    }

}  // namespace haloap