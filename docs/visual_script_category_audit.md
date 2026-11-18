# Visual script category audit

The owner (2026-11-15): "put the nodes in better categories where it makes more sense. it's very confusing where to find
what. I'd rather have more categories than less." This page is the measurement behind that change: where every node sat in
the Add Node menu before, where it sits now, and what was found on the way.

What changed in one paragraph: the menu had 19 topics. One of them, **Advanced, held 515 nodes** (almost all text-script
functions: weather, quests, navigation, behavior trees, accounts, display settings, build mode, tweens and more); Online
held 194 directly, UI 157, Math 116. Now there are **70 topics in 14 sections** with 99 subcategories, every node sits in
the topic a beginner would look in, and no topic has more than 30 nodes directly in it. Nothing was removed and no saved
graph changes: a graph stores a node's type, never its topic. The tree and the rules for adding a node are in
[visual_scripting_naming.md](visual_scripting_naming.md); every node under its topic is in
[visual_scripting_topics.md](visual_scripting_topics.md).

## How this was measured

The node registry, the engine-API table and the Actions steps exist only in C++, so a test dumps them:

```
<build>/tests/test_vs_categories --dump nodes.tsv
python3 scripts/vs_category_report.py nodes.tsv                  # the tables for the places the menu uses now
python3 scripts/vs_category_report.py nodes.tsv --legacy         # the same tables for the old places
python3 scripts/vs_category_report.py nodes.tsv --moved          # every node that moved: old place -> new place
python3 scripts/vs_category_report.py nodes.tsv --markdown-audit --update docs/visual_script_category_audit.md
python3 scripts/vs_category_report.py nodes.tsv --markdown-topics > docs/visual_scripting_topics.md
```

A *palette row* is a node, an engine-API node, or an Actions step. A row is *listed* when the menu shows it (a duplicate of
another row, or one of ten nodes waiting to be tested, is not listed). A *browse* row is a listed row that also shows while you
click through a topic (an Actions step that is the same job as a node is found by search only). The old places were read from
the old code on `origin/main` before the change and then frozen in `editor/VisualScriptLegacyPlaces.hpp`; the frozen copy was
checked against the old code on all 3,171 rows (0 differences), and it feeds the "found by its old topic" search words. The
"sounds like another topic" counts are a heuristic (whole words in the id and the title against the words of the topic's own
name and description): they show the size of the problem, not a verdict on each node.

<!-- generated:start -->

### Numbers at a glance

There are 3171 palette rows (358 api, 1438 builtin, 60 check, 241 gameplay, 224 library, 592 node, 258 step). 2620 are listed in the menu;
2600 of those also show while you click through a topic. All the counts below are those 2600 rows.

| | Before | After |
|---|---:|---:|
| Topics | 19 | 70 |
| Subcategories | 52 | 99 |
| Sections (headings in the sidebar) | 0 | 14 |
| Most nodes directly in one topic | 515 (Advanced) | 29 (Mouse & Touch) |
| Largest single bucket (topic or subcategory) | 515 | 37 |
| Topics holding more than 30 nodes directly | 14 | 0 |
| Rows whose name sounds like another topic (heuristic) | 258 | 39 |
| Rows in a subcategory their topic's description never mentions | 626 | 0 |
| Rows that changed place | | 2533 of 2600 |

### Before: the old 19 topics

| Topic | Nodes | Directly in the topic | Subcategories (nodes) |
|---|---:|---:|---|
| Events | 10 | 10 |  |
| Flow | 72 | 33 | Timing (23), Loops (11), Functions (5) |
| Variables | 136 | 19 | Saved (70), Change (24), Compare (15), Shared (6), Story (2) |
| Math | 293 | 116 | Compare (51), Vectors (48), Curves (43), Angles (17), Random (9), Progression (7), Color (2) |
| Text | 89 | 83 | Language (6) |
| Lists & Maps | 53 | 0 | Lists (35), Maps (15), Structs (3) |
| Objects | 219 | 92 | Survival (57), Items (41), Lines (16), Groups (5), Combat (5), Appearance (3) |
| Movement | 180 | 85 | Tweens (65), Turn (22), Size (8) |
| Physics | 119 | 67 | Queries (38), Collisions (14) |
| Animation | 96 | 63 | Character (19), IK (6), Procedural (4), Cutscenes (4) |
| Audio | 38 | 38 |  |
| Input | 115 | 54 | Mouse (21), Keys (17), Controls (15), Axes (8) |
| Camera | 30 | 30 |  |
| UI | 209 | 157 | Popups (16), Bars (12), Layout (8), Fields (7), Theme (5), Buttons (4) |
| Scene | 128 | 34 | Story (41), Navigation (26), Atmosphere (12), Dialogue (9), Building (6) |
| Time | 52 | 52 |  |
| Online | 211 | 194 | Steam (11), PlayFab (6) |
| Debug | 17 | 17 |  |
| Advanced | 533 | 515 | Behavior (18) |

The 15 biggest buckets:

| Nodes | Bucket |
|---:|---|
| 515 | Advanced |
| 194 | Online |
| 157 | UI |
| 116 | Math |
| 92 | Objects |
| 85 | Movement |
| 83 | Text |
| 70 | Variables / Saved |
| 67 | Physics |
| 65 | Movement / Tweens |
| 63 | Animation |
| 57 | Objects / Survival |
| 54 | Input |
| 52 | Time |
| 51 | Math / Compare |

Nodes whose name or description sounds like another topic than the one they sat in (a heuristic: whole words in the id and the title):

258 rows. The biggest groups:

| Rows | Sat in | The name talks about | Examples |
|---:|---|---|---|
| 18 | Advanced | ui | Apply Screen Theme, Set Screen Theme, Modal Close, Modal Open, Screen Height ... |
| 16 | Advanced | online | Account Error, Account Get, Account is Logged in, Account is Online, Account Login ... |
| 16 | Advanced | display | Begin Display Confirm, Get Display Confirm Seconds, Get Display Height, Get Display Mode, Get Display Resolutions ... |
| 14 | Advanced | building | Build Active, Build Clear, Build Count, Build Enter, Build Exists ... |
| 13 | Math | tween | Ease In, Ease Out, Ease In-Out, Ease Toward, Ease in ... |
| 11 | Objects | spline | Get Point On Spline, Spline Length, Get Point Count, Is Closed, Set Closed ... |
| 11 | Math | animation | Blend Unclamped, Blend Clamped, Blend Angles, Inverse Blend, Color Blend Between ... |
| 11 | Advanced | animation | Mathf Inverse Blend Between, Mathf Blend Between Angle, Vector Blend Between Unclamped, Blend, Blend Param ... |
| 11 | Animation | ik | Inverse Kinematics Clear Target, Inverse Kinematics Enable, Inverse Kinematics Foot, Inverse Kinematics Hand, Inverse Kinematics Look ... |
| 9 | Advanced | navigation | Grid Add, Grid Block, Grid Clear, Grid Fits, Grid Set Cell Cost ... |
| 8 | Objects | weapons | Spawn Projectile, Shoot, Use Ammo, Swing Weapon, Fire Weapon ... |
| 7 | Advanced | inventory | Can Craft, Drop Item, Inv Set Slot, Inv Slot, Item Def ... |

626 rows sit in 35 subcategories that the one-line description of their topic does not mention:

| Rows | Subcategory |
|---:|---|
| 70 | Variables / Saved |
| 65 | Movement / Tweens |
| 57 | Objects / Survival |
| 51 | Math / Compare |
| 43 | Math / Curves |
| 41 | Objects / Items |
| 38 | Physics / Queries |
| 24 | Variables / Change |
| 23 | Flow / Timing |
| 22 | Movement / Turn |
| 17 | Math / Angles |
| 16 | UI / Popups |
| 16 | Objects / Lines |
| 15 | Input / Controls |
| | and 21 more |

110 groups of rows have the same name, or the same job under another name, and sit in different places:

| Name | The rows | Where |
|---|---|---|
| activate | Activate (builtin); Activate Named (step) | Advanced; Objects |
| add | Add (+) (node); Add (builtin) | Advanced; Math |
| axisx | Axis X (node); Axis Right (builtin) | Input; Input / Axes |
| axisy | Axis Y (node); Axis Vertical (builtin) | Input; Input / Axes |
| begindisplayconfirm | Begin Display Confirm (builtin); Ask to Keep Display (step) | Advanced; Scene |
| bob | Move Upward and Downward (node); Bob (builtin) | Advanced; Movement |
| btget | Get Blackboard (node); Behavior Tree Get (builtin) | Advanced; Advanced / Behavior |
| btrun | Run Tree (node); Behavior Tree Run (builtin) | Advanced; Advanced / Behavior |
| btset | Set Blackboard (node); Behavior Tree Set (builtin) | Advanced; Advanced / Behavior |
| call | Call (builtin); Call Script Event (step) | Advanced; Flow / Functions |
| chase | Chase Target (node); Chase (builtin) | Advanced; Movement |
| climbup | Climb Up (node); Climb up (builtin) | Advanced; Animation / Character |
| comparetag | Compare Tag (builtin); Object Compare Tag (builtin) | Advanced; Objects |
| deactivate | Deactivate (builtin); Deactivate Named (step) | Advanced; Objects |

### After: the new topics

| Topic | Nodes | Directly in the topic | Subcategories (nodes) |
|---|---:|---:|---|
| Game Events | 21 | 21 |  |
| Input Events | 18 | 18 |  |
| Collision & Trigger Events | 13 | 13 |  |
| Inventory & Quest Events | 13 | 13 |  |
| Network Events | 4 | 4 |  |
| Branches & Gates | 30 | 24 | State Machines (6) |
| Loops | 12 | 12 |  |
| Waiting & Timers | 41 | 25 | Delayed Calls (16) |
| Functions & Calls | 14 | 14 |  |
| Time & Game Speed | 61 | 0 | Clock (24), Game Speed (14), Time Math (13), Pausing (10) |
| Variables | 64 | 15 | Change a Variable (23), Check a Variable (15), Shared Variables (11) |
| Saving & Loading | 73 | 12 | Save Slots (26), Save Files (19), Saved Values (16) |
| Lists | 47 | 22 | Array Functions (16), Sorting & Searching (9) |
| Maps & Structs | 23 | 23 |  |
| Numbers | 118 | 15 | Limits & Wrapping (24), Rounding & Snapping (19), Script Math Functions (16), More Arithmetic (14), Averages & Percentages (12), Bits (10), Types & Checks (8) |
| Blending & Easing | 70 | 20 | Easing & Curves (37), Smoothing & Springs (13) |
| Angles & Trigonometry | 31 | 17 | Angle Helpers (14) |
| Vectors | 71 | 20 | Flat Vectors (20), Script Vector Functions (17), Directions & Axes (14) |
| Comparisons & Logic | 52 | 26 | True & False Logic (15), Scores & Number Sets (11) |
| Random | 19 | 19 |  |
| Text | 98 | 14 | Clean Up & Padding (27), Search & Replace (18), Numbers & Text (15), Pieces & Words (14), Compare & Check (8), Clipboard (2) |
| Language & Translation | 7 | 7 |  |
| Create & Destroy | 25 | 14 | Spawners & Pools (11) |
| Find Objects | 39 | 24 | Names & Tags (15) |
| Show & Hide | 22 | 22 |  |
| Parents & Components | 17 | 7 | Components & Properties (10) |
| Messages & Signals | 10 | 10 |  |
| Color & Appearance | 34 | 12 | Sprites & Textures (16), Materials & Meshes (6) |
| Particles & Trails | 18 | 18 |  |
| Position | 48 | 25 | Read a Position (16), Stay in Bounds (7) |
| Rotation & Scale | 49 | 21 | Scale (15), Facing & Looking (13) |
| Move & Follow | 53 | 14 | Script Movement Functions (24), Motion Patterns (15) |
| Tweens | 87 | 9 | Control Tweens (25), Start a Tween (24), Tween Options (14), Tween Sequences (10), Tween Events (5) |
| Paths & Splines | 13 | 13 |  |
| Forces & Velocity | 65 | 12 | Body Settings (27), Speed & Jumping (26) |
| Raycasts & Overlaps | 43 | 19 | Ray Results (16), Overlaps & Shape Casts (8) |
| Sight & Distance Checks | 28 | 28 |  |
| Health & Damage | 60 | 18 | Resources & Buffs (15), Health Values (15), Armor & Lives (12) |
| Weapons & Shooting | 20 | 20 |  |
| Inventory & Items | 61 | 21 | Inventory Slots (13), Pick Up & Drop (10), Carry Limits (10), Crafting & Loot (4), Equipment (3) |
| Quests & Dialogue | 53 | 0 | Quests (28), Dialogue (15), Story Flags (10) |
| Score & Winning | 13 | 13 |  |
| Doors & Switches | 6 | 6 |  |
| Build Mode | 22 | 22 |  |
| Animation | 59 | 28 | Animator Parameters (23), Blend Shapes & Trees (8) |
| Character Actions | 31 | 24 | Stances & Ragdoll (7) |
| Inverse Kinematics | 17 | 17 |  |
| Navigation | 45 | 21 | Grid Paths (24) |
| Behavior Trees | 20 | 20 |  |
| Sound Effects | 20 | 20 |  |
| Music & Volume | 22 | 22 |  |
| Camera | 34 | 0 | Zoom & Shake (20), Follow & Switch (14) |
| Screen Text | 42 | 13 | Script Text Widgets (15), Text Style & Format (10), Numbers in Text (4) |
| Buttons & Inputs | 74 | 19 | Sliders & Toggles (20), Pickers & Search (19), Dropdowns & Text Fields (16) |
| Bars & Layout | 92 | 0 | Tabs & Pages (29), Progress Bars (23), Layout Helpers (21), Scroll Areas & Grids (10), Themes (9) |
| Popups & Dialogs | 35 | 0 | Modals & Sheets (19), Toasts & Tooltips (16) |
| Scenes & Levels | 15 | 15 |  |
| Lighting & Sky | 12 | 12 |  |
| Weather & Fog | 9 | 9 |  |
| Post Effects | 5 | 5 |  |
| Window & Display | 19 | 19 |  |
| Cutscenes | 8 | 8 |  |
| Keyboard | 24 | 24 |  |
| Mouse & Touch | 38 | 29 | Touch & Gestures (9) |
| Gamepad & Controls | 39 | 26 | Named Actions (13) |
| Multiplayer | 132 | 0 | Matchmaking & Relay (27), Players & Matches (26), Messages & RPC (23), Hosting & Joining (22), Synced Objects & Variables (19), Server Browser (15) |
| Steam | 61 | 4 | Achievements & Stats (25), Lobbies & Friends (21), Cloud & Workshop (11) |
| Server Security | 17 | 17 |  |
| Accounts & Online Saves | 27 | 0 | Accounts (14), Leaderboards & PlayFab (8), Cloud Saves (5) |
| Debug | 17 | 17 |  |

The 15 biggest buckets now:

| Nodes | Bucket |
|---:|---|
| 37 | Blending & Easing / Easing & Curves |
| 29 | Mouse & Touch |
| 29 | Bars & Layout / Tabs & Pages |
| 28 | Sight & Distance Checks |
| 28 | Animation |
| 28 | Quests & Dialogue / Quests |
| 27 | Text / Clean Up & Padding |
| 27 | Forces & Velocity / Body Settings |
| 27 | Multiplayer / Matchmaking & Relay |
| 26 | Comparisons & Logic |
| 26 | Gamepad & Controls |
| 26 | Forces & Velocity / Speed & Jumping |
| 26 | Multiplayer / Players & Matches |
| 26 | Saving & Loading / Save Slots |
| 25 | Waiting & Timers |

Same heuristic, new places:

39 rows. The biggest groups:

| Rows | Sat in | The name talks about | Examples |
|---:|---|---|---|
| 3 | Blending & Easing | lists | Map Signed, Map Bipolar, Map Range |
| 3 | Popups & Dialogs | text | Set Rich Text, Set Confirm Text, Set Cancel Text |
| 2 | Bars & Layout | particles | Set Trail, Set Bar Trail |
| 2 | Buttons & Inputs | tween | Set Ease, Set Button Ease |
| 2 | Color & Appearance | animation | Color Blend Between, Blend Between Color |
| 2 | Vectors | animation | Vector Blend Between, Vector Blend Between Unclamped |
| 1 | Branches & Gates | cutscene | Sequence |
| 1 | Game Events | animation | On Animation Marker |
| 1 | Game Events | health | On Health Changed |
| 1 | Random | inventory | Pick Random Item |
| 1 | Waiting & Timers | animation | Blend Over Time |
| 1 | Vectors | navigation | Grid Cell |

0 rows sit in 0 subcategories that the one-line description of their topic does not mention.

32 groups of rows have the same name, or the same job under another name, and sit in different places:

| Name | The rows | Where |
|---|---|---|
| movetowards | Move Towards (value) (node); Move Towards (node); Move Towards Object (builtin) | Blending & Easing; Move & Follow; Move & Follow / Script Movement Functions |
| add | Add (+) (node); Add (builtin) | Numbers; Score & Winning |
| chase | Chase Target (node); Chase (builtin) | Move & Follow; Move & Follow / Script Movement Functions |
| exists | Object Exists (node); Exists (api) | Find Objects; Saving & Loading / Save Slots |
| flee | Flee From Threat (node); Flee (builtin) | Move & Follow; Move & Follow / Script Movement Functions |
| formatnumber | Format Number (node); Format Number (gameplay) | Screen Text / Text Style & Format; Text / Numbers & Text |
| get | Get Object (api); Get (builtin) | Position / Read a Position; Variables / Shared Variables |
| getvalue | Get Value (api); Get Object Value (builtin) | Buttons & Inputs / Sliders & Toggles; Variables |
| indexof | Index Of (node); Index of (builtin) | Lists / Sorting & Searching; Text / Search & Replace |
| max | Max (node); Largest (builtin) | Numbers; Numbers / Script Math Functions |
| min | Min (node); Smallest (builtin) | Numbers; Numbers / Script Math Functions |
| mod | Mod (%) (node); Mod (builtin) | Numbers; Numbers / Script Math Functions |
| moveby | Move By (Vec3) (node); Move by (builtin) | Move & Follow / Script Movement Functions; Position |
| moveto | Move to (builtin); Move To Point (step) | Move & Follow; Move & Follow / Script Movement Functions |

### Every node that moved

2533 of 2600 rows changed place; 67 kept the same topic name and subcategory (for example Debug).

#### By old place

| Old place | Rows | Went to (rows) |
|---|---:|---|
| Advanced | 515 | Window & Display (18), Waiting & Timers / Delayed Calls (16), Build Mode (15), Move & Follow / Script Movement Functions (15), Accounts & Online Saves / Accounts (14), Color & Appearance / Sprites & Textures (13), Inventory & Items / Inventory Slots (12), Raycasts & Overlaps / Ray Results (12), and 95 more |
| Online | 194 | Multiplayer / Players & Matches (26), Multiplayer / Messages & RPC (23), Multiplayer / Hosting & Joining (22), Steam / Achievements & Stats (21), Multiplayer / Synced Objects & Variables (19), Multiplayer / Matchmaking & Relay (18), Server Security (17), Steam / Lobbies & Friends (15), and 4 more |
| UI | 157 | Bars & Layout / Tabs & Pages (21), Bars & Layout / Layout Helpers (21), Buttons & Inputs / Pickers & Search (15), Buttons & Inputs / Sliders & Toggles (15), Screen Text / Script Text Widgets (15), Buttons & Inputs (13), Screen Text (11), Screen Text / Text Style & Format (10), and 9 more |
| Math | 116 | Numbers / Limits & Wrapping (21), Numbers (15), Numbers / More Arithmetic (14), Numbers / Averages & Percentages (11), Numbers / Rounding & Snapping (10), Blending & Easing (10), Numbers / Script Math Functions (10), Vectors / Script Vector Functions (8), and 7 more |
| Objects | 92 | Find Objects (16), Create & Destroy (13), Show & Hide (13), Find Objects / Names & Tags (11), Particles & Trails (5), Create & Destroy / Spawners & Pools (4), Messages & Signals (4), Sight & Distance Checks (3), and 13 more |
| Movement | 85 | Position (21), Move & Follow (14), Move & Follow / Motion Patterns (8), Move & Follow / Script Movement Functions (8), Position / Read a Position (5), Position / Stay in Bounds (5), Rotation & Scale (4), Forces & Velocity / Speed & Jumping (4), and 10 more |
| Text | 70 | Text / Clean Up & Padding (20), Text / Search & Replace (16), Text / Pieces & Words (11), Text / Compare & Check (8), Text / Numbers & Text (7), Text / Clipboard (2), Variables / Shared Variables (1), Numbers / Types & Checks (1), and 4 more |
| Variables / Saved | 70 | Saving & Loading / Save Slots (25), Saving & Loading / Save Files (17), Saving & Loading / Saved Values (14), Saving & Loading (12), Build Mode (1), Accounts & Online Saves / Cloud Saves (1) |
| Physics | 67 | Forces & Velocity / Body Settings (27), Forces & Velocity / Speed & Jumping (21), Forces & Velocity (9), Raycasts & Overlaps (5), Raycasts & Overlaps / Ray Results (2), Game Events (1), Rotation & Scale / Facing & Looking (1), Raycasts & Overlaps / Overlaps & Shape Casts (1) |
| Movement / Tweens | 65 | Tweens / Control Tweens (20), Tweens / Start a Tween (18), Tweens / Tween Options (13), Tweens (9), Tweens / Tween Events (5) |
| Objects / Survival | 57 | Health & Damage / Resources & Buffs (15), Health & Damage (14), Health & Damage / Health Values (14), Health & Damage / Armor & Lives (11), Inventory & Items (3) |
| Input | 54 | Gamepad & Controls (15), Mouse & Touch (12), Keyboard (9), Input Events (7), Mouse & Touch / Touch & Gestures (6), Buttons & Inputs / Dropdowns & Text Fields (3), Camera / Follow & Switch (1), Move & Follow / Script Movement Functions (1) |
| Time | 52 | Time & Game Speed / Clock (15), Time & Game Speed / Time Math (13), Time & Game Speed / Game Speed (11), Time & Game Speed / Pausing (6), Tweens / Control Tweens (4), Game Events (1), Waiting & Timers (1), Tweens / Tween Options (1) |
| Math / Compare | 51 | Comparisons & Logic (24), Comparisons & Logic / True & False Logic (15), Comparisons & Logic / Scores & Number Sets (11), Branches & Gates / State Machines (1) |
| Math / Vectors | 48 | Vectors (20), Vectors / Flat Vectors (20), Vectors / Directions & Axes (8) |
| Math / Curves | 43 | Blending & Easing / Easing & Curves (28), Blending & Easing (8), Blending & Easing / Smoothing & Springs (7) |
| Objects / Items | 41 | Inventory & Items (18), Inventory & Items / Carry Limits (10), Inventory & Quest Events (6), Inventory & Items / Pick Up & Drop (3), Inventory & Items / Crafting & Loot (3), Inventory & Items / Equipment (1) |
| Scene / Story | 41 | Quests & Dialogue / Quests (25), Inventory & Quest Events (7), Quests & Dialogue / Story Flags (3), Messages & Signals (2), Cutscenes (2), Quests & Dialogue / Dialogue (2) |
| Audio | 38 | Music & Volume (20), Sound Effects (18) |
| Animation | 38 | Animation / Animator Parameters (23), Inverse Kinematics (11), Game Events (2), Animation / Blend Shapes & Trees (2) |
| Physics / Queries | 38 | Sight & Distance Checks (20), Raycasts & Overlaps (14), Find Objects (4) |
| Lists & Maps / Lists | 35 | Lists (22), Lists / Array Functions (9), Numbers / Types & Checks (2), Accounts & Online Saves / Cloud Saves (1), Maps & Structs (1) |
| Scene | 34 | Scenes & Levels (12), Navigation (7), Score & Winning (3), Game Events (3), Weather & Fog (2), Quests & Dialogue / Quests (2), Sight & Distance Checks (1), Find Objects (1), and 3 more |
| Flow | 33 | Branches & Gates (23), Branches & Gates / State Machines (4), Functions & Calls (3), Messages & Signals (3) |
| Camera | 30 | Camera / Zoom & Shake (18), Camera / Follow & Switch (9), Tweens / Start a Tween (3) |
| Scene / Navigation | 26 | Navigation (13), Navigation / Grid Paths (13) |
| Variables / Change | 24 | Variables / Change a Variable (23), Tweens / Start a Tween (1) |
| Flow / Timing | 23 | Waiting & Timers (23) |
| Movement / Turn | 22 | Rotation & Scale (14), Rotation & Scale / Facing & Looking (8) |
| Input / Mouse | 21 | Mouse & Touch (15), Input Events (6) |
| Animation / Character | 19 | Character Actions (19) |
| Advanced / Behavior | 18 | Behavior Trees (16), Loops (1), Branches & Gates / State Machines (1) |
| Input / Keys | 17 | Keyboard (15), Input Events (2) |
| Math / Angles | 17 | Angles & Trigonometry / Angle Helpers (9), Angles & Trigonometry (8) |
| UI / Popups | 16 | Popups & Dialogs / Modals & Sheets (9), Popups & Dialogs / Toasts & Tooltips (7) |
| Objects / Lines | 16 | Paths & Splines (9), Particles & Trails (7) |
| Input / Controls | 15 | Gamepad & Controls / Named Actions (5), Gamepad & Controls (4), Input Events (3), Mouse & Touch / Touch & Gestures (3) |
| Lists & Maps / Maps | 15 | Maps & Structs (15) |
| Variables / Compare | 15 | Variables / Check a Variable (15) |
| Physics / Collisions | 14 | Collision & Trigger Events (13), Sight & Distance Checks (1) |
| Scene / Atmosphere | 12 | Weather & Fog (7), Post Effects (4), Lighting & Sky (1) |
| UI / Bars | 12 | Bars & Layout / Progress Bars (9), Buttons & Inputs / Sliders & Toggles (3) |
| Flow / Loops | 11 | Loops (11) |
| Online / Steam | 11 | Steam / Lobbies & Friends (6), Steam / Achievements & Stats (4), Steam / Cloud & Workshop (1) |
| Events | 10 | Game Events (10) |
| Math / Random | 9 | Random (9) |
| Scene / Dialogue | 9 | Quests & Dialogue / Dialogue (9) |
| Input / Axes | 8 | Gamepad & Controls (7), Gamepad & Controls / Named Actions (1) |
| Movement / Size | 8 | Rotation & Scale / Scale (8) |
| UI / Layout | 8 | Bars & Layout / Scroll Areas & Grids (5), Bars & Layout / Tabs & Pages (3) |
| Math / Progression | 7 | Score & Winning (5), Waiting & Timers (1), Random (1) |
| UI / Fields | 7 | Buttons & Inputs / Dropdowns & Text Fields (7) |
| Animation / IK | 6 | Inverse Kinematics (6) |
| Variables | 6 | Score & Winning (3), Game Events (3) |
| Scene / Building | 6 | Build Mode (6) |
| Text / Language | 6 | Language & Translation (5), Game Events (1) |
| Variables / Shared | 6 | Variables / Shared Variables (6) |
| Online / PlayFab | 6 | Accounts & Online Saves / Leaderboards & PlayFab (6) |
| Objects / Groups | 5 | Show & Hide (2), Find Objects / Names & Tags (2), Create & Destroy (1) |
| Objects / Combat | 5 | Weapons & Shooting (5) |
| Flow / Functions | 5 | Functions & Calls (5) |
| UI / Theme | 5 | Bars & Layout / Themes (3), Buttons & Inputs / Pickers & Search (2) |
| Animation / Procedural | 4 | Move & Follow / Motion Patterns (4) |
| UI / Buttons | 4 | Buttons & Inputs (4) |
| Animation / Cutscenes | 4 | Cutscenes (4) |
| Lists & Maps / Structs | 3 | Maps & Structs (3) |
| Objects / Appearance | 3 | Color & Appearance (2), Color & Appearance / Sprites & Textures (1) |
| Variables / Story | 2 | Quests & Dialogue / Story Flags (2) |
| Math / Color | 2 | Color & Appearance (2) |
| Debug | 1 | Numbers / Script Math Functions (1) |

#### The full list

Sorted by the new place. Kind is node (hand-written), library (`Lb`), gameplay (`Gl`), builtin (a text-script function),
api (an engine-API property or method), step (an Actions step) or check (an Actions condition). The Id is what a graph file
stores (a builtin starts with `Fn.`), or the step's short name.


##### Game Events

| Node | Kind | Id | Was |
|---|---|---|---|
| On Animation Marker | node | `OnAnimEvent` | Animation |
| On Cutscene Marker | node | `OnSequenceEvent` | Animation |
| On Death | node | `OnDeath` | Events |
| On Destroyed | node | `OnDestroy` | Events |
| On Disabled | node | `OnDisable` | Events |
| On Enabled | node | `OnEnable` | Events |
| On Every Few Seconds | node | `OnTimer` | Time |
| On Every Frame | node | `OnUpdate` | Events |
| On Game Value Changed | node | `OnGlobalVarChanged` | Variables |
| On Health Changed | node | `OnHealthChanged` | Events |
| On Interact | node | `OnInteract` | Events |
| On Language Changed | node | `OnLanguageChanged` | Text / Language |
| On Late Update | node | `OnLateUpdate` | Events |
| On Message | node | `OnEvent` | Events |
| On Object Placed | node | `OnObjectPlaced` | Scene |
| On Object Removed | node | `OnObjectRemoved` | Scene |
| On Physics Update | node | `OnFixedUpdate` | Physics |
| On Scene Value Changed | node | `OnSceneVarChanged` | Variables |
| On Signal | node | `OnLogicSignal` | Scene |
| On Start | node | `OnStart` | Events |
| On Value Changed | node | `OnVarChanged` | Variables |

