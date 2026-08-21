# Setup Guide

1. Download the zip file from the github and extract to a location of your choice.
2. Generate a game
3. Run HaloAP_Launcher.exe
4. Enter your room info
5. On first launch, the launcher will prompt for your MCC install directory.

    **Steam:** Right-click Halo: MCC in your library → Manage → Browse local files. Copy the folder path.

    **Windows Store / Game Pass:** Default path is `C:/XboxGames/Halo- The Master Chief Collection/content`. If you changed your install location, navigate there and use the `content` folder.

    **To change the directory later:** Edit `haloap_config.txt` or delete it to be prompted again on next launch.
6. Play the game
> [!WARNING]
> **Do not close the launcher console window before quitting the game.**
>
> When you quit the game, it sends a cleanup signal to the launcher, which removes the mod files and restores your vanilla install. If you close the launcher first, the mod files remain in your game directory and:
> - The game won't launch normally
> - Future AP attempts may fail
>
> If this happens, verify your game install:
> - **Steam:** Right-click Halo: MCC → Properties → Installed Files → Verify integrity
> - **Windows Store:** Windows Settings → Apps → Halo MCC → Advanced options → Repair