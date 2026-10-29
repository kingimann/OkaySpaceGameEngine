# OkayScript

OkayScript is the engine's built-in scripting language — a small, dependency-free
language that ships in every build (no Lua/C# toolchain required). It's its own
tiny language, designed so you write almost nothing and still do a lot.

The whole point is minimal code:

- **No boilerplate** — the whole file runs every frame, so a complete behaviour
  can be a single line:

  ```c
  on_key_move(5)     // WASD / arrows — the entire script
  ```

- **`function` is optional, and so are braces on one-line bodies:**

  ```c
  // start() runs once, update(dt) runs every frame — no 'function' keyword.
  start()      { set_pos(0, 0) }
  update(dt)   { if (key_down("space")) jump(8) }   // no braces for one line
  ```

- **Movement one-liners are dt-scaled for you** — you never write `* dt`.
  `walk(1, 0, 5)` moves 5 units/second regardless of frame rate; so do `spin`,
  `follow`, `patrol`, `orbit`, `platformer`, `smooth_follow`, `spring_to`, …

Attach a `ScriptComponent` to a GameObject and the engine calls your `start()`
and `update(dt)` (plus event handlers like `on_collision(other)`) as part of the
normal scene lifecycle.

## C#-style syntax

Scripts read like C#, and every piece of syntax does something. A script's
fields and functions **are** the component: there is no class or base type to
write. New scripts start like this:

```csharp
// Attach this script to an object. Its fields and functions are the component.

[Header("Movement")]
public float speed = 5;        // public: shows in the Inspector
public float hopHeight = 1;
public int maxHops = 3;

int hops = 0;                   // not public: only this script sees it

void Start()
{
    Debug.Log($"{gameObject.name} is ready");
}

void Update()
{
    float h = Input.GetAxis("Horizontal");
    float v = Input.GetAxis("Vertical");
    transform.position += new Vector3(h, v, 0) * speed * Time.deltaTime;

    if (Input.GetKeyDown(KeyCode.Space) && hops < maxHops)
    {
        hops++;
        StartCoroutine(Hop());
    }
}

IEnumerator Hop()
{
    transform.position += Vector3.up * hopHeight;
    yield return new WaitForSeconds(0.25);
    transform.position -= Vector3.up * hopHeight;
}
```

### What each part does

| Syntax | What it does |
|---|---|
| `public float speed = 5;` | `public` shows the field in the Inspector so it can be tuned per object. Fields without it are private to the script. `[SerializeField]` shows a non-public field too. |
| `[Header("Movement")]`, `[Tooltip("...")]`, `[Range(0, 10)]` | A heading above the next fields, a hover tip, a slider in the Inspector. |
| `int`, `float`, `bool`, `string` | Real types. An `int` drops the decimals (`int half = 7 / 2;` is 3). Putting the wrong kind of value in one (text in an `int`, a number in a `string`) is an error, shown in the editor as you type when it's a literal. `double`/`decimal` are floats; `long`/`short`/`byte`/... are ints. |
| `var x = ...;` | A variable that can hold anything. Vector3, List, Dictionary and other types also hold anything of their kind. |
| `const` / `readonly` | Can't change after it's set: assigning again is an error. |
| `static int alive;` | One value shared by every object running this script (for example a count of all enemies). |
| `void Hit(int dmg, int bonus = 2)` | Typed parameters convert their argument (`Hit(7.9)` gets 7). A parameter can have a default, and those come last: `Hit(5)` uses 2 for `bonus`. Call with names: `Hit(bonus: 4, dmg: 1)`. `int Score()` converts what it returns. A `void` function can't return a value. |
| `IEnumerator Blink()` + `yield return ...` | A coroutine: runs across frames, pausing at each `yield`. Start it with `StartCoroutine(Blink())`. |
| `enum State { Idle, Run }` | Named values: `State.Run`, `switch (state)`. |

### Functions the engine calls

| Function | When |
|---|---|
| `Awake()`, `OnEnable()`, `Start()` | Once each, in that order, when the scene starts. |
| `Update()` / `LateUpdate()` | Every frame (`Time.deltaTime` is the seconds since the last). |
| `FixedUpdate()` | 50 times a second, however fast the game draws. |
| `OnDisable()`, `OnDestroy()` | When the object is destroyed. |
| `OnCollisionEnter/Stay/Exit(string other)`, `OnTriggerEnter/Stay/Exit(string other)` | Physics contacts, 2D and 3D. `other` is the other object (`other.name`, `other.CompareTag(..)`, `other.transform.position`). |
| `OnMouseDown/Up/Enter/Exit/Over()`, `OnClick()` | Pointer events on the object. |

### Syntax that does nothing

Pasted C# still runs, but these have no effect, and the Script Editor marks
each one with a warning saying what to delete:

- `using X;` and `namespace X { }`: everything is already available.
- A `class X : Base { }` wrapper: the fields and functions inside are the component either way.
- `virtual`, `override`, `abstract`, `sealed`, `protected`, `internal`, `partial`, and `private` (already the default).
- Number suffixes like `5f`: every number is already a decimal.
- Methods the engine never calls (`OnGUI`, `OnApplicationQuit`, `OnBecameVisible`, ...).

### Also available

| Area | Supported |
|---|---|
| Collections | `List<T>`, `Dictionary<K,V>`, arrays `int[]` (`new int[3]`, `new[] {1, 2}`, `{1, 2, 3}`), `int a = 1, b = 2;` |
| Expressions | casts `(int)x`, `$"interpolated {strings}"`, `?:`, `%=`, bitwise `& \| ^`, `++x`, Vector3 maths (`v + w`, `v * 2`, `-v`, `v / 2`), `(a - b).normalized` |
| Methods on values | strings: `ToUpper`, `ToLower`, `Contains`, `StartsWith`, `EndsWith`, `IndexOf`, `Substring`, `Replace`, `Split`, `Trim`, `PadLeft`...; numbers: `ToString("F2")`; lists: `Add`, `Remove`, `RemoveAt`, `Insert`, `Contains`, `IndexOf`, `Clear`, `Sort`, `Reverse`, `Count`; dictionaries: `Add`, `ContainsKey`, `Remove`, `Keys`, `Values`, `GetValueOrDefault`, `foreach (var kv in d)` |
| Lambdas | `x => x * 2`, `(a, b) => { ... }`, and defaults `(x = 3) => x + 1`, with `ForEach`, `Where`, `Select`, `Find`, `FindIndex`, `Any`, `All`, `Count`, `RemoveAll`, `OrderBy`, `Sum`, `Max`, `Sort((a, b) => a - b)` |
| Coroutines | `StartCoroutine(Routine(args))`, `yield return null`, `yield return new WaitForSeconds(t)`, `yield return StartCoroutine(Other())`, `yield break`, `StopCoroutine`, `StopAllCoroutines` |
| Components | `GetComponent<T>()` returns a usable reference: `Rigidbody2D`/`Rigidbody` (`velocity`, `AddForce`, `MovePosition`, `gravityScale`), `SpriteRenderer`/`Renderer` (`color`, `enabled`, `flipX`), `Text` (`text`, `color`), `Animator` (`SetTrigger`, `SetBool`, `SetFloat`, `Play`), `AudioSource` (`Play`, `Stop`, `volume`) |
| Engine API | `transform.Translate/Rotate/LookAt`, `eulerAngles`/`localEulerAngles`, `Vector3.*` (constants, `Distance`, `Lerp`, `MoveTowards`, `Cross`, `Dot`...), `Mathf.*` (incl. `RoundToInt`, `FloorToInt`), `Random.Range`, `Input.GetKey/GetKeyDown/GetAxis/GetAxisRaw/GetMouseButton`, `Time.time/deltaTime/frameCount`, `Debug.Log/LogWarning/LogError`, `GameObject.Find/FindWithTag`, `Instantiate(prefab, pos, rot)`, `Destroy(obj, delay)`, `Invoke`, `InvokeRepeating`, `CancelInvoke`, `string.Join/IsNullOrEmpty`, `int.Parse` |

Objects are referenced by name under the hood, so `GameObject.Find("Enemy")`
returns a reference you can pass to `Destroy`, compare with `null`, or read
`.tag` / `.transform.position` from. Not supported: your own classes as types
you `new`, generics you declare yourself, `ref`/`out` parameters, LINQ query
syntax, and properties with bodies.

In the Script Editor, **Insert ▸ C# snippets** adds fields, handlers, coroutines
and collections, and **Help ▸ Cheat Sheet** shows the short version.

## Compatibility: C-style syntax also parses

OkayScript's native style is the minimal one above — but if you're pasting code
from elsewhere, a fuller C-style form with dot-properties and typed declarations
also parses, so familiar snippets tend to just work. This is optional; prefer the
short native form for new code.

```c
speed = 5

update(dt) {
    // Move with the arrow/WASD axes.
    transform.position.x += axis_x() * speed * dt
    transform.position.y += axis_y() * speed * dt
    if (key_down("space")) log("jump!")
    for (i = 0; i < 3; i = i + 1) { /* ... */ }
}
```

What's supported:

- **Lifecycle functions** `start()`, `update(dt)`, `late_update()` (and `Awake()`
  / `Start()` / `Update()` / `LateUpdate()`). A typed/`void` return and a
  `class { ... }` wrapper are accepted and their methods are hoisted out, so
  familiar code often pastes in unchanged.
- **Dot properties**: `transform.position` / `.localPosition` / `.localScale`
  (and `.x/.y/.z`), `transform.eulerAngles.z`, `Time.deltaTime` / `Time.time` /
  `Time.timeScale`, `gameObject.name` / `.activeSelf` / `.tag`, `Screen.width/height`,
  `Input.mousePosition.x/y`, `Mathf.PI`, `Random.value`, `Vector3.zero/one/up/...`.
  Readable and assignable (`transform.position.x = 5;`).
- **Dot methods**: `Input.GetKey/GetKeyDown/GetKeyUp/GetAxis/GetMouseButton...`,
  `Mathf.Sin/Cos/Sqrt/Abs/Clamp/Lerp/...`, `Debug.Log(...)`,
  `transform.Translate(x,y)` / `transform.Rotate(deg)`, `gameObject.SetActive(b)`,
  `Instantiate(...)`, `Destroy()`, `Physics2D.Raycast(...)`,
  `Random.Range(a,b)`, `SceneManager.LoadScene("name")`.
- **`new Vector3(x,y,z)`** / `new Vector2(x,y)` constructors.
- **Typed declarations** `int x = 0;` `float speed = 5f;` `Vector3 v = ...;` (with
  `f`/`d` numeric suffixes), plus `i++` / `++i` / `i--`.
- **`foreach (var item in list) { ... }`** (C# foreach), alongside the classic
  `for x in list { }`.
- **`switch (x) { case 1: ...; break; default: ...; }`** (C-style fall-through
  until `break`; `case` labels can be numbers or strings).
- **String interpolation** `$"Score: {score}  next={score + 1}"` — embed any
  expression in `{ }` (use `{{`/`}}` for literal braces).
- **More `Mathf`**: `Mathf.Clamp01`, `Mathf.Repeat`, `Mathf.InverseLerp`,
  `Mathf.Approximately`, `Mathf.DeltaAngle`, `Mathf.LerpAngle`.
- **`do { ... } while (cond);`** loops (runs the body at least once).
- **Vector3 values & math**: `var v = new Vector3(3, 4, 0);` then read/write
  `v.x` / `v.y` / `v.z` and read `v.magnitude` / `v.sqrMagnitude` /
  `v.normalized`; or use `vec_add`, `vec_sub`, `vec_scale`, `vec_length`,
  `vec_dot`, `vec_distance`, `vec_normalize`, `vec_lerp`, `vec_move_towards`,
  `vec_x/y/z` (and `Vector3.Lerp` / `Dot` / `Normalize` / `Distance` /
  `MoveTowards`).
- **Direction & input vectors**: `transform.forward` / `transform.up` /
  `transform.right` and `Input.mousePosition` return Vector3 values.
- **`Mathf.PerlinNoise(x, y)`** — smooth 2D noise in `[0, 1]` (terrain, wobble,
  procedural motion).
- **Color values**: `Color.red/green/blue/white/black/yellow/cyan/magenta/gray`
  and `Color(r, g, b)` / `new Color(r, g, b)`; pass them to `set_color` /
  `set_tint` (which also still take separate `r, g, b[, a]` numbers).
- **String formatting**: `format("HP {0}/{1}", hp, max)` (positional) or
  `format("HP {} / {}", hp, max)` (sequential), plus `pad_left(s, width[, ch])`
  and `pad_right(s, width[, ch])` — also as `string.Format(...)`.
- **More Mathf**: `Mathf.Clamp01(x)`, `Mathf.InverseLerp(a, b, v)`,
  `Mathf.Repeat(t, len)`, `Mathf.DeltaAngle(a, b)`, `Mathf.LerpAngle(a, b, t)`,
  `Mathf.Approximately(a, b)` (and `clamp01` / `inverse_lerp` / `delta_angle` /
  `lerp_angle` / `approximately`).
- **`Color.Lerp(a, b, t)`** (`lerp_color`) blends two colors; pair it with
  `set_color` for fades and flashes.
- **`.Length` / `.Count`**: read the length of a string (`name.Length`) or an
  array (`items.Count` / `items.Length`) the C# way.
- **`Vector3.Angle(a, b)`** (`vec_angle`) — angle in degrees between two vectors;
  `Vector2.Lerp` / `Vector3.LerpUnclamped` map onto `vec_lerp`; and
  `Input.GetButton/GetButtonDown/GetButtonUp` are accepted (same as the key
  checks).
- **Generic calls** `GetComponent<Rigidbody2D>()` (truthy if present) and
  `AddComponent<Rigidbody2D>()` (adds SpriteRenderer / Rigidbody2D / TextRenderer
  / BoxCollider2D at runtime).
- **`Quaternion.Euler(x, y, z)`** / `Quaternion.identity` — assign to
  `transform.rotation` (the Z angle drives 2D rotation).
- **C# attributes** like `[SerializeField]` and `[Header("Stats")]` are accepted
  (and ignored) before fields and methods.
- **Event handlers**, PascalCase form: `OnCollisionEnter()`, `OnTriggerEnter()`,
  `OnClick()`, `OnValueChanged()` (alongside the classic `on_collision` etc).

## Language

- **Variables:** `var x = 1;` (function-level scope). Reassign with `x = 2;`.
- **Types:** numbers (float), booleans (`true`/`false`), and strings (`"..."`).
- **Operators:** `+ - * / %`, comparisons `== != < > <= >=`, logical `&& || !`,
  and compound assignment `+= -= *= /=`.
- **Strings:** `+` concatenates if either side is a string; `==`/`!=` compare
  string contents.
- **Control flow:**
  - `if (cond) { ... } else { ... }`
  - `while (cond) { ... }`
  - `for (var i = 0; i < 10; i += 1) { ... }`
  - `for x in myArray { ... }` (foreach over an array)
  - `break` and `continue` inside loops
- **Ternary:** `cond ? a : b`
- **Functions:** `function name(a, b) { return a + b; }`
  A later parameter can have a default (`function heal(hp, bonus = 2)`), and a
  call can name them (`heal(bonus: 4, hp: 1)`). Lambdas too: `(x = 3) => x + 1`.
- **Arrays:** `var a = [1, 2, 3];` — index with `a[0]`, assign `a[1] = 9`,
  append with `a[count(a)] = x` or `push(a, x)`, remove with `pop(a)`, length
  via `count(a)`. Arrays are shared by reference. Make an empty one with
  `array()`.
- **Comments:** `// ...` or `# ...`

Top-level statements run once when the script loads (good for setup); `start()`
runs when the scene starts and `update(dt)` runs every frame.

### Event handlers
Define any of these functions and the engine calls them automatically. Physics
events work for both 2D and 3D colliders; mouse events pick against the object's
sprite (or 2D collider) bounds under the cursor, using the main camera.

| Handler | Fires when |
| --- | --- |
| `on_trigger_enter()` / `on_trigger_stay()` / `on_trigger_exit()` | A trigger collider begins / continues / ends overlapping another |
| `on_collision_enter()` / `on_collision_stay()` / `on_collision_exit()` | A solid collider starts / continues / stops touching another |
| `on_mouse_enter()` / `on_mouse_exit()` | The cursor moves onto / off the object |
| `on_mouse_over()` | Every frame the cursor is over the object |
| `on_mouse_down()` / `on_mouse_up()` | The left button is pressed / released over it |
| `on_click()` | A full press-and-release on the same object (or a UI Button press) |
| `on_change()` / `on_toggle()` | A sibling UI Slider is dragged / UI Toggle is clicked |

The older `on_trigger()` and `on_collision()` names still work — they're treated
as the *enter* handlers.

Subscribe without replacing the function. `+=` adds a listener and `-=` removes
one. The right-hand side is a function name or a lambda. A number on the right
is still ordinary math (`score += 1`). `OnClick` and `on_click` are the same
event. `raise("OnHurt", 3)` runs the listeners on this script (and a function
named `OnHurt`, if there is one).

```
function AddBonus(n) { print("BONUS " + n); }
function start() {
    OnHurt += (n) => { print("HURT " + n); };
    OnHurt += AddBonus;
    raise("OnHurt", 3);    // HURT 3, then BONUS 3
    OnHurt -= AddBonus;
}
```

Inside a collision or trigger handler these describe the touch:

| Value | Meaning |
| --- | --- |
| `other` / `other_tag` | the other object's name / tag |
| `contact_point` / `contact_normal` | where it touched, and the surface direction (vectors; normal points at the other object) |
| `contact_x/_y/_z`, `contact_nx/_ny/_nz` | the same as numbers |
| `impact_speed` | how hard it hit (closing speed; 0 for triggers) |

```
function on_collision_enter(other) {
    if (impact_speed > 8) spawn3("Sparks", contact_x, contact_y, contact_z)
}
```

### Physics queries: layers, triggers, casts

Every ray, cast and overlap takes two optional trailing arguments after the
distance: **layers** (a number from `layer_mask(...)`, or a list such as
`"0,3"` / `"Default,Enemy"`, `"!Ignore Raycast"` for all but one) and
**ignore_triggers** (`true` to pass through trigger colliders).

```
var ground = raycast3(x, y, z, 0, -1, 0, 1.2, "Default", true)
var hits   = raycast_all3(x, y, z, 1, 0, 0, 50)     // nearest first
var wall   = sphere_cast3(x, y, z, 1, 0, 0, 0.4, 5)  // won't slip through gaps
```

Colliders also have a **trigger filter** (Inspector: *Only Tag* / *Only Layers*
on a trigger): a pickup set to `Player` ignores enemies completely — no enter,
stay or exit events on either side. Rigidbodies have **Continuous Collision**
for fast objects (bullets, thrown balls) so they stop at thin walls instead of
passing through between two frames.

## Built-in functions

### Transform
| Function | Effect |
| --- | --- |
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `set_x(x)` / `set_y(y)` | Set one axis |
| `pos_x()` / `pos_y()` | Read local position |
| `rotate(deg)` | Rotate about Z by degrees |

### Movement one-liners (dt-scaled — never write `* dt`)
Each is a whole behaviour meant to be called every frame from `update()`. Speeds
are in units/second (or degrees/second), independent of frame rate.

| Function | Effect |
| --- | --- |
| `walk(dx, dy, speed)` | Head in a direction at `speed` |
| `spin(degPerSec)` | Rotate smoothly forever |
| `on_key_move(speed)` | WASD / arrows (drives a Rigidbody2D if present) |
| `platformer(speed[, jump])` | Full 2D side-scroller: A/D move, W/Up jumps (Rigidbody2D) |
| `move_to(x, y, speed)` | Walk to a point, stop on arrival |
| `spring_to(x, y[, speed])` | Ease to a point, slowing as it arrives |
| `follow("name", speed[, stop])` | Chase an object, stopping `stop` away |
| `smooth_follow("name", speed)` | Chase with easing so it glides in |
| `flee("name", speed)` | Run away from an object |
| `patrol(x1, y1, x2, y2, speed)` | Walk back and forth between two points |
| `orbit("name", radius, degPerSec)` | Circle a target |
| `aim(x, y)` | Rotate to face a world point (turrets, arrows) |
| `grid_snap(size)` | Snap position to a grid (tile / building games) |
| `wander(speed)` | Roam, changing direction ~once a second |
| `bob(amount, speed)` / `pulse(amount, speed)` | Hover / breathe (juice) |
| `on_key("key", "fn")` | Call your function `fn` the frame `key` is pressed |

### Input
| Function | Returns |
| --- | --- |
| `key("a")` | true while the key is held |
| `key_down("a")` | true on the frame the key is pressed |
| `key_up("a")` | true on the frame the key is released |

Key names are a single letter/digit, or a named key: `"space"`, `"up"`, `"down"`,
`"left"`, `"right"`, `"enter"`, `"escape"`, `"backspace"`, `"tab"`, `"shift"`,
`"ctrl"`. (Arrow keys and WASD are interchangeable.)

| Function | Returns |
| --- | --- |
| `axis_x()` / `axis_y()` | -1..1 from A/D and S/W |
| `mouse_x()` / `mouse_y()` | cursor position in pixels |
| `mouse(btn)` | true while a mouse button is held (0=left, 1=right, 2=middle) |
| `mouse_down(btn)` | true on the frame the button is pressed |
| `gamepad_x()` / `gamepad_y()` | left-stick axis (-1..1, y up) |
| `gamepad(btn)` / `gamepad_down(btn)` | held / just-pressed (0=A 1=B 2=X 3=Y 5=Start ...) |

### Time & state
| Function | Returns |
| --- | --- |
| `time()` | seconds since start |
| `dt()` | last frame delta time |
| `after(secs, "fn")` | call a function once after a delay (respawns, cooldowns) |
| `every(secs, "fn")` | call a function repeatedly at an interval (spawn waves, blinking) |
| `cancel_timers()` | clear this script's scheduled after()/every() callbacks |
| `cooldown("name", secs)` | true (and restarts) only once every `secs` — shooting, dashes, abilities |
| `once("name")` | true exactly the first time it's reached — one-time events, no flag |
| `stop()` | zero this object's Rigidbody velocity |
| `is_moving()` / `face_velocity()` | is the Rigidbody2D moving / turn to its heading |
| `set_timescale(x)` / `timescale()` | global speed (0 = pause, 0.5 = slow-mo) |
| `get(name)` / `set(name, value)` | shared host globals (in memory) |
| `spawn(prefabPath, x, y)` | instantiate a `.okayprefab` at a position |
| `destroy()` | destroy this script's own GameObject |
| `activate(name)` / `deactivate(name)` | show/hide another object by name |
| `exists(name)` / `is_active(name)` | query another object by name |
| `set_parent(name)` / `detach()` / `has_parent()` | parent this object under another / unparent / query (pickups, mounts) |
| `obj_x(name)` / `obj_y(name)` / `obj_z(name)` | read another object's world position |
| `dist_to(name)` | 2D distance from this object to a named object (enemy AI, doors) |
| `dist3_to(name)` | full 3D distance to a named object |
| `move_toward3(name, speed)` | move toward a named object in 3D by speed*dt (chase/homing) |
| `look_at3(name)` | rotate forward (+Z) to face a named object in 3D |
| `spawn3(prefab, x, y, z)` | instantiate a prefab at a 3D position |
| `set_scale3(x, y, z)` / `set_scale(s)` | set per-axis or uniform scale |
| `scale_x()` / `scale_y()` / `scale_z()` | read this object's scale |
| `set_rot3(x, y, z)` | set absolute 3D euler rotation (degrees) |
| `set_mesh(name)` | swap this object's MeshRenderer primitive at runtime |
| `npc_goto(name, x, y, z)` | order a named NPC Controller to walk to a world point (pathfinds if enabled; broadcasts `npc_arrived`) — `""` targets a sibling NPC Controller |
| `npc_stop(name)` | cancel an `npc_goto` order |
| `npc_busy(name)` | true while the NPC is still walking to its ordered point |
| `npc_state(name)` | the NPC's AI state name: `Idle`/`Wander`/`Patrol`/`Follow`/`Flee`/`Chase`/`Search`/`Return` |
| `nav_move_to(x, y, z)` | send this object's Nav Mesh Agent walking to a world point over the baked navmesh (`nav_move_to("Name", x, y, z)` for another object); see [navigation.md](navigation.md) |
| `nav_stop()` | stop the Nav Mesh Agent where it is |
| `nav_arrived()` | 1 once the last `nav_move_to` point was reached |
| `nav_remaining()` | path length left to the destination (0 when idle) |
| `spawner_start(name)` / `spawner_stop(name)` | run / pause a Spawner component's waves (`""` targets a sibling Spawner) |
| `spawner_alive(name)` | how many objects that Spawner created are still alive (wave HUDs) |
| `build_enter()` / `build_exit()` / `build_toggle()` / `build_active()` | turn the player's in-game build mode on / off, or ask whether it's on (see [build_mode.md](build_mode.md)) |
| `build_link_mode(on)` | switch build mode's wiring (link) mode on / off; no argument toggles |
| `build_palette_clear()` / `build_palette_add(name, prefab, icon)` | set what players can place; `add` returns the new slot (0-based) |
| `build_palette_select(slot)` / `build_selected()` | pick / read the selected hotbar slot |
| `build_save(name)` / `build_load(name)` / `build_exists(name)` | save / load / check a creation (everything players placed, with its wiring) in the save folder |
| `build_clear()` / `build_count()` | remove everything players placed / how many objects they placed |
| `build_undo()` / `build_redo()` | undo / redo the player's build actions |
| `logic_press(name)` / `logic_state(name)` | press a Logic Source button / read a source's output or a target's on-off state |
| `on_object_placed(name)` / `on_object_removed(name)` | events: a player placed / deleted an object (globals `build_object`, `build_id`) |
| `on_logic_signal(name)` | event: a wired Logic Target turned on or off (globals `signal_name`, `signal_on`) |
| `count_tag(tag)` | how many active objects have a tag (coins left, enemies alive) |
| `nearest_tag(tag)` | name of the nearest tagged object to this one ("" if none) — targeting |
| `screen_w()` / `screen_h()` | render-target size in pixels |
| `set_bg(r, g, b[, a])` | set the main camera's background/clear color (flash, fades) |
| `set_light(x, y, z)` | set the 3D directional light direction (day-night, mood) |
| `set_ambient(a)` / `ambient()` | set/read the 3D ambient (unlit floor) brightness 0..1 |
| `look_at(name)` | rotate (about Z) so local +X faces a named object (turrets, aiming) |
| `cam_x()` / `cam_y()` / `set_cam(x, y)` / `move_cam(dx, dy)` | read/move the main camera (follow, cutscenes) |
| `cam_zoom()` / `set_cam_zoom(z)` | read/set the main camera's orthographic size |
| `load_scene(path)` | load a `.okayscene` at end of frame (level change/restart) |
| `raycast_hit(ox, oy, dx, dy[, dist])` | true if a ray hits a collider |
| `raycast(ox, oy, dx, dy[, dist])` | cast a ray; returns the **name** of the object hit ("" = miss) |
| `ray_hit()` / `ray_object()` | did the last `raycast()` hit, and what it hit |
| `ray_x()` / `ray_y()` | last hit point; `ray_nx()`/`ray_ny()` surface normal; `ray_dist()` distance |
| `raycast3(ox,oy,oz, dx,dy,dz[, dist])` | 3D ray; returns hit name + `ray3_hit/object/x/y/z/nx/ny/nz/dist()` |
| `raycast_all3(ox,oy,oz, dx,dy,dz[, dist])` / `raycast_all(ox,oy,dx,dy[, dist])` | every object the ray passes through, nearest first (a list of names; `ray_all_count/object/dist/point/normal(i)`) |
| `sphere_cast3(o.., d.., r[, dist])` / `box_cast3(c.., half.., d..[, dist])` / `capsule_cast3(a.., b.., r, d..[, dist])` | thick casts: the first thing the shape touches (details via `ray3_*`) |
| `overlap_sphere3(c.., r)` / `overlap_box3(c.., half..)` / `overlap_capsule3(a.., b.., r)` | names of the objects inside a shape |
| `layer_mask("Default", "Enemy", 3)` | a layers value for the queries |
| `overlap(x, y)` | true if a collider contains the point |
| `set_gravity(x, y)` | set the scene's 2D gravity (0,0 for top-down) |
| `set_text(string)` | set this object's TextRenderer text |
| `set_color(r, g, b[, a])` | tint this object's Sprite/Text |
| `set_texture(path)` | set this object's SpriteRenderer image |
| `flip_x(bool)` / `flip_y(bool)` | mirror this object's sprite |
| `velocity_x()` / `velocity_y()` | read this object's Rigidbody2D velocity |
| `set_velocity(x, y)` / `set_vx(x)` / `set_vy(y)` | set its velocity (jump, dash, top-down move) |
| `add_force(x, y)` / `add_impulse(x, y)` | apply a continuous force / instant momentum |
| `set_image(path)` | set this object's UIImage texture (swap icons/states) |
| `set_interactable(bool)` | enable/disable this object's UIButton (grey out menu entries) |
| `set_progress(v)` | set this object's UIProgressBar fill (0..1) |
| `slider_value()` / `set_slider(v)` | read/set this object's UISlider value (fires on its own drag) |
| `toggle_on()` / `set_toggle(b)` | read/set this object's UIToggle checkbox state |
| `tab_value()` / `set_tab(i)` | read/select this object's UITabs tab (fires on_change) |
| `set_tab_badge(i, n)` / `set_tab_enabled(i, b)` | badge a tab (0 none, -1 dot, N number) / enable or disable it |
| `tile_resize(w,h)` `set_tile(x,y,id)` `get_tile(x,y)` `tile_w()` `tile_h()` | edit a sibling Tilemap |
| `emit(n)` | burst n particles from this object's ParticleSystem (explosions, dust) |
| `particles_on(bool)` / `particles_alive()` | start/stop continuous emission / read live count |
| `play_anim()` / `stop_anim()` | restart / pause this object's SpriteAnimator |
| `play_sound()` | play this object's AudioSource |
| `print(...)` | log values to the console |

### Persistent prefs (high scores, settings)
| Function | Effect |
| --- | --- |
| `prefs_set(key, value)` | store a number or string |
| `prefs_get(key, default)` | read a number |
| `prefs_get_str(key, default)` | read a string |
| `prefs_save(path)` / `prefs_load(path)` | write/read the prefs file |
| `set_volume(x)` / `volume()` / `mute(bool)` | global audio level (options menus) |

The standalone player auto-loads `game.okayprefs` on launch and saves it on
exit, so values set with `prefs_set` persist between play sessions.

### Save system

For richer save games — many files (slots/profiles), typed values, and
write-through to disk — use the `save`/`load` family. Each value keeps its type
(number, string, or `Vector3`), and the optional last argument names the file
(default `save.okaysave`).

| Function | Effect |
| --- | --- |
| `save(key, value[, file])` | store a number / string / Vector3 and write the file |
| `load(key[, default][, file])` | read it back; the default's type picks the return type |
| `save_has(key[, file])` | does the key exist? |
| `save_delete(key[, file])` | remove one key (and re-save) |
| `save_clear([file])` | wipe a file's keys |
| `save_exists([file])` | does the save file exist on disk? |
| `save_delete_file([file])` | delete the whole save file |

```cs
// Save a checkpoint, then restore it next run
save("coins", 120);
save("name", "Hero");
save("spawn", new Vector3(4, 0, 2), "slot1.okaysave");

int coins = load("coins", 0);
string who = load("name", "Player");
Vector3 at = load("spawn", new Vector3(0, 0, 0), "slot1.okaysave");
if (!save_exists("slot1.okaysave")) { /* first time playing */ }
```

**File names stay inside the game.** Every script call that names a file
(`save`/`load` and the rest of this family, `prefs_save`/`prefs_load`,
`save_game`/`load_game` slots, `data_*`, `build_save`/`build_load`,
`load_scene`) takes a name or a relative path such as `"slots/one.okaysave"`.
Absolute paths (`/…`, `C:\…`) and `..` are refused: the call returns false (or
the default you passed) and the Console says why. In an exported game, saves
and prefs go into the game's own save folder; files a game saved beside itself
before that still load.

### Rendering: post-processing, fog, weather, time of day

The scene's look (the Lighting panel's Post-processing, Sky, Fog and Weather
sections, see [rendering.md](rendering.md)) at runtime:

| Function | Effect |
| --- | --- |
| `post_set(name, value)` | one post-processing value: `"exposure"`, `"tonemap"` (0 none / 1 ACES / 2 filmic, or the name), `"bloom"`, `"bloom_threshold"`, `"bloom_intensity"`, `"bloom_radius"`, `"grading"`, `"temperature"`, `"tint"`, `"saturation"`, `"contrast"`, `"vignette"`, `"vignette_smoothness"`, `"fxaa"`, `"ssao"`, `"ssao_radius"`, `"ssao_intensity"`, `"dof"`, `"dof_focus"`, `"dof_range"` (0 / 1 for switches); false for an unknown name |
| `fog_set(on[, "#rrggbb"][, start, end][, mode[, density]])` | fog on/off, its colour, the linear start / end, and a mode (`"linear"`, `"exponential"`, `"height"`) with its density |
| `weather_set(preset[, intensity])` | `"none"`, `"rain"`, `"snow"` or `"storm"` at an intensity 0..1; cross-fades over the scene's fade time |
| `time_of_day([hours])` | the Day/Night Cycle's clock (0..24); with an argument sets it. -1 when the scene has none |

```cs
post_set("bloom", 1);
post_set("exposure", 1.2);
fog_set(true, "#c8d8e6", 20, 90);
weather_set("rain", 0.6);
if (time_of_day() > 20) weather_set("storm", 0.9);
```

### Math
`abs sin cos tan asin acos atan atan2 sqrt pow exp log floor ceil round sign
min max clamp lerp smoothstep len hypot wrap(v, lo, hi) ping_pong(t, len)
rand(lo, hi) randi(lo, hi) chance(p) move_toward(cur, target, maxStep)
dist(x1, y1, x2, y2) angle_to(x1, y1, x2, y2) pi() deg2rad(d) rad2deg(r)`

- `wrap(v, lo, hi)` keeps a value inside `[lo, hi)` (angles, looping indices).
- `ping_pong(t, len)` bounces `0..len..0` (patrols, bobbing, breathing scale).
- `smoothstep(a, b, t)` is an ease-in/out interpolation between `a` and `b`.
- `chance(p)` returns true with probability `p` (0..1).
- `angle_to(x1, y1, x2, y2)` returns the heading in **degrees** from p1 to p2.

### Arrays
`array() count(a) push(a, v) pop(a) contains(a, v) index_of(a, v) remove_at(a, i)
sum(a) min_of(a) max_of(a) reverse(a) sort_num(a) choose(a) shuffle(a)`
— plus literals `[...]`, indexing `a[i]`, and `a[i] = v`.

### Strings
`str_len(s) upper(s) lower(s) substr(s, start, n) char_at(s, i) str_find(s, sub)
str_contains(s, sub) starts_with(s, prefix) ends_with(s, suffix)
replace(s, find, repl) trim(s) repeat(s, n) to_num(s) to_str(x)
split(s, sep) join(arr, sep)` — plus `+` concatenation.

### Maps / dictionaries (string keys)
`map() map_set(m, k, v) map_get(m, k[, default]) map_has(m, k) map_remove(m, k)
map_keys(m) map_count(m)` — shared by reference, like arrays.

### Tweening (DOTween-style)
Smoothly animate this object over time. Every tween:
- takes an optional ease name and an optional **on-complete** function name as
  its last argument(s);
- **returns an id** you can use to set it up or control it afterwards;
- stops by itself when its object is destroyed.

| Function | Effect |
| --- | --- |
| `tween_move(x, y, dur[, ease][, "done"])` | move to (x, y); `ease` e.g. `"out_quad"` |
| `tween_move3(x, y, z, dur[, ease][, "done"])` | move in 3D |
| `tween_move_world(x, y, z, dur[, ease][, "done"])` | move in world space (ignores the parent) |
| `tween_move_by(dx, dy, dur[, ease][, "done"])` | move by a **relative** offset |
| `tween_scale(s, dur[, ease][, "done"])` | scale to uniform `s` |
| `tween_scale_xy(sx, sy, dur[, ease][, "done"])` | non-uniform scale to (sx, sy) |
| `tween_rotate(deg, dur[, ease][, "done"])` | spin `deg` degrees about Z (relative; 360 = a full turn) |
| `tween_rotate_to(deg, dur[, ease][, "done"])` | rotate to an **absolute** Z angle |
| `tween_rotate3(x, y, z, dur[, ease][, "done"])` | rotate to Euler angles (3D) |
| `tween_ui_move(x, y, dur[, ease][, "done"])` | move a **UI widget** (anchored position) |
| `tween_ui_size(w, h, dur[, ease][, "done"])` | resize a **UI widget** |
| `tween_color(r, g, b, dur[, ease][, "done"])` | colour of a sprite, mesh, UI image, panel, button or text |
| `tween_fade(a, dur[, ease][, "done"])` | fade alpha to `a` |
| `flash(r, g, b[, dur])` | snap to a colour and fade back (hit feedback) |
| `tween_jump(x, y, height, dur[, jumps][, "done"])` | hop to (x, y) in `jumps` arcs |
| `tween_path(dur, x1, y1, x2, y2, ...)` | move through waypoints |
| `tween_path_smooth(dur, x1, y1, ...)` | the same, curving smoothly through them |
| `tween_loop_move(x, y, dur[, ease])` | ping-pong forever between here and (x, y) |
| `tween_loop_scale(s, dur[, ease])` | ping-pong the scale forever (pulsing) |
| `tween_loop_rotate(dur[, dir])` | spin forever, a turn every `dur` (dir +1/-1) |
| `tween_punch_scale(amount, dur[, vib])` | punch the scale and settle back ("juice") |
| `tween_punch_pos(dx, dy, dur[, vib])` | punch the position and settle back |
| `tween_punch_rotate(deg, dur[, vib])` | wobble the angle and settle back |
| `tween_shake(intensity, dur[, vib])` | shake the position, fading out (impact / camera) |
| `tween_shake_rotate(deg, dur[, vib])` / `tween_shake_scale(amount, dur[, vib])` | shake the angle / scale |
| `tween_number(from, to, dur[, "prefix"])` | count this object's text number (score ticks) |
| `tween_text("Hello!", dur[, "done"])` | type this object's text out letter by letter |
| `tween_var("name", to, dur[, ease][, "done"])` | tween one of the script's own variables |
| `tween_call(seconds, "fn")` | call a function after a delay |

**Set up** a tween. Each of these returns the id, so they nest, e.g.
`tween_loops(tween_scale(1.2, 0.4), -1, "yoyo")`:

| Function | Effect |
| --- | --- |
| `tween_delay(id, s)` | wait before starting |
| `tween_ease(id, "out_back")` | change the ease |
| `tween_loops(id, n[, "restart" \| "yoyo" \| "incremental"])` | repeat (`-1` = forever) |
| `tween_speed(id, x)` | play faster or slower |
| `tween_unscaled(id)` | ignore the time scale (pause menus) |
| `tween_from(id)` | play from the end value back to the current one (intros) |
| `tween_relative(id)` | treat the end value as an offset |
| `tween_set_id(id, "name")` | name it, for the `_id` functions below |
| `tween_keep(id)` | keep it after it ends, so it can be restarted |
| `tween_on_complete(id, "fn")`, `tween_on_step(id, "fn")`, `tween_on_start(id, "fn")`, `tween_on_update(id, "fn")` | callbacks |

**Control** tweens:
- **One tween:** `tween_pause(id)`, `tween_play(id)`, `tween_toggle(id)`,
  `tween_restart(id)`, `tween_rewind(id)`, `tween_complete(id)`,
  `tween_backwards(id)`, `tween_goto(id, seconds)`, and `tween_kill(id[, complete])`.
  To check on one, use `tween_playing(id)`, `tween_active(id)` or
  `tween_progress(id)` (0 to 1).
- **This object's tweens:** `tween_kill_all()`, `tween_pause_all()`,
  `tween_play_all()` and `is_tweening()`.
- **By name, anywhere in the scene:** `tween_kill_id("name")`,
  `tween_pause_id("name")`, `tween_play_id("name")` and
  `tween_restart_id("name")`. These also start a **Tween Animation** component
  with that Id.

**Sequences** run tweens one after another, or together:

| Function | Effect |
| --- | --- |
| `seq_new()` | a new, empty sequence (returns its id; it plays by itself) |
| `seq_append(seq, id)` | play after everything already in it |
| `seq_join(seq, id)` | play together with the last appended tween |
| `seq_insert(seq, time, id)` | play at a time |
| `seq_interval(seq, s)` / `seq_prepend_interval(seq, s)` | a pause at the end / start |
| `seq_callback(seq, "fn")` / `seq_insert_callback(seq, time, "fn")` | call a function at that point |

A sequence's id works with every setup and control function above (loops,
delay, pause...).

