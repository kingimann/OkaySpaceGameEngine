# Scripting migration: OkayScript to Lua and C# (decision record)

Internal record, not a guide. Owner decision (chat, newest word wins, rule 2/17): replace our own OkayScript languages
(the old VM `engine/src/Scripting/OkayScriptVM.cpp` and the typed language `engine/src/Script/*`) with **C# and Lua**.
**Visual Script stays the main way to script.** Done in stages; nothing the repo needs is deleted until a replacement works.

Branch `claude/scripting-migration-1`. Status table at the end of this file.

## 1. What exists today (counted from the tree, 2026-11-19)

| Piece | Where | Notes |
|---|---|---|
| Backend interface | `engine/include/okay/Scripting/ScriptVM.hpp` (`IScriptVM`, `ScriptHost`, `ScriptDiagnostic`) | Load, Validate, ValidateAll, Bind, CallStart, CallUpdate, CallEvent, CallEventWith, CallEventArgs, HasEvent, GetGlobal, SetGlobal, GlobalNames, BuiltinNames. Every language implements this one seam. |
| Registry | `engine/src/Scripting/ScriptVM.cpp`: `AvailableScriptLanguages()` (returns `{"okayscript"}`), `CreateScriptVM(lang)`, `RemovedScriptLanguageMessage(lang)` | Also creates `NextScriptVM` (`okayscript-next`) when asked by name, but does not list it. |
| Component | `engine/include/okay/Components/ScriptComponent.hpp`, `engine/src/Components/ScriptComponent.cpp` | One component for every language: language name, source, path, `fields` (inspector overrides applied with `SetGlobal`), Start/Update/FixedUpdate/LateUpdate/OnEnable/OnDisable/OnDestroy, trigger/collision/mouse/animation events as snake_case names (`on_collision_enter` ...), pool reuse. |
| Old VM | `engine/src/Scripting/OkayScriptVM.cpp`, 12,078 lines | JavaScript/C#-flavoured language, about 1,000 builtins, event alias table (`EventAliases()`), the engine binding. |
| Typed language | `engine/src/Script/*` (Lexer, Parser, Checker, Interpreter, EngineApi, NextScriptVM): 7,600 lines | `okayscript-next`, `class Name : Behaviour`. Not listed in `AvailableScriptLanguages()`. |
| Removed backends | `RemovedScriptLanguageMessage`: names `lua`, `csharp`, `cs` give "no longer supported" | Test `tests/test_script_removed_langs.cpp` (`script_removed_langs`) pins this and that `AvailableScriptLanguages().size() == 1`. |
| Editor | `editor/main.cpp`: language picker (shown only when more than one language is available), `StarterScript(lang)`, 15 mentions of `okayscript` | Starter text is in `engine/include/okay/Scripting/StarterScripts.hpp` (same OkayScript text for every language name). |

### The removed Mono backend (13.3) and why

