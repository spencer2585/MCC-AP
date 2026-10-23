#include "worker_main.h"
#include "ConsoleGuard.h"
#include "logging/log.h"
#include "shutdown.h"
#include "ClientConnection.h"
#include "MinHookGuard.h"

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
    
    //next is hooking up the Pipe. pipe refactor is a large refactor that requires redoing the whole item input and location output messageing system
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2,2),&wsadata) !=0)
    {
        Log("Error starting up WSAStartup. Aborting.");
        return 1;
    }
    ClientConnection client;
    if (!client.connectTCP())
    {
        Log("Error connecting to server. Aborting.");
        return 1;
    }
    Log("Connected to client.");
    while (!g_shutdown.load())
    {
        Sleep(1000);
    }
    
    return 0;
}
