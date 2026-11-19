---
id: 87
row: | 87 | Project upgrade and saving fixes | Launcher project cards; File > Project Upgrade...; example `examples\old_project` | Tested headless (project_version, project_upgrade, project_state); the launcher and editor windows not run, Windows not run |
---
## 87. Project upgrade and saving fixes

**What it is.** The launcher's project cards used to keep saying "older" after you upgraded the project (they were read once and never again), and a plain Open then even picked the old editor from that stale card. They now follow the file. Also fixed: File > Project Upgrade... upgraded the wrong project (or none) when the open scene was a loose file from somewhere else; saving a scene into another project no longer marks that project "up to date"; a zip name with a `\`, `|` or `=` in the build settings came back changed.

**Steps.**
1. Copy the folder `examples\old_project` somewhere you can write (for example your Desktop). It is a small project made by an old version (`engine_version=2.1.0`).
2. In the launcher, open the Projects page and use "Open a project folder" on the copy. The card shows an "older" badge.
3. The editor opens and asks to upgrade. Press **Upgrade**, wait for "Done", press **Continue**.
4. Go back to the launcher window (leave it open the whole time). Within about a second the card's version changes to this editor's version and the "older" badge goes away.
5. In the editor open **File > Project Upgrade...**. It says "Up to date." Close the editor and press Open on the card again: no question is asked.
6. Optional: open a scene from another folder (File > Open), then File > Project Upgrade... Before the fix this did nothing or touched the wrong project. It now works on the project shown in the Project panel.

**What you should see.** The card, the "Last saved by" line in the upgrade window and the next Open all agree, with no restart of the launcher. A backup folder "old_project (backup v2.1.0)" appears next to the project.

**Status.** The version logic is covered by tests that run on the build machine (`test_project_version`, `test_project_upgrade`, `test_project_state`). The launcher and editor windows were not run here and Windows was not run: unverified.
