# OkayScript — Scripting in OkaySpace

OkaySpace games are scripted in **OkayScript**, a small C-style language built
into the engine. Add a **Script** component to a GameObject (Inspector → Add
Component → Scripting → Script), then edit the code in the **Script Editor**
panel. The same reference is available inside the editor under
**Help → Scripting Reference**.

> Heads up: OkayScript uses **C-style braces `{ }` and semicolons `;`** — not
> Lua-style `function ... end`. The starter script the editor inserts is already
> in the right form.

## Lifecycle

Two functions drive every script, like Unity's `MonoBehaviour`:

```
function start() {
    // runs once when the scene starts (or when you press Play)
}

function update(dt) {
    // runs every frame; dt is the seconds elapsed since the last frame
}
```

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

## Language

- Variables: `var x = 5;`
- Control flow: `if/else`, `while`, `for`, `return`, `break`, `continue`
- Operators: `+ - * / %`, `== != < > <= >=`, `&& || !`
- Arrays: `var a = [1, 2, 3]; a[0] = 10;`
- Blocks use `{ }`; statements end with `;`

## Built-in functions

### Movement & Transform
| Function | Description |
|---|---|
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `rotate(deg)` | Rotate around Z by degrees |
| `pos_x()` / `pos_y()` | Current local position |

### Input
| Function | Description |
|---|---|
| `key("w")` | True while a key is held |
| `key_down("w")` | True the frame it's first pressed |
| `axis_x()` / `axis_y()` | WASD / arrow-key axis, range −1..1 |
| `mouse_x()` / `mouse_y()` | Cursor position in pixels |
| `mouse(btn)` / `mouse_down(btn)` | Mouse buttons (0=L, 1=R, 2=M) |
| `gamepad(btn)` / `gamepad_x()` / `gamepad_y()` | Controller input |

### State & Math
| Function | Description |
|---|---|
| `set("key", value)` / `get("key")` | Shared variables across scripts |
| `rand(lo, hi)` | Random float in range |
| `dist(x1, y1, x2, y2)` | Distance between two points |
| `time()` / `dt()` | Elapsed time / current frame delta |
| `print(...)` | Log to the Console panel |

### Timers & Spawning
| Function | Description |
|---|---|
| `after(seconds, "fn")` | Call function `fn` once after a delay |
| `every(seconds, "fn")` | Call `fn` repeatedly on an interval |
| `cancel_timers()` | Clear all scheduled callbacks |
| `spawn("prefab", x, y)` | Instantiate a prefab in 2D |
| `spawn3("prefab", x, y, z)` | Instantiate a prefab in 3D |

## Physics events

If this object has a collider, define either of these to react to contacts:

```
function on_collision() { /* hit a solid */ }
function on_trigger()   { /* entered a trigger volume */ }
```
