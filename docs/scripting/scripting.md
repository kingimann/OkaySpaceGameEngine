# OkayScript

> **What this is:** The OkayScript language guide and its function reference, in one page.
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [OkayScript](#okayscript-1) (was `docs/scripting.md`)
- [OkayScript function reference](#okayscript-function-reference) (was `docs/scripting_reference.md`)

## OkayScript

*Was `docs/scripting.md` until 2026-11-20.*

> New here? This page is the language guide. The per-area function list
> (networking, Steam, JSON, collections, timers, physics events) is in
> [scripting_reference.md](scripting.md#okayscript-function-reference). All docs: [README.md](../README.md).

OkayScript is the engine's built-in scripting language — a small, dependency-free
language that ships in every build (no external toolchain required). It's its own
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

### C#-style syntax

Scripts read like C#, and every piece of syntax does something. A script's
fields and functions **are** the component: there is no class or base type to
write. A new script starts empty, with just the two
functions you almost always want:

```csharp
// Runs once, before the first frame.
void Start()
{
}

// Runs every frame.
void Update()
{
}
```

Fill them in as you go. A fuller example, with an Inspector field, movement
from the keyboard and a coroutine:

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

#### What each part does

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

#### Functions the engine calls

| Function | When |
|---|---|
| `Awake()`, `OnEnable()`, `Start()` | Once each, in that order, when the scene starts. |
| `Update()` / `LateUpdate()` | Every frame (`Time.deltaTime` is the seconds since the last). |
| `FixedUpdate()` | 50 times a second, however fast the game draws. |
| `OnDisable()`, `OnDestroy()` | When the object is destroyed. |
| `OnCollisionEnter/Stay/Exit(string other)`, `OnTriggerEnter/Stay/Exit(string other)` | Physics contacts, 2D and 3D. `other` is a reference to the exact object that was touched, even when several copies share its name (`other.name`, `other.CompareTag(..)`, `other.transform.position`, `other.GetComponent<Rigidbody>()`, `Destroy(other)`). |
| `OnMouseDown/Up/Enter/Exit/Over()`, `OnClick()` | Pointer events on the object. |

#### Syntax that does nothing

Pasted C# still runs, but these have no effect, and the Script Editor marks
each one with a warning saying what to delete:

- `using X;` and `namespace X { }`: everything is already available.
- A `class X : Base { }` wrapper: the fields and functions inside are the component either way.
- `virtual`, `override`, `abstract`, `sealed`, `protected`, `internal`, `partial`, and `private` (already the default).
- Number suffixes like `5f`: every number is already a decimal.
- Methods the engine never calls (`OnGUI`, `OnApplicationQuit`, `OnBecameVisible`, ...).

#### Also available

| Area | Supported |
|---|---|
| Collections | `List<T>`, `Dictionary<K,V>`, arrays `int[]` (`new int[3]`, `new[] {1, 2}`, `{1, 2, 3}`), `int a = 1, b = 2;` |
| Expressions | casts `(int)x`, `$"interpolated {strings}"`, `?:`, `%=`, bitwise `& \| ^`, `++x`, Vector3 maths (`v + w`, `v * 2`, `-v`, `v / 2`), `(a - b).normalized` |
| Methods on values | strings: `ToUpper`, `ToLower`, `Contains`, `StartsWith`, `EndsWith`, `IndexOf`, `Substring`, `Replace`, `Split`, `Trim`, `PadLeft`...; numbers: `ToString("F2")`; lists: `Add`, `Remove`, `RemoveAt`, `Insert`, `Contains`, `IndexOf`, `Clear`, `Sort`, `Reverse`, `Count`; dictionaries: `Add`, `ContainsKey`, `Remove`, `Keys`, `Values`, `GetValueOrDefault`, `foreach (var kv in d)` |
| Lambdas | `x => x * 2`, `(a, b) => { ... }`, and defaults `(x = 3) => x + 1`, with `ForEach`, `Where`, `Select`, `Find`, `FindIndex`, `Any`, `All`, `Count`, `RemoveAll`, `OrderBy`, `Sum`, `Max`, `Sort((a, b) => a - b)` |
| Coroutines | `StartCoroutine(Routine(args))`, `yield return null`, `yield return new WaitForSeconds(t)` (`WaitForSecondsRealtime` ignores time scale), `yield return new WaitUntil(() => cond)`, `yield return new WaitWhile(() => cond)`, `yield return StartCoroutine(Other())`, `yield break`, `StopCoroutine`, `StopAllCoroutines` |
| Components | `GetComponent<T>()` returns a usable reference (`other.GetComponent<T>()` is the other object's component): `Rigidbody` (see below), `Rigidbody2D` (`velocity`, `angularVelocity`, `mass`, `drag`, `gravityScale`, `isKinematic`, `bodyType`, `AddForce(v [, ForceMode2D.Impulse])`, `AddTorque`, `AddImpulse`, `AddRelativeForce`, `AddForceAtPosition`, `AddExplosionForce`, `MovePosition`, `MoveRotation`), `SpriteRenderer`/`Renderer` (`color`, `enabled`, `flipX`), `Text` (`text`, `color`), `Animator` (`SetTrigger`, `SetBool`, `SetFloat`, `Play`), `AudioSource` (`Play`, `Stop`, `volume`) |
| Engine API | `transform.Translate/Rotate/LookAt`, `eulerAngles`/`localEulerAngles`, `Vector3.*` (constants, `Distance`, `Lerp`, `MoveTowards`, `Cross`, `Dot`...), `Mathf.*` (incl. `RoundToInt`, `FloorToInt`), `Random.Range`, `Input.GetKey/GetKeyDown/GetAxis/GetAxisRaw/GetMouseButton`, `Time.time/deltaTime/frameCount`, `Time.sessionTime/sessionFrameCount/unscaledDeltaTime/fps/paused`, `Time.HitStop/SlowMotion/RampTimeScale`, `Debug.Log/LogWarning/LogError`, `GameObject.Find/FindWithTag/FindGameObjectsWithTag` (active objects only), `GameObject.FindIncludingInactive`, `Instantiate(prefab, pos, rot)`, `Destroy(obj, delay)`, `Invoke`, `InvokeRepeating`, `CancelInvoke`, `string.Join/IsNullOrEmpty`, `int.Parse` |

#### Object references

`other` in a collision or trigger handler, `GameObject.Find(..)`,
`FindWithTag(..)`, each entry of `FindGameObjectsWithTag(..)`, `gameObject`,
and what `Instantiate(..)` / `spawn(..)` return are **references**: they carry
the object's unique instance id, so they always mean that one object, even when
spawned copies all have the same name ("Enemy", "Enemy", "Enemy"). A bullet
that runs `Destroy(other)` destroys the enemy it hit, and a loop over
`FindGameObjectsWithTag("Foe")` reaches every enemy.

A reference still behaves like the object's name where text is expected:
`print(other)` and `$"{other}"` show the name, `other == "Target"` compares the
name, and `string other` parameters keep working. Two references are equal only
when they are the same object, and a destroyed object's reference equals `null`.

What you can do through a reference: read `.name`, `.tag`, `.activeSelf`,
`.transform.position` / `localPosition` / `localScale` / `eulerAngles` /
`forward`; write `.transform.position` (and `.x` / `.y` / `.z`),
`localPosition`, `localScale`, `eulerAngles`; call `.CompareTag(..)`,
`.SetActive(..)`, `.GetComponent<T>()` and `.transform.Translate / Rotate /
LookAt(..)`; and pass it to `Destroy`, `send_event` and most builtins that take
an object name. (A few builtins, such as the NPC, Spawner, logic and UI-frame
helpers, still look the name up and so use the first object with that name.)

A plain name in quotes (`Destroy("Enemy")`) still means the **first** object
with that name, exactly as before. `GameObject.Find`, `FindWithTag` and
`FindGameObjectsWithTag` skip switched-off (inactive) objects, as the API docs
say; `GameObject.FindIncludingInactive("Door")` finds a hidden one.

#### Rigidbody (3D) from a script

`Rigidbody rb = GetComponent<Rigidbody>();` (or `other.GetComponent<Rigidbody>()`)
gives the body. Properties: `velocity`, `angularVelocity` (radians per second),
`mass` (more than 0), `drag`, `angularDrag`, `gravityScale`, `useGravity`
(off sets `gravityScale` to 0), `isKinematic`, `freezeRotation`, `bounciness`,
`friction`, `maxFallSpeed`, `position`, `isSleeping`. Methods:
`AddForce(v [, ForceMode.Force / Acceleration / Impulse / VelocityChange])`,
`AddRelativeForce`, `AddImpulse`, `AddTorque(v [, mode])`, `AddRelativeTorque`,
`AddForceAtPosition(force, point [, mode])`, `AddExplosionForce(force, center,
radius [, upwards])`, `MovePosition` (moves the transform directly, it does not
sweep through physics), `MoveRotation`, `WakeUp`, `Sleep`, `IsSleeping`.
Torque in `Impulse` mode is spread over one physics step. A method the body
doesn't have is an error; a property it doesn't have is ignored with a warning in
the Console (once).

Not supported: your own classes as types
you `new`, generics you declare yourself, `ref`/`out` parameters, LINQ query
syntax, and properties with bodies.

In the Script Editor, **Insert ▸ C# snippets** adds fields, handlers, coroutines
and collections, and **Help ▸ Cheat Sheet** shows the short version.

### Compatibility: C-style syntax also parses

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
  `Time.timeScale` / `Time.unscaledDeltaTime` / `Time.sessionTime` /
  `Time.sessionFrameCount` / `Time.fps` / `Time.paused`, `gameObject.name` / `.activeSelf` / `.tag`, `Screen.width/height`,
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
  checks). Touch gestures are `gesture("tap")`, `gesture("swipe up")`,
  `pinch_scale()`, `twist_degrees()` and `is_touch_screen()` (see `docs/engine/input.md`).
- **Generic calls** `GetComponent<Rigidbody2D>()` (truthy if present) and
  `AddComponent<Rigidbody2D>()` (adds SpriteRenderer / Rigidbody2D / TextRenderer
  / BoxCollider2D at runtime).
- **`Quaternion.Euler(x, y, z)`** / `Quaternion.identity` — assign to
  `transform.rotation` (the Z angle drives 2D rotation).
- **C# attributes** like `[SerializeField]` and `[Header("Stats")]` are accepted
  (and ignored) before fields and methods.
- **Event handlers**, PascalCase form: `OnCollisionEnter()`, `OnTriggerEnter()`,
  `OnClick()`, `OnValueChanged()` (alongside the classic `on_collision` etc).

### Language

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

#### Event handlers
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
| `on_spline_end(which)` | A Spline Follower on the object reaches an end of its curve (`which` is `"end"` or `"start"`) |

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

#### Physics queries: layers, triggers, casts

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

### Built-in functions

#### Transform
| Function | Effect |
| --- | --- |
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `set_x(x)` / `set_y(y)` | Set one axis |
| `pos_x()` / `pos_y()` | Read local position |
| `rotate(deg)` | Rotate about Z by degrees |

#### Movement one-liners (dt-scaled — never write `* dt`)
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

#### Input
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

#### Time & state
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
| `session_time()` / `session_unscaled_time()` / `session_frame_count()` | game seconds / real seconds / frames since this Play began (`time()` and `frame_count()` keep counting across Plays; these start from 0 each time) |
| `unscaled_dt()` | real seconds of the last frame: not slowed by `set_timescale`, a hit stop or a pause (animate a menu with it) |
| `is_paused()` | 1 while the game is paused (a menu, a cutscene, the pause button) |
| `push_pause(reason)` / `pop_pause(reason)` / `is_paused_by(reason)` | pause with a reason of your own (`"dialogue"`, `"shop"`): the game stays paused until every reason is let go, so two scripts that pause never resume each other (`pause_game(bool)` is the one shared "manual" reason) |
| `fps()` | real frames per second over the last half second (a pause or slow motion does not change it, a stall shows) |
| `hitch_count()` | frames that took over a tenth of a second since this Play began |
| `hit_stop(seconds [, scale])` | a freeze frame (scale 0, the default) or a brief slow-down for an impact; real time, ends by itself, composes with `set_timescale` |
| `slow_motion(scale, seconds)` | bullet time: `slow_motion(0.2, 2)` runs at a fifth speed for two real seconds, then back to normal |
| `ramp_timescale(target, seconds)` | ease the time scale to `target` over `seconds` real seconds, in a straight line |
| `get(name)` / `set(name, value)` | shared host globals (in memory) |
| `spawn(prefabPath, x, y)` | instantiate a `.okayprefab` at a position |
| `destroy()` | destroy this script's own GameObject |
| `activate(name)` / `deactivate(name)` | show/hide another object by name |
| `exists(name)` / `is_active(name)` | query another object by name |
| `set_parent(name)` / `detach()` / `has_parent()` | parent this object under another / unparent / query (pickups, mounts) |
| `child_count(name)` / `get_child(name, i)` / `parent_of(name)` | children of a named object (child name by index) / its parent's name, `""` if none |
| `hurt(name, n)` / `heal(name, n)` / `add_xp(name, n)` | damage / heal / XP on a named object's `Stats` or `SurvivalStats` (fires `on_death`, `on_level_up`) |
| `get_hp(name)` / `get_max_hp(name)` / `is_dead(name)` | read health / death state of a named object |
| `set_light_intensity(name, v)` / `set_light_range(name, v)` / `set_light_angle(name, deg)` / `set_light_color(name, r, g, b)` | tune a named object's Light component |
| `obj_x(name)` / `obj_y(name)` / `obj_z(name)` | read another object's world position |
| `dist_to(name)` | 2D distance from this object to a named object (enemy AI, doors) |
| `dist3_to(name)` | full 3D distance to a named object |
| `move_toward3(name, speed)` | move toward a named object in 3D by speed*dt (chase/homing) |
| `look_at3(name)` | rotate forward (+Z) to face a named object in 3D |
| `spawn3(prefab, x, y, z)` | instantiate a prefab at a 3D position |
| `pool_get(prefab, x, y [, z])` | like `spawn` / `spawn3`, from the scene's pool for that prefab: an idle object is reused (reset, its scripts start again), else one is made. Returns true on success |
| `pool_release()` | give this script's object back to its pool (it is destroyed when it did not come from one); a bullet's own script ends with it |
| `pool_prewarm(prefab, n)` / `pool_idle(prefab)` | make `n` idle objects ahead of time (returns how many are idle) / how many are idle |
| `set_scale3(x, y, z)` / `set_scale(s)` | set per-axis or uniform scale |
| `scale_x()` / `scale_y()` / `scale_z()` | read this object's scale |
| `set_rot3(x, y, z)` | set absolute 3D euler rotation (degrees) |
| `set_mesh(name)` | swap this object's MeshRenderer primitive at runtime |
| `npc_goto(name, x, y, z)` | order a named NPC Controller to walk to a world point (pathfinds if enabled; broadcasts `npc_arrived`) — `""` targets a sibling NPC Controller |
| `npc_stop(name)` | cancel an `npc_goto` order |
| `npc_busy(name)` | true while the NPC is still walking to its ordered point |
| `npc_state(name)` | the NPC's AI state name: `Idle`/`Wander`/`Patrol`/`Follow`/`Flee`/`Chase`/`Search`/`Return` |
| `nav_move_to(x, y, z)` | send this object's Nav Mesh Agent walking to a world point over the baked navmesh (`nav_move_to("Name", x, y, z)` for another object); see [navigation.md](../gameplay/navigation.md) |
| `nav_stop()` | stop the Nav Mesh Agent where it is |
| `nav_arrived()` | 1 once the last `nav_move_to` point was reached |
| `nav_remaining()` | path length left to the destination (0 when idle) |
| `grid_move_to(x, y)` | walk this object's Grid Path Agent to the cell that contains that world point (`grid_move_to("Name", x, y)` for another object); see [navigation.md](../gameplay/navigation.md) |
| `grid_move_cell(x, y)` | the same, to a tile cell instead of a world point |
| `grid_stop()` | stop the grid walk and drop the path |
| `grid_arrived()` | 1 once the last cell was reached |
| `grid_set_tile_cost(id, cost)` | price a tile number on this object's Grid Movement Costs (0 blocks it; adds the component if it is missing) |
| `grid_set_cell_cost(x, y, cost)` | price one cell the same way |
| `grid_block(x, y)` | block that cell with this object's Grid Obstacle (adds one if it is missing). Agents whose path used it plan again |
| `grid_clear()` | stop that obstacle blocking cells |
| `nav_path_exists(x, y, z)` | 1 when a walkable route leads from this object to the point (`nav_path_exists(fx, fy, fz, tx, ty, tz)` between two points); needs no agent |
| `nav_path_length(x, y, z)` | length of that route, -1 when there is none (also takes the six-number form) |
| `nav_point_walkable(x, y, z[, maxDist])` | 1 when the baked navmesh has ground within `maxDist` (default 1) of the point |
| `nav_nearest_point(x, y, z[, maxDist])` | the closest walkable point on the baked navmesh as a vector (`maxDist` default 50); the point itself when none is in range |
| `bt_set(key, value)` | write a value into this object's Behavior Tree blackboard (`bt_set("Name", key, value)` for another object); see [behavior_trees.md](../gameplay/behavior_trees.md) |
| `bt_get(key)` | read a blackboard value (a number, bool, string, vector or object) |
| `bt_run("Assets/Guard.okaybt")` | load a behavior tree on this object's Behavior Tree Runner (added when missing) and start it; `bt_run()` starts the current tree over |
| `bt_stop()` | stop the tree; running tasks are aborted and the NPC stops |
| `spline_point(name, t)` | the world point a fraction `t` (0..1) of the way along a named object's Spline, by length (`spline_point(t)` for this object's); see [splines.md](../engine/splines.md) |
| `spline_length(name)` | that Spline's length in world units |
| `follower_set_speed(speed)` | set this object's Spline Follower speed (`follower_set_speed("Name", speed)` for another object) |
| `line_set_points(points)` | replace this object's Line Renderer points with an array of vectors, or several vectors (`line_set_points("Name", ...)`) |
| `trail_clear()` | remove this object's Trail Renderer trail at once (`trail_clear("Name")`) |
| `spawner_start(name)` / `spawner_stop(name)` | run / pause a Spawner component's waves (`""` targets a sibling Spawner) |
| `spawner_alive(name)` | how many objects that Spawner created are still alive (wave HUDs) |
| `build_enter()` / `build_exit()` / `build_toggle()` / `build_active()` | turn the player's in-game build mode on / off, or ask whether it's on (see [build_mode.md](../gameplay/build_mode.md)) |
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
| `camera_follow("name"[, smoothing])` | the main camera eases after that object (adds a Camera Follow if needed; `""` stops); higher smoothing is snappier, 0 snaps |
| `load_scene(path)` | load a `.okayscene` at end of frame (level change/restart) |
| `raycast_hit(ox, oy, dx, dy[, dist])` | true if a ray hits a collider |
| `raycast(ox, oy, dx, dy[, dist])` | cast a ray; returns the **name** of the object hit ("" = miss) |
| `ray_hit()` / `ray_object()` | did the last `raycast()` hit, and what it hit |
| `ray_x()` / `ray_y()` | last hit point; `ray_nx()`/`ray_ny()` surface normal; `ray_dist()` distance |
| `raycast3(ox,oy,oz, dx,dy,dz[, dist])` | 3D ray; returns hit name + `ray3_hit/object/x/y/z/nx/ny/nz/dist()` |
| `raycast_all3(ox,oy,oz, dx,dy,dz[, dist])` / `raycast_all(ox,oy,dx,dy[, dist])` | every object the ray passes through, nearest first (a list of names; `ray_all_count/object/dist/point/normal(i)`) |
| `sphere_cast3(o.., d.., r[, dist])` / `box_cast3(c.., half.., d..[, dist])` / `capsule_cast3(a.., b.., r, d..[, dist])` | thick casts: the first thing the shape touches (details via `ray3_*`) |
| `box_cast(cx, cy, hx, hy, dx, dy[, dist [, layers [, ignore_triggers [, angle]]]])` / `capsule_cast(ax, ay, bx, by, r, dx, dy[, dist])` | flat casts: the first thing a box or capsule touches (details via `ray_*`; angle is degrees) |
| `overlap_box(cx, cy, hx, hy)` / `overlap_capsule(ax, ay, bx, by, r)` | names of the objects inside a flat box or capsule |
| `overlap_sphere3(c.., r)` / `overlap_box3(c.., half..)` / `overlap_capsule3(a.., b.., r)` | names of the objects inside a shape |
| `layer_mask("Default", "Enemy", 3)` | a layers value for the queries |
| `overlap(x, y)` | true if a collider contains the point |
| `set_gravity(x, y)` | set the scene's 2D gravity (0,0 for top-down) |
| `physics_steps()` / `set_physics_steps(n)` | physics steps per second (50 usual, 0 once per frame) |
| `physics_smooth()` / `set_physics_smooth(on)` | draw bodies between physics steps |
| `set_text(string)` | set this object's TextRenderer text |
| `set_color(r, g, b[, a])` | tint this object's Sprite/Text |
| `set_texture(path)` | set this object's SpriteRenderer image |
| `flip_x(bool)` / `flip_y(bool)` | mirror this object's sprite |
| `velocity_x()` / `velocity_y()` | read this object's Rigidbody2D velocity |
| `set_velocity(x, y)` / `set_vx(x)` / `set_vy(y)` | set its velocity (jump, dash, top-down move) |
| `add_force(x, y)` / `add_impulse(x, y)` | apply a continuous force / instant momentum |
| `add_explosion_force(cx, cy, force, radius[, up])` | shove this 2D body away from a point; the push fades to nothing at `radius` |
| `set_image(path)` | set this object's UIImage texture (swap icons/states) |
| `set_interactable(bool)` | enable/disable this object's UIButton (grey out menu entries) |
| `set_progress(v)` | set this object's UIProgressBar fill (0..1) |
| `slider_value()` / `set_slider(v)` | read/set this object's UISlider value (fires on its own drag) |
| `toggle_on()` / `set_toggle(b)` | read/set this object's UIToggle checkbox state |
| `tab_value()` / `set_tab(i)` | read/select this object's UITabs tab (fires on_change) |
| `set_tab_badge(i, n)` / `set_tab_enabled(i, b)` | badge a tab (0 none, -1 dot, N number) / enable or disable it |
| `tile_resize(w,h)` `set_tile(x,y,id)` `get_tile(x,y)` `tile_w()` `tile_h()` | edit a sibling Tilemap |
| `emit(n)` | burst n particles from this object's ParticleSystem (explosions, dust) |
| `particles_on(bool)` / `particles_alive()` | start/stop emission (turning a finished one-shot on plays it again) / read live count |
| `play_anim()` / `stop_anim()` | restart / pause this object's SpriteAnimator |
| `play_sound()` | play this object's AudioSource |
| `print(...)` | log values to the console |

#### Persistent prefs (high scores, settings)
| Function | Effect |
| --- | --- |
| `prefs_set(key, value)` | store a number or string |
| `prefs_get(key, default)` | read a number |
| `prefs_get_str(key, default)` | read a string |
| `prefs_save(path)` / `prefs_load(path)` | write/read the prefs file |
| `set_volume(x)` / `volume()` / `mute(bool)` | global audio level (options menus) |

The standalone player auto-loads `game.okayprefs` on launch and saves it on
exit, so values set with `prefs_set` persist between play sessions.

#### Save system

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

The same rule covers prefabs a script spawns: `spawn`, `spawn3`, `shoot_at` and
`spawn_wave`, the action-list ops `spawn`, `spawn3`, `spawn_at` and
`spawn_wave`, and the visual-script **Spawn** node. `spawn("enemy.okayprefab", 0, 5)`
and `spawn("prefabs/enemy.okayprefab", 0, 5)` work as before;
`spawn("../other/enemy.okayprefab", 0, 0)` or `spawn("C:\\x.okayprefab", 0, 0)`
creates nothing, returns false (0 for `spawn_wave`) and logs a
`refusing ...` line in the Console. Before this, those calls loaded a prefab
(and ran its scripts) from anywhere on disk.

#### Rendering: post-processing, fog, weather, time of day

The scene's look (the Lighting panel's Post-processing, Sky, Fog and Weather
sections, see [rendering.md](../rendering/rendering.md#rendering-post-processing-sky-fog-and-weather)) at runtime:

| Function | Effect |
| --- | --- |
| `post_set(name, value)` | one post-processing value: `"exposure"`, `"tonemap"` (0 none / 1 ACES / 2 filmic, or the name), `"bloom"`, `"bloom_threshold"`, `"bloom_intensity"`, `"bloom_radius"`, `"bloom_softness"`, `"grading"`, `"temperature"`, `"tint"`, `"saturation"`, `"contrast"`, `"vignette"`, `"vignette_smoothness"`, `"fxaa"`, `"ssao"`, `"ssao_radius"`, `"ssao_intensity"`, `"ssao_power"`, `"dof"`, `"dof_focus"`, `"dof_range"` (0 / 1 for switches); false for an unknown name |
| `fog_set(on[, "#rrggbb"][, start, end][, mode[, density]])` | fog on/off, its colour, the linear start / end, and a mode (`"linear"`, `"exponential"`, `"height"`) with its density |
| `weather_set(preset[, intensity])` | `"none"`, `"rain"`, `"snow"` or `"storm"` at an intensity 0..1; cross-fades over the scene's fade time |
| `time_of_day([hours])` | the Day/Night Cycle's clock (0..24); with an argument sets it. -1 when the scene has none |
| `wind_speed([x, y, z])` | the wind speed (metres per second) at a point, default where the camera is. The wind is the weather's plus every Wind Zone (`docs/gameplay/wind.md`) |
| `wind_x(x, y, z)`, `wind_y(...)`, `wind_z(...)` | the wind's parts along each axis at a point |
| `wind_set(strength[, yawDegrees])` | the scene's own wind: its speed and (0 = toward +Z, 90 = toward +X) its direction. Makes a "Wind" object if the scene has none |
| `wind_gust(amount[, perSecond])` | how much the scene's wind swells and drops (0 steady, 1 calm to double) and how many gusts come each second |

```cs
post_set("bloom", 1);
post_set("exposure", 1.2);
fog_set(true, "#c8d8e6", 20, 90);
weather_set("rain", 0.6);
if (time_of_day() > 20) weather_set("storm", 0.9);
```

#### Math
`abs sin cos tan asin acos atan atan2 sqrt pow exp log floor ceil round sign
min max clamp lerp smoothstep len hypot wrap(v, lo, hi) ping_pong(t, len)
rand(lo, hi) randi(lo, hi) chance(p) move_toward(cur, target, maxStep)
dist(x1, y1, x2, y2) angle_to(x1, y1, x2, y2) pi() deg2rad(d) rad2deg(r)`

- `wrap(v, lo, hi)` keeps a value inside `[lo, hi)` (angles, looping indices).
- `ping_pong(t, len)` bounces `0..len..0` (patrols, bobbing, breathing scale).
- `smoothstep(a, b, t)` is an ease-in/out interpolation between `a` and `b`.
- `chance(p)` returns true with probability `p` (0..1).
- `angle_to(x1, y1, x2, y2)` returns the heading in **degrees** from p1 to p2.

#### Arrays
`array() count(a) push(a, v) pop(a) contains(a, v) index_of(a, v) remove_at(a, i)
sum(a) min_of(a) max_of(a) reverse(a) sort_num(a) choose(a) shuffle(a)`
— plus literals `[...]`, indexing `a[i]`, and `a[i] = v`.

#### Strings
`str_len(s) upper(s) lower(s) substr(s, start, n) char_at(s, i) str_find(s, sub)
str_contains(s, sub) starts_with(s, prefix) ends_with(s, suffix)
replace(s, find, repl) trim(s) repeat(s, n) to_num(s) to_str(x)
split(s, sep) join(arr, sep)` — plus `+` concatenation.

#### Maps / dictionaries (string keys)
`map() map_set(m, k, v) map_get(m, k[, default]) map_has(m, k) map_remove(m, k)
map_keys(m) map_count(m)` — shared by reference, like arrays.

#### Tweening (DOTween-style)
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
| `tween_prop("Light.intensity", to, dur[, ease][, "done"])` | tween any property of this object by path (`Light.intensity`, `Camera.zoom`, `Transform.scale.y`, `SpriteRenderer.color.a`; see docs/engine/properties.md); a number for a float or integer property |

**Set up** a tween. Each of these returns the id, so they nest, e.g.
`tween_loops(tween_scale(1.2, 0.4), -1, "yoyo")`:

| Function | Effect |
| --- | --- |
| `tween_delay(id, s)` | wait before starting |
| `tween_ease(id, "out_back")` | change the ease; also takes a curve as text: `"cubic-bezier(0.4, 0, 0.2, 1)"`, `"steps(4)"`, `"ease-in-out"` (the ease argument of `tween_move` and the others takes the same) |
| `tween_loops(id, n[, "restart" \| "yoyo" \| "incremental"])` | repeat (`-1` = forever; a NaN plays once; a count past two billion is two billion, not "forever") |
| `tween_speed(id, x)` | play faster or slower |
| `tween_unscaled(id)` | ignore the time scale (pause menus) |
| `tween_clock(id, "ui")` | follow a named clock channel (`"game"`, `"ui"`, `"cinematic"`, or your own) instead of scene time; `""` goes back |
| `tween_tag(id, "menu")` | give it a group label, for the `_tag` functions below |
| `tween_speed_based(id)` | the duration you gave is a speed in units a second: it lasts distance / speed (moves, scales, colours; not rotations or punches) |
| `tween_decay(id, rate)` | how a punch or shake dies away: `0` is a straight fade, `6` is snappy |
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
  with that Id. `tween_rewind_id("name")` puts them back at the start.
- **By tag, anywhere in the scene:** `tween_kill_tag("tag"[, complete])`,
  `tween_pause_tag("tag")`, `tween_play_tag("tag")` and `tween_complete_tag("tag")`
  (the tag is set with `tween_tag(id, "tag")`; many tweens can share one).

**Sequences** run tweens one after another, or together:

| Function | Effect |
| --- | --- |
| `seq_new()` | a new, empty sequence (returns its id; it plays by itself) |
| `seq_append(seq, id)` | play after everything already in it |
| `seq_join(seq, id)` | play together with the last appended tween |
| `seq_insert(seq, time, id)` | play at a time |
| `seq_prepend(seq, id)` | play before everything already in it |
| `seq_interval(seq, s)` / `seq_prepend_interval(seq, s)` | a pause at the end / start |
| `seq_callback(seq, "fn")` / `seq_insert_callback(seq, time, "fn")` | call a function at that point |
| `seq_join_callback(seq, "fn")` | call a function when the last appended tween starts |

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

`ease_curve(t, "OutBack")` gives the eased value of `t` (0 to 1) for any ease the tween functions
take by name or as curve text (`"cubic-bezier(0.4, 0, 0.2, 1)"`, `"steps(4)"`, `"ease-in-out"`), for
animating something by hand: `x = lerp(a, b, ease_curve(t, "out_back"))`. Text it does not know is a straight
line, and a NaN `t` counts as 0 (the `ease_in` ... `ease_bounce` functions do the same).

**Without code:** add a **Tween Animation** component (Add Component →
Animation). Pick what to animate (move, scale, rotate, fade, colour, UI position
or size, punch, shake, jump), plus the end value, duration, delay, ease and loops.
It plays when the scene starts, or with `tween_play_id("Id")`. **Preview**
plays it in the editor and puts the object back afterwards. It also has the settings of the
tween functions above that have no field of their own in the first rows: a **clock** channel and a
curve as text for the ease, **speed based** (the duration is a speed), **decay** (how a punch or shake dies away) and a
**tag** (so `tween_kill_tag("tag")` and its friends reach it).

From C++, `okay::DOTween::Move / Scale / Rotate / PunchScale / ShakePosition /
Jump / Path / Fade / Text ...` return a tween you can chain:
`->SetEase(Ease::BackOut).SetLoops(-1, LoopType::Yoyo)`. Sequences come from
`scene.tweens().Sequence()`.

#### Drag & drop
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

#### Scriptable Objects (Data Assets)
Reusable `.okaydata` files of named fields (item/enemy/level definitions,
config). Create one by right-clicking in the Project panel: **Create → Data Asset**.

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

### Plain-English names

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
| `Time.HitStop(s, scale)` / `Time.SlowMotion(scale, s)` / `Time.RampTimeScale(target, s)` | `hit_stop` / `slow_motion` / `ramp_timescale` |
| `Time.PushPause(r)` / `Time.PopPause(r)` / `Time.IsPausedBy(r)` | `push_pause` / `pop_pause` / `is_paused_by` |
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

### C# features

The typed OkayScript (scripts that start with `class Name : Behaviour { ... }`)
is checked before it runs: every name and type is resolved, and a mistake is
reported with its line and column instead of running wrongly. These C# features
work in it today. More arrive phase by phase; anything not listed here gives a
clear "isn't supported yet" error.

#### Enums

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

#### switch

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

#### Arrays

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

#### static fields and methods

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

#### $"..." text

Put any value inside `{ }` in a `$"..."` string: numbers, text, enums (their
name), `bool`s, `Vector3`s, names of objects, whole expressions, even another
`$"..."`. Write `{{` and `}}` for the braces themselves. `{value:F2}` shows a
number with a fixed number of decimals, for timers and scores on a HUD.

```csharp
Debug.Log($"{gameObject.name} has {hp} HP ({state})");
timerText = $"Time {elapsed:F1}s";          // "Time 12.5s"
label = $"{(won ? "You win" : "Try again")}: {score} points";
```

#### List and Dictionary

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

#### Lambdas, Func and Action

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

#### Events

An `event` is a field that holds functions to call when something happens.
Other code adds a function with `+=` and takes it out with `-=`; outside its
class that's all it can do. The class itself calls it, usually with `?.Invoke`,
since an event nobody has added to is `null`.

```csharp
class Health {
    public event Action<int> Damaged;
    public event Action Died;
    public int hp = 100;
    public void Hit(int amount) {
        hp -= amount;
        Damaged?.Invoke(amount);
        if (hp <= 0) Died?.Invoke();
    }
}

class Player : Behaviour {
    Health health = new Health();
    void OnDied() { Debug.Log("Game over"); }
    void Start() {
        health.Died += OnDied;                         // a method's name works as a function
        health.Damaged += amount => Debug.Log($"-{amount}");
    }
    void OnDestroy() { health.Died -= OnDied; }       // takes out what += OnDied added
}
```

- The functions run in the order they were added. A `Func` event gives the
  last one's value.
- `+=` and `-=` work on any `Func` or `Action` variable too, not only events.
- `-=` takes out the same function: the same method of the same object, or the
  very same lambda value (a second `x => ...` written the same way is a
  different lambda, so keep it in a variable if you'll remove it).
- Adding or removing a handler while the event runs takes effect the next time
  it's called.
- A method's name also works where a lambda would: `targets.Where(IsAlive)`.
- Events and `Func` / `Action` fields don't show in the Inspector.

#### LINQ-style methods

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

#### do / while

`do { ... } while (condition);` runs the body once before it checks.

#### ?? and ?.

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

#### Classes and new

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
  `const` yet.
- Objects are freed as soon as nothing refers to them, except objects that
  refer to each other in a loop (`a.next = b; b.next = a;`). Those stay in
  memory until the game quits, so set one of the links to `null` when you're
  done with them.

#### Inheritance and interfaces

Plain classes can extend one class and implement any number of interfaces, so
different enemies can share code and still each do their own thing.

```csharp
interface IDamageable { void TakeDamage(int amount); }

abstract class Enemy : IDamageable {
    protected int hp;
    public Enemy(int hp) { this.hp = hp; }
    public virtual void TakeDamage(int amount) { hp -= amount; }
    public abstract int Reward();
}

class Boss : Enemy {
    int armor = 5;
    public Boss() : base(500) { }
    public override void TakeDamage(int amount) { base.TakeDamage(amount - armor); }
    public override int Reward() { return 100; }
}

class Crate : IDamageable {
    public void TakeDamage(int amount) { Debug.Log("crack"); }
}
```

```csharp
List<IDamageable> hit = new List<IDamageable> { new Boss(), new Crate() };
foreach (var t in hit) t.TakeDamage(10);      // each runs its own TakeDamage
if (hit[0] is Enemy) total += (hit[0] as Enemy).Reward();
Boss boss = (Boss)hit[0];                      // an error with its line if it isn't one
```

- A class names what it extends after `:`; the class first, then interfaces.
  An interface lists methods without bodies, and every class that implements
  it must have them, `public`.
- To change a method in a subclass, mark it `virtual` (or `abstract`, with no
  body) in the base class and `override` in the subclass, with the same
  parameters and result. A subclass can't reuse a base class's method or field
  name otherwise.
- `base.Method()` runs the base class's version; a constructor runs the base
  class's constructor first: `: base(...)`, or the one that takes nothing.
- `protected` members are for the class and the classes that extend it.
- An `abstract` class, or an interface, can't be made with `new`.
- `x is T` is true when `x` is a `T` (and not null); `x as T` gives it as a
  `T`, or null. A cast `(T)x` stops the callback with an error if `x` isn't one.
- The Behaviour class can't extend a class or implement interfaces yet.

#### Scripts using each other

`GetComponent<Health>()` finds another script on an object, the same way it
finds a `Rigidbody`. The script's class name is a type, and its `public`
fields and methods can be used through it.

```csharp
class Health : Behaviour {
    public int hp = 100;
    public void TakeDamage(int amount) { hp -= amount; if (hp <= 0) Destroy(gameObject); }
}

class Bullet : Behaviour {
    public void Hit(GameObject target) {
        Health h = target.GetComponent<Health>();   // null if target has no Health script
        if (h != null) h.TakeDamage(10);
    }
}
```

- Scripts can load in any order: a script that names one loaded later is
  checked again when the scene starts, and an error then names the type it
  couldn't find.
- Only values that mean the same to every script cross between them: numbers,
  text, `Vector3`, engine objects (`GameObject`, `Transform`, ...), other
  scripts, and coroutines (`StartCoroutine(h.Regen())`). A public field or
  method using one of a script's own types (an array, a list, a plain class
  or an enum) can't be used from another script yet.
- `h.gameObject` and `h.transform` are the object the script is on.
- A reference finds the script again each time it's used, so after the object
  is destroyed (or loses the script) using it is an error with its line.

#### Coroutines

A coroutine is a method that runs over several frames: it returns
`IEnumerator`, and each `yield return` pauses it until something happens.
`StartCoroutine` runs it right away up to its first `yield`; after that the
game picks it up once a frame, after `Update`.

```csharp
class Door : Behaviour {
    public float openTime = 1.5f;
    Coroutine closing;

    IEnumerator OpenThenClose() {
        transform.Translate(0, 3, 0);
        yield return new WaitForSeconds(openTime);   // a wait you can keep in a field and reuse
        transform.Translate(0, -3, 0);
    }
    IEnumerator Flash(int times) {
        for (int i = 0; i < times; i++) { Debug.Log("on"); yield return null; }   // null: the next frame
    }
    public void Open() { closing = StartCoroutine(OpenThenClose()); }
    public void Cancel() { if (closing != null) StopCoroutine(closing); }
}
```

What `yield return` can wait for:

| `yield return` | Goes on |
|---|---|
| `null` (or any other value) | the next frame |
| `new WaitForSeconds(t)` | once `t` seconds of game time have passed |
| `new WaitUntil(() => cond)` / `new WaitWhile(() => cond)` | the first frame `cond` is true / false |
| `new WaitForEndOfFrame()`, `new WaitForFixedUpdate()` | the next frame |
| `StartCoroutine(Other())` or a coroutine already running | when that coroutine ends |

- `yield break;` ends a coroutine early; it also ends at the end of its body.
  `return` isn't used in a coroutine.
- Calling `Blink()` on its own only makes the coroutine; `StartCoroutine(Blink())`
  runs it. `StopCoroutine(c)` stops one (at its next `yield`) and
  `StopAllCoroutines()` stops every coroutine the script started.
- A coroutine belongs to the script that started it, and stops with it.
  A plain class can have coroutine methods too: `StartCoroutine(enemy.Patrol())`.
- `yield` works inside loops, `if` and `switch`, not inside `try`, a lambda,
  or a method that doesn't return `IEnumerator`.
- An error in a coroutine stops that coroutine and is reported with its line;
  the script's other coroutines keep running.

#### try / catch and exceptions

`throw` stops what's running and goes to the nearest `catch` that takes that
kind of exception. Every script has `Exception`, with a `Message`; extend it
for your own kinds.

```csharp
class NotEnoughGold : Exception {
    public int missing;
    public NotEnoughGold(int missing) : base("Need " + missing + " more gold") { this.missing = missing; }
}

public void Buy(Item item) {
    try {
        wallet.Pay(item.price);              // throws NotEnoughGold
        inventory.Add(item);
    } catch (NotEnoughGold e) {
        ShowMessage(e.Message);
    } finally {
        shopOpen = true;                     // runs however the try ends
    }
}
```

- `catch (T e)` takes T or any class that extends it; `catch` and
  `catch (Exception e)` take everything. The first catch that fits runs, so
  a later one that an earlier one already covers is an error.
- Engine errors (a null object, dividing by zero, an index outside a list)
  can be caught as `Exception`, with the error as the `Message`. An endless
  loop or endless recursion can't: it always stops the callback.
- Inside a catch, `throw;` throws the same exception on.
- `finally` runs after the try (and catch) whether they finish, return, break
  or throw. It can't `return` itself, or `break` / `continue` out of itself.
- An exception nothing catches stops the callback and is reported with its
  class, message and the line that threw it.

#### foreach

Loops over an array's items, with `var` or a type. The loop variable can't be
assigned (as in C#); `break` and `continue` work as in any loop.

```csharp
int total = 0;
foreach (var s in scores) total += s;
foreach (float w in new int[] { 1, 2 }) Debug.Log(w / 2);   // ints convert to float
```

#### Properties and => bodies

A property looks like a field from outside and can run code when it is read or
set. An auto-property (`{ get; set; }`) is simply a field; a property with code
becomes the methods `get_Name()` / `set_Name(value)`, which reading and writing
the property call.

```csharp
public int Score { get; private set; }          // a field others read
public float Health { get; set; } = 10f;        // with a starting value
public bool Alive => hp > 0;                    // worked out when read
public float Speed {
    get { return speed; }
    set { speed = Mathf.Clamp(value, 0f, 5f); }
}
int Twice(int x) => x * 2;                       // a method with a => body
```

- `Speed = 9`, `Speed += 1`, `Speed++` call the setter (with the getter for the
  old value). Setting a property that only has a `get` is an error.
- `private set` / `init` on an auto-property are accepted but not enforced.
- Properties can't be `virtual` or in an interface yet, and don't show in the
  Inspector.

### C# scripts (.cs)

Language id `csharp` (also `cs`) and `.cs` files run on this same typed
language in **C# mode**, so the C# a Unity user writes runs unchanged. New
scripts are C# by default (the "Language" box in the Script Editor's New
Script dialog; the choice is remembered in `okay_settings.txt` as
`newscriptcsharp`). Existing scripts keep their language. This is our own C#,
written from the ECMA-334 C# specification: no Mono or .NET is used.

C# mode adds, on top of everything above:

- `using A.B;` lines (also `using static`, `using X = A.B;`) at the top of the
  file; they are read and skipped, since every engine type is available.
- `namespace A.B { ... }` and the file-scoped `namespace A.B;`.
- `MonoBehaviour` as the name of `Behaviour`.
- `private`, `readonly`, `sealed` and `internal` modifiers (accepted; `readonly`
  is not enforced, `internal` means `public`), `public const`.
- Unknown field attributes such as `[HideInInspector]` are a warning, not an
  error.
- `KeyCode.W`, `KeyCode.Space`, `KeyCode.LeftShift`, ... for `Input.GetKey`,
  `GetKeyDown`, `GetKeyUp` (each is the key's name: `KeyCode.W` is `"w"`).
- Collision messages, private or public: `OnTriggerEnter(Collider other)`,
  `OnTriggerStay`, `OnTriggerExit`, `OnCollisionEnter(Collision c)`,
  `OnCollisionStay`, `OnCollisionExit` and the `2D` ones (`Collider2D`,
  `Collision2D`). The argument is the other object: `other.name`, `other.tag`,
  `other.gameObject`, `other.transform`, `other.CompareTag("Coin")`. The
  parameter can also be a `GameObject`, a `string` (its name) or left out.
  `gameObject.CompareTag(tag)` works too.
- Text members: `s.Length`, `ToUpper()`, `ToLower()`, `Trim()`, `Contains`,
  `StartsWith`, `EndsWith`, `IndexOf`, `Substring(start[, length])`,
  `Replace`, `Split(",")` (gives a `string[]`); `x.ToString()` on numbers,
  `bool`, enums and `Vector3` (the same text as `"" + x`). A character literal
  such as `','` is a one-letter text (there is no `char` type). These work in
  `okayscript-next` too.
- `int.Parse("42")`, `float.Parse("2.5")` (text that isn't a number is an
  error with its line), `int.MaxValue` / `MinValue`, `float.MaxValue` /
  `MinValue`, `string.IsNullOrEmpty(s)`.
- Default parameter values: `void Hit(int amount = 1, string why = "bump")`,
  then `Hit()`, `Hit(3)`. Defaults must be fixed values (numbers, text,
  true/false, null, an enum value) at the end of the list; not on
  constructors yet. Works in `okayscript-next` too.
- `Random.Range(0, 3)` (a whole number, 3 left out), `Random.Range(-1f, 1f)`,
  `Random.value` and `Random.InitState(seed)` (these work in `okayscript-next`
  too: the binding table is shared).

```csharp
using UnityEngine;

namespace MyGame
{
    public class Mover : MonoBehaviour
    {
        [SerializeField] private float speed = 6f;

        void Update()
        {
            transform.position += new Vector3(1, 0, 0) * speed * Time.deltaTime;
            if (Input.GetKeyDown(KeyCode.Space)) Debug.Log("jump");
        }
    }
}
```

An old scene that named the removed Mono backend (`csharp` / `cs`) now runs
its script if it compiles; if not, the Console and the scene validator show the
checker's error with its line. Example scene: `examples/csharp/csharp_playground.okayscene`.
The plan for retiring the old OkayScript VM is in [migration_plan.md](migration_plan.md).

### Other backends

OkayScript is the engine's text scripting language, alongside a
node-based **visual scripting** graph (see [visual_scripting.md](visual_scripting.md)).
Scripts can be edited in the in-engine Script Editor panel or in an external IDE.

## OkayScript function reference

*Was `docs/scripting_reference.md` until 2026-11-20.*

> This page lists the built-in functions area by area (networking, Steam, JSON,
> collections, timers and more). For how the language itself works, start with
> [scripting.md](scripting.md#okayscript-1). All docs: [README.md](../README.md).

OkaySpace games are scripted in **OkayScript**, a small C-style language built
into the engine. Add a **Script** component to a GameObject (Inspector → Add
Component → Scripting → Script), then edit the code in the **Script Editor**
panel. The same reference is available inside the editor under
**Help → Scripting Reference**.

> Heads up: OkayScript uses **C-style braces `{ }` and semicolons `;`** — not
> Lua-style `function ... end`. The starter script the editor inserts is already
> in the right form.


### C#-style syntax

Scripts read like C#, and every piece of syntax does something: a script's fields
and functions **are** the component (no class or base type), `public` fields show
in the Inspector, `int` / `float` / `bool` / `string` are enforced, `const` can't
change and `static` is shared by every object running the script. Syntax that
would do nothing (`using`, `namespace`, a class wrapper, `override`, `5f`, ...)
still runs when pasted, but the Script Editor flags it.

See **C#-style syntax** in [scripting.md](scripting.md#okayscript-1) for the starter script,
what each part does, the functions the engine calls, and the full API list.

### Lifecycle

The engine calls these functions by name (PascalCase or lowercase both work):

```cs
void Start() {
    // once, when the scene starts (or when you press Play)
}

void Update() {
    // every frame; Time.deltaTime is the seconds since the last frame
}

void FixedUpdate() {
    // 50 times a second, however fast the game draws
}
```

`Awake()` and `OnEnable()` run just before `Start()`; `OnDisable()` and
`OnDestroy()` run when the object is destroyed.

See **C#-style syntax** in [scripting.md](scripting.md#okayscript-1) for the full list of
supported properties, methods, and event-handler names.

A complete movement example:

```
function start() {
    set_pos(0, 0);
    set("score", 0);
}

function update(dt) {
    var speed = 5;
    move(axis_x() * speed * dt, axis_y() * speed * dt);
    if (key_down("space")) { set("score", get("score") + 1); }
}
```

### Language

- Variables: `var x = 5;`
- Control flow: `if/else`, `while`, `for`, `return`, `break`, `continue`
- Operators: `+ - * / %`, `== != < > <= >=`, `&& || !`
- Arrays: `var a = [1, 2, 3]; a[0] = 10;`
- Blocks use `{ }`; statements end with `;`

### Built-in functions

#### Movement & Transform (2D)
| Function | Description |
|---|---|
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `set_x(v)` / `set_y(v)` | Set one position axis |
| `pos_x()` / `pos_y()` | Current local position |
| `rotate(deg)` | Rotate around Z by degrees |
| `move_toward(x, y, step)` | Step toward a point |
| `look_at("name")` | Rotate to face another object |

#### Transform & Movement (3D)
| Function | Description |
|---|---|
| `move3(dx, dy, dz)` | Translate in 3D |
| `set_pos3(x, y, z)` | Set 3D position |
| `set_z(v)` / `pos_z()` | Z position |
| `rotate3(x, y, z)` | Rotate by Euler degrees |
| `set_rot3(x, y, z)` | Set Euler rotation |
| `set_scale(s)` / `set_scale3(x, y, z)` | Uniform / per-axis scale |
| `scale_x()` / `scale_y()` / `scale_z()` | Current scale |
| `move_forward(d)` / `move_right(d)` | Move along this object's facing |
| `forward_x/y/z()` / `right_x/y/z()` | Facing direction vectors |
| `look_at3("name")` | Face an object in 3D |

#### Rigidbody (2D & 3D)
| Function | Description |
|---|---|
| `set_velocity(x, y)` / `set_velocity3(x, y, z)` | Set velocity |
| `set_vx/set_vy/set_vz(v)` | Set one velocity axis |
| `velocity_x()` / `velocity_y()` / `velocity_z()` | Read velocity |
| `add_force(x, y)` / `add_force3(x, y, z)` | Accumulate a force |
| `add_explosion_force(x, y, force, radius [, up])` | Shove this 2D body away from a point; fades to nothing at the radius |
| `add_impulse(...)` / `add_impulse3(...)` | Instant velocity change |
| `jump(v)` | Set upward velocity (2D or 3D body) |
| `set_gravity(x, y)` / `set_gravity3(x, y, z)` | Change world gravity |
| `physics_steps()` / `set_physics_steps(n)` | Read or set how many times a second physics runs |
| `physics_smooth()` / `set_physics_smooth(on)` | Draw bodies between physics steps |

C#-style: `GetComponent<Rigidbody>()` (3D) has `velocity`, `angularVelocity`,
`mass`, `drag`, `angularDrag`, `gravityScale`, `useGravity`, `isKinematic`,
`freezeRotation`, `bounciness`, `friction`, `position`, and `AddForce(v [,
ForceMode.X])`, `AddRelativeForce`, `AddImpulse`, `AddTorque`,
`AddRelativeTorque`, `AddForceAtPosition`, `AddExplosionForce`, `MovePosition`,
`MoveRotation`, `WakeUp`, `Sleep`. `GetComponent<Rigidbody2D>()` has the 2D
versions (`AddForce(v, ForceMode2D.Impulse)`, `AddTorque`, `angularVelocity` in
degrees per second, `bodyType = RigidbodyType2D.Kinematic`, ...). See
[scripting.md](scripting.md#rigidbody-3d-from-a-script).

#### Input
| Function | Description |
|---|---|
| `key("w")` / `key_down("w")` / `key_up("w")` | Keyboard held / pressed / released |
| `axis_x()` / `axis_y()` | WASD / arrow-key axis, −1..1 |
| `mouse_x()` / `mouse_y()` | Cursor position in pixels |
| `mouse(btn)` / `mouse_down(btn)` / `mouse_up(btn)` | Mouse buttons (0=L, 1=R, 2=M) |
| `gamepad(btn)` / `gamepad_x()` / `gamepad_y()` | Controller input |

#### This object
| Function | Description |
|---|---|
| `name()` / `set_name(s)` | Object name |
| `tag()` / `set_tag(s)` / `has_tag(s)` | Object tag |
| `set_active(b)` / `self_active()` | Enable / query this object |
| `destroy()` | Destroy this object |
| `set_parent("name")` / `detach()` | Re-parent / unparent |
| `child_count("name")` / `get_child("name", i)` / `parent_of("name")` | Hierarchy queries (child name by index; `""` if none) |
| `hurt("name", n)` / `heal("name", n)` / `add_xp("name", n)` | Damage / heal / grant XP on a named object's `Stats` (or `SurvivalStats` for hurt/heal); fires `on_death` / `on_level_up`; return whether it applied |
| `get_hp("name")` / `get_max_hp("name")` / `is_dead("name")` | Read health and death state (`is_dead()` with no argument is the humanoid check) |
| `set_light_intensity("name", v)` / `set_light_range("name", v)` / `set_light_angle("name", deg)` / `set_light_color("name", r,g,b)` | Light component on a named object |
| `set_text(s)` / `set_color(r,g,b,a)` / `set_texture(p)` | Sibling renderers |
| `set_mesh("Sphere")` | Swap a sibling MeshRenderer's primitive |
| `emit(n)` / `play_anim()` / `play_sound()` | Sibling FX, animation, audio |

#### Other objects & scene
| Function | Description |
|---|---|
| `exists("n")` / `is_active("n")` | Query an object by name |
| `activate("n")` / `deactivate("n")` | Show / hide an object |
| `obj_x("n")` / `obj_y("n")` / `obj_z("n")` | Another object's position |
| `dist_to("n")` | Distance to a named object |
| `vel_toward("n", speed)` | Aim this body's velocity at a target |
| `destroy_obj("n")` | Destroy a named object |
| `count_tag("t")` / `nearest_tag("t")` | Tag queries |
| `set_cam(x, y)` / `move_cam(dx, dy)` / `set_cam_zoom(z)` | Camera control |
| `set_bg(r,g,b)` / `set_light(x,y,z)` / `set_ambient(v)` | Background & lighting |
| `load_scene("file")` | Switch to another scene |
| `screen_w()` / `screen_h()` | Viewport size in pixels |

**Object references.** `other` in a collision / trigger handler, `gameObject`,
`GameObject.Find` / `FindWithTag` / `FindGameObjectsWithTag`,
`GameObject.FindIncludingInactive` and what `spawn` / `spawn3` / `pool_get` /
`Instantiate` return carry the object's unique id, so they mean exactly that
object even when copies share a name. They print and compare with text as the
name. Pass one anywhere a name is taken here (`destroy_obj(other)`,
`obj_x(other)`, `send_event(other, "Hit", 5)`); a quoted name still means the
first object with that name. `GameObject.Find`, `FindWithTag` and
`FindGameObjectsWithTag` return active objects only. Details:
[scripting.md](scripting.md#object-references).

#### UI (drive widgets by name)
| Function | Description |
|---|---|
| `ui_set_text("n", "s")` / `ui_get_text("n")` | Text/button label/input text |
| `get_clipboard_text()` / `set_clipboard_text("s")` | The text a player can copy and paste. `set_clipboard_text` returns 1 when the clipboard accepted it. Control characters are removed (a tab and a newline stay) and text past 8192 characters is cut short. A focused input field uses the same clipboard: Ctrl or Command with C, X and V |
| `ui_clicked("n")` | True the frame a named button was clicked |
| `ui_set_interactable("n", on)` | Enable/grey-out a button |
| `ui_slider_value("n")` / `ui_set_slider("n", v)` | Read/set a slider (0..1) |
| `ui_toggle_value("n")` / `ui_set_toggle("n", on)` | Read/set a toggle |
| `ui_dropdown_value("n")` / `ui_dropdown_text("n")` | Selected index / option text |
| `ui_set_dropdown("n", i)` | Select a dropdown option (fires on_change) |
| `ui_tab_value("n")` / `ui_tab_text("n")` | Selected tab index / its label |
| `ui_set_tab("n", i)` | Select a tab (fires on_change; disabled tabs are skipped) |
| `ui_set_tab_badge("n", i, count)` | Badge a tab: 0 = none, -1 = a dot, N = a number |
| `ui_set_tab_enabled("n", i, on)` | Enable / disable (dim) a tab |
| `ui_set_progress("n", v)` | Set a progress bar's fill (0..1) |
| `ui_get_rotation("n")` / `ui_set_rotation("n", degrees)` | Read or turn a UI frame, in degrees clockwise around its pivot. Children turn with it |
| `ui_get_scale("n")` / `ui_set_scale("n", x, y)` | Read or set a UI frame's width and height scale. One number sets both. 1, 1 is unchanged. Children scale with it |
| `ui_set_focus("n")` | Move keyboard and gamepad focus to that widget, draw the focus ring, and scroll it into view. A mouse click hides the ring until the next keyboard, gamepad or script focus |
| `ui_set_fill("n", v)` | Set a filled UI Image's amount (cooldowns/health) |

#### OkayUI (build a HUD from script — no Canvas objects)
These are **immediate-mode** OkayUI widgets: call them every frame from a script and
the toolkit draws them on top of the game, in both the standalone player **and** the
editor's Play mode. Nothing to place in the scene — the script *is* the UI. A widget
returns the live value, so you read and write game state in one line.

| Function | Description |
|---|---|
| `ui_begin("Title", x, y, w, h)` … `ui_end()` | Open/close a draggable window (coords optional) |
| `ui_text("s")` | A line of text |
| `ui_button("Label")` | Returns 1 the frame it's clicked, else 0 |
| `ui_checkbox("Label", on)` | Returns the new on/off (pass the current value back in) |
| `ui_switch("Label", on)` | A sliding on/off switch; returns the new on/off (pass the current value back in) |
| `ui_slider("Label", v, lo, hi)` | Returns the new value (pass the current value back in) |
| `ui_progress(t)` | A progress/health bar, `t` in 0..1 |
| `ui_sameline()` / `ui_separator()` | Lay the next widget beside / draw a divider |

```okay
// A tiny HUD built entirely from script. Attach to any object and press Play.
// hp / paused persist across frames (script variables keep their value).
ui_begin("HUD", 24, 24, 240, 150)
ui_text("Health")
ui_progress(hp / 100)
if (ui_button("Heal")) { hp = 100 }
ui_sameline()
if (ui_button("Hurt")) { hp = hp - 10 }
ui_separator()
paused = ui_checkbox("Paused", paused)
speed  = ui_slider("Speed", speed, 0, 10)
ui_end()
```

#### Tweening (DOTween-style) & saves
Every tween accepts an optional easing and an optional **on-complete** function
name as its trailing argument(s), and **returns an id**. You can use the id with
`tween_loops`, `tween_delay`, `tween_pause`, `tween_kill` and the `seq_*`
sequence functions. [docs/scripting/scripting.md](scripting.md#tweening-dotween-style)
has the full list, plus the **Tween Animation** component for tweens without code.

| Function | Description |
|---|---|
| `tween_move(x, y, dur [, ease][, "done"])` / `tween_move3(x,y,z,dur ...)` | Animate position |
| `tween_scale(s, dur [, ease][, "done"])` | Animate uniform scale |
| `tween_rotate(deg, dur [, ease][, "done"])` | Spin about Z (relative) |
| `tween_rotate_to(deg, dur [, ease][, "done"])` | Rotate to an absolute Z angle |
| `tween_scale_xy(sx, sy, dur [, ease][, "done"])` | Non-uniform scale |
| `tween_ui_move(x, y, dur [, ease][, "done"])` / `tween_ui_size(w, h, dur ...)` | Move / resize a UI widget |
| `tween_color(r, g, b, dur [, ease][, "done"])` / `tween_fade(alpha, dur ...)` | Animate color / opacity |
| `tween_move_by(dx, dy, dur [, ease][, "done"])` | Move by a relative offset |
| `tween_jump(x, y, height, dur [, "done"])` | Arc-jump to a target (coins, hops) |
| `tween_path(dur, x1,y1, x2,y2, ...)` | Move through waypoints |
| `tween_loop_move(x, y, dur [, ease])` / `tween_loop_scale(s, dur [, ease])` | Ping-pong forever (floaters, pulsing) |
| `tween_loop_rotate(dur [, dir])` | Spin continuously (loaders, coins) |
| `tween_number(from, to, dur [, "prefix"])` | Count a sibling text number (score ticks) |
| `tween_punch_scale(amount, dur [, vib])` / `tween_punch_pos(dx, dy, dur [, vib])` | Punch & settle ("juice") |
| `tween_shake(intensity, dur)` | Random shake that decays to a stop |
| `tween_clock(id, channel)` | Follow a Clock channel (`"game"`, `"ui"`, `"cinematic"`, or your own) instead of scene time; `""` goes back. Returns the id |
| `tween_tag(id, tag)` / `tween_speed_based(id)` / `tween_decay(id, rate)` | Group label for the `_tag` calls / duration is a speed in units a second / falloff of a punch or shake. Return the id |
| `tween_kill_tag(tag [, complete])` / `tween_pause_tag(tag)` / `tween_play_tag(tag)` / `tween_complete_tag(tag)` / `tween_rewind_id(id)` | Control every tween with a tag (or id) in the scene; return how many |
| `seq_prepend(seq, id)` / `seq_join_callback(seq, "fn")` | Play a tween before everything in a sequence / call a function when the last appended tween starts |
| `tween_prop(path, to, dur [, ease][, "done"])` | Tween any property of this object by path (`"Light.intensity"`, `"Transform.scale.y"`; see docs/engine/properties.md). The ease is a name or curve text: `"cubic-bezier(0.4,0,0.2,1)"`, `"steps(4)"` |
| `save_game([slot] [, label])` / `load_game([slot])` | Save / load the whole game (see docs/engine/save_games.md) |
| `game_save(slot [, label])` / `game_load(slot)` / `game_continue()` | Save slots for objects marked with a Saveable component (see docs/engine/save_games.md, "Saving without code"). `game_load` and `game_continue` take effect at the start of the next frame and reload the saved level. Return 1 when accepted, 0 (with a `[save]` Console error) when the slot is missing, damaged or from a newer version |
| `game_delete_slot(slot)` / `game_slot_exists(slot)` / `game_can_continue()` / `game_autosave()` | Delete a slot, ask whether it exists, ask whether any valid save exists, save into the autosave ring |
| `get_monitor()` / `get_monitors()` / `set_monitor(name or index)` | The monitor in use, every connected monitor's name, and choosing one |
| `get_display_mode()` / `set_display_mode("windowed" \| "borderless" \| "fullscreen")` | How the window is shown |
| `get_display_width()` / `get_display_height()` / `set_display_resolution(width, height)` | The size in pixels |
| `get_refresh_rate()` / `set_refresh_rate(hertz)` / `get_display_resolutions()` / `get_refresh_rates(width, height)` | The rate, the sizes on offer, and the rates for a size |
| `begin_display_confirm()` / `keep_display_settings()` / `revert_display_settings()` / `get_display_confirm_seconds()` | Start the 10-second "keep these settings?" question, answer it, or read the seconds left |
| `game_slot_text(slot)` / `game_slot_age(slot)` / `game_slot_label(slot)` | When a slot was saved (`"2026-11-08 14:03"` UTC, `""` if none), seconds since then (-1 if none), and its label |
| `save_exists([slot])` / `delete_save([slot])` | Manage save slots |
| `play_sequence("name")` / `stop_sequence()` | Play / stop a cutscene sequence (see docs/gameplay/cutscenes.md) |
| `sequence_playing()` / `sequence_time()` | 1 while a sequence plays / seconds into it; events reach `on_sequence_event(name)` |
| `loc("key" [, args...])` | The key's text in the player's language; `{0}`, `{1}`... from the arguments, `{name}` from a map (see docs/engine/localization.md) |
| `set_language("fr")` / `get_language()` / `languages()` / `language_name("fr")` | Switch (remembered for the next run) / read the language; a change runs `on_language_changed(code)` |
| `spline_point([name,] t)` / `spline_length([name])` | A point a fraction t along a Spline, by length / its length (see docs/engine/splines.md); a follower at an end calls `on_spline_end(which)` |
| `follower_set_speed([name,] s)` / `line_set_points([name,] points)` / `trail_clear([name])` | Spline Follower speed / Line Renderer points / clear a Trail Renderer |

Easings: `linear`, `in/out/in_out_quad`, `..._cubic`, `..._sine`, `..._expo`,
`in_back`/`out_back`, `out_elastic`, `out_bounce`.

#### Drag & drop and Data Assets
Add a **UI Draggable**/**Draggable** + **UI Drop Target**/**Drop Zone** in the
editor; items fire `on_drag_start/on_drag/on_drop` and targets fire
`on_receive` and `on_hover_enter/on_hover_exit`. Read the pair with
`ui_drop_source()/ui_drop_target()` (UI) or `drop_source()/drop_target()`
(world). Scriptable Objects: `data_num/data_str/data_has(path, key)` to read and
`data_set(path, key, v)` + `data_save(path)` to write `.okaydata` assets.

#### Scene Manager (build list)
| Function | Description |
|---|---|
| `load_scene_index(i)` | Load scene `i` from the build list |
| `load_scene_name("n")` | Load by scene name (file stem) or path |
| `load_next_scene()` | Load the next scene (wraps to the first) |
| `load_scene_additive("n"[, x, y, z])` | **Merge** another scene into the running one at an offset (seamless worlds — the current scene stays loaded) |
| `reload_scene()` | Reload the active scene |
| `scene_count()` / `scene_index()` / `scene_name()` | Query the build list |

#### Multiplayer (networking)
| Function | Description |
|---|---|
| `net_host(port)` | Start a server on this machine |
| `net_join("ip", port)` | Connect to a host |
| `net_host_relay("relay_ip", relay_port, "code")` | Host **via a relay** (NAT traversal — no port-forwarding) |
| `net_join_relay("relay_ip", relay_port, "code")` | Join a relay-hosted session by the same code |
| `net_relay_ready()` | 1 once the relay has paired this peer |
| `net_disconnect()` | Leave / stop the session |
| `net_connected()` / `net_is_server()` / `net_is_client()` | Status |
| `net_id()` / `net_peers()` | Your peer id / connected peer count |
| `net_ping()` | Round-trip time to the server in ms (clients) |
| `net_name("name")` | Set/get this peer's display name |
| `net_room("name")` | Set the lobby room (before host/join) — rooms are isolated |
| (host settings) | Set `serverName` / `password` / `maxPlayers` / `snapshotRate` on the Network Manager (inspector or Services panel) |
| `net_ready(1/0)` | Mark this peer ready in the lobby |
| `net_ready_count()` / `net_all_ready()` | (host) ready clients in the room / all ready? |
| `net_start_match()` / `net_match_started()` | (host) begin the match / has it begun? |
| `net_send("channel", "data")` | Broadcast a message to all peers |
| `net_send_to(id, "channel", "data")` | Message one peer |
| `net_send_reliable("channel", "data")` | Like net_send but resent until acked + de-duped |
| `net_kick(id [, "reason"])` / `net_was_kicked()` | Host kicks a peer / client check |
| `net_poll()` | Pop one received message (use in a `while`) |
| `net_msg_channel()` / `net_msg_data()` / `net_msg_from()` | The popped message |
| `net_set("key", "value")` | Set a server-authoritative **synced variable** |
| `net_get("key")` | Read a synced variable (same value on every peer) |
| `net_spawn("prefab", x, y[, z])` | **Replicated spawn** — instantiate a prefab on every peer (a path relative to the game folder; from a client, one of the host's Spawnable prefabs) |

#### Character animation (a Character or a Model Animator on this object, a child or a parent)
| Function | Description |
|---|---|
| `load_clips("text")` | Load keyframe clips from OkayVS-anim text (see [animation.md](../gameplay/animation.md)); returns count |
| `play_clip("name")` | Play a clip by name: the Character's own clip, the model's clip, or a built-in animation (`"Wave"`, `"Point"`, ...). It holds until `stop_clip()`: movement no longer switches it back. Returns 1 on success |
| `stop_clip()` | Release the clip: movement (or the previous animation) takes over again |
| `clip_speed(s)` / `clip_normalized()` | Playback speed (1 = normal) / how far through the clip (0..1) |
| `playing_clip()` / `is_playing_clip()` | The active clip name / whether one is playing |
| `set_anim(n)` / `get_anim()` | Set/get the built-in animation index (1 idle, 2 walk, 3 run, …) |

#### Steam (achievements, stats, leaderboards, cloud)
| Function | Description |
|---|---|
| `steam_name()` | The player's Steam name |
| `steam_unlock("ID")` / `steam_is_unlocked("ID")` / `steam_clear("ID")` | Achievements |
| `steam_set_stat("n", v)` / `steam_get_stat("n")` / `steam_inc_stat("n", by)` | Stats |
| `steam_store()` | Flush stats/achievements to Steam |
| `steam_progress("ID", cur, max)` | Show achievement progress (auto-unlocks at max) |
| `steam_leaderboard("board", score)` | Submit a leaderboard score |
| `steam_leaderboard_top("board", n)` | Top-N as an array of `"rank,name,score"` |
| `steam_cloud_write("file", "data")` / `steam_cloud_read("file")` | Steam Cloud |
| `steam_presence("key", "value")` | Rich presence |
| `steam_friends()` / `steam_overlay("page")` | Friend count / open the overlay |
| `steam_owns(appId)` / `steam_owns_dlc(appId)` | Ownership / DLC checks |
| `steam_achievement_count()` / `steam_achievement_name(i)` | Achievement count / name by index |
| `steam_cloud_has("file")` / `steam_cloud_delete("file")` / `steam_cloud_count()` | Steam Cloud extras |
| `steam_set_stat_int("n", v)` / `steam_get_stat_int("n")` / `steam_inc_stat_int("n", by)` | Integer stats (kills, wins — no float rounding) |
| `steam_get_presence("key")` | Read back a rich-presence value |
| `steam_friend_name(i)` / `steam_friend_id(i)` / `steam_invite(id)` | Friends by index; invite by id (ids are strings) |
| `steam_lobby_create(maxMembers, "name")` → id | Create a matchmaking lobby (returns its id string) |
| `steam_lobby_join(id)` / `steam_lobby_leave()` / `steam_lobby_current()` | Join / leave / current lobby id |
| `steam_lobby_list()` | Open lobbies as an array of `"id,name,members,max,joinable"` |
| `steam_lobby_set_data("k","v")` / `steam_lobby_get_data(id,"k")` | Lobby metadata (map, mode, …) |
| `steam_lobby_members(id)` | Member display names in a lobby, as an array |
| `steam_language()` | Steam client language |

> Lobby and friend ids are passed as **strings** (they exceed float precision).
> Without the Steamworks SDK these run against the in-memory simulation backend,
> which now **persists achievements, stats and cloud files to disk** (under the
> OS temp dir, keyed by app id) so progression and cloud saves survive a restart
> while you test in the editor.

#### Debugging
All of these print into the editor's **Console**.
| Function | Description |
|---|---|
| `print(...)` / `debug_log(...)` / `log_info(...)` | Log a line (args joined by spaces) |
| `log_warn(...)` / `log_error(...)` / `trace(...)` | Log at a level (warnings/errors stand out) |
| `watch("name", value)` | Log `name = value` for quick inspection |
| `assert(cond [, "msg"])` | Log an error when `cond` is false; returns the result |
| `format("hp={} of {}", a, b)` | Fill each `{}` with the next argument |
| `concat(...)` / `str_repeat("ab", 3)` | Join args / repeat a string |

#### State, math & data
| Function | Description |
|---|---|
| `set("k", v)` / `get("k")` | Shared variables across scripts |
| `approach(cur, target, step)` | Step toward a target without overshooting |
| `remap(v, inLo, inHi, outLo, outHi)` | Rescale a value between two ranges |
| `frac(x)` / `mod(a, b)` / `snap(v, step)` | Fraction / positive modulo / round to a step |
| `is_nan(x)` / `is_finite(x)` / `avg(...)` / `min3` / `max3` | Numeric helpers |
| `lerp_angle(a, b, t)` | Interpolate degrees the short way round |
| `prefs_set/prefs_get/prefs_save/prefs_load` | Persist data across runs |
| `rand(lo, hi)` / `randi(lo, hi)` / `chance(p)` | Randomness |
| `dist(x1,y1,x2,y2)` / `dist3(...)` / `angle_to(...)` | Geometry |
| `sin cos tan asin acos atan atan2` | Trigonometry |
| `sqrt pow exp log abs sign floor ceil round` | Math |
| `min max clamp clamp01 lerp smoothstep wrap ping_pong` | Ranges & easing |
| `array push pop count contains index_of sort_num shuffle` | Lists |

#### Collections

Arrays and maps (dictionaries) are shared by reference and can be nested.

| Function | Description |
|---|---|
| `array(...)` / `count(a)` / `push(a, v)` / `pop(a)` | Make a list, its length, append, remove-last |
| `first(a)` / `last(a)` | First / last item (null if empty) |
| `insert_at(a, i, v)` / `remove_at(a, i)` | Insert at / remove at an index |
| `slice(a, start, end)` | Sub-array; negative indices count from the end |
| `range(n)` / `range(lo, hi[, step])` | Build a numeric array |
| `contains(a, v)` / `index_of(a, v)` | Membership / first index (or -1) |
| `sum(a)` / `min_of(a)` / `max_of(a)` | Reduce a numeric array |
| `reverse(a)` / `sort_num(a)` / `sort_str(a)` / `shuffle(a)` / `choose(a)` | Reorder / random pick |
| `clear(a_or_m)` | Empty an array or map in place |
| `map()` / `map_set(m,"k",v)` / `map_get(m,"k")` / `map_has(m,"k")` | Dictionary basics |
| `map_remove(m,"k")` / `map_keys(m)` / `map_values(m)` / `map_count(m)` | Delete / list keys/values / size |
| `map_clear(m)` / `map_merge(dst, src)` | Empty / merge (src wins) |

Read and write elements with `[]` — arrays by number, maps by key — and use
compound assignment directly on an element:

```
var a = array(10, 20, 30);
a[1] += 5;          // 25
a[0]++;             // 11

var m = map();
m["hp"] = 100;      // same as map_set(m, "hp", 100)
m["hp"] -= 30;      // 70
m["score"] += 40;   // a missing key starts at 0 -> 40
```

Iterate either with `foreach`:

```
foreach (var item in myArray) { print(item); }
foreach (var key in myMap)    { print(key + " = " + map_get(myMap, key)); }
```

#### Functional helpers (named callbacks)

Pass the **name** of a function to apply it across an array:

| Function | Description |
|---|---|
| `map_fn(a, "fn")` | New array of `fn(item)` for each item |
| `filter_fn(a, "fn")` | Keep items where `fn(item)` is true |
| `reduce_fn(a, "fn", init)` | Fold left: `acc = fn(acc, item)` |
| `for_each(a, "fn")` | Call `fn(item)` for every item |
| `find_fn(a, "fn")` / `any_fn(a, "fn")` / `all_fn(a, "fn")` / `count_fn(a, "fn")` | Search / test / count |
| `call("fn"[, args...])` | Invoke a function (user or builtin) by name |

```
function dbl(x) { return x * 2; }
var doubled = map_fn(array(1, 2, 3), "dbl");   // [2, 4, 6]
```

These walk a copy of the array taken when they start, so a callback that pushes to,
removes from or clears the same array does not disturb the walk (the callback sees the
array change; the walk still visits the items it started with).

Wherever a built-in wants a whole number (an index, a count, a length, a button), a number
outside the integer range is clamped to the nearest end of that range and `NaN` counts as 0,
instead of wrapping around: `substr("hello", 1, 1e30)` is `"ello"`.

#### JSON & type introspection

| Function | Description |
|---|---|
| `to_json(value)` / `from_json("...")` | Serialize / parse arrays, maps, numbers, strings, bools (also `json_stringify` / `json_parse`) |
| `typeof(v)` | `"number"`, `"string"`, `"bool"`, `"array"`, `"map"`, `"vec3"` or `"null"` |
| `is_num is_str is_bool is_array is_map` | Type tests |

```
var save = map();
map_set(save, "level", 7);
save_prefs("slot1", to_json(save));            // persist
var loaded = from_json(load_prefs("slot1", "{}"));
```

#### Error handling

Wrap risky code in `try` / `catch`, and raise your own errors with `throw`:

```
try {
    var data = from_json(prefs_get_str("save", "{}"));
    if (map_count(data) == 0) { throw "no save data"; }
    print("Level: " + map_get(data, "level"));
} catch (e) {
    print("Load failed: " + e);   // e is the error message
}
```

The `catch` clause is optional — `try { ... }` on its own just swallows any
error and continues. Control flow (`return` / `break` / `continue`) passes
through a `try` unaffected.

#### Easing curves

Each maps a normalized `t` (0..1) to an eased 0..1 — feed the result to `lerp`:

`ease_in`, `ease_out`, `ease_in_out`, `ease_in_cubic`, `ease_out_cubic`,
`ease_in_out_cubic`, `ease_back`, `ease_elastic`, `ease_bounce`,
plus `fract(x)` for the fractional part. A NaN `t` counts as 0.

`ease_curve(t, "OutBack")` is any ease by name or as curve text (`"cubic-bezier(0.4, 0, 0.2, 1)"`,
`"steps(4)"`, `"ease-in-out"`; the text `tween_ease` takes). Text it does not know leaves `t` as it is; a curve
may overshoot 0..1 (Back, Elastic, a bezier with y outside it).

#### More string helpers

`upper lower trim trim_start trim_end capitalize title_case str_reverse`
`substr char_at replace split join repeat pad_left pad_right format`.

#### Friendly aliases
Intuitive names for common builtins so code reads naturally:
`delta_time` (dt), `get_key` / `get_key_down` / `get_key_up` (key…),
`random` / `random_int` (rand/randi), `pick` (choose), `distance` (dist),
`instantiate` (spawn), `destroy_self` (destroy), `translate` (move),
`set_position` (set_pos), `play_audio` (play_sound), `to_string` / `str`
(to_str), `to_number` / `num` (to_num), `get_x/get_y/get_z` (pos_*),
`screen_width` / `screen_height`.
| `upper lower split join substr replace trim str_len` | Strings |
| `time()` / `dt()` / `fps()` | Timing |
| `session_time()` / `session_unscaled_time()` / `session_frame_count()` | Game seconds, real seconds and frames since this Play began (`time()` keeps counting across Plays) |
| `unscaled_dt()` / `is_paused()` / `hitch_count()` | Real frame time (a pause and slow motion do not change it), the pause, frames over 0.1 s |
| `push_pause(reason)` / `pop_pause(reason)` / `is_paused_by(reason)` | Pause with a reason of your own; the game stays paused until every reason is let go (two scripts never resume each other) |
| `hit_stop(seconds [, scale])` / `slow_motion(scale, seconds)` / `ramp_timescale(target, seconds)` | Freeze frame, bullet time, an eased time scale (real seconds; they end by themselves) |
| `print(...)` | Log to the Console panel |

#### Timers & spawning
| Function | Description |
|---|---|
| `after(seconds, "fn")` | Call `fn` once after a delay |
| `actions_load("text")` | Add an Actions list from its saved text and run it on this object. An exported Actions component is this call; every step in the text runs as it does on the component |
| `every(seconds, "fn")` | Call `fn` repeatedly on an interval |
| `cancel_timers()` | Clear all scheduled callbacks |
| `spawn("prefab", x, y)` / `spawn3("prefab", x, y, z)` | Instantiate prefabs; returns a reference to the new copy (`false` when it failed) |
| `pool_get("prefab", x, y [, z])` / `pool_release()` | Spawn from the scene's pool for a prefab (reused objects start fresh) / give this object back to its pool |
| `pool_prewarm("prefab", n)` / `pool_idle("prefab")` | Make `n` idle objects ahead of time / count the idle ones |

#### Physics queries
| Function | Description |
|---|---|
| `raycast_hit(ox, oy, dx, dy [, maxDist])` | 2D ray hits a collider? (boolean) |
| `raycast(ox, oy, dx, dy [, maxDist])` | 2D ray; returns the **name** of the object hit ("" = miss) |
| `ray_hit()` / `ray_object()` / `ray_x()` / `ray_y()` / `ray_nx()` / `ray_ny()` / `ray_dist()` | Details of the last `raycast()` |
| `raycast_hit3(ox,oy,oz, dx,dy,dz [, maxDist])` | 3D ray hits a collider? (boolean) |
| `raycast3(ox,oy,oz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]])` | 3D ray; returns hit name + `ray3_hit/object/x/y/z/nx/ny/nz/dist()` |
| `raycast_all3(...)` / `raycast_all(ox,oy,dx,dy ...)` | Every object hit, nearest first (`ray_all_count/object/dist/point/normal(i)`) |
| `sphere_cast3` / `box_cast3` / `capsule_cast3` | Thick shape casts (skip triggers); details via `ray3_*` |
| `box_cast` / `capsule_cast` | Flat box and capsule casts (skip triggers); details via `ray_*`. A leading `"hit"` / `"point"` / `"normal"` / `"distance"` returns that detail |
| `overlap_box` / `overlap_capsule` | Names of the objects inside a flat box or capsule |
| `overlap_sphere3` / `overlap_box3` / `overlap_capsule3` | Names of the objects inside a 3D shape |
| `layer_mask("Default", "Enemy")` | A layers value (or pass `"0,3"` / `"Default,Enemy"` directly) |
| `overlap(x, y)` | A 2D collider contains this point? |

In collision / trigger handlers, `contact_point`, `contact_normal`,
`contact_x/_y/_z`, `impact_speed` and `other_tag` describe the touch (see
docs/scripting/scripting.md).

### Physics events

If this object has a collider, define either of these to react to contacts:

```
function on_collision() { /* hit a solid */ }
function on_trigger()   { /* entered a trigger volume */ }
```
