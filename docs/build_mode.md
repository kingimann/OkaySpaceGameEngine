# Build mode and logic links: let players build in your game

Build mode lets players build while they play. They place pieces you give them,
move, rotate, copy and delete what they built, and wire pieces together with
**logic links** (a button opens a door, a pressure plate turns on a light) with no
scripting. Their creation saves to a file in their save folder and loads back
exactly.

You decide what players can build and where. The engine handles aiming,
snapping, the preview, undo, wiring and saving.

Build mode is not the same as **Builder Mode** (F2 in Play). Builder Mode is a
modelling tool for you, the developer. Build mode is a feature you turn on for
your players.

## The parts

| Component | Goes on | What it does |
|---|---|---|
| **Build Mode** | the player (or its camera) | Turns building on. Holds the palette, the grid, the controls and the limits. |
| **Buildable** | every palette prefab, plus level props players may rearrange | Marks an object players may edit, and gives it a stable id. |
| **Logic Source** | buttons, pressure plates, trigger zones, timers | Sends an on/off signal down its links. |
| **Logic Target** | doors, movers, lights, spawners, event triggers | Reacts to the sources linked to it. |

Build mode only edits objects that have an editable **Buildable**. Everything a
player places gets one automatically, so the level itself is safe. Players can't
delete your floor.

## Turning it on

1. **Make the pieces.** Build each piece players can place: a block, a wall, a
   ramp, a button, a door. Give each one a mesh and a collider (the collider is
   what other pieces snap against). Add a **Buildable** if you like; it's added
   for you on placement if you don't. Save each piece with **Save as Prefab**
   (`.okayprefab`).
2. **Add Build Mode to the player.** Select the player, then **Add Component →
   Gameplay → Build Mode (players build + wire)**.
3. **Fill the palette.** In the Build Mode inspector, open **Palette**, click
   **+ Add item** for each piece, and set its name, prefab and (optionally) a
   hotbar icon.
4. **Play.** Press **B** to enter build mode. A see-through copy of the piece
   (the ghost) follows where you aim. It's green where the piece fits and red
   where it doesn't. Click to place it.

The aim comes from the centre of the main camera, the same as a first-person
crosshair. It works with first- and third-person cameras: reach is measured from
the player, not the camera.

### Settings

| Setting | Default | Meaning |
|---|---|---|
| Palette | empty | The prefabs players can place (name, prefab, icon). |
| Grid | 1 | Snap step in world units. Pieces' edges land on grid lines. 0 turns the grid off. |
| Rotate Step | 90° | How far one rotate press turns a piece. |
| Max Reach | 8 | How far from the player players can build. |
| Max Objects | 250 | Cap on placed objects (0 = no cap). |
| Build On Layers | all | Collider layers players may build on, e.g. `Ground,Default`. |
| Start in build mode | off | Start the scene with build mode already on. |
| Allow mid-air | off | Let players place at full reach when aiming at nothing. |
| Built-in hotbar | on | Draw the palette hotbar. Turn it off to draw your own. |
| Ghost preview | on | Show the placement ghost. |
| Creations Folder | `creations` | Folder inside the save folder that creations go in. |
| Colours | | Ghost (fits / blocked), selection outline, wires. |
| Controls | see below | Every key, mouse button and gamepad button. |

## Default controls

All of these can be changed under **Controls** in the inspector (or in
`BuildMode::bindings` from code). A key can be any letter or digit.

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Enter / leave build mode | B | Back |
| Place the piece | Left click | R (right shoulder) |
| Delete the aimed object | Right click or X | L (left shoulder) |
| Rotate the piece (Shift: the other way) | R | X |
| Pick a palette item | 1–9, mouse wheel | Y (next item) |
| Move the aimed object (press again or click to drop) | G | – |
| Copy the aimed object | V | – |
| Link mode (wiring) on / off | L | – |
| Undo / redo | U / Y | B (undo) |
| Press a button (outside or inside build mode) | E | X (outside build mode) |

While moving or copying, left click drops the object and right click puts it
back. The object you aim at is outlined: that's the one Move, Copy and Delete
act on.

