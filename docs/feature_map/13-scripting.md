# 13. Scripting

Text scripting for games and for the editor: the default OkayScript language (one big VM with several hundred built-in functions), a newer typed C#-style language that is checked before it runs, the Script component that runs them, and the Script Editor.
Covers `engine/include/okay/Scripting/*`, `engine/include/okay/Script/*`, `engine/src/Scripting/*`, `engine/src/Script/*` (about 11.3k + 7.1k lines), `Components/ScriptComponent`, `docs/scripting*.md` and the Script Editor inside `editor/main.cpp` (about 3k lines plus completion and docs windows).
Two OkayScript languages exist side by side: id `okayscript` (old VM, the default) and `okayscript-next` (typed, `class Name : Behaviour`); the typed language is being grown to replace the old one, with several branches not on main yet.

## 13. Scripting

### 13.1 OkayScript VM
- **What:** The default scripting language. A script file is a component: fields are Inspector settings, `Start()` and `Update()` (or `start()`, `update(dt)`) run by name; C#-style and C-style syntax both parse; hundreds of built-in functions drive the scene.
- **Parts:**
  - 13.1.1 Lexer, parser and runtime - `Scripting/OkayScriptVM` - the interpreter core inside one 10.5k-line file (lexer ~359, AST ~581, runtime ~593, parser ~2256)
  - 13.1.2 C#-style syntax - `Scripting/OkayScriptVM` - typed fields, `[Header]`/`[Range]`, enums, switch, `$"..."`, lists and dictionaries, lambdas and LINQ-style helpers, `??`, `?.`, classes with `new`, try/catch/throw
  - 13.1.3 Coroutines and waits - `Scripting/OkayScriptVM` - `IEnumerator`, `yield return`, `WaitForSeconds`, `WaitUntil`, `WaitWhile`, StartCoroutine/StopCoroutine
  - 13.1.4 Event handlers - `Scripting/OkayScriptVM` - on_click, on_collision, OnTriggerEnter, OnMouseDown, OnCollisionEnter/Stay/Exit and the lifecycle (Awake, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy)
  - 13.1.5 Diagnostics - `Scripting/OkayScriptVM` - `Validate` and `ValidateAll` with line and column, "does nothing" warnings for pasted C# that has no effect
  - 13.1.6 Host builtins - `Scripting/OkayScriptVM` - `AddBuiltin(name, fn)` lets an embedder (editor plugins, tests) add its own functions
  - 13.1.7 Script safety - `Scripting/OkayScriptVM` - file names passed to save, prefs, data, build and scene calls must stay inside the game folders, no absolute paths or `..`
- **Code:** `Scripting/OkayScriptVM`, `Scripting/ScriptVM`
- **API:** `okay::OkayScriptVM`, `CreateScriptVM("okayscript")`, `IScriptVM::Load/Validate/ValidateAll/CallStart/CallUpdate/CallEvent/HasEvent/GetGlobal/SetGlobal`, `OkayScriptVM::AddBuiltin`
- **Editor:** Script Editor (13.5); Inspector header "Script (name)" with the script's public fields; Console shows runtime errors
- **Docs:** `docs/scripting.md`, `docs/scripting_reference.md` (the old `SCRIPTING.md` named in the earlier map no longer exists)
- **Tests:** `scripting`, `scriptapi`, `script_semantics`, `script_lang2`, `script_lang3`, `script_events`, `script_arrays`, `script_maps`, `script_debug`, `script_paths`, `okayscript_csharp`, `csharp_style_script`
- **Status:** Works - default language, used by every sample and by editor plugins; broad test coverage across syntax and builtins
- **Open:** possible doc conflict: `docs/scripting.md` line ~144 says own classes are not supported, while the "Classes and new" section documents them; one 10.5k-line file (splitting is listed in 19.4); the earlier map named a "scripting bug list 1-15": ask before assuming any of it is still open
- **Related:** 13.2, 13.4, 13.5, 13.7, 19.4
- **Words:** OkayScript, okayscript, script language, start update, function, variable, coroutine, yield, lambda, LINQ, C# style, event handler, lifecycle, VM, Lua-like
- **Seen as:** a script does nothing or errors at runtime; "unknown function"; a field does not show in the Inspector; Start or Update never runs; a pasted C# script is accepted but has no effect; a coroutine stops early
- **Ask me like:** "work on 13.1.3, coroutine bugs"; "script says unknown function, which part is that"