Roadmap row 2.860.0 (PR #643, commit `ecb634c2`): "Lua and Mono script backends removed". Both were optional third-party
backends (liblua and Mono embedding, enabled by CMake options) and were deleted under the "write everything ourselves" rule
(17, 19a); the only language left was our own OkayScript, and old scenes naming `lua`/`csharp` get a clear error instead of
failing silently. The Mono backend was an embedding of the Mono runtime plus its own compiler step; it was never shipped
in releases. This migration brings **Lua back, written by us**, and plans C# separately (section 6) because real C# needs
either a runtime or a compiler of our own.

### How Visual Script relies on OkayScript

Visual Script (`engine/src/VisualScript/*`, `.okayvs` files, 69 in the repo) does **not** compile to OkayScript. It
depends on the old VM in three places, so the old VM cannot be deleted before these are replaced:

1. `VsBuiltins.cpp`: `BuiltinNodes()` creates one graph node per OkayScript builtin (`Fn.<name>`, from
   `OkayScriptVM::BuiltinNames()`), and each such node runs `ScratchVm().CallBuiltin(name, args)` (a shared static
   `OkayScriptVM`). This is how most of the visual nodes reach the engine (about 1,000 builtins).
2. `VsBuiltins.cpp`: the "snippet" node loads a one-function OkayScript (`function __snippet() { return (...) }`) in a
   fresh `OkayScriptVM` and calls it.
3. `VsApi.cpp` documents the typed `okayscript-next` language next to the graph nodes.

Also `shared/GameContentValidators.hpp` validates `.okayscript` files through `OkayScriptVM::Validate`, and the console model
links `script.okayscript:12:5` style locations.

### What uses OkayScript (counts from `grep`/`git ls-files`)

| Area | Count |
|---|---|
| Test files that mention `okayscript`, `ScriptComponent` or `CreateScriptVM` | 144 (91 of them name `okayscript` literally) |
| Scenes (`*.okayscene`) naming `okayscript` | 9 (11 scenes have a script component) |
| Editor plug-in scripts `examples/editor-plugins/*.okay` | 2 (`AlignTools.okay`, `CleanupNames.okay`; 76 lines) |
| Docs that mention OkayScript | 67 files (`docs/scripting.md`, `docs/scripting_reference.md` ...) |
| Built-in templates with embedded OkayScript text | `SceneTemplates.hpp` (14 mentions), `UITemplates.hpp` (6), `UIScreens.hpp` (1), `UIDocument.cpp` (2) |
| Build pipeline / launcher | `BuildPipeline.cpp` (1), `launcher/main.cpp` (1): script extensions in export lists |
| Content validators | `.okayscript` extension handled in `GameContentValidators.hpp` |
| Visual Script | 69 `.okayvs` files use `Fn.*` builtin nodes backed by the old VM (see above) |

## 2. Plan

| Stage | What | Status |
|---|---|---|
| 1 | This record | done |
| 2 | Lua 5.4, written by us: `engine/src/Lua/*`, binding `okay::LuaScriptVM`, language `lua` (`.lua`), tests, starter, example | done (section 4); build and test results in section 5 |
| 3 | C# options analysis (section 6), nothing written | done (analysis only) |
| 4 | Compatibility table (section 7) | done |
| later | Port samples and tests, move the Visual Script `Fn.*` nodes off the old VM onto a language-neutral builtin table, then delete OkayScript (owner approval; one release later) | not started |

Rules for every stage: nothing is deleted; `okayscript` stays the default language; new language is added beside it.

## 3. Risks

1. **Visual Script depends on the old VM's builtins** (section 1). Deleting OkayScript first would break about 1,000 nodes. They must move to a neutral builtin table first.
2. **Lua conformance.** The Lua here is written from the manual, so edge behaviour can differ from reference Lua (known gaps are listed in section 4). Scripts written for real Lua libraries (LuaRocks) will not all run.
3. **Sandbox and budgets.** A script language that can loop forever freezes the game. Instruction, time and memory limits exist and are tested, but are per entry call and approximate (time is checked every 2,048 instructions).
4. **Hot reload keeps no state.** Reload builds a new interpreter; script variables are lost unless saved with the existing `GlobalNames`/`SetGlobal` field mechanism.
5. **Two copies of the binding.** The Lua binding re-implements part of the OkayScript binding (Transform, Input, Time, Physics queries). Until the builtin table is shared, new engine features must be bound twice.
6. **Old scenes naming `lua`** used to get "no longer supported"; now they load and run. Their scripts were written for real Lua, so some may fail on gaps.
7. **Save games** read script variables through `GlobalNames`; Lua tables are saved as VsValue maps/arrays, so metatables and functions are not saved.
8. **C#** is a large unresolved decision (section 6) and needs an owner rule exception.
9. Tests: 144 test files depend on OkayScript; they keep running on the old VM and are not touched.

## 4. Lua (stage 2): what was built

Written from the public Lua 5.4 reference manual (lexical conventions, syntax, semantics, standard library chapters), in
our own words and structure. No code, comments or tests from the Lua project, LuaJIT or any other Lua. Constants and
behaviour were checked by tests we wrote from the manual (`tests/test_lua_lang.cpp`, 488 checks).

| File | What it is |
|---|---|
| `engine/src/Lua/LuaCore.hpp` | Value, objects (strings, tables, closures, natives, coroutines, userdata), `State`, limits, bytecode opcodes |
| `engine/src/Lua/LuaCompile.cpp` | Lexer (all escapes, long strings, hex floats), parser (full 5.4 expression priorities, `<const>`), our own **stack bytecode** compiler. Captured locals live in heap cells, so closures made in a loop get one variable per iteration |
| `engine/src/Lua/LuaVM.cpp` | Tables (array part plus insertion-ordered hash part), integer and float numbers with 5.4 rules, metatables (`__index __newindex __call __add ... __lt __le __eq __concat __len __tostring __pairs __metatable`), coroutines (own frame stacks, so `yield` works through `pcall`), `pcall`/`xpcall`, mark-and-sweep garbage collector, memory cap, instruction and wall-clock budget |
| `engine/src/Lua/LuaLib.cpp` | base library, `string` (our own pattern matcher: classes, sets, captures, `%b`, `%f`, back-references, `gsub`, `gmatch`, `format`), `table` (merge sort that survives bad comparison functions), `math`, a limited `os` (`time`, `clock`, `date`, `difftime`, `getenv` returns nil), `io.write` only, `coroutine` |
| `engine/src/Lua/LuaInterp.cpp`, `engine/include/okay/Lua/Lua.hpp` | `okay::lua::Interpreter`: the plain language with no engine types (used by the language tests) |
| `engine/src/Lua/LuaScriptVM.cpp`, `engine/include/okay/Lua/LuaScriptVM.hpp` | `okay::LuaScriptVM`, the `IScriptVM` backend for language `lua` |

Seams extended (no second component): `AvailableScriptLanguages()` now returns `okayscript` and `lua`;
`CreateScriptVM("lua")` creates it; `IScriptVM` got `HotReload` and `SupportsHotReload` (added at the end of the interface);
`ScriptComponent::Reload()` hot reloads when the language supports it; `StarterScriptSource("lua")`; the editor's New
Script dialog has a language picker (`.okay` or `.lua`) and attaching a `.lua` file creates a `lua` component.

### The engine binding

| Lua | Does |
|---|---|
| globals `gameObject`, `transform` | the object the script is on |
| `Start`, `Update(dt)`, `FixedUpdate`, `LateUpdate`, `Awake`, `OnEnable`, `OnDisable`, `OnDestroy` | called by name; the snake_case names (`on_enable`, `fixed_update`) work too. Start order Awake, OnEnable, Start, as OkayScript |
| `OnTriggerEnter(other)`, `OnCollisionEnter(other)` ... Stay, Exit, mouse events | same event names as every language; `other` is a GameObject object. Globals `other`, `contact_point`, `impact_speed` ... are set as for OkayScript |
| `gameObject.name/tag/active`, `:GetComponent("Type")`, `:SetActive`, `:Destroy`, `:IsAlive` | GameObject. A destroyed object reads as dead, not as a crash |
| `transform.position/localPosition/localScale/localEulerAngles/forward/right/up`, `:Translate :Rotate :LookAt` | Transform |
| component proxy: `rb = go:GetComponent("Rigidbody3D"); rb.mass = 7` | any registered component through `PropertyRegistry` paths (type checked; wrong names say so) |
| `Find(name)`, `CreateGameObject(name)`, `Destroy(obj[, seconds])` | scene |
| `Time.deltaTime/time/timeScale/fixedDeltaTime/frameCount` | read-only, `timeScale` can be set |
| `Input.GetKey/GetKeyDown/GetKeyUp(name)`, `GetMouseButton*`, `MousePosition()`, `GetAxis("Horizontal")` | same key names as OkayScript |
| `Physics.Raycast(origin, dir[, max])`, `Physics.OverlapSphere(c, r)` | 3D queries; hit table has `point normal distance gameObject` |
| `Log.info/warn/error/trace`, `print` | the Console (`[script]` prefix) |
| `Vector3.new(x,y,z)`, `+ - * /`, `.magnitude .normalized`, `Dot Cross Distance Lerp`; `Mathf.*` | vectors are plain Lua tables with a metatable (written in Lua, embedded in the VM) |
| public fields | top-level global variables that are numbers, strings, booleans, vectors, arrays or maps. Inspector overrides (`ScriptComponent::fields`), `GlobalNames()` (save games) and hot reload use them; functions and engine globals are not fields |

Errors use plain words with the script name and line: `script.lua:5: attempt to index a nil value (local 'x')`, `attempt to
call a nil value (global 'foo')`, `'end' expected (to close 'if' at line 1) near <eof>`. A runtime error is logged and the
game keeps running (the next frame calls `Update` again).

Limits per entry call (Start, Update, an event): 5,000,000 instructions, 0.5 seconds wall clock (checked every 2,048
instructions), 64 MB. `pcall` cannot catch these two kinds of error. Sandbox: no `io.open`, `os.execute/remove/exit`,
`require`, `dofile`, `loadfile`, `debug`, `package`, `string.dump`; the whole `os` table is removed for game scripts.

### Known gaps (honest)

`goto` and labels, `<close>` variables, `_ENV`, `utf8` library, `string.pack/unpack`, weak tables (`__mode`), `__gc`,
`__close`, tail-call elimination (deep recursion ends at 20,000 frames with "stack overflow"), `load` with an environment
argument, `string.format('%a')` is the C library's. The length of a table with holes can differ from reference Lua (any border
is allowed by the manual). Integer/float comparison and equality are exact; the number to text format is `%.14g`.
Yielding across a metamethod, an iterator called by a library function or `table.sort` is refused with a plain message.
No performance tuning has been done; it is a plain stack bytecode loop.

