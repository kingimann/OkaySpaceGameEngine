# OkayVS — the visual scripting text format

A visual script is a graph of **nodes** wired by **execution** links (control
flow) and **data** links (values). You can build a graph in C++ with
`vs::NodeGraph::Add` / `ConnectExec` / `ConnectData`, or load it from this text
format with `VisualScriptComponent::LoadFromText` / `LoadFromFile`.

## Directives

```
node <id> <Type> [args...]   # declare a node with a file-local id
exec <fromId> <pin> <toId>   # control flow: pin of <from> -> <to>
data <consumerId> <inPin> <producerId> <outPin>   # value wire
entry <name> <id>            # mark an entry node ("OnStart" / "OnUpdate")
# comments start with '#'
```

## Node types

**Events** (1 exec out): `OnStart`, `OnUpdate`

**Data — values & queries**: `Const <value>`, `GetVar <name>`, `Time`,
`DeltaTime`, `AxisX`, `AxisY`, `Key <c>` / `KeyDown <c>` (held / pressed-this-frame,
single-char keys like OkayScript), `MouseX`, `MouseY`, `GetPosition` (Vec3),
`GetX` / `GetY` / `GetZ` (this object's local position component)

**Data — math**: `Add`, `Sub`, `Mul`, `Div`, `Mod`, `Min`, `Max`, `Pow` (two
inputs); `Abs`, `Neg`, `Sqrt`, `Sin`, `Cos`, `Floor`, `Round`, `Sign` (one
input; trig in radians); `Clamp` (in0=value, in1=lo, in2=hi); `Lerp` (in0=a,
in1=b, in2=t); `Random` (0..1); `RandomRange` (in0=lo, in1=hi)

**Data — logic & misc**: `Compare <op>` (`>` `<` `>=` `<=` `==` `!=`), `And`,
`Or`, `Xor` (two inputs), `Not`, `Select` (in0=cond, in1=if-true, in2=if-false —
passes any value type through), `Concat` (in0+in1 as strings),
`MakeVec3` (in0=x, in1=y, in2=z), `VecX` / `VecY` / `VecZ` (in0=vector)

**Data — conditions** (return a bool, pair with `Branch`): `Equals` /
`NotEquals` (type-aware: text if either side is a string, else numeric),
`Approx` (in0≈in1 within in2, default 0.001), `Between` (in1 ≤ in0 ≤ in2),
`Chance <p>` (true with probability p), `HasVar <name>` (variable is set),
`KeyUp <c>` (key released this frame)

**Data — multiplayer**: `NetConnected`, `NetIsServer`, `NetIsClient`,
`NetPeers` (count), `NetId`, `NetGetVar <key>` (synced variable)

**Data — Steam**: `SteamName`, `SteamIsUnlocked <achievement>`,
`SteamGetStat <name>`

**Data — physics & tween**: `VelX` / `VelY` / `VelZ` (this object's Rigidbody2D or
Rigidbody3D velocity; a 2D body reads 0 on Z),
`Raycast` (in0=ox, in1=oy, in2=dx, in3=dy [, in4=maxDist] → did it hit?),
`EaseIn` / `EaseOut` / `EaseInOut` (shape a 0..1 `t` for smooth motion — feed into
`Lerp`)

**Actions** (1 exec out): `SetVar <name>`, `AddVar <name>` (add in0 to a variable),
`Toggle <name>` (flip a boolean variable), `Print`, `LogWarn`, `LogError`,
`Translate` (in0=x, in1=y), `SetPosition` (in0=x, in1=y),
`Rotate` (in0=degrees), `SetRotation` (in0=absolute degrees about Z),
`SetScale` (in0=x, in1=y, in2=z; default 1), `Spawn <prefab>` (instantiate at this
object's position), `Destroy` (remove this object — terminal, no exec out)

**Actions — multiplayer**: `NetHost <port>`, `NetJoin <host> <port>`,
`NetSend <channel>` (in0=data), `NetSetVar <key>` (in0=value), `NetDisconnect`,
`NetChat` (in0=text), `NetRpc <name>` (in0=data — run a named event on every other
peer), `NetSpawnOwned <prefab>` (spawn an object you own that auto-syncs to all
peers, at this object's position), `NetDespawn` (in0=sync id), `NetReady` (in0=bool),
`NetStartMatch`. Value nodes: `NetMatchStarted`, `NetAllReady`

**Actions — Steam**: `SteamUnlock <achievement>`, `SteamSetStat <name>` (in0=value),
`SteamStore`, `SteamLeaderboard <board>` (in0=score)

**Actions — audio & physics**: `PlaySound` (play this object's AudioSource),
`AddForce` (in0=x, in1=y), `AddImpulse` (in0=x, in1=y — jumps/knockback),
`SetVelocity` (in0=x, in1=y) — all on the sibling Rigidbody2D

**Flow**:
- `Branch` — exec out 0 = true, exec out 1 = false; data in0 = condition
- `Sequence <n>` — fire each of `n` exec outputs in order (default 2)
- `Repeat <n>` — run the downstream chain `n` times, then continue
- `Once` — pass through only the first time it's reached (one-shot setup)
- `Timer <seconds>` — pass through at most once every `seconds` (accumulates
  `DeltaTime`); ideal for "every N seconds" pulses under `OnUpdate`
- `Wait <seconds>` — one-shot delay: blocks until `seconds` elapse, then passes
  through from then on (combine with `Once` for a single deferred action)
- `FlipFlop` — alternate between exec out 0 and 1 on each execution

## Example: move right at 2 units/sec

```
node 0 OnUpdate
node 1 Const 2
node 2 DeltaTime
node 3 Mul
node 4 Const 0
node 5 Translate
data 3 0 1 0      # mul.in0 = 2
data 3 1 2 0      # mul.in1 = dt
data 5 0 3 0      # translate.x = 2 * dt
data 5 1 4 0      # translate.y = 0
exec 0 0 5        # OnUpdate -> Translate
entry OnUpdate 0
```

## ActionList — no-code triggers (Game Creator 2 style)

The `ActionList` component is the simpler, designer-facing visual scripting:
a **Trigger** (`OnStart`, `OnUpdate`, `OnKey`, `OnCollision`, `OnClick`,
`OnKeyUp`, `OnMessage`) → a list of **Conditions** (all must pass) → a list of
**Instructions** (run top to bottom). Build it entirely in the Inspector.

- **Conditions**: `always`, `key`, `key_down`, `key_up`, `mouse`, `mouse_down`,
  `chance`, `cooldown` (passes at most once every N seconds — fire rates,
  dashes), `var_eq`, `var_neq`, `var_gt`, `var_lt`, `var_between`, `vars_cmp`,
  `prefs_eq`, `prefs_gt`, `has_tag`, `is_active`, `dist_lt`, `dist_gt`,
  `exists`, `raycast`, `raycast_tag`, `raycast_name`.

### Raycasting (no code)

Cast a ray from the object and react to what it hits — line of sight, ground
checks, shooting, interaction, etc. The Inspector gives you a **Direction**
dropdown (Forward / Back / Up / Down / Left / Right — relative to the object's
facing — or **Toward Object** to aim at a named object) and a **Distance**, so
there are no raw vectors to type. The Scene view draws the ray live: **green**
to the hit point (with a marker), **yellow** when it hits nothing — so you can
see exactly where it points before you press Play.

- Condition `raycast` — passes if the ray hits any collider. Args: `<direction> [distance]`.
- Condition `raycast_tag` — passes only if the hit object has a tag. Args: `<tag> <direction> [distance]`.
- Condition `raycast_name` — passes only if it hits a named object. Args: `<object> <direction> [distance]`.
- Instruction `raycast` — casts and **stores the result in variables**:
  `<prefix>_hit` (1/0), `<prefix>_dist`, and `<prefix>_x/_y/_z` (the hit point),
  so later instructions can branch with `if_goto` or use the position. Args:
  `<direction> [distance] [prefix]` (prefix defaults to `ray`).

`direction` is one of `forward`, `back`, `up`, `down`, `left`, `right`, or
`toward:<ObjectName>`. The caster's own colliders are ignored.
- **Instructions**: movement (`move`, `set_pos`, `rotate`, `set_scale(3)`,
  `move_toward`, `look_at`, `move_dir` — facing-relative movement, `follow`,
  `flee`, `orbit` — circle a named object), control (`wait`, `wait_until` —
  hold until a variable passes a test, `goto`, `stop`, `if` / `else` /
  `end_if` — structured branching without line numbers), juice (`tween_move`,
  `tween_scale`, `tween_rotate`, `shake`, `punch_scale`, `tween_color`,
  `fade`, `flash` — eased, scheduler-driven), variables
  (`set_var`, `add_var`, `mul_var`, `div_var`, `copy_var`, `rand_var`,
  `tween_var` — animate a variable over time),
  objects (`spawn`, `spawn3`, `destroy`, `destroy_obj`, `activate`,
  `deactivate`, `set_active`, `set_tag`), rendering/audio (`set_text`,
  `set_color`, `emit`, `play_anim`, `play_sound`, `set_cam`, `set_bg`,
  `set_light`, `set_ambient`), physics (`velocity`, `impulse`), data
  (`set_prefs`, `add_prefs`, `save_prefs`), **scenes** (`load_scene`,
  `load_scene_index`, `load_next_scene`), **raycasting** (`raycast` — store the
  hit in variables, see below), **multiplayer** (`net_host`,
  `net_join`, `net_send`, `net_set` synced vars, `net_spawn` replicated spawn,
  `net_disconnect`), **Steam** (`steam_unlock`, `steam_set_stat`,
  `steam_inc_stat`, `steam_inc_stat_int`, `steam_store`, `steam_cloud_write`,
  `steam_presence`, `steam_lobby_create`, `steam_lobby_join`,
  `steam_lobby_leave`, `steam_lobby_set_data`, `steam_invite` — matchmaking,
  Cloud saves and rich presence, no code), messaging (`send`), **juice** (`tween_move`,
  `tween_scale`, `shake`, `punch_scale` — eased motion, impact shake and
  scale punches, mirroring the script `tween_*` builtins), `comment`
  (a designer note, skipped at runtime), and `log`.

Multiplayer with zero code: a `Player` with an `OnKey` ActionList whose
instruction is `net_host 45000`, and another whose instruction is
`net_join 127.0.0.1 45000`, is a complete host/join setup.

### PlayFab (cloud login, leaderboards, saves) — no code

The `playfab_*` instructions talk to Azure PlayFab over its REST API (no SDK
to install; get a free Title ID from the PlayFab Game Manager):

- `playfab_login <titleId> <customId>` — sign in (creates the account on
  first use). `customId` is any stable id you pick, or `$textvar`. Sets
  `playfab_ok` (1/0), `playfab_id`, and `playfab_error` on failure. Run it
  once from **On Start**, not every frame — the call blocks while the
  request runs.
- `playfab_name <display name>` — the name shown on leaderboards.
- `playfab_set_stat <stat> <value>` — publish a statistic (leaderboards are
  built from statistics in PlayFab). `value` can be `$var` to send a game
  variable, e.g. `playfab_set_stat highscore $score`.
- `playfab_leaderboard <stat> [count]` — fetch the top entries into
  variables: `pf_count`, then `pf_1_name` / `pf_1_score`, `pf_2_name` / ...
  Show them on screen with **UI Text Bind** (`{pf_1_name} {pf_1_score}`).
- `playfab_set_data <key> <value...>` / `playfab_get_data <key> [intoTextVar]`
  — per-player cloud save data.

The editor's **Services** panel has a PlayFab section to test your Title ID,
publish a stat and fetch a leaderboard before wiring up the actions.

OkayScript has the same powers as builtins: `pf_login`, `pf_login_password`,
`pf_register`, `pf_logged_in`, `pf_id`, `pf_error`, `pf_name`, `pf_set_stat`,
`pf_leaderboard_top` (array of `"rank,name,score"`), `pf_save`, `pf_load`,
`pf_delete` — see the in-editor Scripting Reference.

## Flow Graph editor (View ▸ Flow Graph)

The node view of an object's Actions. Everything the Inspector edits is here
too, plus graph-only conveniences:

- **View**: drag empty space to pan, scroll to zoom (anchored on the cursor),
  **Reset** for 100%, **Fit** to frame every node, and a **Minimap** toggle in
  the right-click menu. Node positions you drag are saved with the scene;
  "Tidy Up Layout" returns to the automatic arrangement.
- **Editing**: click a node to edit it in the strip above the canvas;
  right-click for Edit / Duplicate / Copy / Paste After / Move / Delete.
  **Ctrl+C/V** copy-paste across scripts, **Ctrl+D** duplicates in place,
  **Delete/Backspace** removes the selection.
- **Comments**: right-click empty canvas ▸ **Add Comment** (or add the
  `Comment (note)` block) — a note that is skipped at runtime, for explaining
  what the next blocks do.
- **Debugging**: the running instruction glows green in Play; **Pause** halts
  every Actions script and **Step / Step 10 / Continue** single-step them.
  Right-click a node ▸ **Add Breakpoint** (red badge) to pause automatically
  the moment that node is about to run — then Step from there. Breakpoints
  are debug-only and never saved with the scene.
- **Code round-trip**: **View as Code** renders the blocks as OkayScript to
  copy into a real script; **Paste Code** converts simple OkayScript back
  into blocks.
- **Adding nodes**: + Instruction / + Condition open a searchable palette —
  type and press **Enter** to add the first match without the mouse.
- **Templates & reuse**: **+ Template** inserts ready-made behaviours;
  the ★ Custom buttons save/insert reusable instruction or condition groups
  shared across projects.

## More nodes

**Events:** On Collision, On Trigger (2D and 3D, on enter) and On Click start
chains like On Start / On Update. Inside them, **Other Name** / **Other Tag**
give the object you hit or clicked.

**Math:** Tan, Atan2, Exp, Log, Inverse Lerp, Remap, Smooth Step, Ping-Pong,
Repeat (wrap), Move Towards (value), Delta Angle.

**Vectors:** Vec3 Add / Subtract / Scale / Length / Normalize / Dot / Distance.

**Text:** To Text, Format Number (decimals), Text Length, Contains, Upper / Lower Case.

**Objects** (by name; `self` and `other` work too): Object Exists, Object
Position, Distance To, Self Name, Look At, Move Towards (speed), Set Active, Set
Text (a Text object), Load Scene.

**Input / flow:** Mouse Held, Mouse Pressed (0 left, 1 right, 2 middle), Cooldown (s).

**Animation & IK:** Anim Trigger, Anim Set Float, Anim Set Bool, Anim State
(drive the Animator state machine), IK Look At and IK Hand Reach (Full Body IK).

## Third wave of nodes

**Flow:** For Loop (First/Last; the **Index** output reads the current index, then
**Done** runs once), Repeat N, Flip-Flop (A/B alternate), Gate (passes while Open
is true), Do N Times (with a Count output), Switch (int) with 0 / 1 / 2 / Default
outputs. **Toggle Var** flips a bool variable.

**Math & vectors:** Random Int (both ends included), Ceil, Fraction, One Minus,
Clamp 0-1, Asin, Acos, Deg to Rad, Rad to Deg, Vec3 Cross, Vec3 Lerp, Vec3 Negate.

**Transform (3D):** Forward / Right / Up Vector, Get Scale, Get Rotation (Euler
degrees), Set Position (Vec3), Move By (Vec3), Rotate (3D), Set Rotation (3D).

**Actions:** Set Color (R, G, B, A on the sprite, text or mesh), Play Clip (by
name on the Character or imported model), Set Time Scale, Set Blend Shape (a
morph target's weight on every mesh under the object) and the **Blend Shape**
data node to read one back.

**Input & engine:** Raycast 2D (origin, direction, max distance; outputs Hit),
Mouse Wheel, Time Scale, Frame Count.

**Multiplayer & Steam** (its own palette category): Host Game, Join Game
(`host port` or `host:port`), Disconnect, Is Connected / Is Server / Is Client,
Peer Count, My Net Id, Send Message, Set / Get Net Var, Send Chat, Call RPC, Net
Spawn / Despawn, Set Ready, Start Match, Match Started, All Ready, Steam Name,
Achievement Unlocked, Unlock Achievement, Get / Set Steam Stat, Store Stats,
Submit Score.

**Finding nodes:** right-click the canvas (or press **Space**) and start typing.
- **Search** is forgiving: `ply snd` finds Play Sound. Close matches come first,
  and the matched letters are highlighted.
- **Keys:** **Up / Down** pick a result and **Enter** adds it.
- **Sidebar:** browse by category, by **Do something** / **Check something**
  (the Actions library), by **Recent**, or by **Favourites**. Click a node's star
  to make it a favourite.
- **Details:** the strip at the bottom says what the highlighted node takes and
  gives.
- **Dragging from a pin:** only the nodes that fit that pin are listed.

**Typed values:** every unconnected input has an inline box, so a Print node
can print text without wiring a Const. Graphs attach from the Inspector (Add
Component ▸ Scripts lists `.okayvs` files and **New Visual Script**), by dragging a
`.okayvs` onto an object, or with **Attach to Selected** in the node editor.

**As C#:** *View as OkayScript* shows the graph as a C#-style component class
(`void Start()`, `void Update()`, `void OnCollisionEnter(Collision other)` ...)
that you can copy or save as a `.okay` script.

## Fourth wave: events between objects, keys, timers and more

**Several event nodes of one kind.** A graph may now contain any number of
*On Start*, *On Update* (or any other event) nodes; all of them run, in the
order they were added. Previously only the last one did.

**New events**

| Node | Fires |
|---|---|
| On Key Pressed / On Key Released | the frame a key goes down / up (pick the key with the key picker) |
| On Timer | every N seconds |
| On Custom Event | when a *Send Event* / *Broadcast Event* (or a script's `send_event`) names it; its **Value** output is the value sent |
| On Collision Exit / On Trigger Exit | when contact / overlap ends |
| On Mouse Enter / On Mouse Exit | the pointer moves onto / off the object |

**Talking between objects**

- *Send Event* (event name; inputs **Target**, **Value**): runs the target object's
  *On Custom Event* nodes with that name. Target blank = this object.
- *Broadcast Event* (event name; input **Value**): every object listening for it.
- *Event Value* / *Event Name*: what the current custom event carried.
- *Set Object Var* / *Get Object Var*: read or write another object's graph
  variable (or script global).
- Scripts join in: `send_event("Door", "Open", 1)`, `broadcast_event("Alarm")`,
  and a script receives an event in `void OnEvent_Open(object value)`.

**Saving**: *Save Value* / *Load Value* (key) keep numbers, text and vectors
between runs (the same store as the script `save` / `load`).

**Text**: Format Text (`"Score: {0} / {1}"`), Substring, Replace Text, To Number,
Starts With, Ends With, Trim Spaces, Index Of.

**Math**: Perlin Noise (smooth, -1..1), Snap To Step, Wrap, Average, Distance 2D.

**Objects + game flow**: Find / Count / Nearest With Tag, Teleport (any object,
unset axes keep their value), Set Visible, Pause Game, Quit Game, Show Cursor.

**Flow**: Random Branch (True with a chance), Counter (counts runs, with Reset),
Every N Times.

## Flow Graph editor: working faster

- **Drag a component header, or Position / Rotation / Scale, from the Inspector
  onto the graph.** Transform, Camera and Light drop a Get Position node.
  Sprite, Mesh and Text drop Set Color. A rigidbody drops Set Velocity. Audio
  drops Play Sound. Position, Rotation and Scale drop the matching getter.
  Anything else drops a Const labelled with that name.
- **Drag a wire into empty space** and the node palette opens, filtered to nodes
  whose pin type can take that wire (a Number does not offer a Vector3-only
  input). The bar names the type. The search box also matches pin types, so
  typing `object` or `number` finds those nodes. The node you pick is wired to
  that pin.
- **Box select** by dragging on empty canvas; Ctrl/Shift+click adds to the
  selection; dragging any selected node moves the whole selection.
- **Ctrl+C / Ctrl+V / Ctrl+D** copy, paste (at the mouse) and duplicate;
  **Del** deletes the selection; **Ctrl+A** selects everything.
- **Ctrl+Z / Ctrl+Y** undo and redo every edit (moves, wiring, values).
- **Alt+click a pin** to break its wires; right-click a node ▸ Break All Wires.
- **Comments** (toolbar *Comment*, or **C**): a titled, coloured frame around the
  selected nodes. Drag its title to move the group, drag the corner to resize,
  double-click to rename, right-click for colour / delete.
- **Minimap** in the corner (View ▸ Minimap): click or drag to jump around.
- **Live debugging**: while playing, the graph of the selected object (or the
  first one using this file) lights up each node as it runs, and every data
  output shows its latest value beside the pin. A green *LIVE* tag in the
  toolbar names the object being watched.
- **Space** opens the palette at the mouse, **Home** frames the whole graph.

## One graph editor (Actions merged in)

The Flow Graph window and the Actions list are gone as separate editors: everything
is built in the **Visual Script** graph.

- **+ Add > Do something / Check something** lists every step the Actions component
  had (Move, Play Sound, Set Variable, Has Tag, ...). They become `Action` and
  `Condition` (Check, True/False) nodes; click a node's grey line to edit its
  arguments with the same friendly fields.
- **Action Steps** (`Actions`) keeps a whole list together: Wait, If / Repeat /
  While blocks, jumps. The chain continues once the list finishes.
- **Delay (s)** really pauses the chain and continues it later (once). The older
  **Wait** node only lets a chain that runs every frame through after a while.
- New events: On Mouse Down / Up / Over, On Collision Stay, On Late Update.
- **Templates** adds ready-made behaviours as nodes.
- An object that still has an Actions component keeps working. Its Inspector shows
  what it does in sentences; **Open as Visual Script graph** turns it into an
  equivalent `.okayvs` graph (one chain per trigger) and attaches it.
- Actions messages (`send`, `send_to`, ...) reach graphs' On Custom Event nodes, and
  Send / Broadcast Event reach older Actions "On Message" triggers.

File format: `node <id> Action "<op> <args>"`, `node <id> Actions "<step> ; <step>"`,
`node <id> Condition "<op> <args>"`, `node <id> Delay <seconds>`. Arguments are
%-escaped (no spaces, quotes, `#` or `;` inside one), `%-` is an empty argument.
OkayScript export: `run_action("...")` / `check_action("...")`.

Toolbar: File, Edit, + Add, Templates ... Fit, zoom, View. The Events list on the
left jumps to each chain; right- or middle-drag pans; F frames the selection.

## Physics: 3D raycasts, overlaps, trigger stay and contact details

| Node | What it does |
|---|---|
| On Trigger Stay | runs every frame something stays inside this object's trigger (healing zones, lava) |
| Raycast 3D | a line from Origin along Direction (this object, forward, when unwired). Outputs Hit, Point, Normal, Distance, Object. Ignore Triggers passes through trigger colliders |
| Sphere Cast | the same with a ball of Radius, so it can't slip through small gaps (skips triggers) |
| Raycast All 3D | everything along the line, nearest first: Count, Objects, Nearest |
| Overlap Sphere / Overlap Box | the objects inside a ball / box (Half Size is half the box): Count, Objects |
| Contact Point / Contact Normal / Impact Speed | where the collision or trigger touch that started this chain happened, which way the surface faces, and how hard it hit |

The casts and overlaps never hit this object's own colliders. Their setting is an
optional **layer list** (`0,3` or `Default,Enemy`; `!3` = everything but layer 3;
blank = all layers). In the text format: `node 5 Raycast3D Default,Enemy`; the
contact nodes read `contact_point` / `contact_normal` / `impact_speed` from the
blackboard, which the On Collision / On Trigger events fill in. As OkayScript the
casts become `ray3_cast("point", origin, direction, dist, "layers")` and the
overlaps `overlap3("sphere", center, radius)`.

**Actions raycasts** (Raycast Hits / Raycast (store hit)) gained three options next
to *start Y / ignore / only*: **radius** (above 0 it's a sphere cast), **layers**
(the same layer list) and **skip triggers**. Stored as the trailing tokens `r:`,
`lm:` and `nt:1`. The Actions trigger list also has **On Trigger Stay**.

**Physics debug view.** *View ▸ Scene Overlays ▸ Physics Debug* (or the **Physics**
checkbox on the Game view toolbar) draws colliders (green solid, blue trigger,
grey asleep), this frame's contact points with their normals (red) and, with
*Rays cast from scripts* on, every ray / cast made by OkayScript, visual scripts
and Actions (green to the hit with a white normal, yellow on a miss) — in the
Scene and Game views while playing. When it's off the physics records nothing.

## Objects: Target inputs, Self / Other, Spawn's object

Graphs can now pass **objects** along wires, not just names.

- **Self**, **Other Object** (the other object of a collision, trigger or custom
  event), **Find Object** (by name), **Find With Tag** and **Nearest With Tag**
  output an object. **Spawn** outputs the object it just spawned. **Raycast 3D** /
  **Sphere Cast** output the object they hit, and **Raycast All 3D** / **Overlap**
  nodes output lists of objects. As text, an object reads as its name.
- Nodes that act on an object (Translate, Set Position, Rotate, Set Scale,
  Get Position / X / Y / Z, Add Force, Set Velocity, Set Color, Set Visible, Play
  Sound, Anim Trigger, Get Name, Get Tag, ...) have a last **Target** input.
  Leave it empty for this object, wire an object into it, or type a name.
  Only the node itself acts on the target. Anything wired into its other inputs
  is still read for this object, so `Get X` (no Target) feeding a targeted
  `Set Position` reads *your* X.
- **Destroy** has a **Target** too. Destroying another object carries on to
  the next node; destroying this object ends the chain.
- Object Exists / Object Position / Distance To / Look At / Move Towards /
  Set Active / Set Text / Teleport take an **Object** input that wins over the
  name typed in the node.
- An object value is a reference by id. It is safe after the object is
  destroyed: Branch reads it as false, Print shows nothing, and a node aimed at
  it is skipped with an error in the Console naming the node.
- Equals compares two objects by identity, or an object with a name by name.

Collision → destroy the enemy (graph on the player):

```
node 0 OnCollision
node 1 Other
node 2 GetTag
node 3 Const "Enemy"
node 4 Equals
node 5 Branch
node 6 Destroy
data 2 0 1 0        # Get Tag of Other
data 4 0 2 0
data 4 1 3 0
data 5 0 4 0
data 6 0 1 0        # Destroy's Target = Other
exec 0 0 5
exec 5 0 6
entry OnCollision 0
```

Key → spawn → place the spawned object:

```
node 0 OnKey space
node 1 Spawn bullet.okayprefab
node 2 SetPosition
default 2 0 7
default 2 1 8
default 2 2 9
data 2 3 1 0        # Set Position's Target = Spawn's object
exec 0 0 1
exec 1 0 2
entry OnKey:space 0
```

## More events

| Node | Runs |
|---|---|
| On Fixed Update | 50 times a second of game time (at most 5 per frame), before On Update. Delta Time reads 0.02. |
| On Enable | When the object starts (before On Start) and each time it is switched back on. |
| On Disable | When the object is switched off during play (detected the next frame). |
| On Destroy | When the object is destroyed, while it still exists. It may spawn effects. |

Text scripts get the same `OnEnable()` / `OnDisable()` calls when their
object is switched off and on.

`OnKey:space`, `OnTimer:2` and `OnEvent:Hit` are accepted as node types too
(the same spelling as the `entry` line).

## Build mode nodes

For games where players build (see [build_mode.md](build_mode.md)).

| Node | What it does |
|---|---|
| On Object Placed | A player placed an object (also on undo / redo). **Other Name** is the object, **Event Value** its build id. |
| On Object Removed | A player deleted an object. |
| On Logic Signal | A wired door, light, spawner or event target turned on or off. **Event Name** is its signal name, **Event Value** is true when on. |
| Enter Build Mode / Exit Build Mode | Turn the player's build mode on / off. |
| In Build Mode? | True while it's on. |
| Set Build Palette | Replace what players can place: `Name=prefab, Name=prefab\|icon.png, ...` (the icon is optional) |
| Save Creation / Load Creation | Save / load everything players placed under a name. **Saved** / **Loaded** says whether it worked. |

## When a graph goes wrong

Graphs never freeze the game and never fail silently:

- **Runaway loops.** One event may run at most 1,000,000 node steps,
  including loop bodies and nested loops. Past that, the rest of the event is
  skipped and the Console gets one error naming the object and node. The next
  event runs normally.
- **Data cycles.** A node whose input depends on its own output is reported
  and the event stops.
- **Event ping-pong.** Custom events triggering each other more than 64 deep
  are dropped with an error.
- **Missing objects.** A Target or Destroy aimed at a destroyed or unknown
  object, or a Spawn whose prefab file is missing, is reported and skipped.

While playing, the Visual Script editor outlines the node that failed in red
and shows the message on the canvas. Each distinct error is logged once; the
canvas shows how many times it happened.

`Wait` now pauses the chain and resumes it by itself (like Delay), so it works
under On Start too. Under On Update it holds that chain until the time has
passed, then lets it through every frame.

Every Visual Script on an object is saved with the scene. Before this, only
the first graph on an object survived a save.

## Pin types and the graph checker

Every data pin has a type, shown by its colour (hover a pin to see it):

| Colour | Type |
|---|---|
| green | Number |
| red | Bool |
| pink | Text |
| yellow | Vector3 |
| blue | Object |
| cyan | List |
| grey | Any (variables, events, Select, Equals, Format...) |

A wire is allowed when its value means something in the input:
- the same type, or Any on either end;
- Number ↔ Bool;
- anything → Text;
- Text → Object (a name);
- Object → Bool (does it still exist?).

Anything else is **refused when you drop the wire**, with a tip, for example
"A Vector3 can't go into a Number input - use Vec X / Vec Y / Vec Z or Vec3
Length". Wires like that in an older or hand-written graph are drawn red.

**The checker** runs whenever the graph changes. The toolbar shows "✓ No
problems" or "⚠ N problems"; click it for the list, and click a row to jump to
the node. Problem nodes are outlined, red for errors and amber for warnings,
with a "!" you can hover for the details. It finds:

- **errors** (that part won't work):
  - wires whose types don't fit;
  - key names OkaySpace doesn't know (`F1` would silently act as `f`);
  - Spawn prefab files that don't exist;
  - a graph that can't load.
- **warnings** (probably not what you meant):
  - nodes no event's chain ever reaches;
  - events with nothing wired to them;
  - variables the graph reads but never sets.

**Play** writes every problem in every graph of the scene to the Console.
**Build** checks every graph in the scenes going into the build. If there are
errors, the build stops and Build Settings lists them, with **Build anyway** and
**Dismiss**. Warnings never stop a build.

Not covered yet: integer, Vector2, colour, quaternion and enum values. Pins use
the types above; colours stay R / G / B / A numbers and rotations stay Euler
Vector3s.

## Functions

A function is a chain you write once and run from anywhere in the same graph.

1. Add a **Function** node and write its signature in its text field:
   `Damage(amount: Number, who: Object) -> left: Number, dead: Bool`.
   The parameters become the node's output pins. Return values after `->` are
   optional, and so are types; a parameter without a type is Any. The types are
   Number, Bool, Text, Vector3, Object, List and Any.
2. Wire the body from its exec pin, and end it with **Return**: pick the
   function, then wire the values to return.
3. Run it with **Call Function**: pick the function, and its inputs (the
   parameters) and outputs (the return values) appear. The outputs hold the
   values of the call that just ran, so read them after it.

Rules, all checked by the graph checker and enforced while playing:

- **Return ends the function at once**, even from inside a loop.
- **Recursion works.** A function that calls itself more than 64 deep is
  refused with an error, and the event carries on.
- **Set Local / Get Local** are variables of one call; they're gone when it
  returns. Outside a function they're errors (use Set Var / Get Var there).
- **No pausing:** a function runs to the end at once, so Delay and Wait can't be
  inside one. Put the delay after the Call Function instead.
- **Renaming** a function in its signature renames the Calls and Returns
  that use it.
- **Changing a signature** keeps existing wires. A wire to a parameter that no
  longer exists is reported until you fix it.
- Functions belong to their graph. Another object's graph can't call them;
  use Send Event for that.

The left-hand list shows the graph's functions, with how many places call each one.

Example: test E from the audit (Start → Call Function → Return → Print):

```
node 10 FunctionStart "Double(x: Number) -> result: Number"
node 11 Mul
node 12 Const 2
node 13 Return Double
data 11 0 10 0
data 11 1 12 0
data 13 0 11 0
exec 10 0 13
node 0 OnStart
node 1 CallFunction Double
node 2 Const 21
node 3 Print
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
```

## Engine API nodes

The **Engine API** group in the node search holds one node for every property and
method of the typed script language: GameObject, Transform, Rigidbody, Vector3, Input,
Time, Mathf and Debug. That language is `okayscript-next` in the Script Editor's
language list, with scripts written as `class Name : Behaviour { ... }`. These nodes
are generated from the same binding table that language uses, and they call the same
code. Anything added to that table appears as a
script member and as a node at once. The full list, with each node's pins, is in
[engine_api.md](engine_api.md), which is generated from the table too.

Each node is named the way the code reads, and its tooltip shows the line of script
it equals:

| Node | In a script | Kind |
|---|---|---|
| `Transform.position` | `transform.position` | value |
| `Set Transform.position` | `transform.position = position` | step |
| `Transform.Translate(x, y, z)` | `transform.Translate(x, y, z)` | step |
| `Rigidbody.AddImpulse(impulse)` | `GetComponent<Rigidbody>().AddImpulse(impulse)` | step |
| `Mathf.Clamp(value, min, max)` | `Mathf.Clamp(value, min, max)` | value |
| `Input.GetKey(key)` | `Input.GetKey(key)` | value |

- **Steps vs values.** Setting a property, and methods that change something
  (Translate, AddForce, Debug.Log...), are steps you wire into the flow. Reading a
  property, maths, and questions like `Input.GetKey` are value nodes.
- **Target.** Members of GameObject, Transform and Rigidbody have a last **Target**
  input. Leave it empty to act on this object, or wire in an object (Self, Other, Find
  Object, a spawned object...) or type an object's name.
- **Vector3 members** (`magnitude`, `normalized`) take the vector as their input.
- **Checked like scripts.** Key and axis names typed into `Input.GetKey` /
  `Input.GetAxis` (or fed straight from a Const) are checked before Play. Wires of
  the wrong kind are refused.
- **Errors while playing** go to the Console and the node is outlined in red. Examples:
  an object with no Rigidbody, a Target that doesn't exist, an unknown axis name. The
  event carries on with the next step.
- **In files** the node types read like the code: `Transform.position` (get),
  `Transform.position=` (set), `Transform.Translate/3` (a method and how many
  parameters it takes).

The older hand-made nodes (Translate, Get Position, Add Force...) still work and stay
in their groups. The **Engine API** nodes are the ones that match the scripts exactly.
The generated-code preview doesn't translate Engine API nodes yet. It lists them as
not translated, and each node's tooltip gives the line of script.

## Loops, lists and variable scopes

### Loops

| Node | What it does |
|---|---|
| **For Loop** | Runs Body for every number from First to Last (Index), then Done. |
| **Repeat N** | Runs Body N times. |
| **While** | Runs Body again and again while Cond is true. Cond is checked before each run. Then Done. |
| **For Each** | Runs Body once per item of a List, with Item and Index (counted from 0), then Done. It walks the list as it was when the loop started, so the body may add to or remove from it safely. |
| **Break** | Leaves the loop whose Body runs it. The loop goes on to Done. |
| **Continue** | Skips the rest of this run of the Body. The loop goes on to the next item. |

- Break and Continue act on the innermost loop. In nested loops they only affect the inner one.
- Before Play, the checker says when a Break / Continue isn't inside a loop's Body. While playing it's reported in the Console. That includes one that's after the loop's Done, or inside a function that has no loop of its own: Break doesn't reach out of a function into the loop that called it.
- A loop that never ends (a While whose Cond stays true) is stopped by the event's step budget and reported. The game keeps running.

### Lists

**New List** makes an empty list. Keep it in a variable (Set Var `bag` = New List), then **List Add** items to it.
Lists are shared: every node that reads `bag` sees the same list. The other list nodes:

- **List Get** (by index, from 0)
- **List Length**
- **List Contains**
- **List Remove At**

**All With Tag** gives every active object with a tag as a list, and so do the 3D Overlap queries. Walk one with For Each.
Reading or removing past the end of a list is reported, not ignored.

### Scene and global variables

| Scope | Nodes | Who sees it | How long it lasts |
|---|---|---|---|
| Object | Get / Set / Add Var | this object's graph (other objects: Get / Set Object Var) | as long as the object |
| Scene | Get / Set / Add Scene Var | every graph in the scene | until another scene is loaded |
| Global | Get / Set / Add Global Var | every graph | the whole game run, across scene loads |

Each press of Play in the editor starts with no scene or global variables.

### On Variable Changed

**On Var Changed**, **On Scene Var Changed** and **On Global Var Changed** run when that variable changes.
They give the new **Value** and the **Old** one.

- **When it's checked:** after every event and every frame, so changes made by other graphs and by scripts count too. The event runs in the same frame or the next one.
- **What counts as a change:** setting the same value again isn't a change. Neither is a value that was already there when the object started.
- **Lists:** adding to a list doesn't change the variable that holds it. Setting the variable to another list does.
- **Runaway handlers:** a handler that changes its own variable every time it runs is stopped after 16 rounds and reported.

## Nodes the sections above skip

These are in the node search. The [palette index](#palette-index) at the end lists every hand-written node, in the same groups as the sidebar.

**Animation clip events.** **On Anim Event** runs when a clip fires an event. Its setting is the event name; leave it blank to run for every clip event. **Anim Event Name** reads the name of the event that started the chain.

**UI show and hide.** **Show UI** turns a UI object on (blank = this object) and plays its UI Animation intro. **Hide UI** plays the outro, then hides it; with no UI Animation it hides immediately.

**Object RPCs and sync vars** (Multiplayer & Steam):

| Node | What it does |
|---|---|
| Call Object RPC | Calls a named RPC on this object's NetworkObject. **Data** is the payload. |
| On Object RPC | Runs when that named object RPC arrives. **Value** is the payload. |
| Set Sync Var | Writes this object's SyncVar of that name. With no SyncVar component, a locally owned object still publishes the value as a net var named `<net id>.<name>`. |
| Get Sync Var | Reads that SyncVar, or the same net var when the component is absent. |

**Inventory.** These run on the object whose inventory changed. **Event Name** is the item, slot, or recipe. **Event Value** is the count or the item, when the event has one.

| Node | When |
|---|---|
| On Item Added / On Item Removed | A stack was added or removed. Event Value is the count. |
| On Inventory Full | An add did not fit. Event Value is how many were left over. |
| On Item Used | An item was used. |
| On Equip | An item was equipped. Event Name is the slot, Event Value the item. Taking it off does not fire this node. |
| On Craft | A recipe finished. Event Name is the output item. |

**Cutscenes** (a Sequence Player plays a `.okayseq`, see docs/cutscenes.md): **Play Sequence** and **Stop Sequence** steps, and
**On Sequence Event** (blank catches every event; Event Name reads it).

**Anti-cheat** (the server; the scene needs an Anti-Cheat Config):

| Node | What it does |
|---|---|
| On Cheat Detected | A check failed. Event Name is the kind, Event Value the peer. |
| On Validate Rpc | An RPC is about to run. Event Name is the RPC, Event Value its data. **Reject Rpc** in that chain refuses it. |
| On Validate Hit | A hit claim is being checked. Event Name is the weapon, Event Value the shooter. **Reject Hit** refuses it. |
| Server Budget | That RPC's remaining budget for a player. **Player** is the peer id, **Add** tops it up first (0 only reads). **Left** is what remains, or -1 when that RPC has no budget. |

## Debugging: breakpoints, stepping and the watch panel

A breakpoint freezes the game just before a node runs, so you can see the variables at that moment and step through the graph.
Breakpoints only work in the editor; an exported game ignores them.

### Breakpoints

- **Set one:** select a step node (one with white flow arrows) and press **F9**, or right-click it ▸ **Add Breakpoint**. A red dot appears left of its title.
- **Save first:** breakpoints are kept per graph file. A graph that has never been saved can't have one.
- **Find them:** the left sidebar's **BREAKPOINTS** list shows them all. Click one to jump to it; right-click it to remove it; **Remove all** clears the list.
- **Where they stop:** in every object that uses that graph file, and on every pass through a loop.
- **Lifetime:** breakpoints stay set when you stop Play, but not when you close the editor.

### When the game pauses

When a breakpoint is reached, the game freezes in the middle of its frame and the Visual Script window opens on that graph. Inside it:

- **The node about to run** gets a yellow ring and arrow.
- **An amber bar** says where it stopped: the node, the object and the event.
- **If another graph file is open,** the bar's **Open its graph** button switches to the one that paused (save or undo your changes first).
- **The watch panel** on the right shows:
  - **Call stack:** the event, plus any functions it is inside, innermost first.
  - **Function ‹name›:** the current function's inputs and local variables.
  - **Variables on ‹object›:** that object's graph variables.
  - **Scene variables** and **Global variables**.
- **Wires** show the last value each node gave.

The Console also logs where it paused.

### Stepping

| Button | Key | What it does |
|---|---|---|
| Continue | F5 | Run until the next breakpoint |
| Step | F10 | Run this node and stop at the next one. A **Call Function** runs the whole function. |
| Step Into | F11 | Stop at the very next node, even inside a function it calls |
| Step Out | Shift+F11 | Run until the current function returns, then stop after the call |
| Stop Play | | Stop the game |

The keys work while the Visual Script window has focus.

- **After a step:** if the event ends before there is a next node, the game stops at the next node that runs in any graph.
- **Stop Play:** the frozen frame finishes first, without pausing again, so the nodes after the breakpoint still run once.
- **Breakpoints in On Start work too.** Play starts the objects in the first game frame, which the debugger can freeze.

### Editing while paused

- **Saving:** you can edit and save the graph while the game is paused. The running copy keeps going with the old graph until the paused event ends; the next event uses the saved one.
- **Removing objects:** removing objects, undo, a new scene and loading a scene all let the frozen frame finish first.

## Palette index

Every hand-written node in the editor's node list (`Registry()` in `editor/VisualScriptEditor.hpp`), under the same group names as the sidebar.
The word in backticks is the type stored in a `.okayvs` file.
The **Engine API** group is the generated list in [engine_api.md](engine_api.md): one node for each script property and method. It is not copied here.

### Events

- **On Start** (`OnStart`)
- **On Update** (`OnUpdate`)
- **On Collision** (`OnCollision`)
- **On Trigger** (`OnTrigger`)
- **On Click** (`OnClick`)
- **On Anim Event** (`OnAnimEvent`) — setting: event (blank = any)
- **On Key Pressed** (`OnKey`) — setting: key
- **On Key Released** (`OnKeyUp`) — setting: key
- **On Timer** (`OnTimer`) — setting: every N seconds
- **On Custom Event** (`OnEvent`) — out Value; setting: event name
- **On Collision Exit** (`OnCollisionExit`)
- **On Trigger Exit** (`OnTriggerExit`)
- **On Mouse Enter** (`OnMouseEnter`)
- **On Mouse Down** (`OnMouseDown`)
- **On Mouse Up** (`OnMouseUp`)
- **On Mouse Over** (`OnMouseOver`)
- **On Collision Stay** (`OnCollisionStay`)
- **On Late Update** (`OnLateUpdate`)
- **On Fixed Update** (`OnFixedUpdate`)
- **On Enable** (`OnEnable`)
- **On Disable** (`OnDisable`)
- **On Destroy** (`OnDestroy`)
- **On Mouse Exit** (`OnMouseExit`)
- **On Object Placed** (`OnObjectPlaced`)
- **On Object Removed** (`OnObjectRemoved`)
- **On Logic Signal** (`OnLogicSignal`)
- **On Item Added** (`OnItemAdded`)
- **On Item Removed** (`OnItemRemoved`)
- **On Inventory Full** (`OnInventoryFull`)
- **On Item Used** (`OnItemUsed`)
- **On Equip** (`OnEquip`)
- **On Craft** (`OnCraft`)
- **On Sequence Event** (`OnSequenceEvent`, optional name)
- **On Cheat Detected** (`OnCheatDetected`)
- **On Validate Rpc** (`OnValidateRpc`)
- **On Validate Hit** (`OnValidateHit`)
- **On Trigger Stay** (`OnTriggerStay`)
- **On Var Changed** (`OnVarChanged`) — out Value, Old; setting: variable
- **On Scene Var Changed** (`OnSceneVarChanged`) — out Value, Old; setting: variable
- **On Global Var Changed** (`OnGlobalVarChanged`) — out Value, Old; setting: variable

### Flow

- **Branch** (`Branch`) — in Cond
- **Sequence** (`Sequence`)
- **Once** (`Once`)
- **Every (s)** (`Timer`) — setting: seconds
- **Wait (s)** (`Wait`) — setting: seconds
- **Cooldown (s)** (`Cooldown`) — setting: seconds
- **For Loop** (`ForLoop`) — in First, Last; out Index
- **Repeat N** (`Repeat`) — setting: count
- **Flip-Flop** (`FlipFlop`)
- **Gate** (`Gate`) — in Open
- **Do N Times** (`DoN`) — out Count; setting: N
- **Switch (int)** (`SwitchInt`) — in Value
- **Delay (s)** (`Delay`) — setting: seconds
- **Random Branch** (`ChanceBranch`) — in Chance
- **Counter** (`Counter`) — in Reset; out Count
- **Every N Times** (`EveryN`) — in N
- **Function** (`FunctionStart`) — setting: Name(a: Number) -> out: Number
- **Return** (`Return`)
- **Call Function** (`CallFunction`)
- **While** (`While`) — in Cond
- **For Each** (`ForEach`) — in List; out Item, Index
- **Break** (`Break`)
- **Continue** (`Continue`)

### Variables

- **Get Var** (`GetVar`) — out Value; setting: name
- **Set Var** (`SetVar`) — in Value; setting: name
- **Add Var** (`AddVar`) — in Amount; setting: name
- **Toggle Var** (`Toggle`) — setting: name
- **Set Object Var** (`SetObjectVar`) — in Target, Value; setting: variable
- **Get Object Var** (`GetObjectVar`) — in Target; out Value; setting: variable
- **Save Value** (`SaveValue`) — in Value; setting: key
- **Load Value** (`LoadValue`) — in Default; out Value; setting: key
- **Set Local** (`SetLocal`) — in Value; setting: name
- **Get Local** (`GetLocal`) — out Value; setting: name
- **Get Scene Var** (`GetSceneVar`) — out Value; setting: name
- **Set Scene Var** (`SetSceneVar`) — in Value; setting: name
- **Add Scene Var** (`AddSceneVar`) — in Amount; setting: name
- **Get Global Var** (`GetGlobalVar`) — out Value; setting: name
- **Set Global Var** (`SetGlobalVar`) — in Value; setting: name
- **Add Global Var** (`AddGlobalVar`) — in Amount; setting: name

### Math

- **Add (+)** (`Add`) — in A, B; out Out
- **Sub (-)** (`Sub`) — in A, B; out Out
- **Mul (x)** (`Mul`) — in A, B; out Out
- **Div (/)** (`Div`) — in A, B; out Out
- **Min** (`Min`) — in A, B; out Out
- **Max** (`Max`) — in A, B; out Out
- **Mod (%)** (`Mod`) — in A, B; out Out
- **Pow (^)** (`Pow`) — in A, B; out Out
- **Abs** (`Abs`) — in In; out Out
- **Negate** (`Neg`) — in In; out Out
- **Sqrt** (`Sqrt`) — in In; out Out
- **Sin** (`Sin`) — in In; out Out
- **Cos** (`Cos`) — in In; out Out
- **Floor** (`Floor`) — in In; out Out
- **Round** (`Round`) — in In; out Out
- **Sign** (`Sign`) — in In; out Out
- **Clamp** (`Clamp`) — in Value, Min, Max; out Out
- **Lerp** (`Lerp`) — in A, B, T; out Out
- **Random 0-1** (`Random`) — out Out
- **Random Range** (`RandomRange`) — in Min, Max; out Out
- **Vec X** (`VecX`) — in Vec; out X
- **Vec Y** (`VecY`) — in Vec; out Y
- **Vec Z** (`VecZ`) — in Vec; out Z
- **Ease In** (`EaseIn`) — in T; out Out
- **Ease Out** (`EaseOut`) — in T; out Out
- **Ease In-Out** (`EaseInOut`) — in T; out Out
- **Tan** (`Tan`) — in In; out Out
- **Atan2** (`Atan2`) — in Y, X; out Rad
- **Exp** (`Exp`) — in In; out Out
- **Log (ln)** (`Log`) — in In; out Out
- **Inverse Lerp** (`InverseLerp`) — in A, B, Value; out T
- **Remap** (`Remap`) — in Value, In Min, In Max, Out Min, Out Max; out Out
- **Smooth Step** (`SmoothStep`) — in Edge0, Edge1, X; out Out
- **Ping-Pong** (`PingPong`) — in T, Length; out Out
- **Repeat (wrap)** (`Repeat01`) — in T, Length; out Out
- **Move Towards (value)** (`MoveTowardsValue`) — in Current, Target, Max Step; out Out
- **Delta Angle** (`DeltaAngle`) — in From, To; out Deg
- **Vec3 Add** (`Vec3Add`) — in A, B; out Vec3
- **Vec3 Subtract** (`Vec3Sub`) — in A, B; out Vec3
- **Vec3 Scale** (`Vec3Scale`) — in Vec, Scale; out Vec3
- **Vec3 Length** (`Vec3Length`) — in Vec; out Length
- **Vec3 Normalize** (`Vec3Normalize`) — in Vec; out Vec3
- **Vec3 Dot** (`Vec3Dot`) — in A, B; out Dot
- **Vec3 Distance** (`Vec3Distance`) — in A, B; out Dist
- **Random Int** (`RandomInt`) — in Min, Max; out Out
- **Ceil** (`Ceil`) — in In; out Out
- **Fraction** (`Frac`) — in In; out Out
- **One Minus** (`OneMinus`) — in In; out Out
- **Clamp 0-1** (`Saturate`) — in In; out Out
- **Asin** (`Asin`) — in In; out Rad
- **Acos** (`Acos`) — in In; out Rad
- **Deg to Rad** (`Deg2Rad`) — in Deg; out Rad
- **Rad to Deg** (`Rad2Deg`) — in Rad; out Deg
- **Vec3 Cross** (`Vec3Cross`) — in A, B; out Vec3
- **Vec3 Lerp** (`Vec3Lerp`) — in A, B, T; out Vec3
- **Vec3 Negate** (`Vec3Negate`) — in Vec; out Vec3
- **Perlin Noise** (`Noise`) — in X, Y; out Value
- **Snap To Step** (`Snap`) — in Value, Step; out Out
- **Wrap** (`Wrap`) — in Value, Min, Max; out Out
- **Average** (`Average`) — in A, B; out Out
- **Distance 2D** (`Distance2D`) — in X1, Y1, X2, Y2; out Distance

### Logic

- **Compare** (`Compare`) — in A, B; out Bool
- **And** (`And`) — in A, B; out Out
- **Or** (`Or`) — in A, B; out Out
- **Not** (`Not`) — in In; out Out
- **Xor** (`Xor`) — in A, B; out Out
- **Equals** (`Equals`) — in A, B; out Bool
- **Not Equals** (`NotEquals`) — in A, B; out Bool
- **Approx Equal** (`Approx`) — in A, B; out Bool
- **Between** (`Between`) — in Value, Min, Max; out Bool
- **Chance** (`Chance`) — out Bool; setting: 0..1
- **Has Var** (`HasVar`) — out Bool; setting: name
- **Select** (`Select`) — in Cond, A, B; out Out
- **Contains** (`Contains`) — in Text, Part; out Bool
- **Check** (`Condition`)

### Input

- **Key Held** (`Key`) — out Down; setting: key
- **Key Down** (`KeyDown`) — out Pressed; setting: key
- **Axis X** (`AxisX`) — out X
- **Axis Y** (`AxisY`) — out Y
- **Time** (`Time`) — out t
- **Delta Time** (`DeltaTime`) — out dt
- **Key Up** (`KeyUp`) — out Released; setting: key
- **Mouse X** (`MouseX`) — out X
- **Mouse Y** (`MouseY`) — out Y
- **Velocity X** (`VelX`) — out X
- **Velocity Y** (`VelY`) — out Y
- **Velocity Z** (`VelZ`) — out Z
- **Mouse Held** (`MouseButton`) — out Down; setting: 0 L / 1 R / 2 M
- **Mouse Pressed** (`MouseButtonDown`) — out Pressed; setting: 0 L / 1 R / 2 M
- **Raycast 2D** (`Raycast`) — in Origin X, Origin Y, Dir X, Dir Y, Max Dist; out Hit
- **Mouse Wheel** (`MouseWheel`) — out Delta
- **Time Scale** (`TimeScale`) — out Scale
- **Frame Count** (`FrameCount`) — out Frame
- **Show Cursor** (`ShowCursor`) — in Visible
- **Raycast 3D** (`Raycast3D`) — in Origin, Direction, Max Dist, Ignore Triggers; out Hit, Point, Normal, Distance, Object; setting: layers (blank = all)
- **Sphere Cast** (`SphereCast3D`) — in Origin, Direction, Radius, Max Dist; out Hit, Point, Normal, Distance, Object; setting: layers (blank = all)
- **Raycast All 3D** (`RaycastAll3D`) — in Origin, Direction, Max Dist; out Count, Objects, Nearest; setting: layers (blank = all)
- **Overlap Sphere** (`OverlapSphere3D`) — in Center, Radius; out Count, Objects; setting: layers (blank = all)
- **Overlap Box** (`OverlapBox3D`) — in Center, Half Size; out Count, Objects; setting: layers (blank = all)

### Transform

- **Get X** (`GetX`) — out X
- **Get Y** (`GetY`) — out Y
- **Translate** (`Translate`) — in X, Y, Z
- **Set Position** (`SetPosition`) — in X, Y, Z
- **Rotate** (`Rotate`) — in Deg
- **Get Position** (`GetPosition`) — out Pos
- **Get Z** (`GetZ`) — out Z
- **Set Scale** (`SetScale`) — in X, Y, Z
- **Set Rotation** (`SetRotation`) — in Deg
- **Object Exists** (`ObjectExists`) — in Object; out Bool; setting: name
- **Object Position** (`ObjectPosition`) — in Object; out Pos; setting: name / self / other
- **Distance To** (`DistanceTo`) — in Object; out Dist; setting: name / other
- **Self Name** (`SelfName`) — out Name
- **Look At** (`LookAt`) — in Object; setting: name / other
- **Move Towards** (`MoveTowards`) — in Speed, Object; setting: name / other
- **Forward Vector** (`ForwardVector`) — out Vec3
- **Right Vector** (`RightVector`) — out Vec3
- **Up Vector** (`UpVector`) — out Vec3
- **Get Scale** (`GetScale`) — out Scale
- **Get Rotation** (`GetRotation`) — out Euler
- **Set Position (Vec3)** (`SetPositionVec`) — in Pos
- **Move By (Vec3)** (`MoveBy`) — in Offset
- **Rotate (3D)** (`Rotate3`) — in X, Y, Z
- **Set Rotation (3D)** (`SetRotation3`) — in X, Y, Z
- **Find With Tag** (`FindWithTag`) — out Object; setting: tag
- **Count With Tag** (`CountWithTag`) — out Count; setting: tag
- **Nearest With Tag** (`NearestWithTag`) — out Object; setting: tag
- **Teleport** (`Teleport`) — in X, Y, Z, Object; setting: object (self)
- **Self** (`Self`) — out Object
- **Other Object** (`Other`) — out Object
- **Find Object** (`FindObject`) — out Object; setting: object name
- **Get Name** (`GetName`) — out Name
- **Get Tag** (`GetTag`) — out Tag
- **All With Tag** (`AllWithTag`) — out Objects; setting: tag

### Actions

- **Print** (`Print`) — in Value
- **Play Sound** (`PlaySound`)
- **Spawn** (`Spawn`) — in X, Y, Z; out Object; setting: prefab
- **Destroy** (`Destroy`) — in Target
- **Log Warning** (`LogWarn`) — in Value
- **Log Error** (`LogError`) — in Value
- **Add Force** (`AddForce`) — in X, Y, Z
- **Set Velocity** (`SetVelocity`) — in X, Y, Z
- **Add Impulse** (`AddImpulse`) — in X, Y, Z
- **Set Active** (`SetActive`) — in On, Object; setting: name / self / other
- **Set Text** (`SetText`) — in Text, Object; setting: text object name
- **Load Scene** (`LoadScene`) — setting: scene name
- **Anim Trigger** (`AnimTrigger`) — setting: parameter
- **Anim Set Float** (`AnimSetFloat`) — in Value; setting: parameter
- **Anim Set Bool** (`AnimSetBool`) — in Value; setting: parameter
- **IK Look At** (`IkLook`) — in Point, Weight
- **IK Hand Reach** (`IkHand`) — in Point, Weight; setting: left / right
- **Nav Move To** (`NavMoveTo`) — in Point: the object's Nav Mesh Agent walks there (see docs/navigation.md)
- **Nav Stop** (`NavStop`) — stops the Nav Mesh Agent
- **Nav Arrived** (`NavArrived`) — out Bool: true once the last Nav Move To point was reached
- **Set Color** (`SetColor`) — in R, G, B, A
- **Play Clip** (`PlayClip`) — setting: clip name
- **Set Time Scale** (`SetTimeScale`) — in Scale
- **Set Blend Shape** (`SetBlendShape`) — in Weight; setting: shape name
- **Action** (`Action`)
- **Action Steps** (`Actions`)
- **Send Event** (`SendEvent`) — in Target, Value; setting: event name
- **Broadcast Event** (`BroadcastEvent`) — in Value; setting: event name
- **Reject Rpc** (`RejectRpc`)
- **Reject Hit** (`RejectHit`)
- **Server Budget** (`ServerBudget`) — in Player, Add; out Left; setting: rpc name
- **Enter Build Mode** (`EnterBuildMode`)
- **Exit Build Mode** (`ExitBuildMode`)
- **Set Build Palette** (`SetBuildPalette`) — setting: Name=prefab|icon, ...
- **Save Creation** (`SaveCreation`) — out Saved; setting: creation name
- **Load Creation** (`LoadCreation`) — out Loaded; setting: creation name
- **Set Visible** (`SetVisible`) — in Visible
- **Pause Game** (`PauseGame`) — in Paused
- **Quit Game** (`QuitGame`)
- **Show UI** (`ShowUI`) — setting: UI object (self)
- **Hide UI** (`HideUI`) — setting: UI object (self)

### Data

- **Const** (`Const`) — out Value; setting: value
- **Make Vec3** (`MakeVec3`) — in X, Y, Z; out Vec3
- **Concat (text)** (`Concat`) — in A, B; out Out
- **Anim Event Name** (`AnimEventName`) — out Name
- **Other Name** (`OtherName`) — out Name
- **Other Tag** (`OtherTag`) — out Tag
- **To Text** (`ToString`) — in Value; out Text
- **Format Number** (`ToFixed`) — in Value; out Text; setting: decimals
- **Text Length** (`TextLength`) — in Text; out Len
- **Upper Case** (`Upper`) — in Text; out Text
- **Lower Case** (`Lower`) — in Text; out Text
- **Anim State** (`AnimState`) — out State
- **Blend Shape** (`GetBlendShape`) — out Weight; setting: shape name
- **Event Value** (`EventValue`) — out Value
- **Event Name** (`EventName`) — out Name
- **In Build Mode?** (`InBuildMode`) — out Bool
- **Format Text** (`Format`) — in {0}, {1}, {2}; out Text; setting: Score: {0}
- **Substring** (`Substring`) — in Text, Start, Length; out Text
- **Replace Text** (`Replace`) — in Text, Find, With; out Text
- **To Number** (`ToNumber`) — in Text; out Number
- **Starts With** (`StartsWith`) — in Text, Prefix; out Yes
- **Ends With** (`EndsWith`) — in Text, Suffix; out Yes
- **Trim Spaces** (`Trim`) — in Text; out Text
- **Index Of** (`IndexOf`) — in Text, Find; out Index
- **Contact Point** (`ContactPoint`) — out Point
- **Contact Normal** (`ContactNormal`) — out Normal
- **Impact Speed** (`ImpactSpeed`) — out Speed
- **New List** (`NewList`) — out List
- **List Add** (`ListAdd`) — in List, Item
- **List Remove At** (`ListRemoveAt`) — in List, Index
- **List Get** (`ListGet`) — in List, Index; out Item
- **List Length** (`ListLength`) — in List; out Count
- **List Contains** (`ListContains`) — in List, Item; out Bool

### Multiplayer & Steam

- **Host Game** (`NetHost`) — setting: port (45000)
- **Join Game** (`NetJoin`) — setting: host [port]
- **Disconnect** (`NetDisconnect`)
- **Is Connected** (`NetConnected`) — out Bool
- **Is Server** (`NetIsServer`) — out Bool
- **Is Client** (`NetIsClient`) — out Bool
- **Peer Count** (`NetPeers`) — out Count
- **My Net Id** (`NetId`) — out Id
- **Send Message** (`NetSend`) — in Data; setting: channel
- **Set Net Var** (`NetSetVar`) — in Value; setting: name
- **Get Net Var** (`NetGetVar`) — out Value; setting: name
- **Send Chat** (`NetChat`) — in Text
- **Call RPC** (`NetRpc`) — in Data; setting: rpc name
- **Net Spawn** (`NetSpawnOwned`) — setting: prefab
- **Net Despawn** (`NetDespawn`) — in Net Id
- **Set Ready** (`NetReady`) — in Ready
- **Start Match** (`NetStartMatch`)
- **Match Started** (`NetMatchStarted`) — out Bool
- **All Ready** (`NetAllReady`) — out Bool
- **Call Object RPC** (`NetObjRpc`) — in Data; setting: rpc name
- **Set Sync Var** (`SyncVarSet`) — in Value; setting: name
- **Get Sync Var** (`SyncVarGet`) — out Value; setting: name
- **On Object RPC** (`OnNetRpc`) — out Value; setting: rpc name
- **Steam Name** (`SteamName`) — out Name
- **Achievement Unlocked** (`SteamIsUnlocked`) — out Bool; setting: achievement id
- **Unlock Achievement** (`SteamUnlock`) — setting: achievement id
- **Get Steam Stat** (`SteamGetStat`) — out Value; setting: stat
- **Set Steam Stat** (`SteamSetStat`) — in Value; setting: stat
- **Store Stats** (`SteamStore`)
- **Submit Score** (`SteamLeaderboard`) — in Score; setting: leaderboard
