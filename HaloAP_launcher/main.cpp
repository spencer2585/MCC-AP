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
	std::cout << "HaloAP Bridge v" << haloap::kVersion << "\n\n";

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
	
	// Prompt for game directory
	std::string gameDir = PromptLine("MCC game directory (where mcc-win64-shipping.exe lives)");
	while (gameDir.empty()) {
		std::cout << "Game directory is required.\n";
		gameDir = PromptLine("MCC game directory");
	}
	// Store for later use (DLL copy/cleanup)

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

	// 4. Wait for DLL to connect via pipe (MCC loads it via proxy DLL).
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

	// 5. Main loop: wait for user to exit.
	std::cout << "Press enter to exit (will shut down DLL and disconnect).\n";
	std::cin.get();

	std::cout << "Shutting down...\n";
	pipe.Stop();
	apShutdown.store(true);
	if (apPollThread.joinable()) apPollThread.join();
	apBridge.Stop();

	return 0;
}