**Eases** (any spelling works: `out_quad`, `OutQuad`, `ease_out_quad`):
- `linear`
- `in_` / `out_` / `in_out_` followed by `quad`, `cubic`, `quart`, `quint`,
  `sine`, `expo`, `circ`, `back`, `elastic` or `bounce`.

```c
function start() {
  tween_move(5, 0, 1.0, "out_quad", "arrived");          // calls arrived() when done
  tween_loops(tween_move_by(0, 0.3, 0.8, "in_out_sine"), -1, "yoyo");   // bob forever
  var intro = seq_new();
  seq_append(intro, tween_from(tween_scale(0, 0.4, "out_back")));
  seq_join(intro, tween_fade(1, 0.4));
  seq_interval(intro, 0.2);
  seq_callback(intro, "ready");
}
function arrived() { tween_punch_scale(0.3, 0.25); }    // pop on arrival
```

**Without code:** add a **Tween Animation** component (Add Component →
Animation). Pick what to animate (move, scale, rotate, fade, colour, UI position
or size, punch, shake, jump), plus the end value, duration, delay, ease and loops.
It plays when the scene starts, or with `tween_play_id("Id")`. **Preview**
plays it in the editor and puts the object back afterwards.

From C++, `okay::DOTween::Move / Scale / Rotate / PunchScale / ShakePosition /
Jump / Path / Fade / Text ...` return a tween you can chain:
`->SetEase(Ease::BackOut).SetLoops(-1, LoopType::Yoyo)`. Sequences come from
`scene.tweens().Sequence()`.

