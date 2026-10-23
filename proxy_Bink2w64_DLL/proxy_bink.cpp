#include <windows.h>

// Forward all exports to the renamed original DLL
// Ordinals must match exactly (note: @10 is unused in the original)
#pragma comment(linker, "/export:BinkAllocateFrameBuffers=bink2w64_original.BinkAllocateFrameBuffers,@1")
#pragma comment(linker, "/export:BinkClose=bink2w64_original.BinkClose,@2")
#pragma comment(linker, "/export:BinkCloseTrack=bink2w64_original.BinkCloseTrack,@3")
#pragma comment(linker, "/export:BinkControlBackgroundIO=bink2w64_original.BinkControlBackgroundIO,@4")
#pragma comment(linker, "/export:BinkCopyToBuffer=bink2w64_original.BinkCopyToBuffer,@5")
#pragma comment(linker, "/export:BinkCopyToBufferRect=bink2w64_original.BinkCopyToBufferRect,@6")
#pragma comment(linker, "/export:BinkDoFrame=bink2w64_original.BinkDoFrame,@7")
#pragma comment(linker, "/export:BinkDoFrameAsync=bink2w64_original.BinkDoFrameAsync,@8")
#pragma comment(linker, "/export:BinkDoFrameAsyncMulti=bink2w64_original.BinkDoFrameAsyncMulti,@9")
#pragma comment(linker, "/export:BinkDoFrameAsyncWait=bink2w64_original.BinkDoFrameAsyncWait,@10")
#pragma comment(linker, "/export:BinkDoFramePlane=bink2w64_original.BinkDoFramePlane,@11")
#pragma comment(linker, "/export:BinkFindXAudio2WinDevice=bink2w64_original.BinkFindXAudio2WinDevice,@12")
#pragma comment(linker, "/export:BinkFreeGlobals=bink2w64_original.BinkFreeGlobals,@13")
#pragma comment(linker, "/export:BinkGetError=bink2w64_original.BinkGetError,@14")
#pragma comment(linker, "/export:BinkGetFrameBuffersInfo=bink2w64_original.BinkGetFrameBuffersInfo,@15")
#pragma comment(linker, "/export:BinkGetGPUDataBuffersInfo=bink2w64_original.BinkGetGPUDataBuffersInfo,@16")
#pragma comment(linker, "/export:BinkGetKeyFrame=bink2w64_original.BinkGetKeyFrame,@17")
#pragma comment(linker, "/export:BinkGetPlatformInfo=bink2w64_original.BinkGetPlatformInfo,@18")
#pragma comment(linker, "/export:BinkGetRealtime=bink2w64_original.BinkGetRealtime,@19")
#pragma comment(linker, "/export:BinkGetRects=bink2w64_original.BinkGetRects,@20")
#pragma comment(linker, "/export:BinkGetSummary=bink2w64_original.BinkGetSummary,@21")
#pragma comment(linker, "/export:BinkGetTrackData=bink2w64_original.BinkGetTrackData,@22")
#pragma comment(linker, "/export:BinkGetTrackID=bink2w64_original.BinkGetTrackID,@23")
#pragma comment(linker, "/export:BinkGetTrackMaxSize=bink2w64_original.BinkGetTrackMaxSize,@24")
#pragma comment(linker, "/export:BinkGetTrackType=bink2w64_original.BinkGetTrackType,@25")
#pragma comment(linker, "/export:BinkGoto=bink2w64_original.BinkGoto,@26")
#pragma comment(linker, "/export:BinkLogoAddress=bink2w64_original.BinkLogoAddress,@27")
#pragma comment(linker, "/export:BinkNextFrame=bink2w64_original.BinkNextFrame,@28")
#pragma comment(linker, "/export:BinkOpen=bink2w64_original.BinkOpen,@29")
#pragma comment(linker, "/export:BinkOpenDirectSound=bink2w64_original.BinkOpenDirectSound,@30")
#pragma comment(linker, "/export:BinkOpenMiles=bink2w64_original.BinkOpenMiles,@31")
#pragma comment(linker, "/export:BinkOpenTrack=bink2w64_original.BinkOpenTrack,@32")
#pragma comment(linker, "/export:BinkOpenWaveOut=bink2w64_original.BinkOpenWaveOut,@33")
#pragma comment(linker, "/export:BinkOpenWithOptions=bink2w64_original.BinkOpenWithOptions,@34")
#pragma comment(linker, "/export:BinkOpenXAudio2=bink2w64_original.BinkOpenXAudio2,@35")
#pragma comment(linker, "/export:BinkOpenXAudio27=bink2w64_original.BinkOpenXAudio27,@36")
#pragma comment(linker, "/export:BinkOpenXAudio28=bink2w64_original.BinkOpenXAudio28,@37")
#pragma comment(linker, "/export:BinkPause=bink2w64_original.BinkPause,@38")
#pragma comment(linker, "/export:BinkRegisterFrameBuffers=bink2w64_original.BinkRegisterFrameBuffers,@39")
#pragma comment(linker, "/export:BinkRegisterGPUDataBuffers=bink2w64_original.BinkRegisterGPUDataBuffers,@40")
#pragma comment(linker, "/export:BinkRequestStopAsyncThread=bink2w64_original.BinkRequestStopAsyncThread,@41")
#pragma comment(linker, "/export:BinkRequestStopAsyncThreadsMulti=bink2w64_original.BinkRequestStopAsyncThreadsMulti,@42")
#pragma comment(linker, "/export:BinkService=bink2w64_original.BinkService,@43")
#pragma comment(linker, "/export:BinkSetError=bink2w64_original.BinkSetError,@44")
#pragma comment(linker, "/export:BinkSetFileOffset=bink2w64_original.BinkSetFileOffset,@45")
#pragma comment(linker, "/export:BinkSetFrameRate=bink2w64_original.BinkSetFrameRate,@46")
#pragma comment(linker, "/export:BinkSetIO=bink2w64_original.BinkSetIO,@47")
#pragma comment(linker, "/export:BinkSetIOSize=bink2w64_original.BinkSetIOSize,@48")
#pragma comment(linker, "/export:BinkSetMemory=bink2w64_original.BinkSetMemory,@49")
#pragma comment(linker, "/export:BinkSetOSFileCallbacks=bink2w64_original.BinkSetOSFileCallbacks,@50")
#pragma comment(linker, "/export:BinkSetPan=bink2w64_original.BinkSetPan,@51")
#pragma comment(linker, "/export:BinkSetSimulate=bink2w64_original.BinkSetSimulate,@52")
#pragma comment(linker, "/export:BinkSetSoundOnOff=bink2w64_original.BinkSetSoundOnOff,@53")
#pragma comment(linker, "/export:BinkSetSoundSystem=bink2w64_original.BinkSetSoundSystem,@54")
#pragma comment(linker, "/export:BinkSetSoundSystem2=bink2w64_original.BinkSetSoundSystem2,@55")
#pragma comment(linker, "/export:BinkSetSoundTrack=bink2w64_original.BinkSetSoundTrack,@56")
#pragma comment(linker, "/export:BinkSetSpeakerVolumes=bink2w64_original.BinkSetSpeakerVolumes,@57")
#pragma comment(linker, "/export:BinkSetVideoOnOff=bink2w64_original.BinkSetVideoOnOff,@58")
#pragma comment(linker, "/export:BinkSetVolume=bink2w64_original.BinkSetVolume,@59")
#pragma comment(linker, "/export:BinkSetWillLoop=bink2w64_original.BinkSetWillLoop,@60")
#pragma comment(linker, "/export:BinkShouldSkip=bink2w64_original.BinkShouldSkip,@61")
#pragma comment(linker, "/export:BinkStartAsyncThread=bink2w64_original.BinkStartAsyncThread,@62")
#pragma comment(linker, "/export:BinkUtilCPUs=bink2w64_original.BinkUtilCPUs,@63")
#pragma comment(linker, "/export:BinkUtilFree=bink2w64_original.BinkUtilFree,@64")
#pragma comment(linker, "/export:BinkUtilMalloc=bink2w64_original.BinkUtilMalloc,@65")
#pragma comment(linker, "/export:BinkUtilMutexCreate=bink2w64_original.BinkUtilMutexCreate,@66")
#pragma comment(linker, "/export:BinkUtilMutexDestroy=bink2w64_original.BinkUtilMutexDestroy,@67")
#pragma comment(linker, "/export:BinkUtilMutexLock=bink2w64_original.BinkUtilMutexLock,@68")
#pragma comment(linker, "/export:BinkUtilMutexLockTimeOut=bink2w64_original.BinkUtilMutexLockTimeOut,@69")
#pragma comment(linker, "/export:BinkUtilMutexUnlock=bink2w64_original.BinkUtilMutexUnlock,@70")
#pragma comment(linker, "/export:BinkUtilSoundGlobalLock=bink2w64_original.BinkUtilSoundGlobalLock,@71")
#pragma comment(linker, "/export:BinkUtilSoundGlobalUnlock=bink2w64_original.BinkUtilSoundGlobalUnlock,@72")
#pragma comment(linker, "/export:BinkWait=bink2w64_original.BinkWait,@73")
#pragma comment(linker, "/export:BinkWaitStopAsyncThread=bink2w64_original.BinkWaitStopAsyncThread,@74")
#pragma comment(linker, "/export:BinkWaitStopAsyncThreadsMulti=bink2w64_original.BinkWaitStopAsyncThreadsMulti,@75")
#pragma comment(linker, "/export:RADTimerRead=bink2w64_original.RADTimerRead,@76")

