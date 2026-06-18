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
- **Functions:** `function name(a, b) { return a + b; }`
- **Comments:** `// ...` or `# ...`

Top-level statements run once when the script loads (good for setup); `start()`
runs when the scene starts and `update(dt)` runs every frame.

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
| `get(name)` / `set(name, value)` | shared host globals (in memory) |
| `spawn(prefabPath, x, y)` | instantiate a `.okayprefab` at a position |
| `destroy()` | destroy this script's own GameObject |
| `print(...)` | log values to the console |

### Persistent prefs (high scores, settings)
| Function | Effect |
| --- | --- |
| `prefs_set(key, value)` | store a number or string |
| `prefs_get(key, default)` | read a number |
| `prefs_get_str(key, default)` | read a string |
| `prefs_save(path)` / `prefs_load(path)` | write/read the prefs file |

The standalone player auto-loads `game.okayprefs` on launch and saves it on
exit, so values set with `prefs_set` persist between play sessions.

### Math
`abs sin cos tan sqrt pow floor ceil round sign min max clamp lerp atan2 len
rand(lo, hi) dist(x1, y1, x2, y2) pi() deg2rad(d) rad2deg(r)`

## Other backends

OkayScript is the default, but the engine also supports **Lua** and **C#**
backends when built with `-DOKAY_WITH_LUA=ON` / `-DOKAY_WITH_CSHARP=ON`, and a
node-based **visual scripting** graph (see [visual_scripting.md](visual_scripting.md)).
Scripts can be edited in the in-engine Script Editor panel or in an external IDE.
