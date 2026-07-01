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

Two functions drive every script, the same way Unity uses `MonoBehaviour`
(OkaySpace's base class is `OkaySource`):

```
function start() {
    // runs once when the scene starts (or when you press Play)
}

function update(dt) {
    // runs every frame; dt is the seconds elapsed since the last frame
}
```

### Prefer Unity style? Write it like C#.

OkayScript also accepts a Unity/C# flavor — PascalCase `Start()`/`Update()`,
`transform.position`, `Input.GetKeyDown(...)`, `Time.deltaTime`, `new Vector3(...)`,
typed vars (`float speed = 5f;`), `i++`, and a `class : OkaySource` wrapper
(any base name parses, so Unity code with `: MonoBehaviour` still pastes in
unchanged):

```cs
public class Player : OkaySource {
    float speed = 5f;
    void Update() {
        transform.position.x += Input.GetAxis("Horizontal") * speed * Time.deltaTime;
        if (Input.GetKeyDown("space")) { Debug.Log("jump!"); }
    }
}
```

See **Unity-style syntax** in [scripting.md](scripting.md) for the full list of
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

### UI (drive widgets by name)
| Function | Description |
|---|---|
| `ui_set_text("n", "s")` / `ui_get_text("n")` | Text/button label/input text |
| `ui_clicked("n")` | True the frame a named button was clicked |
| `ui_set_interactable("n", on)` | Enable/grey-out a button |
| `ui_slider_value("n")` / `ui_set_slider("n", v)` | Read/set a slider (0..1) |
| `ui_toggle_value("n")` / `ui_set_toggle("n", on)` | Read/set a toggle |
| `ui_dropdown_value("n")` / `ui_dropdown_text("n")` | Selected index / option text |
| `ui_set_dropdown("n", i)` | Select a dropdown option (fires on_change) |
| `ui_set_progress("n", v)` | Set a progress bar's fill (0..1) |
| `ui_set_fill("n", v)` | Set a filled UI Image's amount (cooldowns/health) |

### OkayUI (build a HUD from script — no Canvas objects)
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

### Tweening (DOTween-style) & saves
Every tween accepts an optional easing and an optional **on-complete** function
name as its trailing argument(s).

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
| `save_game([slot])` / `load_game([slot])` | Snapshot / restore the scene |
| `save_exists([slot])` / `delete_save([slot])` | Manage save slots |

Easings: `linear`, `in/out/in_out_quad`, `..._cubic`, `..._sine`, `..._expo`,
`in_back`/`out_back`, `out_elastic`, `out_bounce`.

### Drag & drop and Data Assets
Add a **UI Draggable**/**Draggable** + **UI Drop Target**/**Drop Zone** in the
editor; items fire `on_drag_start/on_drag/on_drop` and targets fire
`on_receive` and `on_hover_enter/on_hover_exit`. Read the pair with
`ui_drop_source()/ui_drop_target()` (UI) or `drop_source()/drop_target()`
(world). Scriptable Objects: `data_num/data_str/data_has(path, key)` to read and
`data_set(path, key, v)` + `data_save(path)` to write `.okaydata` assets.

### Scene Manager (build list)
| Function | Description |
|---|---|
| `load_scene_index(i)` | Load scene `i` from the build list |
| `load_scene_name("n")` | Load by scene name (file stem) or path |
| `load_next_scene()` | Load the next scene (wraps to the first) |
| `reload_scene()` | Reload the active scene |
| `scene_count()` / `scene_index()` / `scene_name()` | Query the build list |

### Multiplayer (networking)
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
| `net_spawn("prefab", x, y[, z])` | **Replicated spawn** — instantiate a prefab on every peer |

### Character animation (a Character on the same object)
| Function | Description |
|---|---|
| `load_clips("text")` | Load keyframe clips from OkayVS-anim text (see [animation.md](animation.md)); returns count |
| `play_clip("name")` | Play a loaded clip by name (returns 1 on success) |
| `stop_clip()` | Stop the clip, return to the built-in animation |
| `playing_clip()` / `is_playing_clip()` | The active clip name / whether one is playing |
| `set_anim(n)` / `get_anim()` | Set/get the built-in animation index (1 idle, 2 walk, 3 run, …) |

### Steam (achievements, stats, leaderboards, cloud)
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
| `steam_achievement_count()` / `steam_language()` | Achievement count / client language |

### Debugging
All of these print into the editor's **Console**.
| Function | Description |
|---|---|
| `print(...)` / `debug_log(...)` / `log_info(...)` | Log a line (args joined by spaces) |
| `log_warn(...)` / `log_error(...)` / `trace(...)` | Log at a level (warnings/errors stand out) |
| `watch("name", value)` | Log `name = value` for quick inspection |
| `assert(cond [, "msg"])` | Log an error when `cond` is false; returns the result |
| `format("hp={} of {}", a, b)` | Fill each `{}` with the next argument |
| `concat(...)` / `str_repeat("ab", 3)` | Join args / repeat a string |

### State, math & data
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

### Collections

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

Iterate either with `foreach`:

```
foreach (var item in myArray) { print(item); }
foreach (var key in myMap)    { print(key + " = " + map_get(myMap, key)); }
```

### Functional helpers (named callbacks)

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

### JSON & type introspection

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

### Easing curves

Each maps a normalized `t` (0..1) to an eased 0..1 — feed the result to `lerp`:

`ease_in`, `ease_out`, `ease_in_out`, `ease_in_cubic`, `ease_out_cubic`,
`ease_in_out_cubic`, `ease_back`, `ease_elastic`, `ease_bounce`,
plus `fract(x)` for the fractional part.

### More string helpers

`upper lower trim trim_start trim_end capitalize title_case str_reverse`
`substr char_at replace split join repeat pad_left pad_right format`.

### Friendly aliases
Intuitive names for common builtins so code reads naturally:
`delta_time` (dt), `get_key` / `get_key_down` / `get_key_up` (key…),
`random` / `random_int` (rand/randi), `pick` (choose), `distance` (dist),
`instantiate` (spawn), `destroy_self` (destroy), `translate` (move),
`set_position` (set_pos), `play_audio` (play_sound), `to_string` / `str`
(to_str), `to_number` / `num` (to_num), `get_x/get_y/get_z` (pos_*),
`screen_width` / `screen_height`.
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
| `raycast_hit(ox, oy, dx, dy [, maxDist])` | 2D ray hits a collider? (boolean) |
| `raycast(ox, oy, dx, dy [, maxDist])` | 2D ray; returns the **name** of the object hit ("" = miss) |
| `ray_hit()` / `ray_object()` / `ray_x()` / `ray_y()` / `ray_nx()` / `ray_ny()` / `ray_dist()` | Details of the last `raycast()` |
| `raycast_hit3(ox,oy,oz, dx,dy,dz [, maxDist])` | 3D ray hits a collider? (boolean) |
| `raycast3(ox,oy,oz, dx,dy,dz [, maxDist])` | 3D ray; returns hit name + `ray3_hit/object/x/y/z/nx/ny/nz/dist()` |
| `overlap(x, y)` | A 2D collider contains this point? |

## Physics events

If this object has a collider, define either of these to react to contacts:

```
function on_collision() { /* hit a solid */ }
function on_trigger()   { /* entered a trigger volume */ }
```
