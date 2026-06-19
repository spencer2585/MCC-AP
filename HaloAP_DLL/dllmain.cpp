#include <windows.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <psapi.h>
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
#include "hooks/chapter_title.h"

#pragma comment(lib, "psapi.lib")

namespace
{
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

	void InstallHalo1Hooks() {
		if (!haloap::InstallMissionCompleteHook(g_pipe)) {
			printf("Failed to install mission-complete hook.\n");
		}
		haloap::InstallMissionIdLookupHook(g_pipe);
		haloap::InstallMissionLoadHook(g_pipe);
		haloap::InstallLoadLevelSoloHook(g_pipe);
		haloap::InstallChapterTitleHook(g_pipe);
	}
	
	void UninstallHalo1Hooks()
	{
		haloap::UninstallLoadLevelSoloHook();
		haloap::UninstallMissionLoadHook();
		haloap::UninstallMissionCompleteHook();
		haloap::UninstallMissionIdLookupHook();
		haloap::UninstallChapterTitleHook();
	}
	
	void UninstallVtableHooks()
	{
		haloap::UninstallShellCommandHook();
		haloap::UninstallShellLevelLoadHook();
	}
	
	void InstallExeHooks()
	{
		haloap::InstallMissionSelectBlockHook(g_pipe);
	}
	
	void UninstallAllHooks()
	{
		haloap::UninstallMissionSelectBlockHook();
		UninstallVtableHooks();
		UninstallHalo1Hooks();
	}

