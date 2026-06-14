#include "item_handler.h"
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
        constexpr int CE_OFFSET = 100000;
        constexpr int MISSION_COUNT = 10;
    }
    
    bool m_missionCompleted[9] = {};

    void ItemHandler::addItem(int itemID) {
        int missionId = translateItemToMission(itemID);
        if (missionId >= 0) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_unlockedMissions.insert(missionId).second) {
                printf("[items] Mission %d unlocked (AP item %d)\n", missionId, itemID);
            }
        } else {
            printf("[items] Non-mission item received: %d\n", itemID);
        }
    }

    void ItemHandler::setMissionCompletions(const bool completed[9]) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int i = 0; i < 9; i++)
            m_missionCompleted[i] = completed[i];
    }
    
    bool ItemHandler::isMissionAllowed(int missionId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (missionId < 0 || missionId >=10)
            return true;
    
        // The Maw (mission 9): unlocked when all 9 others are completed
        if (missionId == 9) {
            for (int i = 0; i < 9; i++)
                if (!m_missionCompleted[i]) return false;
            return true;
        }
    
        // All other missions: unlocked via AP items
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
    
    

}  // namespace haloap