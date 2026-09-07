#pragma once

struct IGameEvents;

struct IGameModule
{
    virtual bool InstallHooks(IGameEvents* events) = 0;
    
    virtual bool UninstallHooks() = 0;
    
    virtual void SendItem(int localID)= 0;
    
    virtual void ReceiveDeathlink() = 0;
    
    virtual GameInfo GetGameInfo() = 0;
    
    virtual MissionList GetMissionList() = 0;
    
    virtual SkullData GetSkullData() = 0;
    
    virtual ~IGameModule() = default;
};
