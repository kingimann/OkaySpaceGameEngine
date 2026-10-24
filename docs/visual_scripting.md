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

**Data — physics & tween**: `VelX` / `VelY` (this object's Rigidbody2D velocity),
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

**Finding nodes:** right-click the canvas and start typing; the search box
matches titles, type names and categories, and **Enter** adds the first match.

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

- **Drag a wire into empty space** and the node palette opens, filtered to nodes
  that fit; the node you pick is wired to that pin.
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
| Raycast All 3D | everything along the line, nearest first: Count, Objects (names), Nearest |
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