### Drag & drop
Add a **UI Draggable** (UI widget) or **Draggable** (world sprite) component in
the editor, and a **UI Drop Target** / **Drop Zone** to receivers. At runtime
the item follows the cursor; on release over a valid target these handlers fire:

| Handler | On |
| --- | --- |
| `on_drag_start()` / `on_drag()` | the dragged object, when a drag begins / each frame |
| `on_drop()` | the dragged object, when it lands on a valid target |
| `on_receive()` | the target, when an item lands on it |
| `on_hover_enter()` / `on_hover_exit()` | a target, as an item enters/leaves during a drag |

Read who was involved with `ui_drop_source()` / `ui_drop_target()` (UI) or
`drop_source()` / `drop_target()` (world items). Options like snap-into-slot,
accept-tag, grid snap and lock-axis are set in the inspector — no code needed for
a basic inventory.

### Scriptable Objects (Data Assets)
Reusable `.okaydata` files of named fields (item/enemy/level definitions,
config). Create with **Project → New Data Asset**.

| Function | Effect |
| --- | --- |
| `data_num(path, key[, default])` | read a numeric field |
| `data_str(path, key[, default])` | read a string field |
| `data_has(path, key)` | does the field exist |
| `data_set(path, key, value)` | set a field (in memory) |
| `data_save(path)` | write the asset back to disk |

