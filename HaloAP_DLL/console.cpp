#include "console.h"

void SetupConsole()
{
    AllocConsole();
    SetConsoleTitleW(L"HaloAP Dll Console");

    freopen_s(&g_consoleOut, "CONOUT$", "w", stdout);
    freopen_s(&g_consoleErr, "CONOUT$", "w", stderr);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void TeardownConsole()
{
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
}