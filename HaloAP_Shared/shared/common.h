#pragma once

//shared definitions between HaloAP_launcher and HaloAP_DLL

namespace haloap {
	//client version string
	constexpr const char* kVersion = "1.0.1";

	//main executable name on steam
	constexpr const wchar_t* kMCCExecutableName = L"MCC-Win64-Shipping.exe";

	//named pipe use for injector <-->Dll communication
	constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\HaloAP";

	//max message size
	constexpr size_t kMaxMessageSize = 4096;
}