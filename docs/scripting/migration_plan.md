# Retiring the old OkayScript VM: the staged plan

Internal record. Owner request (2026-11-22): "start integrating C# and Lua into the scripting editor and slowly
removing our scripting language, not the visual script."

## What we have

| Piece | Code | Language id | State |
|---|---|---|---|
| Old OkayScript VM | `engine/src/Scripting/OkayScriptVM.cpp` (one ~10.5k-line file) | `okayscript` | Runs every existing script, sample and editor plugin. Several hundred built-in functions (13.7). |
| Typed language | `engine/src/Script/*` (lexer, parser, checker, interpreter), `NextScriptVM` | `okayscript-next` | Checked before it runs; binds to the engine through one table (`EngineApi.cpp`, 13.6) that the visual-script API nodes also use (14.7). |
| C# | the same typed language in C# mode | `csharp` (also `cs`, `.cs` files) | Since this branch: `using`, `namespace`, `MonoBehaviour`, C# modifiers, properties, `KeyCode`. New scripts are C# by default (a setting). |
| Lua | our own Lua (other agent, `claude2/lua-scripting`) | `lua` | Separate work; not covered here. |
| Visual scripts (OkayVS) | `engine/src/VisualScript/*`, `editor/VisualScriptEditor.hpp` | — | Not touched by this plan. Graphs keep running exactly as today. |

C# here is our own implementation, written from the ECMA-334 C# language specification and general
compiler techniques. No Mono, .NET, CoreCLR, Roslyn or other C# library is used or will be (CLAUDE.md 19a).

## Stages

Each stage is small, shipped on its own, and reversible until the last one. The old VM is deleted only in
stage 6, and only with the owner's approval (the `docs/own_code_roadmap.md` pattern: seam, new path, criteria,
flip the default with approval, delete one release later).

### Stage 1: C# is a first-class language (this branch, `claude2/csharp-scripting`)

- `.cs` and the ids `csharp` / `cs` run on the typed VM (`CreateScriptVM`, `AvailableScriptLanguages`,
  `ScriptLanguageForExtension`). Old scenes that named the removed Mono backend now load and run if the script
  compiles, and show the checker's error (scene validator `script.compile`) if it does not.
- Script Editor: New Script offers C# and OkayScript; C# is the default for new scripts only, behind the
  editor setting `newscriptcsharp` (okay_settings.txt; the "Language" box in the New Script dialog). Existing
  scripts keep their language. **Changing the default is the owner's decision**; the setting is the switch.
- Errors and warnings with line and column come from the C# checker; autocomplete for C# tabs comes from the
  binding table; the Scripting Reference has a C# section with the binding table's API list.
