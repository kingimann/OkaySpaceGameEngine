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
- Optional header words: `linear` (default), `smooth` or `step` easing, and
  `override` (movement can't interrupt the clip, see below).
- `speed 1.5` plays the clip faster; `length 1.2` sets a loop's length past its
  last key, so the last key eases back into the first (a seamless loop).
- Each bone turns along the short arc between keys (the rotations are slerped),
  so -150° to 150° swings 60° rather than spinning the long way round.
- `root x y z` inside a key moves the whole body (in the rig's units, before the
  height slider scales it): a walk's hip bob, or a crouch's `root 0 -0.17 0` drop.
  A clip with no `root` lines leaves the body where the animation puts it.

### Bone names

`hips`, `torso`, `head`, `l_uparm`, `l_fore`, `l_hand`, `r_uparm`, `r_fore`,
`r_hand`, `l_thigh`, `l_shin`, `l_foot`, `r_thigh`, `r_shin`, `r_foot`
(case-insensitive). `+x` pitches a limb forward, `+z` spreads it out to the side.

## Playing a clip

No code — set two fields on the Character:

- **clipsFile** → `arms.okayanim`
- **autoPlayClip** → `wave`

It loads and plays on Start. A looping clip played this way (or with `PlayClip`)
gives way to movement: it stops as soon as the character walks, runs or jumps,
and a `once` clip stops after it finishes if the character is moving. A clip
marked `override` keeps playing until you stop it.

Clips saved with the scene win over the file: on Start the file only adds clips
the scene doesn't have. Tick **Linked to file** (`clipsLinked`) to make the file
the source instead; Play then loads it over same-named clips, and the editor's
Apply re-saves it. A relative `clipsFile` is relative to the project folder (the
editor saves it that way); paths with spaces are fine.

### Use your clip for idle, walk, run, jump, fall or crouch

Set **clipIdle**, **clipWalk**, **clipRun**, **clipJump**, **clipFall** or
**clipCrouch** to a clip's name and the character plays it in that movement state
instead of the built-in animation. In the editor's Animation tab, **Use this clip
for** does the same with one click. Leave a state unbound to keep the built-in.

### The Animation tab

Pick a clip in the **Clip** dropdown to edit it: one of the character's, a new
one, or "Walk (from built-in)" (Idle, Walk, Run, Jump) to start from a copy of a
built-in animation, sampled into keys you can tweak. Key Pose, deleting a key,
bone nudges, renames and Apply all undo with Ctrl+Z. **Apply** adds the clip to
the character without playing it; tick **Play on start (overrides movement)** to
make it the start animation.

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
  root bone or leave it on auto. Tick **+ Vertical** to also apply the
  root's Y motion to the object — for authored jumps, climbs and vaults
  where the clip itself carries the rise (leave it off for ordinary
  locomotion so the capsule/physics keeps owning height).
- **Locomotion** — auto-switch idle/walk/run clips from how fast the object
  moves (switches crossfade with the Blend time).

## From a script

The Character clip builtins also work on an object with a Model Animator on
itself, a child (a player whose model sits under it) or a parent. The Play Clip
visual-script node does the same. A played clip holds: Drive By Movement and a
Humanoid Retarget leave it alone until `stop_clip()`. With an Animation State
Machine on the model, the state machine picks the clips and Drive By Movement
waits.

```
play_clip("wave")          # switch clips (crossfades over Blend seconds) and hold it
stop_clip()                # let movement drive again
play_clip_once("attack")   # play once, then return to the previous clip
name = playing_clip()      # current clip name
ev = anim_event()          # next fired event name ("" if none)
if clip_finished() { ... } # a one-shot / non-looping clip reached its end
d = clip_duration("walk")  # clip length in seconds
```

## Animation state machine

For anything beyond locomotion, add an **Animator** component next to the
Model Animator, or onto a built-in Character (Add Component > Animation). On a
Character it plays the Character's own clips (the Animation tab's) and the
default humanoid clips, and while it has states its pose replaces the built-in
animation. Each **state** names a
clip (with speed/loop overrides); **transitions** move between states when
their condition passes — a clip finishing, a float parameter compared to a
value, a bool, or a one-frame trigger — each with its own crossfade time.

Build it visually in the **Animator window** (View > Animator, or the
"Open Animator Graph" button on the component): states are draggable
nodes on a canvas (positions save with the scene), right-click a node to
**Add Transition** (then click the target state), set the entry state, or
delete; click a transition arrow to edit its condition/parameter/blend in
the side panel; right-click empty canvas to add a state. In Play mode the
**active state lights up** so you can watch the machine run.

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
(On Clip End). The machine drives the Model Animator (or the Character), so
blending, root motion and events all keep working.

## IK on imported rigs

The whole IK suite works on imported skeletons, and every solve runs AFTER
animation (state machine, blending, crossfades), correcting the final pose:

- **Look-At IK** — head tracking. **Auto-Detect Chain** maps spine → neck →
  head from the bone names; set a target object (the player, the camera).
- **Aim IK** — point one bone at a target (weapon, turret, head). **Auto:
  Head** finds the head bone.
- **Limb IK** — arm reach/grab. **Auto: L Arm / R Arm** map
  shoulder → forearm → hand (fingers excluded). The bones turn to reach, so
  they keep their length and the mesh bends with them. A **Weight** below 1
  bends the limb part of the way.
  - **Pole Mode** sets where the elbow or knee points when there's no Pole
    object. **Auto** keeps the animation's own bend; **Character** turns
    with the character; **World** is a fixed direction.
  - **Softness** eases the last stretch to full reach, so the limb doesn't
    snap straight.
  - **Match Target Rotation** without a target object uses **Target
    Rotation**.
  - A broken target (NaN) leaves the animated pose alone.
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

## Built-in locomotion: how the blocky character moves

The built-in walk, run and crouch-walk are not fixed loops — every frame the
legs are solved with two-bone IK from a stride laid out for **how fast the
body really moves over the ground**:

- **Feet stay planted.** A planted foot travels back under the hips at
  exactly the body's speed, so it holds its spot on the floor instead of
  skating. Faster means quicker *and* longer steps; above a jog the stance
  shortens and both feet leave the ground (a real run with a flight phase).
- **Hips ride the stride.** They drop a little while a planted foot is far
  ahead or behind (so the legs reach the ground instead of the feet
  floating) and arc up through a running flight. Knees always bend forward.
- **Heel-to-toe.** Walkers land on the heel and roll off the toe; runners
  land on the forefoot and kick the heel up behind before the knee drives
  through. Arms swing against the legs, wider and with more elbow bend the
  faster you go; the chest leans into speed and the head stays level.
- **Strafing and backpedalling.** The hips and legs turn toward the travel
  direction (up to 75°, the chest keeps facing forward) and side-step the
  rest; travelling backwards steps backwards.
- **Air and stances.** Jump (lead knee up, trailing leg, arms driving),
  **Fall** (legs reaching for the ground, arms out for balance), a landing
  absorb, **Crouch** (hips back and down, knees over the toes, hands in
  front), **Crouch Walk**, **Prone** with a **crawl** (elbows pull in turn,
  the opposite knee drags up) and **Hang** (arms up, legs loose).

The First-Person, Third-Person, Top-Down Shooter and 2.5D controllers report
the speed and direction automatically and pick the state from what the body
is actually doing: pressed against a wall it stands instead of running in
place, it idles the moment it stops, and the fall pose only shows after a
real drop (a step down keeps the stride). Standing still, the character
**fidgets**: a slow weight shift and an occasional glance to one side.

### The Animator that plays it

