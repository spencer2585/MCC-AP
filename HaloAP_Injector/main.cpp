#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include "shared/common.h"
#include "ap_bridge.h"
#include "pipe_server.h"

namespace {
	// Prompt user for a line of input with an optional default.
	std::string PromptLine(const std::string& prompt, const std::string& defaultVal = "") {
		if (defaultVal.empty()) {
			std::cout << prompt << ": ";
		}
		else {
			std::cout << prompt << " [" << defaultVal << "]: ";
		}
		std::string input;
		std::getline(std::cin, input);
		if (input.empty()) return defaultVal;
		return input;
	}
}

namespace fs = std::filesystem;

//Find a running process by executable name, return 0 if not found
DWORD FindProcessId(const std::wstring& exeName) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);

	DWORD pid = 0;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (exeName == entry.szExeFile) {
				pid = entry.th32ProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return pid;
}

//Inject LDD into process, return true on success
bool InjectDLL(DWORD pid, const fs::path& dllPath) {
	std::string dllPathStr = dllPath.string();
	size_t pathSize = dllPathStr.size() + 1;

	//open target process
	HANDLE hProcess = OpenProcess(
		PROCESS_CREATE_THREAD |
		PROCESS_VM_OPERATION |
		PROCESS_VM_WRITE |
		PROCESS_VM_READ |
		PROCESS_QUERY_INFORMATION,
		FALSE,
		pid
	);
	if (!hProcess) {
		std::cerr << "OpenProcess failed: " << GetLastError() << "\n";
		return false;
	}

	//Allocate memory inside the target
	LPVOID remoteMem = VirtualAllocEx(
		hProcess,
		nullptr,
		pathSize,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE
	);
	if (!remoteMem) {
		std::cerr << "VirtualAllocEx failed: " << GetLastError() << "\n";
		CloseHandle(hProcess);
		return false;
	}

	//write the DLL path into the targets memory
	if (!WriteProcessMemory(hProcess, remoteMem, dllPathStr.c_str(), pathSize, nullptr)) {
		std::cerr << "WriteProcessMemory failed: " << GetLastError() << "\n";
		VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	//get the address of LoadLibraryA
	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!hKernel32) {
		std::cerr << "GetModuleHandle kernel32 failed\n";
		VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}
	LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
	if (!loadLibraryAddr) {
		std::cerr << "GetProcAddress LoadLibaryA failed\n";
		VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	//start a thread in the target
	HANDLE hThread = CreateRemoteThread(
		hProcess,
		nullptr,
		0,
		(LPTHREAD_START_ROUTINE)loadLibraryAddr,
		remoteMem,
		0,
		nullptr
	);
	if (!hThread) {
		std::cerr << "CreateRemoteThread failed: " << GetLastError() << "\n";
		VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	//wait for LoadLibaryA to finish
	WaitForSingleObject(hThread, INFINITE);

	DWORD exitCode = 0;
	GetExitCodeThread(hThread, &exitCode);

	//cleanup
	CloseHandle(hThread);
	VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
	CloseHandle(hProcess);

	if (exitCode == 0) {
		std::cerr << "LoadLibraryA returned NULL inside target\n";
		return false;
	}

	std::cout << "DLL loaded at address 0x" << std::hex << exitCode << std::dec << " inside target\n";
	return true;
}

int main() {
	std::cout << "HaloAP Injector v" << haloap::kVersion << "\n\n";

	// 1. Prompt for AP connection info.
	std::string serverUri = PromptLine("Server address", "ws://localhost:38281");
	// If user typed a bare host:port, prepend ws:// so apclientpp is happy.
	if (serverUri.find("://") == std::string::npos) {
		if (serverUri.find("localhost") != std::string::npos || 
			serverUri.find("127.0.0.1") != std::string::npos) {
			serverUri = "ws://" + serverUri;
			} else {
				serverUri = "wss://" + serverUri;
			}
	}

	std::string slot = PromptLine("Slot name");
	while (slot.empty()) {
		std::cout << "Slot name is required.\n";
		slot = PromptLine("Slot name");
	}

	std::string password = PromptLine("Password (optional)");

	std::cout << "\n";

	// 2. Locate the DLL next to the injector.
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	fs::path dllPath = fs::path(exePath).parent_path() / L"HaloAP.dll";

	if (!fs::exists(dllPath)) {
		std::cerr << "HaloAP.dll not found at: " << dllPath.string() << "\n";
		std::cerr << "Press enter to exit.\n";
		std::cin.get();
		return 1;
	}

	// 3. Start the AP bridge and wait for initial connection.
	haloap::APBridge apBridge;
	if (!apBridge.Start(serverUri, "Halo The Master Chief Collection", slot, password)) {
		std::cerr << "Failed to start AP bridge.\n";
		std::cin.get();
		return 1;
	}
	
	

	// Start polling in a background thread.
	std::atomic<bool> apShutdown{ false };
	std::thread apPollThread([&apBridge, &apShutdown]() {
		while (!apShutdown.load()) {
			apBridge.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		});

	// Wait up to 10 seconds for slot connection before injecting.
	std::cout << "Waiting for slot connection...\n";
	for (int i = 0; i < 100; i++) {
		if (apBridge.IsConnected()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (!apBridge.IsConnected()) {
		std::cerr << "Could not connect to AP server within 10 seconds.\n";
		std::cerr << "Check server address, slot name, and that the server is running.\n";
		std::cerr << "Press enter to exit.\n";
		apShutdown.store(true);
		if (apPollThread.joinable()) apPollThread.join();
		std::cin.get();
		return 1;
	}

	// 4. Start the pipe server.
	PipeServer pipe;
	pipe.SetMessageHandler([&apBridge](const std::string& msg) {
		apBridge.HandleDllMessage(msg);
		});

	if (!pipe.Start()) {
		std::cerr << "Failed to start pipe server.\n";
		apShutdown.store(true);
		if (apPollThread.joinable()) apPollThread.join();
		std::cin.get();
		return 1;
	}
	
	apBridge.SetSendToDll([&pipe](const std::string& msg) {
		pipe.Send(msg);
	});

	// 5. Find MCC and inject.
	std::cout << "Looking for MCC...\n";
	DWORD pid = 0;
	while (pid == 0) {
		pid = FindProcessId(haloap::kMCCExecutableName);
		if (pid == 0) {
			std::cout << "  MCC not running. Waiting 2s...\n";
			Sleep(2000);
		}
	}

	std::cout << "Found MCC, PID " << pid << ". Injecting...\n";
	if (!InjectDLL(pid, dllPath)) {
		std::cerr << "Injection failed.\n";
		pipe.Stop();
		apShutdown.store(true);
		if (apPollThread.joinable()) apPollThread.join();
		std::cin.get();
		return 1;
	}
	std::cout << "Injection succeeded.\n\n";

	// Wait for DLL to connect via pipe
	std::cout << "Waiting for DLL to connect...\n";
	for (int i = 0; i < 100; i++) {
		if (pipe.IsConnected()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (pipe.IsConnected()) {
		// Small delay to let DLL finish initialization
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		apBridge.ReplayBufferedItems();
	}

	// 6. Main loop: just wait for user to exit.
	std::cout << "Press enter to exit (will shut down DLL and disconnect).\n";
	std::cin.get();

	std::cout << "Shutting down...\n";
	pipe.Stop();
	apShutdown.store(true);
	if (apPollThread.joinable()) apPollThread.join();
	apBridge.Stop();

	return 0;
}