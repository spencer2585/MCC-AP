#include <Windows.h>
#include <cstdio>

// ============================================================
// version.dll proxy
// 
// This DLL is placed in MCC's game directory. When MCC loads,
// Windows finds our version.dll before the system one.
// We load the real version.dll from System32 and forward all
// calls to it. In DllMain, we also load HaloAP.dll.
// ============================================================

// Original function pointers
static HMODULE s_realVersionDll = nullptr;

// Function pointer types
typedef BOOL(WINAPI* pGetFileVersionInfoA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL(WINAPI* pGetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL(WINAPI* pGetFileVersionInfoExA)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL(WINAPI* pGetFileVersionInfoExW)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD(WINAPI* pGetFileVersionInfoSizeA)(LPCSTR, LPDWORD);
typedef DWORD(WINAPI* pGetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef DWORD(WINAPI* pGetFileVersionInfoSizeExA)(DWORD, LPCSTR, LPDWORD);
typedef DWORD(WINAPI* pGetFileVersionInfoSizeExW)(DWORD, LPCWSTR, LPDWORD);
typedef DWORD(WINAPI* pVerFindFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD(WINAPI* pVerFindFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD(WINAPI* pVerInstallFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
typedef DWORD(WINAPI* pVerInstallFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
typedef DWORD(WINAPI* pVerLanguageNameA)(DWORD, LPSTR, DWORD);
typedef DWORD(WINAPI* pVerLanguageNameW)(DWORD, LPWSTR, DWORD);
typedef BOOL(WINAPI* pVerQueryValueA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL(WINAPI* pVerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
typedef DWORD(WINAPI* pGetFileVersionInfoByHandle)(DWORD, LPCVOID, DWORD, LPVOID);

// Original function pointers
static pGetFileVersionInfoA         orig_GetFileVersionInfoA = nullptr;
static pGetFileVersionInfoW         orig_GetFileVersionInfoW = nullptr;
static pGetFileVersionInfoExA       orig_GetFileVersionInfoExA = nullptr;
static pGetFileVersionInfoExW       orig_GetFileVersionInfoExW = nullptr;
static pGetFileVersionInfoSizeA     orig_GetFileVersionInfoSizeA = nullptr;
static pGetFileVersionInfoSizeW     orig_GetFileVersionInfoSizeW = nullptr;
static pGetFileVersionInfoSizeExA   orig_GetFileVersionInfoSizeExA = nullptr;
static pGetFileVersionInfoSizeExW   orig_GetFileVersionInfoSizeExW = nullptr;
static pVerFindFileA                orig_VerFindFileA = nullptr;
static pVerFindFileW                orig_VerFindFileW = nullptr;
static pVerInstallFileA             orig_VerInstallFileA = nullptr;
static pVerInstallFileW             orig_VerInstallFileW = nullptr;
static pVerLanguageNameA            orig_VerLanguageNameA = nullptr;
static pVerLanguageNameW            orig_VerLanguageNameW = nullptr;
static pVerQueryValueA              orig_VerQueryValueA = nullptr;
static pVerQueryValueW              orig_VerQueryValueW = nullptr;
static pGetFileVersionInfoByHandle  orig_GetFileVersionInfoByHandle = nullptr;

static bool LoadRealVersionDll() {
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);
    strcat_s(systemDir, "\\version.dll");

    s_realVersionDll = LoadLibraryA(systemDir);
    if (!s_realVersionDll) return false;

    orig_GetFileVersionInfoA       = (pGetFileVersionInfoA)GetProcAddress(s_realVersionDll, "GetFileVersionInfoA");
    orig_GetFileVersionInfoW       = (pGetFileVersionInfoW)GetProcAddress(s_realVersionDll, "GetFileVersionInfoW");
    orig_GetFileVersionInfoExA     = (pGetFileVersionInfoExA)GetProcAddress(s_realVersionDll, "GetFileVersionInfoExA");
    orig_GetFileVersionInfoExW     = (pGetFileVersionInfoExW)GetProcAddress(s_realVersionDll, "GetFileVersionInfoExW");
    orig_GetFileVersionInfoSizeA   = (pGetFileVersionInfoSizeA)GetProcAddress(s_realVersionDll, "GetFileVersionInfoSizeA");
    orig_GetFileVersionInfoSizeW   = (pGetFileVersionInfoSizeW)GetProcAddress(s_realVersionDll, "GetFileVersionInfoSizeW");
    orig_GetFileVersionInfoSizeExA = (pGetFileVersionInfoSizeExA)GetProcAddress(s_realVersionDll, "GetFileVersionInfoSizeExA");
    orig_GetFileVersionInfoSizeExW = (pGetFileVersionInfoSizeExW)GetProcAddress(s_realVersionDll, "GetFileVersionInfoSizeExW");
    orig_VerFindFileA              = (pVerFindFileA)GetProcAddress(s_realVersionDll, "VerFindFileA");
    orig_VerFindFileW              = (pVerFindFileW)GetProcAddress(s_realVersionDll, "VerFindFileW");
    orig_VerInstallFileA           = (pVerInstallFileA)GetProcAddress(s_realVersionDll, "VerInstallFileA");
    orig_VerInstallFileW           = (pVerInstallFileW)GetProcAddress(s_realVersionDll, "VerInstallFileW");
    orig_VerLanguageNameA          = (pVerLanguageNameA)GetProcAddress(s_realVersionDll, "VerLanguageNameA");
    orig_VerLanguageNameW          = (pVerLanguageNameW)GetProcAddress(s_realVersionDll, "VerLanguageNameW");
    orig_VerQueryValueA            = (pVerQueryValueA)GetProcAddress(s_realVersionDll, "VerQueryValueA");
    orig_VerQueryValueW            = (pVerQueryValueW)GetProcAddress(s_realVersionDll, "VerQueryValueW");
    orig_GetFileVersionInfoByHandle = (pGetFileVersionInfoByHandle)GetProcAddress(s_realVersionDll, "GetFileVersionInfoByHandle");

    return true;
}

// ============================================================
// Forwarding exports
// ============================================================

extern "C" {

__declspec(dllexport) BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (orig_GetFileVersionInfoA) return orig_GetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

__declspec(dllexport) BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (orig_GetFileVersionInfoW) return orig_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

__declspec(dllexport) BOOL WINAPI Proxy_GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (orig_GetFileVersionInfoExA) return orig_GetFileVersionInfoExA(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

__declspec(dllexport) BOOL WINAPI Proxy_GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (orig_GetFileVersionInfoExW) return orig_GetFileVersionInfoExW(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

__declspec(dllexport) DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    if (orig_GetFileVersionInfoSizeA) return orig_GetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    if (orig_GetFileVersionInfoSizeW) return orig_GetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    if (orig_GetFileVersionInfoSizeExA) return orig_GetFileVersionInfoSizeExA(dwFlags, lpwstrFilename, lpdwHandle);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    if (orig_GetFileVersionInfoSizeExW) return orig_GetFileVersionInfoSizeExW(dwFlags, lpwstrFilename, lpdwHandle);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen) {
    if (orig_VerFindFileA) return orig_VerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen) {
    if (orig_VerFindFileW) return orig_VerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen) {
    if (orig_VerInstallFileA) return orig_VerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen) {
    if (orig_VerInstallFileW) return orig_VerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    if (orig_VerLanguageNameA) return orig_VerLanguageNameA(wLang, szLang, cchLang);
    return 0;
}

__declspec(dllexport) DWORD WINAPI Proxy_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    if (orig_VerLanguageNameW) return orig_VerLanguageNameW(wLang, szLang, cchLang);
    return 0;
}

__declspec(dllexport) BOOL WINAPI Proxy_VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    if (orig_VerQueryValueA) return orig_VerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen);
    return FALSE;
}

__declspec(dllexport) BOOL WINAPI Proxy_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    if (orig_VerQueryValueW) return orig_VerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
    return FALSE;
}

__declspec(dllexport) DWORD WINAPI Proxy_GetFileVersionInfoByHandle(DWORD dwFlags, LPCVOID lpData, DWORD dwLen, LPVOID lpOutData) {
    if (orig_GetFileVersionInfoByHandle) return orig_GetFileVersionInfoByHandle(dwFlags, lpData, dwLen, lpOutData);
    return 0;
}

} // extern "C"

// ============================================================
// DllMain
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (!LoadRealVersionDll()) {
            // Can't function without the real version.dll
            return FALSE;
        }

        // Load HaloAP.dll from the same directory as this proxy
        char dllDir[MAX_PATH];
        GetModuleFileNameA(hModule, dllDir, MAX_PATH);
        // Strip filename to get directory
        char* lastSlash = strrchr(dllDir, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(dllDir, "HaloAP.dll");

        HMODULE haloAP = LoadLibraryA(dllDir);
        if (!haloAP) {
            // HaloAP.dll not found — proxy still works for version.dll forwarding
            // but no mod functionality
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (s_realVersionDll) {
            FreeLibrary(s_realVersionDll);
            s_realVersionDll = nullptr;
        }
    }
    return TRUE;
}