Every new Character (the player templates, GameObject > 3D > Character, Add
Component > Character, rigging a model) comes with an **Animator** holding the
default graph, and a scene saved before it existed gets one when it loads. Its
states — Idle, Walk, Run, Crouch, CrouchWalk, Prone, Crawl, GetUp, Drop, Jump,
Fall, Land, Hang and the gestures (Wave, Point, Clap, ThumbsUp, Salute, WaveBoth, Cheer, Sad,
Angry, Think) — play the **default humanoid clips**, baked from the motion
above: the walk, run and crouch-walk at several speeds in eight directions,
each a 2D blend tree on `gaitX`/`gaitZ` that keeps pace with the ground
(`matchSpeed`: a tree whose motions sit at their speeds of travel plays as
fast as its speed parameter over the blended motions' speed). Every Character can play these clips by name; they aren't
copied into the scene, and a clip of your own with the same name wins. A clip
bound with clipIdle / clipWalk / ... plays in that state instead.

Stance changes follow the Animator's **Stance changes** times: standing and
crouching crossfade over the stand <-> crouch time, crouching and prone over
the crouch <-> prone time. Standing still, going prone drops down through a
kneel (**Drop**) and standing up gets up through hands-and-knees and a kneel
(**GetUp**, over the prone -> stand time); on the move the stride blends
straight into the crawl and back. **Crawl** goes forward, backward and to
either side as the body moves. Foot IK is off while lying down. Any change of
mind blends from wherever the body is. Editing the times retimes the default
graph's blends.

A movement controller feeds the graph's parameters every frame: `speed`
(m/s), `run`, `crouch`, `prone`, `stance` (0 stand .. 1 crouch .. 2 prone,
easing through each stance change over the Animator's **Stance changes**
times), `airTime` (seconds off the ground) and `verticalSpeed`; the body itself sets `gaitX`, `gaitZ` (direction of travel,
x = right, z = forward) and `gaitSpeed`. Crossfades: 0.2 s between ground
states, 0.1 s into the air, 0.08 s back onto the ground, where a standing
landing plays **Land** and a moving one gives at the knees mid-stride. A blend
tree entered from another one (walk → run → crouch-walk) keeps the stride's
phase, so the feet stay in step.

`anim` keeps working: it reads the state playing (Walk = 2, Land = 1), and
writing it (a script's `set_anim`, the network, a controller that sets it
itself) moves the Animator to that state and keeps it there until a movement
controller reports again. `anim` 0 hands the body back to its own pose; a
script's held gesture (`play_clip("Wave")`) holds the state until released.
Disable the Animator and the built-in animation plays the body as before.

In the Inspector the Animator lists its states, what each plays (a clip, or a
blend tree's clip count) and its transitions (folded; click to open). On a
Character each state also has **Preview** (plays that clip live in the Scene
view, in edit mode and in Play) and **Edit clip** (opens a copy in the
Animation tab: Apply there saves it on this Character, where it replaces the
default clip of the same name). While a preview is running, a transport under
the row pauses, scrubs, sets a preview speed (it starts at the state's speed
and is not written back) and loops. A blend tree previews one motion from a
dropdown, starting at the middle, straight-ahead clip. The same Preview sits
on the selected state in the Animator graph, on each clip in the Character's
clip list, and on a Model Animator's clip. Closing the Animation window does
not stop it. It stops when you press Stop, start another preview, delete the
target or change scene, select something that isn't the target, its children
or its parents, or toggle Play/Stop. **Open Animator Graph** edits the graph;
**Reset to default** puts the default graph back (Undo restores yours).

### Driving it yourself

Driving a Character yourself (an NPC, a cutscene): set `anim` and, for the
locomotion states, `moveSpeed` (m/s) and `moveAngle` (degrees, + = to the
body's right) every frame — or call `DriveLocomotion` with the body's
velocity and facing. Without a reported speed the cycle plays at a natural
pace on the spot. `DriveLocomotion` measures how far the body really moved;
set `ignoreAxis` to leave out motion that isn't walking (the 2.5D controller
ignores Z, so hopping between depth layers doesn't play the run cycle).

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

## One-click animation preview

Select a player and press **Preview** on a state in the Animator inspector
(or on the selected state in the Animator graph). It plays in the Scene
view with the Animation window closed, and it also plays during Play —
while the preview is on it overrides the running Animator, including while
the game is paused. Scrub the time (that pauses playback), change the
speed, or turn looping off to hold the last frame. A blend tree has a
motion dropdown instead of mixing several clips; the running machine does
that mix. Stopping, selecting something else, or pressing Play/Stop puts
the pose back exactly, and does not mark the scene dirty.

The Animation window's built-in grid still previews **Idle, Walk, Run,
Jump, Crouch, Prone, Wave, Point, Clap, Thumbs Up, Salute, Wave Both,
Cheer, Sad, Angry, Think, Crouch Walk, Fall, Hang** live in the Scene
view. That grid keeps playing if you close the window; selecting a
different object stops it. A **Speed** slider and **Stop Preview** sit
under the grid, and **Set as start animation** makes the previewed state
the one the character starts with in Play mode.

It works on the blocky character, a separated part rig, an auto-rigged
model, and a retargeted imported model. A Model Animator with no Character
has its own **Preview** on the selected clip.

When a character also has imported clips (a swapped model), the Animation
window shows **two tabs — Character and Imported Clips** — so both the
built-in system and the file's own takes are always reachable.

## Previewing & editing imported clips

Select any imported model (or any of its bones) with the **Animation
window** open: pick a clip, press **Play** or scrub the timeline, and the
model animates in the inset preview. Tick **Preview in Scene view** to see
it animate on the model in the Scene view itself while you scrub — the
original pose is restored when you untick it or close the window.

Clip tools: **Rename**, **Duplicate**, **Delete**, **Copy/Paste** (moves a
clip to another model with the same bone names), **Split...** (cut a long
take into idle/walk/attack pieces), **Reverse** (play backwards) and
**Scale Time** (bake slow-motion / speed-up — keys and event markers are
re-timed) and **Bake In-Place** (permanently strip the root bone's ground
travel out of the clip — turns a downloaded travelling walk into an
in-place one for controller-driven characters; the non-destructive
alternative is the **Animate in place** toggle on the Model Animator).
**Events** adds named markers that fire during play (footsteps,
hit windows) — read them in scripts with `anim_event()`.

## Air states for imported characters (jump / fall / land)

Locomotion on the Model Animator also handles the air: map **Jump**,
**Fall** and **Land** clips (auto-mapped by name when you set a character
model) and the machine watches the capsule's VERTICAL motion — launching
plays Jump, past the apex Fall loops, and touchdown plays Land once before
idle/walk/run resumes. It works with every controller, no wiring. In
in-place mode the jump clip's own root rise is stripped too, so the
controller's jump arc isn't doubled.

## Crouch and prone for imported characters

The Model Animator also follows the player's crouch and prone. It has a slot
for each stance clip under **Crouch / prone clips**: **Crouch** and **Crouch
walk**, **Prone** and **Crawl**, and **Get up** (played once when the player
rises from prone). When you set a character model, clips are mapped by name:
- *Crouch Idle* fills Crouch;
- *Crouched Walking* or *Sneak* fills Crouch walk;
- *Prone Idle* fills Prone;
- *Crawling* fills Crawl;
- *Getting Up* or *Stand Up* fills Get up.

Clips that move between stances, like *Standing To Crouched*, aren't
mapped as idle or walk. Locomotion switches to the stance's clips (still and
moving, blended by speed), crossfaded over the stance change times. A
stance with no clips of its own keeps the standing clips.

## Built-in animations on imported rigs (retargeting)

A model you drop on a player often ships with few or NO usable clips — a
Mixamo download with a single dance take, a store model with just a
T-pose. When that happens the engine now **retargets its built-in
animation system onto the imported skeleton** automatically: the model
walks, runs, jumps, lands, crouches, plays the gestures/emotions and your
authored `.okayanim` clips — everything the blocky character can do —
with its bones matched by name (Mixamo and common humanoid naming) and
its rest stance aligned, so a T-pose rig stands and moves naturally.

- Works out of the box on **Set Character Model** / drag-drop: if the
  file has no clips named *idle* and *walk*, retargeting switches on
  (the import log tells you: "built-in animations retargeted (15/15
  bones)").
- The **Humanoid Retarget** component (on the player, next to the
  Character) has a **Weight** slider (blend against the model's bind
  pose) and a **Re-Detect Bones** button after a model change. Add it
  manually (Add Component > Animation) to prefer built-ins even when the
  model has its own clips.
- The model's own imported clips stay in the Model Animator's library —
  play one as a one-shot from a script (`play_clip_once("dance")`) and
  the retarget yields until it finishes.
- Files that DO carry idle+walk clips keep the previous behavior (their
  own clips drive locomotion).

## Custom character models (drag & drop)

Any imported model can BE your playable character — no rigging setup:

- **Drop a model asset** (.glb/.gltf/.fbx/.obj...) from the Project panel
  **onto a player in the Hierarchy** (any object with a controller or a
  Character), or press **Set Character Model...** on the Character /
  First-Person / Third-Person Controller in the Inspector.
- The model imports as a child of the player, auto-sized to the
  character's height and feet grounded on the capsule's origin.
- **Facing is read from the skeleton** (its left/right thighs, arms and
  feet), so a model exported facing +Z, -Z or sideways all end up facing
  the way the controller walks. Next to the button: **Fix Facing**
  re-detects it, **Turn 180** / **90** turn it by hand (models with no
  recognisable left/right bones).
- **The collider follows the model**: the player's Box / Capsule Collider
  is refitted around the model you see (a capsule + Rigidbody 3D are added
  when the player had none). **Fit Collider** redoes it after you rescale.
  Adding a First/Third-Person (or other) controller from *Add Component*
  also gives the object a fitted collider + rigidbody straight away.
- Its animation clips are wired to locomotion automatically: clips named
  *idle/stand*, *walk*, and *run/sprint/jog* map to the auto
  idle/walk/run switching (smooth 1D blend), driven by how fast the
  player actually moves.
- The default blocky body is hidden (the Character component is disabled
  — re-enable it in the Inspector to get it back). All of it saves with
  the scene.
- **Foot IK is wired automatically** when the model's leg bones are
  recognizable (Mixamo and common naming): full plant setup — pelvis
  adjust, plant-down, slope align, smoothing — with zero clicks.
- Jump/Fall/Land clips are mapped by name into the air states (above).
- Crouch / prone / crawl / get-up clips are mapped by name too (above).

Controllers also carry a **Fall Reset Y** safety net (default -100): fall
off the map or through a floor with no collider and the player teleports
back to its spawn point instead of falling forever — and a console
warning explains the usual cause (the floor is missing a collider —
select it and Add Component > Mesh Collider 3D).

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

## State machines: blend trees, exit time, Any State

The **Animator** window (select an object with an `AnimStateMachine`) edits states
and transitions. Beyond single conditions:

- **Blend tree states** — switch a state's Motion to *Blend tree*, pick a float
  parameter (e.g. `speed`) and add motions with thresholds (idle at 0, walk at 2,
  run at 6). The two neighbouring clips play in phase, weighted by the parameter.
  In Play mode each motion shows a live weight bar.
- **Several conditions** — `+ Condition` adds more tests to a transition; all must pass.
- **Exit time** — a transition can wait until the state is a fraction through its
  clip (0.9 = 90%). With the *Always* condition it fires purely on time: an attack
  returning to locomotion when it's nearly done.
- **Any State** — transitions checked from every state first (hit reactions, death).

Scripts: `anim_set_float`, `anim_set_bool`, `anim_trigger`, `anim_state()`,
`anim_prev_state()`, `anim_state_time()`, `anim_normalized_time()`.

## IK

| Component | Use |
| --- | --- |
| Full Body IK | Hands, feet and head reach goals on any humanoid rig; bones auto-map. Out-of-reach goals pull the hips and bend the spine; soft reach stops limbs snapping straight. |
| Limb IK | One arm or leg reaching a target with a pole and bend limits. |
| Chain IK | Long chains (tails, tentacles, spider legs) with FABRIK or CCD, a pole to set the curl direction and per-joint bend limits. |
| Foot IK | Plant both feet on uneven ground and adjust the pelvis. |
| Look-At IK / Aim IK | Head/spine tracking; point a bone (gun, torch) at a target. |

Every IK component has **IK Active** and **Fade** so switching IK on or off blends
instead of popping. Scripts: `ik_enable(on)`, `ik_set_weight(w)`,
`ik_set_target(x, y, z)`, `ik_set_pole(x, y, z)`, and for Full Body IK
`ik_hand("left", x, y, z [, weight])`, `ik_foot(...)`, `ik_look(x, y, z)`,
`ik_release()`.

## Animating any object (the Animation tab)

Select an object with an **Animator** (or press *Add Animator*) and open
**Animation**. The tab docks under the Scene view and grows to a usable height
the first time it opens.

- **Presets (star button).** One click gives a starting animation built around
  the object's current pose: Bob, Hover, Spin, Spin (2D), Pulse, Shake, Sway,
  Bounce, Pop In, Slide In. Set an amount and a length, then press *Apply
  preset*. A preset only replaces the tracks it animates; your other keys stay.
- **Record (red dot).** Move, rotate or scale the object with the Scene gizmos or
  the fields beside each row, and a key lands at the playhead.
- **Rows.** Position, Rotation and Scale each get a row. The arrow expands a
  row into one row per axis, each with its own keys and its value at the
  playhead. Double-click a row to key it at that time.
- **Selecting keys.** Click a key to select it, Shift+click to add to the
  selection, or drag across empty space to box-select. Drag any selected key to
  move them all. **Del** deletes, **Ctrl+C / Ctrl+V** copy and paste at the
  playhead, **Ctrl+A** selects everything, and **Ctrl+Z** undoes.
- **Curves (wave button).** Draws each track as a graph. Drag a point to change
  its time and value; hold Shift to change only the value. Click a row name to
  show just that group's curves.
- **Length.** The field next to *Loop* sets the clip length. Tracks hold their
  first and last values instead of repeating, so a key at 2 s doesn't make a 1 s
  track loop twice.
- **More (⋯).** Playback speed, frame snapping (24, 30 or 60 fps, or off; hold
  Alt while dragging for free time), smooth or linear interpolation per group,
  *Make loop seamless*, Reverse, Twice as fast, Half speed, and Trim to the
  playhead.
- **Navigation.** The mouse wheel zooms the timeline. Shift+wheel, or the wheel
  over the row names, scrolls the rows. Middle-drag pans, **F** fits the view,
  **Space** plays, **K** keys everything, and **[ ]** jump between keys.

### Animation events

The **Events** lane under the ruler holds named moments in the clip. Add one
with the flag button, **E**, or a double-click on the lane. Drag a flag to move
it, double-click it to rename it, or right-click it for more options. Events
are saved with the scene.

When playback passes an event, every component on the object gets
`OnAnimationEvent(name)`:

- **OkayScript:** the global `anim_event` holds the name. Then a function with
  the event's name runs (e.g. `footstep()`), followed by
  `on_anim_event()`.
- **Visual scripts:** an **On Anim Event** node runs. Give it a name to react to
  only that event, or leave it blank to react to all of them. **Anim Event
  Name** outputs the event that fired.
- **C++:** set `Animator::onEvent`, or override `OnAnimationEvent` in a
  component.

```
footstep() {
    play_sound("step.wav")
}
on_anim_event() {
    log("event: " + anim_event)
}
```

## Imported characters: skeleton, skinning, import report

**What import keeps.** glTF, FBX and the Assimp formats come in with their full
node hierarchy, so bones are connected exactly as in the source. Skins keep their
joints, inverse bind matrices and weights. Animations become clips on a **Model
Animator** on the model root.

- **glTF details:**
  - Up to 8 influences per vertex are read; the strongest 4 are kept and
    renormalized.
  - Quantized (normalized integer) UVs, weights and rotations are read correctly.
  - Nodes stored as a 4x4 `matrix` are decomposed, so Z-up exports stand up.
  - Multi-material skinned characters keep every material.
  - STEP and LINEAR keys play back exactly as authored.
- **Bone names:** duplicate node names are made unique, so clips and skins always
  bind to the right bone.
- **Import report:** after an import the Console prints:
  - meshes, vertices and bones, and the skeleton's root bone;
  - weights per vertex and the clips;
  - warnings for anything that will look wrong: missing bones, vertices with no
    weights, or a skeleton split into separate trees.

**Skeleton overlay.** Rigged models show their bones in the Scene view, connected
parent to child (View > Scene Overlays > Skeletons: Selected model / All / Off).
Hovering a bone names it; clicking it selects it.

**Skinned Mesh inspector:**
- **Skinning:** *Linear* (fastest, skinned on the GPU) or *Dual quaternion*, which
  keeps volume at twisting joints (wrists, forearms, shoulders) and runs on the
  CPU.
- Bone and weight statistics, and **Normalize weights**, which makes weights sum
  to 1 and drops tiny ones.
- A bone tree with how many vertices each bone moves.

Skinned meshes also deform in edit mode, so posing bones in the Scene view bends
the mesh.

**Sparse accessors.** glTF accessors stored sparse (common for morph targets) are
read: the dense base, or zeros, with the listed values written over it.

## Blend shapes (morph targets)

Facial expressions, lip-sync visemes and corrective shapes come in with the
model as a **Blend Shapes** component on each mesh that has them:

- **glTF:** every primitive's `targets` (positions and normals), names from
  `extras.targetNames`, default weights from `mesh.weights`, and `weights`
  animation channels.
- **FBX:** blend shape channels and their animated `DeformPercent`.
- Clips drive them with `weights.<n>` tracks on the mesh's node. They play in the
  Animation tab preview, through crossfades and blend trees, and in Play.
- The inspector has one slider per shape, a filter box for faces with dozens of
  shapes, and **Reset all**.
- Scripts: `blend_shape("Smile", 0.8)` sets a shape on every mesh under the
  object; `blend_shape_get("Smile")` reads it back.
- The morph is applied before skinning, so a morphed face still follows the
  skeleton. Shapes are stored sparse (only the vertices they move) and saved with
  the scene; the mesh is only rebuilt when a weight changes.

## Root motion

Turn on **Root Motion** in the Model Animator. The root bone's travel moves the
character, and the bone stays over it.

- Travel is measured through the bone's parent, so centimetre (0.01) and rotated
  (Blender) armatures move the right distance in the right direction.
- **+ Vertical** also lifts the object; **+ Turning** also rotates it (turn-in-place
  and curved walks).
- **Moves: Parent** applies it to the player object the model sits under. A
  `Rigidbody3D` there gets it as velocity, so walls still stop the character.
- Works through crossfades and blend trees. The inspector shows the live root
  speed in Play mode.
- Scripts can take over with `ModelAnimator::onRootMotion`.

## State machine: layers, 2D blend trees, ints

- **Locomotion setup** (Animator toolbar) builds a full setup from the model's
  clips:
  - idle / walk / run blended by `speed`;
  - Jump (Any State on the `jump` trigger), Fall and Land, using `grounded` and
    `verticalSpeed`.

  These are the parameters the player controllers already send.
- **2D blend trees:** pick *2D (strafe)* on a blend tree. Motions sit on a map;
  drag them, and watch live weights in Play. The controllers send `moveX` / `moveZ`
  (velocity in the body's frame) for strafing sets.
- **Layers:** the **+** tab adds a layer, its own states playing over the base
  layer on part of the skeleton. Pick a **Mask bone** (e.g. the spine for
  upper-body actions) and a **Weight**. Layers share the base layer's parameters,
  so `anim_trigger("attack")` drives both.
- **Int parameters** with `Int ==` / `Int !=` conditions, and a per-state **speed
  multiplier** parameter.
- Parameters can be edited live while playing, and the running state shows a
  progress bar.

**Per state and per transition:**
- **On enter / On exit events:** an animation event fired when the machine enters
  or leaves the state. It reaches `anim_event()` in scripts and the visual
  scripts' animation event handlers, like clip events. Use it for jump and land
  sounds or to open a hit window.
- **IK weight:** how much Foot IK and Full Body IK apply in that state, eased
  over the transition. Set it to 0 for jumps, rolls and falls so the feet aren't
  pulled to the ground mid-air. Locomotion setup does this for Jump and Fall.
- **Start at** on a transition starts the target part-way into its clip, to line
  up the feet between walk and run cycles.

Clip events from imported models now also reach visual scripts.

**Graph editor:**
- **Zoom** with the mouse wheel (around the cursor); **Frame** (F) fits every
  state.
- **Labels** shows each transition's conditions on its arrow, e.g.
  `speed > 0.1 & grounded @80%`.
- **Find state** lights up matching states; Enter selects and centres the first.
- **Ctrl+D** duplicates a state; **Ctrl+C / Ctrl+V** copy and paste one, keeping
  its transitions to states that exist. Both are also in the right-click menus.
- Nodes show a marker when the state fires events, and its IK weight when it
  isn't 100%.

## IK: feet, hands, posing

- **Set up humanoid IK** (Model Animator inspector) adds, from the bone names:
  - **Foot IK:** legs bend to put the feet on the ground, feet tilt to slopes, the
    pelvis drops for lower ground, and feet are locked while planted so they don't
    slide.
  - **Full Body IK**, with *LeftHand / RightHand IK Target* objects. Move them, or
    parent them to a weapon or a door handle, and enable the hand effectors.
- **Foot locking** (`footLock`) pins a planted foot where it landed until the
  animation lifts it.
- All IK components now run after animation, crossfades and layers, so nothing
  overwrites their correction.
- **IK posing:** with the **IK** toggle in the Scene toolbar, moving a hand, foot
  or any bone turns the two bones above it to reach, keeping bone lengths. Undo
  works as usual.

## Performance

- Transforms cache their world matrix, so world positions and rotations are no
  longer rebuilt up the whole parent chain on every call.
- The Model Animator samples clips straight into cached bone bindings: no per-bone
  components, no clip copies when switching, and no name lookups per frame.
- Skinned meshes only re-skin when a bone actually moved.

- Skinning sums the weighted bone matrices once per vertex (3x4 affine) and runs
  after IK, once per frame.
- Imported clips get keyframe reduction: keys that lie on the line between
  their neighbours (within a tiny tolerance) are dropped, so baked 30 fps FBX
  takes sample far fewer keys.
- Auto-fitted colliders skip meshes that deform every frame, and `GetComponent`
  takes an exact-type fast path.

A 60-bone mocap clip costs about 0.015 ms per frame to pose. The Khronos Fox
(24 bones, 1,728 vertices) animates and CPU-skins in about 0.06 ms per frame. A
stress scene with 20 animated Foxes and 60 physics cubes went from 1.66 ms to
about 0.65 ms per frame.

### GPU skinning and frustum culling

- Skinned meshes deform on the GPU in the OpenGL editor viewport and in the
  player (OpenGL and Direct3D 11). The bone limit comes from the GPU's uniform
  space (up to 256 bones per mesh); bigger rigs fall back to CPU skinning. The
  Stats panel shows `skinning: GPU` or `CPU`.
- Every renderer skips objects outside the camera (and outside the shadow
  light's view for shadows) before any draw call. Skinned meshes are tested
  around their bones, so animated characters don't pop at the screen edges. The
  Stats panel shows **Drawn** / **Culled** counts and a **Frustum culling**
  checkbox for debugging.
- In a 400-object test scene with the camera looking at a quarter of it, about
  74% of objects were culled and the frame time dropped by about a third.

## IK: smoothing and limits

- **Full Body IK** blends effectors in and out instead of snapping: *Blend Speed*
  (default 8 per second; 0 = instant), *Goal Smoothing* (follows a moving target
  without jitter), and look-at *Smoothing*. The inspector shows each effector's
  live weight and whether it **reached** its target (`ik_reached("left")` in
  scripts).
- **Joint limits:** *Elbow / Knee Min Bend* keep arms and legs from locking
  straight or folding backwards.
- Reach is spread over the lower spine and chest, so leaning for a far target
  looks natural.
- A partial weight blends the *goal* rather than the final pose, so limbs keep
  their length at any weight.
- **Set up humanoid IK** also creates a *Look IK Target*.
- State machine states have an **IK Weight**, so IK can fade out during a roll or
  a climb and back in afterwards.

## Holding objects (Procedural Object Holding)

A character holds things with its hands: the hands go to the object's grips, the
elbows bend down and out, and the object moves with the body.

- Add **Procedural Object Holding** (Add Component > Animation) to the character
  and **Holdable Object** to anything it may pick up. Without a Holdable Object
  an object can still be picked up from a script; it gets automatic settings.
- **Primary Hand** (Left / Right / Both) is the hand a one-handed object goes in.
  *Both* adds the support hand on pistols and swords and carries a small item
  with both hands.
- **Holding Style**: *Automatic* uses each object's own style; *Custom* always
  uses the object's grip points.
- The object's **Style**:
  - *Pistol*: one hand on the handle, aimed forward. With **Support Hand** on, the
    other hand wraps around the first.
  - *Rifle*: stock at the shoulder, one hand on the grip, the other under the barrel.
  - *Box*: both hands on its sides, as low as the arms reach.
  - *Flashlight*: one hand, beam forward.
  - *Sword*: one hand on the hilt, blade up and forward. *Support Hand* makes it
    two-handed.
  - *Cup*: one hand on the handle.
  - *Heavy*: both hands under its bottom edges, hanging low.
  - *Custom*: the hands go on the **Left / Right Grip** points (object-local
    position and rotation; rotation 0 = palm down, the hand along -Z), and the
    object sits at **Hold Offset / Hold Rotation** from the chest. A one-hand
    object authored for the right hand is mirrored when the primary hand is the
    left.
  - *Automatic* picks a style from the object's tag or name ("pistol", "rifle",
    "crate", "sword", "mug", "torch"...), then from its shape and weight: long
    and weighty is a rifle, long and light a sword, short and long a flashlight,
    over 15 kg heavy, wide or over 5 kg a box. Anything else small is held in
    one hand from underneath. The inspector shows what it picked.
- Named styles read the object's long side as the barrel, blade or beam. Guns
  and lights point along local -Z, a sword's blade along +Y, a cup's handle
  along +X. For a model built another way, **Hold Rotation** turns it
  (0, 180, 0 for a gun facing backwards).
- Hands are a plain block: no finger bones and no finger geometry. Holding
  aims the palm at the grip. Finger joints are not posed, including on a rig
  that happens to have them. A run (sprint, or fast enough to use the run
  style) keeps those hands out of the first-person view. Walking, idling and
  holding still show the block. The held object stays on its grip.
- The owner's first-person camera does not draw the head or the neck. The
  head is not scaled away and not deleted from the mesh: it stays active, so
  it still casts a shadow and any other camera still sees it. The neck and
  the flat collar under it are skipped with the head, and the eye sits just
  in front of the face (further forward and up as you look down) so that
  collar is behind the lens. The body is drawn from the outside only, so
  looking down shows the shirt from the outside, the hips, the legs, or the
  ground, not the inside of the cloth and not a crate through it. The eye sits
  in front of the chest and steps further forward and up as you look down, and
  the shirt's lid stays on the body so that view is a solid surface. One
  plain hand stays in view while standing or walking. A run takes both arms
  off the owner's camera. The head stays visible through the switch into first
  person and is skipped only once the lens is in front of the face. Pressing
  V back shows the head and the neck on that same frame, without waiting for
  the ease, including when V is tapped quickly. The orbit pitch returns to the
  third-person angle you started with, so a look down does not come back as a
  top-down camera. The near plane comes back with it. The first-person
  controller does not put the hide back, and the editor Game view does not
  invent one while the game is playing.
- **Socket**: hold it at a named object inside the character (a holster, a back
  slot) instead of in front of the chest.
- The hands never stretch: when a grip is out of reach the object comes closer
  to the body. Picking up eases the object into the hands over *Transition*;
  dropping hands it back to physics with the character's velocity.
- Works on the built-in Character (it switches on the part rig so the arms have
  bones) and on imported humanoids (bones found by name, through the Full Body IK
  the character already has or one it adds). Looking around with a controller's
  *IK Look* turns the chest before the hands are placed, so they stay on the grips.
- **Body Adjustment** (on by default): a heavy or big load (from 3 kg up to
  30 kg, more for big loads and the *Heavy* style) leans the torso back. The
  lean is an additive layer on the character's animation, so walking and
  running keep their motion under it, and it eases in and out. The load also
  slows the character down (down to 0.55x at the heaviest; `SpeedMultiplier()`).
  The first-person, third-person, shooter, top-down and click-to-move
  controllers apply it through `MoveSpeedScale()`, the one shared movement-speed
  hook, so the server and a predicting client agree. The object is also pushed
  out in front instead of sinking into the chest or belly.
- **Carry Animations**: *Automatic* is the built-in lean. *Custom* leaves the
  posture to your Animator: it gets the parameters `holding` (bool),
  `carryStyle` (int: 0 small item, 1 custom, 2 pistol, 3 rifle, 4 box,
  5 flashlight, 6 sword, 7 cup, 8 heavy) and `carryWeight` (kg) for its own
  carry states or layer. The hands still go on the grips either way.
- Scripts: `hold_nearest(range)` and `hold_pickup("Crate")` return 1 when the
  object is in the hands, `hold_drop()` lets go, `hold_object()` is the name of
  what's held ("" when nothing).

## State machine: events, mute, history

- States have **Enter / Exit events** (sent like clip events, so `OnAnimEvent`
  nodes and script handlers receive them).
- Transitions have an **Offset** (start the next clip part-way through) and a
  **Mute** checkbox (muted transitions are dimmed and never fire). Outgoing
  transitions are listed in priority order with up/down buttons.
- In Play mode the graph flashes the transition that just fired and lists the
  **recent state changes** with times.
- **Thresholds from clip speed** sets a 1D blend tree's thresholds from how fast
  each clip's root actually moves.
