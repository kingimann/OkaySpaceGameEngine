# Modularization plan: smaller files, clear subsystems, clear names

> **What this is:** the audit of how the code is split today, and the plan for splitting it into smaller files with
> one job each, grouped by subsystem, with names that say what they do. Part A covers the whole engine and every
> program; Part B is the `editor/main.cpp` split (it started first, as `docs/editor/main_split_plan.md`, which now
> points here); Part C is the map of `launcher/main.cpp`. The target shape is [architecture.md](architecture.md).
>
> **Status:** plan. Audit measured on `origin/main` at `bc5629ee8` (2026-11-22). Editor move 1 and batch T1 (the two compiler
> tools, section 3.1) are done; nothing else has moved yet. No code changes with this page.
>
> **Last checked:** 2026-11-23.

What the owner asked for on 2026-11-23, in short:

- **(A) Modularization.** Break the large files into smaller files with one clear job each; group them by subsystem
  (rendering, physics, audio, animation, scripting, networking, world streaming, UI, platform); keep the core engine
  apart from the editor and the shared code apart from platform code; remove circular dependencies; keep every API
  and every behaviour; update includes and CMake; build and test after each batch; no arbitrary splits and no
  hundreds of tiny files; biggest and hardest files first; write down the architecture. "First audit the repository
  and create a modularization plan, then refactor one subsystem at a time."
- **(B) Naming.** Descriptive names; PascalCase types and functions, camelCase locals and parameters, `m_camelCase`
  private members, `kPascalCase` constants, PascalCase files; keep an existing convention where changing it breaks
  something; split over-complicated functions, name magic numbers, explain unusual choices; delete code only when it
  is proven unused. Not a rewrite: no behaviour change, no slower code, and no change to public APIs, file formats,
  script names or asset identifiers without the owner's approval.

This page builds on what exists: the editor split process (Part B, `scripts/check_pure_move.py`), the feature map
and `docs/SYSTEMS.md` (the named systems), the code index (`docs/engine/code_index.md`), the shared-code table
(`docs/engine/shared_code.md`) and the engine modules of PR #1158 (`engine/modules/*/module.okaymodule`,
`cmake/OkayModules.cmake`, `scripts/check_modules.py`, its page `docs/engine/modular_engine.md`; not on `main` on
2026-11-23). It does not add a second process: every batch below is a "move" in the sense of Part B.

# Part A: the whole engine

## 1. The audit

### 1.1 How it was measured

- **Files:** every tracked `.cpp`, `.hpp`, `.h` and `.c` file, without `engine/third_party/`, `editor/vendor/` (since deleted),
  `okayui/probe/imgui*`, generated `.inc` files, `dist/` and data files. 930,048 lines in all.
- **Functions:** a brace counter (comments, strings and raw strings skipped; the one described in Part B.1) finds
  every outermost function body and its size. Lambdas inside a function count as part of it.