## 5. Status, tests, and what is unverified

Test targets (registered in `tests/CMakeLists.txt`): `lua_lang` (488 checks: conformance written from the manual, error
messages, syntax errors, coroutines, metatables, limits, sandbox, garbage collection under load), `lua_fuzz` (17,000 checks:
token soup, mutated programs, random bytes, pathological nesting), `lua_binding` (148 checks: the engine binding through
`ScriptComponent`, the example scene, hot reload), the changed `script_removed_langs`, and the existing `starter_script`
(now also runs the Lua starter) and `scripting`.

Results on the development machine (2026-11-19, Linux, g++): all of the above pass. `lua_lang` and `lua_fuzz` also ran clean
under AddressSanitizer and UBSan, and the fuzz test found one real bug (a 20,000 term `a + b + ...` chain overflowed the C++
stack in the compiler; the compiler now walks such chains in a loop and the parser refuses chains over 600 operators or
calls with a plain message). The three tests that need the engine were run against the engine sources of
commit `641f54cb` (the only prebuilt engine library on the machine) with the new and changed files patched in, **not** a
full build of current main: a full Debug build of this branch was started and abandoned because the machine was too
loaded (4 hours estimated). Not run: the full CMake build and `ctest` of this branch, the editor (`editor/main.cpp`
changes are unbuilt), the Windows (MinGW) build (`branch_lint.py mingw` compiled the new files with the win32-thread
mingw compiler and passed), and the one `lua_binding` line that calls `HotReload` on an OkayScript VM (it needs the new
interface compiled into the OkayScript backend).