```c
function start() {
  var hp = data_num("data/goblin.okaydata", "health", 10);
  set_text(data_str("data/goblin.okaydata", "name", "?") + " HP:" + hp);
}
```

## Plain-English names

Most builtins have a readable alias so you can usually guess the right word —
they all call the same function, so use whichever reads best to you:

| If you think… | …you can also write |
| --- | --- |
| `place_at(x, y)` | `set_pos` |
| `move_by(dx, dy)` | `move` / `translate` |
| `position_x()` / `position_y()` | `pos_x` / `pos_y` / `get_x` / `get_y` |
| `set_position_x(v)` | `set_x` |
| `turn(deg)` | `rotate` |
| `face("Target")` | `look_at` |
| `key_pressed("space")` | `key_down` / `Input.GetKeyDown` |
| `is_key_held("a")` | `key` / `Input.GetKey` |
| `input_horizontal()` / `input_vertical()` | `axis_x` / `axis_y` / `Input.GetAxis` |
| `mouse_pressed(0)` | `mouse_down` |
| `create_object("p", x, y)` | `spawn` / `instantiate` / `Instantiate` |
| `remove_object()` | `destroy` |
| `show_object("n")` / `hide_object("n")` | `activate` / `deactivate` |
| `object_exists("n")` | `exists` |
| `nearest_with_tag("enemy")` | `nearest_tag` |
| `count_with_tag("coin")` | `count_tag` |
| `distance_to("n")` | `dist_to` |
| `call_later(secs, "fn")` | `after` |
| `repeat_every(secs, "fn")` | `every` |
| `time_elapsed()` / `frame_time()` | `time` / `dt` / `Time.time` / `Time.deltaTime` |
| `set_tint(r,g,b)` | `set_color` |
| `set_sprite("img.png")` | `set_texture` |
| `set_background_color(r,g,b)` | `set_bg` |
| `emit_particles(n)` | `emit` |
| `play_sfx()` | `play_sound` |
| `apply_force(x, y)` / `apply_impulse(x, y)` | `add_force` / `add_impulse` |
| `is_overlapping(x, y)` | `overlap` |
| `cast_ray(ox,oy,dx,dy)` | `raycast` |
| `set_camera(x, y)` / `set_zoom(z)` | `set_cam` / `set_cam_zoom` |
| `save_value(k, v)` / `load_number(k, def)` / `load_text(k, def)` | `prefs_set` / `prefs_get` / `prefs_get_str` |
| `random_float(lo, hi)` / `random_int(lo, hi)` / `random_chance(p)` | `rand` / `randi` / `chance` |
| `log(...)` | `print` / `Debug.Log` |

