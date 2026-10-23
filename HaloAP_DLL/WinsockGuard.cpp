#include "WinsockGuard.h"
#include <winsock2.h>
#include "logging/log.h"

using haloap::Log;

WinsockGuard::WinsockGuard()
{
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2,2),&wsadata) !=0)
    {
        Log("Error starting Winsock");
        initialized = false;
        return;
    }
    initialized = true;
    Log("Initialized Winsock.");
}

WinsockGuard::~WinsockGuard()
{
    if (initialized)
    {
        Log("Uninitialized Winsock.");
        WSACleanup();
    }
}

bool WinsockGuard::ok() const
{
    return initialized;
}