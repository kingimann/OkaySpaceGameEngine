# Build Settings upgrade

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
(`builder::Build`, `OptionsFromSettings`) and `player/main.cpp` (the `game.okayconfig` reader).

| Area | Existing (honoured) | What a game maker expects and was missing | After this change |
| --- | --- | --- | --- |
| General | Product name, company, version, identifier, icon, splash image/time/colour, build number with auto count-up, development build, FPS counter, save to user folder | Description, default language | Description (config and start-up log line), Starting language (Game page). Not done: choice of loading screen style (the project's loading screen is carried over already). |
| Scenes | Scene list with ticks and order (first ticked starts the game), include all scenes | Drag reorder | Unchanged (list order is by the existing arrows). Drag reorder added. |
| Window and Display | Width, height, windowed / borderless / fullscreen, resizable, minimum size, start maximized, vsync, frame rate cap, smooth frame time, Alt+Enter / F11, run in background, mute when unfocused | Aspect lock, DPI mode, start monitor, exclusive fullscreen, custom cursor | Unchanged. Not done: aspect lock, DPI mode, start monitor, exclusive fullscreen, custom cursor. "Pause on focus loss" is the same as the existing "Keep running when not focused". |
| Graphics | Quality preset (Low to Ultra), GPU renderer, Direct3D 12 preview, per-pixel lighting, shadows (distance, softness, cascades, resolution), bloom, ambient occlusion, FXAA, supersampling | Render scale, texture quality, max lights, particle cap, fog, renderer choice and fallback order | 3D renderer choice and Particle limit added (see below). Render scale, max lights, fog off added in round 3. Not done: texture quality (mip bias: no renderer reads one), renderer fallback ORDER (the order is fixed: chosen backend, then software). The engine has no player-side switch for them, so they are not shown. |
| Audio | Master volume, mute when unfocused | Music and effects defaults, sample rate, voices | Music volume and Sound effects volume (Game page; the player's own saved choice still wins). Sound quality (sample rate) added. Max voices added in round 3. Not done: speaker mode. |
| Input | Quit on Escape, hide cursor, lock cursor (none / locked / confined) | Gamepad deadzone, mouse sensitivity, default actions asset, touch controls | Gamepad dead zone added. Mouse sensitivity added in round 3. Not done: default actions asset, touch controls (the player has no switch for them). |
| Physics and Time | Physics rate and smoothing come from Project Settings | Game speed, max steps per frame, default gravity | Game speed and Most physics steps per frame (Game page). Not done: default gravity (each scene has its own). |
| Player / Game | Development build panel, FPS counter, save to user folder | Log level, log file, crash reports, pause and quit keys, autosave count | Log detail and Write a log file (Game page). Crash reports switch and folder added in round 3. Not done: key remapping, autosave count. "Pause on focus loss" and "Run in background" already exist (the same switch). |
| Networking | Dedicated server page (name, port, players, tick, password, sign-in, listing, anti-cheat limits) | Default server address, relay, anti-cheat for the client | Unchanged (server page). Not done for the desktop game. |
| Scripting | none | Script debug, hot reload | Not done. |
| Assets and Size | Strip unused assets, compress the archive, encrypt data, tidy layout, StreamingAssets | Pack file, per-asset exclude list, estimated size | History and the summary show the last build size; Dry Run shows the size of the next one. Not done: pack file, exclude list, key made at build time. |
| Packaging | Output folder, layout (folder / compact / zip), archive name pattern, also make a zip, open folder after | Installer options, desktop shortcut | Installer script with install folder, licence, Start menu, desktop shortcut and uninstaller (Installer page, round 3). Not run: NSIS was not available. |
| Web, Android, Server, Xbox | Platform buttons; the web and Android exports write data and a script; Server and Xbox pages | Canvas size, memory, package name, SDK levels, permissions | Web: memory and compression; Android: package, API levels, direction, permissions (round 3, written into the generated script, manifest and Gradle file). Not done: web canvas size separate from the window, Android icons and signing. |
| Advanced | none | Environment variables, command-line arguments, defines, pre and post build commands, ignore file | Environment variables (Game page), pre and post build commands behind "Allow build commands" (off by default), `.gitignore` button (Advanced page). Baked arguments, custom defines and per-platform differences added in round 3. |
| Window features | Platform picker, section list, profiles (save, save as, delete), Dry Run, Build & Run, Build Result window, Problems list | Search, changed dots, reset page, presets, per-platform overrides, summary, checks with Fix, history, `.okaybuild` files | Search, dots, reset page, Debug and Release starts, duplicate profile, `.okaybuild` files, summary, checks with Fix, history. Drag reorder, coloured last-build log and Open output folder added. Per-platform differences for Web, Android and Server added in round 3 (Output page). |

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

### Round 3

| Setting (page) | Written as | Honoured by |
| --- | --- | --- |
| 3D picture size, render scale (Graphics) | `render_scale` (0.25 to 1) | `player/main.cpp`: the 3D frame is drawn at that share of the size and stretched to the window by the 2D copy. Not applied with a camera stack or a camera render target. UI stays sharp. Software, OpenGL and Direct3D renderers all get the smaller size from the same place; the stretch is untested on screen |
| Most lights at once (Graphics) | `max_lights` | New `SceneLights::LimitTo` (`engine/include/okay/Render/Lighting.hpp`), called by the player after the scene's lights are gathered. All three renderers read that one list. The sun always stays |
| Switch fog off (Graphics) | `fog_off` | `player/main.cpp` sets `scene.renderSettings.fog = false` every frame |
| Mouse sensitivity (Audio & Input) | `mouse_sensitivity` (0.1 to 5) | New `Input::LookScale()`; the first-person, third-person-shooter, free-roam and virtual-camera controllers multiply their own sensitivity by it. The plain third-person controller has no mouse look of its own |
| Most sounds at once (Audio & Input) | `max_voices` | `AudioMixer::maxVoices`: when more sources sound, `PickLoudest` keeps the loudest (volume x group level x distance); the others wait that buffer |
| Crash reports, crash report folder (Game) | `crash_reports`, `crash_folder` | `player/main.cpp` installs `okaycrash::handler::Install` and points `OKAY_CRASH_DIR` at the folder inside the save folder |
| Command-line arguments (Advanced) | one `arg=` line each | `player/main.cpp`, very first thing in `main`: read from `game.okayconfig` and put in front of the real arguments, so typed arguments win |
| Custom defines (Advanced) | `define=NAME=VALUE` | `player/main.cpp`: `Cvars::Set` at start; a name the engine does not know is logged |
| Differences for Web / Android / Server (Output) | not in the game | `OptionsFromSettings`, the Web and Android exports and `BuildServer` use `bsm::EffectiveSettings`: the normal settings with that platform's `name=value;...` on top (names from the profile file, unknown ones refused and warned about). Desktop and Xbox use the normal settings |
| Web: starting memory, compress (Web) | not in the game | `build-web.sh` text from `outtext::WebBuildScript` (`-sINITIAL_MEMORY`, `.gz` copies). With the defaults it is byte for byte the old script |
| Android: package, versions, direction, permissions (Android) | not in the game | `AndroidManifest.xml` and `app-build.gradle.txt` from `outtext::AndroidManifest/AndroidGradle` written into the Android export. These two files are new output |
| Installer: create, folder name, licence, Start menu, shortcut (Installer) | not in the game | `builder::Build` writes `<Name>-installer.nsi` (and copies the licence as `license.txt`) in the output folder from `outtext::InstallerScript`; the uninstaller and the Apps & features entry are always in it. NSIS was not run |
| Build size estimate (summary) | not in the game | the size a Dry Run reported, shown as an estimate only while the settings are unchanged since that Dry Run (a key of all settings). It is the Dry Run result, not a live sum |

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