## C# features

The typed OkayScript (scripts that start with `class Name : Behaviour { ... }`)
is checked before it runs: every name and type is resolved, and a mistake is
reported with its line and column instead of running wrongly. These C# features
work in it today. More arrive phase by phase; anything not listed here gives a
clear "isn't supported yet" error.

### Enums

Declare them at the top of the file or inside the class. Values count up from 0,
or from a value you give. An enum turns into text as its member's name.

```csharp
enum State { Idle, Walking, Running = 10, Flying }   // Flying is 11

class Hero : Behaviour {
    public enum Team { Red, Blue }
    public State state = State.Idle;
    const State Fastest = State.Flying;

    void Update() {
        if (state < State.Running) Debug.Log("slow: " + state);   // "slow: Walking"
        int code = (int)state;          // enum -> int needs a cast, both ways
        State back = (State)code;
    }
}
```

### switch

Works on `int`, `string` and enums (for a `float` or `bool`, use `if`). Case values must be fixed
(numbers, text, enum values or consts), and appear only once. As in C#, a
section can't run into the next one: end each with `break`, `return` or
`continue`. Several `case` labels can share one section.

```csharp
string Describe(Weapon w) {
    switch (w) {
        case Weapon.Sword:
        case Weapon.Axe: return "melee";
        case Weapon.Bow: return "ranged";
        default: return "unknown";
    }
}

void OnCommand(string cmd) {
    switch (cmd) {
        case "jump": Jump(); break;
        case "run": speed = 8; break;
        default: Debug.Log("unknown command " + cmd); break;
    }
}
```

