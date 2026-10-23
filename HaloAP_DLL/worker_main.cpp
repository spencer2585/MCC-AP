#include "worker_main.h"
#include "ConsoleGuard.h"
#include "logging/log.h"
#include "shutdown.h"
#include "ClientConnection.h"
#include "MinHookGuard.h"
#include "WinsockGuard.h"
#include "shared/common.h"

#include <cstdio>

using haloap::Log;

DWORD WINAPI WorkerMain(LPVOID /*param*/)
{
    ConsoleGuard console;

    //create a logging macro
    Log("==========================================");
    Log("  HaloAP DLL v{}", haloap::kVersion);
    Log("  Running inside MCC (PID {})", GetCurrentProcessId());
    Log("==========================================");

    //Initialize minhooks
    MinHookGuard minhook;
    if (!minhook.ok())
    {
        MessageBoxA(nullptr, "Failed to initilize MinHooks. Aborting", "Error initilizing minhooks", MB_OK | MB_ICONERROR);
        return 1;
    }
    //initialize winsock
    WinsockGuard winsock;
    if (!winsock.ok())
    {
        Log("Failed to initilize Winsock. Aborting");
        return 1;
    }
    
    //connect to client
    ClientConnection client;
    if (!client.connectTCP())
    {
        Log("Error connecting to server. Aborting.");
        return 1;
    }
    Log("Connected to client.");
    //loop until game shuts down
    while (!g_shutdown.load())
    {
        Sleep(1000);
    }
    
    return 0;
}
