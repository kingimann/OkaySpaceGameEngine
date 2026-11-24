# 13. Scripting

Text scripting for games and for the editor: the default OkayScript language (one big VM with several hundred built-in functions), a newer typed C#-style language that is checked before it runs, the Script component that runs them, and the Script Editor.
Covers `engine/include/okay/Scripting/*`, `engine/include/okay/Script/*`, `engine/src/Scripting/*`, `engine/src/Script/*` (about 11.3k + 7.1k lines), `engine/src/Lua/*` and `engine/include/okay/Lua/*` (our own Lua interpreter, about 6.9k lines), `Components/ScriptComponent`, `docs/scripting*.md` and the Script Editor inside `editor/main.cpp` (about 3k lines plus completion and docs windows).
Two OkayScript languages exist side by side: id `okayscript` (old VM, the default) and `okayscript-next` (typed, `class Name : Behaviour`); the typed language is being grown to replace the old one, with several branches not on main yet. Lua (`lua`, 13.3) runs on our own interpreter and binds to the same engine API table as the typed language.

## 13. Scripting

### 13.1 OkayScript VM
- **System:** OkayScript - The default script language: small text files that make objects do things, with its own code editor, debugger and profiler.
  Home: `engine/include/okay/Scripting/`, `engine/src/Scripting/`. Entry: `engine/include/okay/Scripting/OkayScriptVM.hpp`. Also called: OkayScriptVM, ScriptVM, .okayscript, scripts, Lua-style script.
- **What:** The default scripting language. A script file is a component: fields are Inspector settings, `Start()` and `Update()` (or `start()`, `update(dt)`) run by name; C#-style and C-style syntax both parse; hundreds of built-in functions drive the scene.
- **Parts:**
  - 13.1.1 Lexer, parser and runtime - `okayscript/` (13.8), run by `Scripting/OkayScriptVM` - the language core is the engine-independent library; the VM instantiates it for the engine's value and adds scene names, object references, components and coroutines through `EngineHooks`
  - 13.1.2 C#-style syntax - `Scripting/OkayScriptVM` - typed fields, `[Header]`/`[Range]`, enums, switch, `$"..."`, lists and dictionaries, lambdas and LINQ-style helpers, `??`, `?.`, classes with `new`, try/catch/throw
  - 13.1.3 Coroutines and waits - `Scripting/OkayScriptVM` - `IEnumerator`, `yield return`, `WaitForSeconds`, `WaitUntil`, `WaitWhile`, StartCoroutine/StopCoroutine
  - 13.1.4 Event handlers - `Scripting/OkayScriptVM` - on_click, on_collision, OnTriggerEnter, OnMouseDown, OnCollisionEnter/Stay/Exit and the lifecycle (Awake, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy)
  - 13.1.5 Diagnostics - `Scripting/OkayScriptVM` - `Validate` and `ValidateAll` with line and column, "does nothing" warnings for pasted C# that has no effect
  - 13.1.6 Host builtins - `Scripting/OkayScriptVM` - `AddBuiltin(name, fn)` lets an embedder (editor plugins, tests) add its own functions
  - 13.1.7 Script safety - `Scripting/OkayScriptVM` - file names passed to save, prefs, data, build and scene calls must stay inside the game folders, no absolute paths or `..`
