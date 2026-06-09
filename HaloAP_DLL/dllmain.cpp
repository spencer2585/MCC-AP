#include <windows.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include "pipe_client.h"
#include "shared/common.h"
#include "minhook/MinHook.h"
#include "hooks/mission_complete.h"
#include "hooks/mission_id_lookup.h"
#include "hooks/mission_load.h"
#include "hooks/shell_level_load.h"
#include "hooks/shell_command.h"
#include "hooks/load_level_solo.h"
#include "hooks/mission_select_block.h"

namespace {
	std::atomic<bool> g_shutdown{ false };
	HANDLE g_workerThread = nullptr;
	FILE* g_consoleOut = nullptr;
	FILE* g_consoleErr = nullptr;
	PipeClient* g_pipe = nullptr;

	void SetupConsole() {
		AllocConsole();
		SetConsoleTitleW(L"HaloAP Dll Console");

		freopen_s(&g_consoleOut, "CONOUT$", "w", stdout);
		freopen_s(&g_consoleErr, "CONOUT$", "w", stderr);

		setvbuf(stdout, nullptr, _IONBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
	}

	void TeardownConsole() {
		if (g_consoleOut) { fclose(g_consoleOut); g_consoleOut = nullptr; }
		if (g_consoleErr) { fclose(g_consoleErr); g_consoleErr = nullptr; }
		FreeConsole();
	}

	// Uninstall all hooks. Safe to call even if hooks were never installed.
	void UninstallAllHooks() {
		haloap::UninstallMissionSelectBlockHook();
		haloap::UninstallShellCommandHook();
		haloap::UninstallLoadLevelSoloHook();
		haloap::UninstallShellLevelLoadHook();
		haloap::UninstallMissionLoadHook();
		haloap::UninstallMissionCompleteHook();
		haloap::UninstallMissionIdLookupHook();
	}

	// Install all pattern-scanned hooks (requires halo1.dll to be loaded).
	void InstallPatternHooks() {
		haloap::InstallMissionSelectBlockHook(g_pipe);
		if (!haloap::InstallMissionCompleteHook(g_pipe)) {
			printf("Failed to install mission-complete hook.\n");
		}
		haloap::InstallMissionIdLookupHook(g_pipe);
		haloap::InstallMissionLoadHook(g_pipe);
		haloap::InstallLoadLevelSoloHook(g_pipe);
	}

	DWORD WINAPI WorkerMain(LPVOID /*param*/) {
		SetupConsole();

		printf("==========================================\n");
		printf("  HaloAP DLL v%s\n", haloap::kVersion);
		printf("  Running inside MCC (PID %lu)\n", GetCurrentProcessId());
		printf("==========================================\n");

		MH_STATUS mhStatus = MH_Initialize();
		if (mhStatus != MH_OK) {
			printf("MH_Initialize failed: %d\n", mhStatus);
		}
		else {
			printf("MinHook initialized.\n");
		}

		Sleep(200);

		g_pipe = new PipeClient();
		if (!g_pipe->Connect()) {
			printf("Failed to connect to injector pipe. Continuing without it.\n");
		}
		else {
			printf("Connected to injector.\n");
			printf("Sending HELLO...\n");
			bool sendOk = g_pipe->Send("HELLO: dll speaking, v" + std::string(haloap::kVersion));
			printf("HELLO send returned %s\n", sendOk ? "true" : "false");
		}

		// Wait for halo1.dll to be loaded (up to 5 seconds).
		for (int i = 0; i < 50; i++) {
			if (GetModuleHandleA("halo1.dll")) break;
			Sleep(100);
		}

		// Initial install of pattern-scanned hooks.
		HMODULE lastHalo1 = GetModuleHandleA("halo1.dll");
		if (lastHalo1) {
			InstallPatternHooks();
		}

		// Track engine object state for vtable hook lifecycle.
		void* lastEngineObj = nullptr;
		bool vtableHooksInstalled = false;

		int tick = 0;
		while (!g_shutdown.load()) {
			HMODULE currentHalo1 = GetModuleHandleA("halo1.dll");

			// --- Detect halo1.dll reload (base address change) ---
			if (currentHalo1 != lastHalo1) {
				if (currentHalo1) {
					printf("[monitor] halo1.dll reloaded at %p (was %p). Reinstalling hooks...\n",
						currentHalo1, lastHalo1);
					UninstallAllHooks();
					InstallPatternHooks();
				}
				else {
					printf("[monitor] halo1.dll unloaded.\n");
					UninstallAllHooks();
				}
				lastHalo1 = currentHalo1;
				lastEngineObj = nullptr;
				vtableHooksInstalled = false;
			}

			// --- Track engine object lifecycle ---
			// --- Track engine object lifecycle ---
			if (currentHalo1 && !vtableHooksInstalled) {
				if (haloap::InstallShellLevelLoadHook(g_pipe)) {
					haloap::InstallShellCommandHook(g_pipe);
					vtableHooksInstalled = true;
					lastEngineObj = haloap::GetEngineObject();
				}
			}
			if (vtableHooksInstalled) {
				void* currentEngineObj = haloap::GetEngineObject();
				if (currentEngineObj != lastEngineObj) {
					printf("[monitor] Engine object changed: %p -> %p. Reinstalling ALL hooks...\n",
						lastEngineObj, currentEngineObj);

					// Tear down everything.
					UninstallAllHooks();
					vtableHooksInstalled = false;

					// Reinstall pattern hooks immediately.
					InstallPatternHooks();

					// Vtable hooks will reinstall on next tick when engine object is ready.
					lastEngineObj = currentEngineObj;
				}
			}

			printf("[heartbeat %d] still here\n", tick);

			if (g_pipe && g_pipe->IsConnected()) {
				std::string msg = "HEARTBEAT: tick " + std::to_string(tick);
				g_pipe->Send(msg);
			}

			tick++;

			for (int i = 0; i < 20 && !g_shutdown.load(); ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}

		printf("Worker shutting down.\n");

		if (g_pipe) {
			g_pipe->Stop();
			delete g_pipe;
			g_pipe = nullptr;
		}

		UninstallAllHooks();
		MH_Uninitialize();
		TeardownConsole();
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		g_workerThread = CreateThread(nullptr, 0, WorkerMain, nullptr, 0, nullptr);
		if (!g_workerThread) {
			return FALSE;
		}
		break;

	case DLL_PROCESS_DETACH:
		g_shutdown.store(true);
		if (g_workerThread) {
			WaitForSingleObject(g_workerThread, 2000);
			CloseHandle(g_workerThread);
			g_workerThread = nullptr;
		}
		break;
	}
	return TRUE;
}