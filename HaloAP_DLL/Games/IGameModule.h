#pragma once

#include "GameInfo.h"

struct IGameEvents;

struct IGameModule
{
    virtual bool InstallHooks() = 0;
    
    virtual bool UninstallHooks() = 0;
    
    virtual void SendItem(int localID, int count)= 0;
    
    virtual void ReceiveDeathlink() = 0;
    
    virtual GameInfo GetGameInfo() = 0;
    
    virtual bool IsMissionAllowed(int missionID) = 0;
    
    virtual int GetMissionCount() = 0;
    
    //TODO handle skulls
    
    virtual ~IGameModule() = default;
};
