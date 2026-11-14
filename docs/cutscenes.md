# Cutscenes: sequences and the Timeline window

A **sequence** is a timeline of tracks that drives a cutscene without scripting:
camera shots, object moves, animation and audio cues, events and subtitles,
letterbox bars, camera shake, a coloured screen fade, and nested sequences. It is
saved as a `.okayseq` text file in the project's `Assets` folder, authored in the
**Timeline** window (View → Editors & Tools → Timeline), and played in a scene by
a **Sequence Player** component. Scripts can start one with `play_sequence("Intro")`.

`examples/cutscene` is a working one: open `intro.okayscene` and press Play. Bars
ease in, the screen fades up from a dark blue, the hero walks, the camera bumps
as `glance.okayseq` swings the door, then the picture fades out. Escape skips it.

## The Sequence Player component

Add Component → Gameplay → **Sequence Player (cutscene)**.

| Field | What it does |
| --- | --- |
| Sequence | The `.okayseq` file (a picker lists the project's). **Timeline** opens it in the window. |
| Play On Start | Play as soon as the scene starts. |
| Loop | Wrap at the loop markers (the sequence's own loop flag counts too). |
| Time Scale | Playback rate; the game's time scale applies on top. |
| Skip Key | Jumps to the end (Escape by default). The remaining set-active, teleport and time-scale actions still apply and the named events are still delivered; subtitles and waits are skipped. |
| Lock Player | While it plays, the player's controllers ignore input, the way they do under an open inventory or menu. |

While a sequence with a camera track plays, the player drives the scene's **main
camera** (after the controllers have placed it) and hands it back exactly as it
was when the sequence ends or is stopped. Objects a transform track moved stay
where the cutscene left them.

Playback state is not saved with the scene: a loaded scene starts every sequence
from the beginning (or not at all), never mid-way. In multiplayer every client
plays its sequences locally; nothing about them is sent over the network, so
start them from a message every client receives (an RPC, a synced variable).

## Tracks

**Camera.** Shots in time order. A shot takes the pose of a **camera object** in
the scene (and its field of view), or an **inline pose** (position, rotation,
optional field of view). Each shot **cuts** or **blends** in from the previous one
over a duration with an easing. The first shot blends from wherever the camera
was.

**Transform** (one per object, by name or `Parent/Child` path). Position,
rotation and scale keys with a per-key easing for the span to the next key. A
channel with no keys leaves that part of the transform alone. Keys are evaluated
by absolute time, so scrubbing backwards works.

**Animation** (per object). At a time: play a clip by name (a Character clip, a
model clip or a built-in animation, through `PlayClipFrom`), stop it, or drive
the object's Animator state machine: set a float or bool, fire a trigger, go to a
state.

**Audio** (per object). Play a sound file as a one-shot through the object's
Audio Source (added when it has none), or restart the source's own clip when no
file is given.

**Events.** Each entry fires exactly once when the playhead crosses its time going
forward (again after a loop wraps or a seek back). A **named event** reaches:

- OkayScript: `on_sequence_event(name)` on every script in the scene
  (`OnSequenceEvent(name)` works too);
- typed (C#-style) scripts: `public void OnSequenceEvent()` with the name in
  `Sequence.lastEvent`;
- visual scripts: the **On Sequence Event** node (blank catches every event;
  **Event Name** reads it);
- Actions lists: an **On Message** trigger with the event's name.

Scripts also get `on_sequence_started(name)` and `on_sequence_finished(name)`.

The built-in actions on an event track:

| Action | What it does |
| --- | --- |
| Set active | Switch an object on or off. |
| Teleport | Move an object to another object's position and rotation. |
| Fade to black / Fade from black | Over a duration, drawn as a full-screen overlay. |
| Subtitle | Speaker and text for a duration, drawn as a card in the launcher look. The editor's **Subtitles** track is an event track whose new entries are subtitles. |
| Time scale | Set the game's time scale from here on; the player restores the value it found when the sequence ends. The sequence itself runs on game time, so a scale of 0 also holds the sequence: use *Wait for input* to pause. |
| Wait for input | Hold the playhead until Enter, Space or the gamepad's confirm button ("Press Enter" shows). The other entries at that instant still fire. |
| End | End the sequence here. |

The fade and the subtitle are a function of the playhead time, so they scrub in
the editor and need no state in a build.

**Letterbox bars.** One track for the picture. Each key is how tall the top bar
and the bottom bar are, as a fraction of the screen (0 none, about 0.12 a film
frame, never more than 0.45). The curve on a key is how the bars grow or shrink
toward the next key. The bar colour is one colour for the whole track (black,
unless you change it). The bars are drawn on top of the frame, after whichever
renderer drew the scene, so they show in the editor, in a built game, and on
every renderer.

**Camera shake.** Keys of strength (0 still, 1 the camera's full shake distance
and angle) and of shakes per second (18 when that channel has no keys). The same
time always shakes the same way, so scrubbing matches playback. The shake is
applied in the camera's view, not by moving the camera object, so a controller
is not fighting it and every renderer sees it. **Reduce motion** in the
accessibility settings makes the strength 0 for the whole track.

**Screen fade.** Keys of a colour and an opacity, with a curve between them.
This is the whole-screen wash (a dark blue opening, a red hit, a fade to any
colour). It is separate from an event's Fade to black / Fade from black, and it
is drawn the same way as the letterbox bars, so it does not depend on the renderer.

**Nested sequence.** A cue names another `.okayseq` file and a time. From that
moment the other sequence plays: its camera, object moves, bars, shake, fade,
sounds, animation and events, for as long as it lasts. **Start this far in**
skips the opening of the child. A sequence that plays itself is skipped, with a
warning, so a cycle cannot hang the game. The child is found next to the parent
file, or under the project folder.

## The Timeline window

- **New / Open / Save** `.okayseq` files; the Project panel opens one on
  double-click. Saving reloads any Sequence Player in the scene that uses the file.
- **+ Add Track**: Camera, Transform, Animation, Audio, Events, Subtitles,
  Letterbox bars, Camera shake, Screen fade, or Nested sequence. A track's
  object is picked in the inspector on the right (the selected scene object is
  the default). The picture tracks explain themselves in plain language: bar
  height as a percent of the screen, shake strength, colour and opacity, and
  the file the nested sequence plays.
- The ruler: click or drag to **scrub**, wheel to zoom, middle-drag to pan.
  **Snap** rounds times to frames at the chosen fps. With **Loop** on, drag the
  blue markers for the loop range.
- Entries: **double-click** a row (or its **+**) to add one at that time, **drag**
  to move, click to select and edit it on the right, **Delete** to remove. A
  transform key holds every channel keyed at that time.
- Shots: pick a camera object or an inline pose, **Shot from Scene view** takes
  the Scene view's camera (3D view), **Shot from object** copies any object's
  pose; **Cut** or a blend time with an easing.
- Outside Play the playhead **previews** the sequence: tracked objects move in
  the Scene view and the Game view shows the camera shot, the fade and the
  subtitle. Play/pause (Space), Home/End, previous/next key. Everything goes back
  when the window closes, Play starts or the scene is rebuilt.
- **Record** keys whatever you change on a tracked object (the gizmo in the Scene
  view, the Inspector) at the playhead; **Key** (K) keys the tracked objects as
  they stand.
- Every edit is undoable with Ctrl+Z / Ctrl+Y while the window is focused.
- In Play mode, **Play in Game** runs the open sequence (with its unsaved edits)
  through a Sequence Player in the running game.

## Scripting

OkayScript:

```
play_sequence("Intro")      # the Sequence Player whose file is Intro, else the
                            # first one (or one added to this object) loading
                            # Assets/Intro.okayseq
stop_sequence()             # every playing sequence; the camera goes back
sequence_playing()          # 1 while one plays
sequence_time()             # seconds into it

function on_sequence_event(name) {
    if (name == "door_opened") { ... }
}
```

Typed scripts: `Sequence.Play("Intro")`, `Sequence.Stop()`, `Sequence.isPlaying`,
`Sequence.time`, `Sequence.lastEvent`. Visual scripts: **Play Sequence**, **Stop
Sequence** and **On Sequence Event** (plus the Engine API nodes `Sequence.*`).

## The file format

```
okayseq 1
sequence "Intro"
length 8
loop 0 0 0                       # on, start, end (end 0 = the length)
camera
  shot 0 cam "Shot A" blend 0 ease linear
  shot 3 pose 0 2 6 -10 180 0 fov 50 blend 1 ease inoutsine
transform "Hero"
  pos 0 0 0 0 linear             # time x y z easing
  pos 2 4 0 0 inquad
  rot 2 0 90 0 linear            # Euler degrees
  scale 1 1 1 1 linear
animation "Hero"
  play 0.5 "wave"
  float 1 "speed" 2.5
  bool 1 "armed" 1
  trigger 2 "attack"
  state 2.5 "Idle"
  stop 3
audio "Hero"
  cue 1 "sfx/door.wav" 0.8       # file ("" = the source's clip) volume
events "Main"
  event 1 "door_opened"
  active 2 "Door" 0
  teleport 2.5 "Hero" "Spawn"
  fadeout 3 1
  fadein 4 1
  subtitle 4 "Guide" "Welcome." 3
  timescale 5 0.5
  wait 6
  end 8
letterbox
  color 0 0 0 1
  bar 0 0 linear                 # time, fraction of the screen height, curve
  bar 1 0.12 sineinout
shake
  strength 0 0 linear            # 0 still .. 1 the camera's full shake
  strength 1 0.8 sineinout
  frequency 0 14 linear          # shakes per second
fade
  rgba 0 0.02 0.03 0.06 1 sineinout    # time, red green blue, opacity, curve
  rgba 1 0.02 0.03 0.06 0 sineinout
nest "The door opens"
  play 2 "glance.okayseq" 0      # time, file, seconds skipped into the child
```

Names are quoted (`\"` and `\\` inside), `#` starts a comment, easing words are
the easing names without spaces (`linear`, `inquad`, `outcubic`, `inoutsine`,
...). Entries may be written in any order; they are sorted by time when read.
