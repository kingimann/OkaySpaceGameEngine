# 14. Visual scripting

Node graphs and no-code rules: a graph of nodes with execution wires (flow) and data wires (values), the graph editor, a pin-type checker, a breakpoint debugger, and the older Actions list (trigger, conditions, steps) that now runs inside the same graph.
Covers `engine/include/okay/VisualScript/*` (about 3.8k header lines) and `engine/src/VisualScript/*` (about 2.4k), `Components/VisualScriptComponent`, `Components/ActionList`, `Components/NoCode`, the 3.6k-line `editor/VisualScriptEditor.hpp` and the Actions editors inside `editor/main.cpp`.
Graphs are saved as `.okayvs` text (OkayVS); about 250 node types across eight node files plus generated Engine API nodes.

## 14. Visual scripting

### 14.1 Graph editor
- **What:** The Visual Script window (View > Editors & Tools > Visual Script): a zoomable canvas where nodes are placed from a palette, wired, commented and run live, with the runtime graph and the OkayVS file format behind it.
- **Parts:**
  - 14.1.1 Canvas - `editor/VisualScriptEditor.hpp` - zoom, pan, grid, box select, comment frames, minimap, Fit, drag wires
  - 14.1.2 Node palette and registry - `editor/VisualScriptEditor.hpp` (`Registry()`), `editor/VisualScriptPalette.hpp` - every node type with category, pins and defaults; right-click, Space or Add node opens one search panel; engine API nodes appended
  - 14.1.3 Node help - `editor/VisualScriptNodeDocs.hpp`, `editor/VisualScriptNodeDocs.inc`, `docs/visual_scripting_nodes.md` - description, every pin (type, meaning, default), requirements, example and keywords for every registered node; the same text is the search details pane, the node tooltip and the generated page
  - 14.1.10 Graph tabs and save prompts - `editor/VisualScriptEditor.hpp` - several graphs open at once; a dirty graph asks Save / Cancel / Don't Save before a tab closes; New adds an Untitled tab; tabs restore from `okay_vs_tabs.txt` and a missing file is an error tab
  - 14.1.4 Toolbar, templates and shortcuts - `editor/VisualScriptEditor.hpp` - File/Edit/+ Add/Templates, events list (click to jump), problems list, key picker for key nodes
  - 14.1.5 Editable graph model - `editor/VisualScriptEditor.hpp` (`VsEditGraph`) - load, save, copy and paste, undo by gesture, layout coordinates, starter graph
  - 14.1.6 Runtime graph - `VisualScript/NodeGraph` - `NodeGraph::Parse`, `Run`, entries, exec and data links, suspended waits, `MakeNode` by type name
  - 14.1.7 Graph component - `Components/VisualScriptComponent` - attaches a graph to an object: OnStart, OnUpdate and event entries, LoadFromText, LoadFromFile, serialized with the scene
  - 14.1.8 Inspector hook - `editor/main.cpp` (Visual Script header ~28850) - graph file path, Open in node editor, Reload; inspector values can be dropped onto a graph
  - 14.1.9 Outline, snap and align - `editor/VisualScriptEditor.hpp`, `editor/VsCanvasTools.hpp` - (#577) a left outline lists the graph's events, variables (grouped by scope) and parameters; a moved node snaps to a 20 px grid on release (the "Snap to grid" menu item); align Left, Right, Top or Bottom for the selected nodes; a hint line on the canvas when a graph has nodes but no event ("Nothing runs yet"); the helpers (`VsSnap`, `VsAlignNodes`, `VsCollectVariables`, `VsCollectParameters`) are pure data, covered by the editor selftest, and nothing new is written into the `.okayvs` file
- **Code:** `editor/VisualScriptEditor.hpp`, `editor/VisualScriptPalette.hpp`, `editor/VisualScriptNodeDocs.hpp`, `editor/VsCanvasTools.hpp`, `editor/VisualScriptNodeHelp.hpp`, `VisualScript/NodeGraph`, `Components/VisualScriptComponent`
- **API:** `vs::NodeGraph::Add/ConnectExec/ConnectData/Parse`, `VisualScriptComponent::SetGraph/LoadFromText/LoadFromFile`; file directives `node`, `exec`, `data`, `entry`
- **Editor:** View > Editors & Tools > Visual Script; Inspector header "Visual Script"; Project panel `.okayvs` files
- **Docs:** `docs/visual_scripting.md` (OkayVS format, Flow Graph editor, working faster, palette index); `docs/visual_scripting_nodes.md` (every node, generated from the editor's doc table)
- **Tests:** `visualscript`, `vs_editor_model` (graph model without drawing, tab dirty state, fuzzy search), `vs_node_docs` (every registered node is documented), `vs_nodes`, `vs_pin_defaults`, `editor` (selftest: snap, align, variable outline, and a saved graph reopening with the same nodes)
- **Status:** Works - editor model, text format and runtime are covered; `vs_editor_model` checks save then load stays identical
- **Open:** earlier map: docs update (the doc still has a retired "Flow Graph editor" section next to "One graph editor"); possible bug area: node definitions exist in the editor registry and in the engine pin table (`VsTypes`), so a new node must be added in both
- **Related:** 14.2, 14.4, 14.7, 15.2, 15.10
- **Words:** visual script, node editor, node graph, flow graph, OkayVS, .okayvs, VisualScriptComponent, palette, wires, comment frame, minimap, node browser
- **Seen as:** nodes missing from the palette; a wire is refused; the graph does not run; saved graph loads differently; the canvas is slow with many nodes; copy and paste invents wires
- **Ask me like:** "work on 14.1.1, node snapping on the canvas"; "my graph saved and now a wire is missing"

### 14.2 Typed pins, variables and scopes, object references
- **What:** Pin types (number, text, bool, vector, object...) with allowed wire conversions, a graph checker that runs before Play and before a build, values that carry object references by id, and variables at graph, object, scene and global scope.
- **Parts:**
  - 14.2.1 Pin type table - `VisualScript/VsTypes` - `PinType`, `NodeSignature` per node type, `PinCompatible`, conversion hints; the editor colours pins and refuses wires from it
  - 14.2.2 Graph checker - `VisualScript/VsTypes` - `ValidateGraphText`, `ValidateSceneGraphs`: wires that cannot work, nodes nothing runs, unset variables, bad key names, missing prefabs; a bad wire sets `VsIssue::pin` so the editor paints that pin red, and the same issues are rows in Problems (15.11.8). `AddNodeRequirement` lets the node browser report a component a node needs
  - 14.2.3 Values - `VisualScript/VsValue` - one value type; `VsObject` holds an instance id, never a pointer
  - 14.2.4 Scene and global variables - `VisualScript/VsVariables` - scene variables go with the scene, globals survive loading another scene
  - 14.2.5 Function signatures - `VisualScript/VsTypes` - `FunctionSig` parsing for Function / Call / Return nodes
  - 14.2.6 Declared variables and structs - `VisualScript/VsDecl`, `editor/VsVariablePanel.hpp` - `decl` and `struct` lines: typed variables (Number, Bool, Text, Vector3, Color, Object, `List<T>`, `Map<T>`, structs) at object, scene or global scope with editable defaults; Set nodes fit or refuse a value; value literals; the Variables panel and the Inspector edit the defaults
- **Code:** `VisualScript/VsTypes`, `VisualScript/VsValue`, `VisualScript/VsVariables`, `VisualScript/VsDecl`, `editor/VsVariablePanel.hpp`, `editor/EditorState.*`
- **API:** `vs::PinType`, `vs::SignatureFor`, `vs::ValidateGraphText`, `vs::ValidateSceneGraphs`, `vs::VsValue`, `vs::VsObject`
- **Editor:** pin colours and wire refusal in the graph editor; problems list in the toolbar; Play logs graph errors and warnings to the Console (`EditorState::Play`); Build Settings stops the build on graph errors and offers a retry
- **Docs:** `docs/visual_scripting.md` ("Pin types and the graph checker", "Scene and global variables", "Declared variables, lists, maps and structs")
- **Tests:** `vs_types`, `vs_pin_defaults`, `vs_acceptance`, `vs_variables` (literals, declarations, typing, copy versus reference, checker, save and scene round trips, hostile input)
- **Status:** Works - table and checker are on main and exercised by `vs_types`; `vs_acceptance` saves and reloads real scenes
- **Open:** none found; object variables and SyncVars are covered in 14.4
- **Related:** 14.1, 14.3, 15.7, 1.5
- **Words:** pin types, typed pins, wire colours, graph checker, validation, variables, scene variable, global variable, object reference, VsValue, signature
- **Seen as:** "this wire can't work"; a number pin refuses text; a variable is empty after loading another scene; the Console lists Visual Script errors when Play starts; a build stops with "visual script error(s) - see Build Settings"
- **Ask me like:** "work on 14.2.2, warn about unused variables"; "why was my wire refused"

### 14.3 Functions, loops, control flow
- **What:** Structure for graphs: reusable functions with typed arguments and return values, While and For Each loops with Break and Continue, lists, Branch, Sequence, Once, Repeat, Wait and timers.
- **Parts:**
  - 14.3.1 Functions - `VisualScript/NodesFunctions` - Function start (signature in its text field), Return, Call Function, local variables for one call
  - 14.3.2 Loops - `VisualScript/NodesLoops` - While, For Each, Break and Continue (loop jump)
  - 14.3.3 Lists, maps and structs - `VisualScript/NodesLoops`, `VisualScript/NodesLogic` - New List, Add, Insert, Set, Get, Find, Remove At / Item, Sort, Reverse, Clear, Copy, Length, Contains; maps with Clear, Count, Copy; New Struct / Get Field / Set Field; Make / Split Color; all objects with a tag
  - 14.3.4 Core flow nodes - `VisualScript/Nodes` - Branch, Sequence, Once, Repeat, Toggle, Flip Flop, Timer, Wait, Select, Chance
  - 14.3.5 Event entries - `VisualScript/Nodes`, `VisualScript/NodesWave4` - On Start, On Update, On Key, timers, custom events, collision and mouse entries
  - 14.3.6 Typed events and dispatchers - `VisualScript/NodesEvents`, `VisualScript/VsEvents`, `VisualScript/VsDecl`, `editor/VsVariablePanel.hpp` - `event` and `dispatcher` lines with typed parameters; Raise Event, On Event Raised, Bind / Unbind / Call Dispatcher across objects; bindings dropped when either object or the scene goes; a 32-deep raise cap; handlers show in the debugger with who raised them; the sidebar's Custom events section
- **Code:** `VisualScript/NodesFunctions`, `VisualScript/NodesLoops`, `VisualScript/Nodes`, `VisualScript/NodesEvents`, `VisualScript/VsEvents`
- **API:** node types `Function`, `Return`, `CallFunction`, `While`, `ForEach`, `Break`, `Continue`, `Branch`, `Sequence`, `OnCustomEvent`, `RaiseEvent`, `CallDispatcher`, `BindEvent`, `UnbindEvent`; `vs::VsEventBindings`, `vs::NodeGraph::Events`, `VisualScriptComponent::RunTypedEvent`
- **Editor:** palette categories Flow, Events, Variables; Function node signature field; sidebar Custom events (declare, rename, add / retype / remove parameters, add handler and raise nodes); event pickers on the nodes
- **Docs:** `docs/visual_scripting.md` ("Functions", "Loops, lists and variable scopes", "Custom events with typed parameters, and dispatchers")
- **Tests:** `visualscript`, `vs_acceptance`, `vs_debug` (stepping into and out of functions), `vs_wave4`, `vs_events` (declarations, typed delivery, ordering, bind / unbind, cleanup, reentrancy, Delay in a handler, debugger frames, checker, round trips, fuzz), `vs_editor_model` (event declarations in the editor)
- **Status:** Works - implemented and covered, including stepping through function calls
- **Open:** none found
- **Related:** 14.4, 14.5, 14.2
- **Words:** function node, call function, return, while loop, for each, break, continue, branch, if, sequence, list, local variable, timer, wait, typed event, event parameters, raise event, dispatcher, delegate, bind event, unbind event, event handler
- **Seen as:** a loop never ends or runs once; a function returns nothing; a Wait node never resumes; a branch goes the wrong way
- **Ask me like:** "work on 14.3.1, recursion limit for function nodes"; "my For Each runs forever"

### 14.4 Node library (physics, story, build, math)
- **What:** The ready-made nodes: math and logic, vectors, text, input, transform, objects, physics casts, dialogue and quests, build mode, inventory, save and load, networking, Steam, audio, camera and particles.
- **Parts:**
  - 14.4.1 Core nodes - `VisualScript/Nodes` - events, constants, math, compare, logic, input, transform, spawn, Wait, networking and Steam nodes
  - 14.4.2 Second wave - `VisualScript/NodesExtra` - more math (remap, smooth step), vectors, text, object queries, mouse buttons, scene and Animator actions, IK, music, camera and particle nodes
  - 14.4.3 Fourth wave - `VisualScript/NodesWave4` - custom events between graphs, object variables, save and load values, text and math helpers, game-control and anti-cheat reject nodes
  - 14.4.4 Physics nodes - `VisualScript/NodesPhysics` - 3D raycast, sphere cast, cast all, overlap, contact info
  - 14.4.5 Story nodes - `VisualScript/NodesStory` - Start Dialogue, story variables, quests (events raised by the Dialogue Runner and Quest Log)
  - 14.4.6 Build nodes - `VisualScript/NodesBuild` - enter and leave build mode, palette, save and load creations
  - 14.4.7 Inventory nodes - `VisualScript/NodesInventory` - Add, Remove, Has, Count, Equip, Craft, Loot Roll, Spawn Pickup
  - 14.4.8 Action nodes - `VisualScript/NodesActions` - Action, Actions block, Condition, Delay (see 14.6)
- **Code:** `VisualScript/Nodes`, `VisualScript/NodesExtra`, `VisualScript/NodesWave4`, `VisualScript/NodesPhysics`, `VisualScript/NodesStory`, `VisualScript/NodesBuild`, `VisualScript/NodesInventory`, `VisualScript/NodesActions`
- **API:** node type names in `.okayvs` files; palette index in `docs/visual_scripting.md`
- **Editor:** graph editor palette (categories Events, Flow, Variables, Math, Logic, Input, Transform, Actions, Data, Multiplayer & Steam)
- **Docs:** `docs/visual_scripting.md` ("More nodes", "Third wave", "Fourth wave", "Physics", "Build mode nodes", "Palette index")
- **Tests:** `vs_nodes`, `vs_wave4`, `vs_inventory`, `vs_media`, `vs_acceptance`, `visualscript`
- **Status:** Works - eight node files with tests per wave; media nodes arrived with #566 and inventory nodes with #562 per git history
- **Open:** custom nodes written by users (plugin area 24.3) do not exist; the earlier plan item "On Value Changed, On Hover, Set Progress, Focus, Create Widget" for UI is not present (12.13)
- **Related:** 14.1, 14.7, 11.1, 10.2, 11.5, 16, 17.10, 24
- **Words:** nodes, node library, math nodes, raycast node, dialogue node, quest node, build node, inventory node, music node, camera node, particle node, custom event, object variable
- **Seen as:** a node I expect is missing; a node does nothing at runtime; a node is in the docs but not in the palette; pins differ between the palette and the file
- **Ask me like:** "work on 14.4.4, add a capsule cast node"; "add a node for UI progress"

### 14.5 Debugger (breakpoints, step, call stack, watch)
- **What:** Stop a running graph at a node: breakpoints saved by graph file and node id, Step Into / Over / Out, the frozen game with the call stack, arguments and locals shown, and a live view of which nodes ran and their last values.
- **Parts:**
  - 14.5.1 Debugger core - `VisualScript/VsDebug` - breakpoints, `RequestStep`, `onStop` callback, survives graph reload
  - 14.5.2 Game fiber - `editor/GameFiber.hpp` - runs the game tick on its own stack so a breakpoint can freeze the frame while the editor keeps drawing
  - 14.5.3 Editor debug bar - `editor/VisualScriptEditor.hpp` - stop bar with Step buttons, Continue, live exec counts and node values on the canvas
  - 14.5.4 Variables watch - `editor/main.cpp` (`DrawVarWatch` ~2252) - live visual-script variables window
  - 14.5.5 Editing while paused - `editor/VisualScriptEditor.hpp`, `editor/main.cpp` (~1793-1810) - you can save while paused; the running copy finishes the paused event with the old graph, the next event uses the saved one (per docs)
- **Code:** `VisualScript/VsDebug`, `editor/GameFiber.hpp`, `editor/VisualScriptEditor.hpp`, `editor/main.cpp`
- **API:** `vs::VsDebugger::Get()`, `SetBreakpoint`, `RequestStep(Step::Into/Over/Out)`, `onStop`
- **Editor:** breakpoint toggle on nodes, debug bar in the Visual Script window, View > Panels > Variables (watch)
- **Docs:** `docs/visual_scripting.md` ("Debugging: breakpoints, stepping and the watch panel")
- **Tests:** `vs_debug`, `game_fiber`
- **Status:** Works - engine half and fiber have tests; the drawing side is not unit-tested
- **Open:** the debugger covers graphs only: text scripts have no breakpoints (13.5); fiber debugging is used only while a breakpoint is set
- **Related:** 14.1, 13.5, 15.5
- **Words:** debugger, breakpoint, step into, step over, step out, call stack, watch, variables window, game fiber, frozen game
- **Seen as:** a breakpoint does not stop; the game freezes and the editor hangs; step goes to the wrong node; watch shows stale values; graph edits lost after continuing
- **Ask me like:** "work on 14.5.3, conditional breakpoints"; "the game froze at a breakpoint and will not resume"

### 14.6 Action framework
- **What:** No-code rules: an Actions list (trigger, optional conditions, steps run top to bottom with Wait) authored from dropdowns, ready-made no-code gameplay components, and the conversion of an Actions list into a graph so one editor covers both.
- **Parts:**
  - 14.6.1 Actions component - `Components/ActionList` - triggers (start, update, key, collision, click, trigger, mouse), conditions, instructions, extra handlers
  - 14.6.2 Actions to graph - `VisualScript/ActionGraph` - an older Actions component becomes an `.okayvs` graph that behaves the same
  - 14.6.3 Actions step library as nodes - `VisualScript/NodesActions` - Action, Actions, Condition, Delay nodes running a private list
  - 14.6.4 Actions editor widgets - `editor/main.cpp` (`DrawSimpleRules` ~22984, step catalogs `kCondOps` / `kInstrOps` ~18392-18838, `DrawActionItem` ~24541, pickers ~24195-24540) - WHEN / DO rule editor and the Inspector Actions card
  - 14.6.5 Custom Actions library - `editor/main.cpp` (~2115-2406) - reusable action templates saved globally
  - 14.6.6 No-code gameplay components - `Components/NoCode` - Collectible, Damage On Touch, Teleporter, Jump Pad, Trigger Zone
  - 14.6.7 Retired Flow Graph window - `editor/main.cpp` (`DrawFlowGraph` ~23127-24191) - kept in the file, never called
- **Code:** `Components/ActionList`, `VisualScript/ActionGraph`, `VisualScript/NodesActions`, `Components/NoCode`, `editor/main.cpp`
- **API:** `ActionList::Trigger`, `ActionList::Item`, `vs::ActionListToGraph`, `vs::ActionTriggerToEvent`; Collectible, DamageOnTouch, Teleporter, JumpPad, TriggerZone components
- **Editor:** Inspector "Actions" card, View > Editors & Tools > Custom Actions, the graph editor's Action / Actions nodes
- **Docs:** `docs/visual_scripting.md` ("ActionList - no-code triggers", "One graph editor (Actions merged in)")
- **Tests:** `actionlist`, `action_graph`
- **Status:** Works - ActionList and the graph conversion have tests
- **Open:** possible dead code: `DrawFlowGraph` is marked retired and has no caller (about 1,060 lines); `LogicSource` / `LogicTarget` in `Components/LogicLinks` belong to build mode (11.5)
- **Related:** 14.1, 14.8, 11.5, 12.2
- **Words:** actions, action list, ActionList, no code, trigger, condition, instruction, Game Creator, custom actions, Collectible, Jump Pad, Teleporter, Trigger Zone
- **Seen as:** an Actions list does nothing; steps run in the wrong order; Wait skips; an old object with Actions behaves differently in the graph; the Flow Graph window cannot be opened
- **Ask me like:** "work on 14.6.1, add a Timer trigger"; "remove the retired Flow Graph code"

### 14.7 Engine API nodes
- **What:** Nodes generated from the typed-language binding table, so everything added to the table once shows up as a node too: `Transform.position` (get), `Transform.position=` (set step), method calls as steps or value nodes, with an optional Target object pin.
- **Parts:**
  - 14.7.1 Generated entries - `VisualScript/VsApi` - `VsApiEntries`, `VsApiFind`, `MakeApiNode`: node type, title, doc, how it reads in code, pins
  - 14.7.2 Literal checks - `VisualScript/VsApi` - `VsApiCheckLiteral` validates typed-in key and axis names
  - 14.7.3 Reference page - `VisualScript/VsApi` - `VsApiReference()` prints `docs/engine_api.md`
- **Code:** `VisualScript/VsApi`, `engine/src/Script/EngineApi.cpp`
- **API:** `vs::VsApiEntries()`, node types such as `Transform.Translate/3`, `Rigidbody.AddForce/1`, `Mathf.Clamp/3`
- **Editor:** palette group "Engine API" (appended by `VsWithApiDefs` in the graph editor)
- **Docs:** `docs/visual_scripting.md` ("Engine API nodes"), `docs/engine_api.md`
- **Tests:** `vs_acceptance` (API nodes end to end), `vs_types`, `vs_pin_defaults`
- **Status:** Works - one binding table feeds scripts, nodes and docs
- **Open:** the binding table is a different list from the old VM's built-ins (13.7)
- **Related:** 13.6, 13.2, 14.4
- **Words:** engine API nodes, generated nodes, VsApi, Transform node, Mathf node, API palette
- **Seen as:** an API member exists in the script but not as a node; a generated node's pins differ from the table
- **Ask me like:** "work on 14.7.1, group API nodes by component in the palette"

### 14.8 Converting between graphs and scripts
- **What:** Moving between the two ways of scripting: the graph editor shows a live OkayScript version of the graph, an Actions list can be turned into OkayScript, and simple OkayScript can be turned back into Actions.
- **Parts:**
  - 14.8.1 Graph to OkayScript - `editor/VisualScriptEditor.hpp` (`CompileToOkayScript`) - live preview beside the canvas, Copy to clipboard
  - 14.8.2 Actions to OkayScript - `editor/main.cpp` (~18839-18974) - readable script from an Actions list, unmapped ops become TODO comments
  - 14.8.3 OkayScript to Actions - `editor/main.cpp` (~18975-19134) - best-effort import of simple calls
  - 14.8.4 Script recipes - `editor/main.cpp` (~19135-19491) - one-click starter handlers ("Jump on Space")
- **Code:** `editor/VisualScriptEditor.hpp`, `editor/main.cpp`
- **API:** menu items in the graph editor (OkayScript preview, Copy); Inspector Actions card conversion buttons
- **Editor:** Visual Script window preview panel; Inspector Actions card
- **Docs:** `docs/visual_scripting.md`
- **Tests:** `vs_editor_model` (model), no tests of the generators found
- **Status:** Basic - best-effort generators with no dedicated tests
- **Open:** two separate generators with different inputs (graph versus Actions list) in two files; neither is tested for equivalence with the runtime
- **Related:** 14.1, 14.6, 13.1
- **Words:** convert graph to script, OkayScript preview, export to code, import script, recipes
- **Seen as:** the generated script behaves differently from the graph; a TODO comment appears in generated code; import loses a step
- **Ask me like:** "work on 14.8.1, make the preview round-trip back into the graph"
