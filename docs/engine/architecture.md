# Architecture: the subsystems and their boundaries

> **What this is:** the shape the code is moving to: which subsystems exist, which folder holds each one, and
> which part may use which. The audit of today's code, the order of the moves and the rules for each move are in
> [modularization_plan.md](modularization_plan.md).
>
> **Status:** target, written 2026-11-23 from the audit in the plan. Today's code does **not** follow all of it
> yet: the plan's section 1 lists every place where it does not, and the batches in section 3 fix them one at a time.
>
> **Last checked:** 2026-11-23 against `origin/main` at `bc5629ee8`.

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

The naming rule (types, functions, members, constants, files) and the list of names that may not change without
the owner's approval are in [modularization_plan.md](modularization_plan.md), section 6.