##### Input Events

| Node | Kind | Id | Was |
|---|---|---|---|
| On Action Held | node | `OnActionHeld` | Input / Controls |
| On Action Pressed | node | `OnAction` | Input / Controls |
| On Action Released | node | `OnActionUp` | Input / Controls |
| On Click | node | `OnClick` | Input / Mouse |
| On Double Tap | node | `OnDoubleTap` | Input |
| On Key | builtin | `Fn.on_key` | Input |
| On Key Pressed | node | `OnKey` | Input / Keys |
| On Key Released | node | `OnKeyUp` | Input / Keys |
| On Long Press | node | `OnLongPress` | Input |
| On Mouse Entered | node | `OnMouseEnter` | Input / Mouse |
| On Mouse Hover | node | `OnMouseOver` | Input / Mouse |
| On Mouse Left | node | `OnMouseExit` | Input / Mouse |
| On Mouse Pressed | node | `OnMouseDown` | Input / Mouse |
| On Mouse Released | node | `OnMouseUp` | Input / Mouse |
| On Pinch | node | `OnPinch` | Input |
| On Swipe | node | `OnSwipe` | Input |
| On Tap | node | `OnTap` | Input |
| On Two Finger Rotate | node | `OnRotate` | Input |

##### Collision & Trigger Events

| Node | Kind | Id | Was |
|---|---|---|---|
| Contact Normal | node | `ContactNormal` | Physics / Collisions |
| Contact Point | node | `ContactPoint` | Physics / Collisions |
| Impact Speed | node | `ImpactSpeed` | Physics / Collisions |
| On Collision End | node | `OnCollisionExit` | Physics / Collisions |
| On Collision Start | node | `OnCollision` | Physics / Collisions |
| On Collision Stay | node | `OnCollisionStay` | Physics / Collisions |
| On Collision With Tag | node | `OnCollisionTag` | Physics / Collisions |
| On Tag Enter | node | `OnTagEnter` | Physics / Collisions |
| On Tag Exit | node | `OnTagExit` | Physics / Collisions |
| On Tag Stay | node | `OnTagStay` | Physics / Collisions |
| On Trigger Enter | node | `OnTrigger` | Physics / Collisions |
| On Trigger Exit | node | `OnTriggerExit` | Physics / Collisions |
| On Trigger Stay | node | `OnTriggerStay` | Physics / Collisions |

##### Inventory & Quest Events

| Node | Kind | Id | Was |
|---|---|---|---|
| On Conversation Ended | node | `OnDialogueEnd` | Scene / Story |
| On Conversation Line | node | `OnDialogueEvent` | Scene / Story |
| On Inventory Full | node | `OnInventoryFull` | Objects / Items |
| On Item Added | node | `OnItemAdded` | Objects / Items |
| On Item Crafted | node | `OnCraft` | Objects / Items |
| On Item Equipped | node | `OnEquip` | Objects / Items |
| On Item Removed | node | `OnItemRemoved` | Objects / Items |
| On Item Used | node | `OnItemUsed` | Objects / Items |
| On Objective Completed | node | `OnObjectiveCompleted` | Scene / Story |
| On Quest Completed | node | `OnQuestCompleted` | Scene / Story |
| On Quest Failed | node | `OnQuestFailed` | Scene / Story |
| On Quest Message | node | `OnQuestEvent` | Scene / Story |
| On Quest Started | node | `OnQuestStarted` | Scene / Story |

##### Network Events

| Node | Kind | Id | Was |
|---|---|---|---|
| On Cheat Detected | node | `OnCheatDetected` | Online |
| On Hit Check | node | `OnValidateHit` | Online |
| On Network Action | node | `OnValidateRpc` | Online |
| On Object RPC | node | `OnNetRpc` | Online |

##### Branches & Gates

| Node | Kind | Id | Was |
|---|---|---|---|
| Always | check | `always` | Flow |
| Became False | node | `BecameFalse` | Flow |
| Became True | node | `BecameTrue` | Flow |
| Branch | node | `Branch` | Flow |
| Branch in Order | node | `BranchInOrder` | Flow |
| Counter | node | `Counter` | Flow |
| Every Few Calls | library | `LbEveryCalls` | Flow |
| Flip Flop | node | `FlipFlop` | Flow |
| Gate | node | `Gate` | Flow |
| Latch Gate | node | `LatchGate` | Flow |
| Once | builtin | `Fn.once` | Advanced |
| Pass After Calls | library | `LbPassAfter` | Flow |
| Random Branch | node | `ChanceBranch` | Flow |
| Run a Number of Times | node | `DoN` | Flow |
| Run and Check | library | `LbRunAndCheck` | Flow |
| Run Once | node | `Once` | Flow |
| Run Unless | node | `DoUnless` | Flow |
| Sequence | node | `Sequence` | Flow |
| Switch Number | node | `SwitchInt` | Flow |
| Switch on Name | node | `SwitchString` | Flow |
| Switch on Number | node | `SwitchCases` | Flow |
| Try Several Times | library | `LbTrySeveral` | Flow |
| Weighted Branch | node | `WeightedBranch` | Flow |
| When Value Changes | node | `WhenChanged` | Flow |

##### Branches & Gates / State Machines

| Node | Kind | Id | Was |
|---|---|---|---|
| Get State | node | `GetState` | Flow |
| Is in State | node | `IsInState` | Math / Compare |
| Run in State | library | `LbRunInState` | Advanced / Behavior |
| Set State | node | `SetState` | Flow |
| When State Becomes | node | `WhenStateBecomes` | Flow |
| When State Leaves | node | `WhenStateLeaves` | Flow |

##### Loops

| Node | Kind | Id | Was |
|---|---|---|---|
| Break Loop | node | `Break` | Flow / Loops |
| Continue Loop | node | `Continue` | Flow / Loops |
| Count Until | node | `CountUntil` | Flow / Loops |
| Count With Step | node | `ForStep` | Flow / Loops |
| For Each | node | `ForEach` | Flow / Loops |
| For Each in Map | node | `ForEachMap` | Flow / Loops |
| For Loop | node | `ForLoop` | Flow / Loops |
| Repeat | node | `Repeat` | Flow / Loops |
| Repeat Until Success | library | `LbRepeatUntilSuccess` | Advanced / Behavior |
| Repeat While | node | `DoWhile` | Flow / Loops |
| Step Through | node | `StepThrough` | Flow / Loops |
| While | node | `While` | Flow / Loops |

##### Waiting & Timers

| Node | Kind | Id | Was |
|---|---|---|---|
| Blend Over Time | node | `BlendTime` | Flow / Timing |
| Cooldown | node | `Cooldown` | Flow / Timing |
| Cooldown Left | library | `LbCooldownLeft` | Flow / Timing |
| Count Down | node | `Countdown` | Flow / Timing |
| Count True Frames | library | `LbCountTrueFrames` | Flow / Timing |
| Every Few Seconds | node | `EveryWhile` | Flow / Timing |
| Every Number of Times | node | `EveryN` | Flow / Timing |
| Every Seconds | node | `Timer` | Flow / Timing |
| Get Cooldown Portion | gameplay | `GlCooldownPortion` | Math / Progression |
| Get Previous Number | library | `LbPreviousNumber` | Flow / Timing |
| Has Happened Within | node | `HappenedNear` | Flow / Timing |
| Has Held Long Enough | node | `HeldLong` | Flow / Timing |
| Hysteresis Gate | library | `LbHysteresis` | Flow / Timing |
| Is Cooldown Ready | library | `LbIsCooldownReady` | Flow / Timing |
| Is Spike | library | `LbSpike` | Flow / Timing |
| Run Stopwatch | node | `Stopwatch` | Flow / Timing |
| Sample and Hold | library | `LbSampleHold` | Flow / Timing |
| Timer | builtin | `Fn.timer` | Time |
| Wait Between Tries | node | `WaitBetween` | Flow / Timing |
| Wait Once | node | `Wait` | Flow / Timing |
| Wait Seconds | node | `Delay` | Flow / Timing |
| Wait Until | node | `WaitUntil` | Flow / Timing |
| Wait Until Steady | node | `WaitSteady` | Flow / Timing |
| Wait While | node | `WaitWhile` | Flow / Timing |
| Wait With Limit | node | `WaitLimit` | Flow / Timing |

##### Waiting & Timers / Delayed Calls

| Node | Kind | Id | Was |
|---|---|---|---|
| After | builtin | `Fn.after` | Advanced |
| Call Later | builtin | `Fn.call_later` | Advanced |
| Cancel Invoke | builtin | `Fn.CancelInvoke` | Advanced |
| Cancel Timers | builtin | `Fn.cancel_timers` | Advanced |
| Every | builtin | `Fn.every` | Advanced |
| Invoke | builtin | `Fn.Invoke` | Advanced |
| Invoke After | builtin | `Fn.invoke_after` | Advanced |
| Invoke Repeating | builtin | `Fn.InvokeRepeating` | Advanced |
| Repeat Every | builtin | `Fn.repeat_every` | Advanced |
| Start Coroutine | builtin | `Fn.StartCoroutine` | Advanced |
| Stop All Coroutines | builtin | `Fn.StopAllCoroutines` | Advanced |
| Stop Coroutine | builtin | `Fn.StopCoroutine` | Advanced |
| Wait for End of Frame | builtin | `Fn.WaitForEndOfFrame` | Advanced |
| Wait for Fixed Update | builtin | `Fn.WaitForFixedUpdate` | Advanced |
| Wait for Seconds | builtin | `Fn.WaitForSeconds` | Advanced |
| Wait for Seconds Realtime | builtin | `Fn.WaitForSecondsRealtime` | Advanced |

##### Functions & Calls

| Node | Kind | Id | Was |
|---|---|---|---|
| Action | node | `Action` | Flow |
| Action Steps | node | `Actions` | Flow |
| Actions Load | builtin | `Fn.actions_load` | Advanced |
| Call | builtin | `Fn.call` | Advanced |
| Call Function | node | `CallFunction` | Flow / Functions |
| Call Script | node | `CallScript` | Advanced |
| Call Script Event | step | `call` | Flow / Functions |
| Check | node | `Condition` | Flow |
| Check Action | builtin | `Fn.check_action` | Advanced |
| Define Function | node | `FunctionStart` | Flow / Functions |
| Return | node | `Return` | Flow / Functions |
| Run Action | builtin | `Fn.run_action` | Advanced |
| Run Another Graph | node | `RunGraph` | Flow / Functions |
| Run Text Snippet | node | `RunSnippet` | Advanced |

##### Time & Game Speed / Clock

| Node | Kind | Id | Was |
|---|---|---|---|
| Delta Time | node | `DeltaTime` | Time |
| Frame Count | node | `FrameCount` | Time |
| Frame Time | builtin | `Fn.frame_time` | Advanced |
| Frames Per Second | builtin | `Fn.fps` | Advanced |
| Get Frames Per Second | api | `Time.fps` | Time |
| Get Hitch Count | api | `Time.hitchCount` | Time |
| Get Physics Frame Time | api | `Time.fixedDeltaTime` | Time |
| Get Physics Steps | api | `Time.physicsSteps` | Time |
| Get Session Frame Count | api | `Time.sessionFrameCount` | Time |
| Get Session Time | gameplay | `GlGetSessionTime` | Time |
| Get Session Unscaled Time | api | `Time.sessionUnscaledTime` | Time |
| Get Unscaled Delta Time | api | `Time.unscaledDeltaTime` | Time |
| Get Unscaled Time | api | `Time.unscaledTime` | Time |
| Hitch Count | builtin | `Fn.hitch_count` | Advanced |
| Is Smooth Physics | api | `Time.smoothPhysics` | Time |
| Seconds | builtin | `Fn.dt` | Advanced |
| Session Frame Count | builtin | `Fn.session_frame_count` | Advanced |
| Session Time | builtin | `Fn.session_time` | Advanced |
| Session Unscaled Time | builtin | `Fn.session_unscaled_time` | Advanced |
| Set Physics Steps | api | `Time.physicsSteps=` | Time |
| Set Smooth Physics | api | `Time.smoothPhysics=` | Time |
| Time | node | `Time` | Time |
| Time Elapsed | builtin | `Fn.time_elapsed` | Advanced |
| Unscaled Seconds | builtin | `Fn.unscaled_dt` | Advanced |

##### Time & Game Speed / Game Speed

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Game Speed | gameplay | `GlGetGameSpeed` | Time |
| Hit Stop | api | `Time.HitStop/2` | Time |
| Ramp Time Scale | api | `Time.RampTimeScale/2` | Time |
| Ramp Timescale | builtin | `Fn.ramp_timescale` | Time |
| Reset Game Speed | gameplay | `GlResetGameSpeed` | Time |
| Set Time Scale | node | `SetTimeScale` | Time |
| Set Timescale | builtin | `Fn.set_timescale` | Time |
| Slow Motion | api | `Time.SlowMotion/2` | Time |
| Slow Motion for Seconds | gameplay | `GlSlowMotion` | Time |
| Time Hit Stop | builtin | `Fn.Time.HitStop` | Advanced |
| Time Ramp Time Scale | builtin | `Fn.Time.RampTimeScale` | Advanced |
| Time Scale | node | `TimeScale` | Time |
| Time Slow Motion | builtin | `Fn.Time.SlowMotion` | Advanced |
| Timescale | builtin | `Fn.timescale` | Time |

##### Time & Game Speed / Pausing

| Node | Kind | Id | Was |
|---|---|---|---|
| Is Paused | api | `Time.paused` | Time |
| Is Paused by | api | `Time.IsPausedBy/1` | Time |
| Pause / Unpause | step | `toggle_pause` | Time |
| Pause Game | node | `PauseGame` | Scene |
| Pop Pause | api | `Time.PopPause/1` | Time |
| Push Pause | api | `Time.PushPause/1` | Time |
| Resume Game | step | `resume` | Time |
| Time is Paused by | builtin | `Fn.Time.IsPausedBy` | Advanced |
| Time Pop Pause | builtin | `Fn.Time.PopPause` | Advanced |
| Time Push Pause | builtin | `Fn.Time.PushPause` | Advanced |

##### Time & Game Speed / Time Math

| Node | Kind | Id | Was |
|---|---|---|---|
| Beat Number | library | `LbBeatNumber` | Time |
| Elapsed Portion | library | `LbElapsedPortion` | Time |
| Frames to Seconds | library | `LbFramesToSeconds` | Time |
| Has Time Ended | library | `LbIsTimeUp` | Time |
| Has Time Passed | gameplay | `GlHasTimePassed` | Time |
| Is Blinking | library | `LbIsBlinking` | Time |
| Is Every Other Interval | gameplay | `GlIsEveryOtherInterval` | Time |
| Is Pulse Active | library | `LbPulseAt` | Time |
| Loop Time | library | `LbLoopTime` | Time |
| Phase | library | `LbPhase` | Time |
| Ping Time | library | `LbPingTime` | Time |
| Seconds to Frames | library | `LbSecondsToFrames` | Time |
| Time Left | library | `LbTimeLeft` | Time |

##### Variables

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Object Value | builtin | `Fn.get_object_var` | Objects |
| Set Object Value | builtin | `Fn.set_object_var` | Objects |

##### Variables / Change a Variable

| Node | Kind | Id | Was |
|---|---|---|---|
| Approach Value | step | `approach_var` | Variables / Change |
| Clamp 0 to 1 | step | `clamp01_var` | Variables / Change |
| Clamp Variable | step | `clamp_var` | Variables / Change |
| Divide Variable | step | `div_var` | Variables / Change |
| Ease Variable | step | `lerp_var` | Variables / Change |
| Floor Variable | step | `floor_var` | Variables / Change |
| Keep Larger | step | `max_var` | Variables / Change |
| Keep Smaller | step | `min_var` | Variables / Change |
| Multiply Variable | step | `mul_var` | Variables / Change |
| Negate Variable | step | `negate_var` | Variables / Change |
| Percent Into Var | step | `percent_var` | Variables / Change |
| Raise To Power | step | `pow_var` | Variables / Change |
| Random Pick | step | `pick_var` | Variables / Change |
| Random Sign | step | `random_sign_var` | Variables / Change |
| Random Variable | step | `rand_var` | Variables / Change |
| Remainder (Mod) | step | `mod_var` | Variables / Change |
| Remap Variable | step | `map_range_var` | Variables / Change |
| Roll Dice | step | `dice_var` | Variables / Change |
| Round To Step | step | `round_to_var` | Variables / Change |
| Round Variable | step | `round_var` | Variables / Change |
| Sign Of Variable | step | `sign_var` | Variables / Change |
| Swap Variables | step | `swap_var` | Variables / Change |
| Wrap In Range | step | `wrap_var` | Variables / Change |

##### Variables / Check a Variable

| Node | Kind | Id | Was |
|---|---|---|---|
| Compare Two Variables | check | `vars_cmp` | Variables / Compare |
| Variable At Least | check | `var_ge` | Variables / Compare |
| Variable At Most | check | `var_le` | Variables / Compare |
| Variable Divisible By | check | `var_multiple_of` | Variables / Compare |
| Variable Equals | check | `var_eq` | Variables / Compare |
| Variable In Range | check | `var_between` | Variables / Compare |
| Variable Is Above | check | `var_gt` | Variables / Compare |
| Variable Is Below | check | `var_lt` | Variables / Compare |
| Variable Is Even | check | `var_even` | Variables / Compare |
| Variable Is Near | check | `var_near` | Variables / Compare |
| Variable Is Negative | check | `var_negative` | Variables / Compare |
| Variable Is Not | check | `var_neq` | Variables / Compare |
| Variable Is Odd | check | `var_odd` | Variables / Compare |
| Variable Is Positive | check | `var_positive` | Variables / Compare |
| Variable Is Zero | check | `var_zero` | Variables / Compare |

##### Variables / Shared Variables

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Global Var | node | `AddGlobalVar` | Variables / Shared |
| Add Scene Var | node | `AddSceneVar` | Variables / Shared |
| Data Has | builtin | `Fn.data_has` | Advanced |
| Data Number | builtin | `Fn.data_num` | Advanced |
| Data Set | builtin | `Fn.data_set` | Advanced |
| Data Text | builtin | `Fn.data_str` | Text |
| Get | builtin | `Fn.gc_get` | Advanced |
| Get Global Var | node | `GetGlobalVar` | Variables / Shared |
| Get Scene Var | node | `GetSceneVar` | Variables / Shared |
| Set Global Var | node | `SetGlobalVar` | Variables / Shared |
| Set Scene Var | node | `SetSceneVar` | Variables / Shared |

##### Saving & Loading

| Node | Kind | Id | Was |
|---|---|---|---|
| Forget Saved Position | gameplay | `GlForgetSavedPosition` | Variables / Saved |
| Get Remembered Value | node | `RecallValue` | Variables / Saved |
| Has Saved Position | gameplay | `GlHasSavedPosition` | Variables / Saved |
| Load Checkpoint | node | `CheckpointLoad` | Variables / Saved |
| Load Health | gameplay | `GlLoadHealth` | Variables / Saved |
| Load Position | gameplay | `GlLoadPosition` | Variables / Saved |
| Load Value | node | `LoadValue` | Variables / Saved |
| Remember Value | node | `RememberValue` | Variables / Saved |
| Save Checkpoint | node | `CheckpointSave` | Variables / Saved |
| Save Health | gameplay | `GlSaveHealth` | Variables / Saved |
| Save Position | gameplay | `GlSavePosition` | Variables / Saved |
| Save Value | node | `SaveValue` | Variables / Saved |

##### Saving & Loading / Saved Values

| Node | Kind | Id | Was |
|---|---|---|---|
| Add To Saved Value | step | `add_prefs` | Variables / Saved |
| Data Save | builtin | `Fn.data_save` | Variables / Saved |
| Grab A Value | step | `get_prefs` | Variables / Saved |
| Load Number | builtin | `Fn.load_number` | Advanced |
| Load Text | builtin | `Fn.load_text` | Text |
| Prefs Get | builtin | `Fn.prefs_get` | Variables / Saved |
| Prefs Get Text | builtin | `Fn.prefs_get_str` | Variables / Saved |
| Prefs Load | builtin | `Fn.prefs_load` | Variables / Saved |
| Prefs Save | builtin | `Fn.prefs_save` | Variables / Saved |
| Prefs Set | builtin | `Fn.prefs_set` | Variables / Saved |
| Save A Value | step | `set_prefs` | Variables / Saved |
| Saved Value Equals | check | `prefs_eq` | Variables / Saved |
| Saved Value Is Above | check | `prefs_gt` | Variables / Saved |
| Saved Value Is Below | check | `prefs_lt` | Variables / Saved |
| Saved Value Not Equal | check | `prefs_neq` | Variables / Saved |
| Write Save File | step | `save_prefs` | Variables / Saved |

##### Saving & Loading / Save Slots

| Node | Kind | Id | Was |
|---|---|---|---|
| Autosave Now | step | `game_autosave` | Variables / Saved |
| Can Continue | step | `game_can_continue` | Variables / Saved |
| Continue | step | `game_continue` | Variables / Saved |
| Delete | api | `SaveGame.Delete/1` | Variables / Saved |
| Delete Slot | step | `game_delete_slot` | Variables / Saved |
| Exists | api | `SaveGame.Exists/1` | Variables / Saved |
| Game Autosave | builtin | `Fn.game_autosave` | Variables / Saved |
| Game Can Continue | builtin | `Fn.game_can_continue` | Variables / Saved |
| Game Continue | builtin | `Fn.game_continue` | Variables / Saved |
| Game Delete Slot | builtin | `Fn.game_delete_slot` | Variables / Saved |
| Game Load | builtin | `Fn.game_load` | Variables / Saved |
| Game Save | builtin | `Fn.game_save` | Variables / Saved |
| Game Slot Age | builtin | `Fn.game_slot_age` | Variables / Saved |
| Game Slot Exists | builtin | `Fn.game_slot_exists` | Variables / Saved |
| Game Slot Label | builtin | `Fn.game_slot_label` | Variables / Saved |
| Game Slot Text | builtin | `Fn.game_slot_text` | Variables / Saved |
| Game Time | builtin | `Fn.game_time` | Advanced |
| Get Play Time | api | `SaveGame.playTime` | Variables / Saved |
| Label | api | `SaveGame.Label/1` | Variables / Saved |
| Load | api | `SaveGame.Load/1` | Variables / Saved |
| Load Slot | step | `game_load` | Variables / Saved |
| Save | api | `SaveGame.Save/2` | Variables / Saved |
| Save To Slot | step | `game_save` | Variables / Saved |
| Slot Exists | step | `game_slot_exists` | Variables / Saved |
| Slot Saved Ago | step | `game_slot_age` | Variables / Saved |
| Slot Saved Time | step | `game_slot_text` | Variables / Saved |

##### Saving & Loading / Save Files

| Node | Kind | Id | Was |
|---|---|---|---|
| Delete Save | builtin | `Fn.delete_save` | Variables / Saved |
| Has Save | builtin | `Fn.has_save` | Variables / Saved |
| Load Game | builtin | `Fn.load_game` | Advanced |
| Play Time | builtin | `Fn.play_time` | Advanced |
| Read Save | builtin | `Fn.read_save` | Variables / Saved |
| Save Ago | builtin | `Fn.save_ago` | Variables / Saved |
| Save Clear | builtin | `Fn.save_clear` | Variables / Saved |
| Save Date | builtin | `Fn.save_date` | Variables / Saved |
| Save Delete | builtin | `Fn.save_delete` | Variables / Saved |
| Save Delete File | builtin | `Fn.save_delete_file` | Variables / Saved |
| Save Exists | builtin | `Fn.save_exists` | Variables / Saved |
| Save Game | builtin | `Fn.save_game` | Variables / Saved |
| Save Has | builtin | `Fn.save_has` | Variables / Saved |
| Save Label | builtin | `Fn.save_label` | Variables / Saved |
| Save Play Text | builtin | `Fn.save_play_text` | Variables / Saved |
| Save Play Time | builtin | `Fn.save_play_time` | Variables / Saved |
| Save Slots | builtin | `Fn.save_slots` | Variables / Saved |
| Save Valid | builtin | `Fn.save_valid` | Variables / Saved |
| Write Save | builtin | `Fn.write_save` | Variables / Saved |

##### Lists

| Node | Kind | Id | Was |
|---|---|---|---|
| Array Longer Than | check | `array_len_gt` | Lists & Maps / Lists |
| Array Shorter Than | check | `array_len_lt` | Lists & Maps / Lists |
| Find Largest | node | `ListMax` | Lists & Maps / Lists |
| Find Smallest | node | `ListMin` | Lists & Maps / Lists |
| List Add | node | `ListAdd` | Lists & Maps / Lists |
| List Append | node | `ListAppend` | Lists & Maps / Lists |
| List Clear | node | `ListClear` | Lists & Maps / Lists |
| List Contains | node | `ListContains` | Lists & Maps / Lists |
| List Copy | node | `ListCopy` | Lists & Maps / Lists |
| List Find | node | `ListFind` | Lists & Maps / Lists |
| List Get | node | `ListGet` | Lists & Maps / Lists |
| List Insert | node | `ListInsert` | Lists & Maps / Lists |
| List Length | node | `ListLength` | Lists & Maps / Lists |
| List Pop | node | `ListPop` | Lists & Maps / Lists |
| List Remove At | node | `ListRemoveAt` | Lists & Maps / Lists |
| List Remove Item | node | `ListRemoveItem` | Lists & Maps / Lists |
| List Reverse | node | `ListReverse` | Lists & Maps / Lists |
| List Set | node | `ListSet` | Lists & Maps / Lists |
| List Slice | node | `ListSlice` | Lists & Maps / Lists |
| List Sort | node | `ListSort` | Lists & Maps / Lists |
| New List | node | `NewList` | Lists & Maps / Lists |
| Shuffle List | node | `ShuffleList` | Lists & Maps / Lists |

##### Lists / Sorting & Searching

| Node | Kind | Id | Was |
|---|---|---|---|
| All | builtin | `Fn.all_fn` | Advanced |
| Any | builtin | `Fn.any_fn` | Advanced |
| Filter | builtin | `Fn.filter_fn` | Advanced |
| Index of | builtin | `Fn.index_of` | Advanced |
| Reduce | builtin | `Fn.reduce_fn` | Advanced |
| Reverse | builtin | `Fn.reverse` | Advanced |
| Shuffle | builtin | `Fn.shuffle` | Advanced |
| Sort Number | builtin | `Fn.sort_num` | Advanced |
| Sort Text | builtin | `Fn.sort_str` | Text |

##### Lists / Array Functions

| Node | Kind | Id | Was |
|---|---|---|---|
| Array | builtin | `Fn.array` | Lists & Maps / Lists |
| Array Clear | step | `array_clear` | Lists & Maps / Lists |
| Array Contains | check | `array_has` | Lists & Maps / Lists |
| Array Get | step | `array_get` | Lists & Maps / Lists |
| Array Length | step | `array_len` | Lists & Maps / Lists |
| Array Pop | step | `array_pop` | Lists & Maps / Lists |
| Array Push | step | `array_push` | Lists & Maps / Lists |
| Array Set | step | `array_set` | Lists & Maps / Lists |
| First | builtin | `Fn.first` | Advanced |
| Insert at | builtin | `Fn.insert_at` | Advanced |
| Last | builtin | `Fn.last` | Advanced |
| New Array | builtin | `Fn.__new_array` | Lists & Maps / Lists |
| Pop | builtin | `Fn.pop` | Advanced |
| Range | builtin | `Fn.range` | Advanced |
| Remove at | builtin | `Fn.remove_at` | Advanced |
| Slice | builtin | `Fn.slice` | Advanced |

##### Maps & Structs

| Node | Kind | Id | Was |
|---|---|---|---|
| From Json | builtin | `Fn.from_json` | Advanced |
| Get Field | node | `StructGet` | Lists & Maps / Structs |
| Json Parse | builtin | `Fn.json_parse` | Advanced |
| Json Stringify | builtin | `Fn.json_stringify` | Advanced |
| Map | builtin | `Fn.map` | Lists & Maps / Lists |
| Map Clear | node | `MapClear` | Lists & Maps / Maps |
| Map Copy | node | `MapCopy` | Lists & Maps / Maps |
| Map Count | node | `MapCount` | Lists & Maps / Maps |
| Map Delete Key | step | `map_del` | Lists & Maps / Maps |
| Map Get | node | `MapGet` | Lists & Maps / Maps |
| Map Get Or | node | `MapGetOr` | Lists & Maps / Maps |
| Map Has | node | `MapHas` | Lists & Maps / Maps |
| Map Has Key | check | `map_has` | Lists & Maps / Maps |
| Map Keys | node | `MapKeys` | Lists & Maps / Maps |
| Map Merge | node | `MapMerge` | Lists & Maps / Maps |
| Map Remove | node | `MapRemove` | Lists & Maps / Maps |
| Map Set | node | `MapSet` | Lists & Maps / Maps |
| Map Size | step | `map_size` | Lists & Maps / Maps |
| Map Values | node | `MapValues` | Lists & Maps / Maps |
| New Map | node | `NewMap` | Lists & Maps / Maps |
| New Struct | node | `StructNew` | Lists & Maps / Structs |
| Set Field | node | `StructSet` | Lists & Maps / Structs |
| To Json | builtin | `Fn.to_json` | Advanced |

