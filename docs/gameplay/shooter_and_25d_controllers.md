# Top-Down Shooter and 2.5D Platformer controllers

There are two ready-made player controllers for two popular kinds of game:

- **Top Down Shooter Controller (3D)**: a twin-stick shooter like Enter the
  Gungeon, Nuclear Throne or Helldivers.
- **2.5D Platformer Controller**: a side-on platformer in the style of
  LittleBigPlanet, with depth layers and grabbing.

To start with one:
- **New scene:** pick *Top-Down Shooter* or *2.5D Platformer* when you create a
  project, either in the launcher or in the editor's project screen.
- **Existing scene:** use GameObject → Player → *Top Down Shooter (3D)* or
  *2.5D Platformer (LittleBigPlanet)*.
- **Your own object:** Add Component → Gameplay.

## Top Down Shooter Controller

| Input | Action |
|---|---|
| WASD / left stick | Move |
| Mouse | Aim (the body turns to the cursor) |
| Left mouse / RB | Fire (hold for automatic fire) |
| Space / A | Dash (dodge, invulnerable while dashing) |
| R | Reload |
| Mouse wheel | Zoom the camera |

- **Aim with:**
  - *Mouse cursor*;
  - *Move direction*;
  - *Auto*: the mouse, switching to the stick direction when you play with a gamepad.
- **Shoots:**
  - *Bullets*: built in, no setup needed.
  - *Projectile prefab*: drop a prefab in the slot. Give it a **Projectile 3D**
    to get damage and hits, or a Rigidbody 3D for plain physics.
  - *Hitscan*: an instant ray.
- **Weapon:**
  - fire rate, damage;
  - pellets (above 1 makes a shotgun) and spread;
  - bullet speed, lifetime, size and colour;
  - impact force and muzzle height.
- **Ammo:** set a magazine size, or 0 for unlimited ammo. Reload time and auto
  reload are adjustable.
- **Dash:** a short dodge. It starts at **Dash Speed** and eases down to your
  movement speed over **Dash Time**, then hands straight back to normal movement:
  no skid, and if you keep moving you carry on at walking speed. The body ducks
  and faces the dash direction while it lasts. **Dash Cooldown** starts when the
  dash ends. Key and pad button are both settable (Space / A by default).
- **Animation** (with a Character): follows what the body is actually doing.
  Idle, walk or run is picked from the real ground speed; the stride is laid
  out for that speed so the feet stay planted; strafing side-steps and the legs
  step backwards when you walk away from where you aim.
  Switching between animations crossfades instead of snapping.
- **Camera:** a high follow camera that leans toward the cursor so you see where
  you shoot, kicks a little on every shot, and zooms with the wheel.
- **Script events** (on the player's script): `on_fire`, `on_reload`,
  `on_reloaded`, `on_empty`, `on_dash`.

Damage goes to the **Health** stat on the object hit, or on one of its parents.
Give enemies a Health stat and they can be shot.

## Projectile 3D

A bullet, rocket or arrow.
- It flies along its velocity. Each frame it checks the path ahead, so fast shots
  can't skip through thin walls.
- It damages the Health it hits and pushes physics objects.
- It calls `on_hit` on its own script.
- **Options:**
  - *Gravity*: 0 flies straight; 1 drops like a grenade.
  - *Pierce*: extra targets it passes through. Walls always stop it.
  - *Bounce*: ricochets off walls.
  - *Lifetime*.

## 2.5D Platformer Controller (LittleBigPlanet style)

| Input | Action |
|---|---|
| A / D, stick | Run (the stick walks at the speed it's pushed) |
| Shift, stick pushed all the way | Sprint |
| Space / A | Jump (hold for higher, tap for a hop) |
| W / S, stick or d-pad up / down | Move to the layer behind / in front; while hanging, climb up / let go |
| E / RB (hold) | Grab: drag or push a physics object, or catch a ledge in the air |

- **Depth layers:** the level is 3D, seen from the side. The player stays on one
  of a few depth **layers** (3 by default, like LittleBigPlanet), spaced *Layer
  Spacing* apart along the depth axis (Z).
  - Layer 0 is the front, nearest the camera, and sits at *Front Layer Z*.
  - **Use Mine** in the Inspector puts the start layer where the player is
    standing.
  - A layer switch only happens when there is room on the other layer, so
    walls on one layer are walked past on another.
  - If something gets in the way while you slide over, you go back to the
    layer you came from instead of getting stuck between two.
- **Moving:**
  - Ledges up to *Step Height* (35 cm by default: kerbs, stairs) are walked up
    without jumping.
  - Pressing into a wall in the air doesn't stick you to it; you slide down.
  - Trigger volumes (pickups, checkpoints) are never treated as ground or walls.
- **Jump:**
  - variable height (*Short Hop*), extra *Fall Gravity* for snappy landings;
  - coyote time, a jump buffer, and double jumps.
- **Grab:**
  - Hold the grab key next to a physics object to drag or push it. You keep
    facing it as you pull.
  - Hold it in the air when your hands can reach the top edge of something
    solid (a ledge, a bar) to catch it: you hang with your hands on the edge.
    A bare wall face isn't something to hang from.
  - While hanging: **W / up** climbs onto the ledge (if you fit on top), **S /
    down** or letting go of grab drops, jump jumps off.
- **Camera:** a side camera with look-ahead and a gentle vertical follow.
  - It pulls back a little at speed.
  - It stays centred on the layers, so switching layers doesn't swing the view.
    Turn on *Camera Follows Layer* to change that.
- **Animation:** with *Drive Animation* on, the character idles, walks and runs
  at the speed it actually moves with the feet planted (no running on the spot
  against a wall), jumps and falls in the air, hangs with its arms up from a
  ledge and scrambles while climbing.
- **Script events:** `on_jump`, `on_land`, `on_layer`, `on_grab`, `on_release`, `on_climb`.

### Building a 2.5D level

- Make platforms with a depth of about one layer (*Layer Spacing*), centred on a
  layer's Z: `Front Layer Z - layer × Layer Spacing`.
- Build the floor deep enough to cover every layer.
- The *2.5D Platformer* template shows each idea:
  - a wall you get past by hopping back a layer;
  - steps and a ledge;
  - a back-layer platform;
  - a crate to drag;
  - a bar to hang from.
