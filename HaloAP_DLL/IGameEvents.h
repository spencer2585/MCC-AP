#pragma once

struct IGameEvents
{
    virtual void SendLocation(int locationID) = 0;
    
    virtual void SendDeathlink() = 0;
    
    virtual ~IGameEvents() = default;
};
