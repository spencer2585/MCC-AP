#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace haloap {

    // Mission code (from the DLL's hook) -> AP location/item ID base.
    // These offsets match the APWorld's definitions:
    // - Used as the location ID for "Mission Complete: <mission>"
    // - Used as the item ID for "Mission Unlock: <mission>"
    inline const std::unordered_map<std::string, int64_t>& GetMissionIdMap() {
        static const std::unordered_map<std::string, int64_t> map = {
            {"a10", 101000},  // Pillar of Autumn
            {"a30", 102000},  // Halo
            {"a50", 103000},  // Truth and Reconciliation
            {"b30", 104000},  // The Silent Cartographer
            {"b40", 105000},  // Assault on the Control Room
            {"c10", 106000},  // 343 Guilty Spark
            {"c20", 107000},  // The Library
            {"c40", 108000},  // Two Betrayals
            {"d20", 109000},  // Keyes
            {"d40", 110000},  // The Maw
        };
        return map;
    }

    // For logging only — reverse mapping from ID to human name.
    inline std::string GetMissionDisplayName(int64_t id) {
        static const std::unordered_map<int64_t, std::string> names = {
            {101000, "Pillar of Autumn"},
            {102000, "Halo"},
            {103000, "Truth and Reconciliation"},
            {104000, "The Silent Cartographer"},
            {105000, "Assault on the Control Room"},
            {106000, "343 Guilty Spark"},
            {107000, "The Library"},
            {108000, "Two Betrayals"},
            {109000, "Keyes"},
            {110000, "The Maw"},
        };
        auto it = names.find(id);
        return it != names.end() ? it->second : ("id:" + std::to_string(id));
    }

}  // namespace haloap