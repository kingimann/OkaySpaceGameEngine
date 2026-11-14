# Input: actions, controllers and rebinding

Games can read raw keys (`Input.GetKey("w")`), but the better way is **actions**:
named things the player does ("Jump", "Move", "Fire"). An action listens to a
keyboard key, a mouse button and a controller button at once, and the player can
rebind it. Their bindings are saved with their prefs and come back next time.

## The built-in actions

| Action | Keyboard / mouse | Controller |
| --- | --- | --- |
| Move (direction) | W A S D, arrow keys | left stick, d-pad |
| Look (direction) | mouse movement | right stick |
| Jump | Space | A |
| Sprint | Shift | left stick click |
| Crouch | Ctrl | B |
| Fire | left mouse | RT |
| Aim | right mouse | LT |
| Interact | E | X |
| Reload | R | Y |
| Inventory | Tab | Back |
| Pause | Escape | Start |

Buttons are **down** while any of their bindings is pressed past half way (a
trigger counts as pressed at half pull). Direction actions give an x, y vector
in -1..1 with y up: sticks keep their analog tilt, keys give full steps, and the
length never goes past 1, so diagonals aren't faster.

## Reading actions in scripts

C#-style scripts (also the **Input** nodes in a visual script):

```csharp
if (Input.GetButtonDown("Jump")) Jump();
bool firing = Input.GetButton("Fire");
float throttle = Input.GetActionValue("Fire");     // 0..1, a trigger can be part way
Vector3 move = Input.GetActionVector("Move");      // x, y in -1..1
Input.Rumble(0.6f, 0.3f, 0.2f);                    // heavy motor, light motor, seconds
Input.RebindAction("Interact", "key:f");
if (Input.gamepadConnected) ShowControllerHints();
```

OkayScript:

```
if action_down("jump"): jump()
speed = action_value("fire")
mx = action_x("move")
my = action_y("move")
rumble(0.6, 0.3, 0.2)
rebind("interact", "key:f")
```

Action names are not case-sensitive.

## Bindings

A binding is written as text:

| Text | Means |
| --- | --- |
| `key:space`, `key:e`, `key:shift`, `key:up`, `key:f5`, `key:left ctrl` | a keyboard key |
| `mouse:left`, `mouse:right`, `mouse:middle` | a mouse button |
| `pad:a` `b` `x` `y` `lb` `rb` `lt` `rt` `ls` `rs` `back` `start` `up` `down` `left` `right` | a controller button (`lt` / `rt` read the trigger's pull) |
| `keys:w,s,a,d` | four keys as a direction: up, down, left, right |
| `stick:left`, `stick:right`, `dpad` | a controller stick or the d-pad as a direction |
| `pad:a@1`, `stick:left@2`, `dpad@1` | the same on another controller (0-3, default 0) |
| `mouse:delta` | mouse movement as a direction |

## Letting players rebind

Add a **UI Keybind Field** (Add Component > UI) and pick its **Input Action**.
It shows the action's current binding. The player clicks it (or selects it
with a controller and presses A) and presses the new key. Escape cancels, and
Backspace puts the action's defaults back.

- Tick **Gamepad** to make the field listen for a controller button instead. A
  controls menu usually has one keyboard field and one gamepad field per action.
- For **Move**, pick a **Direction Key** (Up, Down, Left, Right) to rebind just
  that key.

A rebind replaces the action's first binding for that device. The others stay,
so rebinding Jump to F keeps A on the controller. Only bindings the player
changed are saved.

## Keys

Keys are one `char` each. Letters, digits, punctuation, Space, Enter, Escape,
Backspace, Tab and the arrows work as before. These extended keys were added; the
runtimes feed them from SDL scancodes, so they are the same on every keyboard
layout:

| Name | Notes |
| --- | --- |
| `f1` ... `f12` | `Input::KeyF(n)` in C++ |
| `insert`, `delete`, `home`, `end`, `pageup`, `pagedown` | the navigation block |
| `lshift`, `rshift`, `lctrl`, `rctrl`, `lalt`, `ralt` | one side only |
| `shift`, `ctrl`, `alt` | true while **either** side is held |

