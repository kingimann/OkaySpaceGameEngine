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
  - `for x in myArray { ... }` (foreach over an array)
  - `break` and `continue` inside loops
- **Ternary:** `cond ? a : b`
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
UI Button press. Sibling UI widgets fire their own events too: `on_change()`
when a UI Slider is dragged and `on_toggle()` when a UI Toggle is clicked.

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
Smoothly animate this object over time via the scene scheduler. Every tween
takes an optional easing name and an optional **on-complete** function name as
its last argument(s).

| Function | Effect |
| --- | --- |
| `tween_move(x, y, dur[, ease][, "done"])` | move to (x, y); `ease` e.g. `"out_quad"` |
| `tween_move3(x, y, z, dur[, ease][, "done"])` | move in 3D |
| `tween_scale(s, dur[, ease][, "done"])` | scale to uniform `s` |
| `tween_rotate(deg, dur[, ease][, "done"])` | spin `deg` degrees about Z (relative) |
| `tween_rotate_to(deg, dur[, ease][, "done"])` | rotate to an **absolute** Z angle (shortest path) |
| `tween_scale_xy(sx, sy, dur[, ease][, "done"])` | non-uniform scale to (sx, sy) |
| `tween_ui_move(x, y, dur[, ease][, "done"])` | move a **UI widget** (anchored position) |
| `tween_ui_size(w, h, dur[, ease][, "done"])` | resize a **UI widget** (grow/shrink panels) |
| `tween_color(r, g, b, dur[, ease][, "done"])` | fade the sprite/mesh color |
| `tween_fade(a, dur[, ease][, "done"])` | fade alpha to `a` (works on sprites, meshes & UI Image/Panel) |
| `tween_move_by(dx, dy, dur[, ease][, "done"])` | move by a **relative** offset |
| `tween_jump(x, y, height, dur[, "done"])` | arc-jump to (x, y) peaking `height` up (coins, hops) |
| `tween_path(dur, x1, y1, x2, y2, ...)` | move through a list of waypoints |
| `tween_loop_move(x, y, dur[, ease])` | **ping-pong** forever between here and (x, y) |
| `tween_loop_scale(s, dur[, ease])` | ping-pong the scale forever (pulsing) |
| `tween_loop_rotate(dur[, dir])` | spin continuously, a turn every `dur` (dir +1/-1) |
| `tween_number(from, to, dur[, "prefix"])` | count a sibling text number up/down (score ticks) |
| `tween_punch_scale(amount, dur[, vib])` | punch the scale and settle back ("juice") |
| `tween_punch_pos(dx, dy, dur[, vib])` | punch the position and settle back |
| `tween_shake(intensity, dur)` | random shake that decays to a stop (impact/camera) |

Easings: `linear in_quad out_quad in_out_quad in_cubic out_cubic in_out_cubic
in_sine out_sine in_out_sine in_expo out_expo in_out_expo in_back out_back
out_elastic out_bounce`.

```c
function start() {
  tween_move(5, 0, 1.0, "out_quad", "arrived");  // calls arrived() when done
  tween_loop_move(0, 0.3, 0.8, "in_out_sine");   // bob forever
}
function arrived() { tween_punch_scale(0.3, 0.25); }   // pop on arrival
```

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

## Other backends

OkayScript is the default, but the engine also supports **Lua** and **C#**
backends when built with `-DOKAY_WITH_LUA=ON` / `-DOKAY_WITH_CSHARP=ON`, and a
node-based **visual scripting** graph (see [visual_scripting.md](visual_scripting.md)).
Scripts can be edited in the in-engine Script Editor panel or in an external IDE.
