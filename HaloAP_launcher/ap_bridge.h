#pragma once

#include <apclient.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <set>
#include <cstdint>

namespace haloap {

    class APBridge {
    public:
        APBridge();
        ~APBridge();

        // Start connecting to an AP server. Non-blocking; poll() to make progress.
        // Returns false only if the client couldn't be constructed (bad params, etc.).
        bool Start(const std::string& serverUri,
            const std::string& game,
            const std::string& slot,
            const std::string& password);

        // Must be called regularly (e.g. every 50ms) to service the socket.
        void Poll();

        // Shut down. Safe to call multiple times.
        void Stop();

        // Handle a message from the DLL (forwarded from the pipe).
        // Returns true if the message was recognized and handled.
        bool HandleDllMessage(const std::string& message);

        // Convenience: send a location check. Deduplicates within this session.
        void SendLocation(int64_t locationId);

        bool IsConnected() const { return m_slotConnected.load(); }
        bool IsSocketConnected() const { return m_socketConnected.load(); }
        
        void SetSendToDll(std::function<void(const std::string&)> fn) {
            m_sendToDll = std::move(fn);
        }
        
        void ReplayBufferedItems();

    private:
        // APClient callbacks. These run on the poll thread.
        void OnSocketConnected();
        void OnSocketError(const std::string& error);
        void OnSocketDisconnected();
        void OnRoomInfo();
        void OnSlotConnected(const nlohmann::json& slotData);
        void OnSlotRefused(const std::list<std::string>& reasons);
        void OnItemsReceived(const std::list<APClient::NetworkItem>& items);
        void OnPrintJson(const APClient::PrintJSONArgs& args);
        void SendCompletionState();
        
        std::function<void(const std::string&)> m_sendToDll;

        std::unique_ptr<APClient> m_client;
        std::string m_game;
        std::string m_slot;
        std::string m_password;
        
        std::mutex m_itemBufferMutex;
        std::vector<int64_t> m_itemBuffer;

        std::atomic<bool> m_socketConnected{ false };
        std::atomic<bool> m_slotConnected{ false };
        std::atomic<bool> m_stopped{ false };

        // Locations we've sent this session, for deduplication.
        std::mutex m_sentLocationsMutex;
        std::set<int64_t> m_sentLocations;
        
        std::set<int64_t> m_checkedLocations;
        std::mutex m_checkedMutex;
    };

}  // namespace haloap