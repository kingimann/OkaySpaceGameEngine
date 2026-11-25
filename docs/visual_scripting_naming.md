# Visual script node names

The name a person sees on a node is plain English. The short token stored in a
`.okayvs` file (`OnStart`, `Delay`) stays a single word so old files keep
loading. People search with either one.

This is the rule for every node, including nodes added after this page. The
check lives in `editor/VisualScriptNames.hpp` (`LintVsTitle`, `LintVsPin`) and
`tests/test_vs_names.cpp`.

## What people see

Title case. Words are separated by a single space. The first word starts with
a capital letter. Later small words stay lowercase: a, and, at, by, for, in,
is, of, on, or, the, to, up. Every other word is a full word with a capital
and then lowercase letters (`Seconds`, `Target`).

| Kind | Starts with | Examples |
|---|---|---|
| Event | `On` | On Start, On Every Frame, On Collision Start, On Key Pressed |
| Action (a step in the flow) | a verb | Play Sound, Wait Seconds, Run a Number of Times, Move Towards |
| Yes or no value | `Is` or `Has` | Is Touching, Is Key Held, Has Item |
| Reading a value that already exists | `Get` | Get Position, Get Name, Get Time |
| Working out a new value | a verb | Add, Subtract, Round Down, Make Vector |

Prefer the word a game maker already uses when it is clear. Collision, Trigger,
Enabled, Physics Update, Branch, Sequence, Cooldown, and For Loop are those
words. Do not invent a cuter or vaguer one: On Bump, On Steady Tick, and On
Entered Area are worse than On Collision Start, On Physics Update, and On
Trigger Enter. Stay and Exit stay in that family (On Collision Stay, On
Trigger Exit), not a new metaphor.

Do not use:

- Abbreviations: Var, Vec, Vec3, Pos, Rot, Deg, Rad, Anim, Nav, Rpc, Fov, Obj, Num, Idx, Min, Max, UI, API, IK, Id, Dt, int
- Engine type names as the title: Transform, Rigidbody, GameObject, Prefab, Quat, Euler
- Symbols, digits, or units in brackets: `+`, `/`, `%`, `Delay (s)`, `Every (s)`, `Switch (int)`, `Add (+)`
- Camel case: `MoveTowardsVec3`, `OverlapTest`

Put the unit in the words when it matters: `Wait Seconds`, pin `Seconds`.
Not `Wait (s)` and not a pin named `s`. `On Physics Update` is the steady
50-steps-a-second clock; the node help says that, the title does not say "tick".

Pins use the same words: `Seconds`, `Target`, `Speed`, `Amount`, `Name`,
`Value`, `Text`, `Point`, `Object`, `Previous`, `First`, `Last`, `Count`.
Not `s`, `t`, `dt`, `pos`, `obj`, `In`, `Out`, `Bool`, `Old`, `Var`.

An empty flow pin (the triangle with no word) is fine. Do not label it `Exec`.

## Topics

The Add Node menu (right-click the canvas, Space, Add node, or a wire dropped on empty space) puts every node in one
topic. There are **70 topics in 14 sections**. The sidebar shows the sections as small grey headings (click a heading to
fold it) and the topics under them, with how many nodes each holds. A topic that has more than 30 nodes keeps only the
everyday ones directly in it and sorts the rest into **subcategories**, which appear under the topic when you click it.
Common sections come first: the events, then flow, then variables, then math.

A node lives in exactly one place. Search still finds it by its name, by its topic, and by the topic it used to sit in
(see "Old names still work" below), so nobody has to know the tree to find a node.

There is no catch-all topic: not "Advanced", "Misc", "Other", "Data", "Engine API", "Actions", "Basic", "Look",
"World" or "Do something". `VsCatchAllCategory` in `editor/VisualScriptCategories.hpp` lists the banned names and
`tests/test_vs_categories.cpp` fails if a topic, a subcategory or a section uses one. (The old Advanced topic held 515
nodes, most of them text-script functions: weather, quests, navigation, behavior trees, accounts, display settings and
more. All of them now sit in the topic a beginner would look in.)

Events are special: every node that starts a flow (every "On ..." node) is in one of the five event topics at the top,
whatever it listens to. The events of the inventory, the quests and the network used to sit with their topic; they are
here now, so "where does my script start?" has one answer. The test checks it.

