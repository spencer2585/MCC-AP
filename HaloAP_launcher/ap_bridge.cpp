#include "ap_bridge.h"
#include "mission_map.h"
#include "shared/common.h"
#include <iostream>
#include <vector>
#include <set>
#include <sstream>


namespace haloap {
    
    static const int64_t COMPLETION_LOCATIONS[] = {
        101000, 102000, 103000, 104000, 105000,
        106000, 107000, 108000, 109000
    };
    
    void APBridge::SendCompletionState() {
        if (!m_sendToDll) return;
        std::lock_guard<std::mutex> lock(m_checkedMutex);
    
        std::string msg = "COMPLETED:";
        bool first = true;
        for (int i = 0; i < 9; i++) {
            if (m_checkedLocations.count(COMPLETION_LOCATIONS[i])) {
                if (!first) msg += ",";
                msg += std::to_string(i);
                first = false;
            }
        }
        m_sendToDll(msg);
        std::cout << "[ap] Sent completion state: " << msg << "\n";
    }

    APBridge::APBridge() = default;

    APBridge::~APBridge() {
        Stop();
    }
    
    void APBridge::ReplayBufferedItems() {
        if (!m_sendToDll) return;
        std::lock_guard<std::mutex> lock(m_itemBufferMutex);
        std::cout << "[ap] replaying " << m_itemBuffer.size() << " buffered items to DLL\n";
        for (int64_t itemId : m_itemBuffer) {
            m_sendToDll("ITEM_RECIVED: " + std::to_string(itemId));
        }
        // Also send current completion state
        SendCompletionState();
    }

    bool APBridge::Start(const std::string& serverUri,
        const std::string& game,
        const std::string& slot,
        const std::string& password) {
        m_game = game;
        m_slot = slot;
        m_password = password;

        try {
            // Construct APClient. The UUID should be stable across runs for a user
            // so the server recognizes them, but for alpha we generate one per run.
            std::string uuid = "";
            m_client = std::make_unique<APClient>(uuid, game, serverUri);
        }
        catch (const std::exception& e) {
            std::cerr << "[ap] APClient construction failed: " << e.what() << "\n";
            return false;
        }

        // Register callbacks. Captures `this`; lifetime is fine because the client
        // is owned by us and callbacks only fire during Poll().
        m_client->set_socket_connected_handler([this]() { OnSocketConnected(); });
        m_client->set_socket_error_handler([this](const std::string& err) { OnSocketError(err); });
        m_client->set_socket_disconnected_handler([this]() { OnSocketDisconnected(); });
        m_client->set_room_info_handler([this]() { OnRoomInfo(); });
        m_client->set_slot_connected_handler([this](const nlohmann::json& data) { OnSlotConnected(data); });
        m_client->set_slot_refused_handler([this](const std::list<std::string>& reasons) { OnSlotRefused(reasons); });
        m_client->set_items_received_handler([this](const std::list<APClient::NetworkItem>& items) { OnItemsReceived(items); });
        m_client->set_print_json_handler([this](const APClient::PrintJSONArgs& args) { OnPrintJson(args); });

        std::cout << "[ap] connecting to " << serverUri << " as slot '" << slot << "'...\n";
        return true;
    }

    void APBridge::Poll() {
        if (m_stopped.load()) return;
        if (m_client) {
            m_client->poll();
        }
    }

    void APBridge::Stop() {
        if (m_stopped.exchange(true)) return;
        m_client.reset();
        m_socketConnected.store(false);
        m_slotConnected.store(false);
    }

