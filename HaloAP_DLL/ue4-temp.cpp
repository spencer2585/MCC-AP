#include "ue4-temp.h"

// =================================================================
    // UE4 GUObjectArray diagnostic — paste into dllmain.cpp
    // Replace the existing DumpUObjectSearch function with this.
    // =================================================================
static const uintptr_t GUOBJECTARRAY_OFFSET = 0x3E389B0;
static const uintptr_t SCREEN_STATICCLASS = 0xBD6E50;
static const uintptr_t VM_STATICCLASS = 0xBDB2A4;
static const uintptr_t FNAME_TOSTRING_OFFSET = 0xD3BF38;

struct FString
{
    wchar_t* Data;
    int32_t Count;
    int32_t Max;
};

bool ResolveFName(uint8_t* exe, void* obj, int nameOffset, char* outBuf, int bufSize)
    {
        typedef void (*FNameToStringFn)(void* fname, FString* out);
        auto ToString = (FNameToStringFn)(exe + FNAME_TOSTRING_OFFSET);
        FString result = {};
        __try
        {
            ToString((uint8_t*)obj + nameOffset, &result);
            if (result.Data && result.Count > 0)
            {
                int len = (result.Count < bufSize - 1) ? result.Count : bufSize - 1;
                for (int i = 0; i < len; i++) outBuf[i] = (char)result.Data[i];
                outBuf[len] = 0;
                return true;
            }
        }
        __except (1)
        {
        }
        outBuf[0] = '?';
        outBuf[1] = 0;
        return false;
    }

    // =================================================================
    // UE4 GUObjectArray diagnostic — replace DumpUObjectSearch in dllmain.cpp
    // Keep the constants and ResolveFName helper from before.
    // =================================================================
    void ScanAndModifyWideString(const wchar_t* target, const wchar_t* replacement)
    {
        size_t targetLen = wcslen(target);
        size_t targetBytes = (targetLen + 1) * 2;
        size_t replLen = wcslen(replacement);

        printf("[SCAN] Searching for L\"%ls\" and replacing with L\"%ls\"...\n", target, replacement);

        MEMORY_BASIC_INFORMATION mbi;
        uint8_t* addr = nullptr;
        int found = 0;

        while (VirtualQuery(addr, &mbi, sizeof(mbi)))
        {
            if (mbi.State == MEM_COMMIT &&
                (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
                !(mbi.Protect & PAGE_GUARD))
            {
                uint8_t* base = (uint8_t*)mbi.BaseAddress;
                size_t size = mbi.RegionSize;

                __try
                {
                    for (size_t off = 0; off + targetBytes <= size; off += 2)
                    {
                        if (memcmp(base + off, target, targetBytes) == 0)
                        {
                            printf("[SCAN]   FOUND+MODIFIED at %p\n", base + off);
                            wchar_t* ws = (wchar_t*)(base + off);
                            for (size_t c = 0; c <= replLen; c++) ws[c] = replacement[c];
                            found++;
                            if (found > 30) goto done;
                        }
                    }
                }
                __except (1)
                {
                }
            }
            addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
            if ((uintptr_t)addr < (uintptr_t)mbi.BaseAddress) break;
        }
    done:
        printf("[SCAN] Modified %d occurrences.\n", found);
    }