##### Numbers

| Node | Kind | Id | Was |
|---|---|---|---|
| Abs | node | `Abs` | Math |
| Add (+) | node | `Add` | Math |
| Div (/) | node | `Div` | Math |
| Exp | node | `Exp` | Math |
| Log (ln) | node | `Log` | Math |
| Max | node | `Max` | Math |
| Min | node | `Min` | Math |
| Mod (%) | node | `Mod` | Math |
| Mul (x) | node | `Mul` | Math |
| Negate | node | `Neg` | Math |
| One Minus | node | `OneMinus` | Math |
| Pow (^) | node | `Pow` | Math |
| Sign | node | `Sign` | Math |
| Sqrt | node | `Sqrt` | Math |
| Sub (-) | node | `Sub` | Math |

##### Numbers / Rounding & Snapping

| Node | Kind | Id | Was |
|---|---|---|---|
| Ceil | node | `Ceil` | Math |
| Ceil to Step | library | `LbCeilTo` | Math |
| Floor | node | `Floor` | Math |
| Floor to Step | library | `LbFloorTo` | Math |
| Frac | builtin | `Fn.frac` | Advanced |
| Fract | builtin | `Fn.fract` | Advanced |
| Fraction | node | `Frac` | Math |
| Mathf Ceil | builtin | `Fn.Mathf.Ceil` | Advanced |
| Mathf Ceil to Number | builtin | `Fn.Mathf.CeilToInt` | Advanced |
| Mathf Floor | builtin | `Fn.Mathf.Floor` | Advanced |
| Mathf Floor to Number | builtin | `Fn.Mathf.FloorToInt` | Advanced |
| Mathf Round | builtin | `Fn.Mathf.Round` | Advanced |
| Mathf Round to Number | builtin | `Fn.Mathf.RoundToInt` | Advanced |
| Nearest Multiple | library | `LbNearestMultiple` | Math |
| Round | node | `Round` | Math |
| Round Places | library | `LbRoundPlaces` | Math |
| Snap | builtin | `Fn.snap` | Advanced |
| Snap To Step | node | `Snap` | Math |
| Truncate Toward Zero | library | `LbTruncZero` | Math |

##### Numbers / Limits & Wrapping

| Node | Kind | Id | Was |
|---|---|---|---|
| Apply Dead Zone | library | `LbDeadZone` | Math |
| Clamp | node | `Clamp` | Math |
| Clamp 0-1 | node | `Saturate` | Math |
| Clamp Range | library | `LbClampRange` | Math |
| Clamp Signed One | library | `LbClamp11` | Math |
| Fold in Range | library | `LbFoldRange` | Math |
| Hold Range | library | `LbHoldRange` | Math |
| Keep at Least | library | `LbKeepAtLeast` | Math |
| Keep at Most | library | `LbKeepAtMost` | Math |
| Keep Between Zero and One | api | `Mathf.Clamp01/1` | Math |
| Keep Positive | library | `LbKeepPositive` | Math |
| Math Repeat | builtin | `Fn.math_repeat` | Math |
| Mathf Clamp | builtin | `Fn.Mathf.Clamp` | Math |
| Mathf Ping Pong | builtin | `Fn.Mathf.PingPong` | Advanced |
| Mathf Repeat | builtin | `Fn.Mathf.Repeat` | Advanced |
| Ping Pong | builtin | `Fn.ping_pong` | Advanced |
| Ping-Pong | node | `PingPong` | Math |
| Repeat (wrap) | node | `Repeat01` | Math |
| Repeat in Range | library | `LbRepeatRange` | Math |
| Saturate Number | library | `LbSaturateNumber` | Math |
| Snap Pair | library | `LbSnapPair` | Math |
| Soft Clamp | library | `LbSoftClamp` | Math |
| Wrap | node | `Wrap` | Math |
| Wrap Pair | library | `LbWrapPair` | Math |

##### Numbers / More Arithmetic

| Node | Kind | Id | Was |
|---|---|---|---|
| Absolute Difference | library | `LbAbsDiff` | Math |
| Choose Number | library | `LbChooseNumber` | Math |
| Cube | library | `LbCube` | Math |
| Exponential | library | `LbRaiseE` | Math |
| Fraction of | library | `LbFractionOf` | Math |
| Gap Below One | library | `LbOneMinus` | Math |
| Hypotenuse | library | `LbHypotenuse` | Math |
| Natural Log | library | `LbNaturalLog` | Math |
| Positive Remainder | library | `LbPositiveRemainder` | Math |
| Safe Reciprocal | library | `LbSafeReciprocal` | Math |
| Select Larger | library | `LbSelectLarger` | Math |
| Select Smaller | library | `LbSelectSmaller` | Math |
| Sign of | library | `LbSignOf` | Math |
| Square | library | `LbSquare` | Math |

##### Numbers / Averages & Percentages

| Node | Kind | Id | Was |
|---|---|---|---|
| Accumulate | library | `LbAccumulate` | Math |
| Average | node | `Average` | Math |
| Avg | builtin | `Fn.avg` | Advanced |
| Median of Three | library | `LbMedianThree` | Math |
| Percent of | library | `LbPercentOf` | Math |
| Remember Peak | library | `LbRememberPeak` | Math |
| Remember Valley | library | `LbRememberValley` | Math |
| Running Mean | library | `LbRunningMean` | Math |
| Scale Around | library | `LbScaleAround` | Math |
| Spread Around | library | `LbSpread` | Math |
| Value From Percent | library | `LbValueFromPercent` | Math |
| Weighted Mean | library | `LbWeightedMean` | Math |

##### Numbers / Bits

| Node | Kind | Id | Was |
|---|---|---|---|
| Bit and | builtin | `Fn.bit_and` | Advanced |
| Bit Clear | builtin | `Fn.bit_clear` | Advanced |
| Bit Not | builtin | `Fn.bit_not` | Advanced |
| Bit or | builtin | `Fn.bit_or` | Advanced |
| Bit Set | builtin | `Fn.bit_set` | Advanced |
| Bit Test | builtin | `Fn.bit_test` | Advanced |
| Bit Toggle | builtin | `Fn.bit_toggle` | Advanced |
| Bit Xor | builtin | `Fn.bit_xor` | Advanced |
| Shl | builtin | `Fn.shl` | Advanced |
| Shr | builtin | `Fn.shr` | Advanced |

##### Numbers / Types & Checks

| Node | Kind | Id | Was |
|---|---|---|---|
| Is Array | builtin | `Fn.is_array` | Lists & Maps / Lists |
| Is Finite | builtin | `Fn.is_finite` | Advanced |
| Is Map | builtin | `Fn.is_map` | Lists & Maps / Lists |
| Is Not a Number | builtin | `Fn.is_nan` | Advanced |
| Is Number | builtin | `Fn.is_num` | Advanced |
| Is Text | builtin | `Fn.is_str` | Text |
| Is True or False | builtin | `Fn.is_bool` | Advanced |
| Typeof | builtin | `Fn.typeof` | Advanced |

##### Numbers / Script Math Functions

| Node | Kind | Id | Was |
|---|---|---|---|
| Hypot | builtin | `Fn.hypot` | Advanced |
| Largest | builtin | `Fn.max` | Math |
| Largest of | builtin | `Fn.max_of` | Math |
| Mathf Absolute Value | builtin | `Fn.Mathf.Abs` | Math |
| Mathf Exp | builtin | `Fn.Mathf.Exp` | Advanced |
| Mathf Largest | builtin | `Fn.Mathf.Max` | Math |
| Mathf Log | builtin | `Fn.Mathf.Log` | Debug |
| Mathf Power | builtin | `Fn.Mathf.Pow` | Math |
| Mathf Sign | builtin | `Fn.Mathf.Sign` | Advanced |
| Mathf Smallest | builtin | `Fn.Mathf.Min` | Math |
| Mathf Square Root | builtin | `Fn.Mathf.Sqrt` | Math |
| Mod | builtin | `Fn.mod` | Advanced |
| Power | builtin | `Fn.pow` | Math |
| Smallest | builtin | `Fn.min` | Math |
| Smallest of | builtin | `Fn.min_of` | Math |
| Sum | builtin | `Fn.sum` | Advanced |

##### Blending & Easing

| Node | Kind | Id | Was |
|---|---|---|---|
| Blend Clamped | library | `LbLerpClamp` | Math / Curves |
| Blend Unclamped | library | `LbLerpUnclamped` | Math / Curves |
| Change Between | library | `LbChangeBetween` | Math |
| Inverse Blend | library | `LbLerpInverse` | Math / Curves |
| Inverse Blend Between | builtin | `Fn.inverse_lerp` | Math |
| Inverse Blend Between Value | builtin | `Fn.inverse_lerp_value` | Math |
| Inverse Lerp | node | `InverseLerp` | Math |
| Lerp | node | `Lerp` | Math |
| Map Bipolar | library | `LbMapBipolar` | Math |
| Map Range | library | `LbMapRange` | Math / Curves |
| Map Signed | library | `LbMapSigned` | Math |
| Mathf Blend Between | builtin | `Fn.Mathf.Lerp` | Math |
| Mathf Inverse Blend Between | builtin | `Fn.Mathf.InverseLerp` | Advanced |
| Mathf Move Towards | builtin | `Fn.Mathf.MoveTowards` | Advanced |
| Mix Three | library | `LbMixThree` | Math / Curves |
| Move Towards (value) | node | `MoveTowardsValue` | Math |
| Portion Along | library | `LbPortionAlong` | Math / Curves |
| Portion Unclamped | library | `LbInverseLerpUnclamped` | Math / Curves |
| Remap | node | `Remap` | Math |
| Remap Clamped | library | `LbRemapClamped` | Math / Curves |

##### Blending & Easing / Easing & Curves

| Node | Kind | Id | Was |
|---|---|---|---|
| Bias Curve | library | `LbBiasCurve` | Math / Curves |
| Bounce Out | library | `LbBounceOut` | Math / Curves |
| Circle Ease in | library | `LbCircleEaseIn` | Math / Curves |
| Circle Ease Out | library | `LbCircleEaseOut` | Math / Curves |
| Ease Back | builtin | `Fn.ease_back` | Advanced |
| Ease Bounce | builtin | `Fn.ease_bounce` | Advanced |
| Ease Curve | builtin | `Fn.ease_curve` | Advanced |
| Ease Elastic | builtin | `Fn.ease_elastic` | Advanced |
| Ease In | node | `EaseIn` | Math / Curves |
| Ease in | library | `LbEaseIn` | Math / Curves |
| Ease in Cubic | builtin | `Fn.ease_in_cubic` | Advanced |
| Ease in Out | library | `LbEaseInOut` | Math / Curves |
| Ease in Out Cubic | builtin | `Fn.ease_in_out_cubic` | Advanced |
| Ease In-Out | node | `EaseInOut` | Math / Curves |
| Ease Out | node | `EaseOut` | Math / Curves |
| Ease Out Cubic | builtin | `Fn.ease_out_cubic` | Advanced |
| Ease Out Portion | library | `LbEaseOut` | Math / Curves |
| Ease With Curve | node | `EaseCurve` | Math / Curves |
| Exponential Ease in | library | `LbExpEaseIn` | Math / Curves |
| Exponential Ease Out | library | `LbExpEaseOut` | Math / Curves |
| Gain Curve | library | `LbGainCurve` | Math / Curves |
| Logistic Curve | library | `LbLogistic` | Math / Curves |
| Mathf Smooth Step | builtin | `Fn.Mathf.SmoothStep` | Advanced |
| Power Ease | library | `LbPowerEase` | Math / Curves |
| Response Curve | library | `LbResponseCurve` | Math / Curves |
| Sample Bezier | library | `LbSampleBezier` | Math / Curves |
| Sample Curve | library | `LbSampleCurve` | Math / Curves |
| Sample Hermite | library | `LbHermite` | Math / Curves |
| Sample Noise | library | `LbSampleNoise` | Math / Curves |
| Saw Wave | library | `LbSawWave` | Math / Curves |
| Sine Wave | library | `LbSineWave` | Math / Curves |
| Smooth Portion | library | `LbSmoothStep` | Math / Curves |
| Smooth Step | node | `SmoothStep` | Math / Curves |
| Smoother Step | library | `LbSmootherStep` | Math / Curves |
| Smoothstep | builtin | `Fn.smoothstep` | Advanced |
| Square Wave | library | `LbSquareWave` | Math / Curves |
| Triangle Wave | library | `LbTriangleWave` | Math / Curves |

##### Blending & Easing / Smoothing & Springs

| Node | Kind | Id | Was |
|---|---|---|---|
| Approach | library | `LbApproach` | Math / Curves |
| Damp Toward | library | `LbDampToward` | Math / Curves |
| Ease Toward | node | `EaseToward` | Math / Curves |
| Exponential Decay | library | `LbExponentialDecay` | Math |
| Inverse Square Falloff | library | `LbInverseSquare` | Math |
| Limit Rate | library | `LbLimitRate` | Math / Curves |
| Linear Falloff | library | `LbLinearFalloff` | Math |
| Smooth Damp Step | library | `LbSmoothDampStep` | Math / Curves |
| Smooth Falloff | library | `LbSmoothFalloff` | Math |
| Smooth Larger | library | `LbSmoothLarger` | Math |
| Smooth Smaller | library | `LbSmoothSmaller` | Math |
| Spring Step | node | `SpringStep` | Math / Curves |
| Spring Toward Target | library | `LbSpringStep` | Math / Curves |

##### Angles & Trigonometry

| Node | Kind | Id | Was |
|---|---|---|---|
| Acos | node | `Acos` | Math / Angles |
| Arc Tangent | builtin | `Fn.atan` | Advanced |
| Asin | node | `Asin` | Math / Angles |
| Atan2 | node | `Atan2` | Math / Angles |
| Cos | node | `Cos` | Math / Angles |
| Deg to Rad | node | `Deg2Rad` | Math / Angles |
| Degrees Radians | builtin | `Fn.deg2rad` | Advanced |
| Mathf Acos | builtin | `Fn.Mathf.Acos` | Advanced |
| Mathf Arc Tangent | builtin | `Fn.Mathf.Atan` | Advanced |
| Mathf Asin | builtin | `Fn.Mathf.Asin` | Advanced |
| Mathf Cosine | builtin | `Fn.Mathf.Cos` | Math |
| Mathf Sine | builtin | `Fn.Mathf.Sin` | Math |
| Mathf Tangent | builtin | `Fn.Mathf.Tan` | Math |
| Rad to Deg | node | `Rad2Deg` | Math / Angles |
| Radians Degrees | builtin | `Fn.rad2deg` | Advanced |
| Sin | node | `Sin` | Math / Angles |
| Tan | node | `Tan` | Math / Angles |

##### Angles & Trigonometry / Angle Helpers

| Node | Kind | Id | Was |
|---|---|---|---|
| Angle From Pair | library | `LbAngleFromPair` | Math / Angles |
| Angle to | builtin | `Fn.angle_to` | Advanced |
| Approach Angle | library | `LbApproachAngle` | Math / Angles |
| Blend Angles | library | `LbBlendAngles` | Math / Angles |
| Blend Between Angle | builtin | `Fn.lerp_angle` | Math |
| Delta Angle | node | `DeltaAngle` | Math / Angles |
| Direction From Angle | library | `LbDirectionFromAngle` | Math / Angles |
| Mathf Blend Between Angle | builtin | `Fn.Mathf.LerpAngle` | Advanced |
| Mathf Delta Angle | builtin | `Fn.Mathf.DeltaAngle` | Advanced |
| Shortest Angle | library | `LbShortestAngle` | Math / Angles |
| Snap Angle | library | `LbSnapAngle` | Math / Angles |
| Up From Angle | library | `LbUpFromAngle` | Math / Angles |
| Vector Angle | builtin | `Fn.Vector2.Angle` | Advanced |
| Wrap Angle | library | `LbWrapAngle` | Math / Angles |

##### Vectors

| Node | Kind | Id | Was |
|---|---|---|---|
| Cross | api | `Vector3.Cross/2` | Math / Vectors |
| Distance | api | `Vector3.Distance/2` | Math / Vectors |
| Distance 2D | node | `Distance2D` | Math / Vectors |
| Dot | api | `Vector3.Dot/2` | Math / Vectors |
| Get Length | api | `Vector3.magnitude` | Math / Vectors |
| Get Unit Direction | api | `Vector3.normalized` | Math / Vectors |
| Make Vec3 | node | `MakeVec3` | Math / Vectors |
| Vec X | node | `VecX` | Math / Vectors |
| Vec Y | node | `VecY` | Math / Vectors |
| Vec Z | node | `VecZ` | Math / Vectors |
| Vec3 Add | node | `Vec3Add` | Math / Vectors |
| Vec3 Cross | node | `Vec3Cross` | Math / Vectors |
| Vec3 Distance | node | `Vec3Distance` | Math / Vectors |
| Vec3 Dot | node | `Vec3Dot` | Math / Vectors |
| Vec3 Length | node | `Vec3Length` | Math / Vectors |
| Vec3 Lerp | node | `Vec3Lerp` | Math / Vectors |
| Vec3 Negate | node | `Vec3Negate` | Math / Vectors |
| Vec3 Normalize | node | `Vec3Normalize` | Math / Vectors |
| Vec3 Scale | node | `Vec3Scale` | Math / Vectors |
| Vec3 Subtract | node | `Vec3Sub` | Math / Vectors |

##### Vectors / Directions & Axes

| Node | Kind | Id | Was |
|---|---|---|---|
| Forward Forward | builtin | `Fn.forward_z` | Advanced |
| Forward Right | builtin | `Fn.forward_x` | Advanced |
| Forward Vector | node | `ForwardVector` | Math / Vectors |
| Forward Vertical | builtin | `Fn.forward_y` | Advanced |
| Get Back Direction | api | `Vector3.back` | Math / Vectors |
| Get Down Direction | api | `Vector3.down` | Math / Vectors |
| Get Left Direction | api | `Vector3.left` | Math / Vectors |
| Get One Vector | api | `Vector3.one` | Math / Vectors |
| Get Zero Vector | api | `Vector3.zero` | Math / Vectors |
| Right Forward | builtin | `Fn.right_z` | Advanced |
| Right Right | builtin | `Fn.right_x` | Advanced |
| Right Vector | node | `RightVector` | Math / Vectors |
| Right Vertical | builtin | `Fn.right_y` | Advanced |
| Up Vector | node | `UpVector` | Math / Vectors |

##### Vectors / Flat Vectors

| Node | Kind | Id | Was |
|---|---|---|---|
| Ahead After Limit | library | `LbAheadLimited` | Math / Vectors |
| Cell Center | library | `LbCellCenter` | Math / Vectors |
| Closest on Segment | library | `LbClosestOnSegment` | Math / Vectors |
| Cross Flat | library | `LbCrossFlat` | Math / Vectors |
| Distance to Segment | library | `LbDistanceToSegment` | Math / Vectors |
| Dot Flat | library | `LbDotFlat` | Math / Vectors |
| Flat Distance | library | `LbDistanceFlat` | Math / Vectors |
| Flat Length | library | `LbFlatLength` | Math / Vectors |
| Grid Cell | library | `LbGridCell` | Math / Vectors |
| Limit Flat Length | library | `LbLimitFlat` | Math / Vectors |
| Midpoint | library | `LbMidpoint` | Math / Vectors |
| Midpoint Ahead | library | `LbMidAhead` | Math / Vectors |
| Offset Across | library | `LbOffsetAcross` | Math / Vectors |
| Offset Ahead | library | `LbOffsetAhead` | Math / Vectors |
| Portion Facing | library | `LbPortionFacing` | Math / Vectors |
| Project Flat | library | `LbProjectFlat` | Math / Vectors |
| Reflect Ahead | library | `LbReflectAhead` | Math / Vectors |
| Reflect Flat | library | `LbReflectFlat` | Math / Vectors |
| Squared Distance | library | `LbSquaredDistance` | Math / Vectors |
| Vertical Gap | library | `LbVerticalGap` | Math / Vectors |

##### Vectors / Script Vector Functions

| Node | Kind | Id | Was |
|---|---|---|---|
| Vector | builtin | `Fn.Vector2` | Advanced |
| Vector Add | builtin | `Fn.vec_add` | Math |
| Vector Blend Between | builtin | `Fn.Vector2.Lerp` | Math |
| Vector Blend Between Unclamped | builtin | `Fn.Vector3.LerpUnclamped` | Advanced |
| Vector Clamp Length | builtin | `Fn.Vector3.ClampMagnitude` | Advanced |
| Vector Cross | builtin | `Fn.Vector3.Cross` | Advanced |
| Vector Distance | builtin | `Fn.Vector2.Distance` | Advanced |
| Vector Dot | builtin | `Fn.Vector3.Dot` | Advanced |
| Vector Forward | builtin | `Fn.vec_z` | Math |
| Vector Length | builtin | `Fn.vec_length` | Math |
| Vector Move Towards | builtin | `Fn.Vector3.MoveTowards` | Advanced |
| Vector Normalize | builtin | `Fn.Vector3.Normalize` | Advanced |
| Vector Reflect | builtin | `Fn.Vector3.Reflect` | Advanced |
| Vector Right | builtin | `Fn.vec_x` | Math |
| Vector Scale | builtin | `Fn.vec_scale` | Math |
| Vector Sub | builtin | `Fn.vec_sub` | Math |
| Vector Vertical | builtin | `Fn.vec_y` | Math |

##### Comparisons & Logic

| Node | Kind | Id | Was |
|---|---|---|---|
| Approx Equal | node | `Approx` | Math / Compare |
| Approximately | builtin | `Fn.approximately` | Advanced |
| Between | node | `Between` | Math / Compare |
| Compare | node | `Compare` | Math / Compare |
| Equals | node | `Equals` | Math / Compare |
| Has Opposite Sign | library | `LbHasOppositeSign` | Math / Compare |
| Has Same Sign | library | `LbHasSameSign` | Math / Compare |
| Is Almost Zero | library | `LbIsAlmostZero` | Math / Compare |
| Is Between Pair | library | `LbIsBetweenPair` | Math / Compare |
| Is Even | node | `IsEven` | Math / Compare |
| Is Far | library | `LbIsFar` | Math / Compare |
| Is Mostly True | node | `IsMostly` | Math / Compare |
| Is Multiple | library | `LbIsMultiple` | Math / Compare |
| Is Nearly | library | `LbIsNearly` | Math / Compare |
| Is Negative | library | `LbIsNegative` | Math / Compare |
| Is Odd | node | `IsOdd` | Math / Compare |
| Is Outside Range | library | `LbIsOutside` | Math / Compare |
| Is Positive | library | `LbIsPositive` | Math / Compare |
| Is Slightly True | node | `IsSlightly` | Math / Compare |
| Is Strictly Between | library | `LbIsStrictBetween` | Math / Compare |
| Is Valid | node | `IsValid` | Math / Compare |
| Is Very True | node | `IsVery` | Math / Compare |
| Is Whole Number | library | `LbIsWhole` | Math / Compare |
| Mathf Approximately | builtin | `Fn.Mathf.Approximately` | Advanced |
| Not Equals | node | `NotEquals` | Math / Compare |
| Step at | library | `LbStepAt` | Math / Compare |

##### Comparisons & Logic / True & False Logic

| Node | Kind | Id | Was |
|---|---|---|---|
| All True | node | `AllTrue` | Math / Compare |
| And | node | `And` | Math / Compare |
| Any True | node | `AnyTrue` | Math / Compare |
| First Valid | node | `FirstValid` | Math / Compare |
| Implication Holds | library | `LbImplies` | Math / Compare |
| Is Every Flag False | library | `LbNorThree` | Math / Compare |
| Is Not All True | library | `LbNandThree` | Math / Compare |
| Is Odd Count True | library | `LbXorThree` | Math / Compare |
| Not | node | `Not` | Math / Compare |
| Not and | node | `NotAnd` | Math / Compare |
| Not or | node | `NotOr` | Math / Compare |
| Or | node | `Or` | Math / Compare |
| Pick by Index | node | `PickByIndex` | Math / Compare |
| Select | node | `Select` | Math / Compare |
| Xor | node | `Xor` | Math / Compare |

##### Comparisons & Logic / Scores & Number Sets

| Node | Kind | Id | Was |
|---|---|---|---|
| Closest Score | library | `LbClosestScore` | Math / Compare |
| Count Scores Above | library | `LbScoresAbove` | Math / Compare |
| Greater Gap | library | `LbGreaterGap` | Math / Compare |
| Is Any Number Set | library | `LbAnyNumber` | Math / Compare |
| Is Every Number Set | library | `LbAllNumbers` | Math / Compare |
| Is Every Number Zero | library | `LbNoneNumber` | Math / Compare |
| Normalize Score | library | `LbNormalizeScore` | Math / Compare |
| Pick Best Score | node | `PickBest` | Math / Compare |
| Pick Worst Score | library | `LbPickWorst` | Math / Compare |
| Score Gap | library | `LbScoreGap` | Math / Compare |
| Weigh and Pick | library | `LbWeighPick` | Math / Compare |

##### Random

| Node | Kind | Id | Was |
|---|---|---|---|
| Chance | node | `Chance` | Math / Random |
| Chance This Second | node | `ChanceEachSecond` | Math / Random |
| Choose | builtin | `Fn.choose` | Advanced |
| Draw Without Repeating | node | `ShuffleBag` | Math / Random |
| Mathf Perlin Noise | builtin | `Fn.Mathf.PerlinNoise` | Advanced |
| Noise | builtin | `Fn.noise` | Advanced |
| Perlin Noise | node | `Noise` | Math / Random |
| Pick | builtin | `Fn.pick` | Advanced |
| Pick Random Item | node | `RandomPick` | Math / Random |
| Pick Random Point | node | `RandomPoint` | Math / Random |
| Rand | builtin | `Fn.rand` | Advanced |
| Randi | builtin | `Fn.randi` | Advanced |
| Random | builtin | `Fn.random` | Math |
| Random 0-1 | node | `Random` | Math / Random |
| Random Chance | builtin | `Fn.random_chance` | Math |
| Random Int | node | `RandomInt` | Math / Random |
| Random Number | builtin | `Fn.random_float` | Math |
| Random Range | node | `RandomRange` | Math / Random |
| Roll Chance | gameplay | `GlRollChance` | Math / Progression |

##### Text

| Node | Kind | Id | Was |
|---|---|---|---|
| Join | builtin | `Fn.join` | Advanced |

##### Text / Search & Replace

| Node | Kind | Id | Was |
|---|---|---|---|
| Contains | node | `Contains` | Text |
| Contains Loose | library | `LbContainsLoose` | Text |
| Count Matches | library | `LbCountMatches` | Text |
| Ends With | node | `EndsWith` | Text |
| Find Text | library | `LbFindText` | Text |
| Has Prefix | library | `LbHasPrefix` | Text |
| Has Suffix | library | `LbHasSuffix` | Text |
| Index Of | node | `IndexOf` | Text |
| Replace | builtin | `Fn.replace` | Advanced |
| Replace All | library | `LbReplaceAll` | Text |
| Replace First Match | library | `LbReplaceText` | Text |
| Replace Text | node | `Replace` | Text |
| Starts With | node | `StartsWith` | Text |
| Text Contains | builtin | `Fn.str_contains` | Text |
| Text Ends With | check | `str_ends` | Text |
| Text Find | builtin | `Fn.str_find` | Objects |
| Text Replace | step | `str_replace` | Text |
| Text Starts With | check | `str_starts` | Text |

##### Text / Pieces & Words

| Node | Kind | Id | Was |
|---|---|---|---|
| After Separator | library | `LbAfterSeparator` | Text |
| Before Separator | library | `LbBeforeSeparator` | Text |
| Char at | builtin | `Fn.char_at` | Advanced |
| Count Letters | library | `LbCountLetters` | Text |
| Count Words | library | `LbCountWords` | Text |
| Cut Text | library | `LbCutText` | Text |
| First Word | library | `LbFirstWord` | Text |
| Last Word | library | `LbLastWord` | Text |
| Letter at | library | `LbLetterAt` | Text |
| Split | builtin | `Fn.split` | Advanced |
| Substr | builtin | `Fn.substr` | Advanced |
| Substring | node | `Substring` | Text |
| Text Length | node | `TextLength` | Text |
| Text Substring | step | `str_sub` | Text |

##### Text / Clean Up & Padding

