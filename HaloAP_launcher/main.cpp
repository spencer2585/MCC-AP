#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <shellapi.h>
#include "shared/common.h"
#include "ap_bridge.h"
#include "pipe_server.h"
#include <fstream>

namespace {
	std::string GetConfigPath() {
		char path[MAX_PATH];
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string s(path);
		return s.substr(0, s.find_last_of('\\') + 1) + "haloap_config.txt";
	}

	std::string LoadSavedGameDir() {
		std::ifstream f(GetConfigPath());
		std::string dir;
		if (f.is_open()) std::getline(f, dir);
		return dir;
	}

	void SaveGameDir(const std::string& dir) {
		std::ofstream f(GetConfigPath());
		if (f.is_open()) f << dir;
	}
}

namespace {
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

int main() {
	std::cout << "HaloAP Launcher v" << haloap::kVersion << "\n\n";

	// 1. Prompt for AP connection info.
	std::string serverUri = PromptLine("Server address", "ws://localhost:38281");
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
	
	// Load or prompt for game root directory
	std::string gameRoot = LoadSavedGameDir();
	if (!gameRoot.empty()) {
		std::cout << "Using saved game directory: " << gameRoot << "\n";
	} else {
		gameRoot = PromptLine("MCC game root directory (Steam install folder)");
		while (gameRoot.empty()) {
			std::cout << "Game directory is required.\n";
			gameRoot = PromptLine("MCC game root directory");
		}
		SaveGameDir(gameRoot);
	}

	// Derive the actual binary directory
	std::string binDir = gameRoot + "\\MCC\\Binaries\\Win64";
	std::string exePath = binDir + "\\MCC-Win64-Shipping.exe";

	// Verify the path is correct
	if (GetFileAttributesA(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		std::cerr << "Could not find MCC executable at:\n  " << exePath << "\n";
		std::cerr << "Check your game directory path.\n";
		// Clear saved path so they can re-enter next time
		SaveGameDir("");
		std::cin.get();
		return 1;
	}

	// 2. Start the AP bridge and wait for initial connection.
	haloap::APBridge apBridge;
	if (!apBridge.Start(serverUri, "Halo The Master Chief Collection", slot, password)) {
		std::cerr << "Failed to start AP bridge.\n";
		std::cin.get();
		return 1;
	}

	std::atomic<bool> apShutdown{ false };
	std::thread apPollThread([&apBridge, &apShutdown]() {
		while (!apShutdown.load()) {
			apBridge.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	});

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

	// 3. Start the pipe server and wait for the DLL to connect.
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

	// Copy mod DLLs to game directory
	std::cout << "Copying mod files to game directory...\n";

	char launcherPath[MAX_PATH];
	GetModuleFileNameA(nullptr, launcherPath, MAX_PATH);
	std::string launcherDir = std::string(launcherPath);
	launcherDir = launcherDir.substr(0, launcherDir.find_last_of('\\') + 1);

	std::string srcProxy = launcherDir + "xinput1_3.dll";
	std::string srcHaloAP = launcherDir + "HaloAP.dll";
	std::string dstProxy = binDir + "\\xinput1_3.dll";
	std::string dstHaloAP = binDir + "\\HaloAP.dll";

	if (!CopyFileA(srcProxy.c_str(), dstProxy.c_str(), FALSE)) {
		std::cerr << "Failed to copy xinput1_3.dll (error " << GetLastError() << ")\n";
		pipe.Stop();
		apShutdown.store(true);
		if (apPollThread.joinable()) apPollThread.join();
		std::cin.get();
		return 1;
	}
	std::cout << "  Copied xinput1_3.dll\n";

	if (!CopyFileA(srcHaloAP.c_str(), dstHaloAP.c_str(), FALSE)) {
		std::cerr << "Failed to copy HaloAP.dll (error " << GetLastError() << ")\n";
		DeleteFileA(dstProxy.c_str()); // clean up the proxy we just copied
		pipe.Stop();
		apShutdown.store(true);
		if (apPollThread.joinable()) apPollThread.join();
		std::cin.get();
		return 1;
	}
	std::cout << "  Copied HaloAP.dll\n\n";

	// Launch MCC without anti-cheat via Steam
	std::cout << "Launching MCC (anti-cheat disabled)...\n";
	ShellExecuteA(nullptr, "open", "steam://launch/976730/option2", nullptr, nullptr, SW_SHOWNORMAL);

	// Wait for DLL to connect via pipe
	std::cout << "Waiting for DLL to connect...\n";
	while (!pipe.IsConnected()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	std::cout << "DLL connected!\n";
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	apBridge.ReplayBufferedItems();

	// 5. Monitor for MCC exit (pipe disconnect), then clean up.
	std::cout << "Game running. Waiting for MCC to exit...\n";
	std::cout << "(Or press enter to shut down manually.)\n\n";

	// Monitor in background thread
	std::atomic<bool> gameExited{ false };
	std::thread monitorThread([&pipe, &gameExited]() {
		while (pipe.IsConnected() && !gameExited.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		gameExited.store(true);
	});

	// Also allow manual exit via enter key
	std::thread inputThread([&gameExited]() {
		std::cin.get();
		gameExited.store(true);
	});
	inputThread.detach();

	// Wait for either MCC exit or manual shutdown
	while (!gameExited.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	// Clean up DLLs from game directory
	std::cout << "\nCleaning up mod files...\n";
	std::string proxyPath = binDir + "\\xinput1_3.dll";
	std::string haloAPPath = binDir + "\\HaloAP.dll";

	// Small delay to let MCC fully release the files
	std::this_thread::sleep_for(std::chrono::seconds(2));

	if (DeleteFileA(proxyPath.c_str())) {
		std::cout << "  Removed xinput1_3.dll\n";
	} else {
		std::cerr << "  Failed to remove xinput1_3.dll (error " << GetLastError() << ")\n";
	}
	if (DeleteFileA(haloAPPath.c_str())) {
		std::cout << "  Removed HaloAP.dll\n";
	} else {
		std::cerr << "  Failed to remove HaloAP.dll (error " << GetLastError() << ")\n";
	}

	// Shut down
	std::cout << "Shutting down...\n";
	pipe.Stop();
	apShutdown.store(true);
	if (monitorThread.joinable()) monitorThread.join();
	apBridge.Stop();

	return 0;
}