# Wind

Wind blows trees, grass, flags, cables, particles and physics bodies, and it has a sound. One wind field drives all of
it, so you set the wind once. Branch `claude/wind-1`. Everything here is off unless the
scene has a wind object or you turn a setting on: **a scene without a Wind Zone looks and plays exactly as before.**

## Try it (plain steps)

See `docs/try_it/wind-field.md` (generated into `docs/try_it.md`). Short version: open `examples\wind\wind.okayscene` in the editor (File > Open) and press Play.

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

OkayScript builtins (also listed in `docs/scripting.md`):

- `wind_speed([x, y, z])`: speed in m/s at a point (default: where the camera is). `wind_x(x, y, z)`, `wind_y(...)`, `wind_z(...)`: the parts along each axis.
- `wind_set(strength[, yawDegrees])`: the scene's own wind (0 = toward +Z, 90 = toward +X). Makes a "Wind" object if there is none.
- `wind_gust(amount[, perSecond])`: how much it swells (0..1) and how many gusts a second.

The newer script language has a `Wind` object: `Wind.Strength` and `Wind.Direction` (where the script's object is),
`Wind.At(position)` (a vector), `Wind.SetStrength(x)`, `Wind.SetDirection(vec)`, `Wind.SetGust(amount, perSecond)`.
Every OkayScript builtin above is also a Visual Script block automatically (`Fn.wind_speed`, `Fn.wind_set`, `Fn.wind_gust`, `Fn.wind_x/y/z`, in the palette under Weather & Fog; the
builtin-parity test makes that so). Dedicated hand-written blocks with their own plain-words help (like Set Weather) were **not** made.

## What each file was written from (rule 19a)

All of it is our own code written from general knowledge, with no code taken from another project:

- `Environment/WindField.hpp`: value noise on an integer lattice (textbook idea; our own hash constants), a travelling
  pulse train, smoothstep blending.
- `Components/WindZone.hpp/.cpp`: the drag equation (any physics text), a bend that grows with height, a travelling wave
  for the flag, filtered noise for the sound.
- `editor/WindTools.cpp`: Dear ImGui drawing calls, as the rest of the editor.
- Tests: `tests/test_wind_field.cpp` (the maths), `tests/test_wind.cpp` (in a scene).

## Tests and what was run (2026-11-19, Linux, Release build, one machine)

| Test | Result |
|---|---|
| `wind_field` (`tests/test_wind_field.cpp`: noise determinism and range, bad numbers, gust period and travel, seeds, zone shapes and soft edges, radial, priority and add/replace blending, curve, published field, NaN and zero-size edges) | 788 checks, 0 failures (also clean with ASan + UBSan) |
| `wind` (`tests/test_wind.cpp`: zones in a scene, turn to aim, switch off, particles, Wind Drag, Wind Sway tree and flag, Foliage, Cable, Wind Sound, save and load, broken zones) | 80 checks, 0 failures |
| `save_coverage`, `property_registry` (all four new components round-trip), `scene_serializer_audit`, `foliage`, `particles_fx`, `particles_serialize`, `volumes`, `scriptapi`, `api_expand`, `example_scenes`, `examples_work` (runs `examples/wind` headless) | 0 failures each (`property_registry` found a real bug in the first run, fixed) |
| `vs_script_coverage`, `vs_categories`, `vs_palette_categories`, `vs_names`, `vs_palette_levels`, `vs_node_docs`, `vs_clarity`, `vs_registry_effect`, `vs_search_connect` | 0 failures after the wind blocks were placed in Weather & Fog and `docs/visual_script_coverage.md` and the playground `coverage.okayvs` were regenerated |

The example was also loaded and run for 14 simulated seconds by a throw-away program: 22 swaying plant chunks, the box slid about 4 m and
stopped between gusts, leaves alive, no NaN; two software-renderer pictures are in `docs/images/wind-example-t04.png` and `-t12.png`.

## Not done / unverified

- **Not run on Windows, on a GPU renderer, or by eye on a screen.** The editor files (`editor/WindTools.cpp`, `main.cpp`, `LevelTools.cpp`) were only
  syntax-checked with `g++ -fsyntax-only`; the editor program was not linked or started here. The menus, the Scene-view arrows, the compass in the
  Lighting window and the Inspector rows are therefore unseen.
- **Sway is on the CPU.** It works the same in every renderer, but a very large forest rewrites its chunk meshes up to 20 times a second near the
  camera. A GPU vertex-shader version (one uniform, no re-upload) is the better long-term answer and was not written.
- **Not linked to the wind:** trails (`TrailRenderer`), spline followers, `LineRenderer` ropes other than `Cable`, the fog, `Water` (still the
  weather's vector only), Terrain grass.
- **No hand-made Visual Script blocks** with plain-words help for wind (the automatic `Fn.wind_*` blocks exist).
- **No multiplayer sync:** wind is computed from the same scene data and clock on each machine; the clock is not synchronised and there is no
  hook that carries it, so two players can see gusts at different moments.
- **No resize handles** on a Wind Zone in the Scene view (the shape is drawn; size is edited in the Inspector).
- The Wind Sound is one loop with a slow "breathing" tone; it is a simple filtered noise, not a recording, and was not listened to.
- Particles in a Local-space emitter sample the wind at the emitter, not per particle.
- The wind clock is in seconds of scene time (`WindField::Clock()`); it is not saved with the scene, so a loaded game starts at 0.