- **Code:** `Scripting/OkayScriptVM`, `Scripting/ScriptVM`
- **API:** `okay::OkayScriptVM`, `CreateScriptVM("okayscript")`, `IScriptVM::Load/Validate/ValidateAll/CallStart/CallUpdate/CallEvent/HasEvent/GetGlobal/SetGlobal`, `OkayScriptVM::AddBuiltin`
- **Editor:** Script Editor (13.5); Inspector header "Script (name)" with the script's public fields; Console shows runtime errors
- **Docs:** `docs/scripting/scripting.md`, `docs/scripting/scripting.md` (the old `SCRIPTING.md` named in the earlier map no longer exists)
- **Tests:** `scripting`, `scriptapi`, `script_semantics`, `script_lang2`, `script_lang3`, `script_events`, `script_arrays`, `script_maps`, `script_debug`, `script_paths`, `okayscript_csharp`, `csharp_style_script`
- **Status:** Works - default language, used by every sample and by editor plugins; broad test coverage across syntax and builtins
- **Open:** possible doc conflict: `docs/scripting/scripting.md` line ~144 says own classes are not supported, while the "Classes and new" section documents them; one 10.5k-line file (splitting is listed in 19.4); the earlier map named a "scripting bug list 1-15": ask before assuming any of it is still open
- **Related:** 13.2, 13.4, 13.5, 13.7, 19.4
- **Words:** OkayScript, okayscript, script language, start update, function, variable, coroutine, yield, lambda, LINQ, C# style, event handler, lifecycle, VM
- **Seen as:** a script does nothing or errors at runtime; "unknown function"; a field does not show in the Inspector; Start or Update never runs; a pasted C# script is accepted but has no effect; a coroutine stops early
- **Ask me like:** "work on 13.1.3, coroutine bugs"; "script says unknown function, which part is that"

### 13.2 C#-style language core
- **System:** Typed OkayScript - The newer C#-style typed script language and the table of engine calls it can use - duplicate of OkayScript (two engine binding tables; a merge is planned on branch `claude2/lua-csharp-merge`, no PR yet)
  Home: `engine/src/Script/`, `engine/include/okay/Scripting/NextScriptVM.hpp`. Entry: `engine/include/okay/Scripting/NextScriptVM.hpp`. Also called: NextScript, NextScriptVM, C#-style script, typed script, EngineApi. Fix: PR #1159 (`claude2/one-script-api`) makes one binding table; then `NextScriptVM` gets a plain name.
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
- **Docs:** `docs/scripting/scripting.md` (partly describes both languages), `docs/engine_api.md`
- **Tests:** `okayscript_next`, `okayscript_next_csharp`
- **Status:** Partial - on main: classes, enums, switch, arrays, foreach, statics, interpolation, `??` and `?.`, List and Dictionary, lambdas; not on main: try/catch, coroutines, events, inheritance and interfaces
- **Open:** in flight (branches, no PR, 2026-10-26): claude/csharp-p1-events, -inheritance, -try, -coroutines, -getcomponent, claude/next-event-args; `AvailableScriptLanguages()` lacks `okayscript-next` so New Script cannot pick it (possible gap); replacing the old VM is not done
- **Related:** 13.1, 13.6, 13.5, 14.7
- **Words:** okayscript-next, typed OkayScript, new OkayScript, class Behaviour, checker, interpreter, C# parity, lambdas, LINQ, statics, NextScriptVM
- **Seen as:** a script is rejected with "isn't supported yet"; a new feature works in one language and not the other; "A script needs a class"; errors point at the wrong column
- **Ask me like:** "work on 13.2.2, add try/catch to the typed language"; "which branch has coroutines for the new OkayScript"

