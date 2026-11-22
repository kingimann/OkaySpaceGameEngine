# 13. Scripting

Text scripting for games and for the editor: the default OkayScript language (one big VM with several hundred built-in functions), a newer typed C#-style language that is checked before it runs, the Script component that runs them, and the Script Editor.
Covers `engine/include/okay/Scripting/*`, `engine/include/okay/Script/*`, `engine/src/Scripting/*`, `engine/src/Script/*` (about 11.3k + 7.1k lines), `Components/ScriptComponent`, `docs/scripting*.md` and the Script Editor inside `editor/main.cpp` (about 3k lines plus completion and docs windows).
Two OkayScript languages exist side by side: id `okayscript` (old VM) and `okayscript-next` (typed, `class Name : Behaviour`); the typed language also runs C# (id `csharp` / `cs`, `.cs` files, C# mode) and is being grown to replace the old one (plan: `docs/scripting/migration_plan.md`).

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
- **Docs:** `docs/scripting/scripting.md`, `docs/scripting/scripting.md` (the old `SCRIPTING.md` named in the earlier map no longer exists)
- **Tests:** `scripting`, `scriptapi`, `script_semantics`, `script_lang2`, `script_lang3`, `script_events`, `script_arrays`, `script_maps`, `script_debug`, `script_paths`, `okayscript_csharp`, `csharp_style_script`
- **Status:** Works - default language, used by every sample and by editor plugins; broad test coverage across syntax and builtins
- **Open:** possible doc conflict: `docs/scripting/scripting.md` line ~144 says own classes are not supported, while the "Classes and new" section documents them; one 10.5k-line file (splitting is listed in 19.4); the earlier map named a "scripting bug list 1-15": ask before assuming any of it is still open
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
- **Docs:** `docs/scripting/scripting.md` (partly describes both languages), `docs/engine_api.md`
- **Tests:** `okayscript_next`, `okayscript_next_csharp`, `csharp_scripts`
- **Status:** Partial - classes, enums, switch, arrays, foreach, statics, interpolation, `??` and `?.`, List and Dictionary, lambdas, events (`+=` and `-=` on functions, methods as functions), interfaces, inheritance, `is` / `as` and casts, try / catch / finally with throw and Exception, coroutines, scripts using each other and `GetComponent<MyScript>()` (the five claude/csharp-* branches merged on claude2/csharp-scripting); properties (auto, `=>`, get / set code) and `=>` method bodies; C# mode (13.8)
- **Open:** missing C#: struct, Quaternion, own generics, delegate declarations, static / const in plain classes (list and order in `docs/scripting/migration_plan.md` stage 2); claude/next-event-args does not exist on origin; replacing the old VM is not done (migration plan stages 3-6)
- **Related:** 13.1, 13.6, 13.5, 14.7
- **Words:** okayscript-next, typed OkayScript, new OkayScript, class Behaviour, checker, interpreter, C# parity, lambdas, LINQ, statics, NextScriptVM
- **Seen as:** a script is rejected with "isn't supported yet"; a new feature works in one language and not the other; "A script needs a class"; errors point at the wrong column
- **Ask me like:** "work on 13.2.2, add try/catch to the typed language"; "which branch has coroutines for the new OkayScript"

### 13.3 Removed Lua and C# (Mono) backends
- **What:** The optional third-party Lua and Mono C# backends were removed (our own scripting languages replace them). An old scene or project whose script names language `lua` still loads; the script just does not run, and loading it reports "script language 'lua' is no longer supported" (Console, scene validator, project diagnostics). `csharp` / `cs` are no longer removed: they run on our own typed language (13.8) if the script compiles, else the checker's error is shown.
- **Parts:**
  - 13.3.1 Removed-language message - `Scripting/ScriptVM` - `RemovedScriptLanguageMessage(lang)` gives the clear text for `lua`; used by `ScriptComponent::LoadSource` and the scene validator