Speed (one run, -O2, unloaded core, plain loop and calls): `fib(27)` 0.055 s, a 5,000,000 iteration arithmetic loop 0.25 s,
300,000 `tostring` and `#` operations 0.11 s. No tuning done.

## 6. C# (stage 3): options analysis, nothing written yet

The owner wants C# as a scripting language. Three ways to host it. All three plug into the same `IScriptVM` seam and the same
`ScriptComponent`; none touches Visual Script.

### What already exists that matters

`okayscript-next` (`engine/src/Script/*`, 7,600 lines, our own code) is already a typed, C#-shaped language
(`class Name : Behaviour`, fields, methods, types checked by `Checker.cpp`, a tree interpreter). It is a **C# subset of our
own**, today under another name and not listed in `AvailableScriptLanguages()`. Option (c) means growing that, not starting at zero.

### (a) .NET runtime through the hostfxr hosting API, loaded at run time

How it works: our code finds `hostfxr` (environment variable `DOTNET_ROOT`, the registry key for the installed runtime, or
`<exe folder>/runtime/host/fxr/<version>`), loads it with `LoadLibrary`/`dlopen`, declares the few documented function
signatures itself (`hostfxr_initialize_for_runtime_config`, `hostfxr_get_runtime_delegate`, `load_assembly_and_get_function_pointer`)
so **no SDK header or import library is linked**, starts CoreCLR, loads our own managed assembly `OkayScripting.dll`
(written by us in C#, the `GameObject`/`Transform`/`Input` classes calling back into the engine through a table of
function pointers), and loads user script assemblies. Scripts are compiled on the **developer's machine** with the Roslyn
compiler of an installed .NET SDK (`dotnet build` or `csc` from the SDK; used like a compiler, nothing copied into the repo).

