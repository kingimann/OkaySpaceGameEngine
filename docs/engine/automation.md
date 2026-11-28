# Automation (design note)

Internal record of the Automation system: one table of commands that outside programs (scripts, tests, AI agents
through MCP) run to operate the engine and the editor. Owner's request of 2026-11-28 ("an Engine Automation API and
MCP server ... similar to how Blender exposes its internal functionality through its Python API").

## What it is

- `engine/include/okay/Automation/Automation.hpp` - the **Registry** of `Command`s (name such as `scene.hierarchy`,
  summary, parameters, result shape, error codes, flags, handler), the `Host` they act on, and the one call path
  `Registry::Run`: unknown names, parameters that do not fit the schema, editor-only commands on a host without the
  editor, edits while Play runs and C++ exceptions all become a structured `Error {code, message, details}`.
- Every command that changes the scene has the `Undoable` flag: `Run` wraps the handler in `Host::BeginEdit` /
  `EndEdit`, which on the editor host are `EditorState::PushUndo` / `CommitUndo`, so one command is one entry of
  the editor's own Undo history (visual script file edits ride along as `EditorState::AttachUndoFiles`).
- `Mcp.hpp` serves the table as MCP tools (JSON-RPC 2.0; initialize, ping, tools/list, tools/call; text, image and
  structured content). Tool names are command names with `.` as `_`.
- `Link.hpp` is how `okay mcp` reaches a running editor: 127.0.0.1 only, a fresh 32-byte token from the operating
  system each time, written with the port to a file only this user can read (`LinkInfoPath()`), off unless the person
  ticks View > Automation Link (remembered in the editor's per-user `automation_link.txt`). Commands run on the
  editor's main thread between frames (`oked::automation::LinkFrame`, polled once a frame, never waits).
- Hosts: `oked::automation::EditorHost` (`editor/AutomationEditor.hpp`) around an `EditorState`. The running editor
  uses its own; `okay mcp --headless` and `okay automation` make one with no window. So a command does the same thing
  in both, through the same editor code.

## Ops: every menu item a command (round 2, owner 2026-11-28)

Blender makes every UI action an operator (`bpy.ops`) that Python calls the way the UI does. Here the menus are data
(`MenuBarModel.hpp` and the models built on it), so `editor/AutomationOps.cpp` reads them and adds one `op.<item id>`
per Item and Toggle, plus `op.list` and `op.run`. Decisions:
- Same code as the click: the running editor installs its own menu functions in `Hooks()` (main.cpp's `RunMenuAction`,
  `HierRowAction`, `HierEmptyAction`, `RunUIMenuItem`, `SceneAddRun`, the toggles' flags and the states that grey items
  out). The scene actions those share were moved out of main.cpp into `editor/MenuActions.cpp`, which the headless host
  runs too. Everything else (windows, dialogs, UI builders) answers `unavailable` headless.
- A greyed-out item is refused with the menu's own reason; nothing is refused for Play by the table (the menu decides).
- A dialog never waits: its op says so, takes the parameter that does it without one (`path`, `name`, `discard`, by
  running the matching command) or answers `needs_ui`. `op.file.exit` is refused.
- An item that only repeats a richer command is an alias of it (`Registry::AddAlias`), not a second handler.
- The toolbar and the Scene view's tools are drawn by hand, not from a model, so they are not ops yet.

## script.run (round 2)

Like Blender MCP's run-Python: OkayScript, Lua or JavaScript inside the host, on the engine's own runtimes and their own
sandboxes (nothing is added to them but `automation_call`). Output is what the run logged; the value is Lua's and
JavaScript's top-level `return` or OkayScript's `result`; errors keep the script's own line numbers (the code added
around the script starts on its first line and follows its last); a budget stops a runaway (OkayScript's new
`okayscript::StepBudget` counts every loop turn and call, off for game scripts); one undo step (`EditorHost` opens an
undo group around a command). Safety: only through the agent-started stdio server and the token-protected editor link;
the editor writes "Automation ran a script (N lines, language)" in its Console; View > Automation Link's "Allow
scripts from Automation" is on by default (owner's yes) and remembered with the link setting.

## Connect to Claude (round 2)

`okay mcp` with no `--project` finds the open editor from its link file, so one Claude entry serves every project;
with none open it says so and ends (with `--project` it works headless). View > Automation Link opens the Connect to
Claude panel (`editor/AutomationPanel.cpp`): the switches, "Set up Claude Desktop" (`shared/ClaudeConnect.hpp`: adds or
updates only `mcpServers.okayspace`, keeps the rest, `.bak` first, refuses broken JSON, writes only on the click),
"Copy Claude Code command" and a status line.

## Where the commands live (each calls the system that already does the work)

| Group | File | Calls |
|---|---|---|
| automation, api, script, scene (info, hierarchy, find, validate), object.inspect, component, transform, physics, anim, viewport, log | `engine/src/Automation/EngineCommands.cpp` | ScriptApiRegistry, ScriptBindings (generated classes), PropertyRegistry, Scene, Physics3D/2D, Animator, AnimStateMachine, `testing::RenderSceneToImage` + `ImageWrite` |
| scene (new, open, save, state), object, editor.select, prefab, undo, play, model, ui | `editor/AutomationEditor.cpp` | EditorState, SceneSerializer, `easymodel::RunMeshTool`, `modeling::ApplyShapeOp`, `GetUIScreenRect`, PlayTest's input order |
| vs (node_types, inspect, create, add_node, connect, set_node, remove_node) | `editor/AutomationVs.cpp` | the Visual Script editor's model (`VsEditGraph`, pin types), `vs::ValidateGraphText` |
| project, scene.list/check, script.check, vs.list/check, asset, build, cli.run | `tools/okay-cli/AutomationCli.cpp` | `okaycli::Main` with `--json` (the okay command line's own code) |
| op.list, op.run, op.<menu item id> | `editor/AutomationOps.cpp` | the menu models, main.cpp's menu functions (`Hooks()`), `editor/MenuActions.cpp` |
| script.run | `engine/src/Automation/ScriptRun.cpp` | `CreateScriptVM` (OkayScriptVM, LuaScriptVM, JavaScriptVM), the shared script table |

`component.call`, `component.get/set` and `script.call` reach every bound class and method (the generated script
bindings, the same table OkayScript, C# and visual scripts use) and every Inspector property, so most of the engine
is reachable without a hand-written command.

## How to add a command

1. Write it next to the system it drives, as a `Command` with a plain summary, its `Param`s (types, defaults,
   options), the result shape in words, the error codes and the flags (`Undoable` for a scene edit).
2. Add it in that file's provider function (`detail::RegisterEngineCommands`, `AddEditorCommands`, ...), or for a new
   system call `okay::automation::AddProvider("system", &AddMyCommands)` from that system's code before the host
   starts. MCP, `okay automation list` and the tests pick it up from the table; nothing else lists commands.
3. Add a check to `tests/test_automation.cpp` that it reaches the real system.

## Not covered yet

See the feature map entry (41.15) "Open" line: it is the one list.
