#pragma once

class WinsockGuard
{
public:
    WinsockGuard();
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;
    bool ok() const;
    ~WinsockGuard();
    private:
    bool initialized = false;
    
};