<!-- topics:start -->
| Section | Topic | What goes here | Subcategories |
|---|---|---|---|
| **Events** | Game Events | Where a script starts: the frame, being switched on or off, a message, a changed value, a death. |  |
|  | Input Events | When a key, a mouse button, a touch or a controller action happens. |  |
|  | Collision & Trigger Events | When things touch, enter an area or leave it, and what the touch was like. |  |
|  | Inventory & Quest Events | When an item is added or used, a quest moves on, or a conversation ends. |  |
|  | Network Events | When another player's message arrives, or the server catches a cheat. |  |
| **Flow** | Branches & Gates | Choose a path, run things in order, let the flow through only some of the time, and keep state machines. | State Machines |
|  | Loops | Do something again and again, for each item, or until something changes. |  |
|  | Waiting & Timers | Wait, repeat on a timer, cooldowns, stopwatches, and delayed calls from a script. | Delayed Calls |
|  | Functions & Calls | Define your own function, call another graph or script, or run a list of steps. |  |
|  | Time & Game Speed | The clock, the frame time, slow motion and pausing the game. | Clock, Game Speed, Pausing, Time Math |
| **Quick blocks** | Quick blocks | One-block actions: move around, game rules, objects in the game, show and sound, do in order. | Move Around, Game Rules, Objects In The Game, Show And Sound, Do In Order |
| **Variables, Lists & Saving** | Variables | Remember a number, a word or a yes/no on this object, the scene or the whole game. | Change a Variable, Check a Variable, Shared Variables |
|  | Saving & Loading | Keep things between runs: save slots, saved values and checkpoints. | Saved Values, Save Slots, Save Files |
|  | Lists | Several values in order under one name: add, remove, sort, search, and array functions. | Sorting & Searching, Array Functions |
|  | Maps & Structs | Values you look up by a name, records with fields, and JSON text. |  |
| **Math** | Numbers | Everyday math: arithmetic, rounding, limits, averages, percentages, bits, type checks and script math functions. | Rounding & Snapping, Limits & Wrapping, More Arithmetic, Averages & Percentages, Bits, Types & Checks, Script Math Functions |
|  | Blending & Easing | Mix two values, remap a range, and smooth steps with curves, waves and springs. | Easing & Curves, Smoothing & Springs |
|  | Angles & Trigonometry | Degrees and radians, sine and cosine, and the angle between two things. | Angle Helpers |
|  | Vectors | Points and directions: build, measure, add, scale, and the flat (ground) versions. | Directions & Axes, Flat Vectors, Script Vector Functions |
|  | Comparisons & Logic | Is it bigger, equal or between? And, or, not, picking a value, and comparing scores or number sets. | True & False Logic, Scores & Number Sets |
|  | Random | Dice rolls, random picks, chances and noise. |  |
| **Text** | Text | Join, cut, search, clean up, format and compare words and sentences, and use the clipboard. | Search & Replace, Pieces & Words, Clean Up & Padding, Compare & Check, Numbers & Text, Clipboard |
|  | Language & Translation | Show the game in the player's language and react when it changes. |  |
| **Objects** | Create & Destroy | Make new objects, remove them, and keep spawners and pools of them. | Spawners & Pools |
|  | Find Objects | Find objects by name, tag or distance, and read their names and tags. | Names & Tags |
|  | Show & Hide | Switch objects on and off, and show or hide what they draw. |  |
|  | Parents & Components | Parents and children, and the parts (components) an object has. | Components & Properties |
|  | Messages & Signals | Tell other objects that something happened. |  |
|  | Color & Appearance | Color, sprites, textures, materials and fading. | Sprites & Textures, Materials & Meshes |
|  | Particles & Trails | Particles, lines and trails. |  |
| **Movement & Physics** | Position | Where an object is, putting it somewhere else, and keeping it in bounds. | Read a Position, Stay in Bounds |
|  | Rotation & Scale | Turn an object, face or look at something, and change its size. | Facing & Looking, Scale |
|  | Move & Follow | Walk toward something, follow, chase, patrol, orbit, bob, spin, motion patterns, vehicles and script functions. | Motion Patterns, Vehicles, Script Movement Functions |
|  | Tweens | Smoothly change a position, size, color or number over time. | Start a Tween, Control Tweens, Tween Options, Tween Events, Tween Sequences |
|  | Paths & Splines | Curves and paths an object can follow. |  |
|  | Forces & Velocity | Push things, set their speed, jump, and set up their physics body. | Speed & Jumping, Body Settings |
|  | Raycasts & Overlaps | Shoot an invisible line or shape and see what it hits: ray results, overlaps and shape casts. | Ray Results, Overlaps & Shape Casts |
|  | Sight & Distance Checks | Is the target close, in front, behind, to the left or in view? |  |
| **Gameplay** | Health & Damage | Health, damage, healing, armor, lives, respawning, regeneration and resources. | Health Values, Armor & Lives, Resources & Buffs |
|  | Weapons & Shooting | Projectiles, shooting, swinging, reloading and ammo. |  |
|  | Inventory & Items | Pick up, carry, equip, craft and use items: equipment, slots, loot and limits. | Inventory Slots, Equipment, Crafting & Loot, Carry Limits, Pick Up & Drop |
|  | Quests & Dialogue | Quests and their objectives, conversations, and story flags. | Quests, Dialogue, Story Flags |
|  | Score & Winning | Score, experience and levels, winning, losing and restarting. |  |
|  | Doors & Switches | Doors, levers and wired logic signals. |  |
|  | Build Mode | Let players place and remove objects, and save what they built. |  |
| **Characters & AI** | Animation | Play and stop animation clips, and set animator parameters and blend shapes. | Animator Parameters, Blend Shapes & Trees |
|  | Character Actions | Roll, vault, climb, sit, die, stand and crouch, and ragdoll. | Stances & Ragdoll |
|  | Inverse Kinematics | Make hands reach, feet plant and heads look at things (IK). |  |
|  | Navigation | Walk to a point around obstacles, on a mesh or on a grid. | Grid Paths |
|  | Behavior Trees | Run a behavior tree, read and set its blackboard, and build outcomes. |  |
| **Sound, Camera & Screen** | Sound Effects | Play, stop and shape sounds on an object. |  |
|  | Music & Volume | Music tracks, the volume of each group, muting and ducking. |  |
|  | Camera | Where the player is looking: follow, switch, zoom, tilt and shake. | Follow & Switch, Zoom & Shake |
|  | Screen Text | Words and numbers drawn on the screen. | Text Style & Format, Numbers in Text, Script Text Widgets |
|  | Buttons & Inputs | Buttons, sliders, toggles, dropdowns, text fields and pickers. | Sliders & Toggles, Dropdowns & Text Fields, Pickers & Search |
|  | Bars & Layout | Progress bars, game HUD parts, app parts, tabs and pages, scroll areas and grids, layout helpers and themes. | Progress Bars, Game HUD, App Parts, Tabs & Pages, Scroll Areas & Grids, Layout Helpers, Themes |
|  | Popups & Dialogs | Toasts, tooltips, modals and bottom sheets. | Toasts & Tooltips, Modals & Sheets |
| **Scenes & Environment** | Scenes & Levels | Load, restart and quit scenes, and read which scene is open. |  |
|  | Lighting & Sky | Ambient light, background color and the lights in a scene. |  |
|  | Weather & Fog | Weather, wind and fog. |  |
|  | Post Effects | Full-screen looks: bloom, exposure, color grade and the like. |  |
|  | Window & Display | Screen size, display modes, refresh rate and the game window. |  |
|  | Cutscenes | Play a cutscene (a sequence) and build one from tweens. |  |
| **Input** | Keyboard | Is a key held, pressed or released. |  |
|  | Mouse & Touch | The mouse position and buttons, the cursor, taps and swipes. | Touch & Gestures |
|  | Gamepad & Controls | Movement axes, the gamepad, named actions and rebinding keys, local players and the controllers they use. | Named Actions, Local Players, Controllers |
| **Online** | Multiplayer | Host or join, rooms and invites, browse servers, matchmaking, messages, synced objects, players and matches. | Hosting & Joining, Online Rooms & Invites, Server Browser, Matchmaking & Relay, Messages & RPC, Synced Objects & Variables, Players & Matches |
|  | Steam | Steam achievements, stats, lobbies, friends, cloud files and the workshop. | Achievements & Stats, Lobbies & Friends, Cloud & Workshop |
|  | Server Security | Anti-cheat: validate what clients send, rate limits, kicks and budgets. |  |
|  | Accounts & Online Saves | Log in, cloud saves, leaderboards and PlayFab. | Accounts, Cloud Saves, Leaderboards & PlayFab |
| **Debugging** | Debug | Messages for you while building, not for the player. |  |
<!-- topics:end -->

