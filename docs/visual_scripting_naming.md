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

## Categories

The Add Node menu has one topic per node, in this order. Common topics come
first. A large topic also has subcategories; the everyday nodes stay in the
topic itself, and the subcategory list appears when that topic is selected.
There is no catch-all topic: not "Do something", "Check something", "Actions",
"Data", "Engine API", "Basic", "Look", or "World". Those old words are search
keywords only. Basic is a level, below, not a topic.

| Topic | What goes here | Subcategories, when the topic is large |
|---|---|---|
| Events | Where a script starts: the frame, enable, a message | |
| Flow | Order, choices, loops, waiting, functions | Loops, Timing, Functions |
| Variables | Numbers and yes/no remembered on an object, the scene, or a save | Compare, Change, Shared, Saved, Story |
| Math | Arithmetic, comparisons, vectors, random numbers | Compare, Random, Vectors, Angles, Curves |
| Text | Words, sentences, language | Language |
| Lists & Maps | Several values under one name | Lists, Maps |
| Objects | Find, spawn, show, hide, name, items | Items, Appearance, Lines, Survival |
| Movement | Position, rotation, scale, and moving toward something. Search also finds these under Transform | Turn, Size, Tweens |
| Physics | Forces, velocity, collisions, triggers, raycasts | Collisions, Queries |
| Animation | Clips, character actions, IK, cutscenes | Character, IK, Cutscenes |
| Audio | Sound effects and music | |
| Input | Keys, mouse, movement axes, controller actions | Keys, Mouse, Axes, Controls |
| Camera | Where the player looks, shake, zoom | |
| UI | On-screen text, buttons, bars | Buttons, Bars, Fields, Layout, Popups, Theme |
| Scene | Loading, weather, story, navigation, building | Story, Navigation, Building, Atmosphere |
| Time | The clock, the frame, game speed | |
| Online | Hosting, joining, messages, Steam | Steam, PlayFab |
| Debug | Messages for you while building | |
| Advanced | Behavior trees and asking an object for a component | Behavior |

A new node, including the ones added on the logic-runtime branch, picks one of
these topics (and a subcategory from that row when the node is a special case
of a large topic). Add the type to `VsPlaceForNode` in
`editor/VisualScriptCategories.hpp` (or `VsPlaceForApi` / `VsPlaceForStep` for
a script binding or an action step). Do not invent a new topic, and do not put
the node in a catch-all. `tests/test_vs_palette_categories.cpp` fails if a node
has no topic or lands in a catch-all.

Two entries that do the same job are one row while browsing. A script binding
that matches a hand-written node (Mathf.Abs, Transform.Translate) and an
Actions step that matches one (Wait, Play Sound, Set Position) stay out of the
browse list. Searching for the old name still finds them.

Old menu words still find the node. "Engine API", "Transform", "Actions", and
the old step group (Move, Look, Variables) are search keywords. A script node
that does the same job as a hand-written node (Mathf.Abs, Transform.Translate)
stays out of the browse list and still appears when you search for its old name.

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

Two different things, both required:

1. **Search keywords.** The old title (`On Collision`, `Delay (s)`, `On Var Changed`) and any old spelling people typed are keywords. They are not a second entry in the list and they are not the title on the node. Searching for the old words still finds the node. Add them on the row in `editor/VisualScriptNames.hpp` (`oldTitle` and `extra`).
2. **File aliases.** The token in the file (`OnCollision`) does not change just because the title changed, so saved graphs load with no alias. Change a token only when two nodes become one, or when the token itself was a name people should not have to read. Then add a line to `kNodeAliases` in `engine/include/okay/VisualScript/NodeAliases.hpp`. `MakeNode` resolves it before it builds the node. Do not delete the old token.

```cpp
// engine/include/okay/VisualScript/NodeAliases.hpp
{ "OverlapTest", "IsTouching" },
```

New graphs still write the token the editor already saves (`g.add` uses `d.type`).
Renaming a title does not rewrite files.

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

`tests/test_vs_templates.cpp` loads every template in a scene, presses Play, and
checks the result (the object moved, the door opened, the score went up).

## Docs

The palette index in `docs/visual_scripting.md` uses the title people see, with
the file token in backticks. Update that group when you rename it.

`docs/visual_scripting_nodes.md` (generated on the node-docs branch) must use
these same titles the next time it is generated. Do not invent a second wording.