Names are not case-sensitive and ignore spaces, `_` and `-` (`Left Shift`,
`page_down`). `key:f5`, `key:left shift` and so on work in bindings and rebind
fields. Old saved bindings are unchanged. `Input::IsKnownKeyName` (C++) tells a real name
from a typo: an unknown name such as `f13` still falls back to its first letter, as
before, instead of failing.

The editor's Play mode does not feed the new keys yet; the standalone player does.

## Input contexts

Actions can belong to a **context** so a menu can take keys away from gameplay.
An action with no context is in the base (gameplay) context, which is always
active at the bottom of a stack.

```cpp
InputActions::DefineIn("UI", "Submit", {"key:space", "pad:a"});
InputActions::DefineIn("UI", "Cancel", {"key:escape", "pad:b"});

InputActions::PushContext("UI");     // Submit is live; Jump no longer fires on Space or A
...
InputActions::PopContext("UI");      // gameplay gets its keys back
```

| Mode | While the context is on the stack |
| --- | --- |
| `Consume` (default) | every key / button its actions bind is swallowed from lower contexts; other inputs pass through |
| `Block` | lower contexts get nothing at all (a modal menu) |
| `Passthrough` | lower contexts see everything, both fire |

Contexts nest: the topmost is served first and each one sees only what the ones
above it left. An action whose context is not pushed reads 0. Swallowing happens
per input, so a UI that binds W takes W from Move but leaves A S D. A press that
a context takes over from a held key ends the old action (`Up` fires).

Raw `Input.GetKey` is **not** affected, because scripts that read raw keys do not
go through actions. Use `InputActions::KeyConsumed(key)` /
`PadButtonConsumed(button, player)` to honour the stack from raw code, or read the
action. Contexts are set up in code and are not saved with the player's bindings.
`InputActions::SetContext(action, context)` moves an existing action.

`InputActions::SetThreshold(action, 0.2)` changes the value at which a button
action counts as held (default 0.5), for example a light trigger pull.

## Controllers

Up to four controllers are read, one per player slot (0 is the first). They fill
in the order they are plugged in, and plugging one in or pulling it out mid-game
works. The first controller also presses W A S D and Space (left stick, d-pad, A),
so games written against the keyboard still play on a controller.

A binding says which controller it reads with `@n`: `pad:a@1`, `stick:left@2`,
`dpad@1`. No suffix means the first controller. Rebinding keeps the suffix, and
old saved bindings (no suffix) are unchanged.

```cpp
InputActions::Define("P2Jump", {"pad:a@1"});
```

In C++ every reader takes an optional player: `Input::GamepadAxis(1)`,
`Input::GetGamepadButton(Input::PadA, 1)`, `Input::GamepadTrigger(1, 1)`,
`Input::GamepadConnected(2)`, `Input::GamepadCount()`,
`Input::Rumble(low, high, seconds, 1)`. The runtime feeds each one with
`Input::FeedGamepadRaw(player, ...)`; `okay/Input/SdlGamepad.hpp` has
`SdlGamepadSet` to manage the slots from the SDL event loop.

### Dead zones, curves and trigger thresholds

The runtime feeds raw stick and trigger values and `Input::GamepadSettings(player)`
turns them into what games read:

```cpp
auto& cfg = Input::GamepadSettings(0);
cfg.left.deadZone  = 0.25f;                        // ignore stick drift
cfg.left.shape     = Input::StickSettings::Shape::Axial;
cfg.right.curve    = 2.0f;                         // finer aim near the centre
cfg.right.outerZone = 0.92f;                       // a worn stick that tops out at 0.92 still reads 1
cfg.triggers.deadZone  = 0.05f;
cfg.triggers.threshold = 0.3f;                     // pull needed for the LT / RT button
```

