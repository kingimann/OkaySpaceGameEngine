# Build Settings upgrade

> **What this is:** Owner request: "Building settings needs improvements.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `editor/`, paths and links checked; the text itself was not re-checked against the code).

Owner request: "Building settings needs improvements. Much more settings."

This page lists what the Build Settings window (File > Build Settings, Ctrl+B) had, what the built game really
honours, what a game maker expects, and what this change added. Rule: a setting is shown only if the built game
or the build step really uses it, and each one has a test. Written from our own design; no code was taken from
another project.

How to try it: `docs/try_it/bs2-build-settings-search-game-and-advanced-.md` (also in `docs/try_it.md`).

## Where the code is

| File | What it holds |
| --- | --- |
| `editor/ProjectState.hpp` | `BuildSettings` (every setting), the profile text (`.okay/build_profiles.cfg`, `.okay/build_live.cfg`, `.okaybuild` files). New fields are at the end of the struct and of the read and write lists, so the save/load work on `claude/project-upgrade-save-1` merges easily. |
| `editor/BuildSettingsModel.hpp` | The logic, no ImGui: pages, the registry of settings (search, changed-from-default, reset page), presets, the summary, the checks and Fix actions, history, build commands, the `.gitignore` merge, profile files. |
| `editor/BuildSettingsWindow.hpp/.cpp` | The drawing: left list with search and dots, page header, the Game and Advanced pages, the summary panel, profile extras. `main.cpp` calls these in six places. |
| `engine/include/okay/Core/BuildConfigExtra.hpp` | The extra `game.okayconfig` keys: written by the build step, read by the player. Only non-default values are written. |
| `tests/test_build_settings.cpp`, `tests/test_project_state.cpp` | Tests. |

Not moved yet: the bodies of the older pages (Scenes, Player, Display, Graphics, Audio & Input, Output, Server,
Xbox) are still inside `editor/main.cpp`. They need many of its helpers, so moving them is a separate change.
The Xbox page logic was not touched.

## Existing / Gap

