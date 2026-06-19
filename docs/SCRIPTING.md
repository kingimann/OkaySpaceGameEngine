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

### Movement & Transform (2D)
| Function | Description |
|---|---|
| `move(dx, dy)` | Translate by (dx, dy) |
| `set_pos(x, y)` | Set local position |
| `set_x(v)` / `set_y(v)` | Set one position axis |
| `pos_x()` / `pos_y()` | Current local position |
| `rotate(deg)` | Rotate around Z by degrees |
| `move_toward(x, y, step)` | Step toward a point |
| `look_at("name")` | Rotate to face another object |

### Transform & Movement (3D)
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

### Rigidbody (2D & 3D)
| Function | Description |
|---|---|
| `set_velocity(x, y)` / `set_velocity3(x, y, z)` | Set velocity |
| `set_vx/set_vy/set_vz(v)` | Set one velocity axis |
| `velocity_x()` / `velocity_y()` / `velocity_z()` | Read velocity |
| `add_force(x, y)` / `add_force3(x, y, z)` | Accumulate a force |
| `add_impulse(...)` / `add_impulse3(...)` | Instant velocity change |
| `jump(v)` | Set upward velocity (2D or 3D body) |
| `set_gravity(x, y)` / `set_gravity3(x, y, z)` | Change world gravity |

### Input
| Function | Description |
|---|---|
| `key("w")` / `key_down("w")` / `key_up("w")` | Keyboard held / pressed / released |
| `axis_x()` / `axis_y()` | WASD / arrow-key axis, −1..1 |
| `mouse_x()` / `mouse_y()` | Cursor position in pixels |
| `mouse(btn)` / `mouse_down(btn)` / `mouse_up(btn)` | Mouse buttons (0=L, 1=R, 2=M) |
| `gamepad(btn)` / `gamepad_x()` / `gamepad_y()` | Controller input |

### This object
| Function | Description |
|---|---|
| `name()` / `set_name(s)` | Object name |
| `tag()` / `set_tag(s)` / `has_tag(s)` | Object tag |
| `set_active(b)` / `self_active()` | Enable / query this object |
| `destroy()` | Destroy this object |
| `set_parent("name")` / `detach()` | Re-parent / unparent |
| `set_text(s)` / `set_color(r,g,b,a)` / `set_texture(p)` | Sibling renderers |
| `set_mesh("Sphere")` | Swap a sibling MeshRenderer's primitive |
| `emit(n)` / `play_anim()` / `play_sound()` | Sibling FX, animation, audio |

### Other objects & scene
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

### State, math & data
| Function | Description |
|---|---|
| `set("k", v)` / `get("k")` | Shared variables across scripts |
| `prefs_set/prefs_get/prefs_save/prefs_load` | Persist data across runs |
| `rand(lo, hi)` / `randi(lo, hi)` / `chance(p)` | Randomness |
| `dist(x1,y1,x2,y2)` / `dist3(...)` / `angle_to(...)` | Geometry |
| `sin cos tan asin acos atan atan2` | Trigonometry |
| `sqrt pow exp log abs sign floor ceil round` | Math |
| `min max clamp clamp01 lerp smoothstep wrap ping_pong` | Ranges & easing |
| `array push pop count contains index_of sort_num shuffle` | Lists |
| `upper lower split join substr replace trim str_len` | Strings |
| `time()` / `dt()` / `fps()` | Timing |
| `print(...)` | Log to the Console panel |

### Timers & spawning
| Function | Description |
|---|---|
| `after(seconds, "fn")` | Call `fn` once after a delay |
| `every(seconds, "fn")` | Call `fn` repeatedly on an interval |
| `cancel_timers()` | Clear all scheduled callbacks |
| `spawn("prefab", x, y)` / `spawn3("prefab", x, y, z)` | Instantiate prefabs |

### Physics queries
| Function | Description |
|---|---|
| `raycast_hit(ox, oy, dx, dy [, maxDist])` | 2D ray hits a collider? |
| `raycast_hit3(ox,oy,oz, dx,dy,dz [, maxDist])` | 3D ray hits a collider? |
| `overlap(x, y)` | A 2D collider contains this point? |

## Physics events

If this object has a collider, define either of these to react to contacts:

```
function on_collision() { /* hit a solid */ }
function on_trigger()   { /* entered a trigger volume */ }
```
