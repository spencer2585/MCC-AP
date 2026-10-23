#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include "ClientConnection.h"

#include <string>
#include <ws2tcpip.h>
#include "logging/log.h"

using haloap::Log;

static constexpr int port = 35000;

bool ClientConnection::connectTCP()
{
    client = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        Log("Cannot create socket");
        return false;
    }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1)
    {
        Log("Error setting Socket Address");
        closesocket(client);
        return false;
    }
    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        Log("Error connecting to socket");
        closesocket(client);
        return false;
    }
    return true;
}