	// =================================================================
// UE4 GUObjectArray diagnostic — paste into dllmain.cpp
// Replace the existing DumpUObjectSearch function with this.
// =================================================================

static const uintptr_t GUOBJECTARRAY_OFFSET   = 0x3E389B0;
static const uintptr_t SCREEN_STATICCLASS     = 0xBD6E50;
static const uintptr_t VM_STATICCLASS         = 0xBDB2A4;
static const uintptr_t FNAME_TOSTRING_OFFSET  = 0xD3BF38;

struct FString {
    wchar_t* Data;
    int32_t Count;
    int32_t Max;
};

bool ResolveFName(uint8_t* exe, void* obj, int nameOffset, char* outBuf, int bufSize) {
    typedef void (*FNameToStringFn)(void* fname, FString* out);
    auto ToString = (FNameToStringFn)(exe + FNAME_TOSTRING_OFFSET);
    FString result = {};
    __try {
        ToString((uint8_t*)obj + nameOffset, &result);
        if (result.Data && result.Count > 0) {
            int len = (result.Count < bufSize - 1) ? result.Count : bufSize - 1;
            for (int i = 0; i < len; i++) outBuf[i] = (char)result.Data[i];
            outBuf[len] = 0;
            return true;
        }
    } __except (1) {}
    outBuf[0] = '?'; outBuf[1] = 0;
    return false;
}

// =================================================================
// UE4 GUObjectArray diagnostic — replace DumpUObjectSearch in dllmain.cpp
// Keep the constants and ResolveFName helper from before.
// =================================================================
	void ScanAndModifyWideString(const wchar_t* target, const wchar_t* replacement) {
	size_t targetLen = wcslen(target);
	size_t targetBytes = (targetLen + 1) * 2;
	size_t replLen = wcslen(replacement);
    
	printf("[SCAN] Searching for L\"%ls\" and replacing with L\"%ls\"...\n", target, replacement);
    
	MEMORY_BASIC_INFORMATION mbi;
	uint8_t* addr = nullptr;
	int found = 0;
    
	while (VirtualQuery(addr, &mbi, sizeof(mbi))) {
		if (mbi.State == MEM_COMMIT && 
			(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
			!(mbi.Protect & PAGE_GUARD)) {
            
			uint8_t* base = (uint8_t*)mbi.BaseAddress;
			size_t size = mbi.RegionSize;
            
			__try {
				for (size_t off = 0; off + targetBytes <= size; off += 2) {
					if (memcmp(base + off, target, targetBytes) == 0) {
						printf("[SCAN]   FOUND+MODIFIED at %p\n", base + off);
						wchar_t* ws = (wchar_t*)(base + off);
						for (size_t c = 0; c <= replLen; c++) ws[c] = replacement[c];
						found++;
						if (found > 30) goto done;
					}
				}
			} __except(1) {}
			}
		addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
		if ((uintptr_t)addr < (uintptr_t)mbi.BaseAddress) break;
	}
	done:
		printf("[SCAN] Modified %d occurrences.\n", found);
}
	// Add a vectored exception handler to catch the crash address
	static LONG WINAPI CrashCatcher(EXCEPTION_POINTERS* ep) {
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		void* crashAddr = ep->ExceptionRecord->ExceptionAddress;
		uint8_t* exeBase = (uint8_t*)GetModuleHandleA(nullptr);
		size_t offset = (size_t)crashAddr - (size_t)exeBase;
		printf("[CRASH] AV at exe+0x%zX\n", offset);
        
		// Capture stack trace
		void* frames[20] = {};
		USHORT count = RtlCaptureStackBackTrace(0, 20, frames, nullptr);
		for (USHORT i = 0; i < count; i++) {
			HMODULE hMod = nullptr;
			char name[64] = "?";
			size_t off = 0;
			if (GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)frames[i], &hMod)) {
				char path[MAX_PATH];
				if (GetModuleFileNameA(hMod, path, sizeof(path))) {
					const char* base = strrchr(path, '\\');
					base = base ? base + 1 : path;
					strncpy_s(name, sizeof(name), base, _TRUNCATE);
					off = (size_t)frames[i] - (size_t)hMod;
				}
				}
			printf("[CRASH]   STACK[%u] %s+0x%zx\n", i, name, off);
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
	
// Multi-level reverse pointer scan to find SWidget from display text
	void DumpUObjectSearch() {
    uint8_t* exe = (uint8_t*)GetModuleHandleA(nullptr);
    uint8_t* gArray = exe + GUOBJECTARRAY_OFFSET;
    void** Objects = *(void***)(gArray + 0x10);
    int numElements = *(int*)(gArray + 0x24);
    int numChunks = *(int*)(gArray + 0x2C);
    if (!Objects || numElements <= 0) return;

    // Find SetVisibility UFunction (outer=Widget)
    void* setVisFunc = nullptr;
    for (int i = 0; i < numElements; i++) {
        int c = i / 0x10000, n = i % 0x10000;
        if (c >= numChunks || !Objects[c]) continue;
        void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
        if (!obj) continue;
        __try {
            char name[64] = {};
            ResolveFName(exe, obj, 0x18, name, sizeof(name));
            if (strcmp(name, "SetVisibility") != 0) continue;
            void* outer = *(void**)((uint8_t*)obj + 0x20);
            char outerName[64] = {};
            if (outer) ResolveFName(exe, outer, 0x18, outerName, sizeof(outerName));
            if (strcmp(outerName, "Widget") == 0) {
                setVisFunc = obj;
                printf("[UE4] SetVisibility UFunction at %p\n", obj);
                break;
            }
        } __except (1) {}
    }
    if (!setVisFunc) { printf("[UE4] SetVisibility not found\n"); return; }

    // Find WBP_MCCMenuButton_C class
    void* btnClass = nullptr;
    for (int i = 0; i < numElements; i++) {
        int c = i / 0x10000, n = i % 0x10000;
        if (c >= numChunks || !Objects[c]) continue;
        void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
        if (!obj) continue;
        __try {
            void* cls = *(void**)((uint8_t*)obj + 0x10);
            char cn[128] = {};
            if (ResolveFName(exe, cls, 0x18, cn, sizeof(cn)) &&
                strcmp(cn, "WBP_MCCMenuButton_C") == 0) {
                btnClass = cls;
                break;
            }
        } __except (1) {}
    }
    if (!btnClass) { printf("[UE4] No button class\n"); return; }

    // Collapse ALL live instances
    typedef void (__fastcall *ProcessEventFn)(void* obj, void* func, void* parms);
    printf("[UE4] Collapsing ALL menu buttons...\n");
    int collapsed = 0;

    for (int i = 0; i < numElements; i++) {
        int c = i / 0x10000, n = i % 0x10000;
        if (c >= numChunks || !Objects[c]) continue;
        void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
        if (!obj) continue;
        __try {
            if (*(void**)((uint8_t*)obj + 0x10) != btnClass) continue;
            char on[128] = {};
            ResolveFName(exe, obj, 0x18, on, sizeof(on));
            if (!strstr(on, "_C_")) continue;
        	
        	char* numStr = strstr(on, "_C_") + 3;
        	int btnNum = atoi(numStr);
        	
        	if (btnNum ==22 || btnNum == 24) {
        		uint8_t params[16] = {};
        		params[0] = 1;
        		uint64_t* vtable = *(uint64_t**)obj;
        		ProcessEventFn pe = (ProcessEventFn)vtable[0x40];
        		pe(obj, setVisFunc, params);
        		printf("[UE4]   COLLAPSED '%s'\n", on);
        		collapsed++;
        	} else {
        		printf("[UE4]   skipped '%s'\n", on);
        	}
        } __except (1) {}
    }
    printf("[UE4] Collapsed %d buttons. Check screen.\n", collapsed);
    printf("[UE4] === Done ===\n");
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

		Sleep(5000);

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

	haloap::InstallMissionSelectBlockHook(g_pipe);
	haloap::InitGameModeButtonCollapse();
	
		for (int i = 0; i < 50; i++) {
			if (GetModuleHandleA("halo1.dll")) break;
			Sleep(100);
		}

		HMODULE lastHalo1 = GetModuleHandleA("halo1.dll");
		if (lastHalo1) {
			// Temporary: find cinematic_set_title string
			MODULEINFO mi = {};
			GetModuleInformation(GetCurrentProcess(), lastHalo1, &mi, sizeof(mi));
			uint8_t* base = (uint8_t*)lastHalo1;
			size_t size = mi.SizeOfImage;
			const char* target = "cinematic_set_title";
			size_t targetLen = strlen(target);
			for (size_t i = 0; i < size - targetLen; i++) {
				if (memcmp(base + i, target, targetLen) == 0 && base[i + targetLen] == 0) {
					printf("[search] Found '%s' at halo1.dll+0x%zX\n", target, i);
				}
			}
			InstallHalo1Hooks();
		}
	
		// Wait for UE4 menu system to fully initialize, then cache button data
		//Sleep(500);
		haloap::InitGameModeButtonCollapse();
	

		void* lastEngineObj = nullptr;
		bool vtableHooksInstalled = false;
		bool ue4DumpDone = false;

		int tick = 0;
		while (!g_shutdown.load()) {
			HMODULE currentHalo1 = GetModuleHandleA("halo1.dll");

			if (currentHalo1 != lastHalo1) {
				if (currentHalo1) {
					printf("[monitor] halo1.dll reloaded at %p (was %p). Reinstalling hooks...\n",
						currentHalo1, lastHalo1);
					UninstallHalo1Hooks();
					UninstallVtableHooks();
					InstallHalo1Hooks();
				}
				else {
					printf("[monitor] halo1.dll unloaded.\n");
					UninstallHalo1Hooks();
					UninstallVtableHooks();
				}
				lastHalo1 = currentHalo1;
				lastEngineObj = nullptr;
				vtableHooksInstalled = false;
			}

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
					UninstallVtableHooks();
					UninstallHalo1Hooks();
					vtableHooksInstalled = false;
					InstallHalo1Hooks();
					lastEngineObj = currentEngineObj;
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