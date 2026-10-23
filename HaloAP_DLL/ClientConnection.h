#pragma once
#include <winsock2.h>

class ClientConnection
{
public:
    ClientConnection() = default;
    bool connectTCP();
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;
    ~ClientConnection();
private:
    SOCKET client = INVALID_SOCKET;
};
