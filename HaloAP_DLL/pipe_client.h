#pragma once

#include <windows.h>
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <condition_variable>
#include <deque>

class PipeClient {
public:
    PipeClient();
    ~PipeClient();

    bool Connect();
    void Stop();
    bool Send(const std::string& message);
    bool IsConnected() const { return m_connected.load(); }
    void SendAsync(const std::string& message);

private:
    void ReaderThreadMain();
    void HandleMessage(const std::string& message);
    bool ReadExact(void* buffer, DWORD size);
    void SenderThreadMain();

    std::thread m_senderThread;
    std::mutex m_sendQueueMutex;
    std::condition_variable m_sendQueueCv;
    std::deque<std::string> m_sendQueue;

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_shutdownEvent = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_shutdown{ false };
    std::atomic<bool> m_connected{ false };
};