| Node | Kind | Id | Was |
|---|---|---|---|
| Capitalize | builtin | `Fn.capitalize` | Advanced |
| Ensure Prefix | library | `LbEnsurePrefix` | Text |
| Ensure Suffix | library | `LbEnsureSuffix` | Text |
| Fill Blanks | library | `LbFillBlanks` | Text |
| Limit Text Length | library | `LbClampText` | Text |
| Lower | builtin | `Fn.lower` | Advanced |
| Lower Case | node | `Lower` | Text |
| Lowercase Text | library | `LbLowerText` | Text |
| Pad Left | library | `LbPadLeft` | Text |
| Pad Right | library | `LbPadRight` | Text |
| Remove Spaces | library | `LbRemoveSpaces` | Text |
| Repeat Text | library | `LbRepeatText` | Text |
| Reverse Text | library | `LbReverseText` | Text |
| Text Lowercase | step | `str_lower` | Text |
| Text Repeat | builtin | `Fn.str_repeat` | Text |
| Text Reverse | builtin | `Fn.str_reverse` | Text |
| Text Trim | step | `str_trim` | Text |
| Text Uppercase | step | `str_upper` | Text |
| Title Case | builtin | `Fn.title_case` | Advanced |
| Trim | builtin | `Fn.trim` | Advanced |
| Trim End | builtin | `Fn.trim_end` | Advanced |
| Trim Ends | library | `LbTrimEnds` | Text |
| Trim Spaces | node | `Trim` | Text |
| Trim Start | builtin | `Fn.trim_start` | Advanced |
| Upper | builtin | `Fn.upper` | Advanced |
| Upper Case | node | `Upper` | Text |
| Uppercase Text | library | `LbUpperText` | Text |

##### Text / Compare & Check

| Node | Kind | Id | Was |
|---|---|---|---|
| Equals Loose | library | `LbEqualLoose` | Text |
| Has Text | library | `LbHasText` | Text |
| Is Blank | library | `LbIsBlank` | Text |
| Is Same Text | library | `LbIsSameText` | Text |
| Longer Text | library | `LbLongerText` | Text |
| Text Equals | check | `str_eq` | Text |
| Text Is Empty | check | `str_empty` | Text |
| Text Not Equals | check | `str_neq` | Text |

##### Text / Numbers & Text

| Node | Kind | Id | Was |
|---|---|---|---|
| Convert to Number | builtin | `Fn.Convert.ToInt32` | Advanced |
| Convert to Single | builtin | `Fn.Convert.ToSingle` | Advanced |
| Double Parse | builtin | `Fn.double.Parse` | Advanced |
| Format Number | node | `ToFixed` | Text |
| Hex | builtin | `Fn.hex` | Advanced |
| Is Number Text | library | `LbIsNumberText` | Text |
| Number | builtin | `Fn.num` | Advanced |
| Number From Text | library | `LbNumberFromText` | Text |
| Number Parse | builtin | `Fn.float.Parse` | Advanced |
| Parse Number | builtin | `Fn.parse_int` | Advanced |
| Text From Number | library | `LbTextFromNumber` | Text |
| Text To Number | step | `str_to_num` | Text |
| To Fixed | builtin | `Fn.to_fixed` | Advanced |
| To Number | node | `ToNumber` | Text |
| To Text | node | `ToString` | Text |

##### Text / Clipboard

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Clipboard Text | builtin | `Fn.get_clipboard_text` | Text |
| Set Clipboard Text | builtin | `Fn.set_clipboard_text` | Text |

##### Language & Translation

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Language | node | `GetLanguage` | Text / Language |
| Get Languages | api | `Localization.Languages` | Text / Language |
| Language Name | api | `Localization.LanguageName/1` | Text / Language |
| Languages | builtin | `Fn.languages` | Advanced |
| Loc | builtin | `Fn.loc` | Advanced |
| Localize | node | `Localize` | Text / Language |
| Set Language | node | `SetLanguage` | Text / Language |

##### Create & Destroy

| Node | Kind | Id | Was |
|---|---|---|---|
| Create Object | builtin | `Fn.create_object` | Objects |
| Create Object Fourth | builtin | `Fn.create_object_3d` | Objects |
| Destroy | node | `Destroy` | Objects |
| Destroy After Seconds | node | `DestroyAfter` | Objects |
| Destroy All Tagged | step | `destroy_all_tag` | Objects |
| Destroy All With Tag | gameplay | `GlDestroyAllWithTag` | Objects / Groups |
| Destroy Named | step | `destroy_obj` | Objects |
| Destroy Object | builtin | `Fn.destroy_obj` | Objects |
| Destroy Self | builtin | `Fn.destroy_self` | Objects |
| Instantiate | builtin | `Fn.Instantiate` | Objects |
| Object Destroy | builtin | `Fn.Object.Destroy` | Objects |
| Object Instantiate | builtin | `Fn.Object.Instantiate` | Objects |
| Remove Object | builtin | `Fn.remove_object` | Objects |
| Spawn | node | `Spawn` | Objects |

##### Create & Destroy / Spawners & Pools

| Node | Kind | Id | Was |
|---|---|---|---|
| Pool Get | builtin | `Fn.pool_get` | Advanced |
| Pool Idle | builtin | `Fn.pool_idle` | Advanced |
| Pool Prewarm | builtin | `Fn.pool_prewarm` | Advanced |
| Pool Release | builtin | `Fn.pool_release` | Advanced |
| Release to Pool | node | `PoolRelease` | Objects |
| Spawn Every Few Seconds | node | `SpawnEvery` | Objects |
| Spawn Wave | builtin | `Fn.spawn_wave` | Objects |
| Spawner Alive | builtin | `Fn.spawner_alive` | Advanced |
| Spawner Start | builtin | `Fn.spawner_start` | Advanced |
| Spawner Stop | builtin | `Fn.spawner_stop` | Advanced |
| Take From Pool | node | `PoolGet` | Objects |

##### Find Objects

| Node | Kind | Id | Was |
|---|---|---|---|
| All With Tag | node | `AllWithTag` | Objects |
| Any Object With Tag | check | `any_with_tag` | Objects |
| Count Objects With Tag | gameplay | `GlCountObjectsWithTag` | Physics / Queries |
| Count Tag | builtin | `Fn.count_tag` | Objects |
| Count With Tag | node | `CountWithTag` | Objects |
| Fewer Tagged Than | check | `tag_count_lt` | Objects |
| Find | api | `GameObject.Find/1` | Objects |
| Find Object | node | `FindObject` | Objects |
| Find With Tag | node | `FindWithTag` | Objects |
| Game Object Find | builtin | `Fn.GameObject.Find` | Objects |
| Game Object Find Game Object With Tag | builtin | `Fn.GameObject.FindGameObjectWithTag` | Advanced |
| Game Object Find Game Objects With Tag | builtin | `Fn.GameObject.FindGameObjectsWithTag` | Advanced |
| Game Object Find With Tag | builtin | `Fn.GameObject.FindWithTag` | Advanced |
| Get Farthest Object | node | `FarthestObject` | Physics / Queries |
| Get Nearest Object | node | `NearestObject` | Physics / Queries |
| Get Object by Tag | node | `FindByTag` | Objects |
| More Tagged Than | check | `tag_count_gt` | Objects |
| Nearest Tag | builtin | `Fn.nearest_tag` | Objects |
| Nearest With Tag | node | `NearestWithTag` | Objects |
| Object Exists | node | `ObjectExists` | Objects |
| Other Object | node | `Other` | Objects |
| Pick Random Tagged Object | gameplay | `GlPickRandomTagged` | Physics / Queries |
| Self | node | `Self` | Objects |
| Sort by Distance | node | `SortByDistance` | Scene |

##### Find Objects / Names & Tags

| Node | Kind | Id | Was |
|---|---|---|---|
| Compare Tag | builtin | `Fn.CompareTag` | Advanced |
| Game Object Compare Tag | builtin | `Fn.gameObject.CompareTag` | Advanced |
| Get Name | node | `GetName` | Objects |
| Get Tag | node | `GetTag` | Objects |
| Has Tag | builtin | `Fn.has_tag` | Objects |
| Object Compare Tag | builtin | `Fn.__object_CompareTag` | Objects |
| Object Has Tag | check | `obj_has_tag` | Objects |
| Other Name | node | `OtherName` | Objects |
| Other Tag | node | `OtherTag` | Objects |
| Rename Object | gameplay | `GlRenameObject` | Objects / Groups |
| Self Name | node | `SelfName` | Objects |
| Set Tag on Object | gameplay | `GlSetTagOnObject` | Objects / Groups |
| Tag | builtin | `Fn.tag` | Objects |
| Tag of | builtin | `Fn.tag_of` | Objects |
| This Object Has Tag | check | `has_tag` | Objects |

##### Show & Hide

| Node | Kind | Id | Was |
|---|---|---|---|
| Activate | builtin | `Fn.activate` | Advanced |
| Activate Named | step | `activate` | Objects |
| Deactivate | builtin | `Fn.deactivate` | Advanced |
| Deactivate Named | step | `deactivate` | Objects |
| Game Object Set Active | builtin | `Fn.gameObject.SetActive` | Advanced |
| Hide | builtin | `Fn.hide` | Advanced |
| Hide All Tagged | step | `deactivate_all_tag` | Objects |
| Hide All With Tag | gameplay | `GlHideAllWithTag` | Objects / Groups |
| Hide Object | builtin | `Fn.hide_object` | Objects |
| Is Object Active | builtin | `Fn.is_object_active` | Objects |
| Object Is Active | check | `obj_active` | Objects |
| Object Set Active | builtin | `Fn.__object_SetActive` | Objects |
| Self Active | builtin | `Fn.self_active` | Advanced |
| Set Active | node | `SetActive` | Objects |
| Set Active of | builtin | `Fn.set_active_of` | Advanced |
| Set Visible | node | `SetVisible` | Objects |
| Show | builtin | `Fn.show` | Advanced |
| Show All Tagged | step | `activate_all_tag` | Objects |
| Show All With Tag | gameplay | `GlShowAllWithTag` | Objects / Groups |
| Show Object | builtin | `Fn.show_object` | Objects |
| This Object Is Active | check | `is_active` | Objects |
| Toggle Active | step | `toggle_active` | Objects |

##### Parents & Components

| Node | Kind | Id | Was |
|---|---|---|---|
| Child Count | builtin | `Fn.child_count` | Advanced |
| Clear Parent | step | `unparent` | Objects |
| Detach | builtin | `Fn.detach` | Advanced |
| Get Child | builtin | `Fn.get_child` | Advanced |
| Has Parent | builtin | `Fn.has_parent` | Advanced |
| Parent of | builtin | `Fn.parent_of` | Advanced |
| Set Parent | builtin | `Fn.set_parent` | Advanced |

##### Parents & Components / Components & Properties

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Component | node | `AddComponent` | Advanced |
| Call Component | builtin | `Fn.__call_component` | Advanced |
| Call Method | node | `CallMethod` | Advanced |
| Game Object Get Component | builtin | `Fn.gameObject.GetComponent` | Advanced |
| Get Component | node | `GetComponent` | Advanced |
| Get Property | node | `GetProperty` | Advanced |
| Object Get Component | builtin | `Fn.__object_GetComponent` | Objects |
| Set Component | builtin | `Fn.__set_component` | Advanced |
| Set Property | node | `SetProperty` | Advanced |
| Try Get Component | builtin | `Fn.TryGetComponent` | Advanced |

##### Messages & Signals

| Node | Kind | Id | Was |
|---|---|---|---|
| Broadcast + Number | step | `send_value` | Flow |
| Broadcast + Text | step | `send_text` | Flow |
| Broadcast Event | node | `BroadcastEvent` | Objects |
| Broadcast Message | node | `Broadcast` | Objects |
| Event Name | node | `EventName` | Scene / Story |
| Event Value | node | `EventValue` | Scene / Story |
| Message One Object | step | `send_to` | Flow |
| Raise | builtin | `Fn.raise` | Advanced |
| Send Event | node | `SendEvent` | Objects |
| Send Message | node | `SendMessage` | Objects |

##### Color & Appearance

| Node | Kind | Id | Was |
|---|---|---|---|
| Blend Between Color | builtin | `Fn.lerp_color` | Math |
| Blink | builtin | `Fn.blink` | Advanced |
| Color | builtin | `Fn.Color` | Advanced |
| Color Blend Between | builtin | `Fn.Color.Lerp` | Math |
| Fade Alpha | step | `fade` | Objects / Appearance |
| Flash | builtin | `Fn.flash` | Advanced |
| Make Color | node | `MakeColor` | Math / Color |
| Pulse | builtin | `Fn.pulse` | Advanced |
| Set Color | node | `SetColor` | Objects |
| Set Color From Var | step | `set_color_var` | Objects / Appearance |
| Set Tint | builtin | `Fn.set_tint` | Advanced |
| Split Color | node | `SplitColor` | Math / Color |

##### Color & Appearance / Sprites & Textures

| Node | Kind | Id | Was |
|---|---|---|---|
| Flip Horizontally | step | `flip_x` | Movement |
| Flip Right | builtin | `Fn.flip_x` | Advanced |
| Flip Upside-Down | step | `flip_y` | Movement |
| Flip Vertical | builtin | `Fn.flip_y` | Advanced |
| Get Tile | builtin | `Fn.get_tile` | Advanced |
| Set Flip Right | builtin | `Fn.set_flip_x` | Advanced |
| Set Flip Vertical | builtin | `Fn.set_flip_y` | Advanced |
| Set Glyph | builtin | `Fn.set_glyph` | Advanced |
| Set Sprite | builtin | `Fn.set_sprite` | Advanced |
| Set Sprite Image | step | `set_sprite` | Objects / Appearance |
| Set Texture | builtin | `Fn.set_texture` | Advanced |
| Set Tile | builtin | `Fn.set_tile` | Advanced |
| Sprite Size | builtin | `Fn.sprite_size` | Advanced |
| Tile Height | builtin | `Fn.tile_h` | Advanced |
| Tile Resize | builtin | `Fn.tile_resize` | Advanced |
| Tile Width | builtin | `Fn.tile_w` | Advanced |

##### Color & Appearance / Materials & Meshes

| Node | Kind | Id | Was |
|---|---|---|---|
| Set Emissive | builtin | `Fn.set_emissive` | Advanced |
| Set Mesh | builtin | `Fn.set_mesh` | Advanced |
| Set Mesh Texture | builtin | `Fn.set_mesh_texture` | Advanced |
| Set Specular | builtin | `Fn.set_specular` | Advanced |
| Set Tiling | builtin | `Fn.set_tiling` | Advanced |
| Set Unlit | builtin | `Fn.set_unlit` | Advanced |

##### Particles & Trails

| Node | Kind | Id | Was |
|---|---|---|---|
| Burst Particles | node | `ParticleBurst` | Objects |
| Clear Trail | node | `ClearTrail` | Objects |
| Emit | builtin | `Fn.emit` | Advanced |
| Emit Particles | builtin | `Fn.emit_particles` | Advanced |
| Get Position Count | api | `LineRenderer.positionCount` | Objects / Lines |
| Get Width | api | `LineRenderer.width` | Objects / Lines |
| Is Emitting | api | `TrailRenderer.emitting` | Objects / Lines |
| Line Set Points | builtin | `Fn.line_set_points` | Advanced |
| Particles Alive | builtin | `Fn.particles_alive` | Advanced |
| Particles on | builtin | `Fn.particles_on` | Advanced |
| Play Particles | node | `ParticlePlay` | Objects |
| Set Emitting | api | `TrailRenderer.emitting=` | Objects / Lines |
| Set Line Points | node | `SetLinePoints` | Objects |
| Set Position Count | api | `LineRenderer.positionCount=` | Objects / Lines |
| Set Positions | api | `LineRenderer.SetPositions/1` | Objects / Lines |
| Set Width | api | `LineRenderer.width=` | Objects / Lines |
| Stop Particles | node | `ParticleStop` | Objects |
| Trail Clear | builtin | `Fn.trail_clear` | Advanced |

##### Position

| Node | Kind | Id | Was |
|---|---|---|---|
| Add To X | step | `add_x` | Movement |
| Add To Y | step | `add_y` | Movement |
| Add To Z | step | `add_z` | Movement |
| Copy Position From | gameplay | `GlCopyPositionFrom` | Movement |
| Get Local Position | api | `Transform.localPosition` | Movement |
| Is Above Height | gameplay | `GlIsAboveHeight` | Movement |
| Is Below Height | gameplay | `GlIsBelowHeight` | Movement |
| Move By (Vec3) | node | `MoveBy` | Movement |
| Object Translate | builtin | `Fn.transform.Translate` | Movement |
| Place at | builtin | `Fn.place_at` | Advanced |
| Set Depth (Z) | step | `set_z` | Movement |
| Set Forward | builtin | `Fn.set_z` | Advanced |
| Set Height | gameplay | `GlSetHeight` | Movement |
| Set Local Position | api | `Transform.localPosition=` | Movement |
| Set Object Position | builtin | `Fn.set_object_position` | Movement |
| Set Position | node | `SetPosition` | Movement |
| Set Position (Vec3) | node | `SetPositionVec` | Movement |
| Set Position Right | builtin | `Fn.set_position_x` | Movement |
| Set Position Vertical | builtin | `Fn.set_position_y` | Movement |
| Set Right | builtin | `Fn.set_x` | Advanced |
| Set Vertical | builtin | `Fn.set_y` | Advanced |
| Set X | step | `set_x` | Movement |
| Set Y | step | `set_y` | Movement |
| Teleport | node | `Teleport` | Movement |
| Translate | node | `Translate` | Movement |

##### Position / Read a Position

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Forward | builtin | `Fn.get_z` | Advanced |
| Get Object | api | `GameObject.transform` | Objects |
| Get Position | node | `GetPosition` | Movement |
| Get Right | builtin | `Fn.get_x` | Advanced |
| Get Vertical | builtin | `Fn.get_y` | Advanced |
| Get X | node | `GetX` | Movement |
| Get Y | node | `GetY` | Movement |
| Get Z | node | `GetZ` | Movement |
| Object Forward | builtin | `Fn.obj_z` | Advanced |
| Object Position | node | `ObjectPosition` | Objects |
| Object Right | builtin | `Fn.obj_x` | Advanced |
| Object Vertical | builtin | `Fn.obj_y` | Advanced |
| Position Forward | builtin | `Fn.pos_z` | Advanced |
| Position of | builtin | `Fn.position_of` | Movement |
| Position Right | builtin | `Fn.pos_x` | Advanced |
| Position Vertical | builtin | `Fn.pos_y` | Advanced |

##### Position / Stay in Bounds

| Node | Kind | Id | Was |
|---|---|---|---|
| Keep Above Height | gameplay | `GlKeepAboveHeight` | Movement |
| Keep in Box | builtin | `Fn.keep_in_box` | Advanced |
| Keep Inside Box | gameplay | `GlKeepInsideBox` | Movement |
| Limit Height | gameplay | `GlLimitHeight` | Movement |
| Snap to Grid | gameplay | `GlSnapToGrid` | Movement |
| Wrap Around Box | gameplay | `GlWrapAroundBox` | Movement |
| Wrap in Box | builtin | `Fn.wrap_in_box` | Advanced |

##### Rotation & Scale

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Forward Direction | api | `Transform.forward` | Movement / Turn |
| Get Local Rotation Angles | api | `Transform.localEulerAngles` | Movement / Turn |
| Get Right Direction | api | `Transform.right` | Movement / Turn |
| Get Rotation | node | `GetRotation` | Movement / Turn |
| Get Rotation Angles | api | `Transform.eulerAngles` | Movement / Turn |
| Get Upward Direction | api | `Transform.up` | Movement / Turn |
| Object Rotate | builtin | `Fn.transform.Rotate` | Movement |
| Quaternion Rotation | builtin | `Fn.Quaternion.Euler` | Advanced |
| Reset Rotation | gameplay | `GlResetRotation` | Movement / Turn |
| Rotate | node | `Rotate` | Movement / Turn |
| Rotate (3D) | node | `Rotate3` | Movement / Turn |
| Rotate Forward | builtin | `Fn.rotate_z` | Movement |
| Rotate To Angle | step | `rotate_to` | Movement |
| Rotation Rotation | builtin | `Fn.rotation_euler` | Advanced |
| Set Local Rotation Angles | api | `Transform.localEulerAngles=` | Movement / Turn |
| Set Rotation | node | `SetRotation` | Movement / Turn |
| Set Rotation (3D) | node | `SetRotation3` | Movement / Turn |
| Set Rotation Angles | api | `Transform.eulerAngles=` | Movement / Turn |
| Turn | builtin | `Fn.turn` | Advanced |
| Turn by Degrees | gameplay | `GlTurnBy` | Movement / Turn |
| Turn Towards | node | `RotateTowards` | Movement |

##### Rotation & Scale / Facing & Looking

| Node | Kind | Id | Was |
|---|---|---|---|
| Face | builtin | `Fn.face` | Advanced |
| Face Away From Object | gameplay | `GlFaceAwayFrom` | Movement / Turn |
| Face Direction | gameplay | `GlFaceDirection` | Movement / Turn |
| Face Fourth | builtin | `Fn.face_3d` | Advanced |
| Face Object | gameplay | `GlFaceObject` | Movement / Turn |
| Face Velocity | builtin | `Fn.face_velocity` | Physics |
| Get Facing Angle | gameplay | `GlGetFacingAngle` | Movement / Turn |
| Look at | builtin | `Fn.look_at` | Movement |
| Look At | node | `LookAt` | Movement / Turn |
| Look at Position | gameplay | `GlLookAtPosition` | Movement / Turn |
| Object Look at | builtin | `Fn.transform.LookAt` | Movement |
| Set Facing Angle | gameplay | `GlSetFacingAngle` | Movement / Turn |
| Turn Toward Object | gameplay | `GlTurnTowardObject` | Movement / Turn |

##### Rotation & Scale / Scale

| Node | Kind | Id | Was |
|---|---|---|---|
| Change Size Over Time | gameplay | `GlChangeSizeOverTime` | Movement / Size |
| Flip Side | gameplay | `GlFlipSide` | Movement / Size |
| Get Local Scale | api | `Transform.localScale` | Movement / Size |
| Get Scale | node | `GetScale` | Movement / Size |
| Multiply Size | gameplay | `GlMultiplySize` | Movement / Size |
| Reset Size | step | `reset_scale` | Movement |
| Scale Forward | builtin | `Fn.scale_z` | Advanced |
| Scale Right | builtin | `Fn.scale_x` | Advanced |
| Scale To | step | `scale_to` | Movement |
| Scale Vertical | builtin | `Fn.scale_y` | Advanced |
| Set Local Scale | api | `Transform.localScale=` | Movement / Size |
| Set Scale | node | `SetScale` | Movement / Size |
| Set Size | builtin | `Fn.set_size` | Advanced |
| Set Size (XYZ) | step | `set_scale3` | Movement |
| Set Size Evenly | gameplay | `GlSetSizeEvenly` | Movement / Size |

##### Move & Follow

| Node | Kind | Id | Was |
|---|---|---|---|
| Chase Target | node | `Chase` | Movement |
| Flee From Threat | node | `Flee` | Movement |
| Follow Smoothly | node | `SmoothFollow` | Movement |
| Knock Back | node | `Knockback` | Movement |
| Move Forward | gameplay | `GlMoveForward` | Movement |
| Move In Direction | step | `move_dir` | Movement |
| Move Sideways | gameplay | `GlMoveSideways` | Movement |
| Move To Point | step | `move_to` | Movement |
| Move to Position | gameplay | `GlMoveToPosition` | Movement |
| Move Towards | node | `MoveTowards` | Movement |
| Orbit Around Center | node | `OrbitAround` | Movement |
| Patrol Waypoints | node | `Patrol` | Movement |
| Stay at Distance From | gameplay | `GlStayAtDistanceFrom` | Movement |
| Wander Around | node | `Wander` | Movement |

##### Move & Follow / Motion Patterns

| Node | Kind | Id | Was |
|---|---|---|---|
| Bob | builtin | `Fn.bob` | Advanced |
| Bounce in Box | builtin | `Fn.bounce_in_box` | Advanced |
| Fade Sprite to | gameplay | `GlFadeSpriteTo` | Animation / Procedural |
| Get Sprite Opacity | gameplay | `GlGetSpriteOpacity` | Animation / Procedural |
| Hop | gameplay | `GlHop` | Movement |
| Move Back and Forth | gameplay | `GlMoveBackAndForth` | Movement |
| Move in Circle | gameplay | `GlMoveInCircle` | Movement |
| Move Upward and Downward | node | `Bob` | Movement |
| Orbit | builtin | `Fn.orbit` | Advanced |
| Orbit Object | step | `orbit` | Movement |
| Pulse Size | gameplay | `GlPulseSize` | Animation / Procedural |
| Rise at Speed | gameplay | `GlRiseAtSpeed` | Movement |
| Shake Position | gameplay | `GlShakePosition` | Movement |
| Spin | node | `Spin` | Movement |
| Sway Turn | gameplay | `GlSwayTurn` | Animation / Procedural |

##### Move & Follow / Script Movement Functions

| Node | Kind | Id | Was |
|---|---|---|---|
| Chase | builtin | `Fn.chase` | Advanced |
| Flee | builtin | `Fn.flee` | Advanced |
| Follow | builtin | `Fn.follow` | Advanced |
| Follower Set Speed | builtin | `Fn.follower_set_speed` | Advanced |
| Move | builtin | `Fn.move` | Movement |
| Move by | builtin | `Fn.move_by` | Movement |
| Move Right | builtin | `Fn.move_right` | Movement |
| Move to | builtin | `Fn.move_to` | Movement |
| Move Toward | builtin | `Fn.move_toward` | Movement |
| Move Toward Object | builtin | `Fn.move_toward_object` | Movement |
| Move Towards Object | builtin | `Fn.move_towards_object` | Movement |
| Npc Busy | builtin | `Fn.npc_busy` | Advanced |
| Npc Goto | builtin | `Fn.npc_goto` | Advanced |
| Npc State | builtin | `Fn.npc_state` | Advanced |
| Npc Stop | builtin | `Fn.npc_stop` | Advanced |
| On Key Move | builtin | `Fn.on_key_move` | Input |
| Patrol | builtin | `Fn.patrol` | Advanced |
| Platformer | builtin | `Fn.platformer` | Advanced |
| Run From | builtin | `Fn.run_from` | Advanced |
| Smooth Follow | builtin | `Fn.smooth_follow` | Advanced |
| Spring to | builtin | `Fn.spring_to` | Advanced |
| Walk | builtin | `Fn.walk` | Advanced |
| Wander | builtin | `Fn.wander` | Advanced |
| Wasd Move | builtin | `Fn.wasd_move` | Movement |

##### Tweens

| Node | Kind | Id | Was |
|---|---|---|---|
| Kill Tweens With Tag | node | `TweenKillTag` | Movement / Tweens |
| Pause Tweens With Tag | node | `TweenPauseTag` | Movement / Tweens |
| Play Tweens With Tag | node | `TweenPlayTag` | Movement / Tweens |
| Tween Color | node | `TweenColor` | Movement / Tweens |
| Tween Move | node | `TweenMove` | Movement / Tweens |
| Tween Move By Speed | node | `TweenMoveSpeed` | Movement / Tweens |
| Tween Position | node | `TweenPosition` | Movement / Tweens |
| Tween Rotation | node | `TweenRotation` | Movement / Tweens |
| Tween Scale | node | `TweenScale` | Movement / Tweens |

##### Tweens / Start a Tween

| Node | Kind | Id | Was |
|---|---|---|---|
| Punch Scale | step | `punch_scale` | Movement / Tweens |
| Tween Fade | builtin | `Fn.tween_fade` | Movement / Tweens |
| Tween Loop Move | builtin | `Fn.tween_loop_move` | Movement / Tweens |
| Tween Loop Rotate | builtin | `Fn.tween_loop_rotate` | Movement / Tweens |
| Tween Loop Scale | builtin | `Fn.tween_loop_scale` | Movement / Tweens |
| Tween Move by | builtin | `Fn.tween_move_by` | Movement / Tweens |
| Tween Move World | builtin | `Fn.tween_move_world` | Movement / Tweens |
| Tween Number | builtin | `Fn.tween_number` | Movement / Tweens |
| Tween Path | builtin | `Fn.tween_path` | Movement / Tweens |
| Tween Path Smooth | builtin | `Fn.tween_path_smooth` | Movement / Tweens |
| Tween Prop | builtin | `Fn.tween_prop` | Movement / Tweens |
| Tween Punch Position | builtin | `Fn.tween_punch_pos` | Movement / Tweens |
| Tween Punch Rotate | builtin | `Fn.tween_punch_rotate` | Movement / Tweens |
| Tween Punch Scale | builtin | `Fn.tween_punch_scale` | Movement / Tweens |
| Tween Rotate | builtin | `Fn.tween_rotate` | Movement / Tweens |
| Tween Rotate to | builtin | `Fn.tween_rotate_to` | Movement / Tweens |
| Tween Screen Move | builtin | `Fn.tween_ui_move` | UI |
| Tween Screen Size | builtin | `Fn.tween_ui_size` | UI |
| Tween Shake | builtin | `Fn.tween_shake` | Camera |
| Tween Shake Rotate | builtin | `Fn.tween_shake_rotate` | Camera |
| Tween Shake Scale | builtin | `Fn.tween_shake_scale` | Camera |
| Tween Text | builtin | `Fn.tween_text` | Movement / Tweens |
| Tween Value | builtin | `Fn.tween_var` | Movement / Tweens |
| Tween Variable | step | `tween_var` | Variables / Change |

##### Tweens / Control Tweens

