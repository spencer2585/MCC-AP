#include <windows.h>
#include <atomic>

#include "shutdown.h"
#include "worker_main.h"

namespace
{
    HANDLE g_workerThread = nullptr;
}

std::atomic<bool> g_shutdown{false};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_workerThread = CreateThread(nullptr, 0, WorkerMain, nullptr, 0, nullptr);
        if (!g_workerThread)
        {
            return FALSE;
        }
        break;

    case DLL_PROCESS_DETACH:
        g_shutdown.store(true);
        if (g_workerThread)
        {
            WaitForSingleObject(g_workerThread, 2000);
            CloseHandle(g_workerThread);
            g_workerThread = nullptr;
        }
        break;
    }
    return TRUE;
}
