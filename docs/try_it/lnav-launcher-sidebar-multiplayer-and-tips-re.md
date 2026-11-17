---
id: lnav
row: | lnav | Launcher sidebar: Multiplayer and Tips removed | Open the launcher (`OkaySpace.exe`) and look at the left sidebar | Unverified: edited from the code only, the launcher was not run |
---
## lnav. Launcher sidebar: Multiplayer and Tips removed

**What it is.** The owner asked for "Multiplayer" and "Tips" to be removed from the launcher. Their two entries are taken out of the left sidebar. The pages and the code stay (nothing else was deleted), so a party invite or a link that opens the Multiplayer page still works, and the entries can be put back by restoring two lines.

**Steps.**
1. Update to the release that carries this change and open the launcher (`OkaySpace.exe`).
2. Look at the left sidebar: under MENU there is no "Multiplayer" row, and under LEARN there is no "Tips" row ("What's New" is still there). "Servers" under SOCIAL is not changed.

**What you should see.** The sidebar without those two entries and everything else working as before.

**Status.** Unverified: the change is two deleted lines in `launcher/main.cpp`; the launcher was not built or run here. CI compiles it.