### Arrays

`T[]` for any type, including enums, `Vector3`, engine objects and other arrays
(`int[][]`). Arrays are references, like in C#: copying one shares its items.
Items start at their type's default (0, false, empty text, a zero vector, null).

```csharp
int[] scores = { 5, 10, 15 };              // a list after `=` in a declaration
Vector3[] points = new Vector3[4];         // four zero vectors
float[] weights = new float[] { 1, 2.5f };
int[][] rows = new int[3][];               // three null rows
rows[0] = new int[] { 1, 2 };

scores[1] += 5;
points[2].x = 3;                           // changes the item in place
Debug.Log(scores.Length);                  // 3
```

Reading or writing outside the array (`scores[3]`), a negative length, or an
array of more than a million items stops the callback with an error naming the
line. `new[] { 1, 2 }` takes its type from the items (ints with floats make a
`float[]`).

### static fields and methods

A `static` field is one value shared by every object in the scene running the
script, such as a count of all the enemies or the next free id. Its starting
value is set once, by the first object, and it lasts one play session: loading a
level, or Stop then Play in the editor, starts it again. It isn't shown in the
Inspector, because it isn't any one object's setting. A `static` method is a helper that needs no object: it can use
static fields and other static methods, but not the object's own fields,
`gameObject`, `transform` or `GetComponent`. Use either directly or through the
class name.

