#include "review.h"
// Add a vectored exception handler to catch the crash address
    static LONG WINAPI CrashCatcher(EXCEPTION_POINTERS* ep)
    {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            void* crashAddr = ep->ExceptionRecord->ExceptionAddress;
            uint8_t* exeBase = (uint8_t*)GetModuleHandleA(nullptr);
            size_t offset = (size_t)crashAddr - (size_t)exeBase;
            printf("[CRASH] AV at exe+0x%zX\n", offset);

            // Capture stack trace
            void* frames[20] = {};
            USHORT count = RtlCaptureStackBackTrace(0, 20, frames, nullptr);
            for (USHORT i = 0; i < count; i++)
            {
                HMODULE hMod = nullptr;
                char name[64] = "?";
                size_t off = 0;
                if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)frames[i], &hMod))
                {
                    char path[MAX_PATH];
                    if (GetModuleFileNameA(hMod, path, sizeof(path)))
                    {
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
    void DumpUObjectSearch()
    {
        uint8_t* exe = (uint8_t*)GetModuleHandleA(nullptr);
        uint8_t* gArray = exe + GUOBJECTARRAY_OFFSET;
        void** Objects = *(void***)(gArray + 0x10);
        int numElements = *(int*)(gArray + 0x24);
        int numChunks = *(int*)(gArray + 0x2C);
        if (!Objects || numElements <= 0) return;

        // Find SetVisibility UFunction (outer=Widget)
        void* setVisFunc = nullptr;
        for (int i = 0; i < numElements; i++)
        {
            int c = i / 0x10000, n = i % 0x10000;
            if (c >= numChunks || !Objects[c]) continue;
            void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
            if (!obj) continue;
            __try
            {
                char name[64] = {};
                ResolveFName(exe, obj, 0x18, name, sizeof(name));
                if (strcmp(name, "SetVisibility") != 0) continue;
                void* outer = *(void**)((uint8_t*)obj + 0x20);
                char outerName[64] = {};
                if (outer) ResolveFName(exe, outer, 0x18, outerName, sizeof(outerName));
                if (strcmp(outerName, "Widget") == 0)
                {
                    setVisFunc = obj;
                    printf("[UE4] SetVisibility UFunction at %p\n", obj);
                    break;
                }
            }
            __except (1)
            {
            }
        }
        if (!setVisFunc)
        {
            printf("[UE4] SetVisibility not found\n");
            return;
        }

        // Find WBP_MCCMenuButton_C class
        void* btnClass = nullptr;
        for (int i = 0; i < numElements; i++)
        {
            int c = i / 0x10000, n = i % 0x10000;
            if (c >= numChunks || !Objects[c]) continue;
            void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
            if (!obj) continue;
            __try
            {
                void* cls = *(void**)((uint8_t*)obj + 0x10);
                char cn[128] = {};
                if (ResolveFName(exe, cls, 0x18, cn, sizeof(cn)) &&
                    strcmp(cn, "WBP_MCCMenuButton_C") == 0)
                {
                    btnClass = cls;
                    break;
                }
            }
            __except (1)
            {
            }
        }
        if (!btnClass)
        {
            printf("[UE4] No button class\n");
            return;
        }

        // Collapse ALL live instances
        typedef void (__fastcall *ProcessEventFn)(void* obj, void* func, void* parms);
        printf("[UE4] Collapsing ALL menu buttons...\n");
        int collapsed = 0;

        for (int i = 0; i < numElements; i++)
        {
            int c = i / 0x10000, n = i % 0x10000;
            if (c >= numChunks || !Objects[c]) continue;
            void* obj = *(void**)((uint8_t*)Objects[c] + (n * 0x18));
            if (!obj) continue;
            __try
            {
                if (*(void**)((uint8_t*)obj + 0x10) != btnClass) continue;
                char on[128] = {};
                ResolveFName(exe, obj, 0x18, on, sizeof(on));
                if (!strstr(on, "_C_")) continue;

                char* numStr = strstr(on, "_C_") + 3;
                int btnNum = atoi(numStr);

                if (btnNum == 22 || btnNum == 24)
                {
                    uint8_t params[16] = {};
                    params[0] = 1;
                    uint64_t* vtable = *(uint64_t**)obj;
                    ProcessEventFn pe = (ProcessEventFn)vtable[0x40];
                    pe(obj, setVisFunc, params);
                    printf("[UE4]   COLLAPSED '%s'\n", on);
                    collapsed++;
                }
                else
                {
                    printf("[UE4]   skipped '%s'\n", on);
                }
            }
            __except (1)
            {
            }
        }
        printf("[UE4] Collapsed %d buttons. Check screen.\n", collapsed);
        printf("[UE4] === Done ===\n");
    }