    bool APBridge::HandleDllMessage(const std::string& message) {
        const std::string prefix = "MISSION_COMPLETE: ";
        if (message.rfind(prefix, 0) != 0) {
            return false;  // not our message
        }

        std::string missionCode = message.substr(prefix.size());
        const auto& map = GetMissionIdMap();
        auto it = map.find(missionCode);
        if (it == map.end()) {
            std::cerr << "[ap] unknown mission code: " << missionCode << "\n";
            return true;  // we recognized the message type, just couldn't map it
        }

        int64_t locationId = it->second;
        std::cout << "[ap] MISSION_COMPLETE: " << missionCode
            << " -> location " << locationId
            << " (" << GetMissionDisplayName(locationId) << ")\n";

        SendLocation(locationId);
        
        if (missionCode == "d40")
        {
            m_client->StatusUpdate(APClient::ClientStatus::GOAL);
        }
        
        return true;
    }

    void APBridge::SendLocation(int64_t locationId) {
        if (!m_slotConnected.load()) {
            std::cerr << "[ap] can't send location, not connected to slot\n";
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_sentLocationsMutex);
            if (!m_sentLocations.insert(locationId).second) {
                std::cout << "[ap] location " << locationId << " already sent this session, skipping\n";
                return;
            }
        }

        std::list<int64_t> locs = { locationId };
        m_client->LocationChecks(locs);
        std::cout << "[ap] sent location check: " << locationId << "\n";
    
        // Track completion and notify DLL
        {
            std::lock_guard<std::mutex> lock(m_checkedMutex);
            m_checkedLocations.insert(locationId);
        }
        SendCompletionState();
    }

    // -------- callbacks ---------

    void APBridge::OnSocketConnected() {
        m_socketConnected.store(true);
        std::cout << "[ap] socket connected\n";
    }

    void APBridge::OnSocketError(const std::string& error) {
        std::cerr << "[ap] socket error: " << error << "\n";
    }

    void APBridge::OnSocketDisconnected() {
        m_socketConnected.store(false);
        m_slotConnected.store(false);
        std::cout << "[ap] socket disconnected (will auto-reconnect)\n";
    }

    void APBridge::OnRoomInfo() {
        std::cout << "[ap] room info received; attempting slot connect...\n";
        // ConnectSlot params: slot name, password, items-handling flags, tags, version
        // Items handling 0b111 = all items (local, starting inventory, world items).
        std::list<std::string> tags;  // no special tags for now
        m_client->ConnectSlot(m_slot, m_password, 0b111, tags, { 0, 5, 0 });
    }

    void APBridge::OnSlotConnected(const nlohmann::json& slotData) {
        m_slotConnected.store(true);
        std::cout << "[ap] *** slot connected ***\n";
        if (!slotData.empty()) {
            std::cout << "[ap] slot data: " << slotData.dump() << "\n";
        }
    
        // Load already-checked locations from server
        {
            std::lock_guard<std::mutex> lock(m_checkedMutex);
            auto checked = m_client->get_checked_locations();
            for (int64_t locId : checked) {
                m_checkedLocations.insert(locId);
            }
        }
        SendCompletionState();
    }

    void APBridge::OnSlotRefused(const std::list<std::string>& reasons) {
        std::cerr << "[ap] slot refused:\n";
        for (const auto& r : reasons) {
            std::cerr << "  - " << r << "\n";
        }
    }

    void APBridge::OnItemsReceived(const std::list<APClient::NetworkItem>& items) {
        for (const auto& item : items) {
            std::string itemName = m_client->get_item_name(item.item, m_game);
            std::cout << "[ap] received item: " << itemName
                << " (id " << item.item << ")\n";

            // Buffer the item
            {
                std::lock_guard<std::mutex> lock(m_itemBufferMutex);
                m_itemBuffer.push_back(item.item);
            }

            // Forward to DLL if connected
            if (m_sendToDll) {
                m_sendToDll("ITEM_RECIVED: " + std::to_string(item.item));
            }
        }
    }

    void APBridge::OnPrintJson(const APClient::PrintJSONArgs& args) {
        // Render the message as plain text.
        std::string text;
        for (const auto& part : args.data) {
            text += part.text;
        }
        std::cout << "[ap chat] " << text << "\n";
    }

}  // namespace haloap