The default keys avoid the ones the stock controllers use (Q / E lean, Z prone,
C crafting menu, F flashlight). Two overlaps remain:

- **Right click** also zooms the Player Controller's first-person view while it's held. Use
  **X** to delete, or turn off the controller's **Right-mouse Zoom**.
- **E** presses buttons and also leans right. Set a button's **Press Key** if
  your game uses E for something else.

## Placing and snapping

- A piece sits **flush on the surface** you aim at: on the floor, on top of
  another piece, against its side, or under a ceiling.
- Its edges **snap to the grid**. Next to another placed piece it lines up with
  that piece's edges instead, so rows and stacks stay neat.
- Rotate turns it about its up axis in **Rotate Step** steps, and a long piece's
  footprint turns with it.
- The ghost turns red, and the hotbar says why, when:
  - you're aiming at nothing ("Aim at a surface");
  - the surface's layer isn't in **Build On Layers** ("Can't build here");
  - the piece would overlap something, including the player ("Blocked");
  - the object cap is reached ("Limit reached (250)").

## What players can edit

- Objects players **placed** in build mode.
- Level objects you gave a **Buildable** with **Players can edit** on, for
  example furniture players may rearrange. Turn **Players can edit** off to keep
  a piece permanent once placed.

Everything else (the level, the player, other scenery) can't be selected,
moved, deleted or wired.

Undo and redo cover the player's own actions: place, delete, move, rotate, copy
and wire. History holds the last 200 actions and is cleared when a creation
loads.

## Logic links

Players wire **sources** to **targets**. A target is on while *any* source
linked to it is on.

### Sources (Logic Source)

| Kind | On when | Settings |
|---|---|---|
| Button | pressed (aim at it, press E) | **Toggle**: each press flips it on or off. Or momentary: on for **Hold Time** seconds. Press key, gamepad button and reach. |
| Pressure Plate | something stands on it | **Zone Size / Offset**: the box above the plate that senses things. **Only Tag** narrows it to one tag. |
| Trigger Zone | something is inside it | The same zone, filling the object. |
| Timer | on for **On Time** out of every **Interval** seconds | **Starts on**. |

Plates and zones count anything that moves: players, NPCs and physics props.
They ignore objects marked **Static** and placed pieces without a Rigidbody 3D,
so the floor and walls never hold a plate down. Mark your level geometry Static.

### Targets (Logic Target)

| Kind | Does |
|---|---|
| Door | Slides by **Open Offset** and turns by **Open Turn** over **Move Time** seconds. Closes when the signal goes off. The offset is in the door's own rotated frame, so a rotated door opens the right way. |
| Mover | Like a door, but travels back and forth while the signal is on: a lift or a moving platform. |
| Light | Switches every Light on the object to **Intensity When On**, and makes its meshes glow with **Glow When On**. |
| Spawner | Spawns **Prefab** at **Spawn Offset** each time it switches on, up to **Max Alive** at once. With no prefab set, it fires a Spawner component on the same object. |
| Event | Sends the custom event **Event Name** (value 1 on, 0 off) to every visual script (On Custom Event), script (`OnEvent_<name>(value)`) and Actions list (On Message) in the scene. |

Every target also has:

- **Mode**:
  - **Follow**: on while powered.
  - **Toggle**: each new signal flips it, so a momentary button works like a light switch.
- **Invert**: on while *not* powered.
- **Signal Name**: the name On Logic Signal reports.

### Wiring in game

1. In build mode, press **L** for link mode. Every link shows as a wire: blue
   when off, yellow when the signal is on.
2. Click a source (button, plate, zone or timer). A wire follows your aim.
3. Click a target to link it. Click a linked target again to unlink it.
4. Keep clicking targets to wire one source to several. Right click clears the
   pick. Press **L** again to go back to building.

Example: place a button and a door, press L, click the button, click the door,
press L and B to leave build mode. Aim at the button and press E: the door
opens.

## Saving and loading creations

A **creation** is everything players placed, with its wiring and settings.

