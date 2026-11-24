# Architecture: the subsystems and their boundaries

> **What this is:** the shape the code is moving to: which subsystems exist, which folder holds each one, and
> which part may use which. The audit of today's code, the order of the moves and the rules for each move are in
> [modularization_plan.md](modularization_plan.md).
>
> **Status:** target, written 2026-11-23 from the audit in the plan. Today's code does **not** follow all of it
> yet: the plan's section 1 lists every place where it does not, and the batches in section 3 fix them one at a time.
>
> **Last checked:** 2026-11-23 against `origin/main` at `bc5629ee8`.
> The naming section (systems) was added 2026-11-23 with the registry fields in `docs/SYSTEMS.md`.

## The big picture

OkaySpace is four programs built on one engine:

- the **engine** (`engine/`, the `okay_engine` library): everything a game needs at run time;
- the **player** (`player/`): runs a built game;
- the **editor** (`editor/`): makes games;
- the **launcher** (`launcher/`): installs and starts the editor and games, plus accounts, chat and community.

Next to them sit two shared libraries: **OkayUI** (`okayui/`, our own UI toolkit, area 40) and **shared**
(`shared/`, code that two or more of the programs use, such as the crash handler, the self-updater and the
anti-cheat client). `tools/` holds stand-alone programs (our own compiler `okayms`, `okaycc`, `okaybuild`,
`okay-content`); `tests/` holds the tests; `platform/xbox_uwp/` holds the Xbox shell.

## Layers: who may use whom

A layer may include headers from its own layer and the layers **below** it, never from a layer above. This is
what "no circular dependencies" means in practice: if each part only looks down, no loop can form.