- **Churn:** commits that touched the file from 2026-11-13 to 2026-11-22 (all the history this clone has: 434 commits).
- **Includes:** every `#include "..."` resolved the way the build resolves it (the file's own folder, then
  `engine/include`, `shared`, `launcher`, `editor`, `okayui/*`, `platform/xbox_uwp`), giving a graph of 1 file
  including another. Folder graphs and cycles come from that graph (Tarjan's strongly connected components).
- **Open PRs:** `gh api "repos/kingimann/OkaySpaceGameEngine/pulls?state=open"`, then each PR's file list.

The scripts were throw-away (kept out of the repository); section 7 describes the permanent checks that replace them.

### 1.2 The largest files

Lines, outermost functions, the biggest functions, commits in 10 days, and the open PRs that change the file
(2026-11-23). Tests and tools are listed because they are big, but they come after the programs and the engine.

| # | File | Lines | Functions | Biggest functions (lines) | Commits | Open PRs in it |
|---|---|---|---|---|---|---|
| 1 | `editor/main.cpp` | 58,835 | 823 | `DrawInspector` 12,804; `DrawScene3D` 3,190; `DrawScriptEditor` 2,609; `main` 2,296; `DrawModeling` 1,865; `DrawProject` 1,718 | 94 | #1160, #1158, #1155, #1142, #1140, #1138, #1120 |
| 2 | `launcher/main.cpp` | 27,261 | 573 | `main` 9,133; `DrawChatWindow` 2,186; `DrawPublicProfilePage` 942; `DrawGroupConversation` 808 | 31 | none |
| 3 | `tools/okayms/src/compile.cpp` | 15,052 | 302 | `Gen::gen_expr` 1,233; `Parser::parse_unary` 708; `emit_functions` 613 | 25 | none |
| 4 | `engine/src/Scripting/OkayScriptVM.cpp` | 12,236 | 251 | `RegisterBuiltins` 8,078 (one function registers 1,339 script commands) | 15 | #1159, #1138 |
| 5 | `engine/src/Scene/SceneSerializer.cpp` | 11,126 | 109 | `ParseStep` 5,205; `WriteComponentRecords` 3,205; `RegisterBuiltinComponentSerializers` 695 | 40 | #1160, #1138 |
| 6 | `tools/okaycc/src/parser.cpp` | 9,211 | 308 | `checkCall` 471 | 29 | none |
| 7 | `okayui/src/OkayUI.cpp` | 7,809 | 652 | `Begin` 204 (many small functions: windows, menus, tables, tooltips, drawing) | 13 | none |
| 8 | `tests/test_vs_gameplay_library.cpp` | 6,645 | 10 | `main` 6,399 | 1 | #1166, #1165 |
| 9 | `editor/VisualScriptEditor.hpp` | 6,153 | 139 | `Draw` 2,444; `Registry` 656 (all in a header) | 9 | #1166, #1163 |
| 10 | `engine/include/okay/Render/Mesh.hpp` | 5,170 | 202 | `LoadOBJ` 219; the whole file is one `struct Mesh` (lines 35-5168) with every body inline | 8 | #1074 (6 lines) |
| 11 | `engine/src/Net/NetworkManager.cpp` | 4,291 | 168 | `ServerTick` 839; `ClientTick` 444; `Deliver` 154 | 5 | none |
| 12 | `engine/src/Render/ModelImport.cpp` | 3,850 | 87 | `ImportModelSceneRaw` 430; `AttachCharacterModelRoot` 273 | 21 | none |
| 13 | `tools/okaybuild/okay-build.cpp` | 3,788 | 199 | `parseManifest` 249 | 31 | none |
| 14 | `okayui/screens/OkayUIEditorPanels.cpp` | 3,584 | 70 | `DrawFieldList` 1,039 | 2 | #1160 |
| 15 | `engine/src/UI/UIRenderer.cpp` | 3,384 | 119 | `RenderSceneUIImpl` 1,693 | 7 | none |
| 16 | `player/main.cpp` | 3,076 | 26 | `main` 1,936 | 16 | #1158, #1120 |
| 17 | `engine/src/Render/GLRenderer.cpp` | 3,050 | 49 | `RenderToPixels` 786 | 11 | #1140, #1120 |
| 18 | `engine/include/okay/Render/SoftwareRenderer.hpp` | 2,910 | 99 | `RenderMeshes` 620 (header) | 12 | #1120 |
| 19 | `editor/AnimatorWindow.cpp` | 2,885 | 85 | `DrawCanvas` 591 | 1 | none |
| 20 | `engine/src/Components/Character.cpp` | 2,800 | 111 | `PoseOwnerArm` 307 | 4 | none |
| 21 | `engine/src/Physics/Physics3D.cpp` | 2,603 | 82 | `StepSlice` 963 | 9 | #1160 (removes 1,342 lines) |
| 22 | `engine/src/Script/EngineApi.cpp` | 170 (was 2,465) | 20 | done: `Build` (2,233) split into 14 `Bind<Topic>` functions in 9 `EngineApi_<Topic>.cpp` files (section 3.1, S5) | 9 | #1159 |
| 23 | `engine/src/Render/FbxScene.cpp` | 2,315 | 127 | `Builder::Animation` 204 | 7 | #1140 |
| 24 | `engine/src/Animation/HumanoidActions.cpp` | 2,239 | 120 | `AddStates` 427 | 6 | none |
| 25 | `editor/BehaviorTreeWindow.cpp` | 2,139 | 66 | `DrawCanvas` 551 | 2 | none |
| 26 | `engine/src/Render/D3D11Renderer.cpp` | 2,109 | 50 | `RenderToPixels` 798 | 13 | #1140, #1120 |
| 27 | `engine/src/Render/BlendReader.cpp` | 2,109 | 87 | `ReadMesh` 218 | 3 | none |
| 28 | `editor/DialogueWindow.cpp` | 2,028 | 76 | `DrawCanvas` 465 | 2 | none |
| 29 | `engine/include/okay/Scene/SceneTemplates.hpp` | 2,022 | 43 | `VehicleSeats3D` 166 (header) | 10 | none |
| 30 | `engine/src/Scene/PrefabTools.cpp` | 1,986 | 85 | `ExpandOwn` 110 | 4 | none |
| 31 | `engine/src/Script/Checker.cpp` | 1,967 | 104 | `CheckCall` 146 | 1 | none (split, see S6) |
| 32 | `editor/EditorPanelViews.cpp` | 1,947 | 38 | `DrawFieldListBodyImGui` 548 | 2 | #1160 |
| 33 | `engine/include/okay/VisualScript/NodesGameplayLib.hpp` | 1,902 | 17 | `Exec` 922; `Eval` 739 (header) | 2 | #1166, #1165 |
| 34 | `engine/include/okay/Components/ModelAnimator.hpp` | 1,867 | 110 | `Update` 200 (header) | 2 | none |
| 35 | `engine/src/Components/ActionList.cpp` | 1,834 | - | `ActionList::Update` 1,092 | 2 | none |
| 36 | `engine/include/okay/VisualScript/NodesLibrary.hpp` | 1,702 | - | `Eval` 1,626 (header) | 1 | none |

In numbers: 94 hand-written source files (outside tests and tools) have more than 1,000 lines and 25 have more than
2,000; 21 headers have more than 1,000 lines (their code is compiled again in every file that includes them).
Outside tests and tools, 38 functions are longer than 500 lines and 16 are longer than 1,500.

### 1.3 What the biggest files have in common

- **One giant function per file.** `DrawInspector` (12,804), launcher `main` (9,133, every page of the launcher),
  `RegisterBuiltins` (8,078), `ParseStep` (5,205), `WriteComponentRecords` (3,205), `EngineApi::Build` (2,299). A pure
  move of these gives a file that is just as hard to read. They need a refactor first: each block becomes its own
  function, then the functions move (section 3 marks these batches "refactor").
- **Tables written as code.** `RegisterBuiltins`, `EngineApi::Build`, `ParseStep`/`WriteComponentRecords` and the
  visual script `Eval`/`Exec` switches are long lists, one entry per command, record or node. The engine already has
  the right tool for the scene side: `ComponentSerializerRegistry` (65 records use it already, in
  `RegisterBuiltinComponentSerializers` and its helpers). Moving each record into a registry entry in its
  subsystem's file is the split.
- **Shared state as file globals.** `launcher/main.cpp` has 600 distinct `g_` globals and `editor/main.cpp` 575. They
  are why sections "need" each other (Part B.1, Part C).
- **Big headers.** `Mesh.hpp`, `VisualScriptEditor.hpp`, `SoftwareRenderer.hpp`, `SceneTemplates.hpp`, the node
  libraries: thousands of lines compiled again by every includer.

### 1.4 Where the code is: engine folders

Lines in `engine/include/okay/<Folder>` plus `engine/src/<Folder>`, largest first.

| Folder | Files | Lines | Folder | Files | Lines |
|---|---|---|---|---|---|
| Components | 321 | 78,971 | Input | 40 | 7,245 |
| Render | 125 | 58,031 | AI | 18 | 6,503 |
| Core | 137 | 37,654 | Audio | 23 | 6,392 |
| Scene | 59 | 34,417 | Animation | 13 | 5,086 |
| Platform | 59 | 17,117 | Debug | 21 | 4,789 |
| Net | 54 | 16,976 | Math | 25 | 3,506 |
| VisualScript | 41 | 16,814 | Optimize | 16 | 3,350 |
| Modeling | 43 | 13,331 | Inventory | 18 | 3,060 |
| Scripting | 7 | 12,609 | Testing | 11 | 2,944 |
| Physics | 35 | 9,841 | Graphics | 11 | 2,628 |
| Script | 8 | 7,902 | Crypto | 17 | 2,385 |
| UI | 30 | 7,759 | Texture, ECS, Environment, Combat | 28 | 4,426 |

What this shows against the nine subsystems the owner named:

- **Components** (256 component classes, 79k lines) holds the gameplay parts of every subsystem: physics bodies,
  animators, audio sources, network sync, UI widgets, vehicles. A subsystem's code is split between its own folder
  and `Components/`. The module manifests of PR #1158 already say which module owns each component file; that is
  the map used to group them (section 3, batch group G).
- **Platform** holds two jobs: the operating-system layer (`OwnWin32*`, `OwnLinux*`, `SdlBackend`, `Win32*`) and
  online services (`Account/`, `Friends`, `Forums`, `Messages`, `Achievements`, `Steam/`, `PlayFab/`, `Workshop`).
- **World streaming** has no home: `Optimize/StreamingGrid`, `Components/WorldStreamer`, `Texture/Stream`,
  `Audio/AudioStream`.
- **Scripting** is three folders: `Scripting/` (the OkayScript VM), `Script/` (typed OkayScript, the C#-style
  language: lexer, parser, checker, interpreter, `EngineApi`) and `VisualScript/`. The names `Script` and
  `Scripting` do not say which is which.
- **Graphics** (images, fonts) and **Render** overlap in name; Graphics is the image and font layer the renderer
  and OkayUI both use.

### 1.5 Dependencies between the top folders

Include lines from one top folder into another (tests left out of the "bad" column; they may include anything).

| From | Includes | OK? |
|---|---|---|
| editor | engine 400, okayui 81, shared 68, platform 5, **launcher 1** | `editor/main.cpp` includes `launcher/UpdaterCore.hpp`; `editor/CMakeLists.txt` puts `launcher/` on the editor's include path (3 targets) |
| launcher | shared 48, engine 26, okayui 6 | yes; never includes `editor/` |
| player | engine 33, shared 13, okayui 1 | yes |
| shared | engine 38, okayui 20 | yes; never includes a program folder |
| **engine** | **shared 7** | no: `Net/NetworkManager.cpp`, `Net/DedicatedServer.cpp`, `Net/ClockCheck.cpp`, `Net/RelayServer.cpp` include `shared/AntiCheat*.hpp` and `shared/OfficialGame.hpp`; `Graphics/EmbeddedFont.cpp` includes `shared/OkfDecode.hpp` and `shared/RobotoFontOwn.h`. The engine includes no `editor/`, `launcher/` or `player/` file. |
| **okayui** (library part) | engine 23, shared 22 (tests), editor 24 (tests), launcher 1 (test) | partly: `okayui/src`, `render`, `soft` include `Platform/Painter.hpp`, `Platform/Platform.hpp`, `Graphics/TtfFont.hpp`, `Graphics/Font.hpp` (base layers, fine) and `UI/UIPainterSDL.hpp` (the engine's in-game UI: one layer too high); `okayui/screens` includes `Debug/ConsoleModel.hpp` and `shared/` (it is program-level code living in the library folder) |
| tools | engine 20, editor 3, shared 1 | tools may include anything |

### 1.6 Circular dependencies inside the engine

- **Folder level:** counting `.cpp` and `.hpp` includes, 25 of the 27 engine folders form **one cycle** (every one can
  reach every other; only `Crypto` and `ECS` are outside it). `Components` alone includes 22 other folders.
- **Header level** (only `.hpp` including `.hpp`): 23 pairs of folders include each other, for example
  Core <-> Render, Core <-> Scene, Math <-> Render, Math <-> Scene, Render <-> Scene, Physics <-> Scene,
  Graphics <-> Render, Components <-> (10 folders).
- **The roots are a handful of misplaced headers**, which is good news: a few small moves break the low cycles.
  - `Render/Color.hpp` (99 lines, a plain colour value, includes only `Math/Mathf.hpp`) is included by
    `Math/ColorSpace.hpp`, `Core/Prefs.hpp`, `Core/Tween.hpp`, `Core/Subtitles.hpp`, `Graphics/Image.hpp`,
    `Graphics/ColorVision.hpp`: it makes Math, Core and Graphics depend on Render. It belongs in `Math/`.
  - `Math/IKMath.hpp` and `Math/JointLimit.hpp` include `Scene/Transform.hpp` (a component): IK maths that works
    on Transforms belongs in `Animation/`.
  - `Core/Application.hpp` includes `Render/Renderer.hpp`; `Core/GameSaves.hpp` and `Core/SaveFramework.hpp`
    include `Scene/Component.hpp`. The game loop and the save framework are layer 5 and 4 code living in `Core/`.
  - `Platform/Painter.hpp` includes `UI/UIPainter.hpp`, and `Graphics/TtfFont.cpp` includes `UI/`.
  - Higher up, the cycles are the "assembly" code (serializer, script VM, script bindings, renderer) naming every
    component. PR #1158's `scripts/module_deps_baseline.txt` lists about 800 such includes; they go away as the
    records and bindings move next to their components (batch groups S and G).
- **File level:** only one real header cycle: `Components/Buoyancy.hpp` <-> `Components/Water.hpp`.

### 1.7 Platform code inside shared code

Files that use `_WIN32`, `windows.h`, `winsock2.h`, SDL or X11 outside `engine/src/Platform/` and the renderer
backends (count of such lines in brackets):

- **Programs (allowed in `main`, too much today):** `launcher/main.cpp` (43), `editor/main.cpp` (35),
  `player/main.cpp` (18), `editor/BuildGame.cpp` (12), `editor/LaunchFile.cpp` (10), `editor/GameFiber.hpp` (7),
  `launcher/VoiceChat.hpp` (8), `launcher/VersionsManager.cpp` (5).
- **shared/:** `AppRestart.hpp` (17), `OkayThreads.hpp` (13), `CrashHandler.hpp` (12), `FileDialog.cpp` (11),
  `AntiCheatChecks.hpp` (6), `AntiCheatHardware.hpp` (5), `OwnSwitches.hpp`, `OwnStack.hpp`, `AntiCheatIntegrity.hpp`.
- **engine outside Platform:** sockets in five `Net/` files (`DedicatedServer.cpp` 10, `UdpSocket.cpp` 8,
  `ServerQuery.cpp` 7, `Matchmaker.cpp` 7, `MatchResults.cpp` 6); `Core/CoreThreads.hpp` (6), `Core/SaveFile.cpp` (5),
  `Core/Recovery.cpp` (4); `Crypto/Random.cpp` (7, the OS random source); `Input/Input.cpp` (5).
- **Three thread wrappers:** `shared/OkayThreads.hpp`, `engine/src/Core/CoreThreads.hpp` and the copy inside
  `Net/DedicatedServer.cpp` (CoreThreads' own comment says it follows "the same pattern DedicatedServer.cpp uses").
  One belongs in the platform layer; the others become thin forwarders (a duplicate in the sense of
  `docs/process/no_duplicates.md`).
- **Sockets:** `UdpSocket.cpp` is the socket seam, but four other `Net/` files call winsock directly.

### 1.8 Naming today

Measured per folder (counts of declarations matched by pattern; rough, but the picture is clear):

| Folder | Files | `.hpp` / `.h` | Function names | Private members | Constants | Globals |
|---|---|---|---|---|---|---|
| engine/include | 726 | 726 / 0 | PascalCase 10,292, camelCase 81 | `m_` 6,820, trailing `_` 399 | `kName` 516, ALL_CAPS 24 (macros) | 2 |
| engine/src | 429 | 45 / 0 (rest `.cpp`) | PascalCase 4,374, camelCase 111 | `m_` 4,019, trailing `_` 737 | `kName` 1,001 | 183 `g_` |
| editor | 191 | 138 / 0 | PascalCase 2,979, camelCase 77 | `m_` 436, trailing `_` 84 | `kName` 654 | 642 `g_` |
| launcher | 11 | 7 / 0 | PascalCase 700, camelCase 8 | `m_` 130 | `kName` 93 | 600 `g_` |
| shared | 74 | 70 / 0 (+3 font `.h`) | PascalCase 841, camelCase 17 | `m_` 318, trailing `_` 153 | `kName` 168 | 11 |
| okayui | 123 | 34 / 2 | PascalCase 1,808, camelCase 234 | `m_` 313, trailing `_` 457 | `kName` 95 | 292 `g_` |
| tools/okayms, okaycc | 151 | 20 / 2 | snake_case 610, camelCase 549 | trailing `_` 327 | `kName` 77 | 5 |

So the engine and the programs **already follow the owner's convention** for types, functions, files and constants:
PascalCase files with `.hpp`, PascalCase functions (97%+), `m_` private members (about 88%), `kPascalCase`
constants, ALL_CAPS only for macros (`OKAY_*`). Public data fields of components and plain structs are camelCase
without a prefix (`float yaw`, `tr->minVertexDistance`). File globals use `g_camelCase`. The exceptions are:

- about 1,800 private members with a trailing underscore (`valid_` in `Render/TriangleSpan.hpp:101`, `w_`, `h_`,
  `bias_` in `Optimize/OcclusionBuffer.hpp:110-113`, `indexDirty_` in `Components/Foliage.hpp:110`), most in
  okayui and engine/src;
- camelCase functions in okayui's internals (`deriveColor`, `pushClipSpan`, `reorderWindows`) and 66 lower-case file
  names in okayui (60 of them its tests, `test_okayui_*.cpp`);
- `tools/okayms` and `tools/okaycc`: C-compiler code in snake_case with lower-case files, a different style on
  purpose (section 6.3).

**Vague names, worst real examples** (161 functions in the engine and programs have a name of one or two letters;
most are fine, such as `Is`, `At`, `Id`, `Of`; these are not):

| Where | Name | What it really does |
|---|---|---|
| `engine/src/Core/FrameRecord.cpp:128` | `F(fmt, v)` | formats one number with a printf format into a string |
| `engine/src/Input/InputAsset.cpp:35` | `F(float)` | writes a float as `%g` text |
| `engine/src/Script/EngineApiInternal.hpp:73` | `F(const Value&)` | reads a script value as a float |
| `shared/AntiCheatBackend.hpp:405` | `B(o, k)` | reads the yes/no field `k` of a JSON object |
| `engine/include/okay/Components/SurvivalSystems.hpp:165-166` | `P(k, v)`, `G(k, def)` | save and load one float setting in Prefs |
| `engine/include/okay/Components/VehicleControlScheme.hpp:30-34` | `K`, `M`, `B`, `A`, `T` | make a key, mouse, pad button, pad axis and trigger binding |
| `editor/ProjectBrowser.cpp:21-22` | `P(s)`, `U(p)` | text to a file path, and a file path to UTF-8 text |
| `editor/AnimatorWindow.cpp:326`, `DialogueWindow.cpp:202`, `BehaviorTreeWindow.cpp:191/293`, `GuidedPanel.cpp:82`, `TimelineWindow.cpp:84` | `U()`, `D()`, `S()` | return the window's one static state object |
| `editor/UIMenuModel.hpp:20` | `I(id, label, tip)` | makes a menu item with no shortcut |
| `launcher/main.cpp:16884`, `17424` | `g_off`, `g_oa` | the state of the official-game installer, and of its admin page |
| `launcher/main.cpp:11133`, `12953`, `13474`, `14102` | `g_frCode`, `g_flMsg`, `g_adMsg`, `g_tkLink` | my friend code; the follow list's last message; the admin page's last message; the join link being checked |
| `launcher/main.cpp:7406` | `greacts`, `gmyreacts`, `fById` (parameters) | reactions per group message, my own reactions, friends by id |
| `engine/src/Scripting/OkayScriptVM.cpp:3761-3762` | `b`, `tf` | the built-in command table, and "the host object's Transform" |

**Magic numbers, examples:** `Physics3D.cpp:712` (`/ 200.0f`, "at most about 200 x 200 cells", explained only in a
comment), `Physics3D.cpp:1605` (`60`, `6000` solver pass limits), `Physics3D.cpp:2010` (`1000.0f` far point),
`Physics3D.cpp:2029` (`4096` loop guard), `launcher/main.cpp:2078` (`1160, 752` default window size). The good
pattern already exists in the same file: `kParallelBoundsMin = 256` (`Physics3D.cpp:848`).

### 1.9 Names that must not change (frozen)

These names are read by something outside the C++ code: saved files, scripts people wrote, other programs, the
command line, or players' settings. Changing one breaks old projects or tools. About **3,900 names in 11 groups**:

| Group | How many | Where they are defined | Example |
|---|---|---|---|
| OkayScript commands | 1,339 | `b["..."]` in `engine/src/Scripting/OkayScriptVM.cpp` (snake_case on purpose: the script language's style) | `print`, `debug_log`, `log_warn` |
| Typed OkayScript (C#-style) API | 298 | `prop(`, `ro(`, `method(` in `engine/src/Script/EngineApi_*.cpp` | `transform.position` |
| Visual script node tokens (and pin names) | 468 nodes | the token stored in `.okayvs` files; `docs/visual_scripting_nodes.md`; rule in `docs/visual_scripting_naming.md` | `OnStart`, `Delay` |
| Scene and prefab record tags | 357 written + 65 registry names | `out << "  <tag>"` in `engine/src/Scene/*.cpp`, `.name = "..."` entries | `transform`, `sprite`, `trailrenderer` |
| Scene record keys | 673 (75 of one or two letters) | `KvOut`/`KvIn` keys in `engine/src/Scene/*.cpp`, `Components/*.cpp` | `"c"`, `"w"`, `"g"`, `"mindist"` |
| Component class names and their public fields | 256 classes | `engine/include/okay/Components/` (shown in Add Component, the property registry, scripts and the Inspector) | `Rigidbody3D`, `TrailRenderer::minVertexDistance` |
| File extensions and project keys | 40 extensions | `.okay*` strings; `project.okayproj` keys | `.okayscene`, `.okayvs`, `modules=` |
| Command-line flags | 45 | editor 25, launcher 8, player 12 (`"--..."` in each `main`) | `--selftest`, `--build-game`, `--join` |
| Environment variables | about 302 | `getenv("...")` and `"OKAY_..."` strings | `OKAY_UI_DEFAULT_BACKEND` |
| Saved settings keys | 63 literal keys (more built at run time) | `Prefs::Set*/Get*("...")` | survival settings |
| Engine include paths and module names | 726 headers, 40 modules | `okay/<Folder>/<File>.hpp`; `engine/modules/*` (PR #1158) | `okay/Render/Mesh.hpp` |

Also frozen and not counted: network message layouts, the Supabase table and column names (`docs/sql/`), the keys
of `versions.json` and other release files, save game files, and the CMake option names (`OKAY_BUILD_EDITOR`...).

An include path is "soft frozen": a header may move if the old path keeps a one-line forwarding header
(`#include "okay/Math/Color.hpp"`) for at least one release, as `docs/own_code_roadmap.md` does for old code.
Removing a forwarding header later is a separate, announced change.

### 1.10 Open work that would collide (2026-11-23)

18 PRs are open. The ones that touch a file in section 1.2:

| PR | Branch | Big files it changes |
|---|---|---|
| #1166 | `claude/vs-new-nodes-1` | `VisualScriptEditor.hpp` (+5), `NodesGameplayLib.hpp` (+234), `test_vs_gameplay_library.cpp` (+831) |
| #1165 | `claude/vs-forward-fix-1` | `NodesGameplayLib.hpp`, `test_vs_gameplay_library.cpp` |
| #1163 | `claude/vs-editor-bugs-1` | `VisualScriptEditor.hpp` (+32/-9) |
| #1160 | `claude2/physics-no-classic` | `editor/main.cpp` (+47/-36: Project Settings, Vehicle and Joint OkayUI parts, `DrawInspector`, `DrawScene3D`), `Physics3D.cpp` (-1,342), `SceneSerializer.cpp`, `OkayUIEditorPanels.cpp`, `EditorPanelViews.cpp` |
| #1159 | `claude2/one-script-api` | `EngineApi.cpp`, `OkayScriptVM.cpp` (changes how commands are registered) |
| #1158 | `claude2/modular-s3-newproject` | `editor/main.cpp` (+33: self-test, file dialogs, New Project, Project Settings), `player/main.cpp`; adds the modules |
| #1155 | `claude2/one-navigation` | `editor/main.cpp` (`DrawInspector`) |
| #1142 | `claude/controller-2` | `editor/main.cpp` (`CompHeader`, `DrawInspector`) |
| #1140 | `claude2/dedup-small` | `editor/main.cpp` (`DrawAnimationEditor`, `DrawInspector`, `main`), `GLRenderer.cpp`, `D3D11Renderer.cpp`, `FbxScene.cpp` |
| #1138 | `claude2/active-ragdoll` | `editor/main.cpp` (Ragdoll OkayUI part, `DrawInspector`), `OkayScriptVM.cpp` (+35), `SceneSerializer.cpp` |
| #1120 | `claude2/texture-system` | `editor/main.cpp` (`DrawProfiler`, `main`), `SoftwareRenderer.hpp`, `GLRenderer.cpp`, `D3D11Renderer.cpp`, `player/main.cpp` |
| #1074 | `claude/windows-tests-fix-1` | `Mesh.hpp` (+6/-2) |

Nothing open touches `launcher/main.cpp`, `NetworkManager.cpp`, `OkayUI.cpp`, `ModelImport.cpp`, `UIRenderer.cpp`,
`Render/Color.hpp`, `Math/IKMath.hpp`, the editor's self-updater or the editor's script completion and docs
sections. Those go first. The other 6 open PRs (#1164, #1162, #1156, #1152, #1148, #1143) touch none of these files.

## 2. Target layout and dependency rules

The target is written once, in [architecture.md](architecture.md): the seven layers (base, platform, object model,
subsystems, gameplay, assembly, programs), the nine subsystems with their folders, and the three rules (the engine
never includes a program or `shared/`; platform code lives behind the platform interface; subsystems talk through
small interfaces). What has to change to get there, in short:

| Change | Kind | Batch |
|---|---|---|
| `Render/Color.hpp` to `Math/Color.hpp`; `Math/IKMath.hpp`, `Math/JointLimit.hpp` to `Animation/` | header moves with forwarding headers | B1 |
| `Core/Application`, `Core/GameSaves`, `Core/SaveFramework` out of the base layer (a new `okay/App/` for the game loop; saves to `okay/Save/`) | header moves with forwarding headers | B2 |
| the 7 `engine -> shared` includes: the anti-cheat client checks and `OfficialGame` move to `engine/.../Net/`, the embedded font data to `Graphics/` | file moves, `shared/` keeps forwarders | B3 |
| `launcher/UpdaterCore.hpp` to `shared/`; drop `launcher/` from the editor's include path | file move | B4 |
| `okay/Platform` split into OS (`Platform/`) and online services (`Online/`) | header moves with forwarding headers | P1 |
| one thread wrapper, one socket seam, `_WIN32` out of `shared/` and `Core/` | refactor behind the platform interface | P2-P4 |
| `okay/World/` for streaming | header moves with forwarding headers | W1 |
| components grouped with their subsystem | after the module manifests merge (#1158) and only where a module owns the file | G |
| editor: one file pair per window; `editor/VisualScript/` | Part B moves; one whole-folder move | E, V1 |

What is **not** planned: renaming the `okay/<Folder>` include paths wholesale, moving `Components/` into
sub-folders in one go, or changing `.hpp` to `.h` (section 6.2). Each of those would touch thousands of include lines
in every open branch for little gain.

## 3. The batches, ranked

Rules for the order (the same as Part B.2, for the whole repository): the owner asked for the biggest and hardest
files first, so the two `main.cpp` files and the big engine files lead; inside them, sections that are large,
self-contained (few names needed from the rest of the file), rarely edited and not inside an open PR go first. A
batch that needs a refactor before it can move ("refactor" in the table) is its own PR with its own tests, before
the move. One batch per PR. Sizes are lines today; re-measure before each batch.

| # | Batch | Size | Kind | Why here | Touched now by | Risk |
|---|---|---|---|---|---|---|
| **1** (done on `claude2/editor-split-2`) | **E2** editor self-updater (`namespace updater`, main.cpp 1850-2121) to `editor/Updater.hpp/.cpp` | 272 | pure move | next in Part B's order; its 4 declarations leave `MainShared.hpp` (the "list shrinks" rule); a warm-up for the process | nobody | low |
| **2** (done on `claude2/editor-split-2`) | **E3** editor script completion, signatures, C# members and the Scripting Reference (main.cpp about 12758-14958: `DrawScriptDocs` to just before `DrawScriptEditor`) to `editor/ScriptAssist.hpp/.cpp` | about 2,200 | pure move | 1% churn, used only by the Script Editor; it was waiting for #1128, which has merged | nobody (no open PR has a hunk there) | low |
| **3** | **N1** `NetworkManager.cpp` into `NetworkManager.cpp` (lifetime, sessions, identity: lines 1-1000), `NetworkManagerSync.cpp` (spawn, despawn, deliver, 1000-2000), `NetworkManagerHits.cpp` (lag compensation, hit checks, life and death, 924 and 2000-3000), `NetworkManagerServer.cpp` (`ServerTick`) and `NetworkManagerClient.cpp` (`ClientTick`) **Done (2026-11-23, `claude2/modularize-net-cycle`):** `NetworkManager.cpp` (1,163 lines: lifetime, host identity, sessions, join gate, launch join and `Start`, hit claims and checks, life and death, peers, requests, reconnect, kick, roster), `NetworkManagerSync.cpp` (`Update`, remote objects, snapshots and interpolation, `Deliver`, spawn and despawn, synced vars, the reliable, ordered and sequenced channels, encryption, datagrams and fragments, `Send`), `NetworkManagerAuthority.cpp` (lag compensation, network prefab path checks, anti-cheat checks, movement authority and prediction), `NetworkManagerServer.cpp` (`ServerTick`), `NetworkManagerClient.cpp` (`ClientTick`), and the message types, constants and five packet helpers in `engine/src/Net/NetworkManagerInternal.hpp` (an anonymous namespace: no state, the helpers marked `inline`). Changed from the plan: the hit checks and life and death stay in `NetworkManager.cpp`, and the third file is called `NetworkManagerAuthority.cpp`. The reason: `NetNoteDeath`/`NetNoteRespawn` (just after them) use the live-manager lock (`std::lock_guard`), which must stay in one file with its `<mutex>`; with the hit code moved out around them, `git diff` lost track of those lines and `branch_lint` reported them as new `std::lock_guard` lines (rule 21a). Each anonymous-namespace helper moved with its users. Pure move: `docs/engine/moves/N1_network_manager.rules`. | 4,291 | pure move | the biggest engine file nobody is editing (5 commits, no PR); methods of one class can sit in several `.cpp` files, so `NetworkManager.hpp` and every caller stay untouched; the file-local helpers go into a private `engine/src/Net/NetworkManagerInternal.hpp` | nobody | low to medium (anonymous-namespace helpers must keep internal linkage: see 4.6) |
| **4** | **L1** start the launcher split: `launcher/LauncherShared.hpp` (the launcher's `MainShared.hpp`) and the first section, emoji, chat text formatting and link previews (main.cpp 2654-3516), to `launcher/ChatText.hpp/.cpp` | 863 | pure move | the second-largest file, nobody editing it; this section needs only 8 names from the rest of the file (Part C) | nobody | low |
| **5** | **B1** base-layer cycle breakers: `Render/Color.hpp` to `Math/Color.hpp`, `Math/IKMath.hpp` and `Math/JointLimit.hpp` to `Animation/`, each old path left as a one-line forwarding header **Done (2026-11-23, `claude2/modularize-net-cycle`):** the three headers moved (`git mv`, no edits except `JointLimit.hpp`'s own include of `IKMath.hpp`), forwarding headers at the old paths (`docs/engine/moves/B1_base_layer_headers.rules`). The six base-layer callers named in 1.6 (`Math/ColorSpace.hpp`, `Core/Prefs.hpp`, `Core/Tween.hpp`, `Core/Subtitles.hpp`, `Graphics/Image.hpp`, `Graphics/ColorVision.hpp`) now include `okay/Math/Color.hpp`; every other caller still uses the old path. Measured (headers only, folder pairs that include each other): 23 before, 21 after; Math <-> Render and Graphics <-> Render are gone. Math <-> Scene became Math <-> Animation, because `Math/CCD.hpp` and `Math/Fabrik.hpp` (IK solvers) still include `Math/IKMath.hpp`, now a forwarder to `Animation/`: moving those two headers to `Animation/` as well (a follow-up header move) takes Math out of the big cycle. Core <-> Render stays (`Core/Application.hpp`, the 4th of 4). | about 400 | header move | removes Math -> Render, Math -> Scene, Core -> Render (3 of 4), Graphics -> Render: the base of the one big cycle (1.6); nobody includes a different path, so no caller changes | nobody | low |
| 6 | **L2-L8** the rest of the launcher, one section per PR, cleanest first: anti-cheat backend and tickets (643, needs 12), cover art and avatars (167), sidebar icons (264), reports (1,013), public profiles (943), member moderation (1,024), multiplayer (1,691), chat extras (1,151), the chat window and voice calls (2,553 + 845) | 10,300 | pure move | Part C; the chat and voice sections need over 100 names each, so they go after the small ones have built up `LauncherShared.hpp` | nobody | medium |
| 7 | **O1** `okayui/src/OkayUI.cpp` by widget family, next to the existing `OkayUI_FontAtlas.cpp`: `OkayUI_Windows.cpp` (windows, children, scrolling), `OkayUI_Menus.cpp` (popups, menus, selectables), `OkayUI_Tables.cpp`, `OkayUI_Tooltips.cpp`, `OkayUI_DrawList.cpp` (shapes and paths) | 7,809 | pure move | big, many small functions, no PR; the file already starts a split (`OkayUI_FontAtlas.cpp`) | nobody | medium (shared file-static state must go into a private header) |
| 8 | **M1** `Render/Mesh.hpp`: keep `struct Mesh` and its small accessors in `Mesh.hpp`; move the bodies of the shape generators (lines 1000-2000), UV projection, the modifiers (2000-5000) and the OBJ reader and writer out of the class into `engine/src/Render/MeshShapes.cpp`, `MeshUV.cpp`, `MeshModifiers.cpp`, `MeshObj.cpp` | 5,170 | refactor (declarations stay in the struct; bodies move) | every file that includes `Mesh.hpp` compiles 5,000 lines today; after #1074 merges | #1074 (6 lines) | medium: only rarely-called functions leave the header, so no speed is lost; small hot accessors stay inline (check with the perf tests, `docs/engine/perf_log.md`) |
| 8a | **M2** `Render/SoftwareRenderer.hpp` (2,894 lines): the file is free `inline` functions in `namespace okay`, not a class. Keep the structs, `Raster`, the settings accessors, the texture cache, the shadow lookups used per pixel (`ShadowFactor`, `LocalShadowFactor(s)`) and every declaration (with default arguments) in the header; move the bodies of the per-frame functions out: shadow map passes (`RenderCascadeDepth`, `BuildCascade`, `RenderShadowMap`, `ComputeDirectionalShadowVP`, `RasterLocalDepth`, `RenderLocalShadowMaps`, 322 lines) to `engine/src/Render/SoftwareRendererShadows.cpp`; post effects (`ApplySSAO`, `ApplyBloom`, `ApplyToneMap`, `ApplyColorGrade`, `ApplyScenePost`, `ApplySceneDof`, `ApplyFXAA`) to `SoftwareRendererPost.cpp`; `RenderMeshes` (620), `RenderMeshesSS` and `FillOcclusionDepth` to `SoftwareRendererMeshes.cpp`. Header 2,894 to 1,507 lines. Rules: `docs/engine/moves/M2_*.rules` | 1,390 | refactor (declarations stay; bodies move; `inline` and default arguments dropped from the definitions) | every file that includes `SoftwareRenderer.hpp` (about 40, `Okay.hpp` among them) compiled 2,900 lines; those that call these functions must link `okay::engine` | nobody | low: each moved function runs once per frame and its inner per-pixel loops compile inside the same `.cpp`; the per-pixel shadow lookups stay inline |
| 8b | **M3** `Scene/SceneTemplates.hpp` (2,022 lines, all free `inline` functions in `okay::Templates`): keep `kHudTop`, the small premade-player helpers, `detail::Block` and every declaration (with default arguments) in the header; the 26 function bodies of 25 lines or more (the scene builders from `Platformer` to `BoatLake`, `VehicleSeats3D` 166, `VoxelSandbox` 137, `MainMenu` 127, `BrickBreaker` 108, plus `AddTemplateHoldables`, `AddTopDown2DPlayer`, `WireCharacterIK`, `AddBoat`) to `engine/src/Scene/SceneTemplatesBuilders.cpp`. Header 2,022 to 433 lines. Rules: `docs/engine/moves/M3_scene_templates.rules` | 1,590 | refactor (declarations stay; bodies move; `inline` and default arguments dropped) | every file that includes `SceneTemplates.hpp`; callers must link `okay::engine` | nobody | none: the builders run once when a scene is made; the two `VoxelSandbox` overloads stay distinct so `&Templates::VoxelSandbox` still converts to `void (*)(Scene&)` |
| 8c | **M4** `Components/ModelAnimator.hpp` (1,867 lines, class `ModelAnimator`): the bodies of `Update` (200), `DirectionalBlendStep`, `SmoothBlendStep`, `BlendClips`, `BlendMany`, `YawLowerBody`, `PoseWeighted` and `PoseLayer` (627 lines) to `engine/src/Components/ModelAnimatorUpdate.cpp`, as `ModelAnimator::Name` definitions (the class keeps their declarations with `override` and default arguments; the bodies keep their indentation so the move is verbatim). Header 1,867 to 1,240 lines. The small accessors, slot helpers, `ApplyPose`, `SampleNode`, `Bind` and `RootDelta` stay inline. Rules: `docs/engine/moves/M4_model_animator.rules` | 627 | refactor (declarations stay; bodies move) | every file that includes `ModelAnimator.hpp` | nobody | low: `Update`, `BlendClips` and `BlendMany` are virtual, so they were never inlined into callers; the per-bone helpers they call stay inline in the header, visible to the new file |
| 9 | **B2-B4** the rest of the layer fixes (section 2 table) | small | header and file moves | finishes "the engine never includes `shared/`" and "editor never includes `launcher/`" | nobody | low |
| 10 | **E4-E19** the rest of Part B's order (Animation tab after #1140, Find in Files, Asset Manager, plugins...) | about 45,000 | pure move, then refactor for `DrawInspector` | Part B.2 | see Part B.2 | as Part B |
| 11 | **I1** `engine/src/Render/ModelImport.cpp` by format family and step (scene import, character root, materials) | 3,850 | pure move | no PR, but 21 commits in 10 days: claim on #422 first | nobody now | medium |
| 12 | **U1** `engine/src/UI/UIRenderer.cpp`: `RenderSceneUIImpl` (1,693 lines) split into one function per widget kind, then moved | 3,384 | refactor, then pure move | no PR | nobody | medium (drawing order must not change; the UI image tests compare pixels) |
| 13 | **S1** `SceneSerializer.cpp`: each record in `WriteComponentRecords` and `ParseStep` becomes a `ComponentSerializerRegistry` entry (the 65 existing entries show the form), then the entries move to one file per subsystem (`SceneRecordsPhysics.cpp`, `...Render.cpp`...) | 11,126 | refactor, then pure move | 40 commits and 2 open PRs: after #1160 and #1138 merge, in a quiet week; the scene round-trip tests and old scene files in `tests/` must load byte for byte the same | #1160, #1138 | high (file format: a test must compare the written text before and after) |
| 14 | **S2** `OkayScriptVM.cpp`: `RegisterBuiltins` (8,078 lines) into one `Register<Topic>Builtins(table)` function per topic (it already has about 40 `// ----` topic headers: Debugging, Object pools, NPC commands, Navigation, Multiplayer, Inventory, Tweening...), then each topic to its own file | 12,236 | refactor, then pure move | after #1159 (one command table for both languages) merges: that PR changes the same lines and may make this simpler | #1159, #1138 | medium (command names are frozen; a test lists every name before and after) |
| 15 | **S5 (done, section 3.1)** `EngineApi.cpp::Build` (2,299) the same way, per type | 2,465 | refactor, then pure move | after #1159 | #1159 | medium |
| 16 | **V1** `editor/VisualScriptEditor.hpp` into a header plus `.cpp` files (`Draw` is 2,444 lines), and the 39 `VisualScript*`/`Vs*` files into `editor/VisualScript/` | 6,153 | refactor, then whole-file moves | 2 open PRs (#1166, #1163) and the visual-scripting work is busy: after they merge, claimed on #422 | #1166, #1163 | medium |
| 17 | **P1-P4** platform: split `okay/Platform` into OS and `okay/Online`; one thread wrapper; one socket seam; `_WIN32` out of `shared/` and `Core/` | about 17,000 touched | header moves, then refactors | needs the Windows cross-compile job for every step (most of this code is not compiled on Linux) | nobody | medium to high (Windows-only code: unverified until CI's Windows build passes) |
| 18 | **R1** renderer backends: `RenderToPixels` in `GLRenderer.cpp` (786), `D3D11Renderer.cpp` (798), `SoftwareRenderer.hpp::RenderMeshes` (620) split into passes (shadows, opaque, transparent, post) | 8,000 | refactor | after #1120 and #1140 merge; the renderer image tests compare pixels | #1120, #1140 | high (GPU code; D3D only checked by CI on Windows) |
| 19 | **G** components next to their subsystem, one module at a time, with forwarding headers | 79,000 | header moves | only after #1158 merges and its manifests say who owns each file | #1158 | low per file, large in total |
| 20 | **W1** `okay/World/` for the streaming pieces | about 1,000 | header moves | small, after B1-B4 | nobody | low |
| 21 | **X1** player `main` (1,936) and launcher `main` (9,133): one function per page or step, then moved | 11,000 | refactor | last: every page's batch must have moved first; `main` is touched by most PRs | #1158, #1120 | medium |
| 22 | **T1** (done, 3.1) tools: `tools/okayms/src/compile.cpp` into lexer, parser, type checker and code generator files; `tools/okaycc/src/parser.cpp` the same | 24,000 | pure move | big, but a separate program with its own build (`tools/okayms/build.sh`) and tests; whoever owns the compiler does it | nobody (25 and 29 commits) | low for the engine |
| 23 | tests: `test_vs_gameplay_library.cpp` (`main` is 6,399 lines) into one function per node group | 6,645 | refactor | only when someone is working there anyway | #1166, #1165 | low |

### 3.1 Batches done

| Batch | Branch | What moved | Checks |
|---|---|---|---|
| I1, part 1 (taken in place of S2, which needs a refactor first) | local worktree | `engine/src/Render/ModelImport.cpp` tail (`AnalyzeModel` to `FixCharacterAnimations`, 1,165 lines) to `ModelImportCharacter.cpp`; `Lower` and `BaseName` leave `static` and are declared in `ModelImportInternal.hpp` under `okay::model_import` | `check_pure_move.py` exit 0 with `docs/engine/moves/S2_model_import_character.rules`; engine and 58 test targets built; 65 of 67 matching tests pass (2 failures are not from this change) |
| I1, part 2 | local worktree | `engine/src/Components/Character.cpp` first-person owner arm block (`SetOwnerArmActive` to `FinishTwoHands`, with `FirstPersonHands::Update`; 817 lines) to `CharacterFirstPerson.cpp`; `OwnerCamera` leaves the anonymous namespace and is declared in `CharacterFirstPersonInternal.hpp` under `okay::character_fp` | `check_pure_move.py` exit 0 with `docs/engine/moves/S3_character_first_person.rules` (one ambiguous-brace drop/add pair, see the rules file) |
| I1, part 3 | local worktree | `engine/src/Animation/HumanoidActions.cpp` runtime tail (`ActionNames` to `Seat::Update`: queries, play, death, swim, emotes, sit, hit, dodge, traversal, `Seat`; 727 lines) to `HumanoidActionsRuntime.cpp`; the two top anonymous namespaces became `okay::HumanoidActions::humanoid_actions`, declared in `HumanoidActionsInternal.hpp` | `check_pure_move.py` exit 0 with `docs/engine/moves/S4_humanoid_actions_runtime.rules` |
| S3 | local worktree | `engine/src/Render/BlendReader.cpp` (2,109 lines, everything inside one unnamed namespace): the reader's types (container and SDNA parsing, `Ref`, math helpers, `MeshData`, the `Reader` struct, 1,100 lines) to the private header `BlendReaderInternal.hpp`, and the build stage (`Reader::Run`, `BuildNodes`, `BuildParts`, `BuildPartsOf`, `EvaluateModifiers`, `BuildAnimation`, `Notes` and the part helpers, 893 lines) to `BlendReaderBuild.cpp`. `BlendReader.cpp` keeps `Gunzip` and the public entry points (103 lines). The unnamed namespace became `okay::blnd`; the eleven plain math functions got `inline`. `tests/CMakeLists.txt` lists the new `.cpp` where tests compile `BlendReader.cpp` themselves | `check_pure_move.py` exit 0 with `docs/engine/moves/S3_blend_reader_build.rules` (11 `inline` change rules, 2 drop rules) |
| S4 | local worktree | `engine/src/Render/FbxScene.cpp` (2,282 lines): the builder's types and helpers (matrices, properties, layers, curves, `Builder`, 1,305 lines) to the private header `FbxSceneInternal.hpp`, and the animation reader (`Builder::Animation`), the clip-key `Animator` and `BuildClips` (618 lines) to `FbxSceneClips.cpp`. `FbxScene.cpp` keeps the polygon-to-parts step and the public entry points (337 lines). The unnamed namespace content moved into `okay::fbx::scene_build`, opened with `using namespace scene_build;`; 23 plain functions got `inline`. `tests/CMakeLists.txt` and `platform/xbox_uwp/CMakeLists.txt` list the new `.cpp` where a test compiles `FbxScene.cpp` itself | `check_pure_move.py` exit 0 with `docs/engine/moves/S4_fbx_scene_clips.rules` (23 `inline` change rules, 1 add rule) |
| U1 | local worktree | `engine/src/UI/UIRenderer.cpp` (3,384 lines): the scene pass (the pass cache, `FrameXformPainter` and `RenderSceneUIImpl`, about 2,000 lines) to `UIScenePass.cpp`; `UIRenderer.cpp` keeps the drawing helpers, shapes, text and `RenderSceneUI` (1,378 lines). What the two share (the pass state globals, `UITextScale`, `RichLinkTable`, `DrawRichRuns`, the entry point) is declared in `UIRendererInternal.hpp` under `okay::ui2d::ui_pass` | `check_pure_move.py` exit 0 with `docs/engine/moves/U1_ui_scene_pass.rules` (1 edited line: `RenderSceneUIImpl` is no longer `static`); engine and 48 test targets (UI, prefab and script tests) built with `ninja -j2`, 48 of 48 pass after `test_ui_layer_table.cpp` was pointed at `UIScenePass.cpp` (it reads the source text) |
| P1 | local worktree | `engine/src/Scene/PrefabTools.cpp` (1,986 lines): the reader and resolver stage (text helpers, raw documents, deltas, `Normalize`, `Ctx`, `ReadPrefabText`, `BuildDoc`, `ResolveFile`; 763 lines) to `PrefabResolve.cpp` and `PrefabInternal.hpp` under `okay::prefab_text`; `PrefabTools.cpp` is 1,266 lines | `check_pure_move.py` exit 0 with `docs/engine/moves/P1_prefab_resolve.rules` (1 edited line: `RecordKey` loses its default argument, which the declaration carries) |
| S1 (no refactor needed: both giant functions are already whole blocks) | local worktree | `engine/src/Scene/SceneSerializer.cpp` (11,160 lines to 1,400), in seven pure moves: the shared record helpers (`Quote`, `KvIn`/`KvOut`, curve, list and stance helpers, `FilePath`, `g_loadingSceneDir`, `WriteDisabledComponents`, `ApplyComponentOrder`, `ParseSession`; about 540 lines) to the private header `SceneSerializerInternal.hpp` and `SceneSerializerHelpers.cpp`, namespace `okay::sceneser`; `RegisterBuiltinComponentSerializers` and the generic property path (769 lines) to `SceneSerializerRegistry.cpp`; `WriteComponentRecords` (3,218 lines) to `SceneSerializerWrite.cpp`; `ParseStep` (5,200 lines) to `SceneSerializerParse.cpp` (no longer `static`, defaults moved to its declaration) | `check_pure_move.py` exit 0 for each move with `docs/engine/moves/S1_sceneserializer_*.rules`; engine and the matching test targets built, run results in the PR |
| (not split) `engine/src/Script/Checker.cpp` | - | 1,967 lines, of which 1,820 are one class, `Checker`, with every method defined inside it; the rest is 110 lines of type helpers. There is no block that can move without cutting the class, so no pure move exists. It needs a refactor first (methods defined outside the class, then one file per group: statements, expressions, calls) | - |
| S6 (the refactor, then the moves; rules file S5_checker.rules) | this PR | `engine/src/Script/Checker.cpp` (1,967 lines, one `class Checker` with every method defined inside it). **Refactor (behaviour-neutral, not a pure move):** the class (declarations, 91 methods turned into out-of-class definitions) and the type helpers it needs (`EditDistance`, `Suggest`, `LocalVar`) go to the private header `engine/src/Script/CheckerInternal.hpp` (245 lines, namespace `okay::script::checker_detail`, replacing the unnamed namespace); in `Checker.cpp` each method body is unchanged apart from one level of indentation, only the signature line became `Checker::Name(...)`; the member template `Names` stays in the class. **Moves (pure, one commit each):** `CheckerOperators.cpp` (`CheckUnary` to `CheckConditional`, 229 lines), `CheckerCalls.cpp` (`PickOverload` to `CheckNewObject`: overloads, calls, GetComponent, collections, LINQ, new; 435), `CheckerStatements.cpp` (`CheckBlockBody` to `CheckCondition`; 226), `CheckerTypes.cpp` (`Fold` to `CheckItems`: constants, type names, conversions; 215), `CheckerExpressions.cpp` (`VisibleNames` to `CheckMember`: names, lambdas, arrays, members; 411). `Checker.cpp` keeps `Run`, the small accessors, `DeclareMembers`, `CheckEnums`, fields, methods and scopes, `Check` and `Compile` (about 450 lines). Rules: `docs/engine/moves/S5_checker.rules`. | `check_pure_move.py`: PURE MOVE for each of the five moves (no edited line; 13 scaffolding lines per new file). The refactor commit is not covered by it. `okay_engine` and 45 script test programs built (ninja exit 0); 47 script, diagnostics and module tests (the 45 plus `check_modules_script`, `module_sizes_script`) all passed. |
| S2 (the refactor, then the moves) | this PR | `engine/src/Scripting/OkayScriptVM.cpp` (12,242 lines) split. **Refactor (`acfd6d67a`, behaviour-neutral):** `RegisterBuiltins` (8,000 lines) became 58 `Register<Topic>Builtins(BuiltinCtx&)` members of `Impl`, called in the original order. The shared closures (`tf`, `go`, `sceneOf`, `findNamed`, `alias`, `selfObj`, `charSelf`, `modelSelf`, `smSelf`) became small functor types held by `BuiltinCtx`; each topic copies the ones it uses at its top. The other locals (`joinArgs`, `uiS`, `at`, `tweens`, `lookup` ...) turned out to be used by one topic only. **Header (`d480a0c31`):** `OkayScriptVMInternal.hpp` (private, `okay::script_vm`) holds the includes, `Value`, JSON, lexer, `Runtime`, the object helpers, `IsComponentRef`, `SharedApiCall`, `BuiltinCtx` and the `Impl` class; `RealFps` is declared there. The VM's own parser, expression nodes and the `Runtime` methods stay in `OkayScriptVM.cpp` (now 3,065 lines, in `okay::script_vm`). **Moves (each commit its own check):** topics now live in `OkayScriptVM_Ui.cpp` (868), `_Input` (305), `_Gameplay` (1,067: debug, motion, score and spawn, pools, named objects, NPC, spawner, build mode, movement, vehicles), `_Camera` (267), `_World` (768), `_Navigation` (229), `_Animation` (718), `_Inventory` (262), `_Network` (1,032: net, matchmaking, matches, server browser, Steam, friends, lobbies, PlayFab, accounts), `_Math` (676), `_Physics` (837), `_Tween` (383), `_Alias` (427), `_Scene` (551). The coroutine engine of `Runtime` (thread, mutex, condition variable) also left the header for `OkayScriptVM_Coroutine.cpp` (115), because `branch_lint` refuses thread types in a header (`S2_okayscriptvm_coroutine.rules`). The header is 1,234 lines. Rules: `docs/engine/moves/S2_okayscriptvm_header.rules`, `S2_okayscriptvm_topics.rules`. | `check_pure_move.py`: PURE MOVE, exit 0, for the header commit, the 13 topic-file moves, the `IsComponentRef` move and the coroutine move (the header one allows the 6 `static` to `inline` edits, `namespace {` to `namespace script_vm {`, and the in-class to out-of-class function lines). Built: `okay_engine` and the 47 test programs matching `script\|vm\|okayscript\|builtin`, ninja exit 0; `ctest -R` on the 56 tests: 54 pass, 2 fail (`gen_feature_map_script`, `publish_content_script`: Python tests that do not use the VM; unverified whether they fail on main too). Command names: `BuiltinNames()` lists 1,555 names before (origin/main object linked into a test) and after, identical. `dup_lint.py check`: 0 errors (3 moved-line false positives carry a `dup-allow` note; 2 baseline entries removed). Not verified: Windows (MinGW) build of the new files. | Parser, expression nodes and the `Runtime` methods (about 2,700 lines) are still in `OkayScriptVM.cpp`: they can go to `OkayScriptVM_Parser.cpp` and `_Runtime.cpp` the same way. `OkayScriptVM_Gameplay.cpp` and `_Network.cpp` are the biggest and can be split by topic. | nobody | low |
| T1, part 1 | `claude2/modularize-tools` | `tools/okayms/src/compile.cpp` (15,052 lines) split by job, in the file's own order: `compile_internal.hpp` (tokens, types, syntax tree, symbol tables, the cross-file declarations), `preprocess.hpp/.cpp` (macros, `#include`, `#if`), `lex.cpp` (lexer, token kinds), `mangle.cpp` (MSVC decorated names), `const_fold.cpp` (constant folding), `parser.hpp` (`struct Parser`: declarations, types, classes, templates; its in-class bodies stay in the class, so the header is 5,736 lines), `parse_stmt.cpp`, `parse_call.cpp`, `parse_expr.cpp` (the out-of-class parser methods: statements; packs, calls and operator functions; expressions including `parse_unary`), `gen.hpp` (`struct Gen`), `gen.cpp` (addresses, loads, stores, statements), `gen_expr.cpp` (`Gen::gen_expr`), `optimize.cpp` (constant propagation, inlining), `emit.cpp` (`emit_functions`, the object file). `compile.cpp` keeps `compile_c` (82 lines). The old anonymous namespace became `okayms::compiler`. No CMake list: `tools/okayms/build.sh` compiles every `src/*.cpp`. Rules: `docs/engine/moves/T1_okayms_compile.rules`. | `check_pure_move.py`: PURE MOVE (4 allowed edits: `peel_array` made `inline` in the header, 2 default arguments moved to declarations, `has_effects` no longer `static`; 27 declaration lines added). `tools/okayms/build.sh` before and after: the same 64 test programs, `checks passed 1670, failed 443` both times, logs identical apart from timings (every failure is a missing `wine` or MinGW runner, exit 127, on this machine). Same output: 300 objects from `examples/`, `linktests/`, `runtime/` and okaycc's C and C++ test programs, each with no flag, `/O1`, `/O2` and `/Z7 /O2`, byte-identical between the old and new `okay-cl` (44 inputs fail the same way in both). MinGW `-fsyntax-only -Wall -Wextra -Werror` clean. Not verified: the Windows-run tests (no wine here). Follow-up (a refactor, not a move): move `Parser`'s in-class bodies out of the class so `parser.hpp` shrinks. |
| T1, part 2 | `claude2/modularize-tools` | `tools/okaycc/src/parser.cpp` (9,211 lines) split by job, in the file's own order: `ast.hpp` (the syntax tree, the template tables, the cross-file declarations), `ast.cpp` (copies, the printed tree, type queries), `mangle.cpp` (Itanium mangled names), `templates.cpp` (template arguments, instance requests, mangled types), `check.cpp` (`class Checker` with `checkCall`, and `typecheckTu`), `lower.cpp` (`class Lowerer`, `lowerProgram`, `lowerSource`, `setForceExceptions`). `parser.cpp` keeps `class Parser` (fully in-class), `frontEnd` and the public `astOf`/`checkSource` functions (3,830 lines). The old anonymous namespace became `okaycc::compiler`. `tools/okaycc/build.sh` lists the five new `.cpp` files (there is no CMake list). Rules: `docs/engine/moves/T1_okaycc_parser.rules`. | `check_pure_move.py`: PURE MOVE (no edited line; 27 declaration lines and 2 `using namespace compiler;` added). `dup_lint.py` counts its "hand-made Trim / ToLower" rule per file, so the 7 hits that moved to `lower.cpp` failed it: they are false positives (`lowerStmt`, `lowerIf` and the other tree-lowering methods match the word "lower"), so each has a `// dup-allow:` comment line above it, and the `tools/okaycc/src/parser.cpp: 7` entry left `scripts/dup_baseline.json` (it shrank). `tools/okaycc/build.sh` before and after: identical logs apart from timings (lexer 69, preproc 71, parser 18, typecheck 32, ir 7, codegen 86, elf 68, diff 51, cppdiff 27, link 2, libc 1, var 1, coff 1 passed, 0 failed; "plan check passed"). Same output: 88 test programs (`progs`, `progs_cpp`, `progs_cpp_multi`) through the old and new `okay-cc` with `--ast`, `--check`, `--ir`, `--asm`, `--elf`, `--obj`: 528 runs, all identical (text, exit code and written files). MinGW `-fsyntax-only` clean. |
| O1, part 1 | merged earlier | `okayui/src/OkayUI.cpp` (7,809 lines, now 6,312) by widget family, in four commits: `OkayUIInternal.hpp` (284 lines; namespace `OkayUI::ui_core`: the structs, constants and small inline helpers the families use moved there, the shared variables got `extern` lines and the shared functions prototypes, their definitions stay in `OkayUI.cpp`; the toolkit's big anonymous namespace of state and four smaller blocks became `ui_core`), then `OkayUI_Tooltips.cpp` (166 lines), `OkayUI_DrawList.cpp` (536: shapes, paths, textured polygons, the window and foreground list handles) and `OkayUI_Tables.cpp` (664: columns and tables), all added to `okayui/CMakeLists.txt`. Rules: `docs/engine/moves/O1_okayui_split.rules`. The remaining two families are in part 2 (next row). | `check_pure_move.py`: PURE MOVE for all four commits (step 1 uses 97 added-by-rule lines (`extern` lines, prototypes, the changed definition lines) and 21 dropped-by-rule lines; `hit`, `fMeasure`, `availW` and `hashLabel` lost `inline`, so they are now ordinary functions; `fillCircle` and `scrollbarThumb` lost their default arguments to the prototype). Public symbols (`nm`) of the old `OkayUI.o` and of the four new objects joined with `ld -r`: the same 439, no undefined `ui_core` symbol. Build in `/tmp/okayui_split_build` (Release, `ninja -j2`, exit 0): `okay_ui` and all 67 test targets of `ctest -N -R "okayui\|ui_toolkit\|okay_ui"`; `ctest`: 67 of 67 passed, none "Not Run", including `okayui_golden`, `okayui_render`, `okayui_tables`, `okayui_windows`, `okayui_capture`. `branch_lint.py --base origin/main all`: exit 0. Not verified: the Windows (MinGW) cross-compile, and any run of the editor or launcher with the split library. |
| O1, part 2 | merged earlier | `okayui/src/OkayUI.cpp` (6,312 lines, now 4,894) in three more commits: step 1 grows `OkayUIInternal.hpp` (284 to 467 lines: the structs `MenuKb`, `CurMenu`, `PopupCtx`, `ChildSave`, `WinSlot`, `NextCollapse`, `VisLabel`, `OvRect`, `PopRect`, `WinRect`, `WinRec`, `CurWin`, `WinEnd`, `NextWin` and the small helpers `fW`, `fCharW`, `visW`, `leftEdge`, `fullW`, `cursorX`, `widthOverridden`, `realAvailW`, `rnd`, `registerOverlay`, `coveredByHigher`, `registerWindowRect`, `wrecBegin`, `wrecEnd`, `hitOverlay`, `inClip`, `clampPx` moved there, all still `inline`; `extern` lines for about 55 more variables; prototypes for `kbSlot`, `openState`, `panelColored`, `framedComboBox`, `chevron`, `xMark`, `softShadow`), then `OkayUI_Menus.cpp` (636 lines: menu bar, menus, `MenuItem`, `Selectable`, popups, modal, combo, context popup, and `AnyPopupOpen`, `ClosePopupToLevel`, `ListOpenPopups`, `GetMousePosOnOpeningCurrentPopup`, `GetTopMostPopupModal`) and `OkayUI_Windows.cpp` (682 lines: `Begin` / `End`, the ImGui-style `Begin`, window queries and setters, `BeginChild` / `EndChild`, scrolling), both added to `okayui/CMakeLists.txt`. Rules: part 2 at the end of `docs/engine/moves/O1_okayui_split.rules`. Nothing lost `inline` in this part. Edits that are not moves: the file-static window state (`g_pinNext`, `g_curWin`, `g_we`, `g_wflagsNext`, `g_focusWin`, `g_nextWinPos`, `g_nextWinSize`, `g_bareNext`, `g_noTitleNext`, `g_resizeNext`, `g_dockAware`, `g_dockRouted`) lost `static` and is defined as `ui_core::<name>`; `panelColored` lost `static` and is defined as `ui_core::panelColored`; `g_popupPadX`, `g_popupPadY`, `g_popupW`, `g_innerX` and `framedComboBox` left the anonymous namespace for `ui_core`; `OkayUI_Windows.cpp` declares `ChannelsMergeAll()` (defined in `OkayUI.cpp`). | `check_pure_move.py`: PURE MOVE for all three commits (step 1: 132 lines moved, 62 added by rule, 13 dropped by rule). Global symbols (`nm -g`) of the old four objects and of the six new objects joined with `ld -r`: 718 before, 736 after; the 18 new ones are all `OkayUI::ui_core::` (the 12 window variables, `g_popupPadX/Y`, `g_popupW`, `g_innerX`, `g_we`, `framedComboBox`, `panelColored`), nothing outside `ui_core` changed, no undefined `ui_core` symbol. Build in `/tmp/okayui2_build` (Release, `ninja -j2`, exit 0): `okay_ui` and all 67 test targets of `ctest -N -R "okayui|ui_toolkit|okay_ui"`; `ctest`: 67 of 67 passed, none "Not Run", including `okayui_golden`, `okayui_render`, `okayui_tables`, `okayui_windows`, `okayui_capture`. Not verified: the Windows (MinGW) cross-compile, and any run of the editor or launcher with the split library. |
| M1, part 1 | local worktree | `Mesh.hpp` shape generators and props (`Quad` to `FromName`, `Extrude`, `Lathe`, `Capsule`, `Screw`, `SweepPath`; 58 functions, 1,143 lines) to `engine/src/Render/MeshShapes.cpp`; bodies unchanged, qualified `Mesh::`, `static` and default arguments stay on the declaration | `check_pure_move.py` exit 0 with `docs/engine/moves/M1_shapes.rules`; engine and 28 mesh/modeling/OBJ/navmesh test targets built (ninja exit 0); 28 of 28 matching tests pass |
| M1, part 2 | local worktree | `Mesh.hpp` UV projection (`ProjectUVPlanar/Box/Cylinder/Sphere`, `SetCornerUVs`, `UnwrapAroundU`; 95 lines) to `engine/src/Render/MeshUV.cpp` | `check_pure_move.py` exit 0 with `docs/engine/moves/M1_uv.rules`; engine and 28 mesh/modeling/OBJ/navmesh test targets built (ninja exit 0); 28 of 28 matching tests pass |
| M1, part 3 | local worktree | `Mesh.hpp` `LoadOBJ` and `SaveOBJ` (324 lines) to `engine/src/Render/MeshObj.cpp`; one vertex-key line carries `dup-allow` (moved unchanged) | `check_pure_move.py` exit 0 with `docs/engine/moves/M1_obj.rules`; engine and 28 mesh/modeling/OBJ/navmesh test targets built (ninja exit 0); 28 of 28 matching tests pass |
| M1, part 4 | local worktree | `Mesh.hpp` modifiers and editing operations longer than 11 lines after line 2310 (`Mirror`, `Solidify`, `WeldVertices`, `SculptBrush`, `ConvexHull`, `FillHoles`, `BevelVertices` and 25 more; 1,044 lines) to `engine/src/Render/MeshModifiers.cpp`; `SplitEdges` and `SplitByPlane` carry `dup-allow` (not string helpers); `Mesh.hpp` dup baseline entries removed | `check_pure_move.py` exit 0 with `docs/engine/moves/M1_modifiers.rules`; engine and 28 mesh/modeling/OBJ/navmesh test targets built (ninja exit 0); 28 of 28 matching tests pass |
| S5 (EngineApi, plan row 15) | this PR | `engine/src/Script/EngineApi.cpp` (2,455 lines on main): **Refactor (behaviour-neutral):** `Build` (2,233 lines) became `Ctx::Init` and 14 `Ctx::Bind<Topic>` members (Core, Gameplay, UiControls, UiApp, UiLibrary, Input, Runtime, Rendering, Navigation, Story, Splines, Animation, Trail, Mathf) called in the original order. The context is a struct in the private header `EngineApiInternal.hpp` (`okay::script::engine_api`) with the table, the common types and the `prop`/`ro`/`method` makers; the value helpers (`F`, `I`, `S`, `V`, `UiComp`, `Body`...) became `inline` there. `goIdx` and `trIdx` are copied from the context into locals where a lambda captures them. **Moves (one commit each):** `EngineApi_Core.cpp` (227), `_Ui` (925), `_Input` (127), `_Runtime` (212), `_World` (240), `_Story` (195), `_Curves` (143), `_Animation` (181), `_Math` (45); `EngineApi.cpp` keeps `Find`, the shared-table glue and `Build` (170 lines). Rules: `docs/engine/moves/S3_engineapi_topics.rules`. | `check_pure_move.py`: PURE MOVE, exit 0, for all 9 moves. Built: `okay_engine`, ninja exit 0. Bound names: a temporary test (not committed) dumps every type, property, method, parameter, return type, doc string and flag in table order: 484 lines on origin/main and 484 after, identical (`cmp`). | Windows (MinGW) build of the new files unverified. `EngineApi_Ui.cpp` (925) can be split by widget family. | nobody | low |

M1 result: `Mesh.hpp` went from 5,174 to 2,666 lines; new files 1,209 + 109 + 334 + 1,084 lines. Left inline on purpose: the first 725 lines (accessors, hide/slot/seam helpers, `InheritAttributes`, `ComputeSmoothNormals`, `ComputeAutoSmoothNormals`) and the short helpers after line 1818 (`Bounds`, `Center`, `Size`, `Normals`, `FaceNormal`, `Transformed`, `Append`...). A further part (the first 725 lines, about 450 lines of big bodies) is not done. Any target that includes `Mesh.hpp` and calls a moved function must now link `okay_engine`.

**The first five, in plain words:** finish the editor's next two moves (they are planned, safe and nobody is in the
way), split the network manager (the biggest engine file nobody is touching, and the class does not change), start
the launcher with its cleanest section, and break the bottom of the dependency cycle with three tiny header moves.

## 4. How every batch is done

This is Part B's procedure, for every file in the repository. "Move" means a pure move; renames and refactors are
separate commits or PRs.

1. **Claim it** on issue #422 ("moving `<file>` lines a-b to `<new file>`; please do not edit it until merged"),
   from a fresh `origin/main`, on a branch `claude2/split-<batch>` (or `claude2/editor-split-N` for Part B).
   Always claim before moving a file that another Claude edits (any file with an open PR in section 1.10).
2. **Re-measure** the section on today's `main` and check the open PRs again (the commands in 1.1 and Part B.1).
3. **New files.** `<Feature>.hpp` declares what the rest of the old file calls (and holds, moved unchanged, the
   structs and inline helpers those callers need). `<Feature>.cpp` includes what the old file included that it
   needs, its own header and the shared declarations header, then the same namespace lines and the moved code.
   - For the editor: `#include "EditorCommon.hpp"`, `MainShared.hpp`, `namespace oked { ... }` (Part B.3).
   - For the launcher: `LauncherShared.hpp`, made in batch L1 the same way.
   - For an engine class split over several `.cpp` files: no new public header; private helpers go into
     `engine/src/<Folder>/<Class>Internal.hpp` (the pattern of `engine/src/Script/ScriptInternal.hpp` and
     `engine/src/Platform/Internal.hpp`).
   Add each `.cpp` to its target's source list in the matching `CMakeLists.txt` with a comment, next to the old file.
   If PR #1158 has merged, add the new file to the right module manifest's `sources=` (or check that a pattern
   already covers it), so `scripts/check_modules.py` and the module sizes stay right.
4. **Shared state: reuse, never add a second.** What a moved file still needs from the old file is declared in the
   shared declarations header (`MainShared.hpp`, `LauncherShared.hpp`, `<Class>Internal.hpp`); each declaration
   moves to its owner's own header when the owner moves out. Do not add a new context struct; do not copy a helper.
5. **The code moves verbatim.** Only what is needed to compile changes: includes; `static` becomes plain (external
   linkage) for a function or global both files use, with an `extern` or prototype in a header; a default argument
   moves from the definition to the declaration; namespace lines. No renames, no reformatting, no "while I am here"
   fixes. A duplicate found during the move is written down (Part B.4 for the editor, a new line in the same list
   for other files), not fixed.
6. **Things that look harmless and are not:**
   - A `static` helper that exists in the old file and also in a header the new file includes: the call can silently
     bind to the other one. Before the move, list every name the section uses that the old file defines (the
     compiler's "not declared" errors give most; grep the rest) and make sure each resolves to the same function.
   - A helper in an anonymous namespace used by both files: putting it in a header keeps it file-local in each file
     (each `.cpp` gets its own copy). That is right for `inline` helpers and constants; for a helper with a `static`
     variable inside, or a global, it would make two copies of the state. Those must become one external definition
     with a declaration.
   - `#define`s made in the old file before the section (for example `OKAY_BREADCRUMB`, the Modeling window's
     `Button` / `Checkbox` highlight macros in `editor/main.cpp`) do not exist in the new file. Grep the section for
     every macro name the old file defines above it.
   - `inline` functions used on both sides must go into the header whole (as `HumanBytes` did in editor move 1).
   - Code under `#if defined(_WIN32)` or `OKAY_XBOX_UWP` is not compiled on Linux: say so as unverified, and let the
     Windows cross-compile job check the `_WIN32` side.
   - Order of static initialisation: a global whose initial value calls another file's global must not move to a
     different file than the one it depends on unless both are `constexpr` or function-local statics.
7. **A header move** (batches B, P1, W1, G): `git mv` the header to its new folder, change its own includes, and leave
   at the old path a forwarding header: `#pragma once`, one comment line ("moved to okay/Math/Color.hpp; this
   forwarder stays until <release + 1>"), one `#include`. Callers are not changed in the same PR. Switching callers
   to the new path is a later, mechanical PR; deleting the forwarder is announced and comes one release later.
8. **A whole-file move inside a program** (batch V1: `editor/VisualScript*.hpp` into `editor/VisualScript/`): `git mv`
   with no content change, plus the include lines that name it. `git diff -M100%` must show every moved file as a
   100% rename.
9. **A refactor batch** (section 3, "refactor"): split one long function into named functions in the **same** file,
   with no other change, as its own PR with tests that pin the behaviour first (for the serializer: write every test
   scene before and after and compare the text; for the script VM: list every command name before and after). The
   move comes in the next PR.
10. **Write the rules file** `docs/editor/main_split/NN_<feature>.rules` (editor) or
    `docs/engine/moves/<batch>.rules` (everything else), listing every allowed edit (section 5, check 4).
11. **Run the checks** (section 5), push, and ask the main session to open the PR. Merge it fast; then merge `main`
    into every open branch that touches the old file (CLAUDE.md rule 8). Each move is small and merged fast, so a
    branch that edited moved code gets one clear conflict (the code is now in the new file) instead of a slow drift.

## 5. The checks for every batch

1. **It builds.** Only the targets that compile the moved files (`ninja -j2 <target>`; `df -h /` first, CLAUDE.md
   rule 20). For the editor: configure with `-DOKAY_BUILD_EDITOR=ON`, compile `main.cpp` alone (`ninja -j1`,
   `-O0 -g0` if memory is short), then the new file, then link `OkayEngine`.
2. **The tests that cover the area pass.** `ctest -R <area>`; with PR #1158 merged, `ctest -L module:<name>` runs
   one module's tests. The editor also runs `./build/bin/OkayEngine --selftest` (the CI job). If no test links the
   moved files, run the ones that exercise the feature (editor move 1: `tests/export/verify.sh`, `verify_layout.sh`)
   and say so.
3. **Same output.** Where a batch can change what a program writes (scenes, builds, script command lists, pixels),
   compare the output before and after on the same input, as editor move 1 did for Build Game.
4. **`git diff` is a pure move**, checked by `scripts/check_pure_move.py` (standard library only; its test is
   `tests/scripts/test_check_pure_move.py`):
   ```
   python3 scripts/check_pure_move.py --base $(git merge-base HEAD origin/main) --source <old file> \
       --dest <new>.hpp --dest <new>.cpp --dest <shared declarations header> --rules <rules file>
   ```
   `--base` is the commit the branch started from (the merge base), not today's `main`: once `main` moves on, its own
   edits would show up as differences. It matches the lines removed from the old file against the lines added to the
   destination files in order (so a reordered line fails), and fails on anything left over unless it is scaffolding
   (blank lines, `//` comments, `#pragma once`, `#include`, namespace lines) or listed in the rules file
   (`change: old ==> new`, `add: regex`, `drop: regex`). A change rule that matches nothing also fails, so the rules
   stay exact. A header move or whole-file move is checked with `git diff -M100% --stat` (every file a 100% rename)
   plus the forwarding header. A refactor batch is not a pure move; its PR says so and lists its pinning tests.
5. **No conflict markers**: `git grep --cached -n -E '^(<<<<<<<|=======|>>>>>>>)( |$)' -- .`
6. **`bash scripts/precheck.sh`** before pushing.
7. **Docs** in the same change: this page (mark the batch done, fix the line numbers of later sections if you
   re-measured), the feature map rows that named the old file, `docs/engine/code_index.md` if a public header moved
   (`python3 scripts/gen_code_index.py`), then `python3 scripts/gen_feature_map.py` and `--check`.

## 6. Naming

### 6.1 The rule

For new code, and for old code when a rename batch reaches it:

| Kind | Style | Example |
|---|---|---|
| Types (class, struct, enum, alias) | PascalCase | `NetworkManager`, `TrailRenderer` |
| Functions and methods | PascalCase | `ServerTick`, `FormatNumber` |
| Local variables and parameters | camelCase | `targetSpeed`, `jointCount` |
| Private and protected data members | `m_camelCase` | `m_occlusionBias` |
| Public data fields of plain structs and components | camelCase, no prefix | `minVertexDistance`, `yaw` |
| Constants (`constexpr`, `static const`) | `kPascalCase` | `kParallelBoundsMin` |
| Enumerators of an `enum class` | PascalCase | `LineAlignment::Flat` |
| File globals (should become rare) | `g_camelCase` | `g_project` |
| Macros | `OKAY_UPPER_CASE` | `OKAY_BREADCRUMB` |
| Namespaces | lower case | `okay`, `okay::editor`, `oked` |
| Files | PascalCase, `.hpp` and `.cpp`, named after the main type or the job | `NetworkManagerServer.cpp` |

And these, from the owner's request:

- **A name says what the thing is or does**, in whole words: `FormatNumber(fmt, v)` not `F`, `ReadJsonBool` not `B`,
  `WindowState()` not `U()`, `g_officialInstall` not `g_off`. One- or two-letter names only for loop counters,
  coordinates (`x`, `y`, `z`), maths with a known formula, and well-known short words (`Id`, `At`, `Is`, `Of`).
- **Name the number.** A number whose meaning is not obvious from its line gets a `kName` constant next to where it is
  used, with a comment that says where the value comes from (measured, from a spec, chosen by hand).
- **Split a function** that does several jobs or passes about 300 lines, one job per function, in the same file
  first (a refactor batch).
- **Explain the unusual.** A choice that looks wrong but is on purpose (a back-compatible trailing field, a recursive
  mutex, a short frozen key) gets one comment line saying why.
- **Delete only what is proven unused**: no caller in the repository (grep and the code index), not reachable from a
  script, a scene record, a menu or a command-line flag, and the build and tests pass without it. Say in the PR how
  it was proven.

### 6.2 Where today's code keeps its own convention

- **`.hpp`, not `.h`.** The owner wrote ".h", and also "preserve existing conventions where changing them causes
  incompatibility". Every one of the about 1,000 headers in the engine and programs is `.hpp` (the only `.h` files are
  embedded font data and vendored ImGui). Renaming them would change about 10,700 include lines, every open branch
  would conflict, and every forwarding header would double the header count. Nothing would work better. The
  recommendation is to keep `.hpp`; this is listed for the owner in section 8.
- **Public fields of components stay as they are** (camelCase, no prefix). They are part of the public API: scripts,
  the property registry, the Inspector and the visual script nodes read them by name.
- **The frozen names of section 1.9 do not change** in a naming batch, even when vague (`"c"`, `"w"` scene keys,
  snake_case script commands). A clearer name can be added beside an old one (an alias the reader accepts) only
  with the owner's approval.
- **`tools/okayms` and `tools/okaycc`** are a C compiler written in the style of C code (snake_case, lower-case
  files). They are separate programs with their own build; renaming them to the engine's style would touch most of
  their 24,000 lines for no gain in the engine. They keep their style unless the owner says otherwise.
- **`g_` globals** are not in the owner's list. They are kept (the prefix warns that a name is shared state); the
  real fix is fewer globals, which the splits bring.

### 6.3 How renames are done

1. **Internal names** (anything not in section 1.9: private members, locals, `static` functions, file globals,
   internal helpers): a rename batch is its own commit or PR, after the file's move has merged, one file or one
   folder at a time, never mixed with a move. The build and the area's tests must pass; a rename changes no
   behaviour, so `git diff --word-diff` should show only the old and new names.
2. **Public C++ names** (classes and functions in `engine/include/` that the programs and tests use): the same, but
   the PR lists every changed name, and a type that might be used by game code outside the repository keeps a
   `using OldName = NewName;` (or an inline forwarding function) for one release.
3. **Frozen names** (section 1.9): never in a naming batch. To propose one, write it in a list on issue #422 for the
   owner: the old name, the new name, who reads it (files, scripts, tools), and how old projects keep working (the
   loader accepts both, a project upgrade step rewrites old files, `docs/editor/project_upgrade.md`). Nothing changes
   until the owner says yes.
4. **Trailing-underscore members** (about 1,800) become `m_` members file by file, as internal renames.

## 7. Checks that can run in CI later (described, not built)

Each one starts as a warning (as `scripts/check_modules.py` does in PR #1158), with a baseline file of today's
findings that may only shrink, and becomes an error once the baseline is empty.

1. **Include direction.** Extend `scripts/check_modules.py` (PR #1158) instead of writing a new script. Today it
   checks includes between modules; add the layer rules of [architecture.md](architecture.md): `engine/` must not
   include `editor/`, `launcher/`, `player/` or `shared/`; `editor/` and `launcher/` must not include each other;
   `shared/` must not include a program folder; `okayui/src`, `render`, `soft` may include only `Math`, `Core`
   (base), `Platform` and `Graphics`; a lower engine layer must not include a higher one. Baseline today: the 7
   `engine -> shared` lines, the 1 `editor -> launcher` line and the header pairs of section 1.6.
2. **No new include cycles.** The same script builds the header graph and fails when a change adds a new cycle
   between folders or files (today: the folder cycles in the baseline, and `Buoyancy.hpp` <-> `Water.hpp`).
3. **Platform code only where it belongs.** A `branch_lint.py` check: a changed line that adds `_WIN32`,
   `windows.h`, `winsock2.h`, `SDL`, or `X11/` outside `engine/src/Platform/`, the renderer backends, the
   programs' `main.cpp` files and a short allow list (baseline: the files of section 1.7) is reported.
4. **File and function size.** A `branch_lint.py` warning when a changed hand-written source file grows past 2,000
   lines, or grows at all while already above 3,000; and when a change adds a function longer than 300 lines
   (the brace counter of section 1.1). Tests, tools, generated files and vendored code are left out.
5. **Pure move.** When a branch name starts with `claude2/split-` or `claude2/editor-split-`, CI runs
   `check_pure_move.py` with the rules file named in the PR body.
6. **Frozen names.** A test that lists every frozen name of the first five groups of 1.9 (script commands, typed
   API, node tokens, record tags, record keys) and fails when one disappears. Adding names is fine; removing or
   renaming needs the owner's approval and an update of the list.

## 8. What needs the owner's decision

1. **`.hpp` or `.h`.** Recommended: keep `.hpp` (section 6.2). The safe default until told otherwise: `.hpp`.
2. **`m_` on public fields.** The rule above puts `m_` only on private members; public fields of components and
   plain structs keep their names because scripts and saved files use them. Safe default: as written.
3. **Frozen but vague names** (the one- and two-letter scene keys, snake_case script commands): leave them, or allow
   aliases with a project upgrade step? Safe default: leave them.
4. **Forwarding headers**: keep each one for one release after a header moves, then delete (the own-code roadmap's
   pattern). Safe default: one release.
5. **Tools' style** (`okayms`, `okaycc` in snake_case). Safe default: leave them.
6. **New folder names**: `okay/App` (game loop), `okay/Save`, `okay/Online`, `okay/World`, `editor/VisualScript/`.
   Safe default: these names, unless the owner prefers others.

# Part B: `editor/main.cpp`

The owner approved splitting `editor/main.cpp` into smaller files by feature, slowly, so code is easy to find and
duplicates stop. Nothing may break. This part was `docs/editor/main_split_plan.md` (that path now points here); its
general steps and checks are Part A, sections 4 and 5. What follows is the editor's own map, order and details.
Feature map: 15.10.

Line numbers in B.1 and B.2 are for `origin/main` at `61332f077` (59,952 lines), before move 1. They go stale with
every merge: re-measure a section (grep for its first and last line) right before you move it. On `bc5629ee8`
(2026-11-22, 58,835 lines) the next moves sit at: self-updater 1850-2121; Scripting Reference (`DrawScriptDocs`)
and script completion 12758-14958; `DrawScriptEditor` from 14959; `DrawInspector` from 35565; `main` from 56539.

## B.1 What is in `main.cpp`

How the numbers were made: a small brace counter (comments and strings skipped) found every top-level definition and
its size; a section is a run of definitions that belong to one feature, cut at the file's own `// ----` headers.
"Changed" is the share of the section's lines that `git blame` dates inside the 9 days of history this clone has
(2026-11-13 to 2026-11-22): a rough measure of how often it is edited. "Needs" counts the names defined elsewhere in
`main.cpp` that the section uses (globals, helpers, structs: each becomes a declaration in a shared header when the
section moves). "Used by" counts the names it defines that the rest of `main.cpp` uses (each becomes a declaration
in the section's new header). Both counts are rough (a few false hits on common words); the compiler gives the exact
list during a move.

| # | Lines | Size | Section | Changed | Needs | Used by | Who calls it | Open PR inside |
|---|---|---|---|---|---|---|---|---|
| 1 | 1-1014 | 1,014 | Includes, script lint, headless self-test (`RunSelfTest`, `RunStaleGraphTest`) | 10% | 0 | 13 | `main`, Actions, Inspector | #1120 (one include) |
| 2 | 1015-1773 | 759 | Crash diagnostics, direct viewport textures, thumbnails, colour and IK helpers | 9% | 2 | 59 | almost everything | - |
| 3 | 1774-1848 | 75 | Built-in 2D sprite shapes | 0% | 0 | 3 | menus, Sprite editor | - |
| 4 | 1849-2120 | 272 | Self-updater (`namespace updater`) | 22% | 2 | 9 | Build Game, update popup, menus, `main` | - |
| 5 | 2121-2335 | 215 | Window flags, UI backend switch, small shared state | 14% | 2 | 117 | everything | #1128 (`UseOkayUIBackend`) |
| 6 | 2336-2846 | 511 | Collider wireframes, physics material fields, settings load and save | 47% | 48 | 27 | Scene view, Inspector, Hierarchy | - |
| 7 | 2847-2985 | 139 | Custom-action library | 1% | 3 | 28 | Actions, Inspector | - |
| 8 | 2986-3102 | 117 | Import Model dialog, script tabs, build-dialog globals | 10% | 0 | 60 | many | - |
| 9 | 3103-3525 | 423 | Build profiles, project settings globals (`g_project`) | 17% | 4 | 153 | many | - |
| 10 | 3526-4401 | 876 | Mesh-edit undo, polygon view, modeling commands, selection tools | 64% | 41 | 67 | Modeling, Scene view, Inspector | - |
| 11 | 4402-4468 | 67 | External-IDE helpers (`namespace extide`) | 6% | 1 | 9 | Project panel, Script Editor, plugins | - |
| 12 | 4469-5615 | 1,147 | **Build Game (`namespace builder`)**: the game, Web / Android / Xbox data, dedicated server | 5% | 8 | 11 | build dialogs, `RunBuildGameCli`, `main`, UI menu runner | - |
| 13 | 5616-5676 | 61 | Editor theme | 13% | 2 | 1 | menus, `main` | - |
| 14 | 5677-6815 | 1,139 | Panel tabs, layouts, panel switcher | 7% | 33 | 24 | menus, Viewport, Inspector | - |
| 15 | 6816-7260 | 445 | UI menu runner, playback speed controls | 70% | 11 | 6 | menus, Viewport, `main` | - |
| 16 | 7261-8571 | 1,311 | Main menu bar, creators, GameObject menus, toolbar, status bar, dock space | 64% | 131 | 18 | `main`, Hierarchy, Scene view | - |
| 17 | 8572-8786 | 215 | Audio Mixer, Crash Log, History panels | 6% | 6 | 4 | `main` | - |
| 18 | 8787-8994 | 208 | Find in Files | 0% | 2 | 17 | Script Editor, Project panel | - |
| 19 | 8995-9166 | 172 | Project panel right-click menus, file pickers | 17% | 7 | 35 | Project panel, Animation, Hierarchy | - |
| 20 | 9167-9806 | 640 | Character model swap, auto-rig, folder tree | 9% | 19 | 34 | Inspector, Hierarchy, Project panel | - |
| 21 | 9807-10100 | 294 | Asset Manager | 0% | 8 | 1 | `main` | - |
| 22 | 10101-10694 | 594 | Editor plugins, bone map, plugin manager | 0% | 9 | 5 | menus, Inspector, `main` | - |
| 23 | 10695-12636 | 1,942 | Asset colour labels, Project panel (`DrawProject`, 1,718 lines) | 31% | 94 | 3 | `main`, Inspector | - |
| 24 | 12637-13718 | 1,082 | Services, Save Manager, Material editor, Stats, Look and Scene settings | 6% | 28 | 8 | `main`, Inspector | - |
| 25 | 13719-14540 | 822 | Quit prompt, shortcuts window, Scripting Reference | 4% | 10 | 6 | `main`, Script Editor | - |
| 26 | 14541-16094 | 1,554 | Script completion, signatures, docs, C# members, code-widget glue | 1% | 12 | 31 | Script Editor only | - |
| 27 | 16095-18703 | 2,609 | Script Editor (`DrawScriptEditor`, one function) | 3% | 59 | 1 | `main` | #1128 |
| 28 | 18704-20118 | 1,415 | Keyboard shortcuts, file / build / export dialogs (`DrawFileDialogs`) | 16% | 95 | 2 | `main` | - |
| 29 | 20119-20986 | 868 | Projects: errors, save prompt, recents, New Project, gate, About, project settings | 12% | 41 | 9 | `main` | - |
| 30 | 20987-22131 | 1,145 | Hierarchy | 31% | 25 | 4 | `main`, Inspector, Scene view | - |
| 31 | 22132-22879 | 748 | UI anchor helpers, Inspector add queue, Actions op table | 0% | 12 | 16 | Inspector, Actions, UV editor | - |
| 32 | 22880-23396 | 517 | OkayScript to visual script, script recipes | 0% | 6 | 5 | Actions, Inspector, `main` | - |
| 33 | 23397-26703 | 3,307 | Animation tab: live preview, shared timeline widgets, character and model clips, object timeline | 0% | 10 | 16 | `main`, Inspector, Scene view | - |
| 34 | 26704-28908 | 2,205 | Actions rules and items, flow graph, particle editors, component header, script fields | 7% | 15 | 42 | Inspector, Scene view | - |
| 35 | 28909-31179 | 2,271 | UV editor, Modeling window (`DrawModeling`, 1,865 lines) | 36% | 96 | 3 | `main` | - |
| 36 | 31180-36700 | 5,521 | Inspector parts drawn by OkayUI (stages 1 to 9) | 100% | 70 | 114 | Inspector | - (being written now) |
| 37 | 36701-49497 | 12,797 | Inspector (`DrawInspector`, **one function**) | 12% | 259 | 1 | `main` | #1059 |
| 38 | 49498-50947 | 1,450 | UI overlay and UI widget editing in the Scene view | 2% | 35 | 16 | Scene view, Inspector | - |
| 39 | 50948-51588 | 641 | Scene view "add here" menu, 2D scene view | 41% | 42 | 4 | Viewport | - |
| 40 | 51589-54843 | 3,255 | 3D scene view (`DrawScene3D`, 3,190 lines) | 23% | 157 | 4 | Viewport | - |
| 41 | 54844-55716 | 873 | Viewport, shared resolution, mobile emulator | 0% | 61 | 11 | `main` | - |
| 42 | 55717-56687 | 971 | Sprite editor, perf overlay, Profiler, Game view | 9% | 50 | 5 | `main` | #1120 (`DrawProfiler`) |
| 43 | 56688-57172 | 485 | Saving, prefab mode, document recovery, `SaveProject` | 3% | 56 | 6 | `main`, launcher gate | - |
| 44 | 57173-57667 | 495 | Launcher gate, single instance, multi-window, hand-off, `RunBuildGameCli` | 12% | 26 | 18 | `main` | - |
| 45 | 57668-59952 | 2,285 | `main()` | 19% | 258 | 0 | - | #1128, #1127, #1120, #1108 |

Open pull requests on 2026-11-22 (`gh api "repos/kingimann/OkaySpaceGameEngine/pulls?state=open"`, then
`git diff --stat origin/main...origin/<branch> -- editor/main.cpp` for each): 13 are open; 5 touch `main.cpp`:
#1128 `claude2/own-codeedit-2` (27+/8-: `UseOkayUIBackend`, `DrawScriptEditor`, `main`), #1127
`claude2/own-platform-launcher` (1 line in `main`), #1120 `claude2/texture-system` (41+: an include, `DrawProfiler`,
`main`), #1108 `claude2/found-console` (2+ in `main`), #1059 `claude/unfinished-ledge-1` (9+/2-: `DrawInspector`).


## B.2 The order

Rules for the order: large (about 1,000 to 3,000 lines), self-contained (few "Needs"), rarely edited, and not inside
an open PR first. What collides with an open PR, what is edited every day and what needs a real refactor go last.

| Move | Section(s) | Size | Why here |
|---|---|---|---|
| 1 (done on `claude2/editor-split-1`) | 12 Build Game → `editor/BuildGame.hpp/.cpp` | 1,145 | Already its own namespace with external linkage, needs only 7 outside names, 5% changed, no open PR, and a CI job (`export-check`) runs it for real. |
| 2 (done on `claude2/editor-split-2`) | 4 Self-updater → `editor/Updater.hpp/.cpp` | 272 | Small, and its four declarations that move 1 put in `MainShared.hpp` then move to its own header (the "list shrinks" rule of `MainShared.hpp`). |
| 3 (done on `claude2/editor-split-2`) | 26 Script completion and docs (+ 25 Scripting Reference) → `editor/ScriptAssist.*` | 2,376 | 1% changed; used only by the Script Editor. #1128 (Script Editor) has merged, so this is unblocked: batch 2 in Part A, section 3. |
| 4 | 33 Animation tab, in two moves: object timeline + model clips (25228-26703, 1,476) then live preview + widgets + character clips (23397-25227, 1,831) → `editor/AnimationTab*.cpp` | 3,307 | 0% changed, 10 outside names. The shared timeline widgets (`AnimGlyphButton` ...) are already declared in `MainShared.hpp`; they move to the new header. |
| 5 (done on `claude2/editor-split-2`) | 18 Find in Files, 21 Asset Manager, 22 Editor plugins → `editor/FindInFiles.*`, `editor/AssetManagerWindow.*`, `editor/PluginsWindow.*` | 1,096 | 0% changed, few names. |
| 6 | 31 + 32: UI anchor helpers, Actions op table, OkayScript to visual script, recipes → `editor/ActionsTable.*` | 1,265 | 0% changed. Must go before move 9 (Actions editors use it). |
| 7 | 24 Services, Save Manager, Material editor, Stats, Scene settings → `editor/ProjectWindows.*` | 1,082 | 6% changed, 28 names. |
| 8 | 38 UI overlay and UI widget editing → `editor/SceneUIEdit.*` | 1,450 | 2% changed. |
| 9 | 34 Actions editors → `editor/ActionsEditor.*` | 2,205 | 7% changed, many callers in the Inspector (42 names). |
| 10 | 14 Panel tabs and layouts, 17 small panels → `editor/PanelLayout.*` | 1,354 | 7% changed. |
| 11 | 41 Viewport and mobile emulator, 29 Projects and New Project | 1,741 | Low churn, but many names (61 and 41). |
| 12 | 1 self-test (441-897) → `editor/EditorSelfTest.cpp` | ~460 | After #1120 merges (it adds an include there). |
| 13 | 20 Character model swap and auto-rig, 2 helpers | 1,399 | Many callers; do after the panels that call them have moved, so their header is the only door. |
| 14 | 27 Script Editor, 28 file and build dialogs | 4,024 | After #1128. `DrawFileDialogs` needs 95 names: move the build dialog part with Build Game's header. |
| 15 | 23 Project panel, 30 Hierarchy, 35 Modeling and UV, 10 mesh-edit tools, 16 menus | ~7,500 | Edited often (31% to 64%): only in a quiet week, each alone, claimed on #422 first. |
| 16 | 39 + 40 Scene views, 42 Game view and Profiler | ~4,900 | `DrawScene3D` is one 3,190-line function; move whole first, split later. After #1120 (Profiler). |
| 17 | 36 Inspector parts drawn by OkayUI | 5,521 | Written right now (100% of lines changed this week). Move when that work stops, by the person doing it. |
| 18 | 37 `DrawInspector` | 12,797 | One function: a pure move only gives a 12,800-line file. First turn each component's block into its own function (a refactor, its own PR with its own tests), then move groups. After #1059. |
| 19 | 45 `main()` and the window loop | 2,285 | Every open PR touches it. Last. |


## B.3 Editor details of the move procedure

Steps 3 and 4 of Part A, section 4, as they apply to `editor/main.cpp`:

3. **New files.** `editor/<Feature>.hpp` declares what the rest of `main.cpp` calls (and holds, moved unchanged, the
   structs and inline helpers those callers need). `editor/<Feature>.cpp` starts with `#include "EditorCommon.hpp"`
   (what every editor file includes, with the same `using namespace okay; using namespace okay::editor;` as
   `main.cpp`), its own header, `MainShared.hpp`, and the headers it needs; then `namespace oked {`, the moved code,
   `} // namespace oked`. Add the `.cpp` to the `okay-editor` source list in `editor/CMakeLists.txt` with a comment
   (the `okay-editor-okayui` target copies that list, so it gets the file too).
4. **Shared state: reuse, never add a second.** There is no editor context struct: the shared state is
   `okay::editor::EditorState` (`editor/EditorState.hpp`, passed as `ed`) plus file globals in namespace `oked`. What a
   moved file still needs from `main.cpp` is declared in `editor/MainShared.hpp` (that is its job: "functions and
   state still defined in main.cpp that the split-out panels use"). Each declaration moves to the owner's own header
   when that owner moves out (move 2 does this for the updater). Do not add an `EditorContext` struct; do not copy a
   helper into the new file.

## B.4 Duplicates found so far (fix after the moves, one at a time, with tests)

- `builder::HumanBytes` (now `editor/BuildGame.hpp`) and `okay::buildpipe::HumanBytes` both format a byte count;
  `BuildGame.cpp` uses both.
- `Lower(std::string)` is copied in `AnimatorWindow.cpp`, `BehaviorTreeWindow.cpp`, `ContentEditors.cpp`,
  `DialogueWindow.cpp`, `InspectorModel.cpp`, `LocalizationWindow.cpp`, `ProjectBrowser.cpp`, `ProjectUpgrade.cpp`,
  `WeaponEditor.cpp` and `main.cpp`.
- `extide::WriteFile` (`main.cpp`), `WriteFile` in `ContentEditors.cpp` and `builder::WriteFileExact` write a text
  file three ways.


## B.5 Done

| Move | Branch | What | Result |
|---|---|---|---|
| 1 | `claude2/editor-split-1` | `namespace builder` (main.cpp 4469-5613, 1,145 lines) → `editor/BuildGame.hpp` (types, `HumanBytes`, declarations) and `editor/BuildGame.cpp`; 7 declarations added to `MainShared.hpp` (updater, `g_project`, the Xbox export result). Rules: `docs/editor/main_split/01_build_game.rules`. | `check_pure_move.py`: PURE MOVE (1,146 lines removed: 1,144 back unchanged and in order, 2 default-argument edits, the rest blank). Linux, 2026-11-22: editor built (Debug `-O0 -g0`; `main.cpp` alone with `ninja -j1`, about 5.7 GB peak), `--selftest` OK, `tests/export/verify.sh` and `verify_layout.sh` pass. Same-input comparison against an editor linked from `origin/main`'s `main.cpp`: `--build-game` in the folder, compact and zip layouts and the dedicated-server export (`OKAY_UI_BUILDSERVER`, under Xvfb) give identical files once the random object GUIDs, the build date and the output path are taken out. Not verified: the Windows build (left to the CI cross-compile), the `OKAY_XBOX_UWP` branch (no build compiles it), Web / Android export. |
| 2 | `claude2/editor-split-2` | `namespace updater` (272 lines) → `editor/Updater.hpp` (`UpdateInfo`, declarations) and `editor/Updater.cpp`; the four updater declarations left `MainShared.hpp` for `Updater.hpp`; `SysNoWindow` lost `inline` (declared in `MainShared.hpp`). Rules: `docs/editor/main_split/02_updater.rules`. | `check_pure_move.py`: PURE MOVE. Build and test results: the PR text. |
| 3 | `claude2/editor-split-2` | Scripting Reference (`DrawScriptDocs`) and script assist (completion, signatures, docs, C# members, help card, snippet painter, palette, language definition; 1,981 lines) → `editor/ScriptAssist.hpp/.cpp`. The Script Editor's tab glue (`CodeEditorFor`, `ScriptTabDirty`, formatting, close / reopen) stays with `DrawScriptEditor`. Rules: `03_script_assist.rules`. | PURE MOVE. |
| 5 | `claude2/editor-split-2` | Find in Files (67 lines) → `editor/FindInFiles.*`; Asset Manager (294) → `editor/AssetManagerWindow.*`; editor plugins, Plugins menu and Plugin Manager (513) → `editor/PluginsWindow.*`. "Map Humanoid Bones" (between them) stays in `main.cpp`. Rules: `05_asset_plugins.rules`. | PURE MOVE. Move 4 (Animation tab) waits for open PR #1140. |

# Part C: `launcher/main.cpp`

The launcher is one 27,261-line file (plus 10 small ones: `Versions*`, `VoiceChat.hpp`, `NetGuard.hpp`, `UpdaterCore.hpp`) (`main` alone is 9,133 lines: every page of the launcher). Nothing open touches
it on 2026-11-23. Its own `// ----` headers give natural sections; the table is measured the same way as Part B.1
("Needs": names defined elsewhere in the file that the section uses, with how many are `g_` globals; "Used by":
names it defines that the rest of the file uses). Rough counts; the compiler gives the exact list during a move.
Line numbers are for `bc5629ee8`.

| # | Lines | Size | Section | Needs (globals) | Used by |
|---|---|---|---|---|---|
| 1 | 1-214 | 214 | Includes, window wrapper | 1 (0) | 28 |
| 2 | 215-1621 | 1,407 | Auto-updater, download manager, git for projects, Supabase update mirror, start-up update screen, update poll, playtime | 5 (2) | 96 |
| 3 | 1622-2062 | 441 | Anti-cheat (client side, opt-in) | 19 (4) | 62 |
| 4 | 2063-2653 | 591 | Loading screen, notifications, editor crash reports, collections, shared look helpers | 14 (6) | 115 |
| 5 | 2654-3516 | 863 | Emoji, chat text formatting, link previews | 8 (1) | 23 |
| 6 | 3517-3683 | 167 | Game cover art, avatar textures | 10 (2) | 13 |
| 7 | 3684-4372 | 689 | Chat media: colour emoji, GIFs, custom emoji and stickers, the picker | 27 (5) | 22 |
| 8 | 4373-4636 | 264 | Sidebar icons, What's New history | 4 (1) | 10 |
| 9 | 4637-4870 | 234 | The OkayUI backend host | 84 (78) | 5 |
| 10 | 4871-5715 | 845 | Floating chat window, attachments, game invites, voice messages | 15 (5) | 75 |
| 11 | 5716-6050 | 335 | Chat poll state, blocks and privacy, sending crash reports, outbox | 16 (12) | 29 |
| 12 | 6051-7201 | 1,151 | Chat extras: pins, edit history, scheduled messages, custom status, search, forwarding, group chats, polls, games in chat | 56 (15) | 90 |
| 13 | 7202-8225 | 1,024 | Member moderation; `DrawGroupConversation` (808) | 101 (19) | 7 |
| 14 | 8226-10778 | 2,553 | Voice calls and their icons; `DrawChatWindow` (2,186) | 226 (107) | 24 |
| 15 | 10779-11155 | 377 | Tray, single instance, background friends and forum workers | 8 (6) | 63 |
| 16 | 11156-12846 | 1,691 | Multiplayer: joining, inviting, presence, server browser, parties, Multiplayer page, recent players, leaderboards | 59 (19) | 15 |
| 17 | 12847-13025 | 179 | Account: download my data, delete my account | 6 (6) | 30 |
| 18 | 13026-13588 | 563 | Public profiles data, admin data | 9 (6) | 52 |
| 19 | 13589-14231 | 643 | Anti-cheat backend, my bans and appeals, session tickets | 12 (4) | 7 |
| 20 | 14232-15244 | 1,013 | Reports (abuse, spam, scams); `DrawAdminPage` (579) | 43 (14) | 10 |
| 21 | 15245-16187 | 943 | `DrawPublicProfilePage` (942) | 52 (11) | 1 |
| 22 | 16188-16853 | 666 | Background workers: chat poll, emoji and GIF library, servers poll; the chat's own window | 112 (79) | 33 |
| 23 | 16854-18128 | 1,275 | The official game and its admin page | 48 (10) | 3 |
| 24 | 18129-27261 | 9,133 | `main` (every page) | 522 (257) | 0 |

The order (batches L1-L8 and X1 in Part A, section 3): section 5 first (few needs; it also creates
`launcher/LauncherShared.hpp`), then 19, 6, 8, 20, 21, 13, 16, 12, then the chat window and voice calls (14, 10, 22),
the updater (2, used by 96 names: its header is the "door" for the rest), and `main` last, after it has been
turned into one function per page. Section 9 (the OkayUI host) needs 78 globals and goes with `main`.
