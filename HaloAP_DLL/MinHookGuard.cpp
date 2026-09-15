#include "MinHookGuard.h"
#include "minhook/MinHook.h"
#include "logging/log.h"

using haloap::Log;

MinHookGuard::MinHookGuard()
{
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK)
    {
        m_initialized = false;
        Log("MH_Initialize failed: {}", static_cast<int>((mhStatus)));
    }
    else
    {
        m_initialized = true;
        Log("Minhook Initialized");
    }
}

MinHookGuard::~MinHookGuard()
{
    if (m_initialized)
    {
        Log("Removing Minhooks");
        MH_Uninitialize();
    }
}

bool MinHookGuard::ok() const
{
    return m_initialized;
}
