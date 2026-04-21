#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include "shared/common.h"
#include "pipe_server.h"

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
	std::cout << "HaloAP Injector v" << haloap::kVersion << "\n";

	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	fs::path dllPath = fs::path(exePath).parent_path() / L"HaloAP.dll";

	if (!fs::exists(dllPath)) {
		std::cerr << "HaloAP.dll not found next to injector at: "
			<< dllPath.string() << "\n";
		std::cerr << "Press enter to exit.\n";
		std::cin.get();
		return 1;
	}

	std::cout << "DLL path: " << dllPath.string() << "\n";

	// Start the pipe server BEFORE injection, so the DLL can connect
	// as soon as it finishes loading.
	PipeServer pipe;
	if (!pipe.Start()) {
		std::cerr << "Failed to start pipe server\n";
		std::cin.get();
		return 1;
	}

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
		std::cin.get();
		return 1;
	}
	std::cout << "Injection succeeded.\n";

	// Main loop: send a ping every few seconds, exit on user keypress.
	std::cout << "Press enter to exit (will shut down DLL).\n";

	std::atomic<bool> pingShutdown{ false };
	std::thread pingThread([&pipe, &pingShutdown]() {
		int n = 0;
		while (!pingShutdown.load()) {
			// Sleep in short chunks so we notice shutdown quickly.
			for (int i = 0; i < 50 && !pingShutdown.load(); ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (pingShutdown.load()) break;

			if (pipe.IsConnected()) {
				std::string msg = "COMMAND: ping " + std::to_string(n++);
				if (pipe.Send(msg)) {
					std::cout << "[pipe -> dll] " << msg << "\n";
				}
			}
		}
		});

	std::cin.get();

	std::cout << "Shutting down...\n";
	pingShutdown.store(true);
	pingThread.join();
	pipe.Stop();
	return 0;
}