DWORD WINAPI LoadHaloAPThread(LPVOID module)
{
    //cast back to HMODULE
    HMODULE hModule = static_cast<HMODULE>(module);
    
    // Get the directory this proxy DLL lives in
    char path[MAX_PATH];
    //where path is going to be stored
        
    GetModuleFileNameA(hModule, path, MAX_PATH);
    //get full path on disk for this modlue
        
    char* lastSlash = strrchr(path, '\\');
    //get the last \\ before the file name, EX if path is C: ...\\MCC\\bink2w64.dll it will get the \\ before bink2w64
    
    //stores if strcat_fails
    bool failed = false;
        
    if (lastSlash) {
        //if we fould the last backslash
            
        *(lastSlash + 1) = '\0';
        //get the character right after the \\ and change it to a null terminator so its the end
        
        if (strcat_s(path, "HaloAP.dll") != 0)
        {
            failed = true;
            //set to true, strcat failed
        }
        //add HaloAP.dll to the end right after the last \\ //
    }

    HMODULE hap = nullptr;
    
    //load the library
    //only if strcat succeeded
    if (!failed)
    {
         hap = LoadLibraryA(path);
    }
        
    //if loading by full path fails try with just the filename
    if (!hap) 
        {
        hap = LoadLibraryA("HaloAP.dll");
    }
    
    //if both attempts fail
    if (!hap)
    {
    MessageBoxA(nullptr, "Failed to load HaloAP.dll, check that HaloAP.dll is with the AP Launcher, redownload the mod if not. Otherwise report in the discord thread", "Error Loading AP Mod", MB_OK | MB_ICONERROR);
    }
    
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    //Notes for me
    //hModule is a handle to this dll, reason is why DllMain is being called, reserved is not relevent for the code here
    if (reason == DLL_PROCESS_ATTACH) {
        //filters to just the process attached event
        
        DisableThreadLibraryCalls(hModule);
        //tell windows to stop calling this dllMain for thread attach/detach events
        
        //dummy hmodule
        HMODULE dummy;
        //Pin DLL
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCSTR>(hModule), &dummy))
        {
            //error pinning
        }
        
        //create thread to load haloap.dll
        HANDLE hThread = CreateThread(nullptr, 0,LoadHaloAPThread, hModule, 0, nullptr);
        if (!hThread)
        {
            //error creating thread
        }
        else
        {
            CloseHandle(hThread);
        }
    }
    
    return TRUE;
    //tell the loader the dll loaded correctly
}

