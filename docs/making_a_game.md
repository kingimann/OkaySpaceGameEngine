# Make your first game

This walks you from an empty editor to a shipped, double-clickable game in a few
minutes. It uses only what ships in the box — no plugins, no external tools.

## 1. Open the engine

Run **`dist/OkaySpaceEngine.exe`** (Windows). That single self-contained `.exe`
*is* the editor: docked Hierarchy, Scene, Inspector, Console, Project, Services,
and Script Editor panels around a 2D/3D viewport.

## 2. Start a project

**File → New Project** offers:

- **2D Scene** / **3D Scene** — a camera + a starter object.
- **Platformer** / **Top-Down** — playable, component-wired starters.
- **Coin Collector (full game)** — a complete game you can dissect: a WASD
  player, a follow camera, spinning coins that score on pickup, and a HUD.

Pick **Coin Collector** to see everything working, or **2D Scene** to build from
scratch. Press the green **▶ Play** in the toolbar to run it; **■ Stop** restores
the exact pre-play state.

## 3. Build a scene from scratch

1. **GameObject → Create Sprite** (or right-click the Hierarchy). It appears as a
   colored quad in the viewport.
2. Select it; in the **Inspector** set its Transform, **Color**, **Size**, an
   optional **Texture** (a PNG next to your scene), **Sort Order** (layering),
   and **Flip X/Y**.
3. **Add Component** (searchable) to attach more: `Rigidbody2D` + `Box Collider
   2D` for physics, `Mover`/`Spinner`/`Lifetime` for no-code motion,
   `CameraFollow` to chase a target, `Text` for a HUD, `Audio Source` for sound,
   a `Sprite Animator` for frame animation, or a **Script**.

## 4. Script behavior

Add a **Script (OkayScript)** component and open the **Script Editor** panel
(or edit the file in your own IDE). A script defines `start()`, `update(dt)`, and
optional `on_trigger()` / `on_collision()`:

```c
function update(dt) {
    move(axis_x() * 5 * dt, axis_y() * 5 * dt);   // WASD / arrow movement
    if (key_down("space")) { play_sound(); }
}
function on_trigger() {                            // touched a trigger collider
    prefs_set("score", prefs_get("score") + 1);    // shared, persistent score
    destroy();
}
```

OkayScript has loops, functions, strings, math, input (keyboard + mouse),
`spawn`/`destroy`, `load_scene`, physics `raycast_hit`/`overlap`, component
control (`set_text`/`set_color`/`set_texture`/`flip_x`), and `prefs_*` save data.
Full reference: [`scripting.md`](scripting.md).

## 5. Play, then ship

- **▶ Play** runs the real lifecycle — scripts, physics, audio, particles.
- **File → Build Game** (Ctrl+B): pick a name and folder. The editor writes
  `game.okayscene`, copies the player runtime as `<YourGame>.exe`, and copies
  every texture / sound / animation frame the scene uses.
- The output folder is a self-contained game: double-click `<YourGame>.exe`.
  Move with WASD or the arrow keys; press **Esc** to quit.

## Where to go next

- [`editor.md`](editor.md) — every panel and the Build pipeline.
- [`scripting.md`](scripting.md) — the full OkayScript language + builtins.
- [`visual_scripting.md`](visual_scripting.md) — node-graph scripting.
- [`steam_release.md`](steam_release.md) — shipping on Steam.
