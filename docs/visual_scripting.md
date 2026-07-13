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
  `steam_inc_stat`), messaging (`send`), **juice** (`tween_move`,
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
