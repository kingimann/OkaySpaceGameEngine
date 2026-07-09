<!-- (world-space UI is documented in docs/ui_world_space.md) -->
# Custom character animations (keyframe clips)

Besides the built-in animations (idle / walk / run / wave / jump / crouch /
gestures / emotions), you can make **your own** animations for a `Character`
without writing C++ — just describe a few poses over time in a small text file
and play the clip by name. The engine interpolates between your keyframes each
frame.

## The format

A `.okayanim` file holds one or more `clip` blocks:

```
# arms.okayanim
clip wave loop            # name, then "loop" (default) or "once"
key 0.0                   # a keyframe at t = 0 seconds
  r_uparm 0 0 -150        # <bone> <x> <y> <z>   (Euler degrees, local)
key 0.35
  r_uparm 0 0 -150
  r_fore  0 0 -30
key 0.7
  r_uparm 0 0 -150

clip bow once
key 0.0
key 0.6
  torso 40 0 0            # bend forward at the waist
  head  -20 0 0           # ...keep looking ahead
```

- A keyframe only lists the bones it moves; everything else stays at rest.
- Times are seconds; author keyframes in increasing time order. The clip's length
  is its last keyframe.
- `loop` repeats; `once` holds the final pose.

### Bone names

`hips`, `torso`, `head`, `l_uparm`, `l_fore`, `l_hand`, `r_uparm`, `r_fore`,
`r_hand`, `l_thigh`, `l_shin`, `l_foot`, `r_thigh`, `r_shin`, `r_foot`
(case-insensitive). `+x` pitches a limb forward, `+z` spreads it out to the side.

## Playing a clip

No code — set two fields on the Character:

- **clipsFile** → `arms.okayanim`
- **autoPlayClip** → `wave`

It loads and plays on Start.

From C++ / a script-driven flow:

```cpp
auto* ch = obj->AddComponent<Character>();
ch->LoadClipsFromFile("arms.okayanim");   // or LoadClips(textString)
ch->PlayClip("wave");                      // play by name (resets its clock)
// ch->IsPlayingClip(); ch->PlayingClip();
ch->StopClip();                            // back to the built-in `anim`
```

You can also build a clip in code (`AnimClip` + `AddClip`) if you'd rather
generate it. A playing clip drives the whole body and overrides the built-in
`anim`; `animSpeed` scales clip playback too.

# Imported model animations (FBX / GLB)

Importing an animated model (drag-drop, double-click in Project, or the
Import dialog) brings in its skeleton as objects, a `SkinnedMesh` that
deforms with the bones, and a **Model Animator** on the import root holding
every animation in the file as a named clip.

## In the editor

Select the model with the **Animation tab** open:

- **Clip picker + transport** — play, pause, and scrub any clip in EDIT mode
  (the scene pose isn't modified; press the toolbar Play to run it for real).
- **Rename / Duplicate / Delete / Copy / Paste** — manage the clip library.
  Paste targets another model with the same bone names (Mixamo-style rigs).
- **Split…** — carve a time range into a new named clip, so a single-take
  file becomes separate idle / walk / attack clips.
- **Events** — named markers on the timeline (footsteps, hit windows).
- **Dope Sheet** — per-node keys, event markers, click to seek.

The Inspector's **Model Animator** section holds playback settings:

- **Blend** — crossfade seconds when switching clips (0 = snap).
- **Root Motion** — move the OBJECT by the clip's root-bone ground
  translation instead of letting the bone slide inside the model; pick the
  root bone or leave it on auto.
- **Locomotion** — auto-switch idle/walk/run clips from how fast the object
  moves (switches crossfade with the Blend time).

## From a script

The Character clip builtins also work on an object with a Model Animator on
itself or an ancestor:

```
play_clip("walk")          # switch clips (crossfades over Blend seconds)
play_clip_once("attack")   # play once, then return to the previous clip
name = playing_clip()      # current clip name
ev = anim_event()          # next fired event name ("" if none)
if clip_finished() { ... } # a one-shot / non-looping clip reached its end
d = clip_duration("walk")  # clip length in seconds
```

## Animation state machine

For anything beyond locomotion, add an **Anim State Machine** component next
to the Model Animator (Add Component > Animation). Each **state** names a
clip (with speed/loop overrides); **transitions** move between states when
their condition passes — a clip finishing, a float parameter compared to a
value, a bool, or a one-frame trigger — each with its own crossfade time.

Gameplay sets the parameters:

```
anim_set_float("speed", velocity)   # feeds Float > / Float < conditions
anim_set_bool("armed", 1)           # feeds Bool true / Bool false
anim_trigger("attack")              # fires Trigger transitions (consumed once)
s = anim_state()                    # current state name
anim_goto("Dead")                   # force a state directly
```

A typical setup: `Idle -> Run` (speed > 2), `Run -> Idle` (speed < 1),
`Idle/Run -> Attack` (trigger "attack", loop off), `Attack -> Idle`
(On Clip End). The machine drives the Model Animator, so blending, root
motion and events all keep working.

## IK on imported rigs

The whole IK suite works on imported skeletons, and every solve runs AFTER
animation (state machine, blending, crossfades), correcting the final pose:

- **Look-At IK** — head tracking. **Auto-Detect Chain** maps spine → neck →
  head from the bone names; set a target object (the player, the camera).
- **Aim IK** — point one bone at a target (weapon, turret, head). **Auto:
  Head** finds the head bone.
- **Limb IK** — arm reach/grab. **Auto: L Arm / R Arm** map
  shoulder → forearm → hand (fingers excluded).
- **Chain IK** — long chains (tails, tentacles), FABRIK/CCD.
- **Foot IK** — see below.

## Foot IK on imported rigs

Add a **Foot IK** component to the import root (Add Component > Animation)
and press **Auto-Detect Bones** — it maps the usual leg-bone names (Mixamo
"LeftUpLeg / LeftLeg / LeftFoot", thigh/shin/calf variants, `_l`/`_r`
suffixes) automatically. In Play mode each foot raycasts the ground and a
two-bone solve plants it on slopes and steps; turn on **Adjust Pelvis** so
the body lowers to reach lower ground, and **Align To Slope** to tilt the
sole. The solve runs after all animation (including the state machine and
locomotion blending), so it corrects the final pose.

Corrections are **smoothed**: each frame moves the foot a fraction of the
way to its planted target (the **Smoothing** field, per second — 0 =
instant), so stepping over a ledge or stair edge eases the foot down
instead of popping it. The pelvis shift is smoothed the same way. The
one-click **Humanoid (foot IK)** player ships with the full plant setup on:
pelvis adjust, plant-down, slope align and smoothing.

On the built-in blocky Character, Foot IK **wires itself**: drop the
component next to a Character and the leg bones and pelvis hook up
automatically (and re-hook after save/load).

## Built-in animation polish

The procedural walk/run cycles roll each **foot** heel-to-toe through the
stride and counter-sway the **shoulders** against the hips, and any
jump that lands into idle/walk/run plays a short **landing recovery** —
knees flex, the body dips, then springs back over ~0.3s. Standing still,
the character **fidgets**: a slow side-to-side weight shift plus an
occasional glance to one side. All automatic, on every Character.

**Look-At IK** and **Aim IK** also have a **Smoothing** field now: the
head/bone swings smoothly onto a new target instead of snapping the frame
it changes (Look-At defaults to a natural head-turn speed; Aim defaults to
instant for turrets — raise it for organic tracking).

## Rig any model in the editor (auto-rig)

A model with NO rig and NO animations can be rigged right in the editor:
right-click it in the Hierarchy and choose **Rig Model (Humanoid)**. Its
meshes are merged and every vertex is bound to the engine's humanoid
skeleton automatically (nearest-bone weights) — and from that moment the
model plays **everything the built-in character can**: idle/walk/run
cycles, crouch, jump, the emotions and gestures, your authored keyframe
clips, movement states from any controller, and live preview in the
**Animation window**. The model should stand upright, facing +Z, in a
T/A/rest pose; texture and UVs are kept, and the bind saves with the
scene. **Unrig** on the Character component restores the blocky body.

For models that ALREADY have a skeleton + clips (Mixamo, Meshy GLB/FBX),
you don't need this — import keeps their real rig and animations; drop
them on a player (below) or drive them with the Model Animator.

## Previewing & editing imported clips (Unity-style)

Select any imported model (or any of its bones) with the **Animation
window** open: pick a clip, press **Play** or scrub the timeline, and the
model animates in the inset preview. Tick **Preview in Scene view** to see
it animate on the model in the Scene view itself while you scrub — the
original pose is restored when you untick it or close the window.

Clip tools: **Rename**, **Duplicate**, **Delete**, **Copy/Paste** (moves a
clip to another model with the same bone names), **Split...** (cut a long
take into idle/walk/attack pieces), **Reverse** (play backwards) and
**Scale Time** (bake slow-motion / speed-up — keys and event markers are
re-timed). **Events** adds named markers that fire during play (footsteps,
hit windows) — read them in scripts with `anim_event()`.

## Custom character models (drag & drop)

Any imported model can BE your playable character — no rigging setup:

- **Drop a model asset** (.glb/.gltf/.fbx/.obj...) from the Project panel
  **onto a player in the Hierarchy** (any object with a controller or a
  Character), or press **Set Character Model...** on the Character /
  Third-Person Controller in the Inspector.
- The model imports as a child of the player, auto-sized to the
  character's height, feet grounded on the capsule's origin, and turned
  to face the way the body faces.
- Its animation clips are wired to locomotion automatically: clips named
  *idle/stand*, *walk*, and *run/sprint/jog* map to the auto
  idle/walk/run switching (smooth 1D blend), driven by how fast the
  player actually moves.
- The default blocky body is hidden (the Character component is disabled
  — re-enable it in the Inspector to get it back). All of it saves with
  the scene.

## Tips

- **Meshy / Mixamo FBX**: animations import via Assimp. For the richest
  results (full skins + all takes) prefer the **GLB** export when available.
- **Materials import too**: albedo, normal, specular/gloss and AO maps
  (external files or FBX-embedded), plus diffuse/emissive colors and
  shininess/metallic/roughness factors, land on each mesh's renderer
  automatically — a downloaded model lights the way it did in the store
  preview.
- Multi-take files import one clip per take; single-take files can be cut
  apart with **Split…**.
