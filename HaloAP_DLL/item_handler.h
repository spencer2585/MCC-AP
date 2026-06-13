#pragma once

#include <set>
#include <mutex>
#include <cstdint>

namespace haloap {

    class ItemHandler {
    public:
        // Call when an item is received from the AP server.
        // Handles mission unlock items and skull disabler items.
        // itemID is the AP item ID — translated internally to mission or skull disabler index.
        void addItem(int itemID);

        // Check if a mission (by mission index 0-9) is unlocked/allowed.
        bool isMissionAllowed(int missionId) const;

        // Get the number of currently unlocked missions.
        int getUnlockedCount() const;

    private:
        mutable std::mutex m_mutex;
        std::set<int> m_unlockedMissions;

        // Translate AP item ID to mission index (0-9).
        // Returns -1 if the item is not a mission unlock.
        int translateItemToMission(int itemID) const;

        // Translate AP item ID to skull disabler index (0-20, matching
        // CE_SKULL_DISABLERS in items.py). Returns -1 if not a skull disabler.
        int translateItemToSkullDisabler(int itemID) const;
    };

    // Global singleton instance.
    ItemHandler& GetItemHandler();

}  // namespace haloap