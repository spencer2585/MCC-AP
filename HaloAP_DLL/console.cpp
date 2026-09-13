#include "console.h"
#include <cstdio>
#include <windows.h>

namespace{
    #ifdef HALOAP_ENABLE_CONSOLE
    FILE* g_consoleOut = nullptr;
    FILE* g_consoleErr = nullptr;
    #endif
}

namespace haloap
{
    void SetupConsole()
    {
        #ifdef HALOAP_ENABLE_CONSOLE
        AllocConsole();
        SetConsoleTitleW(L"HaloAP Dll Console");

        freopen_s(&g_consoleOut, "CONOUT$", "w", stdout);
        freopen_s(&g_consoleErr, "CONOUT$", "w", stderr);

        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        #endif
    }

    void TeardownConsole()
    {
        #ifdef HALOAP_ENABLE_CONSOLE
        if (g_consoleOut)
        {
            fclose(g_consoleOut);
            g_consoleOut = nullptr;
        }
        if (g_consoleErr)
        {
            fclose(g_consoleErr);
            g_consoleErr = nullptr;
        }
        FreeConsole();
        #endif
    }
}