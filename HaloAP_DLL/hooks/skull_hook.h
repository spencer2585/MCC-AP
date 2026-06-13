#pragma once
#include "../pipe_client.h"

namespace haloap {

    // Hook OnSkullClaimed in halo1.dll (RVA 0x63640). Fires when the player
    // walks into a skull trigger zone. Sends "LOCATION_CHECKED: <id>" to the AP
    // server via the pipe for each valid skull pickup.
    bool InstallSkullHook(PipeClient* pipe);
    void UninstallSkullHook();

    // Apply the current forced-skull set to MCC's skull bitmask in game memory.
    // Called each worker tick to keep forced skulls active across level loads.
    void ApplyForcedSkulls();

    // Set which skulls are forced on based on the skullsanity tier from AP slot
    // data. Tiers: 0=off, 1=non_scoring, 2=hard, 3=harder, 4=laso.
    void SetSkullsanityTier(int tier);

    // Disable a skull disabler item by its 0-based index in CE_SKULL_DISABLERS
    // (items.py). Called from item_handler when a Disable Skull item is received.
    void DisableSkull(int disablerIdx);

}  // namespace haloap