### 13.2 C#-style language core
- **What:** The typed OkayScript (`okayscript-next`): a script is `class Name : Behaviour`, source goes through lexer, parser, checker (every name resolved, every type known) and only then the interpreter, so a script either runs or says why not, with line and column.
- **Parts:**
  - 13.2.1 Lexer - `engine/src/Script/Lexer.cpp` - tokens with line and column, `$"..."` nesting, keywords
  - 13.2.2 Parser - `engine/src/Script/Parser.cpp` - syntax tree; C# words not supported yet (try, throw, struct, interface, override, virtual, yield, event, async, delegate...) give a clear error
  - 13.2.3 Checker - `engine/src/Script/Checker.cpp` - name and type resolution, lifecycle method table, errors and "does nothing" warnings
  - 13.2.4 Interpreter - `engine/src/Script/Interpreter.cpp` - walks the checked tree; runtime errors (destroyed object, divide by zero, endless loop) carry a line
  - 13.2.5 Shared internals - `engine/src/Script/ScriptInternal.hpp` - types, values, program and instance structures
  - 13.2.6 VM wrapper - `Scripting/NextScriptVM`, `engine/src/Script/NextScriptVM.cpp` - `IScriptVM` over the pipeline; static fields last one play session per scene
- **Code:** `Scripting/NextScriptVM`, `engine/src/Script/NextScriptVM.cpp`, `engine/src/Script/Lexer.cpp`, `engine/src/Script/Parser.cpp`, `engine/src/Script/Checker.cpp`, `engine/src/Script/Interpreter.cpp`, `engine/src/Script/ScriptInternal.hpp`
- **API:** `NextScriptVM::kLanguage` (`okayscript-next`), `NextScriptVM::ForgetSceneStatics`, `LastError`, `ErrorCount`; script syntax `class Name : Behaviour { public float speed; void Update() {...} }`
- **Editor:** the Script Editor runs this checker for scripts whose language is `okayscript-next` (inline errors and warnings with columns)
- **Docs:** `docs/scripting.md` (partly describes both languages), `docs/engine_api.md`
- **Tests:** `okayscript_next`, `okayscript_next_csharp`
- **Status:** Partial - on main: classes, enums, switch, arrays, foreach, statics, interpolation, `??` and `?.`, List and Dictionary, lambdas; not on main: try/catch, coroutines, events, inheritance and interfaces
- **Open:** in flight (branches, no PR, 2026-10-26): claude/csharp-p1-events, -inheritance, -try, -coroutines, -getcomponent, claude/next-event-args; `AvailableScriptLanguages()` lacks `okayscript-next` so New Script cannot pick it (possible gap); replacing the old VM is not done
- **Related:** 13.1, 13.6, 13.5, 14.7
- **Words:** okayscript-next, typed OkayScript, new OkayScript, class Behaviour, checker, interpreter, C# parity, lambdas, LINQ, statics, NextScriptVM
- **Seen as:** a script is rejected with "isn't supported yet"; a new feature works in one language and not the other; "A script needs a class"; errors point at the wrong column
- **Ask me like:** "work on 13.2.2, add try/catch to the typed language"; "which branch has coroutines for the new OkayScript"