| Node | Kind | Id | Was |
|---|---|---|---|
| Is Tweening | builtin | `Fn.is_tweening` | Advanced |
| Tween Active | builtin | `Fn.tween_active` | Movement / Tweens |
| Tween Complete | builtin | `Fn.tween_complete` | Movement / Tweens |
| Tween Complete Tag | builtin | `Fn.tween_complete_tag` | Movement / Tweens |
| Tween Goto | builtin | `Fn.tween_goto` | Movement / Tweens |
| Tween Jump | builtin | `Fn.tween_jump` | Movement / Tweens |
| Tween Kill | builtin | `Fn.tween_kill` | Movement / Tweens |
| Tween Kill All | builtin | `Fn.tween_kill_all` | Movement / Tweens |
| Tween Kill Identifier | builtin | `Fn.tween_kill_id` | Movement / Tweens |
| Tween Kill Tag | builtin | `Fn.tween_kill_tag` | Movement / Tweens |
| Tween Pause | builtin | `Fn.tween_pause` | Time |
| Tween Pause All | builtin | `Fn.tween_pause_all` | Time |
| Tween Pause Identifier | builtin | `Fn.tween_pause_id` | Time |
| Tween Pause Tag | builtin | `Fn.tween_pause_tag` | Time |
| Tween Play | builtin | `Fn.tween_play` | Movement / Tweens |
| Tween Play All | builtin | `Fn.tween_play_all` | Movement / Tweens |
| Tween Play Identifier | builtin | `Fn.tween_play_id` | Movement / Tweens |
| Tween Play Tag | builtin | `Fn.tween_play_tag` | Movement / Tweens |
| Tween Playing | builtin | `Fn.tween_playing` | Movement / Tweens |
| Tween Progress | builtin | `Fn.tween_progress` | Movement / Tweens |
| Tween Restart | builtin | `Fn.tween_restart` | Movement / Tweens |
| Tween Restart Identifier | builtin | `Fn.tween_restart_id` | Movement / Tweens |
| Tween Rewind | builtin | `Fn.tween_rewind` | Movement / Tweens |
| Tween Rewind Identifier | builtin | `Fn.tween_rewind_id` | Movement / Tweens |
| Tween Toggle | builtin | `Fn.tween_toggle` | Movement / Tweens |

##### Tweens / Tween Options

| Node | Kind | Id | Was |
|---|---|---|---|
| Tween Backwards | builtin | `Fn.tween_backwards` | Movement / Tweens |
| Tween Clock | builtin | `Fn.tween_clock` | Movement / Tweens |
| Tween Decay | builtin | `Fn.tween_decay` | Movement / Tweens |
| Tween Delay | builtin | `Fn.tween_delay` | Time |
| Tween Ease | builtin | `Fn.tween_ease` | Movement / Tweens |
| Tween From | builtin | `Fn.tween_from` | Movement / Tweens |
| Tween Keep | builtin | `Fn.tween_keep` | Movement / Tweens |
| Tween Loops | builtin | `Fn.tween_loops` | Movement / Tweens |
| Tween Relative | builtin | `Fn.tween_relative` | Movement / Tweens |
| Tween Set Identifier | builtin | `Fn.tween_set_id` | Movement / Tweens |
| Tween Speed | builtin | `Fn.tween_speed` | Movement / Tweens |
| Tween Speed Based | builtin | `Fn.tween_speed_based` | Movement / Tweens |
| Tween Tag | builtin | `Fn.tween_tag` | Movement / Tweens |
| Tween Unscaled | builtin | `Fn.tween_unscaled` | Movement / Tweens |

##### Tweens / Tween Events

| Node | Kind | Id | Was |
|---|---|---|---|
| Tween Call | builtin | `Fn.tween_call` | Movement / Tweens |
| Tween on Complete | builtin | `Fn.tween_on_complete` | Movement / Tweens |
| Tween on Start | builtin | `Fn.tween_on_start` | Movement / Tweens |
| Tween on Step | builtin | `Fn.tween_on_step` | Movement / Tweens |
| Tween on Update | builtin | `Fn.tween_on_update` | Movement / Tweens |

##### Tweens / Tween Sequences

| Node | Kind | Id | Was |
|---|---|---|---|
| Seq Append | builtin | `Fn.seq_append` | Advanced |
| Seq Callback | builtin | `Fn.seq_callback` | Advanced |
| Seq Insert | builtin | `Fn.seq_insert` | Advanced |
| Seq Insert Callback | builtin | `Fn.seq_insert_callback` | Advanced |
| Seq Interval | builtin | `Fn.seq_interval` | Advanced |
| Seq Join | builtin | `Fn.seq_join` | Advanced |
| Seq Join Callback | builtin | `Fn.seq_join_callback` | Advanced |
| Seq New | builtin | `Fn.seq_new` | Advanced |
| Seq Prepend | builtin | `Fn.seq_prepend` | Advanced |
| Seq Prepend Interval | builtin | `Fn.seq_prepend_interval` | Advanced |

##### Paths & Splines

| Node | Kind | Id | Was |
|---|---|---|---|
| Evaluate Position | api | `Spline.EvaluatePosition/1` | Objects / Lines |
| Evaluate Tangent | api | `Spline.EvaluateTangent/1` | Objects / Lines |
| Follow Path | node | `FollowPath` | Movement |
| Get Distance | api | `SplineFollower.distance` | Objects / Lines |
| Get Point Count | api | `Spline.pointCount` | Objects / Lines |
| Get Point On Spline | node | `GetSplinePoint` | Objects |
| Is Closed | api | `Spline.closed` | Objects / Lines |
| Length | api | `Spline.Length/0` | Objects / Lines |
| Nearest Point | api | `Spline.NearestPoint/1` | Objects / Lines |
| Set Closed | api | `Spline.closed=` | Objects / Lines |
| Set Distance | api | `SplineFollower.distance=` | Objects / Lines |
| Spline Length | node | `SplineLength` | Objects |
| Spline Point | builtin | `Fn.spline_point` | Advanced |

##### Forces & Velocity

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Explosion Force | node | `AddExplosionForce` | Physics |
| Add Force | node | `AddForce` | Physics |
| Add Force at Position | api | `Rigidbody.AddForceAtPosition/2` | Physics |
| Add Impulse | node | `AddImpulse` | Physics |
| Add Torque | api | `Rigidbody.AddTorque/1` | Physics |
| Apply Force | builtin | `Fn.apply_force` | Physics |
| Apply Impulse | builtin | `Fn.apply_impulse` | Physics |
| Explode | builtin | `Fn.explode` | Advanced |
| Force Mode Fourth Impulse | builtin | `Fn.ForceMode2D.Impulse` | Physics |
| Physics Body Fourth Add Explosion Force | builtin | `Fn.Rigidbody2D.AddExplosionForce` | Advanced |
| Push Away From Object | gameplay | `GlPushAwayFrom` | Physics |
| Push With Impulse | gameplay | `GlPushWithImpulse` | Movement |

##### Forces & Velocity / Speed & Jumping

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Angular Velocity | api | `Rigidbody.angularVelocity` | Physics |
| Get Flat Speed | gameplay | `GlGetFlatSpeed` | Physics |
| Get Speed | gameplay | `GlGetSpeed` | Movement |
| Get Spin Speed | gameplay | `GlGetSpinSpeed` | Physics |
| Get Velocity Right | builtin | `Fn.get_velocity_x` | Physics |
| Get Velocity Vertical | builtin | `Fn.get_velocity_y` | Physics |
| Get Vertical Speed | gameplay | `GlGetVerticalSpeed` | Physics |
| Is Falling | gameplay | `GlIsFalling` | Physics |
| Is Moving | gameplay | `GlIsMoving` | Movement |
| Is Rising | gameplay | `GlIsRising` | Physics |
| Jump | gameplay | `GlJump` | Physics |
| Limit Speed | gameplay | `GlLimitSpeed` | Physics |
| Set Angular Velocity | api | `Rigidbody.angularVelocity=` | Physics |
| Set Flat Velocity | gameplay | `GlSetFlatSpeed` | Physics |
| Set Velocity | gameplay | `GlSetVelocity` | Movement |
| Set Velocity | node | `SetVelocity` | Physics |
| Spin at Speed | gameplay | `GlSpinAtSpeed` | Physics |
| Stop Moving | gameplay | `GlStopMoving` | Movement |
| Velocity | builtin | `Fn.velocity` | Physics |
| Velocity Forward | builtin | `Fn.velocity_z` | Physics |
| Velocity Right | builtin | `Fn.velocity_x` | Physics |
| Velocity Toward | builtin | `Fn.vel_toward` | Advanced |
| Velocity Vertical | builtin | `Fn.velocity_y` | Physics |
| Velocity X | node | `VelX` | Physics |
| Velocity Y | node | `VelY` | Physics |
| Velocity Z | node | `VelZ` | Physics |

##### Forces & Velocity / Body Settings

| Node | Kind | Id | Was |
|---|---|---|---|
| Freeze Axis | gameplay | `GlFreezeAxis` | Physics |
| Get Air Drag | gameplay | `GlGetAirDrag` | Physics |
| Get Bounciness | gameplay | `GlGetBounciness` | Physics |
| Get Drag | api | `Rigidbody.drag` | Physics |
| Get Gravity Scale | api | `Rigidbody.gravityScale` | Physics |
| Get Gravity Strength | gameplay | `GlGetGravityStrength` | Physics |
| Get Mass | gameplay | `GlGetMass` | Physics |
| Get Surface Friction | gameplay | `GlGetSurfaceFriction` | Physics |
| Is Body Movable | gameplay | `GlIsBodyMovable` | Physics |
| Is Kinematic | api | `Rigidbody.isKinematic` | Physics |
| Is Rotation Locked | gameplay | `GlIsRotationLocked` | Physics |
| Lock Rotation | gameplay | `GlLockRotation` | Physics |
| Physics Smooth | builtin | `Fn.physics_smooth` | Physics |
| Physics Steps | builtin | `Fn.physics_steps` | Physics |
| Set Air Drag | gameplay | `GlSetAirDrag` | Physics |
| Set Body Type | gameplay | `GlSetBodyType` | Physics |
| Set Bounciness | gameplay | `GlSetBounciness` | Physics |
| Set Drag | api | `Rigidbody.drag=` | Physics |
| Set Fall Speed Limit | gameplay | `GlSetFallSpeedLimit` | Physics |
| Set Gravity | builtin | `Fn.set_gravity` | Physics |
| Set Gravity Scale | api | `Rigidbody.gravityScale=` | Physics |
| Set Gravity Strength | gameplay | `GlSetGravityStrength` | Physics |
| Set Kinematic | api | `Rigidbody.isKinematic=` | Physics |
| Set Mass | gameplay | `GlSetMass` | Physics |
| Set Physics Smooth | builtin | `Fn.set_physics_smooth` | Physics |
| Set Surface Friction | gameplay | `GlSetSurfaceFriction` | Physics |
| Wake Body | gameplay | `GlWakeBody` | Physics |

##### Raycasts & Overlaps

| Node | Kind | Id | Was |
|---|---|---|---|
| Box Cast | node | `BoxCast2D` | Physics / Queries |
| Capsule Cast | node | `CapsuleCast2D` | Physics / Queries |
| Cast Ray | builtin | `Fn.cast_ray` | Physics |
| Overlap Box | node | `OverlapBox3D` | Physics / Queries |
| Overlap Capsule | node | `OverlapCapsule2D` | Physics / Queries |
| Overlap Rectangle | node | `OverlapBox2D` | Physics / Queries |
| Overlap Sphere | node | `OverlapSphere3D` | Physics / Queries |
| Physics Fourth Raycast | builtin | `Fn.Physics2D.Raycast` | Physics |
| Physics Raycast | builtin | `Fn.Physics.Raycast` | Physics |
| Raycast | builtin | `Fn.raycast` | Physics |
| Raycast (store hit) | step | `raycast` | Physics / Queries |
| Raycast 2D | node | `Raycast` | Physics / Queries |
| Raycast 3D | node | `Raycast3D` | Physics / Queries |
| Raycast All | builtin | `Fn.raycast_all` | Physics |
| Raycast All 3D | node | `RaycastAll3D` | Physics / Queries |
| Raycast Hits | check | `raycast` | Physics / Queries |
| Raycast Hits Object | check | `raycast_name` | Physics / Queries |
| Raycast Hits Tag | check | `raycast_tag` | Physics / Queries |
| Sphere Cast | node | `SphereCast3D` | Physics / Queries |

##### Raycasts & Overlaps / Ray Results

| Node | Kind | Id | Was |
|---|---|---|---|
| Ray | builtin | `Fn.ray3_nx` | Advanced |
| Ray All Count | builtin | `Fn.ray_all_count` | Advanced |
| Ray All Distance | builtin | `Fn.ray_all_dist` | Advanced |
| Ray All Normal | builtin | `Fn.ray_all_normal` | Advanced |
| Ray All Object | builtin | `Fn.ray_all_object` | Objects |
| Ray All Point | builtin | `Fn.ray_all_point` | Advanced |
| Ray Cast | builtin | `Fn.ray3_cast` | Physics |
| Ray Distance | builtin | `Fn.ray3_dist` | Advanced |
| Ray Forward | builtin | `Fn.ray3_z` | Advanced |
| Ray Hit | builtin | `Fn.ray3_hit` | Advanced |
| Ray Normal | builtin | `Fn.ray3_normal` | Advanced |
| Ray Object | builtin | `Fn.ray3_object` | Objects |
| Ray Point | builtin | `Fn.ray3_point` | Advanced |
| Ray Right | builtin | `Fn.ray3_x` | Advanced |
| Ray Vertical | builtin | `Fn.ray3_y` | Advanced |
| Raycast Hit | builtin | `Fn.raycast_hit` | Physics |

##### Raycasts & Overlaps / Overlaps & Shape Casts

| Node | Kind | Id | Was |
|---|---|---|---|
| Is Overlapping | builtin | `Fn.is_overlapping` | Advanced |
| Layer Mask | builtin | `Fn.layer_mask` | Advanced |
| Overlap | builtin | `Fn.overlap` | Physics |
| Physics Fourth Box Cast | builtin | `Fn.Physics2D.BoxCast` | Advanced |
| Physics Fourth Capsule Cast | builtin | `Fn.Physics2D.CapsuleCast` | Advanced |
| Physics Fourth Overlap Box | builtin | `Fn.Physics2D.OverlapBox` | Advanced |
| Physics Fourth Overlap Capsule | builtin | `Fn.Physics2D.OverlapCapsule` | Advanced |
| Point Overlaps | builtin | `Fn.point_overlaps` | Advanced |

##### Sight & Distance Checks

| Node | Kind | Id | Was |
|---|---|---|---|
| Closer Than | check | `dist_lt` | Objects |
| Count Nearby | node | `CountNearby` | Physics / Queries |
| Count Tagged Objects in Range | gameplay | `GlCountTaggedInRange` | Physics / Queries |
| Distance To | node | `DistanceTo` | Objects |
| Distance to | builtin | `Fn.dist3_to` | Advanced |
| Farther Than | check | `dist_gt` | Objects |
| Get Alert Level | gameplay | `GlGetAlertLevel` | Physics / Queries |
| Get Angle to Object | gameplay | `GlGetAngleToObject` | Physics / Queries |
| Get Direction to Object | gameplay | `GlGetDirectionToObject` | Physics / Queries |
| Get Distance to Position | gameplay | `GlGetDistanceToPosition` | Movement |
| Has Line of Sight | node | `LineOfSight` | Physics / Queries |
| Is Above | library | `LbIsAbove` | Physics / Queries |
| Is Any Tagged Object in Range | gameplay | `GlIsAnyTaggedInRange` | Physics / Queries |
| Is Behind | library | `LbIsBehindFlat` | Physics / Queries |
| Is Below | library | `LbIsBelow` | Physics / Queries |
| Is Facing Object | gameplay | `GlIsFacingObject` | Physics / Queries |
| Is in Cone | node | `InCone` | Physics / Queries |
| Is Inside Area | node | `InsideArea` | Scene |
| Is Inside Circle | library | `LbIsInsideCircle` | Physics / Queries |
| Is Inside Sphere | library | `LbIsInsideSphere` | Physics / Queries |
| Is Near Position | gameplay | `GlIsNearPosition` | Movement |
| Is Nearby | node | `Proximity` | Physics / Collisions |
| Is Object Behind | gameplay | `GlIsObjectBehind` | Physics / Queries |
| Is Object Higher | gameplay | `GlIsObjectHigher` | Physics / Queries |
| Is Target Visible | node | `CanSee` | Physics / Queries |
| Is to the Left | library | `LbIsLeftFlat` | Physics / Queries |
| Is to the Right | library | `LbIsRightFlat` | Physics / Queries |
| Is Within Flat Distance | library | `LbIsWithinFlat` | Physics / Queries |

##### Health & Damage

| Node | Kind | Id | Was |
|---|---|---|---|
| Apply Damage | node | `Damage` | Objects |
| Damage | step | `hurt` | Objects / Survival |
| Drink | step | `drink` | Objects / Survival |
| Eat | step | `eat` | Objects / Survival |
| Get Critical Damage | gameplay | `GlCriticalDamage` | Objects / Survival |
| Get Damage by Distance | gameplay | `GlDamageFalloff` | Objects / Survival |
| Get Hit Protection Time | gameplay | `GlGetHitProtectionTime` | Objects / Survival |
| Heal | node | `Heal` | Objects |
| Hurt | builtin | `Fn.hurt` | Advanced |
| Is Dead | node | `IsDead` | Objects |
| Is Invincible | gameplay | `GlIsInvincible` | Objects / Survival |
| Set Hit Protection Time | gameplay | `GlSetInvincibleTime` | Objects / Survival |
| Set Invincible | gameplay | `GlSetInvincible` | Objects / Survival |
| Set Smallest Hit | gameplay | `GlSetSmallestHit` | Objects / Survival |
| Survival Action | step | `survival` | Objects / Survival |
| Survival On Object | step | `survival_on` | Objects / Survival |
| When Health is Full | node | `HealthFull` | Objects / Survival |
| When Health Runs Out | node | `HealthEmpty` | Objects / Survival |

##### Health & Damage / Health Values

| Node | Kind | Id | Was |
|---|---|---|---|
| Damage Portion | gameplay | `GlDamagePortion` | Objects / Survival |
| Get Health | gameplay | `GlGetHealth` | Objects / Survival |
| Get Health Portion | gameplay | `GlGetHealthPortion` | Objects / Survival |
| Get Largest Health | builtin | `Fn.get_max_hp` | Math |
| Get Maximum Health | gameplay | `GlGetMaxHealth` | Objects / Survival |
| Get Missing Health | gameplay | `GlGetMissingHealth` | Objects / Survival |
| Heal Portion | gameplay | `GlHealPortion` | Objects / Survival |
| Heal to Full | gameplay | `GlHealToFull` | Objects / Survival |
| Is Health Above | gameplay | `GlIsHealthAbove` | Objects / Survival |
| Is Health Full | gameplay | `GlIsHealthFull` | Objects / Survival |
| Is Health Low | gameplay | `GlIsHealthLow` | Objects / Survival |
| Kill | gameplay | `GlKill` | Objects / Survival |
| Set Health | gameplay | `GlSetHealth` | Objects / Survival |
| Set Health Portion | gameplay | `GlSetHealthPortion` | Objects / Survival |
| Set Maximum Health | gameplay | `GlSetMaximumHealth` | Objects / Survival |

##### Health & Damage / Armor & Lives

| Node | Kind | Id | Was |
|---|---|---|---|
| Does Respawn | gameplay | `GlIsRespawnOn` | Objects / Survival |
| Get Armor | gameplay | `GlGetArmor` | Objects / Survival |
| Get Damage After Armor | gameplay | `GlDamageAfterArmor` | Objects / Survival |
| Get Damage Resistance | gameplay | `GlGetResistance` | Objects / Survival |
| Get Lives | gameplay | `GlGetLives` | Objects / Survival |
| Respawn | node | `Respawn` | Objects |
| Revive With Health | gameplay | `GlReviveWithHealth` | Objects / Survival |
| Set Armor | gameplay | `GlSetArmor` | Objects / Survival |
| Set Damage Resistance | gameplay | `GlSetResistance` | Objects / Survival |
| Set Lives | gameplay | `GlSetLives` | Objects / Survival |
| Set Remove on Death | gameplay | `GlSetRemoveOnDeath` | Objects / Survival |
| Set Respawn Time | gameplay | `GlSetRespawnTime` | Objects / Survival |

##### Health & Damage / Resources & Buffs

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Timed Buff | node | `AddBuff` | Objects / Survival |
| Could Spend | library | `LbCouldSpend` | Objects / Survival |
| Damage Over Time | gameplay | `GlDamageOverTime` | Objects / Survival |
| Diminishing Bonus | library | `LbDiminishing` | Objects / Survival |
| Get Overheal Limit | gameplay | `GlGetOverhealLimit` | Objects / Survival |
| Heal Over Time | gameplay | `GlHealOverTime` | Objects / Survival |
| Is Resource Full | library | `LbIsResourceFull` | Objects / Survival |
| Is Resource Low | library | `LbIsResourceLow` | Objects / Survival |
| Overflow Amount | library | `LbOverflow` | Objects / Survival |
| Regen Step | library | `LbRegenStep` | Objects / Survival |
| Resource Portion | library | `LbResourcePortion` | Objects / Survival |
| Set Overheal Limit | gameplay | `GlSetOverhealLimit` | Objects / Survival |
| Set Regeneration | gameplay | `GlSetRegeneration` | Objects / Survival |
| Stack Multiply | library | `LbStackMultiply` | Objects / Survival |
| Try Spend | library | `LbTrySpend` | Objects / Survival |

##### Weapons & Shooting

| Node | Kind | Id | Was |
|---|---|---|---|
| Aim | builtin | `Fn.aim` | Advanced |
| Fire Weapon | node | `FireWeapon` | Objects / Combat |
| Get Ammo | node | `WeaponAmmo` | Objects / Combat |
| Kick Spread | api | `Crosshair.KickSpread/1` | UI |
| Recoil | builtin | `Fn.recoil` | Advanced |
| Reload Weapon | node | `ReloadWeapon` | Objects / Combat |
| Set Spread | api | `Crosshair.SetSpread/1` | UI |
| Set Spread Recover | api | `Crosshair.SetSpreadRecover/1` | UI |
| Set Weapon | node | `SetWeapon` | Objects / Combat |
| Shoot | node | `Shoot` | Objects |
| Shoot at | builtin | `Fn.shoot_at` | Advanced |
| Spawn Projectile | node | `SpawnProjectile` | Objects |
| Spread | api | `Crosshair.Spread/0` | UI |
| Swing Weapon | node | `SwingWeapon` | Objects / Combat |
| Use Ammo | node | `Ammo` | Objects |
| Weapon Ammo | builtin | `Fn.weapon_ammo` | Advanced |
| Weapon Fire | builtin | `Fn.weapon_fire` | Advanced |
| Weapon Reload | builtin | `Fn.weapon_reload` | Advanced |
| Weapon Set | builtin | `Fn.weapon_set` | Advanced |
| Weapon Swing | builtin | `Fn.weapon_swing` | Advanced |

##### Inventory & Items

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Item | node | `AddItem` | Objects / Items |
| Add Item When Room Allows | gameplay | `GlAddItemIfRoom` | Objects / Items |
| Can Fit Item | gameplay | `GlCanFitItem` | Objects / Items |
| Count All Items | gameplay | `GlCountAllItems` | Objects / Items |
| Count Item | gameplay | `GlCountItem` | Objects / Items |
| Empty Inventory | gameplay | `GlEmptyInventory` | Objects / Items |
| Get Free Slots | gameplay | `GlGetFreeSlots` | Objects / Items |
| Get Used Slots | gameplay | `GlGetUsedSlots` | Objects / Items |
| Give Item | step | `give` | Objects / Survival |
| Has Both Items | gameplay | `GlHasBothItems` | Objects / Items |
| Has Item | check | `has_item` | Objects / Items |
| Has Item? | node | `HasItem` | Objects / Items |
| Is Inventory Empty | gameplay | `GlIsInventoryEmpty` | Objects / Items |
| Is Inventory Full | gameplay | `GlIsInventoryFull` | Objects / Items |
| Item Count | node | `ItemCount` | Objects / Items |
| Move Item to Object | gameplay | `GlMoveItem` | Objects / Items |
| Remove All of Item | gameplay | `GlRemoveAllOfItem` | Objects / Items |
| Remove Item | node | `RemoveItem` | Objects / Items |
| Take Item | step | `take` | Objects / Survival |
| Take One Item | gameplay | `GlTakeOneItem` | Objects / Items |
| Use Item | step | `use_item` | Objects / Survival |

##### Inventory & Items / Inventory Slots

| Node | Kind | Id | Was |
|---|---|---|---|
| Inv Add | builtin | `Fn.inv_add` | Advanced |
| Inv Capacity | builtin | `Fn.inv_capacity` | Advanced |
| Inv Clear | builtin | `Fn.inv_clear` | Advanced |
| Inv Count | builtin | `Fn.inv_count` | Advanced |
| Inv Has | builtin | `Fn.inv_has` | Advanced |
| Inv Move | builtin | `Fn.inv_move` | Movement |
| Inv Remove | builtin | `Fn.inv_remove` | Advanced |
| Inv Select | builtin | `Fn.inv_select` | Advanced |
| Inv Selected | builtin | `Fn.inv_selected` | Advanced |
| Inv Set Slot | builtin | `Fn.inv_set_slot` | Advanced |
| Inv Slot | builtin | `Fn.inv_slot` | Advanced |
| Inv Transfer | builtin | `Fn.inv_transfer` | Advanced |
| Item Def | builtin | `Fn.item_def` | Advanced |

##### Inventory & Items / Equipment

| Node | Kind | Id | Was |
|---|---|---|---|
| Equip | node | `EquipItem` | Objects / Items |
| Equipped | builtin | `Fn.equipped` | Advanced |
| Unequip | builtin | `Fn.unequip` | Advanced |

##### Inventory & Items / Crafting & Loot

| Node | Kind | Id | Was |
|---|---|---|---|
| Can Craft | builtin | `Fn.can_craft` | Advanced |
| Craft | node | `CraftItem` | Objects / Items |
| Loot Roll | node | `LootRoll` | Objects / Items |
| Spawn Pickup | node | `SpawnPickup` | Objects / Items |

##### Inventory & Items / Carry Limits

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Carried Weight | gameplay | `GlGetCarriedWeight` | Objects / Items |
| Get Carry Limit | gameplay | `GlGetCarryLimit` | Objects / Items |
| Get Inventory Size | gameplay | `GlGetInventorySize` | Objects / Items |
| Get Slowdown From Weight | gameplay | `GlGetSlowdownFromWeight` | Objects / Items |
| Get Stack Limit | gameplay | `GlGetStackLimit` | Objects / Items |
| Get Stacks Needed | gameplay | `GlStacksNeeded` | Objects / Items |
| Is Overweight | gameplay | `GlIsOverweight` | Objects / Items |
| Set Carry Limit | gameplay | `GlSetCarryLimit` | Objects / Items |
| Set Inventory Size | gameplay | `GlSetInventorySize` | Objects / Items |
| Set Stack Limit | gameplay | `GlSetStackLimit` | Objects / Items |

##### Inventory & Items / Pick Up & Drop

| Node | Kind | Id | Was |
|---|---|---|---|
| Collect | node | `Collect` | Objects / Items |
| Drop | node | `Drop` | Objects / Items |
| Drop Item | builtin | `Fn.drop_item` | Advanced |
| Hold Charge | builtin | `Fn.hold_charge` | Advanced |
| Hold Drop | builtin | `Fn.hold_drop` | Advanced |
| Hold Nearest | builtin | `Fn.hold_nearest` | Advanced |
| Hold Object | builtin | `Fn.hold_object` | Objects |
| Hold Pickup | builtin | `Fn.hold_pickup` | Advanced |
| Hold Throw | builtin | `Fn.hold_throw` | Advanced |
| Pick up | node | `PickUp` | Objects / Items |

##### Quests & Dialogue / Quests

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Progress | api | `Quest.AddProgress/3` | Scene / Story |
| Add Quest Progress | gameplay | `GlAddQuestProgress` | Scene / Story |
| Advance Objective | node | `AdvanceObjective` | Scene / Story |
| Complete | api | `Quest.Complete/1` | Scene / Story |
| Complete Objective | node | `CompleteObjective` | Scene / Story |
| Complete Quest | gameplay | `GlCompleteQuest` | Scene / Story |
| Count Active Quests | gameplay | `GlCountActiveQuests` | Scene / Story |
| Count Completed Quests | gameplay | `GlCountCompletedQuests` | Scene / Story |
| Fail | api | `Quest.Fail/1` | Scene / Story |
| Fail All Active Quests | gameplay | `GlFailActiveQuests` | Scene / Story |
| Fail Quest | gameplay | `GlFailQuest` | Scene / Story |
| Finish All Objectives of Quest | gameplay | `GlFinishObjectives` | Scene / Story |
| Get Objective Count | gameplay | `GlGetObjectiveCount` | Scene / Story |
| Get Quest Progress | gameplay | `GlGetQuestProgress` | Scene / Story |
| Is Any Quest Active | gameplay | `GlIsAnyQuestActive` | Scene / Story |
| Is Objective Done | gameplay | `GlIsObjectiveDone` | Scene / Story |
| Is Quest Active | gameplay | `GlIsQuestActive` | Scene / Story |
| Is Quest Completed | gameplay | `GlIsQuestCompleted` | Scene / Story |
| Is Quest Done | library | `LbIsQuestDone` | Scene / Story |
| Quest Active | builtin | `Fn.quest_active` | Scene |
| Quest Completed | builtin | `Fn.quest_completed` | Scene |
| Quest Portion | library | `LbQuestPortion` | Scene / Story |
| Quest Stage | library | `LbQuestStage` | Scene / Story |
| Quest State | node | `QuestState` | Scene / Story |
| Reset Quest | gameplay | `GlResetQuest` | Scene / Story |
| Start Quest | node | `StartQuest` | Scene / Story |
| Steps Left | library | `LbStepsLeft` | Scene / Story |
| Toggle Journal | builtin | `Fn.toggle_journal` | Advanced |

