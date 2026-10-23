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
    //if already connected close the connection
    if (client != INVALID_SOCKET)
    {
        closesocket(client);
        client = INVALID_SOCKET;
    }
    //create new socket
    client = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP);
    //if creating the socket fails
    if (client == INVALID_SOCKET)
    {
        Log("Cannot create socket");
        return false;
    }
    //setup socket connection
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    //if setting up the connection fails
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1)
    {
        Log("Error setting Socket Address");
        closesocket(client);
        client = INVALID_SOCKET;
        return false;
    }
    //connect and make sure it succeeds
    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        Log("Error connecting to socket");
        closesocket(client);
        client = INVALID_SOCKET;
        return false;
    }
    return true;
}

ClientConnection::~ClientConnection()
{
    //make sure the client is connected
    if (client != INVALID_SOCKET)
    {
        closesocket(client);
        client = INVALID_SOCKET;
        Log("Client disconnected");
    }
}