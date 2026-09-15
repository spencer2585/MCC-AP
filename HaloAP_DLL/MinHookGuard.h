#pragma once

class MinHookGuard
{
    public:
        MinHookGuard();
        MinHookGuard(const MinHookGuard&) = delete;
        MinHookGuard& operator=(const MinHookGuard&) = delete;
        bool ok() const;
        ~MinHookGuard();
    
    private:
        bool m_initialized = false;
};
