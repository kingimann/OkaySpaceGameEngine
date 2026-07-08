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

## Foot IK on imported rigs

Add a **Foot IK** component to the import root (Add Component > Animation)
and press **Auto-Detect Bones** — it maps the usual leg-bone names (Mixamo
"LeftUpLeg / LeftLeg / LeftFoot", thigh/shin/calf variants, `_l`/`_r`
suffixes) automatically. In Play mode each foot raycasts the ground and a
two-bone solve plants it on slopes and steps; turn on **Adjust Pelvis** so
the body lowers to reach lower ground, and **Align To Slope** to tilt the
sole. The solve runs after all animation (including the state machine and
locomotion blending), so it corrects the final pose.

## Tips

- **Meshy / Mixamo FBX**: animations import via Assimp. For the richest
  results (full skins + all takes) prefer the **GLB** export when available.
- Multi-take files import one clip per take; single-take files can be cut
  apart with **Split…**.
