# Minimap and turn order

Two ready-made pieces that need no script to put on screen: a map in the
corner, and a list that says whose turn it is.

## The minimap

The minimap is a picture of the ground, drawn from straight above. East is to
the right. North is up. It is not a camera looking across the world, and it
does not tip or show buildings in perspective. **Heading Up** turns that flat
picture so the player faces the top of the map; it does not tilt it.

Add it with **Add Component > UI > Minimap** (it is also in the UI view's add
list, with the other HUD widgets). The panel uses **Pos**, **Size** and
**Anchor** like any other widget. **Background**, **Border** and **Border
Width** are the frame.

**Target** is the name of the object the map stays centred on. Leave it empty
and the map stays on the world origin. **Use XZ (3D)** is on for a 3D game:
the map reads world X as east and world Z as north. Turn it off for a 2D game,
which uses X and Y instead.

**World/Pixel** is the zoom. It is how many world units one map pixel stands
for. A smaller number is a closer view.

### Marks on the map

Anything you want to see as a dot needs a **Minimap Blip** (**Add Component >
UI > Minimap Blip**). A blip is drawn on every minimap. Set its **Color**,
**Size** and **Shape** (square, dot, triangle or arrow). **Face Heading** on a
triangle or arrow points it the way that object faces. **Icon** replaces the
shape with a picture. **Label** is the name drawn beside it when the map's
**Show Labels** is on.

On the map itself:

| Setting | What it does |
| --- | --- |
| **Blip Size** | Size used when a blip does not set its own. |
| **Blip Range** | Hide marks farther than this many world units from the centre. 0 shows everything. |
| **Clamp Blips** | A mark that would fall off the panel is pinned to the edge, so you can still tell which way it is. |
| **Show Self** | Draw the centre mark. Off hides it. |
| **Player Arrow** | The centre mark is an arrow pointing the way **Target** faces. Off draws the target colour as a plain mark. |
| **Heading Up** | Spin the map so **Target** always faces up. |
| **Circular** | A round map instead of a box. Marks outside the circle are hidden (or pinned to the rim, if **Clamp Blips** is on). |
| **Grid** and **Spacing** | Faint lines, **Grid Color**, so distances are easier to read. Drawn on a square map only, not a circular one. |
| **North Tick** | A mark at the edge for north. It moves when the map spins. |
| **View Cone** | A faint wedge in front of the player. **Cone Angle** and **Cone Length** are how wide and how long it is drawn, in degrees and in pixels. This wedge is only a drawing. It is not an NPC's sight. |
| **Range Rings** | Up to six circles around the centre. |

### A map you draw, and a big map

**Map Image** is a picture of the world laid under the marks. **World Size** is
how many world units the whole picture spans (it is square). **World Center**
is the east and north of the picture's middle. A picture stays north-up: the
map does not spin, and the player arrow turns instead, so the art stays the
right way up.

**Map Shapes** are vector marks in the same east / north plane:

- **Line** is a road or a wall. Thickness is in pixels.
- **Rect** is a house, from two opposite corners. **Fill** paints it in, or leave it as an outline.
- **Poly** is a zone, from as many points as you give it.

**+ Road**, **+ House** and **+ Zone** add one of each. **Generate from Scene**
adds a rectangle for every mesh, from that object's position and size, so the
map matches the level you built. **Clear Shapes** removes them. Shapes are part
of the map component, so they save with the scene.

**Markers** are single points (a shop, an exit) drawn as diamonds. Each has a
world position, a colour, a size and a label. **Route To Marker** draws a line
from the player to that marker. -1 draws none. **Route Width** and **Route
Color** style the line.

**Fullscreen Key** (M, unless you change it; blank turns it off) opens a large
map over the screen. **Fullscreen Fill** is how much of the screen it uses.
**Fullscreen Zoom** multiplies **World/Pixel** while it is open, so the big map
can show more ground. With **Click To Set** on, a left click on that big map
drops a waypoint and a right click clears it. The line to the waypoint uses
the route style. **Clear WP** removes it from the inspector.

**Zoom In** and **Zoom Out** are keys that step **World/Pixel**, clamped to
**Min Zoom** and **Max Zoom**. Leave a key blank and it does nothing.

### When the map looks wrong

- Nothing is drawn but the frame: no object has a **Minimap Blip**, or **Blip Range** is shorter than the distance to them.
- Marks sit in the wrong place: a 3D scene needs **Use XZ** on. A 2D scene needs it off. Markers and shapes use the same east / north.
- The map tracks the wrong object, or the origin: **Target** has to be that object's name, spelled the same way.
- The picture is shifted or scaled wrong: **World Size** and **World Center** have to match the art.
- A blip you expected at the edge is missing: **Clamp Blips** is off, so anything outside the panel is skipped. **Circular** also cuts the corners.

## Turn order

**Add Component > RPG / Turn-Based > Turn Manager** keeps an ordered list of
names for a turn-based fight or a board game. Type each participant's object
name, top to bottom, in the order they should act. **Add** appends a row.
The row with the arrow is the one acting now. The header shows the round and
that name.

**Auto Start** calls the first turn when the scene starts. Turn it off if
something else should decide when the fight begins.

**End Turn** advances to the next name. In the editor the **End Turn
(preview)** button does that while you are looking at the component. In the
game, code calls `EndTurn()` on the component. When the last name finishes,
the list wraps to the first and the round goes up by one.

Each time a turn starts, that object's script receives `on_turn_start`. When
the list wraps, the turn manager's own object receives `on_round_start` as
well. A name that matches no object still takes its place in the order; no
script is called for it. Removing a row (the X, or `Remove` in code) drops
that name and keeps the current turn pointed at whoever should act next.
`SetTurn` jumps to a row by index, for a fresh initiative order.
