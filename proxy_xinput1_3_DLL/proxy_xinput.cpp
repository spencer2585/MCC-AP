#include <Windows.h>
#include <stdio.h>
// ============================================================
// xinput1_3.dll proxy (runtime forwarding)
//
// XInput has only 8 exports — simplest possible proxy.
// ============================================================

static HMODULE s_realXInput = nullptr;
static FARPROC s_funcs[8] = {};

static const char* s_funcNames[] = {
    "XInputGetState",                      // 0
    "XInputSetState",                      // 1
    "XInputGetCapabilities",               // 2
    "XInputEnable",                        // 3
    "XInputGetDSoundAudioDeviceGuids",     // 4
    "XInputGetBatteryInformation",         // 5
    "XInputGetKeystroke",                  // 6
    "XInputGetAudioDeviceIds",             // 7
};

static bool LoadRealXInput() {
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);
    strcat_s(systemDir, "\\xinput1_3.dll");
    s_realXInput = LoadLibraryA(systemDir);
    if (!s_realXInput) return false;
    for (int i = 0; i < 8; i++) {
        s_funcs[i] = GetProcAddress(s_realXInput, s_funcNames[i]);
    }
    return true;
}

extern "C" {

// DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
__declspec(dllexport) DWORD WINAPI Proxy_XInputSetState(DWORD a, void* b) {
    typedef DWORD(WINAPI* F)(DWORD, void*);
    return s_funcs[1] ? ((F)s_funcs[1])(a, b) : ERROR_DEVICE_NOT_CONNECTED;
}

// DWORD XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCaps)
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetCapabilities(DWORD a, DWORD b, void* c) {
    typedef DWORD(WINAPI* F)(DWORD, DWORD, void*);
    return s_funcs[2] ? ((F)s_funcs[2])(a, b, c) : ERROR_DEVICE_NOT_CONNECTED;
}

// void XInputEnable(BOOL enable)
__declspec(dllexport) void WINAPI Proxy_XInputEnable(BOOL a) {
    typedef void(WINAPI* F)(BOOL);
    if (s_funcs[3]) ((F)s_funcs[3])(a);
}

// DWORD XInputGetDSoundAudioDeviceGuids(DWORD, GUID*, GUID*)
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD a, void* b, void* c) {
    typedef DWORD(WINAPI* F)(DWORD, void*, void*);
    return s_funcs[4] ? ((F)s_funcs[4])(a, b, c) : ERROR_DEVICE_NOT_CONNECTED;
}

// DWORD XInputGetBatteryInformation(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*)
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetBatteryInformation(DWORD a, BYTE b, void* c) {
    typedef DWORD(WINAPI* F)(DWORD, BYTE, void*);
    return s_funcs[5] ? ((F)s_funcs[5])(a, b, c) : ERROR_DEVICE_NOT_CONNECTED;
}

// DWORD XInputGetKeystroke(DWORD, DWORD, PXINPUT_KEYSTROKE)
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetKeystroke(DWORD a, DWORD b, void* c) {
    typedef DWORD(WINAPI* F)(DWORD, DWORD, void*);
    return s_funcs[6] ? ((F)s_funcs[6])(a, b, c) : ERROR_DEVICE_NOT_CONNECTED;
}

// DWORD XInputGetAudioDeviceIds(DWORD, LPWSTR, UINT*, LPWSTR, UINT*)
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetAudioDeviceIds(DWORD a, void* b, void* c, void* d, void* e) {
    typedef DWORD(WINAPI* F)(DWORD, void*, void*, void*, void*);
    return s_funcs[7] ? ((F)s_funcs[7])(a, b, c, d, e) : ERROR_DEVICE_NOT_CONNECTED;
}

} // extern "C"

static HMODULE s_proxyModule = nullptr;
static bool s_haloLoaded = false;

static void EnsureHaloAPLoaded() {
    if (s_haloLoaded) return;
    s_haloLoaded = true;
    
    char dllDir[MAX_PATH];
    GetModuleFileNameA(s_proxyModule, dllDir, MAX_PATH);
    char* lastSlash = strrchr(dllDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(dllDir, "HaloAP.dll");
    
    HMODULE h = LoadLibraryA(dllDir);
    
    // Temporary debug popup — remove after testing
    //char msg[512];
    //sprintf_s(msg, sizeof(msg), "Path: %s\nResult: %p\nError: %lu", 
    //          dllDir, h, h ? 0 : GetLastError());
    //MessageBoxA(nullptr, msg, "HaloAP Proxy Debug", MB_OK);
}

// Add this call to XInputGetState (called every frame by MCC):
__declspec(dllexport) DWORD WINAPI Proxy_XInputGetState(DWORD a, void* b) {
    EnsureHaloAPLoaded();
    typedef DWORD(WINAPI* F)(DWORD, void*);
    return s_funcs[0] ? ((F)s_funcs[0])(a, b) : ERROR_DEVICE_NOT_CONNECTED;
}

// DllMain just loads the real xinput and saves the module handle:
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        if (!LoadRealXInput()) return FALSE;

        char dllDir[MAX_PATH];
        GetModuleFileNameA(hModule, dllDir, MAX_PATH);
        char* lastSlash = strrchr(dllDir, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(dllDir, "HaloAP.dll");
        LoadLibraryA(dllDir);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (s_realXInput) {
            FreeLibrary(s_realXInput);
            s_realXInput = nullptr;
        }
    }
    return TRUE;
}