The table above is printed by `test_vs_categories --topic-table`, and the test fails when it differs from the code, so it
is always the real list. The page with every node under its topic is [visual_scripting_topics.md](visual_scripting_topics.md);
where every node sat before and where it went is in [visual_script_category_audit.md](visual_script_category_audit.md).

Three subcategories hold "script functions" (Script Math Functions, Script Vector Functions, Script Movement Functions,
and Array Functions in Lists): the same job as a node next to them, written the way a text script calls it
(`Mathf.Abs`, `min`, `move_to`). They are there so the everyday nodes are not buried under their twins.

### Adding a node

1. **Pick the topic a beginner would look in**, using the table above. Use a subcategory only when the topic has one for
   it. If the node starts a flow, it goes in an event topic.
2. **Add one row** to the matching table in `editor/VisualScriptPlaceRules.hpp`. A row is `{pattern, topic, subcategory}`,
   the first row that matches wins, so a narrow row goes above a wide one. The tables are:
   - `kNodeRules`: the type stored in a `.okayvs` file (`Branch`, `OnKey`, `LbSquare`, `GlGetHealth`);
   - `kBuiltinRules`: a text-script function without the `Fn.` (`print`, `net_host`, `ui_button`);
   - `kApiRules`: `Type.member` of an engine-API node (`Transform.Translate`, `UIButton.Press`);
   - `kStepRules`: the short name of an Actions step (`set_var`, `play_sound`).
   A pattern is a whole name, or a name with a `*` at the start or the end (`OnKey*`, `tween_*`). A `*` in the middle
   is not supported.
