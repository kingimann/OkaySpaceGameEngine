# Wind

Wind blows trees, grass, flags, cables, particles and physics bodies, and it has a sound. One wind field drives all of
it, so you set the wind once. Added in version 0.x of this branch (`claude/wind-1`). Everything here is off unless the
scene has a wind object or you turn a setting on: **a scene without a Wind Zone looks and plays exactly as before.**

## Try it (plain steps)

See `docs/try_it/wind-field.md` (generated into `docs/try_it.md`). Short version: File > Examples is not used; open
`examples\wind\wind.okayscene` in the editor and press Play.

## What was already there, and the gap

Searched on 2026-11-19: `grep -ri wind engine/ editor/ docs/ examples/`, the feature map (2.4 Weather, 2.7 Particles, 11
Volumes) and the `claude2/*` branches (no wind work; the only `wind` branch name is `claude2/launcher-own-wav-window`,
which is about a window).

| Existing | Where | Gap |
|---|---|---|
| One global wind vector from the weather (`WindVector()`, set by `Weather.SetWind` / `weather_set` / Lighting > Weather > Wind, plus storm gusts) | `Environment/Wind.hpp`, `Environment/Weather.hpp` | Same everywhere, no direction control beyond three numbers, no noise, no zones, no time curve, no way to ask "what is the wind at this point". Kept: it is still the base the field starts from. |
| Particles drift with it (`ParticleSystem::windInfluence`, saved as `particleswind`) | `Components/ParticleSystem.hpp` | Not in the Inspector (only in the property list), no label; every particle felt the same wind. Now: an "Affected by wind" slider and each particle samples the field where it flies. |
| Water waves follow it | `Components/Water.hpp` | Unchanged (still uses the weather's vector). |
| Volume > Force "wind, draught, updraft" | `Components/Volume.hpp` | Pushes physics bodies only, with a steady acceleration; nothing else sees it. Kept as it is (it is a push, not a wind). |
| Trees / grass / foliage | `Components/Foliage.hpp` | Stand still. Now: "Sway in the wind". |
| Cable (rope with sag) | `Components/Cable.hpp` | Hangs still. Now: "Sway in the wind". |
| Rigidbody 3D, Ragdoll | `Physics/Rigidbody3D.hpp`, `Components/Ragdoll.hpp` | Ignore the wind. Now: the Wind Drag component. |
| Audio mixer, procedural clips (`AudioClip::Noise`, `Sine`) | `Audio/` | No wind sound. Now: Wind Sound makes its own loop. |
| Weather rain / snow | `Environment/Weather.hpp` | Rain already leans with the wind (through the particles). |

Not done (see the end): trails, spline followers, fog linked to wind, multiplayer sync, Visual Script nodes, the GPU
vertex-shader sway.

## The pieces

| Piece | Add it with | What it does |
|---|---|---|
| **Wind Zone** (`okay::WindZone`) | GameObject > Effects > Environment > Wind (whole scene) / Wind Zone / Fan / Calm Zone, or Add Component > Environment > Wind Zone | Blows. Three kinds: *Everywhere*, *Direction* inside a box, ball or tube, *Pushes outward* from the middle (fan, blast, negative sucks in). |
| **Wind Drag** | Add Component > Environment > Wind Drag | A Rigidbody 3D (or all the bones below it, for a ragdoll) is pushed by the wind. Off unless the component is there. |
| **Wind Sway** | Add Component > Environment > Wind Sway | A mesh leans (tree, bush, grass patch) or waves (flag). |
| **Wind Sound** (`WindAmbience`) | GameObject > Effects > Environment > Wind Sound | A looping whoosh made by the engine; louder in stronger wind. |
| Foliage "Sway in the wind", Cable "Sway in the wind", Particle "Affected by wind" | Their own Inspectors | The same wind, for those components. |
| Lighting window > **Wind** | Window > Lighting | The scene's wind: compass direction, strength, gusts, swirl and a live arrow. |

## How the wind is worked out

`engine/include/okay/Environment/WindField.hpp` (plain maths, no scene, unit tested). At a position `p` and time `t`:

1. Start from the weather's wind (`WindVector()`).
2. Take every live wind source and sort them by **priority** (lowest first; ties keep creation order).
3. Each source has a **coverage** at `p`: 1 for *Everywhere*; for a zone, how deep inside its shape `p` is, 0 outside, rising
   smoothly to 1 over the *Soft edge* fraction.
4. A source's own wind is `direction * strength`, times
   - the **time curve** (optional `seconds:multiplier` keys that repeat),
   - the **gust**: `1 + gustAmount * (2 * pulse - 1)`, where `pulse` is a smooth repeating pulse of
     `(t - distance along the wind / gustSpeed) * gustFrequency`. So gusts repeat exactly every `1 / gustFrequency`
     seconds and *travel* downwind at `gustSpeed`; with a gust speed of 0 the whole zone gusts together,
   - the **weaker toward the edge** factor (*Weaker toward the edge*),
   plus **swirl**: our own value noise (hash of the lattice corner, smooth blend) in x, y and z, a fraction of the strength.
5. The result is blended: *replace* (default for zones: `out = lerp(out, wind, coverage)`, so a Calm Zone shelters you) or
   *add* (`out += wind * coverage`, default for the scene's wind).

Everything is deterministic: the same seed and time give the same wind on every machine (the noise uses integer hashing,
no random numbers). Bad numbers (NaN, zero size, zero direction) give "no wind", never NaN.

Components publish their sources once a frame (`WindZone::UpdateAll`, called by `Scene::Update` right after the weather);
`WindField::At(position)` reads them. The wind clock (`WindField::Clock()`) advances with the scene's update.

## Consumers

- **Particles** (`windInfluence`, label "Affected by wind"): with Wind Zones in the scene each particle samples the wind
  where it is (in world space; local-space emitters sample at the emitter). With none, the old code runs unchanged.
- **Wind Drag**: force from the drag equation `F = 1/2 * 1.2 kg/m^3 * Cd * A * v^2` on the wind relative to the body.
  Capped so one physics step cannot overshoot the wind speed (no jitter on light bodies). Under 0.5 m/s relative wind it
  does nothing, so resting bodies can still sleep.
- **Wind Sway** and **Foliage "Sway in the wind"**: done on the CPU, so it works in the software renderer, OpenGL, D3D11
  and D3D12 the same way. The component keeps a copy of the mesh and rewrites the vertices up to 20 times a second:
  each vertex moves along the wind by `amount * lean * weight^stiffness` (weight = height up the plant, or distance along
  a flag), with a small noise shiver; the far edge of a flag gets a travelling wave and the flag turns downwind. Far from
  the camera (default 80 m) it stops. Foliage chunks get per-vertex weights from each plant's height times its size (a
  4 m tree moves "amount" metres, grass barely), measured at the chunk's centre.
  *Cost:* a big forest means rewriting its chunk meshes. Lower *Updates per second* or *Stop when farther than*, or leave
  *Sway in the wind* at 0 on layers that do not need it.
- **Cable**: the middle swings sideways downwind, none at the ends.
- **Wind Sound**: `WindAmbience::MakeClip` writes a 6 s clip of white noise (our own xorshift) through two low-pass filters
  whose cut-off breathes three times per loop, crossfaded so the end flows into the start. Volume follows
  `smoothstep((speed - quietBelow) / (fullAt - quietBelow))`, smoothed over 1.5 s, pitch rises a little with the wind.
  A plain 2D sound on the SFX group; it uses the normal mixer.

## Saving

Scene records `windzone`, `winddrag`, `windsway`, `windambience`; Foliage gained `wind=`, Cable `swing=`. Old scenes load
with all of them off. Properties are registered with the property registry (Inspector, scripts, Saveable).

## Scripting

OkayScript: `wind_strength()` (the wind speed at the camera), `wind_dir()` is not a single value, so use
`wind_x(x, y, z)`, `wind_y(...)`, `wind_z(...)` or the API object below. API object (`Wind`):

- `Wind.Strength` (read): speed at the scene's origin... see `docs/scripting.md` after the next release for the final list.

(The exact script names are listed in the "Scripting" section at the end of this file once built.)

## What each file was written from (rule 19a)

All of it is our own code written from general knowledge, with no code taken from another project:

- `Environment/WindField.hpp`: value noise on an integer lattice (textbook idea; our own hash constants), a travelling
  pulse train, smoothstep blending.
- `Components/WindZone.hpp/.cpp`: the drag equation (any physics text), a bend that grows with height, a travelling wave
  for the flag, filtered noise for the sound.
- `editor/WindTools.cpp`: Dear ImGui drawing calls, as the rest of the editor.
- Tests: `tests/test_wind_field.cpp` (the maths), `tests/test_wind.cpp` (in a scene).

## Not done / unverified

See the final section of this file, kept up to date at the end of the work.