### 13.3 Lua scripts (our own interpreter) and the removed C# (Mono) backend
- **System:** Lua scripting - Lua 5.4 scripts on game objects, run by our own interpreter okay::lua Home: `engine/src/Lua/`, `engine/include/okay/Lua/` Entry: `engine/include/okay/Lua/Lua.hpp` Doc: `docs/scripting/lua.md` Also called: okay::lua, LuaScriptVM, .lua
- **What:** `.lua` scripts (language `lua`) run on okay::lua, our own Lua 5.4 interpreter written from the Lua 5.4 Reference Manual (no Lua C source, no port). A script returns a table: its fields are Inspector settings and `Start`, `Update(dt)`, `OnTriggerEnter(other)` and the other lifecycle names run by name (old `function update(dt)` scripts work too). The engine API is the typed-language binding table (13.6), the same one the C#-style language and the visual-script Engine API nodes use. The third-party Lua and Mono backends stay removed; a scene naming `csharp` or `cs` still loads and says "no longer supported".
- **Parts:**
  - 13.3.1 Compiler - `engine/src/Lua/LuaCompiler.cpp` - lexer, one-pass parser and code generator to our own stack bytecode (manual sections 3 and 9): locals, `<const>` and `<close>`, multiple assignment and results, varargs, closures and upvalues, numeric and generic for, goto and labels, integer and float literals, string escapes, long strings and comments; syntax errors with line and column, several per check
  - 13.3.2 Runtime - `engine/src/Lua/LuaState.cpp`, `engine/include/okay/Lua/Lua.hpp`, `engine/src/Lua/LuaCode.hpp` - values, tables (array part + hash part), metatables and metamethods (Lua metamethods and pcall/xpcall run as loop frames, so coroutines yield inside them), weak tables, `__gc` finalizers, integer/float arithmetic rules, mark and sweep collector, coroutines without threads (an explicit frame stack), errors with chunk:line, instruction budget and memory limit (endless loops stop; pcall can not catch the stop)
  - 13.3.3 Libraries - `engine/src/Lua/LuaLibBase.cpp`, `LuaLibString.cpp`, `LuaLibTable.cpp`, `LuaLibUtf8.cpp` - sandboxed base, coroutine, string (with patterns, format, pack/unpack/packsize), table, utf8, math (xoshiro256** random) and a safe os (clock, time, date, difftime); no io, os.execute, require, loadlib or debug
  - 13.3.4 Engine glue - `engine/src/Lua/LuaScriptVM.cpp`, `engine/include/okay/Lua/LuaScriptVM.hpp` - `IScriptVM` for `lua`: engine objects as userdata (an object id, so a destroyed object is an error, not a crash), Vector3 with operators, static types as global tables (Input, Time, Debug, Mathf, GameObject, Vector3, ...), `GetComponent("Rigidbody")`, `Destroy(obj)`, events get the other GameObject, Inspector settings from the returned table, per-callback budget
  - 13.3.5 Script Editor support - `editor/main.cpp` (`LuaLangDef`, `ScriptFieldsFor`) - Lua colours, New Script / language picker gives the Lua template (`StarterScriptSource("lua")`), errors with line and column, warnings for engine names that do not exist and misspelled handlers (`LuaScriptVM::Lint`), autocomplete of Lua words and the bound API including `obj:` members, hover docs with the bound signatures, Format Document by Lua blocks, Inspector settings from the returned table; dev screenshot hook `OKAY_UI_SCRIPT=open|lua|luaerr`
  - 13.3.6 Removed-language message - `Scripting/ScriptVM` - `RemovedScriptLanguageMessage(lang)` gives the clear text for `csharp` and `cs`; used by `ScriptComponent::LoadSource` and the scene validator
- **Code:** `engine/src/Lua/*`, `engine/include/okay/Lua/*`, `Scripting/ScriptVM`, `Components/ScriptComponent`
- **API:** `CreateScriptVM("lua")`, `LuaScriptVM` (`kLanguage`, `SetBudget`, `CompletionNames`, `CompletionMembers`, `CompletionDoc`, `Lint`, `Settings`, `Format`), `okay::lua::State` (Load, PCallHost, DoString, CheckSyntax, SetBudget, memoryLimit), `RemovedScriptLanguageMessage()`, `AvailableScriptLanguages()` (`okayscript`, `lua`)
- **Editor:** Script Editor language picker and New Script offer Lua; `.lua` files open in the Script Editor with Lua colours; Inspector shows the script table's fields; example scene `examples/lua/lua_scripts.okayscene`
- **Docs:** `docs/scripting/lua.md` (supported language and library, binding rules, what each file was written from), `docs/scripting/scripting.md`
- **Tests:** `lua_core` (standalone: conformance script `tests/lua/conformance.lua` written from the manual, syntax errors, budget, memory limit, host API, mutation fuzz; clean under ASan and UBSan), `lua_script` (game scripts driving GameObjects), `script_removed_langs`, `starter_script`, `examples_work`
- **Status:** Partial - language, coroutines and libraries work and are tested (string.pack/unpack/packsize, `__gc` finalizers for tables, yielding inside pcall, xpcall and Lua metamethods); not in Lua: finalizers at State close and for userdata, full ephemeron weak keys, yielding across a metamethod a library function calls (`__tostring`, `__pairs`, sort comparators), `__close`, method lookup or a 3+ value `__concat` (a clear error), binary chunks; the Script Editor has no Lua debugger
- **Open:** the Lua parts above; a Lua visual-script node (UNFINISHED "text-script nodes"); editor build and screenshots unverified on Windows
- **Related:** 13.4, 13.5, 13.6, 14.7
- **Words:** Lua, lua script, .lua, okay::lua, Lua 5.4, coroutine, metatable, pcall, C#, Mono, csharp, removed language, language not supported, old project, script language dropdown
- **Seen as:** a Lua script errors with "script:12: ..."; a Lua script stops with "instruction budget exceeded"; a Lua setting is missing in the Inspector; an old C# script does nothing and the Console says the language is no longer supported
- **Ask me like:** "work on 13.3.2, add weak tables to Lua"; "why does my Lua script say yield across a C-call boundary"

