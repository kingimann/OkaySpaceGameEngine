# Scripting deep dive

Read against `origin/main` at `f599d99e` (OkayScript VM, visual-script graph, script editor, ActionLists). This is the findings report before new syntax and nodes. Claude's open script PRs are left alone: #444 (lexer-accurate comment skipping) and #445 (undo of Format, Rename, Replace All, Reload).

## How a script runs

OkayScript source is lexed, parsed, and run in `engine/src/Scripting/OkayScriptVM.cpp`. A `ScriptComponent` loads that source (`engine/src/Components/ScriptComponent.cpp`) and the scene calls Start / Update / events while `runScripts` is on. Built-ins are registered in the same VM file and reached from gameplay (input, physics, UI, animation, audio, save, network) by name.

The script editor lives in `editor/main.cpp` with `editor/ScriptText.hpp`. Highlighting, autocomplete, go-to-definition, find/replace, hover text, and the Problems list are already there. Squiggles use the line from `ScriptDiagnostic`. The column field on that diagnostic is not filled.

Visual scripts are text graphs. `NodeGraph::MakeNode` (`engine/src/VisualScript/NodeGraph.cpp`) builds the runtime node. The editor palette is `Registry()` in `editor/VisualScriptEditor.hpp`. Play runs the graph from `VisualScriptComponent`. `VsDebug` already steps visual-script graphs. ActionLists (`engine/src/Components/ActionList.cpp`) are the trigger / condition / instruction lists; some visual-script nodes embed those steps.

## Bugs

Severity is what a person hits while making a game.

| Sev | Where | What goes wrong |
|---|---|---|
| High | `docs/scripting.md:901-918` vs `OkayScriptVM.cpp` (no `??` or `?.` token) | The doc's null fallback and null-conditional do not parse. `??` and `?.` are described with a working-looking sample. |
| High | `docs/scripting.md:922-955` vs `OkayScriptVM.cpp:1788-1796` and `:1852-1863` | "Plain classes" and `new Item("Potion")` are documented. The parser strips `class` / `struct` and treats the methods as the component. The editor warning says the class wrapper does nothing. |
| High | `docs/SCRIPTING.md:115` vs `engine/src/Input/Input.cpp:246-252` | The API table says `axis_x()` / `axis_y()` are "WASD / arrow-key". `Input::AxisWASD` reads only W, A, S, and D. Arrow keys do not move that axis. |
| High | `editor/main.cpp:14702-14711` and `editor/EditorState.cpp:699-724` | Ctrl+P / Play does not compile the open script buffer. `LoadSource` runs from the editor's Run item (`editor/main.cpp:13056`), not from Play. A script changed in the editor and not run or saved is not what Play executes. |
| Medium | `OkayScriptVM.cpp:334` and `:372` | An unterminated string throws `ScriptError` with no line, so the Problems list cannot point at it. |
| Medium | `OkayScriptVM.cpp:8271` (comment) and `ScriptDiagnostic` | Diagnostics carry a column and the editor does not get one. Squiggles are line-only. |
| Medium | `engine/src/Components/ActionList.cpp:58-60` | `ActionList::Vars()` is one process-global map. `EditorState::Play` resets it (`EditorState.cpp:704`). Two scenes or two lists share the same names. |
| Medium | `docs/visual_scripting.md` vs `editor/VisualScriptEditor.hpp` `Registry()` | The doc lists a subset of the nodes the palette actually has. A node can exist and still be invisible in the written guide. |

Not a crash. The class and `??` gaps fail at edit time or do the wrong thing. Play-without-reload fails quietly: the old script keeps running.

## What already works

Confirmed by reading the editor and VM, not by a new game.

- Syntax highlighting, autocomplete, hover text, outline, go-to-definition (`editor/ScriptText.hpp`, `editor/main.cpp`).
- Find and Replace All.
- Problems list and line squiggles.
- Script templates / snippets in the editor.
- OkayScript already has string interpolation (`$"..."`, lexer around `OkayScriptVM.cpp:358`), `foreach`, lambdas, `switch`, enums (`ScanEnums` at `:1699`), and `after` / `every` timers plus coroutines.
- Visual-script palette categories, comments, copy/paste, live pin values, node help (`editor/VisualScriptNodeHelp.hpp`), and graph validation.
- Visual-script breakpoints and stepping (`VsDebug`). Text-script F11 bookmarks are not breakpoints.

## Missing against the scripting plan

| Asked for | On main today |
|---|---|
| `??`, `??=`, `?.` | Documented, not implemented |
| User classes / structs with `new` | Documented, stripped to one component |
| Default and named arguments | Not in the parser |
| `+=` event subscription | Not in the VM |
| Arrow keys on the documented axis | WASD only |
| Reroute nodes, subgraphs, pin-type search | Not in the visual-script editor |
| Drag a component or field onto the graph | Not wired |
| One-click recipes (door, pickup, health, chase) | Not in the visual editor or the script snippet list |
| Play keeps unsaved script text | Play does not reload the buffer |
| Fast reload during Play that keeps field values | Not on the Play path |

Already present, so the later PRs should not rebuild them: interpolation, foreach, lambdas, switch, enums, coroutines, one-liner builtins that already exist, palette categories, comments, copy/paste, node docs, visual-script stepping.

## Fix order

High, before new syntax:

1. On Play, compile every open OkayScript buffer into its component (`editor/EditorState.cpp`, `editor/main.cpp`).
2. Either implement `??` / `?.` or stop documenting them (`OkayScriptVM.cpp`, `docs/scripting.md`). The plan is to implement them, backward compatible.
3. Either implement plain classes or change the class section so it matches the strip-and-warn behavior. The plan is real classes.
4. Make `axis_x` / `axis_y` include arrow keys, or change `docs/SCRIPTING.md:115` to say WASD only. The plan is to include the arrows.

Medium, next:

5. Put the source line on every lexer `ScriptError`, including unterminated strings (`OkayScriptVM.cpp:334`, `:372`).
6. Fill `ScriptDiagnostic.column` from the token and show it on the squiggle.
7. Give each scene its own ActionList variables (`ActionList.cpp`).
8. Bring `docs/visual_scripting.md` in line with `Registry()`.

Then the queued syntax, node library, recipes, and example-game templates. Each of those gets its own small draft PR on `grok/script-*`, with a script or graph that actually runs.

## Not finished in this pass

QA of open `grok/*` pull requests is still ahead of feature work. Two of those PRs merge cleanly onto main and were not executed yet: #416 (`grok/bind-rest-pose`) and #426 (`grok/inv-ui-polish`). The comments on the other open grok PRs say what was run.
