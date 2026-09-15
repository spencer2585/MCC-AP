#pragma once

class ConsoleGuard
{
public:
    ConsoleGuard();
    ConsoleGuard(const ConsoleGuard&) = delete;
    ConsoleGuard& operator=(const ConsoleGuard&) = delete;
    ~ConsoleGuard();
};