##### Quests & Dialogue / Dialogue

| Node | Kind | Id | Was |
|---|---|---|---|
| Advance Dialogue | gameplay | `GlAdvanceDialogue` | Scene / Dialogue |
| Choose Dialogue Option | gameplay | `GlChooseDialogueOption` | Scene / Dialogue |
| Dialogue Active | builtin | `Fn.dialogue_active` | Scene |
| Get Dialogue Line | gameplay | `GlGetDialogueLine` | Scene / Dialogue |
| Get Dialogue Option Count | gameplay | `GlGetDialogueOptionCount` | Scene / Dialogue |
| Get Dialogue Option Text | gameplay | `GlGetDialogueOptionText` | Scene / Dialogue |
| Get Dialogue Speaker | gameplay | `GlGetDialogueSpeaker` | Scene / Dialogue |
| Is Dialogue Running | gameplay | `GlIsDialogueRunning` | Scene / Dialogue |
| Is Dialogue Waiting for Choice | gameplay | `GlIsDialogueWaitingForChoice` | Scene / Dialogue |
| Screen Dialogue Choice | builtin | `Fn.ui_dialogue_choice` | UI |
| Screen Dialogue Open | builtin | `Fn.ui_dialogue_open` | UI |
| Screen Dialogue Show | builtin | `Fn.ui_dialogue_show` | UI |
| Start | api | `Dialogue.Start/1` | Scene / Story |
| Start Dialogue | node | `StartDialogue` | Scene / Story |
| Stop Dialogue | gameplay | `GlStopDialogue` | Scene / Dialogue |

##### Quests & Dialogue / Story Flags

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Story | builtin | `Fn.get_story` | Advanced |
| Get Story Var | node | `GetStoryVar` | Variables / Story |
| Has | api | `Story.Has/1` | Scene / Story |
| Has Flag | api | `Story.HasFlag/1` | Scene / Story |
| Has Story | builtin | `Fn.has_story` | Advanced |
| Set Flag | api | `Story.SetFlag/2` | Scene / Story |
| Set Story | builtin | `Fn.set_story` | Advanced |
| Set Story Flag | builtin | `Fn.set_story_flag` | Advanced |
| Set Story Var | node | `SetStoryVar` | Variables / Story |
| Story Flag | builtin | `Fn.story_flag` | Advanced |

##### Score & Winning

| Node | Kind | Id | Was |
|---|---|---|---|
| Add | builtin | `Fn.add_xp` | Advanced |
| Add Score | node | `ScoreAdd` | Variables |
| Get Experience for Level | gameplay | `GlExperienceForLevel` | Math / Progression |
| Get Experience to Next Level | gameplay | `GlExperienceToNextLevel` | Math / Progression |
| Get Level From Experience | gameplay | `GlLevelFromExperience` | Math / Progression |
| Get Price With Discount | gameplay | `GlPriceWithDiscount` | Math / Progression |
| Get Score | node | `ScoreGet` | Variables |
| Get Stat by Level | gameplay | `GlGrowByLevel` | Math / Progression |
| Restart Scene | node | `RestartScene` | Scene |
| Score | builtin | `Fn.score` | Advanced |
| Set Score | node | `ScoreSet` | Variables |
| Show Loss | node | `Lose` | Scene |
| Show Win | node | `Win` | Scene |

##### Doors & Switches

| Node | Kind | Id | Was |
|---|---|---|---|
| Close Door | node | `DoorClose` | Objects |
| Fire Output | builtin | `Fn.fire_output` | Advanced |
| Flip Lever | node | `Lever` | Objects |
| Logic Press | builtin | `Fn.logic_press` | Advanced |
| Logic State | builtin | `Fn.logic_state` | Advanced |
| Open Door | node | `DoorOpen` | Objects |

##### Build Mode

| Node | Kind | Id | Was |
|---|---|---|---|
| Build Active | builtin | `Fn.build_active` | Advanced |
| Build Clear | builtin | `Fn.build_clear` | Advanced |
| Build Count | builtin | `Fn.build_count` | Advanced |
| Build Enter | builtin | `Fn.build_enter` | Advanced |
| Build Exists | builtin | `Fn.build_exists` | Advanced |
| Build Exit | builtin | `Fn.build_exit` | Advanced |
| Build Link Mode | builtin | `Fn.build_link_mode` | Advanced |
| Build Load | builtin | `Fn.build_load` | Advanced |
| Build Palette Add | builtin | `Fn.build_palette_add` | Advanced |
| Build Palette Clear | builtin | `Fn.build_palette_clear` | Advanced |
| Build Palette Select | builtin | `Fn.build_palette_select` | Advanced |
| Build Redo | builtin | `Fn.build_redo` | Advanced |
| Build Save | builtin | `Fn.build_save` | Variables / Saved |
| Build Selected | builtin | `Fn.build_selected` | Advanced |
| Build Toggle | builtin | `Fn.build_toggle` | Advanced |
| Build Undo | builtin | `Fn.build_undo` | Advanced |
| Enter Build Mode | node | `EnterBuildMode` | Scene / Building |
| Exit Build Mode | node | `ExitBuildMode` | Scene / Building |
| In Build Mode? | node | `InBuildMode` | Scene / Building |
| Load Creation | node | `LoadCreation` | Scene / Building |
| Save Creation | node | `SaveCreation` | Scene / Building |
| Set Build Palette | node | `SetBuildPalette` | Scene / Building |

##### Animation

| Node | Kind | Id | Was |
|---|---|---|---|
| Load Clips | builtin | `Fn.load_clips` | Advanced |
| Play Layer | builtin | `Fn.play_layer` | Advanced |
| Stop Layer | builtin | `Fn.stop_layer` | Advanced |

##### Animation / Animator Parameters

| Node | Kind | Id | Was |
|---|---|---|---|
| Anim Event Name | node | `AnimEventName` | Animation |
| Anim Set Bool | node | `AnimSetBool` | Animation |
| Anim Set Float | node | `AnimSetFloat` | Animation |
| Anim State | node | `AnimState` | Animation |
| Anim Trigger | node | `AnimTrigger` | Animation |
| Animation Event | builtin | `Fn.anim_event` | Animation |
| Animation Get Layer Weight | builtin | `Fn.anim_get_layer_weight` | Animation |
| Animation Get Number | builtin | `Fn.anim_get_float` | Animation |
| Animation Get True or False | builtin | `Fn.anim_get_bool` | Animation |
| Animation Goto | builtin | `Fn.anim_goto` | Animation |
| Animation Normalized Time | builtin | `Fn.anim_normalized_time` | Animation |
| Animation Previous State | builtin | `Fn.anim_prev_state` | Animation |
| Animation Set Layer Weight | builtin | `Fn.anim_set_layer_weight` | Animation |
| Animation Set Number | builtin | `Fn.anim_set_float` | Animation |
| Animation Set True or False | builtin | `Fn.anim_set_bool` | Animation |
| Animation State | builtin | `Fn.anim_state` | Animation |
| Animation State Time | builtin | `Fn.anim_state_time` | Animation |
| Animation Trigger | builtin | `Fn.anim_trigger` | Animation |
| Get Layer Weight | api | `Animator.GetLayerWeight/1` | Animation |
| Set Layer Weight | api | `Animator.SetLayerWeight/2` | Animation |
| Set Number | api | `Animator.SetFloat/2` | Animation |
| Set Trigger | api | `Animator.SetTrigger/1` | Animation |
| Set True or False | api | `Animator.SetBool/2` | Animation |

##### Animation / Blend Shapes & Trees

| Node | Kind | Id | Was |
|---|---|---|---|
| Blend | builtin | `Fn.blend` | Advanced |
| Blend Param | builtin | `Fn.blend_param` | Advanced |
| Blend Shape | node | `GetBlendShape` | Animation |
| Blend Shape Get | builtin | `Fn.blend_shape_get` | Advanced |
| Blend Tree | builtin | `Fn.blend_tree` | Advanced |
| Capture Pose | builtin | `Fn.capture_pose` | Advanced |
| Clear Blend Tree | builtin | `Fn.clear_blend_tree` | Advanced |
| Set Blend Shape | node | `SetBlendShape` | Animation |

##### Character Actions

| Node | Kind | Id | Was |
|---|---|---|---|
| Can Dive | builtin | `Fn.can_dive` | Advanced |
| Climb Up | node | `ClimbUp` | Animation / Character |
| Climb up | builtin | `Fn.climb_up` | Advanced |
| Die | node | `Die` | Animation / Character |
| Dive | node | `Dive` | Animation / Character |
| Dodge | node | `Dodge` | Animation / Character |
| Hit React | node | `HitReact` | Animation / Character |
| Interact | node | `Interact` | Animation / Character |
| Is Hanging | node | `IsHanging` | Animation / Character |
| Is in Action | builtin | `Fn.is_in_action` | Advanced |
| Is In Action | node | `IsInAction` | Animation / Character |
| Is Sitting | node | `IsSitting` | Animation / Character |
| Ledge Grab | node | `LedgeGrab` | Animation / Character |
| Let | builtin | `Fn.let_go` | Advanced |
| Let Go | node | `LetGo` | Animation / Character |
| Mantle | node | `Mantle` | Animation / Character |
| Revive | node | `Revive` | Animation / Character |
| Roll | node | `Roll` | Animation / Character |
| Seat Prompt | node | `SeatPrompt` | Animation / Character |
| Shimmy | node | `Shimmy` | Animation / Character |
| Sit | node | `Sit` | Animation / Character |
| Stand up | builtin | `Fn.stand_up` | Advanced |
| Stand Up | node | `StandUp` | Animation / Character |
| Vault | node | `Vault` | Animation / Character |

##### Character Actions / Stances & Ragdoll

| Node | Kind | Id | Was |
|---|---|---|---|
| Crouch | builtin | `Fn.crouch` | Advanced |
| Is Ragdoll | builtin | `Fn.is_ragdoll` | Advanced |
| Prone | builtin | `Fn.prone` | Advanced |
| Ragdoll | builtin | `Fn.ragdoll` | Advanced |
| Ragdoll Push | builtin | `Fn.ragdoll_push` | Advanced |
| Set Stance | builtin | `Fn.set_stance` | Advanced |
| Stand | builtin | `Fn.stand` | Advanced |

##### Inverse Kinematics

| Node | Kind | Id | Was |
|---|---|---|---|
| Clear Limb Target | api | `IK.ClearLimbTarget/1` | Animation / IK |
| IK Hand Reach | node | `IkHand` | Animation / IK |
| IK Look At | node | `IkLook` | Animation / IK |
| Inverse Kinematics Clear Target | builtin | `Fn.ik_clear_target` | Animation |
| Inverse Kinematics Enable | builtin | `Fn.ik_enable` | Animation |
| Inverse Kinematics Foot | builtin | `Fn.ik_foot` | Animation |
| Inverse Kinematics Hand | builtin | `Fn.ik_hand` | Animation |
| Inverse Kinematics Look | builtin | `Fn.ik_look` | Animation |
| Inverse Kinematics Look at | builtin | `Fn.ik_look_at` | Animation |
| Inverse Kinematics Reached | builtin | `Fn.ik_reached` | Animation |
| Inverse Kinematics Release | builtin | `Fn.ik_release` | Animation |
| Inverse Kinematics Set Pole | builtin | `Fn.ik_set_pole` | Animation |
| Inverse Kinematics Set Target | builtin | `Fn.ik_set_target` | Animation |
| Inverse Kinematics Set Weight | builtin | `Fn.ik_set_weight` | Animation |
| Set Kind Weight | api | `IK.SetKindWeight/2` | Animation / IK |
| Set Limb Target | api | `IK.SetLimbTarget/2` | Animation / IK |
| Set Weight | api | `IK.SetWeight/1` | Animation / IK |

##### Navigation

| Node | Kind | Id | Was |
|---|---|---|---|
| Arrived | api | `NavMeshAgent.Arrived/0` | Scene / Navigation |
| Get Destination | api | `NavMeshAgent.destination` | Scene / Navigation |
| Get Stopping Distance | api | `NavMeshAgent.stoppingDistance` | Scene / Navigation |
| Has Path | api | `NavMeshAgent.HasPath/0` | Scene / Navigation |
| Is Stopped | api | `NavMeshAgent.isStopped` | Scene / Navigation |
| Nav Arrived | node | `NavArrived` | Scene / Navigation |
| Nav Move To | node | `NavMoveTo` | Scene / Navigation |
| Nav Stop | node | `NavStop` | Scene / Navigation |
| Navigation Arrived | builtin | `Fn.nav_arrived` | Scene |
| Navigation Move to | builtin | `Fn.nav_move_to` | Movement |
| Navigation Nearest Point | builtin | `Fn.nav_nearest_point` | Scene |
| Navigation Path Exists | builtin | `Fn.nav_path_exists` | Scene |
| Navigation Path Length | builtin | `Fn.nav_path_length` | Scene |
| Navigation Point Walkable | builtin | `Fn.nav_point_walkable` | Scene |
| Navigation Remaining | builtin | `Fn.nav_remaining` | Scene |
| Navigation Stop | builtin | `Fn.nav_stop` | Scene |
| Remaining Distance | api | `NavMeshAgent.RemainingDistance/0` | Scene / Navigation |
| Resume | api | `NavMeshAgent.Resume/0` | Scene / Navigation |
| Set Destination | api | `NavMeshAgent.SetDestination/1` | Scene / Navigation |
| Set Speed | api | `NavMeshAgent.speed=` | Scene / Navigation |
| Set Stopping Distance | api | `NavMeshAgent.stoppingDistance=` | Scene / Navigation |

##### Navigation / Grid Paths

| Node | Kind | Id | Was |
|---|---|---|---|
| Grid Add | builtin | `Fn.grid_add` | Advanced |
| Grid Arrived | node | `GridArrived` | Scene / Navigation |
| Grid Block | builtin | `Fn.grid_block` | Advanced |
| Grid Clear | builtin | `Fn.grid_clear` | Advanced |
| Grid Fits | builtin | `Fn.grid_fits` | Advanced |
| Grid Move Cell | builtin | `Fn.grid_move_cell` | Movement |
| Grid Move to | builtin | `Fn.grid_move_to` | Movement |
| Grid Set Cell Cost | builtin | `Fn.grid_set_cell_cost` | Advanced |
| Grid Set Tile Cost | builtin | `Fn.grid_set_tile_cost` | Advanced |
| Grid Snap | builtin | `Fn.grid_snap` | Advanced |
| Grid Stop | builtin | `Fn.grid_stop` | Advanced |
| Grid Weight | builtin | `Fn.grid_weight` | Advanced |
| Is Allow Diagonal | api | `GridPathAgent2D.allowDiagonal` | Scene / Navigation |
| Is Smooth Path | api | `GridPathAgent2D.smoothPath` | Scene / Navigation |
| Is Use Costs | api | `GridPathAgent2D.useCosts` | Scene / Navigation |
| Place | api | `GridObstacle2D.Place/2` | Scene / Navigation |
| Set Allow Diagonal | api | `GridPathAgent2D.allowDiagonal=` | Scene / Navigation |
| Set Cell | api | `GridPathAgent2D.SetCell/2` | Scene / Navigation |
| Set Cell Cost | api | `GridCostLayer.SetCellCost/3` | Scene / Navigation |
| Set Smooth Path | api | `GridPathAgent2D.smoothPath=` | Scene / Navigation |
| Set Tile Cost | api | `GridCostLayer.SetTileCost/2` | Scene / Navigation |
| Set Use Costs | api | `GridPathAgent2D.useCosts=` | Scene / Navigation |
| Stop Grid Walk | node | `GridStop` | Scene / Navigation |
| Walk the Grid | node | `GridMoveTo` | Scene / Navigation |

##### Behavior Trees

| Node | Kind | Id | Was |
|---|---|---|---|
| All Must Succeed | node | `AllSucceed` | Advanced / Behavior |
| Always Succeed | node | `AlwaysSucceed` | Advanced / Behavior |
| Behavior Tree Get | builtin | `Fn.bt_get` | Advanced |
| Behavior Tree Run | builtin | `Fn.bt_run` | Advanced |
| Behavior Tree Set | builtin | `Fn.bt_set` | Advanced |
| Behavior Tree Stop | builtin | `Fn.bt_stop` | Advanced |
| Check Condition | node | `CheckOutcome` | Advanced / Behavior |
| Fail Now | node | `FailNow` | Advanced / Behavior |
| Flip Result | node | `FlipOutcome` | Advanced / Behavior |
| Get Blackboard | node | `BtGet` | Advanced / Behavior |
| Get Number | api | `BehaviorTree.GetFloat/1` | Advanced / Behavior |
| Get True or False | api | `BehaviorTree.GetBool/1` | Advanced / Behavior |
| Get Vector | api | `BehaviorTree.GetVector/1` | Advanced / Behavior |
| Is Running | api | `BehaviorTree.isRunning` | Advanced / Behavior |
| Repeat Until Fail | node | `RepeatUntilFail` | Advanced / Behavior |
| Run | api | `BehaviorTree.Run/1` | Advanced / Behavior |
| Run Tree | node | `BtRun` | Advanced / Behavior |
| Set Blackboard | node | `BtSet` | Advanced / Behavior |
| Succeed Now | node | `SucceedNow` | Advanced / Behavior |
| Try First | node | `TryFirst` | Advanced / Behavior |

##### Sound Effects

| Node | Kind | Id | Was |
|---|---|---|---|
| Fade Volume to | gameplay | `GlFadeVolumeTo` | Audio |
| Get Pitch | gameplay | `GlGetPitch` | Audio |
| Get Volume | gameplay | `GlGetVolume` | Audio |
| Is Sound Looping | gameplay | `GlIsSoundLooping` | Audio |
| Is Sound Playing | gameplay | `GlIsSoundPlaying` | Audio |
| Is Sound Positional | gameplay | `GlIsSoundPositional` | Audio |
| Mute | builtin | `Fn.mute` | Advanced |
| Mute Sound | gameplay | `GlMuteSound` | Audio |
| Pause Sound | gameplay | `GlPauseSound` | Audio |
| Play Audio | builtin | `Fn.play_audio` | Audio |
| Play Sound | node | `PlaySound` | Audio |
| Play Sound Here | node | `PlaySoundAt` | Audio |
| Play Sound Now | gameplay | `GlPlaySoundNow` | Audio |
| Randomize Pitch | gameplay | `GlRandomizePitch` | Audio |
| Set Pitch | gameplay | `GlSetPitch` | Audio |
| Set Sound Looping | gameplay | `GlSetSoundLooping` | Audio |
| Set Sound Position Effect | gameplay | `GlSetSound3D` | Audio |
| Set Volume | gameplay | `GlSetVolume` | Audio |
| Stop Sound | node | `StopSound` | Audio |
| Volume | builtin | `Fn.volume` | Advanced |

##### Music & Volume

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Group Volume | api | `Audio.GetGroupVolume/1` | Audio |
| Get Master Volume | api | `Audio.masterVolume` | Audio |
| Group Volume | builtin | `Fn.group_volume` | Advanced |
| Is Muted | api | `Audio.muted` | Audio |
| Music Next | builtin | `Fn.music_next` | Audio |
| Music Pause | builtin | `Fn.music_pause` | Audio |
| Music Play | builtin | `Fn.music_play` | Audio |
| Music Previous | builtin | `Fn.music_previous` | Audio |
| Music Resume | builtin | `Fn.music_resume` | Audio |
| Music Stop | builtin | `Fn.music_stop` | Audio |
| Mute Group | api | `Audio.MuteGroup/2` | Audio |
| Next Music Track | node | `MusicNext` | Audio |
| Pause Music | node | `MusicPause` | Audio |
| Play Music | node | `MusicPlay` | Audio |
| Resume Music | node | `MusicResume` | Audio |
| Save Audio Settings | builtin | `Fn.save_audio_settings` | Audio |
| Save Settings | api | `Audio.SaveSettings/0` | Audio |
| Set Ducking | builtin | `Fn.set_ducking` | Advanced |
| Set Group Volume | api | `Audio.SetGroupVolume/2` | Audio |
| Set Master Volume | api | `Audio.masterVolume=` | Audio |
| Set Muted | api | `Audio.muted=` | Audio |
| Stop Music | node | `MusicStop` | Audio |

##### Camera / Follow & Switch

| Node | Kind | Id | Was |
|---|---|---|---|
| Camera Follow | node | `CameraFollow` | Camera |
| Camera Right | builtin | `Fn.cam_x` | Advanced |
| Camera Vertical | builtin | `Fn.cam_y` | Advanced |
| Center Camera On | step | `cam_center_on` | Camera |
| Follow Object With Camera | gameplay | `GlCameraFollowObject` | Camera |
| Get View | api | `PlayerView.GetView/0` | Camera |
| Look at Object With Camera | gameplay | `GlCameraLookAtObject` | Camera |
| Move Camera | builtin | `Fn.move_cam` | Movement |
| Reset Camera | gameplay | `GlResetCamera` | Camera |
| Set Camera | builtin | `Fn.set_cam` | Advanced |
| Set Camera Target | node | `SetCameraTarget` | Camera |
| Set View | api | `PlayerView.SetView/1` | Camera |
| Set View Mode | api | `PlayerController.SetViewMode/1` | Input |
| Switch Camera | node | `SwitchCamera` | Camera |

##### Camera / Zoom & Shake

| Node | Kind | Id | Was |
|---|---|---|---|
| Camera Dutch | builtin | `Fn.camera_dutch` | Camera |
| Camera Field of View | builtin | `Fn.camera_fov` | Camera |
| Camera Shake | node | `CameraShake` | Camera |
| Camera Zoom | builtin | `Fn.cam_zoom` | Advanced |
| Camera Zoom to | builtin | `Fn.camera_zoom_to` | Camera |
| Get Camera Tilt | gameplay | `GlGetCameraTilt` | Camera |
| Get Camera Zoom | gameplay | `GlGetCameraZoom` | Camera |
| Get Field of View | gameplay | `GlGetFieldOfView` | Camera |
| Get Flat Camera Size | gameplay | `GlGetFlatCameraSize` | Camera |
| Is Camera Zoomed | gameplay | `GlIsCameraZoomedIn` | Camera |
| Set Camera FOV | node | `SetFov` | Camera |
| Set Camera Zoom | gameplay | `GlSetCameraZoom` | Camera |
| Set Field of View | gameplay | `GlSetFieldOfView` | Camera |
| Set Flat Camera Size | gameplay | `GlSetFlatCameraSize` | Camera |
| Set Zoom | builtin | `Fn.set_zoom` | Advanced |
| Shake | builtin | `Fn.shake` | Camera |
| Shake Camera | gameplay | `GlShakeCamera` | Camera |
| Tilt Camera | gameplay | `GlTiltCamera` | Camera |
| Zoom Camera | node | `Zoom` | Camera |
| Zoom Smoothly to | gameplay | `GlZoomSmoothlyTo` | Camera |

##### Screen Text

| Node | Kind | Id | Was |
|---|---|---|---|
| Add Line to Text | gameplay | `GlAddLineToText` | UI |
| Clear Text | gameplay | `GlClearText` | UI |
| Fade Screen | node | `FadeScreen` | UI |
| Get Text | gameplay | `GlGetText` | UI |
| Hide UI | node | `HideUI` | UI |
| Set Image | builtin | `Fn.set_image` | Advanced |
| Set Screen Image | builtin | `Fn.set_ui_image` | UI |
| Set Text | node | `SetText` | UI |
| Set Text of | builtin | `Fn.set_text_of` | Text |
| Set Text On Object | step | `set_text_on` | UI |
| Show Floating Text | node | `ShowFloatingText` | UI |
| Show UI | node | `ShowUI` | UI |
| Text To Object | step | `str_set_text` | UI |

##### Screen Text / Text Style & Format

| Node | Kind | Id | Was |
|---|---|---|---|
| Fade Text to | gameplay | `GlFadeTextTo` | UI |
| Format Clock | gameplay | `GlFormatClock` | UI |
| Format Number | gameplay | `GlFormatNumber` | UI |
| Format Percent | gameplay | `GlFormatPercent` | UI |
| Get Text Opacity | gameplay | `GlGetTextOpacity` | UI |
| Get Text Size | gameplay | `GlGetTextSize` | UI |
| Set Text Color | gameplay | `GlSetTextColor` | UI |
| Set Text Opacity | gameplay | `GlSetTextOpacity` | UI |
| Set Text Size | gameplay | `GlSetTextSize` | UI |
| Type Out Text | gameplay | `GlTypewriterText` | UI |

##### Screen Text / Numbers in Text

| Node | Kind | Id | Was |
|---|---|---|---|
| Show Health in Text | gameplay | `GlShowHealthInText` | UI |
| Show Number in Text | gameplay | `GlShowNumberInText` | UI |
| Show Session Time in Text | gameplay | `GlShowSessionTimeInText` | UI |
| Show Two Numbers in Text | gameplay | `GlShowTwoNumbersInText` | UI |

##### Screen Text / Script Text Widgets

| Node | Kind | Id | Was |
|---|---|---|---|
| Screen Avatar | builtin | `Fn.ui_avatar` | UI |
| Screen Badge | builtin | `Fn.ui_badge` | UI |
| Screen Caption | builtin | `Fn.ui_caption` | UI |
| Screen Damage Number | builtin | `Fn.ui_damage_number` | UI |
| Screen Date Text | builtin | `Fn.ui_date_text` | UI |
| Screen Dropdown Text | builtin | `Fn.ui_dropdown_text` | UI |
| Screen Get Text | builtin | `Fn.ui_get_text` | UI |
| Screen Heading | builtin | `Fn.ui_heading` | UI |
| Screen Key Value | builtin | `Fn.ui_key_value` | UI |
| Screen Set Rich | builtin | `Fn.ui_set_rich` | UI |
| Screen Set Text | builtin | `Fn.ui_set_text` | UI |
| Screen Tab Text | builtin | `Fn.ui_tab_text` | UI |
| Screen Text | builtin | `Fn.ui_text` | UI |
| Screen Text Dim | builtin | `Fn.ui_text_dim` | UI |
| Screen Text Wrapped | builtin | `Fn.ui_text_wrapped` | UI |

##### Buttons & Inputs

| Node | Kind | Id | Was |
|---|---|---|---|
| Drop Source | builtin | `Fn.drop_source` | Advanced |
| Drop Target | builtin | `Fn.drop_target` | Advanced |
| Focus | api | `UIButton.Focus/0` | UI / Buttons |
| Press | api | `UIButton.Press/0` | UI / Buttons |
| Screen Button | builtin | `Fn.ui_button` | UI |
| Screen Clicked | builtin | `Fn.ui_clicked` | UI |
| Screen Drop Source | builtin | `Fn.ui_drop_source` | UI |
| Screen Drop Target | builtin | `Fn.ui_drop_target` | UI |
| Screen Hovered | builtin | `Fn.ui_hovered` | UI |
| Screen is Hovered | builtin | `Fn.ui_is_hovered` | UI |
| Screen Pointer Over | builtin | `Fn.ui_pointer_over` | UI |
| Screen Primary Button | builtin | `Fn.ui_primary_button` | UI |
| Screen Set Button Ease | builtin | `Fn.ui_set_button_ease` | UI |
| Screen Set Focus | builtin | `Fn.ui_set_focus` | UI |
| Screen Set Interactable | builtin | `Fn.ui_set_interactable` | UI |
| Set Button Ease | builtin | `Fn.set_button_ease` | UI |
| Set Button Enabled | builtin | `Fn.set_button_enabled` | UI |
| Set Ease | api | `UIButton.SetEase/2` | UI / Buttons |
| Set Interactable | api | `UIButton.SetInteractable/1` | UI / Buttons |

##### Buttons & Inputs / Sliders & Toggles

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Value | api | `UISlider.GetValue/0` | UI / Bars |
| Screen Checkbox | builtin | `Fn.ui_checkbox` | UI |
| Screen Radio Value | builtin | `Fn.ui_radio_value` | UI |
| Screen Set Radio | builtin | `Fn.ui_set_radio` | UI |
| Screen Set Slider | builtin | `Fn.ui_set_slider` | UI |
| Screen Set Slider Format | builtin | `Fn.ui_set_slider_format` | UI |
| Screen Set Slider Step | builtin | `Fn.ui_set_slider_step` | UI |
| Screen Set Toggle | builtin | `Fn.ui_set_toggle` | UI |
| Screen Slider | builtin | `Fn.ui_slider` | UI |
| Screen Slider Value | builtin | `Fn.ui_slider_value` | UI |
| Screen Switch | builtin | `Fn.ui_switch` | UI |
| Screen Toggle Value | builtin | `Fn.ui_toggle_value` | UI |
| Set Format | api | `UISlider.SetFormat/1` | UI / Bars |
| Set Slider | builtin | `Fn.set_slider` | UI |
| Set Slider Format | builtin | `Fn.set_slider_format` | UI |
| Set Slider Step | builtin | `Fn.set_slider_step` | UI |
| Set Step | api | `UISlider.SetStep/1` | UI / Bars |
| Set Toggle | builtin | `Fn.set_toggle` | Advanced |
| Slider Value | builtin | `Fn.slider_value` | UI |
| Toggle on | builtin | `Fn.toggle_on` | Advanced |

