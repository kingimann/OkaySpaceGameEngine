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