3. **Generated library nodes** (`Lb...` from `tools/gen_logic_library.py`, `Gl...` from `tools/gen_gameplay_library.py`)
   still write an old-style place into `editor/generated/*_place.inc`. `kLegacyMoves` in the same file translates those old
   places (Objects / Survival becomes Health & Damage) so a new library node needs no row, unless you want it somewhere
   else: then add a row to `kNodeRules`. Do not edit the generated files.
4. **Run `test_vs_categories`.** It says what is wrong, node by node: no place, an unknown topic, an undeclared
   subcategory, a topic with more than 30 nodes directly in it, a subcategory with more than 45, a rule that matches
   nothing, a rule that an earlier row hides, an event outside the event topics, a vague name.
5. **A new topic is allowed and often right** (the menu prefers many specific topics to a few big ones). Add it to
   `kPaletteCategories` in `editor/VisualScriptCategories.hpp` in the place where beginners would expect it (name, one plain
   sentence, section), add its subcategories to `kPaletteSubs` (every subcategory has its own sentence and a name no other
   subcategory and no topic uses), paste the new table (`test_vs_categories --topic-table`) over the one above, and
   regenerate the two pages with `scripts/vs_category_report.py`. The test checks each of these.
6. **The old places in `editor/VisualScriptLegacyPlaces.hpp` are frozen.** Do not add nodes there and do not change
   them; they only feed the "found by its old topic" search words.

## How hard a node is

Every node has a level, stored on the registry row as `VsDef::level` and filled
by `Registry()` from `editor/VisualScriptLevels.hpp`.

| Level | Who it is for | Examples |
|---|---|---|
| Basic | A first script | On Start, Branch, Set Variable, Add, Spawn, Print |
| Intermediate | The rest of the toolbox | For Loop, lists, tweens, raycasts, quests |
| Advanced | A specialist tool | Online play, behavior trees, IK, build mode, calling another graph |

Inside a topic, Basic nodes come first, then Intermediate, then Advanced. Each
row has a small badge. The filter above the search is All, Basic, Intermediate,
or Advanced. All is the default. The choice is remembered in `okay_vsnodes.txt`.

**Starter** is the first section. It is the 26 Basic nodes a beginner needs:
On Start, On Every Frame, On Key, Branch, Sequence, Wait Seconds, Get Variable,
Set Variable, Add To Variable, a fixed value, Add, Compare, a random number,
Print, Spawn, Destroy, Set Active, Get Position, Set Position, Move, a key,
Play Sound, Set Text, Show UI, the frame time, and Pause. **Recently used** is
the section under it.

Search puts the closest match first, then the easier level, then a node the
person used lately. Dropping a wire on empty space lists the nodes that fit
that pin first, still easiest first.

A new node, including one added on the logic-runtime branch, gets a level in
`VsLevelForNode` (or `kSharedLevels` when the type is new on that branch), a
one-line `VsNodeHelp` sentence, and a `VsNodeExample` when a beginner would
benefit from one. `tests/test_vs_palette_levels.cpp` fails if a node in this
tree has no level or no help, if the filter or the search order is wrong, or
if a shared type from that branch has no level.

## Old names still work

Three things, all required:

1. **Search keywords.** The old title (`On Collision`, `Delay (s)`, `On Var Changed`) and any old spelling people typed are keywords. They are not a second entry in the list and they are not the title on the node. Searching for the old words still finds the node. Add them on the row in `editor/VisualScriptNames.hpp` (`oldTitle` and `extra`).
2. **File aliases.** The token in the file (`OnCollision`) does not change just because the title changed, so saved graphs load with no alias. Change a token only when two nodes become one, or when the token itself was a name people should not have to read. Then add a line to `kNodeAliases` in `engine/include/okay/VisualScript/NodeAliases.hpp`. `MakeNode` resolves it before it builds the node. Do not delete the old token.

```cpp
// engine/include/okay/VisualScript/NodeAliases.hpp
{ "OverlapTest", "IsTouching" },
```

3. **Old topics.** A node that moved is also found by the topic and subcategory it used to sit in: type `Scene` or
   `Atmosphere` and Set Weather is there, type `Advanced` and the text-script functions that used to be in that bucket
   are there, type `Input` and On Key Pressed is there. The words come from `VsMovedFromWords` in
   `editor/VisualScriptCategories.hpp`, which reads the old place from `editor/VisualScriptLegacyPlaces.hpp`. The
   older menu words still work too: "Engine API", "Transform", "Actions", and the old step group (Move, Look,
   Variables). A script binding or an Actions step that does the same job as a hand-written node (Mathf.Abs,
   Transform.Translate, Wait, Play Sound) stays out of the browse list and still appears when you search for its old name.

New graphs still write the token the editor already saves (`g.add` uses `d.type`).
Renaming a title does not rewrite files, and moving a node to another topic changes nothing in a saved graph: a graph
stores the node's type, never its topic.

Favourites and recently used nodes (`okay_vsnodes.txt`, lines like `fav n:OnKey`, `fav a:set_var`, `rec c:var_eq`) are
remembered by node name, never by topic, so a node that moved keeps its star. `VsPaletteNodeKey` and `VsPaletteStepKey` make
the keys and `tests/test_vs_categories.cpp` checks the format.

## Where to add a name

`editor/VisualScriptNames.hpp`, one `VsName` row per node:

- `type` — the file token, unchanged
- `kind` — Event, Action, Question, Reading, or Calculation (the table above)
- `title` — the words on the node
- `oldTitle` — the previous title, even when it matches `title` (the node was reviewed)
- `extra` — more search words, or `""`
- pin arrays only for pins whose words change (`Old` becomes `Previous`)

`Registry()` in `editor/VisualScriptEditor.hpp` applies this table when the
editor starts. Do not paste the friendly title into the big registry list;
that list is the file token and the fallback. The table is the name people see.

`tests/test_vs_names.cpp` fails if:

- a node in a finished group has no row
- a title or pin breaks the rules above
- the old title is missing from search
- the file token no longer builds a node

Finished groups so far: **Events**. Add the next group to `kGatedNameCategories`
in the test when its rows are in the table. A new node in a finished group
needs a row in the same change. A new node in any group should already follow
this page, even before its group is finished.

## Templates

Graph > Templates in the visual script editor replaces the graph with a complete
script from `editor/VisualScriptTemplates.hpp`. Each one is a working graph: a
comment on the canvas, a level from the table above, and `var` lines. Those
numbers and words show on the Visual Script component, and Play starts with
them set. Picking one on a selected object attaches the graph and adds the
parts it names (a rigidbody, a trigger, text, a sound, or particles).

The old Starter Scripts list is one-step Actions recipes. It stays on an Actions
component. It is not what this menu shows.

| Category | Template | Level |
|---|---|---|
| Movement | Move with WASD, Spin, Float, Jump | Basic |
| Movement | Follow a target, Patrol | Intermediate |
| Interaction | Door, Pickup, Press E | Basic |
| Gameplay | Moving platform, Win at score | Basic |
| Gameplay | Health, Checkpoint, Chase | Intermediate |
| UI | Score readout, Message | Basic |
| UI | Countdown, Hit feedback | Intermediate |
| Camera | Camera shake | Basic |
| Camera | Camera follow, Camera zoom | Intermediate |
| Logic | Cooldown ability | Intermediate |
| Logic | State machine | Advanced |
| Gameplay | Spawner, Shooter, Win or lose | Intermediate |
| Gameplay | Line of sight | Advanced |
| UI | Health bar | Intermediate |
| Movement | Click to move | Intermediate |
| Camera | Camera switch | Intermediate |