"Honoured" means the player or the build step reads it. All "Existing" rows were checked against `editor/main.cpp`
(`builder::Build`, `OptionsFromSettings`; since the split's move 1 they are in `editor/BuildGame.cpp`) and `player/main.cpp` (the `game.okayconfig` reader).

| Area | Existing (honoured) | What a game maker expects and was missing | After this change |
| --- | --- | --- | --- |
| General | Product name, company, version, identifier, icon, splash image/time/colour, build number with auto count-up, development build, FPS counter, save to user folder | Description, default language | Description (config and start-up log line), Starting language (Game page). Not done: choice of loading screen style (the project's loading screen is carried over already). |
| Scenes | Scene list with ticks and order (first ticked starts the game), include all scenes | Drag reorder | Unchanged (list order is by the existing arrows). Drag reorder added. |
| Window and Display | Width, height, windowed / borderless / fullscreen, resizable, minimum size, start maximized, vsync, frame rate cap, smooth frame time, Alt+Enter / F11, run in background, mute when unfocused | Aspect lock, DPI mode, start monitor, exclusive fullscreen, custom cursor | Unchanged. Not done: aspect lock, DPI mode, start monitor, exclusive fullscreen, custom cursor. "Pause on focus loss" is the same as the existing "Keep running when not focused". |
| Graphics | Quality preset (Low to Ultra), GPU renderer, Direct3D 12 preview, per-pixel lighting, shadows (distance, softness, cascades, resolution), bloom, ambient occlusion, FXAA, supersampling | Render scale, texture quality, max lights, particle cap, fog, renderer choice and fallback order | 3D renderer choice and Particle limit added (see below). Not done: render scale, texture quality (mip bias), max lights, fog, renderer fallback ORDER (the order is fixed: chosen backend, then software). The engine has no player-side switch for them, so they are not shown. |
| Audio | Master volume, mute when unfocused | Music and effects defaults, sample rate, voices | Music volume and Sound effects volume (Game page; the player's own saved choice still wins). Sound quality (sample rate) added. Not done: speaker mode, voice limit. |
| Input | Quit on Escape, hide cursor, lock cursor (none / locked / confined) | Gamepad deadzone, mouse sensitivity, default actions asset, touch controls | Gamepad dead zone added. Not done: mouse sensitivity (each controller component keeps its own), default actions asset, touch controls. |
| Physics and Time | Physics rate and smoothing come from Project Settings | Game speed, max steps per frame, default gravity | Game speed and Most physics steps per frame (Game page). Not done: default gravity (each scene has its own). |
| Player / Game | Development build panel, FPS counter, save to user folder | Log level, log file, crash reports, pause and quit keys, autosave count | Log detail and Write a log file (Game page). Not done: crash handler switch and folder (the player does not install `shared/CrashHandler.hpp` yet), key remapping, autosave count. "Pause on focus loss" and "Run in background" already exist (the same switch). |
| Networking | Dedicated server page (name, port, players, tick, password, sign-in, listing, anti-cheat limits) | Default server address, relay, anti-cheat for the client | Unchanged (server page). Not done for the desktop game. |
| Scripting | none | Script debug, hot reload | Not done. |
| Assets and Size | Strip unused assets, compress the archive, encrypt data, tidy layout, StreamingAssets | Pack file, per-asset exclude list, estimated size | History and the summary show the last build size; Dry Run shows the size of the next one. Not done: pack file, exclude list, key made at build time. |
| Packaging | Output folder, layout (folder / compact / zip), archive name pattern, also make a zip, open folder after | Installer options, desktop shortcut | Not done. |
| Web, Android, Server, Xbox | Platform buttons; the web and Android exports write data and a script; Server and Xbox pages | Canvas size, memory, package name, SDK levels, permissions | Not done (only name, company, version, size, identifier, icon go into those exports). |
| Advanced | none | Environment variables, command-line arguments, defines, pre and post build commands, ignore file | Environment variables (Game page), pre and post build commands behind "Allow build commands" (off by default), `.gitignore` button (Advanced page). Not done: baked command-line arguments and custom defines (the player has no place that reads them yet). |
| Window features | Platform picker, section list, profiles (save, save as, delete), Dry Run, Build & Run, Build Result window, Problems list | Search, changed dots, reset page, presets, per-platform overrides, summary, checks with Fix, history, `.okaybuild` files | Search, dots, reset page, Debug and Release starts, duplicate profile, `.okaybuild` files, summary, checks with Fix, history. Drag reorder, coloured last-build log and Open output folder added. Not done: per-platform overrides. |

## What each new setting does

| Setting (page) | Written as | Honoured by |
| --- | --- | --- |
| Game speed | `time_scale` | `player/main.cpp`: `Time::SetTimeScale` after the session starts |
| Most physics steps per frame | `max_fixed_steps` | `player/main.cpp`: `GameLoop::Options::maxFixedSteps` |
| Music volume, Sound effects volume | `music_volume`, `sfx_volume` | `player/main.cpp`: `AudioMixer::groupVolume` before the player's saved volumes load |
| Starting language | `default_language` | `player/main.cpp`: tried first by `Localization::LoadSettings`; a language the player chose earlier wins |
| Log detail | `log_level` | `player/main.cpp`: `Log::Configure` |
| Write a log file | `log_file=game.log` | `player/main.cpp`: `Log::OpenFile` in the save folder |
| Description | `description` | `player/main.cpp`: a start-up log line |
| Environment variables | `env=NAME=VALUE` (one line each) | `player/main.cpp`: set before the rest of start-up. `PATH`, `LD_PRELOAD`, `DYLD_*` and similar are refused (at build and at read time) |
| Allow build commands, command before, command after | not in the game | `builder::Build`: before runs first (a failure stops the build), after runs after a good build; never in a Dry Run, never when the switch is off. `{output} {name} {version} {build} {project}` are filled in. |

| 3D renderer (Graphics) | `renderer` (1 Direct3D 11, 2 OpenGL, 3 software) | `player/main.cpp`: skips the other backends; software forced for 3; a backend that cannot start still falls back to software |
| Particle limit (Graphics) | `particle_cap` | `player/main.cpp` sets `ParticleSystem::GlobalCap()`, which `SafeMax()` (the pool size) obeys; Low and Medium quality presets set 100 and 300 |
| Gamepad dead zone (Audio & Input) | `gamepad_deadzone` | `player/main.cpp`: `Input::GamepadSettings(p).left/right.deadZone` for all four pads |
| Sound quality (Audio & Input) | `sample_rate` (22050, 44100, 48000) | `player/main.cpp`: the rate asked of the sound card (the card may still choose its own) |
| Files left out of the build (Output) | not in the game | `builder::Build`: matching files are not copied; a file a scene uses is reported as a warning. Project panel right-click "Exclude from Build" / "Include in Build" edits the same list |
| Scene drag reorder (Scenes) | `scene=` list order | the list in the Scenes page can be dragged; the first ticked scene still starts the game |
| Estimated size, last build log | not in the game | summary shows the last Dry Run size and the last build size; Advanced page shows the last build log, errors red, warnings yellow, with Open output folder |

Old projects: every new field defaults to the old behaviour and writes no line, so `game.okayconfig` of an
untouched project is byte for byte what it was (`tests/test_build_settings.cpp`, "extra lines").

## Tests

* `tests/test_build_settings.cpp`: every `BuildSettings` member is in the registry or on the short "not an option"
  list; changed dots; reset page (the scene list and build counter are never reset); search; pages per platform;
  presets; summary wording; every check and its Fix; history (cap, text round trip, hostile text); command words,
  allowed/dry-run rules and exit codes; `.gitignore` merge; profile files and duplicate; the extra config lines
  round trip through the player's reader, hostile and broken values are tamed, defaults write nothing.
* `tests/test_project_state.cpp`: the 12 new fields round trip in a profile (a field added without a test line fails).

## Known gaps and notes

* The profile text escapes `\`, `=` and `|` but not a newline; the window uses single-line boxes so no value has one.
  Fixing the save format belongs to the project save/load work.
* Game speed is applied when the game session starts. A scene load that resets the game clock may set it back to 1;
  unverified.
* Environment variables are set by the game itself. They do not reach the launcher that started it.