| Question | Answer |
|---|---|
| Real C#? | Yes: the full language, `System.*`, NuGet packages, LINQ, async. |
| Size | Dev machine: the .NET SDK (about 250 MB, installed by the developer). Exported game: a runtime next to the exe, about 70 MB self-contained (less if trimmed; unverified here), plus our `OkayScripting.dll` (small) and the script assembly. A game with no C# ships none of it. |
| Windows build / export impact | Our hosting code is about 600 lines and only `LoadLibrary`/`GetProcAddress`, so the MinGW cross build is unchanged. Export has to copy the runtime folder and a `runtimeconfig.json` and add the MIT notice to `docs/THIRD_PARTY.md`. Cannot be tested in this Linux sandbox; needs a Windows machine with the SDK. Linux/macOS are parked (rule 14). |
| Startup / memory | Runtime start is roughly 50 to 100 ms and tens of MB (estimate, unverified). |
| Hot reload | Good: load script assemblies into a collectible `AssemblyLoadContext`, unload and load again; fields can be copied by name (as the Lua hot reload does). |
| Sandbox and budgets | **Weak.** .NET has no in-process sandbox (code access security is gone) and `Thread.Abort` is not supported, so an endless loop in a script cannot be stopped from outside, and a script can use files and network. The honest options: accept it for developer-written scripts, or run C# scripts in a separate process and kill it (costly, slow calls). No instruction or memory cap like the Lua one. |
| Debugging | Real debugger support is possible (attach Visual Studio); error text with line numbers from compiler output. |
| Rule 17/19a | The runtime and the compiler are third-party code, so this needs the owner's exception (text below). They stay outside the repo. |
| Main risks | The owner's machine must have the SDK; every engine API is written twice (C++ and the managed wrapper); version drift of the hosting API; a crash in managed code takes the process down. |

### (b) Mono embedding (what the removed backend did)

`libmonosgen` loaded or linked, `mono_jit_init`, `mono_runtime_invoke`, internal calls to our C++. Compiler: `mcs`/Roslyn.