| Setting | Meaning |
| --- | --- |
| `shape` | `Radial` (default): the whole stick vector must pass the dead zone and keeps its direction. `Axial`: each axis is dead on its own, so a slightly off-axis stick snaps to straight lines |
| `deadZone` | tilt that still reads 0 (default 0.18, kept from before; clamped to 0..0.95) |
| `outerZone` | tilt that reads full strength (default 1) |
| `curve` | `out = in ^ curve`; 1 is linear, 2 is gentler near the middle |
| `triggers.deadZone`, `curve` | the same for each trigger |
| `triggers.threshold` | processed pull at which the trigger counts as a pressed button (default 0.5) |

The defaults match the old fixed behaviour. `Input::ProcessStick` and
`Input::ProcessTrigger` apply a setting to a value without feeding anything.
Values passed to the older `FeedGamepad` / `FeedGamepadExtra` are taken as given.

`Input.Rumble(low, high, seconds)` shakes the controller. `low` drives the heavy
motor and `high` the light one.

## Touch

A finger is a real touch, not a stand-in for the mouse. The player (desktop and the
web build) reads SDL finger events and keeps every finger that is down. The first
finger still moves the mouse, which is how a tap hits an ordinary button. The extra
fingers are for the controls and the gestures below.

`Input.TouchScreen` (OkayScript `is_touch_screen()`) becomes true when the machine
reports a touch screen, or when a finger first lands. It stays true for the rest
of the run.

### Gestures

| Gesture | What it is |
| --- | --- |
| Tap | a finger down and up, without moving, inside a third of a second |
| Double tap | two taps close together. The single tap is held back until it is clear this is not a double tap |
| Long press | a finger held still for half a second |
| Swipe | a quick flick. The direction is up, down, left or right |
| Pinch | two fingers moving apart or together. The scale is 1 where the pinch began |
| Rotate | two fingers twisting. The angle is in degrees, positive counter-clockwise |

OkayScript, true on the frame it happens:

```
if gesture("tap"):        set_text("Tap")
if gesture("swipe up"):   set_text(swipe_direction())
scale = pinch_scale()
angle = twist_degrees()
```

`gesture` also takes `"double tap"`, `"long press"`, `"swipe"`, `"swipe down"`,
`"swipe left"`, `"swipe right"`, `"pinch"` and `"rotate"`.

C#-style scripts use `Input.Gesture("tap")`, `Input.swipeDirection`,
`Input.pinchScale` and `Input.twistDegrees`.

A visual script has **On Tap**, **On Double Tap**, **On Long Press**, **On Swipe**
(the box can say up, down, left, right, or stay empty for any direction),
**On Pinch** and **On Two Finger Rotate**. **Get Swipe Direction**, **Get Pinch Scale**
and **Get Twist Angle** read the same values. A finger that is on a virtual stick,
a D-pad or a touch button is that control, not a swipe.

### On-screen controls

Add Component > UI, or GameObject > UI:

| Control | Drives | Notes |
| --- | --- | --- |
| Virtual Joystick | a direction action, Move by default | Fixed, or floating (it appears under the finger inside its rectangle) |
| D-Pad | a direction action, Move by default | Up, down, left, right. Diagonals can be turned off |
| Touch Button | a button action, Jump by default | Held while the finger is down. The label is drawn on it |

Each one has **Only On Touch**. On, the control is hidden and ignores the mouse
until a touch screen is in use, so a desktop game is not covered in buttons. Off,
it is always drawn, and a mouse can press it. The keyboard and controller bindings
of the same action keep working: the stick adds to Move, it does not replace W A S D.

`examples/touch_controls` is a small scene: a stick, a D-pad and a Jump button,
a marker that walks with Move, and a line of text that names the latest gesture.

## Your own actions

A game can add actions or change a built-in one's defaults, usually from a
script's start:

```csharp
Input.DefineAction("Dash", "key:q", "pad:rs");
Input.DefineDirection("Steer", "keys:i,k,j,l", "stick:right");
```

```
define_action("dash", "key:q", "pad:rs")
define_direction("steer", "keys:i,k,j,l", "stick:right")
```

A player who rebound the action keeps their binding; the new defaults are what
Backspace on a keybind field goes back to. In C++ it is `InputActions::Define`.