- **Code:** `Scripting/ScriptVM`, `Components/ScriptComponent`
- **API:** `CreateScriptVM(lang)` (returns null for `lua`), `RemovedScriptLanguageMessage()`, `AvailableScriptLanguages()`
- **Editor:** `.lua` files still show as scripts in the Project Browser; opening one loads it with the "no longer supported" message (`.cs` files open as C#, 13.8)
- **Docs:** `docs/scripting/scripting.md`
- **Tests:** `script_removed_langs`
- **Status:** Works
- **Open:** our own Lua is separate work (claude2/lua-scripting); nothing converts old Lua scripts
- **Related:** 13.4, 13.5
- **Words:** Lua, C#, Mono, mcs, csharp, lua backend, removed language, language not supported, old project, script language dropdown
- **Seen as:** a Lua or C# script does nothing; the Console says the script language is no longer supported; an old scene loads but its script is silent
- **Ask me like:** "why does my old Lua script not run"

### 13.4 Script components and script UI
- **What:** The glue that puts a script on an object: the language-neutral VM interface, the Script component with its source and public fields, starter scripts, and the bridge that lets scripts draw immediate-mode UI.
- **Parts:**
  - 13.4.1 VM interface and factory - `Scripting/ScriptVM` - `IScriptVM`, `ScriptHost`, `ScriptDiagnostic`, `CreateScriptVM`, `AvailableScriptLanguages`
  - 13.4.2 Script component - `Components/ScriptComponent` - language, source or file path, Reload, lifecycle forwarding, serialized per-instance field overrides
  - 13.4.3 Public fields in the Inspector - `editor/main.cpp` (`ParseScriptFields` ~25567-25708, Script header ~31140) - reads `public` fields and `[Header]`, `[Tooltip]`, `[Range]` from the source
  - 13.4.4 Starter scripts - `Scripting/StarterScripts` - what a new empty script starts as, per language (C#: `public class NewScript : MonoBehaviour` with Start and Update)
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
- **Docs:** `docs/editor/editor.md` (Script Editor), `docs/scripting/scripting.md`
- **Tests:** `editor` (selftest covers ScriptText outline, identifier search and the lint), `starter_script`
- **Status:** Works - features are present as menu items and code per the editor source; no separate unit tests except the selftest
- **Open:** New Script offers C# (default for new scripts, setting `newscriptcsharp`, the dialog's Language box) and OkayScript; C# tabs get errors from the C# checker and autocomplete from the binding table, and the Scripting Reference has a C# section (all unverified in a real Windows editor; see 13.8); signature help and member completion are still fed by the old VM's tables; no step debugger for text scripts (only the visual-script debugger, 14.5); the 2.7k-line window lives in `main.cpp` (15.10). Script compile errors and missing script files are also rows in the project Problems panel (15.11.8), separate from this window's own Problems list
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

### 13.8 C# scripts on our own typed language
- **What:** `.cs` files and the language ids `csharp` / `cs` run on the typed language (13.2) in C# mode, so a Unity-style C# script (using lines, a namespace, `public class X : MonoBehaviour`, `[SerializeField] private`, properties, `KeyCode.W`) runs unchanged. Our own C#, written from the ECMA-334 specification; no Mono, .NET or other C# library. New scripts are C# by default (a setting).
- **Parts:**
  - 13.8.1 Language ids and file extension - `Scripting/ScriptVM` - `CreateScriptVM("csharp"/"cs")`, `AvailableScriptLanguages()` lists `csharp`, `ScriptLanguageForExtension(".cs")`
  - 13.8.2 C# mode - `engine/src/Script/Parser.cpp`, `Checker.cpp` (`Program::csharp`) - using directives, namespaces (block and file-scoped), MonoBehaviour, private / readonly / sealed / internal, public const, unknown attributes as warnings
  - 13.8.3 Properties and => bodies - `engine/src/Script/Parser.cpp` (`Property`, `ArrowBody`), `Checker.cpp` (`PropGetOwn`, `PropSet`) - auto-properties are fields; code properties are get_X / set_X methods the checker calls
  - 13.8.4 KeyCode - `engine/src/Script/EngineApi.cpp` - static string properties (A-Z, Alpha0-9, Space, Return, Escape, Tab, Backspace, shifts, controls, arrows)
  - 13.8.8 Random - `engine/src/Script/EngineApi.cpp` - `Random.Range` (int, max left out; float), `Random.value`, `Random.InitState` over `okay::Random::Shared()`
  - 13.8.10 Text members - `EngineApi.cpp` (a script-only `string` type, `ApiType::scriptOnly`), `Checker.cpp`, `Lexer.cpp` (character literals as one-letter text) - Length, ToUpper, ToLower, Trim, Contains, StartsWith, EndsWith, IndexOf, Substring, Replace, Split; ToString() on numbers, bool, enums, Vector3; `int.Parse`, `float.Parse`, `int/float.MaxValue/MinValue`, `string.IsNullOrEmpty`
  - 13.8.11 Default parameter values - `Parser.cpp` (`Params`), `Checker.cpp` (`CheckDefaults`, `ArgsFit` fills left-out arguments) - fixed values at the end of the list; not on constructors
  - 13.8.9 Collision messages - `Checker.cpp` (`CheckLifecycleSignature`), `NextScriptVM.cpp` (`CallEventArgs`), `Components/ScriptComponent` (`FireWithObject`), `EngineApi.cpp` (Collider, Collision, Collider2D, Collision2D types; `GameObject.CompareTag`) - `OnTriggerEnter(Collider other)` and the other five, plus 2D, private or public
  - 13.8.5 C# starter script - `Scripting/StarterScripts` - `using UnityEngine; public class NewScript : MonoBehaviour { Start, Update }`
  - 13.8.6 Editor - `editor/main.cpp` - New Script Language box (setting `newscriptcsharp`, C# default), `.cs` open / drop / attach as C#, C# checker diagnostics, `CSharpCompletions()` from the binding table, Scripting Reference "C# (new scripts)" section with the API list (`VsApiReference`)
  - 13.8.7 Example - `examples/csharp/` - `csharp_playground.okayscene` with PlayerMover.cs (move, hop coroutine, property), Spinner.cs, Scoreboard.cs (interface, List, foreach, switch, GetComponent<PlayerMover>()); shipped by `scripts/build-windows-dist.sh`
- **Code:** `Scripting/ScriptVM`, `Scripting/NextScriptVM`, `engine/src/Script/*`, `Scripting/StarterScripts`, `editor/main.cpp`
- **API:** `NextScriptVM::kCSharpLanguage`, `NextScriptVM(const std::string& language)`, `IsCSharpId`, `IsTypedId`, `ScriptLanguageForExtension`, `script::Compile(src, csharp)`
- **Editor:** Script Editor > File > New Script (Language: C# / OkayScript (old)); Project > Create > Script; drag a `.cs` onto an object; Help > Scripting Reference > C# (new scripts)
- **Docs:** `docs/scripting/scripting.md` ("C# scripts (.cs)", "Properties and => bodies"), `docs/scripting/migration_plan.md`
- **Tests:** `csharp_scripts`, `script_removed_langs`, `starter_script`, `okayscript_next_csharp`
- **Status:** Partial - the language core runs C# files; the gaps of 13.2 apply
- **Open:** making C# the default for new scripts is the owner's call (setting in place, default C#); editor parts not seen in a real Windows editor; signature help / member lists are the old VM's; converter from old OkayScript to C# and the C# target of the graph preview (14.8) are planned in `docs/scripting/migration_plan.md`
- **Related:** 13.2, 13.3, 13.5, 13.6, 14.8
- **Words:** C#, csharp, .cs, Unity script, MonoBehaviour, using, namespace, SerializeField, property, get set, KeyCode, new script language, default language
- **Seen as:** a `.cs` script reports an error; `using` or `namespace` is rejected; New Script makes the wrong kind of file; an old C# scene's script does not run
- **Ask me like:** "work on 13.8, add Quaternion for C# scripts"; "make new scripts OkayScript again"
