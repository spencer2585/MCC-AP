#pragma once
#include <unordered_map>
#include <string>

namespace haloap {

    struct ChapterDef {
        int locationId;         // AP location ID
        const char* name;      // display name for logging
    };

    // Key: "missionCode:chapterIndex" e.g. "a10:1"
    inline std::string ChapterKey(const std::string& mission, int index) {
        return mission + ":" + std::to_string(index);
    }

    // Chapter location mapping
    inline const std::unordered_map<std::string, ChapterDef>& GetChapterMap() {
        static const std::unordered_map<std::string, ChapterDef> map = {
            // a10 - Pillar of Autumn
            { "a10:0", { 101011, "Reveillie" } },
            { "a10:2", { 101012, "AI Constructs and Cyborgs First!" } },

            // a30 - Halo
            { "a30:0", { 102011, "Flawless Cowboy" } },
            { "a30:1", { 102012, "Reunion Tour" } },
            
            //a50 - The Truth and Reconciliation
            { "a50:0", { 103011, "Truth and Reconciliation" } },
            { "a50:1", { 103012, "Into the Belly of the Beast" } },
            { "a50:2", { 103013, "Shut up and get Behind me...Sir" } },
            
            //b30 - The Silent Cartographer
            { "b30:0", { 104011, "The Silent Cartographer" } },
            { "b30:1", { 104012, "It's Quiet..." } },
            { "b30:2", { 104013, "Shafted" } },
            
            //b-40 - Assault on the Control Room
            { "b40:0", { 105011, "I Would Have Been Your Daddy..." } },
            { "b40:1", { 105012, "Rolling Thunder" } },
            { "b40:2", { 105013, "If I Had a Super Weapon..." } },
            
            //c10 - 343 Guilty Spark
            { "c10:0", { 106011, "Well Enough Alone" } },
            { "c10:1", { 106012, "The Flood" } },
            { "c10:2", { 106013, "343 Guilty Spark" } },
            
            //c20 - The Library
            { "c20:0", { 107011, "The Library" } },
            { "c20:1", { 107013, "But I Don't Want to Ride the Elevator!" } },
            { "c20:2", { 107014, "Fourth Floor: Tools, Guns, Keys to Super Weapons" } },
            { "c20:5", { 107012, "Wait, it Gets Worse" } },
            
            //c40 - Two Betrayals
            { "c40:0", { 108011, "The Gun Pointed at the Head of the Universe" } },
            { "c40:1", { 108012, "Breaking Stuff to Look Tough" } },
            { "c40:2", { 108013, "The Tunnels Below" } },
            { "c40:3", { 108014, "Final Run" } },
            
            //d20 - Keyes
            { "c40:0", { 109011, "Under New Management" } },
            { "c40:1", { 109012, "Upstairs, Downstairs" } },
            { "c40:2", { 109013, "The Captain" } },
            
            //d40 - The Maw
            {"d40:0", {110011, "...And the Horse You Rode In On"} },
            {"d40:1", {110012, "Light Fuse, Run Away"} },
            {"d40:3", {110013, "Warning: Hitchhikers May Be Escaping Convicts"} },
        };
        return map;
    }

}  // namespace haloap