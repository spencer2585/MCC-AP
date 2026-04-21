#include "pipe_server.h"
#include "shared/common.h"
#include <iostream>
#include <vector>
#include <cstdint>

PipeServer::PipeServer() = default;

PipeServer::~PipeServer() {
    Stop();
}

bool PipeServer::Start() {
    // Create a manual-reset event for signaling shutdown.
    m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_shutdownEvent) {
        std::cerr << "[pipe] CreateEvent failed: " << GetLastError() << "\n";
        return false;
    }

    // Create the named pipe with FILE_FLAG_OVERLAPPED so all I/O is async.
    m_pipe = CreateNamedPipeW(
        haloap::kPipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        haloap::kMaxMessageSize,
        haloap::kMaxMessageSize,
        0,
        nullptr
    );

    if (m_pipe == INVALID_HANDLE_VALUE) {
        std::cerr << "[pipe] CreateNamedPipe failed: " << GetLastError() << "\n";
        CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
        return false;
    }

    m_thread = std::thread(&PipeServer::ServerThreadMain, this);
    return true;
}

void PipeServer::Stop() {
    if (m_shutdown.exchange(true)) {
        return;
    }
    m_connected.store(false);

    // Signal the shutdown event to wake any blocked WaitForMultipleObjects.
    if (m_shutdownEvent) {
        SetEvent(m_shutdownEvent);
    }

    if (m_thread.joinable()) {
        m_thread.join();
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

bool PipeServer::Send(const std::string& message) {
    if (!m_connected.load() || m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint32_t length = static_cast<uint32_t>(message.size());

    // Helper: write exactly `size` bytes from `data`, using overlapped I/O.
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

        // Wait for either completion or shutdown.
        HANDLE waits[] = { ov.hEvent, m_shutdownEvent };
        DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0 + 1) {
            // Shutdown. Cancel the I/O before closing the event.
            CancelIoEx(m_pipe, &ov);
            WaitForSingleObject(ov.hEvent, INFINITE);  // wait for cancel to finish
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

bool PipeServer::ReadExact(void* buffer, DWORD size) {
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

void PipeServer::ServerThreadMain() {
    std::cout << "[pipe] waiting for DLL to connect...\n";

    // Overlapped ConnectNamedPipe.
    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED ov = {};
    ov.hEvent = connectEvent;

    BOOL connected = ConnectNamedPipe(m_pipe, &ov);
    DWORD err = GetLastError();

    // ConnectNamedPipe in overlapped mode returns FALSE and sets one of:
    //   ERROR_IO_PENDING    - waiting for connection (normal)
    //   ERROR_PIPE_CONNECTED - client already connected (also success)
    if (!connected && err != ERROR_IO_PENDING && err != ERROR_PIPE_CONNECTED) {
        std::cerr << "[pipe] ConnectNamedPipe failed: " << err << "\n";
        CloseHandle(connectEvent);
        return;
    }

    if (err == ERROR_IO_PENDING) {
        // Wait for connection or shutdown.
        HANDLE waits[] = { connectEvent, m_shutdownEvent };
        DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0 + 1) {
            std::cout << "[pipe] shutdown during connect\n";
            CancelIoEx(m_pipe, &ov);
            WaitForSingleObject(ov.hEvent, INFINITE);
            CloseHandle(connectEvent);
            return;
        }
    }

    CloseHandle(connectEvent);
    m_connected.store(true);
    std::cout << "[pipe] DLL connected\n";

    // Read loop: length prefix, then body.
    while (!m_shutdown.load()) {
        uint32_t length = 0;
        if (!ReadExact(&length, sizeof(length))) {
            if (!m_shutdown.load()) {
                std::cerr << "[pipe] length read failed\n";
            }
            break;
        }

        if (length == 0 || length > haloap::kMaxMessageSize) {
            std::cerr << "[pipe] bad message length: " << length << "\n";
            break;
        }

        std::vector<char> body(length);
        if (!ReadExact(body.data(), length)) {
            if (!m_shutdown.load()) {
                std::cerr << "[pipe] body read failed\n";
            }
            break;
        }

        std::string message(body.data(), length);
        HandleMessage(message);
    }

    m_connected.store(false);
    std::cout << "[pipe] disconnected\n";
}

void PipeServer::HandleMessage(const std::string& message) {
    std::cout << "[pipe <- dll] " << message << "\n";
}