- `build_save("castle")` writes `<save folder>/creations/castle.okaycreation`.
  - The save folder is the game's per-user save folder, the same one Save /
    Load uses.
  - A name with a folder or extension, such as `maps/castle.txt`, is used as a
    path inside the save folder.
- `build_load("castle")` replaces what players placed with the saved creation.
  Your level stays as it is.
- `build_clear()` removes everything players placed.

What gets saved:

- Each object is written in the same format as scenes, so every component on a
  piece comes back.
- Doors and movers are saved closed, even if they were open.
- Links keep working after a load. If an id clashes with an object already in
  the scene, it's renumbered, and links follow.

The file starts with a format version (`okaycreation 1`):

- A newer engine still reads old creations.
- Lines this version doesn't know are skipped, so later versions can add to the
  format.
- A file from a newer version is refused with a clear error.
- A damaged file loads nothing; the scene isn't left half-loaded.

## Scripting hooks

### OkayScript

| Call | Does |
|---|---|
| `build_enter()` / `build_exit()` / `build_toggle()` / `build_active()` | build mode on / off / flip / is it on? |
| `build_link_mode(on)` | link mode on / off (no argument: flip) |
| `build_palette_clear()` / `build_palette_add(name, prefab, icon)` | set the palette; `add` returns the slot (0-based) |
| `build_palette_select(slot)` / `build_selected()` | pick / read the selected slot |
| `build_save(name)` / `build_load(name)` / `build_exists(name)` / `build_clear()` | creations |
| `build_undo()` / `build_redo()` / `build_count()` | history, and how many objects are placed |
| `logic_press(name)` / `logic_state(name)` | press a button / read a source or target |

Events (define the function and it runs):

```
var placed = 0

function on_object_placed(name) {      // build_object, build_id are set too
    placed = placed + 1
    if (build_count() >= 50) { ui_toast("Big build!") }
}
function on_object_removed(name) { placed = placed - 1 }
function on_logic_signal(name) {       // signal_name, signal_on
    if (name == "Vault" && signal_on) { ui_toast("The vault is open") }
}
```

`OnObjectPlaced`, `OnObjectRemoved` and `OnLogicSignal` work as handler names too.

### Visual scripts

| Node | |
|---|---|
| On Object Placed / On Object Removed | events; **Other Name** is the object |
| On Logic Signal | event; **Event Name** / **Event Value** give the signal |
| Enter Build Mode / Exit Build Mode / In Build Mode? | turn it on / off, ask |
| Set Build Palette | `Name=prefab, Name=prefab\|icon.png, ...` (icon optional) |
| Save Creation / Load Creation | by name; a Bool output says if it worked |

## Your own build UI

Turn **Built-in hotbar** off and draw your own with the UI tools or
`ui_*` calls:

- read `build_active()`, `build_selected()` and `build_count()`;
- call `build_palette_select(i)` when a slot is clicked.

The built-in hotbar is ordinary scene UI (a Canvas with buttons and text) that
only appears in build mode. Like the ghost and the wires, it's never saved with
the scene or a creation.

## From C++

```cpp
auto* bm = player->AddComponent<okay::BuildMode>();
bm->AddToPalette("Block", "Assets/block.okayprefab", "Assets/icons/block.png");
bm->AddToPalette("Door",  "Assets/door.okayprefab");
bm->gridSize = 0.5f;
bm->bindings.toggle = 'v';
bm->onPlaced = [](okay::GameObject* go) { /* ... */ };
bm->SaveCreation("castle");
```

Everything players can do is also a call: `PlaceAt` / `PlaceItem`, `Remove`,
`BeginMove`, `Duplicate`, `DropCarried`, `Link`, `Unlink`, `Undo` and `Redo`.
`ComputePlacement` returns where a piece would go and why it can't.
`okay::BuildCreation` saves and loads creations without a Build Mode, for
example on a server.

## Not covered yet

- **Multiplayer:** placements and links aren't synced between players yet.
- **2D:** building is 3D only (it uses the 3D physics raycasts).
- **Pressure plates and zones** sense colliders. An object needs a collider to
  press one.
