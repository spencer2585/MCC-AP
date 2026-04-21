#include <windows.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include "pipe_client.h"
#include "shared/common.h"

namespace {
	std::atomic<bool> g_shutdown{ false };
	HANDLE g_workerThread = nullptr;
	FILE* g_consoleOut = nullptr;
	FILE* g_consoleErr = nullptr;
	PipeClient* g_pipe = nullptr;

	void SetupConsole() {
		//create new console window
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

	DWORD WINAPI WorkerMain(LPVOID /*param*/) {
		SetupConsole();

		printf("==========================================\n");
		printf("  HaloAP DLL v%s\n", haloap::kVersion);
		printf("  Running inside MCC (PID %lu)\n", GetCurrentProcessId());
		printf("==========================================\n");

		// Give the injector a moment to have the pipe ready, then connect.
		// (The injector creates the pipe before injecting, so normally it's ready
		// immediately, but a small delay is harmless and defends against timing.)
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

		int tick = 0;
		while (!g_shutdown.load()) {
			printf("[heartbeat %d] still here\n", tick);

			if (g_pipe && g_pipe->IsConnected()) {
				printf("  sending heartbeat...\n");
				std::string msg = "HEARTBEAT: tick " + std::to_string(tick);
				bool ok = g_pipe->Send(msg);
				printf("  heartbeat send returned %s\n", ok ? "true" : "false");
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