Countdown is the on-screen timer. Win or lose writes You win or You lose on the object's text. Spawner and Shooter use `examples/node_playground/marker.okayprefab` and `shot.okayprefab`. Line of sight chases only when a ray hits the object named Player. Click to move uses Mouse Ground, the point where the cursor meets the floor.

`tests/test_vs_templates.cpp` loads every template in a scene, presses Play, and
checks the result (the object moved, the door opened, the score went up).

## Docs

The palette index in `docs/scripting/visual_scripting.md` uses the title people see, with
the file token in backticks. Update that group when you rename it.

`docs/visual_scripting_nodes.md` (generated on the node-docs branch) must use
these same titles the next time it is generated. Do not invent a second wording.

## The gameplay library

`tools/gen_gameplay_library.py` writes the gameplay library: nodes that act on an
object and its components (health, items, a Rigidbody, a Camera, text, sound,
quests, saved positions, groups of objects, time and input). Each node is one
entry in the generator with its plain-English title, a level, a one-line help
sentence, its topic, its behaviour, and the scene test that proves it. The same
run writes `engine/include/okay/VisualScript/NodesGameplayLib.hpp`,
`editor/generated/gplib_*.inc`, `tests/test_vs_gameplay_library.cpp`, and one
playground graph per topic (`examples/visual_script/playground/gl_*.okayvs`, with
a matching scene). Node ids start with `Gl`. Do not edit the generated files.

| Topic | Subcategory | What is there |
|---|---|---|
| Movement | (none), Turn, Size | Move forward or sideways, move to a position, hop, circle, turn toward an object, snap to a grid, keep inside a box |
| Physics | (none), Queries | Speed, jump, push, mass, gravity, drag, bounce; facing, behind, nearby counts, alert level |
| Objects | Survival | Health, armor, resistance, lives, respawn, damage and heal over time, critical hits and falloff |
| Objects | Items | Count, fit, move and take items, carry limit and weight |
| Objects | Groups | Hide, show and destroy everything with a tag |
| Camera | | Zoom, field of view, shake, tilt, follow and look at an object |
| UI | | Show a number, a clock, health or time in text; color, size, opacity, typing out |
| Audio | | Volume, pitch, looping, muting, fading |
| Animation | Procedural | Pulse, sway, fade a sprite |
| Scene | Story | Start, complete, fail and count quests, and their objectives |
| Scene | Dialogue | Start a conversation, show a line, offer choices, read the answer, end it (nine nodes, run against a conversation file kept beside the scene) |
| Variables | Saved | Save and load a position or health under a name |
| Math | Progression | Level from experience, stat growth, prices, cooldown share, roll a chance |
| Time | | Slow motion, game speed, session time |
| Input | Axes, Keys, Mouse | Horizontal and vertical input, sprint, crouch, jump, fire, aim |

The Topic column above is the label the generator writes (old-style, kept so the generator did not have to change).
`kLegacyMoves` and a few rows in `kNodeRules` in `editor/VisualScriptPlaceRules.hpp` turn each label into the real topic
(Movement becomes Position, Move & Follow or Rotation & Scale; Objects / Survival becomes Health & Damage; Scene / Story
becomes Quests & Dialogue; and so on), so the list in the Add Node menu is the one in
[visual_scripting_topics.md](visual_scripting_topics.md), not this table.

There are 241 gameplay library nodes. The count is in `tools/gameplay_library_count.txt`. A new node needs a test in
the generator, or the run fails the check that every node is covered.

## Every node must do something

`tests/test_vs_registry_effect.cpp` builds every registered node and runs it
against the same graph without it. A node passes when something you can see
differs (a transform, a component value, a variable, a log line, a saved key, a
render setting) or when it reports a clear error. It runs in the plain test scene,
then in a scene with the components it drives (an animator, a nav agent, a
behaviour tree, Full Body IK, particles, a line, a music player), then in a
targeted scenario (a hurt body, a seat, a wall to vault, a quest, a playing
sound). Online nodes run with no session and must report or return a value; the
ones that would open a connection are built only. Event nodes are fired by what
raises them. A node that does none of this fails the test, so it has to be fixed
or hidden from the Add Node menu (its factory stays so old graphs still load).
Ten online nodes are hidden that way for now, in `kHiddenUntilTested` in `editor/VisualScriptPalette.hpp` with a reason each: `Fn.net_join_link`, `Fn.net_pin_host` and the eight PlayFab `Fn.pf_*` nodes. The registry test asserts that list, so it cannot grow quietly.
`tests/vs_registry_known_gaps.inc` is the list of exceptions and is empty.
