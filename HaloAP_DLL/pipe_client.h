#pragma once

#include <windows.h>
#include <atomic>
#include <thread>
#include <string>

class PipeClient {
public:
    PipeClient();
    ~PipeClient();

    bool Connect();
    void Stop();
    bool Send(const std::string& message);
    bool IsConnected() const { return m_connected.load(); }

private:
    void ReaderThreadMain();
    void HandleMessage(const std::string& message);
    bool ReadExact(void* buffer, DWORD size);

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_shutdownEvent = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_shutdown{ false };
    std::atomic<bool> m_connected{ false };
};