### 13.3 Lua (our own) and the removed C# (Mono) backend
(C# is back as real C# through the hosted .NET runtime: see 13.8.)
- **What:** Language `lua` (`.lua` files) is our own Lua 5.4, written from the public manual (decision record: `docs/scripting_migration.md`). It runs through the same `IScriptVM` seam and `ScriptComponent` as OkayScript: global functions are the entry points (Start, Update, events), global variables are the public fields, with instruction, time and memory limits and hot reload. The third-party Mono C# backend stays removed; an old scene naming `csharp` or `cs` still loads and says "no longer supported". Real C# is an open decision (options analysis in the migration record).
- **Parts:**
  - 13.3.1 Lua language core - `Lua/LuaCompile`, `Lua/LuaVM`, `Lua/LuaLib` - lexer, parser, stack bytecode compiler, VM with tables, metatables, closures, coroutines, garbage collector, string patterns, math, table, limited os and io
  - 13.3.2 Lua engine binding - `Lua/LuaScriptVM` - gameObject, transform, components by property name, Find, Destroy, Time, Input, Log, Physics.Raycast, Vector3, Mathf, events with `other`, public fields, hot reload
  - 13.3.3 Limits and sandbox - `Lua/LuaVM` - per-call instruction and wall-clock budget, memory cap, no file, process or module access
  - 13.3.4 Removed-language message - `Scripting/ScriptVM` - `RemovedScriptLanguageMessage(lang)` now returns empty for every name (the Mono backend stays gone; `csharp` is the hosted .NET language of 13.8)
- **Code:** `Lua/LuaCore`, `Lua/LuaCompile`, `Lua/LuaVM`, `Lua/LuaLib`, `Lua/LuaInterp`, `Lua/LuaScriptVM`, `Scripting/ScriptVM`, `Components/ScriptComponent`
- **API:** `okay::LuaScriptVM`, `okay::lua::Interpreter`, `CreateScriptVM("lua")`, `IScriptVM::HotReload/SupportsHotReload`, `RemovedScriptLanguageMessage()`, `AvailableScriptLanguages()`
- **Editor:** Script Editor "New Script" has a Language picker (OkayScript or Lua); `.lua` files attach as `lua` script components (unbuilt here: see the migration record)
- **Docs:** `docs/scripting_migration.md` (internal record)
- **Tests:** `lua_lang`, `lua_fuzz`, `lua_binding`, `script_removed_langs`, `starter_script`
- **Status:** Partly works - the language and the basic binding are tested (language and fuzz tests also under ASan and UBSan); not yet bound: instantiate, 2D physics queries, wait or coroutine scheduler, UI, audio, tweening, saves; no `goto`, `<close>`, `utf8`; the editor change and the full build are unverified
- **Open:** the OkayScript builtin table is not shared with Lua (the same engine feature must be bound twice); Visual Script nodes still run on the OkayScript VM; real C# is 13.8
- **Related:** 13.1, 13.4, 13.5, 14
- **Words:** Lua, lua script, .lua, C#, Mono, csharp, removed language, language not supported, old project, script language dropdown, hot reload, sandbox, instruction limit
- **Seen as:** a Lua script errors with `script.lua:LINE:`; a Lua script says "too many instructions"; a C# script says "C# is not enabled in this build"
- **Ask me like:** "work on 13.3.2, bind Instantiate for Lua"; "why does my C# script not run"

### 13.4 Script components and script UI
- **What:** The glue that puts a script on an object: the language-neutral VM interface, the Script component with its source and public fields, starter scripts, and the bridge that lets scripts draw immediate-mode UI.
- **Parts:**
  - 13.4.1 VM interface and factory - `Scripting/ScriptVM` - `IScriptVM`, `ScriptHost`, `ScriptDiagnostic`, `CreateScriptVM`, `AvailableScriptLanguages`
  - 13.4.2 Script component - `Components/ScriptComponent` - language, source or file path, Reload, lifecycle forwarding, serialized per-instance field overrides
  - 13.4.3 Public fields in the Inspector - `editor/main.cpp` (`ParseScriptFields` ~25567-25708, Script header ~31140) - reads `public` fields and `[Header]`, `[Tooltip]`, `[Range]` from the source
  - 13.4.4 Starter scripts - `Scripting/StarterScripts` - what a new empty script starts as, per language
  - 13.4.5 Script UI bridge - `Scripting/ScriptUI`, `shared/OkayScriptUIBridge.hpp` - `ui_*` builtins forward to a host toolkit (OkayUI); see 40.4
- **Code:** `Scripting/ScriptVM`, `Components/ScriptComponent`, `Scripting/StarterScripts`, `Scripting/ScriptUI`, `shared/OkayScriptUIBridge.hpp`
- **API:** `ScriptComponent::LoadSource/LoadFile/Reload/Source/fields`, `SetScriptUI`, `ScriptUIBridge`, `StarterScriptSource(lang)`
- **Editor:** Add Component > Scripts (every `.okay` file in the project, New Script...); Inspector header "Script (name)"; Script Editor
- **Docs:** `docs/scripting.md`, `docs/editor.md`
- **Tests:** `script_components`, `script_ui`, `starter_script`, `scripting`
- **Status:** Works - used by every script; starter script test loads it in the real engine
- **Open:** `Scripting/ScriptUI` bridge has only an OkayUI implementation (in `shared/`), so a build without OkayUI draws nothing for `ui_*` calls
- **Related:** 13.1, 13.5, 40.4, 12.13
- **Words:** ScriptComponent, Script component, script field, public variable, Inspector field, starter script, new script, ScriptUIBridge, IScriptVM, ScriptHost
- **Seen as:** a public field is missing or reset in the Inspector; a new script starts with the wrong template; a script edit does not reload; `ui_button` draws nothing
- **Ask me like:** "work on 13.4.3, show enum fields in the Inspector"; "new scripts should start with a class"

### 13.5 Script editor: autocomplete, errors, rename, format
- **What:** The built-in code editor (View > Editors & Tools > Script Editor): tabs, syntax colours, inline errors and warnings, autocomplete with docs, outline, rename, find in files, formatting and an external-IDE bridge.
- **Parts:**
  - 13.5.1 Editor window and tabs - `editor/main.cpp` (`DrawScriptEditor` ~12930-15670) - one tab per Script component, float or dock, File menu (New, Save, Reload, Switch Script); since #577 a larger code face and a collapsible script list (Alt+1)
  - 13.5.2 Text widget and colours - `editor/vendor/TextEditor.*` (vendored), `editor/main.cpp` (`OkayScriptLangDef` ~12687-12930) - syntax highlighting, gutter, changed-line bars, minimap, zoom
  - 13.5.3 Problems and inline errors - `editor/main.cpp` (~76-230 `AnalyzeScriptWarnings`, Problems panel ~15460) - ValidateAll diagnostics, did-you-mean lint, click-to-fix gutter action
  - 13.5.4 Autocomplete and signature help - `editor/main.cpp` (C#-aware member completion ~12178, details with code examples ~12380, floating `##autocomplete`, `##sighelp`, `##sighint`) - fed by `BuiltinNames()` and the typed Engine API
  - 13.5.5 Navigation - `editor/ScriptText.hpp` - tokenizer, outline, Go to Symbol, Go to Definition, Find All References, bookmarks, Rename Symbol (pure text, covered by selftest)
  - 13.5.6 Find, replace and Find in Files - `editor/main.cpp` (Find and replace bar ~13930-13990 with Replace and Replace All since #577, `DrawFindInFiles` ~6795-6944) - Ctrl+F, Ctrl+Shift+F across project scripts
  - 13.5.7 Edit tools - `editor/main.cpp` - Format Document, Toggle Comment, duplicate and move line, auto-close brackets, Command Palette, Go to Line
  - 13.5.8 External IDE - `editor/main.cpp` (`namespace extide` ~3359-3432) - Open in External IDE, Live Sync of outside edits, conflict banner
  - 13.5.9 Insert menu - `editor/main.cpp` - C# snippets, Behaviour Template, API Command picker
  - 13.5.10 Scripting Reference window - `editor/main.cpp` (`DrawScriptDocs` ~10940-12177) - the function reference inside the editor (Help > Scripting Reference)
- **Code:** `editor/main.cpp`, `editor/ScriptText.hpp`, `editor/vendor/TextEditor.*` (vendored, out of scope)
- **API:** menu File / Edit / View / Go / Insert / Help inside the Script Editor; shortcuts Ctrl+F, Ctrl+Shift+F, Ctrl+/, Ctrl+D, Alt+Up/Down
- **Editor:** View > Editors & Tools > Script Editor; Help > Scripting Reference; Layout preset "Scripting"
- **Docs:** `docs/editor.md` (Script Editor), `docs/scripting.md`
- **Tests:** `editor` (selftest covers ScriptText outline, identifier search and the lint), `starter_script`
- **Status:** Works - features are present as menu items and code per the editor source; no separate unit tests except the selftest
- **Open:** earlier map: "meaningful syntax only; classes and templates for New Script" (open); no step debugger for text scripts (only the visual-script debugger, 14.5); the 2.7k-line window lives in `main.cpp` (15.10). Script compile errors and missing script files are also rows in the project Problems panel (15.11.8), separate from this window's own Problems list
- **Related:** 13.1, 13.2, 13.4, 15.10, 15.8
- **Words:** script editor, code editor, autocomplete, red squiggles, problems, rename symbol, format document, find in files, go to definition, outline, snippets, external IDE, VS Code, live sync, syntax highlighting
- **Seen as:** red squiggles are wrong or missing; autocomplete does not offer a function; Rename misses a usage; Format changes code; a save in the external IDE does not reload; typing lags in a big script
- **Ask me like:** "work on 13.5.4, show signature help for typed scripts"; "autocomplete should offer GetComponent"

### 13.6 Engine API and generated docs
- **What:** One table of bindings (every type, property and method a typed script can use), backed by real engine calls. The checker resolves names against it, the editor autocompletes from it, the visual-script Engine API nodes are generated from it, and `docs/engine_api.md` is printed from it.
- **Parts:**
  - 13.6.1 Binding table - `engine/src/Script/EngineApi.cpp` - GameObject, Transform, Rigidbody, Input, Time, Debug, Audio, SaveGame, Localization, Sequence, post-process, NavMeshAgent, Dialogue, Quest, BehaviorTree, Spline, UI components and more
  - 13.6.2 IK and Animator script tools - `Script/AnimIkTools` - shared by OkayScript builtins and the typed API so one IK kind name means the same everywhere (foot, look, aim, hand_l, hand_r, full)
  - 13.6.3 Generated reference - `docs/engine_api.md` - regenerate with `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` from the build folder; the `vs_types` test fails while it is stale
- **Code:** `engine/src/Script/EngineApi.cpp`, `Script/AnimIkTools`, `VisualScript/VsApi`
- **API:** `OKAY_WRITE_API_DOCS=1`, `VsApiReference()`, `ParseIkKind`
- **Editor:** Script Editor autocomplete list; visual-script palette "Engine API" nodes (14.7)
- **Docs:** `docs/engine_api.md` (generated, do not edit by hand)
- **Tests:** `vs_types` (checks the generated page), `okayscript_next`, `vs_acceptance` (Engine API nodes end to end), `api_expand` (script API additions, exercised with an animation clip)
- **Status:** Works - the page is generated from the table and guarded by a test
- **Open:** animation and IK script tools are Grok's area (5.6, 5.7): do not edit them here
- **Related:** 13.2, 14.7, 5.6
- **Words:** engine API, binding table, EngineApi, API reference, autocomplete list, IK script tools, generated docs, engine_api.md
- **Seen as:** a member autocompletes but fails to run; engine_api.md differs from the code; an API node is missing from the palette
- **Ask me like:** "work on 13.6.1, add Camera shake to the typed API"; "regenerate engine_api.md"

### 13.7 Built-in function library
- **What:** The several hundred built-in functions of the old VM, grouped as in `docs/scripting_reference.md`: movement one-liners, transforms, rigidbody, input, objects and scene, UI, tweens, saves, scene manager, networking, character animation, Steam, collections, JSON, timers, spawning and physics queries.
- **Parts:**
  - 13.7.1 Movement, transform and rigidbody - `Scripting/OkayScriptVM` - dt-scaled one-liners (walk, spin, follow, patrol, orbit, platformer, smooth_follow)
  - 13.7.2 Input, objects, scene and spawning - `Scripting/OkayScriptVM` - key and axis, find and spawn objects, hierarchy queries, Stats and survival access
  - 13.7.3 Tweens, easing and timers - `Scripting/OkayScriptVM` - tween_move, tween_ui_*, shake, easing curves, timers; since #587 `tween_prop(path, to, dur[, ease][, "done"])` tweens any registered property (1.2.11), `tween_clock(id, channel)` follows a named clock, and `tween_ease` and the ease argument of `tween_*` accept curve text such as `cubic-bezier(0.4,0,0.2,1)`; the editor autocomplete list does not know the two new names
  - 13.7.4 Saves and prefs - `Scripting/OkayScriptVM` - save_game slots, prefs_get/set, data assets
  - 13.7.5 Collections, strings, JSON and functional helpers - `Scripting/OkayScriptVM` - arrays, maps, to_json/from_json, map/filter-style helpers, error handling
  - 13.7.6 Physics queries and events - `Scripting/OkayScriptVM` - raycasts, overlaps, layers, trigger and collision handlers
  - 13.7.7 Camera, effects, tilemap, navmesh, music, particles - `Scripting/OkayScriptVM` - camera and post-processing calls, tilemap edits, nav_path_*, music_*
  - 13.7.8 Multiplayer, Steam and accounts - `Scripting/OkayScriptVM` - net_* messaging, steam_* achievements and stats, account_* sign-in
  - 13.7.9 Character animation - `Scripting/OkayScriptVM`, `Script/AnimIkTools` - play and query clips on a Character or Model Animator (Grok area)
- **Code:** `Scripting/OkayScriptVM`, `Script/AnimIkTools`
- **API:** `BuiltinNames()` lists every function; reference tables in `docs/scripting_reference.md`
- **Editor:** Help > Scripting Reference; Script Editor autocomplete; Insert > API Command
- **Docs:** `docs/scripting_reference.md`, `docs/scripting.md`
- **Tests:** `script_spawn`, `script_physics`, `script_events`, `script_util`, `script_lib`, `script_camera`, `script_fx`, `script_objects`, `script_tilemap`, `script_net`, `script_anim`, `script_batch1`, `script_nav`, `script_paths`, `steam_script`, `account_script`, `anim_script_control`, `inventory_script`
- **Status:** Works - large and tested by area; each test file maps to one slice of the table
- **Open:** the same calls are re-implemented for the typed language in `EngineApi.cpp`: the two tables can drift (possible duplicated system, see 13.2)
- **Related:** 13.1, 13.6, 7, 9, 16, 17.10, 11.1
- **Words:** builtin, built-in function, scripting reference, walk, spin, follow, tween_move, prefs_get, save_game, raycast, net_send, steam_unlock, nav_path, music_play, ui_set_text
- **Seen as:** "unknown function"; a function behaves differently than the reference says; a function exists in OkayScript but not in a graph node or the typed API
- **Ask me like:** "work on 13.7.6, add a capsule cast"; "list the built-ins for camera shake"

### 13.8 Real C# (hosted .NET runtime)
- **What:** Language `csharp` (`.cs` files): real C# 12 compiled by the developer's installed .NET SDK, run in the .NET runtime that the engine starts through its documented hosting API (hostfxr, loaded at run time, nothing linked or copied into the repo). A script is `public class X : Behaviour` using our own managed API assembly `OkayScripting.dll` (`csharp/OkayScripting`). Opt-in per project; CMake option `OKAY_WITH_CSHARP` is OFF by default and the code then compiles into stubs that say "C# is not enabled in this build". Windows only for now. Owner approved 2026-11-19 (rule 24c).
- **Parts:**
  - 13.8.1 Runtime host - `CSharp/DotnetHost` - finds .NET (game `runtime/`, DOTNET_ROOT, standard folders), picks a version, writes `runtimeconfig.json`, starts hostfxr, plain-words errors
  - 13.8.2 Managed API - `csharp/OkayScripting` - Behaviour, GameObject, Transform, Component, Vector2/3, Quaternion, Mathf, Time, Input, Debug, Physics.Raycast, Find, Destroy, Instantiate, public fields in the Inspector
  - 13.8.3 Script VM - `CSharp/CSharpScriptVM` - `IScriptVM` for `csharp`: lifecycle and collision/trigger events, fields, hot reload (collectible load context), one plain error line per fault, over-budget warning (cannot abort a call)
  - 13.8.4 Compile - `CSharp/CSharpCompiler` - `dotnet build` of each script cached by content hash, diagnostics with line and column, editor compile on save
  - 13.8.5 Export - `CSharp/CSharpCompiler` (`ExportCSharp`) - Build Game ships the runtime, `OkayScripting.dll` and compiled scripts only for projects with C# scripts
- **Code:** `CSharp/DotnetHost`, `CSharp/CSharpCompiler`, `CSharp/CSharpScriptVM`, `CSharp/HostfxrBridge`, `csharp/OkayScripting/*.cs`, `Scripting/ScriptVM`, `Components/ScriptComponent`
- **API:** `okay::CSharpScriptVM`, `CreateScriptVM("csharp")`, `okay::csharp::DotnetRuntime`, `csharp::CompileScript`, `csharp::CompileForEditor`, `csharp::ExportCSharp`, `OkayNativeApi`
- **Editor:** New Script offers "C# (.cs)" when compiled in; saving a C# script compiles it and lists errors in the Console and Problems
- **Docs:** `docs/scripting_migration.md` section 8 (internal record)
- **Tests:** `csharp_host`, `script_removed_langs`
- **Status:** Partial - the pure logic, the VM (against a fake managed side), the compiler helpers and the export copy are tested on Linux; **unverified: everything that needs a real .NET on Windows** (starting the runtime, compiling `OkayScripting.dll` and scripts, running a script, hot reload, the editor change, export of the runtime)
- **Open:** build and run on Windows; 2D physics, coroutines, tags and layers, engine-component `GetComponent<T>`; the hosted runtime cannot abort a runaway script
- **Related:** 13.1, 13.3, 13.4
- **Words:** C#, csharp, .cs, .NET, dotnet, hostfxr, Behaviour, OkayScripting, Roslyn, compile on save, Scripts.dll, C# not enabled
- **Seen as:** "C# is not enabled in this build"; "The .NET runtime was not found. Install .NET 8"; "The .NET SDK was not found"
- **Ask me like:** "why does my C# script not run"; "work on 13.8, add 2D raycasts to OkayScripting"
