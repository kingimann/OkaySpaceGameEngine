# OkaySpace Game Engine

OkaySpace is a game engine written in C++17. It comes as four parts:

| Part | Program | What you do with it |
|---|---|---|
| **Launcher** | `OkaySpace.exe` | Sign in, find and create projects, install and update the editor, play and share games, talk to friends. |
| **Editor** | `OkayEngine.exe` | Build scenes from objects and components, add scripts, press Play, and export a game. |
| **Player** | `OkaySpacePlayer.exe` | Runs a finished game. An exported game is the player plus your project's files. |
| **Engine** | (a C++ library) | The code under all three: scenes, rendering, physics, sound, scripting, networking and more. |

Releases are for **Windows** only for now. Linux and macOS builds are parked until the owner says otherwise (see
[Platforms](#platforms)).

The goal is to write every part ourselves. Most of the engine already is our own code. A few outside libraries are
still in use while our replacements are finished (see [Our own code](#our-own-code)).

**Version on `main` when this page was written:** 2.903.3 (from `CMakeLists.txt`, 2026-11-23). What changed in each
release, in plain words: [dist/CHANGELOG.txt](dist/CHANGELOG.txt).

## Contents

- [What you can make](#what-you-can-make)
- [The main systems](#the-main-systems)
- [Our own code](#our-own-code)
- [Download and update](#download-and-update)
- [Build from source](#build-from-source)
- [Repository layout](#repository-layout)
- [Trying features](#trying-features)
- [How releases work](#how-releases-work)
- [How contributors work](#how-contributors-work)
- [Platforms](#platforms)
- [A taste of the API](#a-taste-of-the-api)
- [Help and more](#help-and-more)
- [License](#license)

## What you can make

- **3D games** in first or third person: walk, run, jump, crouch, swim, drive cars and boats, fight, build.
- **2D games**: top-down and platformer games with sprites, sprite sheets and tile maps.
- **Open worlds**: sculpted and painted terrain, water, sky, weather and a day-night cycle, with streaming for big
  levels.
- **Story games**: dialogue, quests, cutscenes on a timeline.
- **Survival and sandbox games**: inventory, crafting, health and hunger, voxel digging and a build mode for players.
- **Multiplayer games**: host or join, a relay for players behind home routers, dedicated servers, voice chat.

You can make all of this without code (ready-made components, Action Lists and Visual Script), with scripts
(OkayScript), or in C++. A first game, step by step: [docs/gameplay/making_a_game.md](docs/gameplay/making_a_game.md).

## The main systems

Each system has one name. [docs/SYSTEMS.md](docs/SYSTEMS.md) lists every system with its status and main code, and
[docs/FEATURE_MAP.md](docs/FEATURE_MAP.md) says where each part's code, tests and docs are. Both pages are made by a
script; do not edit them by hand.

**Scenes and objects.** A level is a scene. A scene holds game objects; each object has a position, a parent and
components (the parts that make it do things). Scenes are saved as readable text files (`.okayscene`). Prefabs save an
object as a reusable file: change the file and every copy changes. There are save games with slots, a whole-scene
save, timers and tweens, and a property registry that lets undo, live editing and saving reach any setting by name.
Docs: [scene management](docs/engine/scene_management.md), [prefabs](docs/engine/prefabs.md),
[save games](docs/engine/save_games.md), [loop and time](docs/engine/loop_and_time.md).

**Rendering.** One `Renderer` interface with four backends: software, OpenGL, Direct3D 11 and Direct3D 12. On top:
materials and textures (one shared texture asset per picture, with our own mipmaps), sun, point, spot and area lights
with shadows, post-processing (bloom, colour grading, depth of field, anti-aliasing), sky, fog, rain and snow,
particles, and culling and LOD so big scenes stay fast. How well each backend works is written down honestly in the
rendering page. Docs: [rendering](docs/rendering/rendering.md), [lighting](docs/rendering/lighting.md),
[textures](docs/rendering/textures.md), [particles](docs/rendering/particles.md).

**Physics.** Solid 2D and 3D objects fall, collide, bounce and join. 3D physics has one path: our own physics core,
`okay::phys` (its own library, `okay_physcore`). It was written only from published papers and textbooks, and each
source file names the papers it follows. No code from Bullet, PhysX, Jolt, Box2D or any other engine was used. It has
a broad phase, GJK and EPA contact finding, a sequential-impulse solver with warm starting, sleeping islands, and one
general joint (fixed, ball, hinge, slider, cone-twist, distance and six-axis are presets) with motors, springs and
breaking. There are physics materials and "physics toys" that need no code. Docs: [physics](docs/physics/physics.md),
[the physics core](docs/physics/physics_core.md).

**Ragdolls.** Two kinds. The **Ragdoll** component makes a character go limp on death or flinch when hit; it is a
particle simulation over the 15 humanoid bones. The **Active Ragdoll** is a simulated body that follows its animation
with joint "muscles", keeps its balance when pushed, steps to catch itself, falls, and gets up again. It is a first
version (2026-11-22) that works in the unit tests on a flat floor and in its example scene. Docs:
[ragdolls in the physics page](docs/physics/physics.md), [active ragdoll](docs/gameplay/active_ragdoll.md).

**Animation and IK.** Keyframe clips, an Animator with state machines and two editor windows, imported model clips,
a built-in humanoid with retargeting, root motion and skinning. IK (inverse kinematics: bending bones so a hand or
foot reaches a target) covers feet, limbs, chains, look-at, aim and full body. Docs:
[animation](docs/gameplay/animation.md), [IK](docs/gameplay/ik.md).

**Characters and vehicles.** One Player Controller for first and third person (walk, run, jump, crouch, prone, swim),
ready-made controllers for top-down, platformer, twin-stick, shooter and click-to-move games, a character creator,
seats, and cameras that follow, orbit, blend, shake and avoid walls. The Vehicle Controller drives cars (an open-world
driving model and an older arcade model) and boats. Docs: [first game and Player Controller](docs/gameplay/making_a_game.md),
[cameras](docs/gameplay/cameras.md), [vehicles](docs/gameplay/vehicles.md),
[shooter and 2.5D controllers](docs/gameplay/shooter_and_25d_controllers.md).

**Input.** Keyboard, mouse, gamepads and touch, controller rumble, input actions (name an action once, bind it to many
keys and buttons) and on-screen touch controls. Docs: [input](docs/engine/input.md).

**Scripting.** Three ways to write game logic, all reading one shared table of engine functions
([script API registry](docs/scripting/script_api_registry.md)):
- **OkayScript**, the default language. A script file is a component: its fields show in the Inspector, and `Start()`
  and `Update()` run by name. C#-style and C-style syntax both work. It has its own code editor, debugger and profiler.
  Since 2.903.8 and 2.903.9 it is a standalone language (`okayscript/`, with its own `okayscript` command line) that the
  engine runs as bytecode, and `GetComponent("Name")` reaches every engine component through generated bindings.
  [OkayScript guide](docs/scripting/scripting.md), [bytecode machine](docs/scripting/okayscript_vm.md),
  [bindings](docs/scripting/script_bindings.md).
- **Typed OkayScript** (the C#-style language): a script is `class Name : Behaviour`, and every name and type is
  checked before it runs, so a script either runs or says why not, with line and column. It shares one command table
  with OkayScript for the commands moved so far (#1159).
- **Visual Script**: logic made by wiring boxes (nodes) together instead of typing. Nodes explain themselves.
  [Visual Script format](docs/scripting/visual_scripting.md), [node list](docs/visual_scripting_nodes.md).
- **Action Lists**: no-code rules, "when this happens, if this is true, do these steps".

The old optional Lua and Mono C# backends were removed. A project that names them still loads; those scripts just do
not run and say so. New C# (on the .NET runtime) and Lua (our own) are planned as alternatives next to OkayScript
(`CLAUDE.md` rule 24c); neither is on main yet.

**UI.** Two separate things. **Game UI** is the interface inside your game: a canvas with buttons, bars and the usual
widgets, keyboard and gamepad focus, HTML/CSS-style documents, in-world UI and localization
([in-game UI](docs/ui/in_game_ui.md)). **OkayUI** (`okayui/`) is our own UI toolkit that is replacing Dear ImGui in the
editor and launcher: the Crash Log, Audio Mixer and History windows and the script editor's code view are OkayUI only,
most other screens are still Dear ImGui ([OkayUI](docs/ui/own_ui_toolkit.md), [catalogue](docs/ui_catalogue.md)).

**Audio.** AudioSource components mixed into stereo, mixer groups, WAV, OGG and MP3, music, 3D sound with echo,
pitch that follows speed and muffling through walls. WAV files are read by our own decoder. Docs:
[audio](docs/audio/audio.md), [own WAV decoder](docs/audio/own_wav_decoder.md).

**Networking and online.** Host or join over UDP with authenticated encryption, sync and prediction, remote calls,
networked players, matchmaking and lobbies, a relay for players behind home routers, dedicated servers (the same
player with no window), server-side anti-cheat and voice chat. An easy layer does host-or-join, events and the player list in one call or one node,
with host migration on a LAN and a rollback mode. Online services (accounts, cloud saves, leaderboards)
run on a Supabase backend that our own code talks to over its plain web API. The launcher adds friends, parties,
chat, community servers and a workshop. Docs: [multiplayer](docs/online/multiplayer.md),
[relay](docs/online/relay.md), [dedicated servers](docs/online/dedicated-server.md),
[anti-cheat](docs/online/anticheat.md), [accounts](docs/online/accounts.md),
[Supabase backend](docs/online/supabase-backend.md).

**Saves.** Save slots and the save game framework, cloud saves through the online backend, and editor autosave and
crash recovery. Docs: [save games](docs/engine/save_games.md), [undo and recovery](docs/editor/undo_recovery.md).

**World and terrain.** Heightmap terrain you sculpt and paint, voxel terrain you can dig, foliage and decals, water
that is not a flat plane, floating objects, wind, and world streaming that loads parts of a big world by distance.
Docs: [terrain and water](docs/rendering/terrain_water.md), [level tools](docs/editor/level_tools.md),
[wind](docs/gameplay/wind.md), [optimization tools](docs/engine/optimization_tools.md).

**Modules and architecture.** The engine is described as layers and nine subsystems (rendering, physics, audio,
animation, scripting, networking, world streaming, UI, platform) in [architecture](docs/engine/architecture.md).
Engine modules (units a project can turn off, `engine/modules/<name>/module.okaymodule`) come from PR #1158; that
folder is **not on `main`** when this was written. The plan for splitting big files is
[modularization plan](docs/engine/modularization_plan.md).

**Xbox export.** An **experimental** export to a UWP package that an Xbox One in Developer Mode can run, made with
Microsoft's public Windows SDK only. A minimal test package was built, installed and started on a Windows 11 PC by the
owner (2026-11-18). Nothing has been verified on an Xbox console. Docs: [Xbox export](docs/platform/xbox_uwp.md).

**Editor.** Dockable panels (Hierarchy, Scene, Game, Inspector, Console, Project), Play, Stop and Step, prefabs,
materials, undo and redo, autosave and crash recovery, a built-in mesh editor, level tools, plugins written in
OkayScript, and File > Build Settings (Ctrl+B) to export a game. Docs: [editor](docs/editor/editor.md),
[menus](docs/editor/editor_menus.md), [build pipeline](docs/release/build_pipeline.md).

## Our own code

The owner's rule: write every part ourselves, even if it takes time ([CLAUDE.md](CLAUDE.md) rules 17 to 19a). We
write from specifications, file-format documents and papers, never by copying code from another project. Each
replacement goes through the same steps: a seam (one place the old library is called), our own backend, tests
against the old one until ours gives the same or better results, then ours becomes THE path and the old code is
deleted. Nothing is kept off by default (rule 17, 2026-11-22); cryptography is the one exception (below). The full plan: [docs/status/own_code_roadmap.md](docs/status/own_code_roadmap.md). What is ours and
what is still outside code: [docs/status/THIRD_PARTY.md](docs/status/THIRD_PARTY.md).

**Ours and on by default:** the engine core, physics, our glTF and OBJ readers, our own readers for many other model
formats (switched on 2026-11-17; Assimp stays as the fallback where it is built), our own FBX reader (the only FBX
reader since ufbx was deleted; it reads FBX 6.0 to 7.x), the texture library, and since 2.903.3 the OkayUI screens
and Inspector and the OkayUI renderer (Settings > Experimental starts with every box ticked; untick one to go back), and
our own WAV decoder (the only WAV reader since 2026-11-23).

**Deleted on 2026-11-23:** ufbx, `SDL_LoadWAV` and the vendored ImGuiColorTextEdit text editor widget (the script
editor uses our own `OkayUI::CodeEdit`).

**HTTP:** every download and web call goes through one seam: WinHTTP (part of Windows) on Windows, our own HTTP client on
Linux when switched on, and curl only as the last fallback.

**Ours but not the default yet:** the platform layer that replaces SDL2 (the Linux backend is tested, the Windows one
is not yet), our own build tool `okay-build` (replaces CMake) and our own compiler work (outside CMake, CI and releases).

**Outside code still in use:** SDL2, Dear ImGui, Assimp (on in the Windows release build, as the fallback reader),
curl (last HTTP fallback) and libsodium. No new outside libraries or SDKs may be added.

**The crypto exception.** Encryption cannot be replaced on our word: it needs an outside review first, and the owner
decided on 2026-11-14 not to hire one. So **libsodium stays the default in every release**, our own `okay::crypto`
stays off, and release builds refuse to configure with our own crypto. Docs: [crypto](docs/platform/crypto.md).

## Download and update

1. Download the latest Windows build from the GitHub Releases page (an installer or a zip).
2. Run **`OkaySpace.exe`**, the launcher. Everything else is in a `Tools/` folder next to it.
3. Create a project, make a scene, press **Play**, then **File > Build Settings…** (Ctrl+B) to export a game.

**Updates.** The launcher updates itself, the editor and the player in place. It downloads the new files, checks each
one against a SHA-256 checksum from `versions.json`, and swaps them all at once or not at all, so a failed update can
be rolled back ([updater](docs/release/updater.md)). The Versions page keeps other editor versions next to the one you
run, so each project can open with the version it was made with ([launcher versions](docs/release/launcher_versions.md)).

**Release channels** ([release channels](docs/release/release_channels.md)):

| Channel | What it is |
|---|---|
| **Latest** | Every normal release. New features and fixes. The default. |
| **LTS** | One chosen version line that only gets fixes, as patch releases. For projects that should stay on a stable editor. |
| **Test builds** | Not a channel for players. One branch built so the owner can try unmerged work. Only a launcher with **Settings > Behavior > Startup & updates > Get test builds** switched on sees them. |

Installing, updating and uninstalling in detail: [install](docs/release/install.md),
[packaging](docs/release/packaging.md).

## Build from source

You need CMake 3.16 or newer and a C++17 compiler. The engine core needs nothing else. These commands are the ones the
repository's scripts and CI use; check `.github/workflows/ci.yml` for the current exact flags.

**Linux development build and tests** (what CI's "Build & test (Ubuntu)" job runs):

```bash
sudo apt-get install -y cmake g++ ninja-build libsdl2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure -j4 --timeout 300
```

Building everything takes a long time and a lot of disk. Build only the targets you need
(`cmake --build build --target <name>`), and check free space first (`df -h /`).

**The editor and launcher** on Linux (what CI's editor job runs, plus the headless self-test):

```bash
cmake -S . -B build-editor -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOKAY_BUILD_EDITOR=ON -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=ON
cmake --build build-editor -j2
./build-editor/bin/OkayEngine --dev
```

**The small demo:** with the default options (`OKAY_BUILD_SANDBOX=ON`), `./build/bin/sandbox` runs the example game
(WASD to move, Q to quit).

**The Windows release** is cross-compiled from Linux with MinGW-w64. This is what the release workflow runs:

```bash
sudo apt-get install -y mingw-w64 cmake ninja-build
export SDL2_MINGW_PREFIX=/path/to/SDL2-<ver>/x86_64-w64-mingw32   # from SDL2-devel-<ver>-mingw.tar.gz
./scripts/build-windows-dist.sh
```

The output goes to `build-win-dist/`: `OkaySpace.exe` and `SDL2.dll` at the top, the rest in `Tools/`. The script's
header lists its other settings.

**Useful CMake options** (all in the top `CMakeLists.txt`):

| Option | Default | What it does |
|---|---|---|
| `OKAY_BUILD_TESTS` | ON | The unit tests (CTest). |
| `OKAY_BUILD_SANDBOX` | ON | The sandbox demo game. |
| `OKAY_BUILD_LAUNCHER` | ON | The self-updating launcher. |
| `OKAY_BUILD_EDITOR` | OFF | The editor (needs SDL2 and OpenGL). |
| `OKAY_BUILD_PLAYER` | OFF | The game player runtime (needs SDL2). |
| `OKAY_USE_SODIUM` | ON | Packet encryption with libsodium. |
| `OKAY_OWN_CRYPTO` | OFF | Our own crypto instead of libsodium. Unreviewed; keep off. |
| `OKAY_USE_ASSIMP` | OFF | Assimp as a fallback for model formats (on in the Windows release script). |
| `OKAY_OWN_<FORMAT>` | mostly ON | Our own reader for one model or sound format. |

`./launch.sh` (Linux) builds the launcher once and hands off to it. The other build scripts are in `scripts/`
(web: `build-web.sh`, Xbox: `build-xbox-uwp.ps1` on Windows).

## Repository layout

| Folder | What is in it |
|---|---|
| `engine/` | The engine library: headers in `include/okay/<area>/`, code in `src/` (no vendored outside code is left; `third_party/` was deleted with ufbx). |
| `okayscript/` | OkayScript as a standalone language: the library the engine runs on, the `okayscript` command line and its tests. |
| `editor/` | The editor (`OkayEngine.exe`). |
| `launcher/` | The launcher (`OkaySpace.exe`): sign-in, library, updates, community. |
| `player/` | The runtime that plays an exported game (`OkaySpacePlayer.exe`), also used as the dedicated server. |
| `okayui/` | OkayUI, our own UI toolkit, with its own tests. |
| `shared/` | Code shared by the launcher, editor and player (self-update, test hooks, fonts). |
| `relay/` | The multiplayer relay server. |
| `platform/` | Platform-specific packaging; today the Xbox UWP project. |
| `tools/` | Our own tools: the `okay` command line (`okay-cli`: projects, scenes, scripts, export, Git underneath with scene-aware merge), the build tool (`okaybuild`), compiler work (`okaycc`, `okayms`), texture and content tools, generators. |
| `examples/` | Small example projects and scenes: physics, ragdoll, vehicles, boat, water, UI, visual script, networking, an account server, editor plugins and more. |
| `sandbox/` | The small demo game. |
| `tests/` | Unit and integration tests (CTest), test assets and export checks. |
| `assets/` | Fonts and other assets that ship. |
| `scripts/` | Build, check, packaging and release scripts. |
| `docs/` | The documentation. Start at [docs/README.md](docs/README.md). |
| `dist/` | Files that ship with releases, including `CHANGELOG.txt`. |
| `supabase/`, `deploy/`, `steam/`, `config/` | The online backend function, deploy files, Steam upload files and public settings. |
| `cmake/` | CMake helper files. |

## Trying features

- [docs/try_it.md](docs/try_it.md) holds the "how to try it" entries for older features: what to open and what to
  expect. New changes no longer add entries (owner rule 4a, 2026-11-17), but the page is kept.
- [docs/verification/README.md](docs/verification/README.md) holds the owner's test checklists for the Windows release,
  one page per area.
- The `examples/` folder has scenes for many systems, and the editor's project templates give a working start.
- `scripts/okay_run.py` runs a scene with no window, with scripted input, and checks what happened
  ([okay_run](docs/process/okay_run.md)).

## How releases work

Releases are small and often: whatever is merged and green goes out. In short
([releasing](docs/release/releasing.md)):

1. Bump the version in `CMakeLists.txt`, add a row to [ROADMAP.md](ROADMAP.md) and a plain-language entry to
   [dist/CHANGELOG.txt](dist/CHANGELOG.txt), and merge that.
2. Run the release workflow (`release.yml`, ref `main`, channel `latest`). It builds the Windows programs with
   `scripts/build-windows-dist.sh`, publishes them to the Supabase release bucket and to GitHub Releases.
3. Check it worked: the publish step, the "make latest" step, and `versions.json` having a SHA-256 for
   `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe`. A release without checksums must not become latest.

A version is never built twice: re-running the workflow for an existing version would change the checksums. A test
build of any branch goes to a separate `dev/` folder and never touches "latest" or LTS (`dev-build.yml`).

## How contributors work

People and Claude sessions work on this repository. The owner's standing rules are in [CLAUDE.md](CLAUDE.md); how the
three Claude sessions share the work is in [docs/process/collaboration.md](docs/process/collaboration.md). The short
version:

- **Lanes.** Three Claude sessions work at once: the coordinator (`claude/*`), Claude 2 (`claude2/*`) and Claude 3
  (`claude3/*`). Each owns certain files ([docs/lanes.json](docs/lanes.json)) and claims its work on issue #422.
- **Two branches per Claude.** Each has `<lane>/work`, where work is built, and `<lane>/ready`, which its pull request
  comes from. Agents work in local worktrees and do not push.
- **No duplicates.** Before building anything, search what exists (`python3 scripts/find_existing.py <keywords>` and
  the feature map) and extend it instead of writing a second copy. A guard (`scripts/dup_lint.py`) fails pasted code
  ([no duplicates](docs/process/no_duplicates.md), [shared code](docs/engine/shared_code.md)).
- **Precheck before every push.** `bash scripts/precheck.sh` (about a minute) catches conflict markers, branches that
  no longer merge, secrets, duplicate code, invalid feature map pages and copies between open branches.
  `bash scripts/precheck.sh --build <build folder> --tests` also builds what changed and runs every test it touches.
  `bash scripts/install_hooks.sh` makes git run it before each push.
- **PR checks.** On a pull request GitHub runs branch-lint, the PR-body check and the Windows cross-compile. The full
  suite (Ubuntu build and tests, sanitizers, editor, player, web and more) runs on every push to `main` and every
  night. A PR is merged only when its checks are green and its precheck was clean; a red PR is fixed, not re-run.
- **Docs in the same change.** Update the feature map pages (`docs/feature_map/NN-*.md`, checked with
  `python3 scripts/gen_feature_map.py --validate`), the roadmap and `docs/status/UNFINISHED.md`. Generated pages are
  rebuilt on `main` only, never committed in a PR.
- **Write it yourself.** No copied code, no new outside libraries or SDKs. Assets (fonts, images, sounds) only with a
  licence that allows our use, recorded in `docs/status/THIRD_PARTY.md`.
- **Be honest.** Say what was run and what was not, and say "unverified" when it is.

## Platforms

| Platform | State |
|---|---|
| Windows | Supported. The launcher, editor and player ship in every release. |
| Linux | Builds from source for development and CI. Release builds are parked. |
| macOS | Parked. Never tested on a Mac. |
| Web | WebAssembly builds of the player work (`scripts/build-web.sh`). |
| Mobile | Limited. |
| Xbox One (Developer Mode) | Experimental, not verified on a console. |

Details: [platform layer](docs/platform/platform_layer.md), [web and mobile](docs/platform/web_mobile.md),
[Xbox](docs/platform/xbox_uwp.md).

## A taste of the API

```cpp
#include <Okay.hpp>
using namespace okay;

class MySpin : public Behaviour {
public:
    float speed = 90.0f; // degrees per second
    void Update(float dt) override { transform->Rotate({0, 0, speed * dt}); }
};

int main() {
    Application app({"My Game", 80, 30});
    Scene scene("Main");

    scene.CreateGameObject("Camera")->AddComponent<Camera>()->orthographicSize = 8.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->glyph = '@';
    player->AddComponent<MySpin>();

    app.Run(scene);
}
```

The same idea in OkayScript, with multiplayer:

```c
update(dt) {
  if (key_down("h")) net_host(45000)                 // H hosts
  if (key_down("j")) net_join("127.0.0.1", 45000)    // J joins
}
```

Every function a script can call is listed in [docs/engine_api.md](docs/engine_api.md). To render somewhere new,
implement `okay::IRenderer` and pass it to `Application::SetRenderer(...)`.

## Help and more

- All documentation, grouped by who it is for: [docs/README.md](docs/README.md).
- What is not finished: [docs/status/UNFINISHED.md](docs/status/UNFINISHED.md).
- What is being worked on now: `WORK_BOARD.md` on the `work-board` branch (rewritten from GitHub every 10 minutes)
  and the hand-written snapshot [docs/status/work_board.md](docs/status/work_board.md).
- Releases and plans: [ROADMAP.md](ROADMAP.md) and [dist/CHANGELOG.txt](dist/CHANGELOG.txt).
- Found a bug or have a question: open an issue on the GitHub repository. Crash reports from the editor can become
  issues too ([crash reports](docs/online/crash_reports.md)).

## License

Proprietary, all rights reserved. You may make and sell games with the engine without royalty; see `LICENSE`. Earlier
versions published under MIT stay MIT for those copies.