- Example: `examples/csharp/csharp_playground.okayscene` (three C# scripts).

### Stage 2: close the C# gaps that real game scripts hit

In order of how often Unity-style scripts need them (rule 24: a failing test first for each):

1. `Quaternion` (and `transform.rotation`), `Vector2`, `Color` (`Random.Range` / `value` / `InitState` done).
2. `struct` value types (copy semantics), then generic methods and classes of the script's own.
3. `delegate` declarations; `params`; default parameter values; named arguments.
4. `static` and `const` in plain classes; nested classes; `readonly` enforced.
5. Instantiate / prefabs, tags and layers, physics queries (`Physics.Raycast`), collision messages with
   `Collision` / `Collider` arguments, `OnTriggerEnter(Collider other)`.
6. Text: `ToString("F2")`, `string.Join`, `int.TryParse` (`int.Parse`, `float.Parse`, `Length`, `Substring`, `Split`, `ToString()` and friends done).
7. `get` / `set` on interfaces, `virtual` properties, indexers.

Each gap closed is listed in `docs/scripting/scripting.md` ("C# features") and feature-map row 13.2.

### Stage 3: the converter, old OkayScript to C#

A tool that turns an `.okay` script into a `.cs` file (it does not change the original):

- Engine: `script::ConvertOkayScriptToCSharp(source) -> {csharp, notes}` in `engine/src/Script/`, written
  over the old VM's own parser output (its syntax tree), never by text replacement.
  - Globals and `var` fields become typed fields (type inferred from the starting value; unknown types
    become a note and `float`).
  - `start()` / `update(dt)` become `void Start()` / `void Update()`; `dt` becomes `Time.deltaTime`.
  - Each built-in function call maps through a table to the binding-table call (`move(x, y)` to
    `transform.Translate(x * Time.deltaTime, ...)`, `key("w")` to `Input.GetKey(KeyCode.W)`, ...). A built-in
    with no mapping yet stays as a call to a `// TODO` method and is listed in the notes, so the file still
    compiles and the user sees what is left.
  - The table doubles as the to-do list for the binding table: a built-in that cannot be mapped is a missing
    API, added to `EngineApi.cpp` (and so to the visual-script nodes too).
- Editor: Script Editor "File > Convert to C#..." writes `Name.cs` beside `Name.okay`, opens it, shows the notes
  in the Problems list, and offers to switch the Script component to the new file.
- Tests: every `.okay` script in `examples/` and the starter templates converts, compiles in C# mode with no
  errors, and behaves the same in a short headless run (positions, logged lines) as the original.

### Stage 4: warnings, nothing breaks

- Loading an `okayscript` script logs one info line per script per session: "OkayScript (old) is being retired:
  File > Convert to C# makes a C# copy". No error, no change in behaviour.
- Project diagnostics count old scripts; the scene validator gets an info-level `script.legacy` code.
- New Script no longer offers OkayScript unless the setting asks for it (owner approval needed, as in stage 1).
- Editor plugins (`examples/editor-plugins`, `.okay` plugin folders) keep running on the old VM until the
  plugin API exists for C#; this is the longest tail and is tracked separately.

### Stage 5: criteria to retire the old VM

All of these, checked by tests or by the release checklist:

- Every shipped example, template and sample is C# (converted, reviewed, tests green).
- The converter handles every built-in function in `BuiltinNames()` (mapped or explicitly "no equivalent",
  with the reason), and its test suite passes.
- Editor plugins have a C# path.
- One release has shipped with the stage 4 warning and no open "my old script broke" reports.

### Stage 6: delete (only with the owner's approval)

- Remove `OkayScriptVM` and its tests one release after the owner approves; `okayscript` scripts then load with
  a clear message (the same mechanism as `RemovedScriptLanguageMessage` today) and the converter stays, so an
  old project can still be converted.

## Visual scripting

Graphs (OkayVS) stay. Two things change for them:

- **14.8 "Graph to OkayScript" preview gets a C# target.** `CompileToOkayScript` in
  `editor/VisualScriptEditor.hpp` becomes one of two back ends of one graph walk: a C# writer prints the same
  graph as a `class Name : MonoBehaviour` with the event nodes as lifecycle methods (`On Start` to `Start()`,
  `On Update` to `Update()`, `On Key` to an `if (Input.GetKeyDown(KeyCode.X))` in `Update`), Wait / Delay as
  coroutines, and the Engine API nodes (14.7) as the binding-table calls they already name (each node already
  knows "how it reads in code"). The preview gets a "C# / OkayScript" switch, C# first. Test: the exported
  C# of each `vs_editor_model` export case compiles in C# mode and matches the graph's runtime result, the way
  the OkayScript exports are checked today.
- **14.8.3 "OkayScript to Actions"** keeps reading OkayScript until stage 6; a C# reader for simple calls follows
  the converter (stage 3), sharing its call table.
- Visual scripts never depend on the old VM at run time: the graph runtime is separate. Deleting the old VM must
  not change any graph's behaviour; the `vs_*` tests are the guard.

## Risks

- Two script tables drift (13.7 Open): stage 3's mapping table is the cure, since every unmapped built-in becomes
  a binding-table entry.
- C# users expect more of the language than we have; every unsupported construct must keep giving a clear error
  with its line, never silently doing nothing.
- The default language for new scripts is a user-facing change: it is a setting, and flipping it in releases is
  the owner's call.
