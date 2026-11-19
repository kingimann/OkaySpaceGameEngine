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
| 2 | Lua 5.4, written by us: `engine/src/Lua/*`, binding `okay::LuaScriptVM`, language `lua` (`.lua`), tests, starter, example | see status table |
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
