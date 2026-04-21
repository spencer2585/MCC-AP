#pragma once

#include <windows.h>
#include <atomic>
#include <thread>
#include <string>

class PipeServer {
public:
    PipeServer();
    ~PipeServer();

    bool Start();
    void Stop();
    bool Send(const std::string& message);
    bool IsConnected() const { return m_connected.load(); }

private:
    void ServerThreadMain();
    void HandleMessage(const std::string& message);

    // Reads exactly `size` bytes into `buffer`, using overlapped I/O.
    // Returns true on success, false on shutdown or error.
    bool ReadExact(void* buffer, DWORD size);

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_shutdownEvent = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_shutdown{ false };
    std::atomic<bool> m_connected{ false };
};