#include "worker_main.h"
#include "console.h"
#include "shutdown.h"

#include "shared/common.h"

#include <cstdio>


DWORD WINAPI WorkerMain(LPVOID /*param*/)
{
    haloap::SetupConsole();

    //create a logging macro
    printf("==========================================\n");
    printf("  HaloAP DLL v%s\n", haloap::kVersion);
    printf("  Running inside MCC (PID %lu)\n", GetCurrentProcessId());
    printf("==========================================\n");

        
    haloap::TeardownConsole();
    return 0;
}

void UninstallAllHooks()
{
    haloap::UninstallMissionSelectBlockHook();
    UninstallVtableHooks();
    UninstallHalo1Hooks();
}

PipeClient* g_pipe = nullptr;

void reviewWorker()
{
    MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus != MH_OK)
        {
            printf("MH_Initialize failed: %d\n", mhStatus);
        }
        else
        {
            printf("MinHook initialized.\n");
        }

        Sleep(5000);

        g_pipe = new PipeClient();
        if (!g_pipe->Connect())
        {
            printf("Failed to connect to injector pipe. Continuing without it.\n");
        }
        else
        {
            printf("Connected to injector.\n");
            printf("Sending HELLO...\n");
            bool sendOk = g_pipe->Send("HELLO: dll speaking, v" + std::string(haloap::kVersion));
            printf("HELLO send returned %s\n", sendOk ? "true" : "false");
        }

        haloap::InstallMissionSelectBlockHook(g_pipe);
        haloap::InitGameModeButtonCollapse();

        for (int i = 0; i < 50; i++)
        {
            if (GetModuleHandleA("halo1.dll")) break;
            Sleep(100);
        }

        HMODULE lastHalo1 = GetModuleHandleA("halo1.dll");
        if (lastHalo1)
        {
            // Temporary: find cinematic_set_title string
            //MODULEINFO mi = {};
            //GetModuleInformation(GetCurrentProcess(), lastHalo1, &mi, sizeof(mi));
            //uint8_t* base = (uint8_t*)lastHalo1;
            //size_t size = mi.SizeOfImage;
            //const char* target = "cinematic_set_title";
            //size_t targetLen = strlen(target);
            //for (size_t i = 0; i < size - targetLen; i++) {
            //	if (memcmp(base + i, target, targetLen) == 0 && base[i + targetLen] == 0) {
            //		printf("[search] Found '%s' at halo1.dll+0x%zX\n", target, i);
            //	}
            //}
            InstallHalo1Hooks();
        }

        // Wait for UE4 menu system to fully initialize, then cache button data
        //Sleep(500);
        haloap::InitGameModeButtonCollapse();


        void* lastEngineObj = nullptr;
        bool vtableHooksInstalled = false;
        bool ue4DumpDone = false;

        int tick = 0;
        while (!g_shutdown.load())
        {
            HMODULE currentHalo1 = GetModuleHandleA("halo1.dll");

            if (currentHalo1 != lastHalo1)
            {
                if (currentHalo1)
                {
                    printf("[monitor] halo1.dll reloaded at %p (was %p). Reinstalling hooks...\n",
                           currentHalo1, lastHalo1);
                    UninstallHalo1Hooks();
                    UninstallVtableHooks();
                    InstallHalo1Hooks();
                }
                else
                {
                    printf("[monitor] halo1.dll unloaded.\n");
                    UninstallHalo1Hooks();
                    UninstallVtableHooks();
                    haloap::SetInMission(false);
                }
                lastHalo1 = currentHalo1;
                lastEngineObj = nullptr;
                vtableHooksInstalled = false;
            }

            if (currentHalo1 && !vtableHooksInstalled)
            {
                if (haloap::InstallShellLevelLoadHook(g_pipe))
                {
                    haloap::InstallShellCommandHook(g_pipe);
                    vtableHooksInstalled = true;
                    lastEngineObj = haloap::GetEngineObject();
                }
            }
            if (vtableHooksInstalled)
            {
                void* currentEngineObj = haloap::GetEngineObject();
                if (currentEngineObj != lastEngineObj)
                {
                    printf("[monitor] Engine object changed: %p -> %p. Reinstalling ALL hooks...\n",
                           lastEngineObj, currentEngineObj);
                    UninstallVtableHooks();
                    UninstallHalo1Hooks();
                    vtableHooksInstalled = false;
                    InstallHalo1Hooks();
                    lastEngineObj = currentEngineObj;
                    
                    if (haloap::InstallShellLevelLoadHook(g_pipe))
                    {
                        haloap::InstallShellCommandHook(g_pipe);
                        vtableHooksInstalled = true;
                        lastEngineObj = haloap::GetEngineObject();
                    }
                    
                    // If immediate install failed, retry quickly
                    if (!vtableHooksInstalled && currentHalo1)
                    {
                        for (int retry = 0; retry < 10; retry++)
                        {
                            Sleep(500);
                            if (haloap::InstallShellLevelLoadHook(g_pipe))
                            {
                                haloap::InstallShellCommandHook(g_pipe);
                                vtableHooksInstalled = true;
                                lastEngineObj = haloap::GetEngineObject();
                                break;
                            }
                        }
                    }
                }
            }

            // --- UE4 diagnostic: run once after tick 3 ---
            //if (!ue4DumpDone && tick == 10) {
            //	ue4DumpDone = true;
            //	printf("\n[UE4] === Running GUObjectArray search ===\n");
            //	DumpUObjectSearch();
            //	printf("[UE4] === Search done ===\n\n");
            //}

            //printf("[heartbeat %d] still here\n", tick);

            //if (g_pipe && g_pipe->IsConnected()) {
            //	std::string msg = "HEARTBEAT: tick " + std::to_string(tick);
            //	g_pipe->Send(msg);
            //}

            haloap::ApplyForcedSkulls();

            tick++;

            for (int i = 0; i < 20 && !g_shutdown.load(); ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        printf("Worker shutting down.\n");

        if (g_pipe)
        {
            g_pipe->Stop();
            delete g_pipe;
            g_pipe = nullptr;}

    UninstallAllHooks();
    MH_Uninitialize();
}