#pragma once
#include <winsock2.h>

class ClientConnection
{
public:
    bool connectTCP();
private:
    SOCKET client = INVALID_SOCKET;
};