```csharp
class Enemy : Behaviour {
    static int alive;
    static int nextId = 1;
    public int id;

    void Start() { alive++; id = nextId++; }
    void OnDestroy() { alive--; if (Enemy.alive == 0) Debug.Log("wave cleared"); }

    static float Remap(float v, float lo, float hi) { return (v - lo) / (hi - lo); }
}
```

Lifecycle methods (`Start`, `Update`, ...) can't be static, since the engine calls
them on each object, and a `const` is already shared, so it doesn't take
`static`.

### $"..." text

Put any value inside `{ }` in a `$"..."` string: numbers, text, enums (their
name), `bool`s, `Vector3`s, names of objects, whole expressions, even another
`$"..."`. Write `{{` and `}}` for the braces themselves. `{value:F2}` shows a
number with a fixed number of decimals, for timers and scores on a HUD.

```csharp
Debug.Log($"{gameObject.name} has {hp} HP ({state})");
timerText = $"Time {elapsed:F1}s";          // "Time 12.5s"
label = $"{(won ? "You win" : "Try again")}: {score} points";
```

### List and Dictionary

`List<T>` grows and shrinks; `Dictionary<K, V>` looks values up by key (an int,
string, bool, enum or object). Both are references, like arrays.

```csharp
List<GameObject> targets = new List<GameObject>();
targets.Add(enemy);                      // Add, Insert, Remove, RemoveAt, Clear
if (targets.Contains(enemy)) { }          // Contains, IndexOf, Count, Sort, Reverse, ToArray, AddRange
var scores = new List<int> { 5, 9, 2 };
scores.Sort();

var stock = new Dictionary<string, int>();
stock["potion"] = 3;                      // adds or replaces
stock["potion"] += 1;                     // the key must already be there to read it
if (stock.ContainsKey("sword")) { }       // Add, TryAdd, Remove, ContainsKey, ContainsValue, Clear
int arrows = stock.GetValueOrDefault("arrow", 0);
foreach (var kv in stock) Debug.Log($"{kv.Key}: {kv.Value}");   // in the order they were added
```

