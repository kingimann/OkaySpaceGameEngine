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
| `key:space`, `key:e`, `key:shift`, `key:up` | a keyboard key |
| `mouse:left`, `mouse:right`, `mouse:middle` | a mouse button |
| `pad:a` `b` `x` `y` `lb` `rb` `lt` `rt` `ls` `rs` `back` `start` `up` `down` `left` `right` | a controller button (`lt` / `rt` read the trigger's pull) |
| `keys:w,s,a,d` | four keys as a direction: up, down, left, right |
| `stick:left`, `stick:right`, `dpad` | a controller stick or the d-pad as a direction |
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

## Controllers

The first connected controller is read every frame, and plugging one in or
pulling it out mid-game works. Both sticks have a dead zone. Games written
against the keyboard still play on a controller: the left stick and d-pad also
press W A S D, and A presses Space.

`Input.Rumble(low, high, seconds)` shakes the controller. `low` drives the heavy
motor and `high` the light one.

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