##### Buttons & Inputs / Dropdowns & Text Fields

| Node | Kind | Id | Was |
|---|---|---|---|
| Screen Dropdown Value | builtin | `Fn.ui_dropdown_value` | UI |
| Screen Input | builtin | `Fn.ui_input` | UI |
| Screen Set Dropdown | builtin | `Fn.ui_set_dropdown` | UI |
| Searchable | api | `UIDropdown.Searchable/0` | UI / Fields |
| Set Content Type | api | `UIInputField.SetContentType/1` | UI / Fields |
| Set Dropdown Icon | builtin | `Fn.set_dropdown_icon` | Advanced |
| Set Dropdown Largest | builtin | `Fn.set_dropdown_max` | Math |
| Set Dropdown Search | builtin | `Fn.set_dropdown_search` | Advanced |
| Set Input Limit | builtin | `Fn.set_input_limit` | Input |
| Set Input Multiline | builtin | `Fn.set_input_multiline` | Input |
| Set Input Type | builtin | `Fn.set_input_type` | Input |
| Set Largest Length | api | `UIInputField.SetMaxLength/1` | UI / Fields |
| Set Largest Visible | api | `UIDropdown.SetMaxVisible/1` | UI / Fields |
| Set Multiline | api | `UIInputField.SetMultiline/1` | UI / Fields |
| Set Option Icon | api | `UIDropdown.SetOptionIcon/2` | UI / Fields |
| Set Searchable | api | `UIDropdown.SetSearchable/1` | UI / Fields |

##### Buttons & Inputs / Pickers & Search

| Node | Kind | Id | Was |
|---|---|---|---|
| Clear | api | `UISearchBar.Clear/0` | UI |
| Match Count | api | `UISearchBar.MatchCount/0` | UI |
| Screen File Open | builtin | `Fn.ui_file_open` | UI |
| Screen File Selected | builtin | `Fn.ui_file_selected` | UI |
| Screen File Set Root | builtin | `Fn.ui_file_set_root` | UI |
| Screen Search Count | builtin | `Fn.ui_search_count` | UI |
| Screen Search Query | builtin | `Fn.ui_search_query` | UI |
| Screen Set Date | builtin | `Fn.ui_set_date` | UI |
| Screen Set Search | builtin | `Fn.ui_set_search` | UI |
| Screen Set Time | builtin | `Fn.ui_set_time` | UI |
| Selected | api | `UIFilePicker.Selected/0` | UI |
| Set Date | api | `UIDateTimePicker.SetDate/3` | UI |
| Set Query | api | `UISearchBar.SetQuery/1` | UI |
| Set Root | api | `UIFilePicker.SetRoot/1` | UI |
| Set Swatch | builtin | `Fn.set_swatch` | Advanced |
| Set Time | api | `UIDateTimePicker.SetTime/2` | UI |
| Set Value | api | `UIColorPicker.SetValue/1` | UI / Theme |
| Swatch Value | builtin | `Fn.swatch_value` | Advanced |
| Value | api | `UIColorPicker.Value/0` | UI / Theme |

##### Bars & Layout / Progress Bars

| Node | Kind | Id | Was |
|---|---|---|---|
| Is Loading | api | `UISkeleton.IsLoading/0` | UI |
| Screen Set Loading | builtin | `Fn.ui_set_loading` | UI |
| Screen Spinner | builtin | `Fn.ui_spinner` | UI |
| Set Bar Format | builtin | `Fn.set_bar_format` | Text |
| Set Bar Segments | builtin | `Fn.set_bar_segments` | Advanced |
| Set Bar Trail | builtin | `Fn.set_bar_trail` | Advanced |
| Set Clockwise | api | `UIRadialProgress.SetClockwise/1` | UI / Bars |
| Set Loading | api | `UISkeleton.SetLoading/1` | UI |
| Set Progress | node | `SetProgress` | UI / Bars |
| Set Progress Bar | builtin | `Fn.set_progress_bar` | Advanced |
| Set Radial | builtin | `Fn.set_radial` | Advanced |
| Set Radial Clockwise | builtin | `Fn.set_radial_clockwise` | Advanced |
| Set Radial Percent | builtin | `Fn.set_radial_percent` | Advanced |
| Set Radial Spin | builtin | `Fn.set_radial_spin` | Advanced |
| Set Radial Start | builtin | `Fn.set_radial_start` | Advanced |
| Set Radial Thickness | builtin | `Fn.set_radial_thickness` | Advanced |
| Set Segments | api | `UIProgressBar.SetSegments/1` | UI / Bars |
| Set Show Percent | api | `UIRadialProgress.SetShowPercent/1` | UI / Bars |
| Set Spin | api | `UIRadialProgress.SetSpin/1` | UI / Bars |
| Set Start Angle | api | `UIRadialProgress.SetStartAngle/1` | UI / Bars |
| Set Text Format | api | `UIProgressBar.SetTextFormat/1` | UI / Bars |
| Set Thickness | api | `UIRadialProgress.SetThickness/1` | UI / Bars |
| Set Trail | api | `UIProgressBar.SetTrail/1` | UI / Bars |

##### Bars & Layout / Tabs & Pages

| Node | Kind | Id | Was |
|---|---|---|---|
| Close Tab | api | `UITabs.CloseTab/1` | UI / Layout |
| Count | api | `UITabs.Count/0` | UI / Layout |
| Is Expanded | api | `UIAccordion.IsExpanded/1` | UI |
| Next | api | `UIPagination.Next/0` | UI |
| Page | api | `UIPagination.Page/0` | UI |
| Path | api | `UIBreadcrumbs.Path/0` | UI |
| Previous | api | `UIPagination.Previous/0` | UI |
| Screen Accordion Open | builtin | `Fn.ui_accordion_open` | UI |
| Screen Accordion Toggle | builtin | `Fn.ui_accordion_toggle` | UI |
| Screen Breadcrumb Path | builtin | `Fn.ui_breadcrumb_path` | UI |
| Screen Breadcrumb Select | builtin | `Fn.ui_breadcrumb_select` | UI |
| Screen Page | builtin | `Fn.ui_page` | UI |
| Screen Page Count | builtin | `Fn.ui_page_count` | UI |
| Screen Set Page | builtin | `Fn.ui_set_page` | UI |
| Screen Set Tab | builtin | `Fn.ui_set_tab` | UI |
| Screen Set Tab Badge | builtin | `Fn.ui_set_tab_badge` | UI |
| Screen Set Tab Enabled | builtin | `Fn.ui_set_tab_enabled` | UI |
| Screen Tab Value | builtin | `Fn.ui_tab_value` | UI |
| Screen Tabs | builtin | `Fn.ui_tabs` | UI |
| Set Closable | api | `UITabs.SetClosable/1` | UI / Layout |
| Set Expanded | api | `UIAccordion.SetExpanded/2` | UI |
| Set Page | api | `UIPagination.SetPage/1` | UI |
| Set Path | api | `UIBreadcrumbs.SetPath/1` | UI |
| Set Tab | builtin | `Fn.set_tab` | Advanced |
| Set Tab Badge | builtin | `Fn.set_tab_badge` | Advanced |
| Set Tab Enabled | builtin | `Fn.set_tab_enabled` | Advanced |
| Set Tabs Closable | builtin | `Fn.set_tabs_closable` | Advanced |
| Set Total | api | `UIPagination.SetTotal/2` | UI |
| Tab Value | builtin | `Fn.tab_value` | Advanced |

##### Bars & Layout / Scroll Areas & Grids

| Node | Kind | Id | Was |
|---|---|---|---|
| Set Bar Style | api | `UIScrollView.SetBarStyle/1` | UI / Layout |
| Set Columns | api | `UISlotGrid.SetColumns/1` | UI / Layout |
| Set Elastic | api | `UIScrollView.SetElastic/1` | UI / Layout |
| Set Inertia | api | `UIScrollView.SetInertia/1` | UI / Layout |
| Set Rows | api | `UISlotGrid.SetRows/1` | UI / Layout |
| Set Scroll Bar | builtin | `Fn.set_scroll_bar` | Advanced |
| Set Scroll Elastic | builtin | `Fn.set_scroll_elastic` | Advanced |
| Set Scroll Inertia | builtin | `Fn.set_scroll_inertia` | Advanced |
| Set Slot | builtin | `Fn.set_slot` | Advanced |
| Slot Value | builtin | `Fn.slot_value` | Advanced |

##### Bars & Layout / Layout Helpers

| Node | Kind | Id | Was |
|---|---|---|---|
| Screen Begin | builtin | `Fn.ui_begin` | UI |
| Screen Card Begin | builtin | `Fn.ui_card_begin` | UI |
| Screen Card End | builtin | `Fn.ui_card_end` | UI |
| Screen End | builtin | `Fn.ui_end` | UI |
| Screen Get Rotation | builtin | `Fn.ui_get_rotation` | UI |
| Screen Get Scale | builtin | `Fn.ui_get_scale` | UI |
| Screen Hide | builtin | `Fn.ui_hide` | UI |
| Screen List Item | builtin | `Fn.ui_list_item` | UI |
| Screen Play | builtin | `Fn.ui_play` | UI |
| Screen Progress | builtin | `Fn.ui_progress` | UI |
| Screen Sameline | builtin | `Fn.ui_sameline` | UI |
| Screen Separator | builtin | `Fn.ui_separator` | UI |
| Screen Set Bar Format | builtin | `Fn.ui_set_bar_format` | UI |
| Screen Set Bar Segments | builtin | `Fn.ui_set_bar_segments` | UI |
| Screen Set Bar Trail | builtin | `Fn.ui_set_bar_trail` | UI |
| Screen Set Fill | builtin | `Fn.ui_set_fill` | UI |
| Screen Set Progress | builtin | `Fn.ui_set_progress` | UI |
| Screen Set Rotation | builtin | `Fn.ui_set_rotation` | UI |
| Screen Set Scale | builtin | `Fn.ui_set_scale` | UI |
| Screen Show | builtin | `Fn.ui_show` | UI |
| Screen Spacing | builtin | `Fn.ui_spacing` | UI |

##### Bars & Layout / Themes

| Node | Kind | Id | Was |
|---|---|---|---|
| Apply | api | `UITheme.Apply/0` | UI / Theme |
| Apply Screen Theme | builtin | `Fn.ApplyUITheme` | Advanced |
| Apply to | api | `UITheme.ApplyTo/1` | UI / Theme |
| Screen Load Theme | builtin | `Fn.ui_load_theme` | UI |
| Screen Set Variant | builtin | `Fn.ui_set_variant` | UI |
| Screen Style | builtin | `Fn.ui_style` | UI |
| Screen Theme Preset | builtin | `Fn.ui_theme_preset` | UI |
| Set Preset | api | `UITheme.SetPreset/1` | UI / Theme |
| Set Screen Theme | builtin | `Fn.SetUITheme` | Advanced |

##### Popups & Dialogs / Toasts & Tooltips

| Node | Kind | Id | Was |
|---|---|---|---|
| Push | api | `UIToast.Push/3` | UI / Popups |
| Screen Toast | builtin | `Fn.ui_toast` | UI |
| Set Corner | api | `UIToast.SetCorner/1` | UI / Popups |
| Set Delay | api | `UITooltip.SetDelay/1` | UI / Popups |
| Set Duration | api | `UIToast.SetDuration/1` | UI / Popups |
| Set Largest Stack | api | `UIToast.SetMaxStack/1` | UI / Popups |
| Set Offset | api | `UITooltip.SetOffset/2` | UI / Popups |
| Set Rich Text | api | `UITooltip.SetRichText/1` | UI / Popups |
| Set Toast Corner | builtin | `Fn.set_toast_corner` | UI |
| Set Toast Duration | builtin | `Fn.set_toast_duration` | UI |
| Set Toast Largest | builtin | `Fn.set_toast_max` | UI |
| Set Tooltip Delay | builtin | `Fn.set_tooltip_delay` | UI |
| Set Tooltip Offset | builtin | `Fn.set_tooltip_offset` | UI |
| Set Tooltip Rich | builtin | `Fn.set_tooltip_rich` | UI |
| Toast Count | builtin | `Fn.toast_count` | UI |
| Toast Push | builtin | `Fn.toast_push` | UI |

##### Popups & Dialogs / Modals & Sheets

| Node | Kind | Id | Was |
|---|---|---|---|
| Close | api | `UIModal.Close/0` | UI / Popups |
| Is Open | api | `UIModal.IsOpen/0` | UI / Popups |
| Modal Close | builtin | `Fn.modal_close` | Advanced |
| Modal Open | builtin | `Fn.modal_open` | Advanced |
| Open | api | `UIModal.Open/0` | UI / Popups |
| Screen Sheet Close | builtin | `Fn.ui_sheet_close` | UI |
| Screen Sheet Open | builtin | `Fn.ui_sheet_open` | UI |
| Screen Sheet Open State | builtin | `Fn.ui_sheet_open_state` | UI |
| Set Cancel Text | api | `UIModal.SetCancelText/1` | UI / Popups |
| Set Card Radius | api | `UIModal.SetCardRadius/1` | UI / Popups |
| Set Confirm Text | api | `UIModal.SetConfirmText/1` | UI / Popups |
| Set Message | api | `UIModal.SetMessage/1` | UI / Popups |
| Set Modal Cancel | builtin | `Fn.set_modal_cancel` | Advanced |
| Set Modal Confirm | builtin | `Fn.set_modal_confirm` | Advanced |
| Set Modal Message | builtin | `Fn.set_modal_message` | Advanced |
| Set Modal Style | builtin | `Fn.set_modal_style` | Advanced |
| Set Modal Title | builtin | `Fn.set_modal_title` | Advanced |
| Set Style Card | api | `UIModal.SetStyleCard/1` | UI / Popups |
| Set Title | api | `UIModal.SetTitle/1` | UI / Popups |

##### Scenes & Levels

| Node | Kind | Id | Was |
|---|---|---|---|
| Application Quit | builtin | `Fn.Application.Quit` | Advanced |
| Load Next Scene | builtin | `Fn.load_next_scene` | Scene |
| Load Scene | node | `LoadScene` | Scene |
| Load Scene # | step | `load_scene_index` | Scene |
| Load Scene Additive | builtin | `Fn.load_scene_additive` | Scene |
| Load Scene Index | builtin | `Fn.load_scene_index` | Scene |
| Load Scene Name | builtin | `Fn.load_scene_name` | Scene |
| Quit | builtin | `Fn.quit` | Advanced |
| Quit Game | node | `QuitGame` | Scene |
| Reload Scene | builtin | `Fn.reload_scene` | Scene |
| Save Scene | builtin | `Fn.save_scene` | Scene |
| Scene Count | builtin | `Fn.scene_count` | Scene |
| Scene Index | builtin | `Fn.scene_index` | Scene |
| Scene Manager Load Scene | builtin | `Fn.SceneManager.LoadScene` | Advanced |
| Scene Name | builtin | `Fn.scene_name` | Scene |

##### Lighting & Sky

| Node | Kind | Id | Was |
|---|---|---|---|
| Ambient | builtin | `Fn.ambient` | Advanced |
| Set Ambient | builtin | `Fn.set_ambient` | Advanced |
| Set Ambient Light | step | `set_ambient` | Scene / Atmosphere |
| Set Background | builtin | `Fn.set_background` | Advanced |
| Set Background Color | builtin | `Fn.set_background_color` | Advanced |
| Set Light | builtin | `Fn.set_light` | Advanced |
| Set Light Angle | builtin | `Fn.set_light_angle` | Advanced |
| Set Light Color | builtin | `Fn.set_light_color` | Advanced |
| Set Light Direction | builtin | `Fn.set_light_direction` | Advanced |
| Set Light Intensity | builtin | `Fn.set_light_intensity` | Advanced |
| Set Light Range | builtin | `Fn.set_light_range` | Advanced |
| Time of Day | builtin | `Fn.time_of_day` | Advanced |

##### Weather & Fog

| Node | Kind | Id | Was |
|---|---|---|---|
| Fog Set | builtin | `Fn.fog_set` | Scene |
| Get Preset | api | `Weather.preset` | Scene / Atmosphere |
| Is Enabled | api | `Fog.enabled` | Scene / Atmosphere |
| Set Enabled | api | `Fog.enabled=` | Scene / Atmosphere |
| Set Fog | node | `SetFog` | Scene / Atmosphere |
| Set Mode | api | `Fog.SetMode/2` | Scene / Atmosphere |
| Set Weather | node | `SetWeather` | Scene / Atmosphere |
| Set Wind | api | `Weather.SetWind/3` | Scene / Atmosphere |
| Weather Set | builtin | `Fn.weather_set` | Scene |

##### Post Effects

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Exposure | api | `PostProcess.exposure` | Scene / Atmosphere |
| Post Set | builtin | `Fn.post_set` | Advanced |
| Set | api | `PostProcess.Set/2` | Scene / Atmosphere |
| Set Exposure | api | `PostProcess.exposure=` | Scene / Atmosphere |
| Set Post Effect | node | `SetPostEffect` | Scene / Atmosphere |

##### Window & Display

| Node | Kind | Id | Was |
|---|---|---|---|
| Ask to Keep Display | step | `begin_display_confirm` | Scene |
| Begin Display Confirm | builtin | `Fn.begin_display_confirm` | Advanced |
| Get Display Confirm Seconds | builtin | `Fn.get_display_confirm_seconds` | Advanced |
| Get Display Height | builtin | `Fn.get_display_height` | Advanced |
| Get Display Mode | builtin | `Fn.get_display_mode` | Advanced |
| Get Display Resolutions | builtin | `Fn.get_display_resolutions` | Advanced |
| Get Display Width | builtin | `Fn.get_display_width` | Advanced |
| Get Monitor | builtin | `Fn.get_monitor` | Advanced |
| Get Monitors | builtin | `Fn.get_monitors` | Advanced |
| Get Refresh Rate | builtin | `Fn.get_refresh_rate` | Advanced |
| Get Refresh Rates | builtin | `Fn.get_refresh_rates` | Advanced |
| Keep Display Settings | builtin | `Fn.keep_display_settings` | Advanced |
| Revert Display Settings | builtin | `Fn.revert_display_settings` | Advanced |
| Screen Height | builtin | `Fn.screen_h` | Advanced |
| Screen Width | builtin | `Fn.screen_w` | Advanced |
| Set Display Mode | builtin | `Fn.set_display_mode` | Advanced |
| Set Display Resolution | builtin | `Fn.set_display_resolution` | Advanced |
| Set Monitor | builtin | `Fn.set_monitor` | Advanced |
| Set Refresh Rate | builtin | `Fn.set_refresh_rate` | Advanced |

##### Cutscenes

| Node | Kind | Id | Was |
|---|---|---|---|
| Get Last Event | api | `Sequence.lastEvent` | Animation / Cutscenes |
| Is Playing | api | `Sequence.isPlaying` | Animation / Cutscenes |
| Play | api | `Sequence.Play/1` | Animation / Cutscenes |
| Play Sequence | node | `PlaySequence` | Scene / Story |
| Sequence Playing | builtin | `Fn.sequence_playing` | Advanced |
| Sequence Time | builtin | `Fn.sequence_time` | Advanced |
| Stop | api | `Sequence.Stop/0` | Animation / Cutscenes |
| Stop Sequence | node | `StopSequence` | Scene / Story |

##### Keyboard

| Node | Kind | Id | Was |
|---|---|---|---|
| Any Of These Keys | check | `key_any` | Input / Keys |
| Get Key | api | `Input.GetKey/1` | Input / Keys |
| Get Key Down | api | `Input.GetKeyDown/1` | Input / Keys |
| Get Key up | api | `Input.GetKeyUp/1` | Input / Keys |
| Input Get Key | builtin | `Fn.Input.GetKey` | Input |
| Input Get Key Down | builtin | `Fn.Input.GetKeyDown` | Input |
| Input Get Key up | builtin | `Fn.Input.GetKeyUp` | Input |
| Is Any Key Held | gameplay | `GlIsAnyKeyHeld` | Input / Keys |
| Is Crouching | gameplay | `GlIsCrouching` | Input / Keys |
| Is Interact Held | gameplay | `GlIsInteractHeld` | Input / Keys |
| Is Jump Held | gameplay | `GlIsJumpHeld` | Input / Keys |
| Is Key Down | builtin | `Fn.is_key_down` | Input |
| Is Key Held | builtin | `Fn.is_key_held` | Input |
| Is Sprinting | gameplay | `GlIsSprinting` | Input / Keys |
| Key | builtin | `Fn.key` | Input |
| Key Down | node | `KeyDown` | Input / Keys |
| Key Held | node | `Key` | Input / Keys |
| Key Is Held | check | `key` | Input / Keys |
| Key Just Pressed | check | `key_down` | Input / Keys |
| Key Just Released | check | `key_up` | Input / Keys |
| Key Pressed | builtin | `Fn.key_pressed` | Input |
| Key Released | builtin | `Fn.key_released` | Input |
| Key up | builtin | `Fn.key_up` | Input |
| Key Up | node | `KeyUp` | Input / Keys |

##### Mouse & Touch

| Node | Kind | Id | Was |
|---|---|---|---|
| Cursor Lock | builtin | `Fn.cursor_lock` | Advanced |
| Cursor Lock Mode | builtin | `Fn.cursor_lock_mode` | Advanced |
| Get Mouse Button | api | `Input.GetMouseButton/1` | Input / Mouse |
| Get Mouse Button Down | api | `Input.GetMouseButtonDown/1` | Input / Mouse |
| Get Mouse Button up | api | `Input.GetMouseButtonUp/1` | Input / Mouse |
| Get Mouse Position | api | `Input.mousePosition` | Input / Mouse |
| Input Get Mouse Button | builtin | `Fn.Input.GetMouseButton` | Input |
| Input Get Mouse Button Down | builtin | `Fn.Input.GetMouseButtonDown` | Input |
| Input Get Mouse Button up | builtin | `Fn.Input.GetMouseButtonUp` | Input |
| Is Aim Held | gameplay | `GlIsAimHeld` | Input / Mouse |
| Is Fire Held | gameplay | `GlIsFireHeld` | Input / Mouse |
| Is Mouse Held | builtin | `Fn.is_mouse_held` | Input |
| Mouse | builtin | `Fn.mouse` | Input |
| Mouse Button Held | check | `mouse` | Input / Mouse |
| Mouse Down | builtin | `Fn.mouse_down` | Input |
| Mouse Ground | node | `MouseGround` | Input / Mouse |
| Mouse Held | node | `MouseButton` | Input / Mouse |
| Mouse Just Clicked | check | `mouse_down` | Input / Mouse |
| Mouse Position Right | builtin | `Fn.mouse_position_x` | Input |
| Mouse Position Vertical | builtin | `Fn.mouse_position_y` | Input |
| Mouse Pressed | node | `MouseButtonDown` | Input / Mouse |
| Mouse Released | builtin | `Fn.mouse_released` | Input |
| Mouse Right | builtin | `Fn.mouse_x` | Input |
| Mouse up | builtin | `Fn.mouse_up` | Input |
| Mouse Vertical | builtin | `Fn.mouse_y` | Input |
| Mouse Wheel | node | `MouseWheel` | Input / Mouse |
| Mouse X | node | `MouseX` | Input / Mouse |
| Mouse Y | node | `MouseY` | Input / Mouse |
| Show Cursor | node | `ShowCursor` | Input / Mouse |

##### Mouse & Touch / Touch & Gestures

| Node | Kind | Id | Was |
|---|---|---|---|
| Gesture | api | `Input.Gesture/1` | Input / Controls |
| Get Pinch Scale | node | `GesturePinchScale` | Input |
| Get Swipe Direction | node | `GestureSwipeDirection` | Input |
| Get Twist Angle | node | `GestureTwist` | Input |
| Get Twist Degrees | api | `Input.twistDegrees` | Input / Controls |
| Is Touch Screen | api | `Input.touchScreen` | Input / Controls |
| Pinch Scale | builtin | `Fn.pinch_scale` | Input |
| Swipe Direction | builtin | `Fn.swipe_direction` | Input |
| Twist Degrees | builtin | `Fn.twist_degrees` | Input |

##### Gamepad & Controls

| Node | Kind | Id | Was |
|---|---|---|---|
| Axis Right | builtin | `Fn.axis_x` | Input |
| Axis Vertical | builtin | `Fn.axis_y` | Input |
| Axis X | node | `AxisX` | Input / Axes |
| Axis Y | node | `AxisY` | Input / Axes |
| Gamepad | builtin | `Fn.gamepad` | Input |
| Gamepad Down | builtin | `Fn.gamepad_down` | Input |
| Gamepad Right | builtin | `Fn.gamepad_x` | Input |
| Gamepad Vertical | builtin | `Fn.gamepad_y` | Input |
| Get Axis | api | `Input.GetAxis/1` | Input / Axes |
| Get Button | api | `Input.GetButton/1` | Input / Controls |
| Get Button Down | api | `Input.GetButtonDown/1` | Input / Controls |
| Get Button up | api | `Input.GetButtonUp/1` | Input / Controls |
| Get Horizontal Input | gameplay | `GlGetHorizontalInput` | Input / Axes |
| Get Move Direction | node | `GetMoveAxis` | Input / Axes |
| Get Move Strength | gameplay | `GlGetMoveStrength` | Input / Axes |
| Get Vertical Input | gameplay | `GlGetVerticalInput` | Input / Axes |
| Input Get Axis | builtin | `Fn.Input.GetAxis` | Input |
| Input Get Axis Raw | builtin | `Fn.Input.GetAxisRaw` | Input |
| Input Get Button | builtin | `Fn.Input.GetButton` | Input |
| Input Get Button Down | builtin | `Fn.Input.GetButtonDown` | Input |
| Input Get Button up | builtin | `Fn.Input.GetButtonUp` | Input |
| Input Horizontal | builtin | `Fn.input_horizontal` | Input |
| Input Vertical | builtin | `Fn.input_vertical` | Input |
| Is Gamepad Connected | api | `Input.gamepadConnected` | Input / Controls |
| Move Input Right | builtin | `Fn.move_input_x` | Input |
| Move Input Vertical | builtin | `Fn.move_input_y` | Input |

##### Gamepad & Controls / Named Actions

| Node | Kind | Id | Was |
|---|---|---|---|
| Action Down | builtin | `Fn.action_down` | Advanced |
| Action Right | builtin | `Fn.action_x` | Advanced |
| Action Time | builtin | `Fn.action_time` | Advanced |
| Action up | builtin | `Fn.action_up` | Advanced |
| Action Value | builtin | `Fn.action_value` | Advanced |
| Action Vertical | builtin | `Fn.action_y` | Advanced |
| Define Action | api | `Input.DefineAction/3` | Input / Controls |
| Define Direction | api | `Input.DefineDirection/3` | Input / Controls |
| Get Action Value | api | `Input.GetActionValue/1` | Input / Controls |
| Get Action Vector | api | `Input.GetActionVector/1` | Input / Axes |
| Rebind | builtin | `Fn.rebind` | Advanced |
| Rebind Action | api | `Input.RebindAction/2` | Input / Controls |
| Rumble | api | `Input.Rumble/3` | Input / Controls |

##### Multiplayer / Hosting & Joining

| Node | Kind | Id | Was |
|---|---|---|---|
| Disconnect | node | `NetDisconnect` | Online |
| Host Game | node | `NetHost` | Online |
| Is Client | node | `NetIsClient` | Online |
| Is Connected | node | `NetConnected` | Online |
| Is Server | node | `NetIsServer` | Online |
| Join Game | node | `NetJoin` | Online |
| My Net Id | node | `NetId` | Online |
| Net Connected | builtin | `Fn.net_connected` | Online |
| Net Disconnect | builtin | `Fn.net_disconnect` | Online |
| Net Host | builtin | `Fn.net_host` | Online |
| Net Invite Friends | builtin | `Fn.net_invite_friends` | Online |
| Net is Client | builtin | `Fn.net_is_client` | Online |
| Net is Server | builtin | `Fn.net_is_server` | Online |
| Net Join | builtin | `Fn.net_join` | Online |
| Net Launch Join | builtin | `Fn.net_launch_join` | Online |
| Net Ping | builtin | `Fn.net_ping` | Online |
| Net Poll | builtin | `Fn.net_poll` | Online |
| Net Room | builtin | `Fn.net_room` | Online |
| Net Server Name | builtin | `Fn.net_server_name` | Online |
| Net Tick | builtin | `Fn.net_tick` | Online |
| Net Tick Rate | builtin | `Fn.net_tick_rate` | Online |
| Peer Count | node | `NetPeers` | Online |

##### Multiplayer / Server Browser

