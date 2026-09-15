#include "ConsoleGuard.h"
#include "logging/console.h"

ConsoleGuard::ConsoleGuard()
{
    haloap::SetupConsole();
}

ConsoleGuard::~ConsoleGuard()
{
    haloap::TeardownConsole();
}
