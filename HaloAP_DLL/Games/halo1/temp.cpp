#include "temp.h"

void InstallHalo1Hooks()
{
    if (!haloap::InstallMissionCompleteHook(g_pipe))
    {
        printf("Failed to install mission-complete hook.\n");
    }
    haloap::InstallMissionIdLookupHook(g_pipe);
    haloap::InstallMissionLoadHook(g_pipe);
    haloap::InstallLoadLevelSoloHook(g_pipe);
    haloap::InstallChapterTitleHook(g_pipe);
    haloap::InstallSkullHook(g_pipe);
}

void UninstallHalo1Hooks()
{
    haloap::UninstallLoadLevelSoloHook();
    haloap::UninstallMissionLoadHook();
    haloap::UninstallMissionCompleteHook();
    haloap::UninstallMissionIdLookupHook();
    haloap::UninstallChapterTitleHook();
    haloap::UninstallSkullHook();
}

void UninstallVtableHooks()
{
    haloap::UninstallShellCommandHook();
    haloap::UninstallShellLevelLoadHook();
}