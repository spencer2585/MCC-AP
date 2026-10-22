## Module Responsibilities
game has its own module; this module's responsibility is to handle anything pertaining to that specific game.  
This includes but is not limited to:
- Game-Specific Items
- Hooks on the game engine
- Detecting when to send locations
- Detecting a death for deathlink and killing the player when one is received

Each Module is separate from core and other modules; core will be able to function without a game's module.  
Modules can communicate with core and the server using two provided interfaces. 

IMPORTANT: Functionality surrounding skulls is not finalized or documented.

  

## Shared Tools
IGameModule is the interface all game modules must build off and includes all methods core may call on a game's module  
IGameEvents Includes all calls you can make to core.  
All modules can use the built-in log function available by adding #include log.h and called with haloap::Log. The log function will output to the console in debug mode and write to a log file with the line and file where the log call was made (Logging to a file is not currently implemented)  
Modules do not need to initialize minhooks, it is initialized on startup  
All modules can use PatternScan, which takes an HModule and a bit pattern and will return the location of the first result. This should be used for finding all hooks to allow them to work across all builds. Note that your pattern MUST be unique, or it can return the wrong location. All uses of PatternScan MUST check for a nullptr

## Module Lifetime
Upon receiving slot Data core will initialize all modules for included games with slot data and a handle to a game event sink  
Core will detect when the dll for your game is started, upon detection it will call your InstallHooks  
When the games engine stops it will call the modules UninstallHooks.  
The Install/Uninstall cycle can happen several times over the course of the modules lifetime

## Game Info
Game info is a struct that each module should have a variation of.  
The struct can be found in the games directory in GameInfo.h  
NOTE: THE GAME INFO STRUCT IS NOT FINALIZED

### int Prefix
This is the value of the game prefix for the modules items and locations

### String Name
The name of the modules game

## IGameModule
These are the methods a module MUST implement, these can and will be called by core  
  
### Constructor(SlotData, *GameEvents)  
When creating a module core will pass in 2 pieces of data. First is the slotData for the slot, it will contain all the slotData not just data specific to the modules game  
Second is a handle to a GameEvents Object, this will be the object the module uses to make calls to core.

### bool InstallHooks()  
Core will call this when the modules engine dll is loaded, it should handle installing all needed hooks for the modules games
returns a bool, 1 for successful installation, return 0 if there is an error

### bool UninstallHooks()  
Core will call this when the engine dll for the modules game is unloaded.  
It should handle uninstalling all hooks installed by the module.
This MUST fully reverse Install hooks, any hook left installed after the engine unloads will crash the game  
Return 1 for success or 0 for failure

### void SendItem(int localId, int count)  
fires when an item for your game is received. the itemID will be stripped of its game identifier, see docs on item/location ID scheme for more detail on the id scheme  
count is the total amount this slot should have of the item total. module should replace any count of an item with the count passed

### void ReceiveDeathlink()  
called when a deathlink is received, the module should kill the player at the next valid moment

### GameInfo GetGameInfo()  
is called shortly after the module is created  
returns a GameInfo object that contains all info about the module that the core needs

### bool IsMissionAllowed(int missionID)  
Called when populating mission select  
Core will pass in the id of the mission and the module should return if the mission is unlocked or not.  
Return True to allow, False to lock

### int GetMissionCount()  
Return how many missions are in the game the module handles

### destructor()  
Should ensure all hooks are uninstalled before cleanly deconstructing

## IGameEvents
This Interface contains all methods a module can use to pass/ask for data from core  
GameEvents object is passed to a module during construction

### void SendLocation(int locationID)  
Send a location out, Core will add the game identifier to the location ID after you pass it, see docs on ID scheme for more detail on the game identifier

### void SendDeathlink()  
Should be called if deathlink is enabled and the player dies, sends out a deathlink