| Layer | What it is | Folders (target) | May include |
|---|---|---|---|
| 0. Base | Maths, plain value types, logging, strings, files, hashing, threads, crypto | `okay/Math`, `okay/Core` (base part), `okay/Crypto` | only the C++ standard library and each other (Math and Crypto do not include Core's higher parts) |
| 1. Platform | The operating system behind one interface: window, input devices, audio device, clipboard, file dialogs, sockets, threads, time | `okay/Platform` (interface), `engine/src/Platform/<Backend>/` (Win32, Linux, SDL, Xbox) | layer 0 |
| 2. Object model | GameObject, Component, Transform, the scene graph, the property registry, events | `okay/Scene` (core part), `okay/ECS` | layers 0-1 |
| 3. Subsystems | Each one does one job; see the table below | `okay/Render`, `okay/Graphics`, `okay/Texture`, `okay/Audio`, `okay/Input`, `okay/Physics`, `okay/Animation`, `okay/Net`, `okay/Scripting`, `okay/Script`, `okay/VisualScript`, `okay/UI`, `okay/AI`, `okay/Modeling`, `okay/Optimize`, `okay/World` (new) | layers 0-2, and another subsystem only through a small interface header named in the table |
| 4. Gameplay | Ready-made components built from the subsystems (characters, vehicles, inventory, combat, survival...) | `okay/Components`, `okay/Inventory`, `okay/Combat`, `okay/Environment` | layers 0-3 |
| 5. Assembly | The code that has to know every component: scene files, scene templates, script bindings, visual-script node libraries, the game loop, test helpers | `okay/Scene/SceneSerializer*`, `okay/Scene/SceneTemplates`, `engine/src/Script/EngineApi*`, the OkayScript built-ins, `okay/VisualScript/Nodes*`, `okay/Core/Application`, `okay/Testing` | layers 0-4 |
| 6. Programs | The four programs, plus their shared code and the UI toolkit's screens | `player/`, `editor/`, `launcher/`, `shared/`, `okayui/screens/` | the engine (all layers) and OkayUI; `editor/` and `launcher/` never include each other; `shared/` includes no program folder |
| - | UI toolkit | `okayui/include`, `okayui/src`, `okayui/render`, `okayui/soft` | layers 0-1 and `okay/Graphics` (fonts) only, so it stays a stand-alone library |
| - | Tools and tests | `tools/`, `tests/`, `okayui/tests/` | anything (they test the parts) |

Three plain rules sum it up:

1. **The engine never includes the editor, the launcher, the player or `shared/`.** (Today it never includes the
   first three; it includes `shared/` in 7 places, listed in the plan, section 1.5.)
2. **Platform code lives behind the platform interface.** `#ifdef _WIN32`, `<windows.h>`, SDL and X11 belong in
   `engine/src/Platform/<Backend>/`, in a renderer backend (`engine/src/Render/D3D11*`, `GL*`) and in a program's
   `main` (entry point, icons, resources). Shared code calls the interface instead.
3. **A subsystem talks to another subsystem through a small interface**, not by including its big headers. Where a
   subsystem must name every component (scene files, script bindings), that code belongs in layer 5, not inside
   the component or the subsystem.

## The subsystems

The owner named nine subsystems on 2026-11-23. Each row says what it does, where its code lives (target), what it
may use, and the engine modules (`engine/modules/<name>/module.okaymodule`, from the module work in PR #1158) that
belong to it. A module is a unit a project can turn off; a subsystem is the folder that holds it.

| Subsystem | Does | Folders (target) | Uses (besides layers 0-2) | Modules (PR #1158) |
|---|---|---|---|---|
| **Rendering** | Draws the world: meshes, materials, lights, shadows, sky, post effects, particles; four backends (software, OpenGL, Direct3D 11, Direct3D 12) behind one `Renderer` interface | `okay/Render` (+ `engine/src/Render/<Backend>*`), `okay/Graphics` (images, fonts), `okay/Texture`, `okay/Optimize` (LOD, culling, batching) | Platform (window, GPU context) | render3d_effects, particles, sky_weather, terrain_foliage |
| **Physics** | Rigid bodies, colliders, joints, ragdolls, 2D and 3D | `okay/Physics`, physics components | none | physics2d, physics3d, ragdoll |
| **Audio** | Mixing, buses, 3D sound, decoders, voice chat capture | `okay/Audio` | Platform (audio device) | audio |
| **Animation** | Clips, state machines, blending, IK, humanoid actions | `okay/Animation` (IK maths moves here from `okay/Math`) | Physics (ragdolls, through `Physics/` interfaces) | animation, ik |
| **Scripting** | OkayScript (dynamic VM), typed OkayScript (the C#-style language), visual scripts | `okay/Scripting` (VM), `okay/Script` (typed language), `okay/VisualScript` (graphs and nodes) | the bindings to every component live in layer 5 | scripting_okayscript, scripting_visual |
| **Networking** | Sessions, replication, lag compensation, relay, matchmaking, dedicated server | `okay/Net` (sockets through `Platform`) | Physics (hit checks) | networking, relay_online, anticheat |
| **World streaming** | Loading and unloading parts of a big world by distance: cells, LOD, streamed textures and audio | `okay/World` (new; today `Optimize/StreamingGrid`, `Components/WorldStreamer`, `Texture/Stream`, `Audio/AudioStream`) | Render, Audio (their streaming interfaces) | terrain_foliage (part) |
| **UI** | In-game UI (`okay/UI`: widgets drawn in a game), the OkayUI toolkit (`okayui/`), the editor and launcher screens (`okayui/screens`, `editor/OkayUIHost*`) | as listed | in-game UI uses Render and Input | ui_kit |
| **Platform** | The operating system and online services, as two parts: the OS layer (layer 1) and online services (accounts, friends, forums, achievements, Steam, workshop) | `okay/Platform` (OS only), `okay/Online` (new; today mixed into `okay/Platform`) | Online uses Net and Crypto | (core) |

The other engine folders keep their jobs: `okay/Input` (keys, pads, actions; uses Platform), `okay/AI` (navigation,
behavior trees), `okay/Modeling` (mesh editing), `okay/Debug` (debug draw, console), `okay/Inventory`,
`okay/Combat`, `okay/Environment` (gameplay, layer 4).

## Core engine and editor

The engine holds everything a built game runs. The editor holds everything that only makes games: panels,
inspectors, gizmos, the build pipeline, importers' settings windows. A rule of thumb for code that sits on the
line: if a built game ever runs it, it is engine code; if only a person making the game sees it, it is editor code.
Data models of editor panels with no drawing (`shared/EditorPanelModels.hpp`, `editor/*Model.hpp`) are editor code
too, kept free of drawing so tests can check them.

Inside `editor/`, files are grouped by what a person sees: one file pair per window or panel (`<Window>.hpp/.cpp`),
plus `EditorCommon.hpp` (what every editor file includes), `EditorState.hpp` (the shared state, passed as `ed`)
and `MainShared.hpp` (what is still defined in `main.cpp`; it shrinks to nothing as the split goes on). When a
group passes about 10 files it gets its own folder (`editor/VisualScript/` is the first: 39 `VisualScript*` and `Vs*`
files today).

## Naming

### Systems: one name, one home (CLAUDE.md rule 19e)

The registry of systems is [docs/SYSTEMS.md](../SYSTEMS.md), generated from the `System:` lines of the feature map
(format in [feature_map/README.md](../feature_map/README.md)). Each system has one official name, one home folder,
one entry file, one doc and one owner lane, and lists every old and other name under "Also called", so a search for an
old name (`python3 scripts/find_existing.py own UI`) lands on the system that has it now.

- **One plain name per system**, in plain words: "Physics", "Input", "Input Actions", "UI toolkit (OkayUI)", "Game UI",
  "Platform Layer". "Okay" only where the code already uses it (OkayUI, OkayScript).
- **No "Own", "New", "Improved", "Classic", "V2" or "2"** (and no "Next" meaning a newer version) in a system, file,
  folder, class, enum value or switch name. When our code replaces a third-party part, ours IS the system and gets
  the plain name; the third-party part gets the qualifier while it lasts (`BackendKind::Sdl2`, `ImportBackend::Assimp`).
  When a better version of our own code is written, it replaces the old one in the same change (rule 19d), so there
  is never a "Classic" and an "Improved" to tell apart.
- **One home.** New code for a system goes in its Home folder. An editor window for it (`editor/`) and its components
  (`okay/Components/`) are its tool side and gameplay side, not a second home. A system whose code sits in two homes
  is listed under "Still duplicated" in the registry with its fix.
- **Frozen names stay** (section 1.9 of the plan: script API, scene and prefab keys, component class names, file
  formats, command-line flags, env vars, CMake options). They are listed as aliases of the system; a new clear name
  can be added beside one only with the owner's approval.
- **The tools enforce it.** `scripts/dup_lint.py check` (CI, `scripts/precheck.sh`) refuses a new file or folder named
  with a banned word, and a new top-level folder or a new folder under `engine/include/okay/`, `engine/src/` or
  `editor/` that no system's `Home:` names; a new class or function with a banned word is a warning;
  `scripts/gen_feature_map.py` warns about a system with no Home or Entry, a missing path, a name two systems share
  and a system name with a banned word.

### Names that break the rule today

Not renamed in this change: each rename is its own batch (plan section 6.3), after the open PRs that touch the file
have merged. Frozen names are kept as aliases and only listed.

| Today | Kind | Proposed name | Notes |
|---|---|---|---|
| `PhysicsQuality::Classic` / `::Improved`, editor labels "Classic" / "Improved" (Project Settings, Physics) | enum, UI label | one physics step, no quality choice | PR #1160 (`claude2/physics-no-classic`) removes Classic; the scene key `physicsquality` is frozen and still read |
| `engine/src/Physics/Physics2DImproved.inl`, `Physics3DImproved.inl` | files | gone | both deleted: the steps are `Physics2DEngine.inl` and `Physics3DEngine.inl` (the bridges to the physics core) |
| `engine/include/okay/Platform/OwnHttp.hpp`, `OwnHttpDownload.hpp`, `OwnHttpInflate.hpp`, `OwnHttpNet.hpp`; `struct OwnDownloadSpec`, `OwnDownloadResult` | files, types | `Http.hpp`, `HttpDownload.hpp`, `HttpInflate.hpp`, `HttpNet.hpp`; `DownloadSpec`, `DownloadResult` | curl is the qualified one while it lasts; `OKAY_OWN_HTTP` (CMake, env) frozen alias |
| `engine/src/Platform/OwnWin32*.cpp`, `OwnLinux*.cpp`, `OwnLinux.hpp`; `BackendKind::Own`; `class OwnBackend` | files, enum, class | `Win32Platform*.cpp`, `LinuxPlatform*.cpp`; `BackendKind::Native`; `NativeBackend` | `BackendKind::Sdl2` keeps its qualifier; `OKAY_PLATFORM_BACKEND=own`, `OKAY_PLATFORM_DEFAULT_OWN` frozen aliases |
| `shared/OwnPainter.hpp`, `enum class OwnPainterPick` | file, enum | `shared/PainterPick.hpp`, `PainterPick` | `OKAY_OWN_PAINTER` env frozen alias |
| `engine/include/okay/Scripting/NextScriptVM.hpp`, `engine/src/Script/NextScriptVM.cpp`, `class NextScriptVM` | files, class | `TypedScriptVM` (system: Typed OkayScript) | after PR #1159 (one binding table) merges |
| `ImportBackend::Own` | enum value | `ImportBackend::Native` (third-party: `::Assimp`, `::Ufbx`) | `OKAY_OWN_<FORMAT>` and `OKAY_PREFER_OWN_READERS` CMake options frozen aliases |
| `shared/RobotoFontOwn.h`, `shared/JetBrainsMonoFontOwn.h` | generated files | `RobotoFontOkf.h`, `JetBrainsMonoFontOkf.h` (the OKF container they hold) | change `scripts/gen_embedded_fonts.py` with them |
| `editor/ProjectNewAssets.hpp` | file | `ProjectCreateMenu.hpp` | "New" here is the menu's name; low priority |
| `examples/own_readers/`, `examples/own_wav/`, `scripts/own_http_bench.py`, `platform/xbox_uwp/XboxOwnHttp.hpp`, `platform/xbox_uwp/engine/uwp_own_readers.cmake` | folders, files | `examples/model_readers/`, `examples/wav/`, `scripts/http_bench.py`, `XboxHttp.hpp`, `uwp_model_readers.cmake` | the dist script and tests that name them change with them |
| `shared/OwnStack.hpp`, `shared/OwnSwitches.hpp`, `OKAY_OWN_STACK`, `scripts/check_release_own_stack.sh` | files, CMake option | system renamed **Replacement Switch** in the registry; files are deleted when the last replacement is the default (rule 17) | `OKAY_OWN_STACK` frozen alias |
| `OKAY_OWN_CRYPTO`, `OKAY_XBOX_OWN_CRYPTO`, "own crypto" | CMake options | stay | rule 18: libsodium stays the default until an outside review, so ours is the qualified one here |
| `OKAY_UI_OWN_RENDER`, `OKAY_CLASSIC_UI` | env vars | stay (frozen); docs say "OkayUI renderer" and "Dear ImGui look" | aliases of OkayUI |
| Doc wording "own UI", "okayui host", "own platform", "own readers", "own core", "Own X" | prose | the official system names (OkayUI, Platform Layer, Model Import, Physics) | fix as docs are touched; the registry lists them as aliases so searches still land |
| System "Own-Stack Switch" | registry name | **Replacement Switch** (done in this change; old name kept as alias) | - |

### Code names

The naming rule (types, functions, members, constants, files) and the list of names that may not change without
the owner's approval are in [modularization_plan.md](modularization_plan.md), section 6.
