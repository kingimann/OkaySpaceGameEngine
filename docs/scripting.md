# OkayScript

OkayScript is the engine's built-in scripting language — a small, dependency-free
language that ships in every build (no Lua/C# toolchain required). Attach a
`ScriptComponent` to a GameObject, write a script, and the engine calls your
`start()` and `update(dt)` functions as part of the normal scene lifecycle.

```c
function start() {
    set_pos(0, 0);
}

function update(dt) {
    // Move with WASD at 5 units/second.
    move(axis_x() * 5 * dt, axis_y() * 5 * dt);
    if (key_down("space")) { print("jump!"); }
}
```

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
  - `break` and `continue` inside loops
- **Functions:** `function name(a, b) { return a + b; }`
- **Arrays:** `var a = [1, 2, 3];` — index with `a[0]`, assign `a[1] = 9`,
  append with `a[count(a)] = x` or `push(a, x)`, remove with `pop(a)`, length
  via `count(a)`. Arrays are shared by reference. Make an empty one with
  `array()`.
- **Comments:** `// ...` or `# ...`

Top-level statements run once when the script loads (good for setup); `start()`
runs when the scene starts and `update(dt)` runs every frame. Define
`on_trigger()` or `on_collision()` to react when this object's collider
overlaps or contacts another (pickups, damage), or `on_click()` to handle a
UI Button press.

## Built-in functions

### Transform
| Function | Effect |
| --- | --- |
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `set_x(x)` / `set_y(y)` | Set one axis |
| `pos_x()` / `pos_y()` | Read local position |
| `rotate(deg)` | Rotate about Z by degrees |

### Input
| Function | Returns |
| --- | --- |
| `key("a")` | true while the key is held |
| `key_down("a")` | true on the frame the key is pressed |
| `axis_x()` / `axis_y()` | -1..1 from A/D and S/W |
| `mouse_x()` / `mouse_y()` | cursor position in pixels |
| `mouse(btn)` | true while a mouse button is held (0=left, 1=right, 2=middle) |
| `mouse_down(btn)` | true on the frame the button is pressed |

### Time & state
| Function | Returns |
| --- | --- |
| `time()` | seconds since start |
| `dt()` | last frame delta time |
| `set_timescale(x)` / `timescale()` | global speed (0 = pause, 0.5 = slow-mo) |
| `get(name)` / `set(name, value)` | shared host globals (in memory) |
| `spawn(prefabPath, x, y)` | instantiate a `.okayprefab` at a position |
| `destroy()` | destroy this script's own GameObject |
| `load_scene(path)` | load a `.okayscene` at end of frame (level change/restart) |
| `raycast_hit(ox, oy, dx, dy[, dist])` | true if a ray hits a collider |
| `overlap(x, y)` | true if a collider contains the point |
| `set_gravity(x, y)` | set the scene's 2D gravity (0,0 for top-down) |
| `set_text(string)` | set this object's TextRenderer text |
| `set_color(r, g, b[, a])` | tint this object's Sprite/Text |
| `set_texture(path)` | set this object's SpriteRenderer image |
| `flip_x(bool)` / `flip_y(bool)` | mirror this object's sprite |
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

### Math
`abs sin cos tan sqrt pow floor ceil round sign min max clamp lerp atan2 len
rand(lo, hi) dist(x1, y1, x2, y2) pi() deg2rad(d) rad2deg(r)`

### Arrays
`array() count(a) push(a, v) pop(a) contains(a, v) index_of(a, v) remove_at(a, i)`
— plus literals `[...]`, indexing `a[i]`, and `a[i] = v`.

### Strings
`str_len(s) upper(s) lower(s) substr(s, start, n) char_at(s, i) str_find(s, sub)
to_num(s) to_str(x) split(s, sep) join(arr, sep)` — plus `+` concatenation.

### Maps / dictionaries (string keys)
`map() map_set(m, k, v) map_get(m, k[, default]) map_has(m, k) map_remove(m, k)
map_keys(m) map_count(m)` — shared by reference, like arrays.

## Other backends

OkayScript is the default, but the engine also supports **Lua** and **C#**
backends when built with `-DOKAY_WITH_LUA=ON` / `-DOKAY_WITH_CSHARP=ON`, and a
node-based **visual scripting** graph (see [visual_scripting.md](visual_scripting.md)).
Scripts can be edited in the in-engine Script Editor panel or in an external IDE.