| Node | Kind | Id | Was |
|---|---|---|---|
| Net Browser Address | builtin | `Fn.net_browser_address` | Online |
| Net Browser Busy | builtin | `Fn.net_browser_busy` | Online |
| Net Browser Close | builtin | `Fn.net_browser_close` | Online |
| Net Browser Count | builtin | `Fn.net_browser_count` | Online |
| Net Browser is Open | builtin | `Fn.net_browser_is_open` | Online |
| Net Browser Join | builtin | `Fn.net_browser_join` | Online |
| Net Browser Largest | builtin | `Fn.net_browser_max` | Online |
| Net Browser Locked | builtin | `Fn.net_browser_locked` | Online |
| Net Browser Map | builtin | `Fn.net_browser_map` | Online |
| Net Browser Name | builtin | `Fn.net_browser_name` | Online |
| Net Browser Open | builtin | `Fn.net_browser_open` | Online |
| Net Browser Ping | builtin | `Fn.net_browser_ping` | Online |
| Net Browser Players | builtin | `Fn.net_browser_players` | Online |
| Net Browser Refresh | builtin | `Fn.net_browser_refresh` | Online |
| Open Server Browser | step | `net_browser_open` | Online |

##### Multiplayer / Matchmaking & Relay

| Node | Kind | Id | Was |
|---|---|---|---|
| Addr | builtin | `Fn.mm_addr` | Advanced |
| Cancel Find Match | step | `net_queue_cancel` | Online |
| Find Match | step | `net_queue_join` | Online |
| Heartbeat | builtin | `Fn.mm_heartbeat` | Advanced |
| Host | builtin | `Fn.mm_host` | Advanced |
| Host via Relay | step | `net_host_relay` | Online |
| Join via Relay | step | `net_join_relay` | Online |
| Name | builtin | `Fn.mm_name` | Advanced |
| Net Host Relay | builtin | `Fn.net_host_relay` | Online |
| Net Join Relay | builtin | `Fn.net_join_relay` | Online |
| Net Queue Accept | builtin | `Fn.net_queue_accept` | Online |
| Net Queue Auto | builtin | `Fn.net_queue_auto` | Online |
| Net Queue Cancel | builtin | `Fn.net_queue_cancel` | Online |
| Net Queue Count | builtin | `Fn.net_queue_count` | Online |
| Net Queue Error | builtin | `Fn.net_queue_error` | Online |
| Net Queue is Host | builtin | `Fn.net_queue_is_host` | Online |
| Net Queue Join | builtin | `Fn.net_queue_join` | Online |
| Net Queue State | builtin | `Fn.net_queue_state` | Online |
| Net Queue Time | builtin | `Fn.net_queue_time` | Online |
| Net Relay Ready | builtin | `Fn.net_relay_ready` | Online |
| Players | builtin | `Fn.mm_players` | Advanced |
| Port | builtin | `Fn.mm_port` | Advanced |
| Refresh | builtin | `Fn.mm_refresh` | Advanced |
| Relay Ready | check | `net_relay_ready` | Online |
| Room | builtin | `Fn.mm_room` | Advanced |
| Unregister | builtin | `Fn.mm_unregister` | Advanced |
| Using Relay | check | `net_relayed` | Online |

##### Multiplayer / Messages & RPC

| Node | Kind | Id | Was |
|---|---|---|---|
| Call Event (RPC) | step | `net_rpc` | Online |
| Call Object RPC | node | `NetObjRpc` | Online |
| Call RPC | node | `NetRpc` | Online |
| Net Chat | builtin | `Fn.net_chat` | Online |
| Net Message Channel | builtin | `Fn.net_msg_channel` | Online |
| Net Message Data | builtin | `Fn.net_msg_data` | Online |
| Net Message From | builtin | `Fn.net_msg_from` | Online |
| Net Network Message | builtin | `Fn.net_rpc` | Online |
| Net Object Network Message | builtin | `Fn.net_obj_rpc` | Online |
| Net Object Network Message Data | builtin | `Fn.net_obj_rpc_data` | Online |
| Net Object Network Message From | builtin | `Fn.net_obj_rpc_from` | Online |
| Net Object Network Message Name | builtin | `Fn.net_obj_rpc_name` | Online |
| Net Object Network Message Poll | builtin | `Fn.net_obj_rpc_poll` | Online |
| Net Object Network Message Reliable | builtin | `Fn.net_obj_rpc_reliable` | Online |
| Net Object Network Message to | builtin | `Fn.net_obj_rpc_to` | Online |
| Net Send | builtin | `Fn.net_send` | Online |
| Net Send Reliable | builtin | `Fn.net_send_reliable` | Online |
| Net Send Reliable to | builtin | `Fn.net_send_reliable_to` | Online |
| Net Send to | builtin | `Fn.net_send_to` | Online |
| Send Chat | node | `NetChat` | Online |
| Send Message | node | `NetSend` | Online |
| Send Message (fast) | step | `net_send` | Online |
| Send Message (sure) | step | `net_send_reliable` | Online |

##### Multiplayer / Synced Objects & Variables

| Node | Kind | Id | Was |
|---|---|---|---|
| Despawn Networked | step | `net_despawn` | Online |
| Get Net Var | node | `NetGetVar` | Online |
| Get Sync Var | node | `SyncVarGet` | Online |
| Net Despawn | node | `NetDespawn` | Online |
| Net Get | builtin | `Fn.net_get` | Online |
| Net Object Identifier | builtin | `Fn.net_obj_id` | Online |
| Net Object Owned | builtin | `Fn.net_obj_owned` | Online |
| Net Set | builtin | `Fn.net_set` | Online |
| Net Set Pawn | builtin | `Fn.net_set_pawn` | Online |
| Net Spawn | node | `NetSpawnOwned` | Online |
| Net Spawn Owned | builtin | `Fn.net_spawn_owned` | Online |
| Net Syncvar Get | builtin | `Fn.net_syncvar_get` | Online |
| Net Syncvar Set | builtin | `Fn.net_syncvar_set` | Online |
| Net Teleport | builtin | `Fn.net_teleport` | Online |
| Set Net Var | node | `NetSetVar` | Online |
| Set Network Value | step | `net_set` | Online |
| Set Sync Var | node | `SyncVarSet` | Online |
| Spawn Mine (synced) | step | `net_spawn_owned` | Online |
| Spawn Networked | step | `net_spawn` | Online |

##### Multiplayer / Players & Matches

| Node | Kind | Id | Was |
|---|---|---|---|
| All Ready | node | `NetAllReady` | Online |
| End Match | step | `net_match_end` | Online |
| Mark Ready | step | `net_ready` | Online |
| Match Started | node | `NetMatchStarted` | Online |
| Net All Ready | builtin | `Fn.net_all_ready` | Online |
| Net Identifier | builtin | `Fn.net_id` | Online |
| Net Match End | builtin | `Fn.net_match_end` | Online |
| Net Match Seconds | builtin | `Fn.net_match_seconds` | Online |
| Net Match Start | builtin | `Fn.net_match_start` | Online |
| Net Match Started | builtin | `Fn.net_match_started` | Online |
| Net Match Status | builtin | `Fn.net_match_status` | Online |
| Net Name | builtin | `Fn.net_name` | Online |
| Net Peer Name | builtin | `Fn.net_peer_name` | Online |
| Net Peers | builtin | `Fn.net_peers` | Online |
| Net Ready | builtin | `Fn.net_ready` | Online |
| Net Ready Count | builtin | `Fn.net_ready_count` | Online |
| Net Roster Count | builtin | `Fn.net_roster_count` | Online |
| Net Roster Identifier | builtin | `Fn.net_roster_id` | Online |
| Net Score | builtin | `Fn.net_score` | Online |
| Net Set Score | builtin | `Fn.net_set_score` | Online |
| Net Start Match | builtin | `Fn.net_start_match` | Online |
| Net Stats Mine | builtin | `Fn.net_stats_mine` | Online |
| Net Stats Top | builtin | `Fn.net_stats_top` | Online |
| Net User Identifier | builtin | `Fn.net_user_id` | Online |
| Set Ready | node | `NetReady` | Online |
| Start Match | node | `NetStartMatch` | Online |

##### Steam

| Node | Kind | Id | Was |
|---|---|---|---|
| Steam Language | builtin | `Fn.steam_language` | Online |
| Steam Overlay | builtin | `Fn.steam_overlay` | Online |
| Steam Owns | builtin | `Fn.steam_owns` | Online |
| Steam Owns Dlc | builtin | `Fn.steam_owns_dlc` | Online |

##### Steam / Achievements & Stats

| Node | Kind | Id | Was |
|---|---|---|---|
| Achievement Unlocked | node | `SteamIsUnlocked` | Online |
| Get Steam Stat | node | `SteamGetStat` | Online |
| Set Steam Stat | node | `SteamSetStat` | Online |
| Steam Achievement | step | `steam_unlock` | Online / Steam |
| Steam Achievement Count | builtin | `Fn.steam_achievement_count` | Online |
| Steam Achievement Name | builtin | `Fn.steam_achievement_name` | Online |
| Steam Add Int Stat | step | `steam_inc_stat_int` | Online / Steam |
| Steam Add Stat | step | `steam_inc_stat` | Online / Steam |
| Steam Clear | builtin | `Fn.steam_clear` | Online |
| Steam Get Stat | builtin | `Fn.steam_get_stat` | Online |
| Steam Get Stat Number | builtin | `Fn.steam_get_stat_int` | Online |
| Steam Inc Stat | builtin | `Fn.steam_inc_stat` | Online |
| Steam Inc Stat Number | builtin | `Fn.steam_inc_stat_int` | Online |
| Steam is Unlocked | builtin | `Fn.steam_is_unlocked` | Online |
| Steam Leaderboard | builtin | `Fn.steam_leaderboard` | Online |
| Steam Leaderboard Top | builtin | `Fn.steam_leaderboard_top` | Online |
| Steam Progress | builtin | `Fn.steam_progress` | Online |
| Steam Set Stat | builtin | `Fn.steam_set_stat` | Online |
| Steam Set Stat Number | builtin | `Fn.steam_set_stat_int` | Online |
| Steam Store | builtin | `Fn.steam_store` | Online |
| Steam Store Stats | step | `steam_store` | Online / Steam |
| Steam Unlock | builtin | `Fn.steam_unlock` | Online |
| Store Stats | node | `SteamStore` | Online |
| Submit Score | node | `SteamLeaderboard` | Online |
| Unlock Achievement | node | `SteamUnlock` | Online |

##### Steam / Lobbies & Friends

| Node | Kind | Id | Was |
|---|---|---|---|
| Steam Create Lobby | step | `steam_lobby_create` | Online / Steam |
| Steam Friend Identifier | builtin | `Fn.steam_friend_id` | Online |
| Steam Friend Name | builtin | `Fn.steam_friend_name` | Online |
| Steam Friends | builtin | `Fn.steam_friends` | Online |
| Steam Get Presence | builtin | `Fn.steam_get_presence` | Online |
| Steam Invite | builtin | `Fn.steam_invite` | Online |
| Steam Invite Friend | step | `steam_invite` | Online / Steam |
| Steam Join Lobby | step | `steam_lobby_join` | Online / Steam |
| Steam Leave Lobby | step | `steam_lobby_leave` | Online / Steam |
| Steam Lobby Create | builtin | `Fn.steam_lobby_create` | Online |
| Steam Lobby Current | builtin | `Fn.steam_lobby_current` | Online |
| Steam Lobby Get Data | builtin | `Fn.steam_lobby_get_data` | Online |
| Steam Lobby Join | builtin | `Fn.steam_lobby_join` | Online |
| Steam Lobby Leave | builtin | `Fn.steam_lobby_leave` | Online |
| Steam Lobby List | builtin | `Fn.steam_lobby_list` | Online |
| Steam Lobby Members | builtin | `Fn.steam_lobby_members` | Online |
| Steam Lobby Set Data | builtin | `Fn.steam_lobby_set_data` | Online |
| Steam Name | node | `SteamName` | Online |
| Steam Presence | builtin | `Fn.steam_presence` | Online |
| Steam Set Lobby Data | step | `steam_lobby_set_data` | Online / Steam |
| Steam Set Presence | step | `steam_presence` | Online / Steam |

##### Steam / Cloud & Workshop

| Node | Kind | Id | Was |
|---|---|---|---|
| Steam Cloud Count | builtin | `Fn.steam_cloud_count` | Online |
| Steam Cloud Delete | builtin | `Fn.steam_cloud_delete` | Online |
| Steam Cloud Has | builtin | `Fn.steam_cloud_has` | Online |
| Steam Cloud Read | builtin | `Fn.steam_cloud_read` | Online |
| Steam Cloud Save | step | `steam_cloud_write` | Online / Steam |
| Steam Cloud Write | builtin | `Fn.steam_cloud_write` | Online |
| Steam Workshop Count | builtin | `Fn.steam_workshop_count` | Online |
| Steam Workshop Path | builtin | `Fn.steam_workshop_path` | Online |
| Steam Workshop Publish | builtin | `Fn.steam_workshop_publish` | Online |
| Steam Workshop Subscribe | builtin | `Fn.steam_workshop_subscribe` | Online |
| Steam Workshop Unsubscribe | builtin | `Fn.steam_workshop_unsubscribe` | Online |

##### Server Security

| Node | Kind | Id | Was |
|---|---|---|---|
| Kick Player | step | `net_kick` | Online |
| Net Anticheat | builtin | `Fn.net_anticheat` | Online |
| Net Anticheat Load | builtin | `Fn.net_anticheat_load` | Online |
| Net Auth Token | builtin | `Fn.net_auth_token` | Online |
| Net Authoritative | builtin | `Fn.net_authoritative` | Online |
| Net Budget | builtin | `Fn.net_budget` | Online |
| Net Encrypt | builtin | `Fn.net_encrypt` | Online |
| Net Encrypted | builtin | `Fn.net_encrypted` | Online |
| Net Hit | builtin | `Fn.net_hit` | Online |
| Net Kick | builtin | `Fn.net_kick` | Online |
| Net Largest Speed | builtin | `Fn.net_max_speed` | Online |
| Net Rate Limit | builtin | `Fn.net_rate_limit` | Online |
| Net Violations | builtin | `Fn.net_violations` | Online |
| Net Was Kicked | builtin | `Fn.net_was_kicked` | Online |
| Reject Hit | node | `RejectHit` | Online |
| Reject Rpc | node | `RejectRpc` | Online |
| Server Budget | node | `ServerBudget` | Online |

##### Accounts & Online Saves / Accounts

| Node | Kind | Id | Was |
|---|---|---|---|
| Account Error | builtin | `Fn.account_error` | Advanced |
| Account Get | builtin | `Fn.account_get` | Advanced |
| Account is Logged in | builtin | `Fn.account_is_logged_in` | Advanced |
| Account is Online | builtin | `Fn.account_is_online` | Advanced |
| Account Login | builtin | `Fn.account_login` | Advanced |
| Account Logout | builtin | `Fn.account_logout` | Advanced |
| Account Post | builtin | `Fn.account_post` | Advanced |
| Account Register | builtin | `Fn.account_register` | Advanced |
| Account Token | builtin | `Fn.account_token` | Advanced |
| Account Username | builtin | `Fn.account_username` | Advanced |
| Account Verify | builtin | `Fn.account_verify` | Advanced |
| Error | builtin | `Fn.pf_error` | Advanced |
| Identifier | builtin | `Fn.pf_id` | Advanced |
| Logged in | builtin | `Fn.pf_logged_in` | Advanced |

##### Accounts & Online Saves / Cloud Saves

| Node | Kind | Id | Was |
|---|---|---|---|
| Cloud Delete | builtin | `Fn.cloud_delete` | Advanced |
| Cloud Has | builtin | `Fn.cloud_has` | Advanced |
| Cloud List | builtin | `Fn.cloud_list` | Lists & Maps / Lists |
| Cloud Load | builtin | `Fn.cloud_load` | Advanced |
| Cloud Save | builtin | `Fn.cloud_save` | Variables / Saved |

##### Accounts & Online Saves / Leaderboards & PlayFab

| Node | Kind | Id | Was |
|---|---|---|---|
| Leaderboard Submit | builtin | `Fn.leaderboard_submit` | Advanced |
| Leaderboard Top | builtin | `Fn.leaderboard_top` | Advanced |
| PlayFab Leaderboard | step | `playfab_leaderboard` | Online / PlayFab |
| PlayFab Load Data | step | `playfab_get_data` | Online / PlayFab |
| PlayFab Login | step | `playfab_login` | Online / PlayFab |
| PlayFab Save Data | step | `playfab_set_data` | Online / PlayFab |
| PlayFab Set Name | step | `playfab_name` | Online / PlayFab |
| PlayFab Set Stat | step | `playfab_set_stat` | Online / PlayFab |

##### Debug

| Node | Kind | Id | Was |
|---|---|---|---|
| Watch | builtin | `Fn.watch` | Advanced |

<!-- generated:end -->

## Plain words and fewer blocks (internal note, 2026-11-17)

Bar for every name, description and recipe: "imagine a 10 year old was using it".

### What already existed (not rebuilt)

- Names: `editor/VisualScriptNames.hpp` (title, old title kept as a search word, pin names) plus a style check
  (`LintVsTitle`). Hand names, generated engine-API names and text-script builtin names all end up in `Registry()`.
- Help sentence and example: `VisualScriptNodeHelp.hpp`, `VisualScriptNodeDocs.inc`, `VsNodeExample`.
- Starter group (26 blocks, now 29), levels (Basic, Intermediate, Advanced), the palette search, a Templates window
  (30 long templates) and a small Recipes menu with 5 older entries.
- Saved identifiers: the `.okayvs` file stores the type token, not the title, so a rename of a title never breaks a
  saved graph. `okay::vs::CanonicalNodeType` (NodeAliases.hpp) is the old-token-to-new-token table; it is still empty
  because no token was retired in this pass.

### Glossary (the list the test enforces: `editor/VisualScriptGlossary.hpp`)

Banned in a palette name or description: variable, boolean, float, integer, vector, instance, callback, handler,
parameter, argument, iterate, index, string, array, function, node, euler, quaternion, gameobject, prefab, transform,
rigidbody, api, enum, struct, null, void, const, cast, json, rpc, lerp, raycast, loop, var, anim, num, id, nav, ik,
min, max, deg, dist (and their plurals). `VisualScriptPlain.hpp` swaps each for an everyday word when a name or help
line is shown: variable -> saved value, vector -> direction, vec3 -> position, rigidbody -> physics body,
prefab -> saved object, function -> named action, string -> text, array -> list, node -> block, loop -> repeat,
raycast -> line check, index -> place number, rpc -> remote call. Hand-picked names (`VsPlainTitleFor`) win over the swap.
The allow-list (`kVsGlossaryAllowed`) has four words, each with a reason, and the tests do not use it.
Verbs first, 2 to 4 words, small words lowercase ("Change Score by"), events keep "On ...".

### Counts (blocks and wires, current set vs the new one-click recipes; measured by tests/test_vs_recipes.cpp)

| Task | Before | Recipe now |
| --- | --- | --- |
| Move with keys | 12 blocks, 12 wires (Move with WASD) | 7 blocks, 6 wires |
| Jump | 3 blocks, 2 wires | 2 blocks, 1 wire |
| Score (coin) | 6 blocks, 5 wires (Pickup) | 3 blocks, 2 wires |
| Open door | 19 blocks, 18 wires | 2 blocks, 1 wire |
| Enemy follows | 3 blocks, 2 wires (Chase) | 2 blocks, 1 wire |
| Show text | 3 blocks, 2 wires (Message) | 2 blocks, 1 wire |
| Play sound on click | not in the template list | 2 blocks, 1 wire |
| Game over at 0 health | 13 blocks, 12 wires (Win or lose, also does score) | 2 blocks, 1 wire |
| Spawn on click | 8 blocks, 7 wires (Spawner, timed) | 2 blocks by hand (On Click, Spawn Copy Of), not a recipe yet |
| Change scene | not measured | 2 blocks by hand (On Key Pressed, Load Scene), not a recipe yet |

Halved: door, score, show text, follow, jump (3 to 2). Move with keys is cut by 42 percent (12 to 7 blocks), wires by
half. Not halved yet: nothing needs a separate number or "this object" block in any recipe (the numbers are typed on
the block, "Target" empty means this object), but moving with two axes still needs a Multiply block per axis.
A single "Move With Keys" block would make it 2 blocks; that macro block is NOT built.

Budgets live in the data (`VsRecipe::maxNodes/maxWires`) and the test fails when a recipe grows past them.

### Quick blocks, default palette and counts (second pass, 2026-11-18)

Quick blocks (`engine/include/okay/VisualScript/QuickBlocks.hpp`): 30 one-block actions, written as DATA (a table row
per block: plain name, one sentence, example, typed fields with defaults, and a small text body naming the ordinary
low-level nodes and wires it stands for). One generic expander (`ExpandQuickBlocks`, called at the start of
`NodeGraph::Parse`) turns each block into those nodes when a graph loads. A saved graph stores only the block token
and the values typed on it (`node 3 QbAddToScore` + `default 3 0 5`), so it is one node in the editor and in the file.
Continuous blocks (Move With Keys, Follow Object, Move To Spot, Walk Back And Forth, Spin, Show Score, Do Forever) start
on their own, so they need no event and no wire. The blocks that already exist as ONE ordinary node were not
duplicated: Heal, Save Checkpoint, Shake Camera, Say Text (Print), Add Score, Wait Then Do (Delay), Repeat Times.

Default palette: 2447 palette nodes in the whole library; **55 by default** (30 Quick blocks and 25 basics) in six
groups (Events 9, Move 9, Look 10, Game rules 9, Sound and screen 6, Flow 12); **2447 with "Show all nodes"**. The
budget (60 in total, 12 per group) is a constant in `VisualScriptAdvanced.hpp` and a test fails above it. Search finds
hidden nodes and marks them Advanced; "Search all nodes" (or the switch) lists them. Beginner mode shows the six
groups plus Everything, Recent and Favourites and no topic tree.

Blocks and wires for the small complete game (collect 5 coins, patrolling enemy, game over on touch, show score,
win at 5), as scripts on 4 objects:

| | Before (templates) | Now |
| --- | --- | --- |
| Player moves with arrow keys | 12 blocks, 12 wires | 1 block, 0 wires |
| Coin (all five share it) | 6 blocks, 5 wires | 1 block, 0 wires |
| Enemy patrols, touch ends the game | 18 + 2 blocks, 20 + 1 wires | 2 blocks, 0 wires |
| Score on screen, win at 5 | 6 + 7 blocks, 5 + 6 wires | 2 blocks, 0 wires |
| Total | about 51 blocks, 49 wires | **6 blocks, 0 wires** |

(Before = the old Move with WASD, Pickup, Patrol, Score readout and Win at score templates plus an On Trigger -> Lose pair;
the game-over-on-touch part of the before count is my estimate, the rest is measured by tests/test_vs_recipes.cpp.)
The 10 recipes are 1 or 2 blocks and at most 1 wire each (budget: 3 blocks, 2 wires).

Also done: auto-connect when a block is added from the popup, insert-on-wire when a node is dropped on a flow wire (both
tested on the real graph model), plain wire-refusal sentences with a banned-word test, recipe cards on the empty canvas.

### Not done

- The Rule super-block (WHEN / IF / DO list inside one node), block options and presets, the "Do this to all objects
  named X" target, smart defaults from the selected object, and highlighting the running block in Play.
- Merged blocks with dropdowns (Change Number, Compare, Play Sound Start/Stop ...) and the old-id alias for them. The old
  nodes were NOT merged; they are only hidden from the default list. The alias table is still empty.
- Fields are plain text boxes; there is no key picker, object picker or dropdown widget on a block yet.
- Quick blocks are not translated to OkayScript text (the export lists them as not translated).
- The editor window was never run on a real desktop.

## Found on the way (listed, not fixed)

These came up while sorting. Nothing below was changed (the titles come from generators and tables other work depends on);
each one is a small, separate job.

**Duplicates.** Two nodes with the same title, listed in the menu:

| Title | The two nodes | Note |
|---|---|---|
| Set Velocity | `SetVelocity` and `GlSetVelocity` | Both set a Rigidbody's exact speed. Before the change they sat in different topics (Physics, Movement); now they sit side by side in Forces & Velocity. `tests/test_vs_palette_categories.cpp` allows exactly this pair, with a comment; retire one and delete that line. |
| Send Message | `NetSend` (a network message to a peer) and `SendMessage` (a script message to one object) | Different jobs, same title. Rename `NetSend` to something like "Send Network Message". |
| Format Number | `ToFixed` and `GlFormatNumber` | The same job: both turn a number into text with a fixed number of decimals. One of them can be hidden as a duplicate. |

The dedupe rule in the menu (`VsPaletteHidesAsDuplicate`) only hides a text-script function or an engine-API node that matches a
hand-written one by name or title. Twins that differ by a word are all still listed (for example `Bob` and `Fn.bob`, `Chase Target`
and `Fn.chase`, `Host Game` and `Fn.net_host`). They now sit in the same topic, next to each other, so the pair is easy to see;
the "same job" groups in the table above count them.

**Titles that look wrong.** All come from the generated friendly titles of the text-script functions:

- **"Fourth"** replaces "2D"/"3D" in 9 titles: Force Mode Fourth Impulse, Physics Fourth Raycast, Physics Fourth Box Cast,
  Physics Fourth Capsule Cast, Physics Fourth Overlap Box, Physics Fourth Overlap Capsule, Physics Body Fourth Add Explosion Force,
  Create Object Fourth, Face Fourth.
- **"Largest" and "Smallest" replace Max and Min** in 17 titles, and read badly where "Maximum" is meant: Get Largest Health
  (`get_max_hp`), Set Dropdown Largest, Set Toast Largest, Net Browser Largest, Net Largest Speed, Set Largest Visible, Set Largest Length,
  Set Largest Stack.
- **x, y, z become Right, Vertical, Forward** in the script functions (Get Right, Get Vertical, Get Forward, Set Right, Position Right,
  Mouse Vertical, Object Forward ...), while the hand-written nodes say Get X, Get Y, Get Z. "Vertical" is wrong for a z axis in 3D.
- **Too short to mean anything**: Addr, Host, Name, Port, Room, Players (the `mm_*` matchmaking functions), Get (`gc_get`),
  Let (`let_go`), Add (`add_xp`), Ray (`ray3_nx`), Error (`pf_error`), Loc (`loc`), Rand, Randi, Shl, Shr.
- **A component's own methods have no component in the title**: Set Value, Count, Open, Close, Push, Is Open, Page, Next, Previous
  (the `UI*` engine-API nodes). The new subcategories (Sliders & Toggles, Toasts & Tooltips ...) give them the missing context in the menu.

**No description.** 1,091 of the 2,600 browse rows (42%, all text-script functions) have the same hover text: "Calls this text-script
builtin on the current object. Requires: wire only the arguments it takes (First, Second, Third, Fourth) ...". Nothing says what they
do. Sorting them into specific topics makes them findable, but each needs a one-line `VsNodeHelp` sentence (and a level).

**Wrong pin descriptions on 26 math and logic nodes (seen in the headless render).** The details pane for **Add (+)** says pin A is
"Opacity, from 0 (invisible) to 1 (solid)" and pin B is "Blue, from 0 to 1". The cause is in `scripts/gen_vs_node_docs.py`: it keeps one
table of pin meanings keyed by the pin's name alone ("A" is Opacity, "B" is Blue, "R" is Red, "G" is Green, meant for colour pins), so every
node with a pin named A or B gets the colour text. Hand-written nodes affected (counted in `editor/VisualScriptNodeDocs.inc`): Add, Sub, Mul,
Div, Mod, Pow, Min, Max, Lerp, InverseLerp, Approx, Average, Compare, Equals, NotEquals, And, Or, Xor, Select, Concat, Vec3Add, Vec3Sub, Vec3Cross,
Vec3Dot, Vec3Distance, Vec3Lerp. Not changed here (it is not a menu-topic problem). A fix would key the table by node type and pin name, then
regenerate `editor/VisualScriptNodeDocs.inc` and `docs/visual_scripting_nodes.md`.

**Things the new tree makes visible**

- `Fn.on_key` ("On Key") is a text-script call that registers a key handler; it sits in Input Events with the real events. It is
  the only non-event in an event topic besides the three collision readings (Contact Point, Contact Normal, Impact Speed).
- The three generic step nodes (Action, Check, Action Steps) sit in Functions & Calls; a beginner may not guess that.
- `Fn.gc_get` and `Fn.gc_set` (titles Get and Set) read and write the shared Actions variables. They sit in Variables / Shared Variables next to
  Get Global Var and Set Global Var; whether the two are the same store was not checked.

## What was not verified

- The real editor window: no editor was opened here. The sidebar (section headings, folding, colours) and the topic list were
  drawn headlessly with the real panel code, ImGui's software renderer and its default font, and looked at
  ([docs/images/vs_palette_topics.png](images/vs_palette_topics.png), [search](images/vs_palette_search_weather.png),
  [folded section](images/vs_palette_folded_section.png)); how it feels in the editor window (font, scrolling) is not known.
- Windows. The data headers and the tests compile on Linux with g++; CI compiles the editor.
- Whether the topic names read well to a beginner. They are plain words and each has a one-sentence description, but only the
  owner's eyes decide that.
