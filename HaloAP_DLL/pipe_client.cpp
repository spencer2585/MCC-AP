#include "pipe_client.h"
#include "shared/common.h"
#include <cstdio>
#include <vector>
#include <cstdint>

PipeClient::PipeClient() = default;

PipeClient::~PipeClient() {
    Stop();
}

bool PipeClient::Connect() {
    m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_shutdownEvent) {
        printf("[pipe] CreateEvent failed: %lu\n", GetLastError());
        return false;
    }

    m_pipe = CreateFileW(
        haloap::kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,     // <-- async
        nullptr
    );

    if (m_pipe == INVALID_HANDLE_VALUE) {
        printf("[pipe] CreateFile failed: %lu\n", GetLastError());
        CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
        return false;
    }

    m_connected.store(true);
    m_thread = std::thread(&PipeClient::ReaderThreadMain, this);
    m_senderThread = std::thread(&PipeClient::SenderThreadMain, this);
    return true;
}

void PipeClient::Stop() {
    if (m_shutdown.exchange(true)) {
        return;
    }
    m_connected.store(false);

    if (m_shutdownEvent) {
        SetEvent(m_shutdownEvent);
    }
    m_sendQueueCv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_senderThread.joinable()) {
        m_senderThread.join();
    }

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    if (m_shutdownEvent) {
        CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
    }
}

bool PipeClient::Send(const std::string& message) {
    if (!m_connected.load() || m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint32_t length = static_cast<uint32_t>(message.size());

    auto writeExact = [this](const void* data, DWORD size) -> bool {
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ev) return false;

        OVERLAPPED ov = {};
        ov.hEvent = ev;

        DWORD written = 0;
        BOOL ok = WriteFile(m_pipe, data, size, &written, &ov);
        DWORD err = GetLastError();

        if (!ok && err != ERROR_IO_PENDING) {
            CloseHandle(ev);
            return false;
        }

        HANDLE waits[] = { ov.hEvent, m_shutdownEvent };
        DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0 + 1) {
            CancelIoEx(m_pipe, &ov);
            WaitForSingleObject(ov.hEvent, INFINITE);
            CloseHandle(ev);
            return false;
        }

        if (!GetOverlappedResult(m_pipe, &ov, &written, FALSE) || written != size) {
            CloseHandle(ev);
            return false;
        }

        CloseHandle(ev);
        return true;
        };

    if (!writeExact(&length, sizeof(length))) return false;
    if (!writeExact(message.data(), length)) return false;
    return true;
}

bool PipeClient::ReadExact(void* buffer, DWORD size) {
    char* p = static_cast<char*>(buffer);
    DWORD totalRead = 0;

    while (totalRead < size) {
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ev) return false;

        OVERLAPPED ov = {};
        ov.hEvent = ev;

        DWORD chunk = 0;
        BOOL ok = ReadFile(m_pipe, p + totalRead, size - totalRead, &chunk, &ov);
        DWORD err = GetLastError();

        if (!ok && err != ERROR_IO_PENDING) {
            CloseHandle(ev);
            return false;
        }

        HANDLE waits[] = { ov.hEvent, m_shutdownEvent };
        DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0 + 1) {
            CancelIoEx(m_pipe, &ov);
            WaitForSingleObject(ov.hEvent, INFINITE);
            CloseHandle(ev);
            return false;
        }

        if (!GetOverlappedResult(m_pipe, &ov, &chunk, FALSE) || chunk == 0) {
            CloseHandle(ev);
            return false;
        }

        totalRead += chunk;
        CloseHandle(ev);
    }

    return true;
}

void PipeClient::ReaderThreadMain() {
    while (!m_shutdown.load()) {
        uint32_t length = 0;
        if (!ReadExact(&length, sizeof(length))) {
            if (!m_shutdown.load()) {
                printf("[pipe] length read failed\n");
            }
            break;
        }

        if (length == 0 || length > haloap::kMaxMessageSize) {
            printf("[pipe] bad message length: %u\n", length);
            break;
        }

        std::vector<char> body(length);
        if (!ReadExact(body.data(), length)) {
            if (!m_shutdown.load()) {
                printf("[pipe] body read failed\n");
            }
            break;
        }

        std::string message(body.data(), length);
        HandleMessage(message);
    }

    m_connected.store(false);
}

void PipeClient::HandleMessage(const std::string& message) {
    printf("[pipe <- injector] %s\n", message.c_str());
}

void PipeClient::SendAsync(const std::string& message) {
    if (m_shutdown.load()) return;
    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        m_sendQueue.push_back(message);
    }
    m_sendQueueCv.notify_one();
}

void PipeClient::SenderThreadMain() {
    while (!m_shutdown.load()) {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(m_sendQueueMutex);
            m_sendQueueCv.wait(lock, [this]() {
                return !m_sendQueue.empty() || m_shutdown.load();
                });
            if (m_shutdown.load()) break;
            msg = std::move(m_sendQueue.front());
            m_sendQueue.pop_front();
        }

        // Send using the same overlapped I/O as before.
        // Reuse the existing Send() logic.
        Send(msg);
    }
}