| Question | Answer |
|---|---|
| Real C#? | The older .NET Framework 4.x style class library; recent language versions only through Roslyn output. |
| Size | Runtime about 15 to 25 MB plus the class library (about 30 MB, unverified). |
| Hot reload / budgets | Better than (a): Mono still has app-domain unload and thread abort, so an endless loop can be stopped and code reloaded (unverified here). |
| Risks | The standalone Mono project is in maintenance and no longer distributes Windows installers for new versions; we would carry our own Mono build, which is third-party **source** to build and keep. It was already removed once (#643) for exactly that reason. |
| Verdict | Not recommended: same exception as (a), plus a dying dependency. |

### (c) Our own C#-subset compiler and VM (grow `okayscript-next`)

Lexer, parser, checker, bytecode and VM written by us (the front end already exists in `engine/src/Script/*`).

| Question | Answer |
|---|---|
| Real C#? | **No.** A subset: classes, structs, enums, interfaces, generics for List/Dictionary, lambdas, a small `System` subset. No NuGet, no reflection, no async, no unsafe code. Unity tutorials will not paste in unless we also write a Unity-shaped API. |
| Size | Everything is ours and is part of the engine: no runtime to ship. Effort: large. Rough estimate from the existing 7,600 lines: another 15,000 to 25,000 lines (bytecode VM, generics, properties, interfaces, a larger library, LINQ) and many weeks. |
| Windows build / export | None: it is plain C++ in the engine. |
| Hot reload / sandbox / budgets | All the same as Lua: instruction cap, wall clock cap, memory cap, no file access unless we add it. |
| Rule 17/19a | Fully inside the rules. Written from the C# language specification (ECMA-334), no code from Roslyn or Mono. |

### Recommendation

**(a), with hosting code written by us, behind a CMake option that is OFF by default, and used only for projects that
contain `.cs` scripts.** It is the only route to *real* C# that a game developer will recognise, and the third-party part
(the runtime and the compiler) never enters the repository and is not linked. Keep **(c)** (`okayscript-next`) as the
dependency-free, sandboxed option meanwhile; it is not thrown away. Do not do (b).

Cost the owner accepts with (a): a Microsoft runtime in exported C# games (about 70 MB), no hard stop for a runaway C#
script, the developer needs the .NET SDK, and everything unverified until built on Windows.

**Rule 19a exception text the owner must confirm (not applied; CLAUDE.md is not changed by this branch):**

> Exception for C# scripting (owner, date to fill in): the Microsoft .NET runtime (CoreCLR, MIT licence) may be loaded at
> run time through its documented hosting API (hostfxr) to run C# scripts, and the Roslyn compiler of a .NET SDK the
> developer installs may be run as a build tool, like MSVC (rule 24b). Nothing from the runtime, the SDK or NuGet is copied or
> linked into the repository; our hosting code and our managed API assembly (`OkayScripting.dll`) are written by us. It is
> opt-in per project behind a CMake option that is OFF by default; a game with no C# scripts ships no runtime. Exported C#
> games ship the runtime files with their MIT notice in `docs/THIRD_PARTY.md`. Windows only (rule 14). Say "unverified" for
> anything not built and run on Windows. This allows no other SDK or library, and rule 19a still holds for everything else.

## 7. Compatibility: OkayScript to Lua and C#

"Lua" means our Lua after this branch. "C# (a)" is the recommended real C# with a Unity-like API wrapper (not built).
**Nothing was ported or deleted.** Where OkayScript has a one-line behaviour builtin, the Lua version is a few lines.

| OkayScript | Lua (now) | C# (a), planned |
|---|---|---|
| `void Start()`, `void Update()`, `Awake`, `OnEnable`, `FixedUpdate`, `LateUpdate`, `OnDisable`, `OnDestroy` | `function Start() end` etc., same names, `dt` is passed to `Update` | `void Start()` in a `class X : Behaviour` |
| `OnTriggerEnter(other)`, `OnCollisionEnter(other)` ... | same names, `other` is a GameObject | same |
| fields at file level (public fields) | global variables | `public float speed;` |
| `var`, `int`, `float`, `string`, `bool` | untyped locals (`local x = 1`); integer and float are separate subtypes | typed |
| `List<T>`, arrays, `Dictionary<K,V>` | tables (`{}`) | `List<T>`, `Dictionary` |
| `Vector3`, `new Vector3(x,y,z)`, `v + w`, `.normalized` | `Vector3.new(x,y,z)`, operators, `.normalized` | `Vector3` struct |
| `transform.position`, `Translate`, `Rotate`, `LookAt` | same | same |
| `GetComponent<T>()` | `go:GetComponent("T")`, properties by name | same |
| `Find("x")`, `Destroy(x)`, `Instantiate(...)` | `Find`, `Destroy`; **`Instantiate` and prefabs are not bound yet** | planned |
| `key("a")`, `key_down`, `axis_x()` | `Input.GetKey("a")`, `GetKeyDown`, `GetAxis("Horizontal")`; gamepad and touch **not bound yet** | planned |
| `Time.deltaTime`, `Time.time` | same | same |
| `raycast`, `overlap_*` | `Physics.Raycast`, `Physics.OverlapSphere` (3D). **2D queries, layers, shape casts not bound yet** | planned |
| `Log`, `print`, `watch` | `Log.info/warn/error`, `print`; `watch` not bound | planned |
| movement one-liners `walk`, `spin`, `patrol`, `follow`, `bob` | not provided: write the few lines, or call them from Visual Script | not provided |
| coroutines `StartCoroutine`, `yield return` | `coroutine.wrap` / `coroutine.yield` (our own scheduler for `wait(seconds)` is **not** provided) | `IEnumerator` |
| tweening (DOTween-style), drag and drop, save system, prefs, rendering/post-processing, UI, audio, network, inventory, spline, weapons builtins (about 1,000 builtins) | **not bound** (these are the OkayScript builtin table in `OkayScriptVM.cpp`) | planned |
| Visual Script `Fn.*` nodes and the snippet node | still run on the old VM (unchanged) | unchanged |
| `.okay` files, `okayscript` language name | `.lua` files, `lua` | `.cs` |

### Samples that would need porting (not done)

| What | Count / where | Note |
|---|---|---|
| Editor plug-ins | `examples/editor-plugins/AlignTools.okay`, `CleanupNames.okay` | use editor builtins; stay OkayScript |
| Scenes with an OkayScript component | 9 `*.okayscene` that name `okayscript` | port one by one after the builtins they use are bound |
| Built-in templates and screens | `SceneTemplates.hpp` (14), `UITemplates.hpp` (6), `UIScreens.hpp` (1), `UIDocument.cpp` (2) | embedded OkayScript text |
| Tests | 91 test files name `okayscript` (144 touch `ScriptComponent`) | keep running on the old VM; a port needs the builtin table first |
| Docs | 67 docs mention OkayScript | internal records only (CLAUDE.md rule 5) |

### What must happen before OkayScript can be deleted (later, owner approval)

1. A language-neutral builtin table so Visual Script `Fn.*` nodes and the snippet node no longer need `OkayScriptVM`.
2. The Lua binding covers the builtins the shipped scenes and templates use (2D physics, instantiate, wait/coroutine scheduler, UI, audio, tweening, saves).
3. The samples and tests above are ported or retired.
4. Real C# decided (section 6), or OkayScript's typed language kept as the C# subset.
5. Delete one release later (rule 17).
