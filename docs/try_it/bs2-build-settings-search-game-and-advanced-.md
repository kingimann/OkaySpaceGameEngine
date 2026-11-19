---
id: bs2
row: | bs2 | Build Settings: search, Game and Advanced pages, checks, history | Editor: File > Build Settings (Ctrl+B) | New in this change; the editor and player were compile-checked on Linux but not run on Windows |
---
## bs2. Build Settings: search, Game and Advanced pages, checks, history

**What it is.** The Build Settings window has a search box over every setting, a dot beside each page that has changed settings, a "Reset page to defaults" button, two new pages (Game and Advanced), a "What will happen" summary with checks and Fix buttons, Debug and Release starting points, duplicate profile, `.okaybuild` files and a build history. Every new setting is read by the built game or by the build step (see `docs/build_settings_upgrade.md`).

**Steps.**
1. Open the editor, open a project, press Ctrl+B (File > Build Settings). The platform must be Desktop.
2. Type `fps` in the search box at the top left. Click "Frame rate cap (Display)". You land on the Display page.
3. Change the window width. A blue dot appears beside Display. Click "Reset page to defaults" and confirm; the width goes back and the dot goes away.
4. Open the Game page. Set Game speed to 0.5, Log detail to "Warnings", tick "Write a log file", type `de` as Starting language, and under Description type a sentence. Choose an output folder and press Build & Run.
5. In the running game everything moves at half speed. Close it. Open the save folder of the game (the per-user app folder, named after the company and the game): `game.log` is there. In the build's `<Name>_Data/game.okayconfig` you see `time_scale=0.5`, `log_level=warn`, `log_file=game.log`, `default_language=de`.
6. Open the Advanced page. Turn on "Allow build commands", type `echo built {name} {version}` after the build, build again. The Console says "ran your command after the build". With the switch off, nothing runs.
7. Still on Advanced: press "Create or update .gitignore" (a `.gitignore` appears in the project) and look at the Build history list (time, version, size, "Open output folder").
8. At the bottom, "What will happen" lists the build in plain words. Make the name `Bad:Name`, tick Full screen and Borderless together: Check lines appear with Fix buttons. Press Fix and they go away.
9. Under the profile bar press Debug or Release (changes the developer panel, FPS counter, stripping, compression, logging). Save a profile, press Duplicate profile, then "Save as file": `BuildProfiles/<name>.okaybuild` appears in the project; "Load file..." brings it back.

**What you should see.** As in each step. Defaults write nothing new: a project that never touched the Game or Advanced pages builds exactly the `game.okayconfig` it built before.

**Status.** Tested on Linux: the model (pages, search, reset, presets, summary, checks and fixes, history, commands, ignore file, profile files) and the player's config reader (`tests/test_build_settings.cpp`), and every new setting through the profile file (`tests/test_project_state.cpp`). `editor/main.cpp` and `player/main.cpp` were compile-checked (syntax only) on Linux. NOT run: the window on screen, a Windows build, and the player reading the config in a real game.
