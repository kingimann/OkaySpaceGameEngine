# Make your first game

This walks you from an empty editor to a shipped, double-clickable game in a few
minutes. It uses only what ships in the box — no plugins, no external tools.

## 1. Open the engine

Run **`dist/OkayEngine.exe`** (Windows). That single self-contained `.exe`
*is* the editor: docked Hierarchy, Scene, Inspector, Console, Project, Services,
and Script Editor panels around a 2D/3D viewport.

## 2. Start a project

Open the **Launcher**, go to the **Create** tab, enter a project Name and
Location, then pick a template — the editor opens with the project folder and
starting scene already created. Templates include:

- **2D Scene** / **3D Scene** — a camera + a starter object.
- **Platformer** / **Top-Down** — playable, component-wired starters.
- **Coin Collector (full game)** — a complete game you can dissect: a WASD
  player, a follow camera, spinning coins that score on pickup, and a HUD.
- **Snake** and **Brick Breaker** — two more complete games written in
  OkayScript (a tilemap snake, and a paddle that clears a wall of bricks).

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

## Controller movement extras

All three controllers (First-Person, Third-Person, Character Controller 3D)
share these movement niceties:

- **Step Offset** (default 0.35): stairs and curbs up to this height are
  climbed automatically instead of stopping the player. 0 disables.
- **Slope Limit** (default 50 deg): terrain steeper than this can't be
  climbed — the uphill velocity is cancelled and the player slides back
  down. 0 disables.
- **Fall Reset Y** (default -100): falling below this world height teleports
  the player back to spawn (with a console hint about missing colliders).

## Crouch and prone

- **Crouch Key** (default Ctrl) and **Prone Key** (default Z) — tap to toggle
  (**Toggle Stance** on) or hold.
- **Gamepad:** tap the **Stance Pad Button** (default B) to crouch / stand
  up, hold it (**Hold For Prone**, 0.4 s) to drop prone.
- **Allow Prone** switches lying down off entirely.
- **Resize Body** (on by default): crouching and prone really make the
  player shorter — the collider shrinks to **Crouch Height** / **Prone
  Height** (fractions of the standing body), so you can crawl under low
  cover. Standing up is refused while something is overhead; in toggle mode
  the player stands up by itself once clear (from prone it settles for a
  crouch if only that fits).
- Prone moves at **Prone Speed** with a crawl animation; you can't sprint or
  jump while crouched or prone.
- Stance changes take time: by default 0.25 s to crouch or stand from a
  crouch, 0.5 s between crouch and prone, 0.8 s to get up from prone. The
  collider, the camera and the speed limit all follow the body through the
  change, and changing your mind halfway carries on from where the body is.
  Set the times under **Stance changes** on the player's Animator.
- The other movement controllers — Character Controller 3D, Third Person
  Shooter, Top Down, Click To Move and NPC — have the same options under
  their **Crouch / prone** fold (keys, toggle or hold, Allow Prone, Resize
  Body, speeds, heights). Top Down starts with prone off; Click To Move and
  NPC have no keys by default. A script, a cutscene or an AI sets the stance
  with `SetStance(0 stand / 1 crouch / 2 prone)` on any of the controllers.
