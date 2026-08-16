# Randomization Documentation

This document describes what is randomized in the current release (1.2.0).

## Halo: Combat Evolved

### Goal
Beat the final mission configured in your yaml. Final mission is unlocked by beating all other missions

### Locations
**Missions:** Beating each mission sends a location

**Skulls:** Collecting each skull in a mission sends a location

**Chapters:** Reaching each chapter in a mission sends a location
(A chapter is what I call the moments when black bars appear at the top and bottom of the screen and a title appears at the bottom)
[Chapter Example](docs/assets/chapter_example.jpg)

### Items
**Mission Unlocks:** Each Mission is locked until you get the item to unlock it, a starting  mission is unlocked at the start. Goal mission is locked behind completing all other missions

**Skulls:** Skulls are locked behind an item that allows you to toggle them on or off. Full skull behavior and item amount is decided by the skullsanity option in the yaml

### Locking behavior
Locked missions will not appear in the mission select. Locked Skulls will show a locked icon in the skull select menu
