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

**Actions** (1 exec out): `SetVar <name>`, `AddVar <name>` (add in0 to a variable),
`Print`, `Translate` (in0=x, in1=y), `SetPosition` (in0=x, in1=y),
`Rotate` (in0=degrees), `SetRotation` (in0=absolute degrees about Z),
`SetScale` (in0=x, in1=y, in2=z; default 1), `Destroy` (remove this object —
terminal, no exec out)

**Flow**:
- `Branch` — exec out 0 = true, exec out 1 = false; data in0 = condition
- `Sequence <n>` — fire each of `n` exec outputs in order (default 2)
- `Once` — pass through only the first time it's reached (one-shot setup)
- `Timer <seconds>` — pass through at most once every `seconds` (accumulates
  `DeltaTime`); ideal for "every N seconds" pulses under `OnUpdate`
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
  `chance`, `var_eq`, `var_neq`, `var_gt`, `var_lt`, `prefs_eq`, `prefs_gt`,
  `has_tag`, `is_active`, `dist_lt`, `dist_gt`, `exists`.
- **Instructions**: movement (`move`, `set_pos`, `rotate`, `set_scale(3)`,
  `move_toward`, `look_at`), control (`wait`, `goto`, `stop`), variables
  (`set_var`, `add_var`, `mul_var`, `div_var`, `copy_var`, `rand_var`),
  objects (`spawn`, `spawn3`, `destroy`, `destroy_obj`, `activate`,
  `deactivate`, `set_active`, `set_tag`), rendering/audio (`set_text`,
  `set_color`, `emit`, `play_anim`, `play_sound`, `set_cam`, `set_bg`,
  `set_light`, `set_ambient`), physics (`velocity`, `impulse`), data
  (`set_prefs`, `add_prefs`, `save_prefs`), **scenes** (`load_scene`,
  `load_scene_index`, `load_next_scene`), **multiplayer** (`net_host`,
  `net_join`, `net_send`, `net_set` synced vars, `net_spawn` replicated spawn,
  `net_disconnect`), **Steam** (`steam_unlock`, `steam_set_stat`,
  `steam_inc_stat`), messaging (`send`), and `log`.

Multiplayer with zero code: a `Player` with an `OnKey` ActionList whose
instruction is `net_host 45000`, and another whose instruction is
`net_join 127.0.0.1 45000`, is a complete host/join setup.