Reading a missing key, adding a key that's already there, an index outside a
list, or changing a list or dictionary inside a `foreach` over it stops the
callback with an error naming the line.

### Lambdas, Func and Action

A lambda is a small function written where it's used: `x => x * 2`,
`(a, b) => a - b`, `() => { ... }`. Its type comes from where it goes:
`Func<int, bool>` takes an int and gives a bool, and `Action<int>` takes an int
and gives nothing. Lambdas can be kept in variables and fields and called later.

```csharp
class Button { public Action onClick; }

class Menu : Behaviour {
    Func<float, float> half = x => x / 2;
    int clicks;

    void Start() {
        Button play = new Button();
        play.onClick = () => clicks++;
        play.onClick?.Invoke();          // nothing happens while it's null
        play.onClick();                  // same as Invoke()
    }
}
```

- A lambda uses the variables around it, not copies: changes on either side show
  on the other, even after the method that made it has returned. A variable
  declared inside a loop (including a `foreach` variable) is a new one each time
  round, so lambdas made in the loop each keep their own. A `for` loop's own
  counter is one variable for the whole loop.
- `var` can't hold a lambda (write the `Func` or `Action` type). A lambda can't
  use `break` or `continue` to reach a loop around it.
- A lambda made by a script that has since been destroyed stops the callback
  with an error if it's called.

### LINQ-style methods

Lists and arrays have the usual query methods. Each gives a new `List` (or a
single value) and leaves the original alone.

```csharp
List<Enemy> near = enemies.Where(e => e.distance < 10).OrderBy(e => e.distance).ToList();
Enemy closest = enemies.OrderBy(e => e.distance).FirstOrDefault();
int alive = enemies.Count(e => e.health > 0);
float total = enemies.Sum(e => e.damage);
```

- `Where`, `Select`, `OrderBy`, `OrderByDescending`, `ToList` (and `ToArray`
  for arrays).
- `First`, `FirstOrDefault`, `Last`, `LastOrDefault`, `Any`, `All` and `Count`,
  with or without a condition.
- `Sum`, `Min`, `Max` and `Average`, of the numbers or of what a lambda picks.
- On a `List` only: `Find`, `FindAll`, `FindIndex`, `Exists`, `RemoveAll`,
  `ForEach`, and `Sort((a, b) => a.score - b.score)`.
- `First`, `Last`, `Min`, `Max` and `Average` of an empty list stop the callback
  with an error (use the `OrDefault` forms when it may be empty). An int `Sum`
  too big for an int is an error, not a wrong number.

### do / while

`do { ... } while (condition);` runs the body once before it checks.

### ?? and ?.

For objects and arrays, which can be null. A destroyed object counts as null
here, just as it does in `== null`.

- `a ?? b` is `a`, or `b` when `a` is null: a fallback target.
- `x ??= value` sets `x` only while it's null, and only then works out `value`:
  look something up once and keep it.
- `a?.b` gives null (and skips the rest of the line's chain) when `a` is null.
  The result has to be something that can be null, or a call whose result isn't
  used; for a number or vector, check `a != null` first.

```csharp
GameObject target;
void Update() {
    target ??= GameObject.FindWithTag("Player");             // found once, again if it's destroyed
    target?.transform.LookAt(transform.position);           // nothing happens while there's none
    GameObject focus = target ?? gameObject;
}
```

### Classes and new

A script has one `Behaviour` class (the component on the object) and can have
plain classes next to it for its own data: an inventory item, a path node, a
wave of enemies. Make them with `new`; they're references like arrays, so
copies share the object, and an unset one is `null`.

```csharp
class Item {
    public string name;
    public int count = 1;
    public Item(string name) { this.name = name; }
    public bool Use() { count--; return count <= 0; }
}

class Inventory : Behaviour {
    Item[] slots = new Item[4];

    void Start() { slots[0] = new Item("Potion"); }
    public void UseFirst() {
        if (slots[0] != null && slots[0].Use()) slots[0] = null;
    }
}
```

- Fields and methods are private to their class unless marked `public`.
- A constructor is a method named after its class; a class can have several
  that take different numbers of values. A class without one is made with
  `new Item()`, all fields at their starting values.
- `this` is the object itself (`this.name = name` sets the field, not the
  parameter).
- Plain classes aren't on an object, so `gameObject`, `transform` and
  `GetComponent` only exist in the Behaviour class. They can't be `static` or
  `const` yet, and classes can't extend each other yet.
- Objects are freed as soon as nothing refers to them, except objects that
  refer to each other in a loop (`a.next = b; b.next = a;`). Those stay in
  memory until the game quits, so set one of the links to `null` when you're
  done with them.

### foreach

Loops over an array's items, with `var` or a type. The loop variable can't be
assigned (as in C#); `break` and `continue` work as in any loop.

```csharp
int total = 0;
foreach (var s in scores) total += s;
foreach (float w in new int[] { 1, 2 }) Debug.Log(w / 2);   // ints convert to float
```

## Other backends

OkayScript is the default, but the engine also supports **Lua** and **C#**
backends when built with `-DOKAY_WITH_LUA=ON` / `-DOKAY_WITH_CSHARP=ON`, and a
node-based **visual scripting** graph (see [visual_scripting.md](visual_scripting.md)).
Scripts can be edited in the in-engine Script Editor panel or in an external IDE.