### 13.4 Script components and script UI
- **System:** OkayScript
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
- **Docs:** `docs/scripting/scripting.md`, `docs/editor/editor.md`
- **Tests:** `script_components`, `script_ui`, `starter_script`, `scripting`
- **Status:** Works - used by every script; starter script test loads it in the real engine
- **Open:** `Scripting/ScriptUI` bridge has only an OkayUI implementation (in `shared/`), so a build without OkayUI draws nothing for `ui_*` calls
- **Related:** 13.1, 13.5, 40.4, 12.13
- **Words:** ScriptComponent, Script component, script field, public variable, Inspector field, starter script, new script, ScriptUIBridge, IScriptVM, ScriptHost
- **Seen as:** a public field is missing or reset in the Inspector; a new script starts with the wrong template; a script edit does not reload; `ui_button` draws nothing
- **Ask me like:** "work on 13.4.3, show enum fields in the Inspector"; "new scripts should start with a class"

### 13.5 Script editor: autocomplete, errors, rename, format
- **System:** OkayScript
- **What:** The built-in code editor (View > Editors & Tools > Script Editor): tabs, syntax colours, inline errors and warnings, autocomplete with docs, outline, rename, find in files, formatting and an external-IDE bridge.
- **Parts:**
  - 13.5.1 Editor window and tabs - `editor/main.cpp` (`DrawScriptEditor` ~12930-15670) - one tab per Script component, float or dock, File menu (New, Save, Reload, Switch Script); since #577 a larger code face and a collapsible script list (Alt+1)
  - 13.5.2 Text widget and colours - our own `OkayUI::CodeEdit` (`okayui/src/OkayUICodeEdit.cpp`, adapter `editor/ScriptEditorCodeEdit.*`, `editor/ScriptEditorWidget.hpp`), palette and language words in `editor/ScriptAssist.cpp` (`OkayScriptPalette`, `OkayScriptLanguageWords`), overlays in `editor/main.cpp` - syntax highlighting, gutter, hover docs on built-ins, changed-line bars, minimap, zoom. The vendored ImGuiColorTextEdit (`editor/vendor/`) is deleted (`docs/ui/own_ui_code_editor.md` step 31)
  - 13.5.3 Problems and inline errors - `editor/main.cpp` (~76-230 `AnalyzeScriptWarnings`, Problems panel ~15460) - ValidateAll diagnostics, did-you-mean lint, click-to-fix gutter action
  - 13.5.4 Autocomplete and signature help - `editor/ScriptAssist.*` (completion words, signatures, docs, C#-aware member completion, details with code examples), `editor/main.cpp` (`DrawScriptEditor`: floating `##autocomplete`, `##sighelp`, `##sighint`) - fed by `BuiltinNames()` and the typed Engine API
  - 13.5.5 Navigation - `editor/ScriptText.hpp` - tokenizer, outline, Go to Symbol, Go to Definition, Find All References, bookmarks, Rename Symbol (pure text, covered by selftest)
  - 13.5.6 Find, replace and Find in Files - `editor/main.cpp` (Find and replace bar ~13930-13990 with Replace and Replace All since #577, `editor/FindInFiles.*` `DrawFindInFiles`) - Ctrl+F, Ctrl+Shift+F across project scripts
  - 13.5.7 Edit tools - `editor/main.cpp` - Format Document, Toggle Comment, duplicate and move line, auto-close brackets, Command Palette, Go to Line
  - 13.5.8 External IDE - `editor/main.cpp` (`namespace extide` ~3359-3432) - Open in External IDE, Live Sync of outside edits, conflict banner
  - 13.5.9 Insert menu - `editor/main.cpp` - C# snippets, Behaviour Template, API Command picker
  - 13.5.10 Scripting Reference window - `editor/ScriptAssist.cpp` (`DrawScriptDocs`) - the function reference inside the editor (Help > Scripting Reference)
- **Code:** `editor/main.cpp`, `editor/ScriptText.hpp`, `editor/vendor/TextEditor.*` (vendored, out of scope)
- **API:** menu File / Edit / View / Go / Insert / Help inside the Script Editor; shortcuts Ctrl+F, Ctrl+Shift+F, Ctrl+/, Ctrl+D, Alt+Up/Down
- **Editor:** View > Editors & Tools > Script Editor; Help > Scripting Reference; Layout preset "Scripting"
- **Docs:** `docs/editor/editor.md` (Script Editor), `docs/scripting/scripting.md`
- **Tests:** `editor` (selftest covers ScriptText outline, identifier search and the lint), `starter_script`; the own code widget: `okayui_codeedit`, `okayui_codeedit_fuzz`, `okayui_script_adapter`, `okayui_host`, `okayui_capture`, `okayui_codeedit_imgui`
- **Status:** Works - features are present as menu items and code per the editor source; no separate unit tests except the selftest
- **Open:** earlier map: "meaningful syntax only; classes and templates for New Script" (open); no step debugger for text scripts (only the visual-script debugger, 14.5); the 2.7k-line window lives in `main.cpp` (15.10). Script compile errors and missing script files are also rows in the project Problems panel (15.11.8), separate from this window's own Problems list
- **Related:** 13.1, 13.2, 13.4, 15.10, 15.8
- **Words:** script editor, code editor, autocomplete, red squiggles, problems, rename symbol, format document, find in files, go to definition, outline, snippets, external IDE, VS Code, live sync, syntax highlighting
- **Seen as:** red squiggles are wrong or missing; autocomplete does not offer a function; Rename misses a usage; Format changes code; a save in the external IDE does not reload; typing lags in a big script
- **Ask me like:** "work on 13.5.4, show signature help for typed scripts"; "autocomplete should offer GetComponent"

### 13.6 Engine API and generated docs
- **System:** Typed OkayScript
- **What:** One table of bindings (every type, property and method a typed script can use), backed by real engine calls. The checker resolves names against it, the editor autocompletes from it, the visual-script Engine API nodes are generated from it, and `docs/engine_api.md` is printed from it.
- **Parts:**
  - 13.6.1 Binding table - `engine/src/Script/EngineApi.cpp` - GameObject, Transform, Rigidbody, Input, Time, Debug, Audio, SaveGame, Localization, Sequence, post-process, NavMeshAgent, Dialogue, Quest, BehaviorTree, Spline, UI components and more
  - 13.6.2 IK and Animator script tools - `Script/AnimIkTools` - shared by OkayScript builtins and the typed API so one IK kind name means the same everywhere (foot, look, aim, hand_l, hand_r, full)
  - 13.6.3 Generated reference - `docs/engine_api.md` - regenerate with `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` from the build folder; the `vs_types` test fails while it is stale
  - 13.6.4 Shared script-function table - `Scripting/ScriptApiRegistry`, `engine/src/Scripting/ScriptApiCore.cpp` - one entry per engine function that OkayScript (by snake_case name) and the typed language (as Type.Member, so also the Engine API nodes) both read; first batch: keys, actions, forces, whole-game saves, audio groups, time ([guide](../scripting/script_api_registry.md))
- **Code:** `engine/src/Script/EngineApi.cpp`, `Scripting/ScriptApiRegistry`, `Scripting/ScriptApiCore.cpp`, `Script/AnimIkTools`, `VisualScript/VsApi`
- **API:** `OKAY_WRITE_API_DOCS=1`, `VsApiReference()`, `ParseIkKind`
- **Editor:** Script Editor autocomplete list; visual-script palette "Engine API" nodes (14.7)
- **Docs:** `docs/engine_api.md` (generated, do not edit by hand)
- **Tests:** `vs_types` (checks the generated page), `script_api_parity` (every shared entry from both languages), `okayscript_next`, `vs_acceptance` (Engine API nodes end to end), `api_expand` (script API additions, exercised with an animation clip)
- **Status:** Works - the page is generated from the table and guarded by a test
- **Open:** animation and IK script tools are Grok's area (5.6, 5.7): do not edit them here
- **Related:** 13.2, 14.7, 5.6
- **Words:** engine API, binding table, EngineApi, ScriptApiRegistry, shared script table, add_torque, API reference, autocomplete list, IK script tools, generated docs, engine_api.md
- **Seen as:** a member autocompletes but fails to run; engine_api.md differs from the code; an API node is missing from the palette
- **Ask me like:** "work on 13.6.1, add Camera shake to the typed API"; "regenerate engine_api.md"

### 13.7 Built-in function library
- **System:** OkayScript
- **What:** The several hundred built-in functions of the old VM, grouped as in `docs/scripting/scripting.md`: movement one-liners, transforms, rigidbody, input, objects and scene, UI, tweens, saves, scene manager, networking, character animation, Steam, collections, JSON, timers, spawning and physics queries.
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
- **API:** `BuiltinNames()` lists every function; reference tables in `docs/scripting/scripting.md`
- **Editor:** Help > Scripting Reference; Script Editor autocomplete; Insert > API Command
- **Docs:** `docs/scripting/scripting.md`, `docs/scripting/scripting.md`
- **Tests:** `script_spawn`, `script_physics`, `script_events`, `script_util`, `script_lib`, `script_camera`, `script_fx`, `script_objects`, `script_tilemap`, `script_net`, `script_anim`, `script_batch1`, `script_nav`, `script_paths`, `steam_script`, `account_script`, `anim_script_control`, `inventory_script`
- **Status:** Works - large and tested by area; each test file maps to one slice of the table
- **Open:** the same calls are re-implemented for the typed language in `EngineApi.cpp`: the two tables can drift (possible duplicated system, see 13.2)
- **Related:** 13.1, 13.6, 7, 9, 16, 17.10, 11.1
- **Words:** builtin, built-in function, scripting reference, walk, spin, follow, tween_move, prefs_get, save_game, raycast, net_send, steam_unlock, nav_path, music_play, ui_set_text
- **Seen as:** "unknown function"; a function behaves differently than the reference says; a function exists in OkayScript but not in a graph node or the typed API
- **Ask me like:** "work on 13.7.6, add a capsule cast"; "list the built-ins for camera shake"

### 13.8 Standalone OkayScript language and CLI
- **System:** OkayScript Home: `okayscript/`
- **What:** The whole OkayScript language (lexer, parser, syntax tree, interpreter core) as a library that needs only the C++17 standard library, plus the `okayscript` command-line program that runs `.okay` files. The interpreter core is a template over the value type: the CLI and embedders run it on `okayscript::Value`, the engine's OkayScriptVM runs the same code on `vs::VsValue`.
- **Parts:**
  - 13.8.1 Tokens, lexer, errors - `okayscript/include/okayscript/Token.hpp`, `Lexer.hpp`, `Error.hpp` - SyntaxError and RuntimeError with line and column
  - 13.8.2 Syntax tree and parser - `okayscript/include/okayscript/Ast.hpp`, `Parser.hpp` - C#-style and C-style syntax, classes, enums, yield, events; `ValidateSource` lists every problem; `Dump()` prints the tree's shape
  - 13.8.3 Interpreter core - `okayscript/include/okayscript/Runtime.hpp`, `detail/*.ipp` - scopes, calls, classes, statics, events, lambdas, vector maths; call-depth and loop limits; `BasicHost` hooks for the embedder's names, objects and coroutines
  - 13.8.4 Values - `okayscript/include/okayscript/Value.hpp` - the standalone value: 32-bit numbers (the engine's), text, vectors, colours, handles, lists, maps
  - 13.8.5 Native functions - `okayscript/include/okayscript/Native.hpp`, `Interpreter.hpp` - `Define(name, fn, Arity)` with arity and argument-type errors on the calling line; raw `builtins` for the engine's commands
  - 13.8.6 Shared builtins and standard library - `okayscript/include/okayscript/CoreBuiltins.hpp`, `Stdlib.hpp` - the maths and conversion builtins the engine and the CLI both register (same rules: missing arguments read 0, never an error); the CLI adds print, println, write, input, exit
  - 13.8.7 Command-line runner - `okayscript/cli/main.cpp` - `okayscript file.okay [args]`, `okayscript -e "code"`; runs the top level then `main()`; `--` before a file name; exit 0 / 1 runtime error or out of memory / 2 syntax error / 64 usage / 66 unreadable file, `exit(n)` clamped to 0..255; errors as `file:line: message`
  - 13.8.8 Bytecode compiler and stack machine - `okayscript/include/okayscript/Bytecode.hpp`, `src/Compile.cpp`, `detail/Vm.ipp` - the only way OkayScript runs (the tree walker is deleted): every function, lambda, top-level body, default argument and field starting value compiles to bytecode the first time it runs; a function's locals are frame slots (no scope map per call, scoping stays dynamic); variable and call caches, globals cached across calls, constant folding, fused compare-and-jump, try/catch handlers and switch in the machine (`docs/scripting/okayscript_vm.md`)
- **Code:** `okayscript/`
- **API:** `okayscript::Interpreter` (`Define`, `Run`, `Call`, `SetGlobal`, `GetGlobal`, `SetHost`, `GetLimits`), `okayscript::BasicInterpreter<V>`, `BasicRuntime<V>`, `BasicHost<V>`, `Args`, `Arity`, `InstallStandardLibrary`, `ParseSource`, `ValidateSource`
- **Editor:** none (the Script Editor reaches it through OkayScriptVM)
- **Docs:** `docs/scripting/scripting.md` ("Standalone OkayScript")
- **Tests:** `okayscript_lang_lexer`, `okayscript_lang_parser`, `okayscript_lang_interpreter`, `okayscript_lang_native`, `okayscript_lang_errors`, `okayscript_lang_classes`, `okayscript_lang_vm` (about 85 programs give the output, errors, lines and globals recorded from the tree walker before it was deleted, `okayscript/tests/vm_expected.txt`; listings), `okayscript_engine_value`, `okayscript_bench` (callgrind instruction counts of hot loops against `tests/okayscript_bench_baseline.txt`, fails at +10%; skips without valgrind), `okayscript_cli_*` (cases in `tests/okayscript_cli/`), `okayscript_no_engine`, and every engine script test (the engine runs this core)
- **Status:** Partial - works and is tested on Linux; Windows build unverified (MinGW syntax check and link only)
- **Open:** the standalone value repeats the engine `vs::VsValue` conversion rules (one value type for both is not done); the engine keeps runtime errors without a line (`Limits::runtimeErrorLines` off) to keep its messages as they were
- **Related:** 13.1, 13.7
- **Words:** okayscript cli, okayscript.exe, run .okay file, standalone script language, embed OkayScript, native function, register C++ function, okayscript_lang, interpreter library, BasicHost, script core
- **Seen as:** "okayscript: can't read"; a native says "takes 2 arguments, got 3"; a script error names the wrong line
- **Ask me like:** "work on 13.8.5, add a native"; "why does the CLI and the engine print a number differently"
