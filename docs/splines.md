# Splines, lines and trails

A **Spline** is a curve you draw in the Scene view: points with handles, open
or closed. Other components build on it. A **Spline Follower** moves an object
along it (moving platforms, carts, patrols, flybys), a **Spline Mesh** turns it
into geometry (roads, pipes, curbs, a fence made of repeated sections), a
**Spline Instancer** places objects along it (lamp posts, trees, coins) and a
**Line Renderer** can draw it. Line and Trail Renderers also work on their
own: lasers, ropes and aiming arcs, and the fading ribbon a sword or a
projectile leaves behind.

## Making a spline

**GameObject > Spline** adds an object with three points in front of the Scene
camera, dropped onto whatever is under the camera's target, and picks the
middle point so you can drag it straight away. **Add Component > Gameplay >
Spline** puts one on an existing object.

The points are in the object's local space: moving, turning or scaling the
object moves the whole curve, and everything built on it follows.

| Setting | Meaning |
|---|---|
| Type | **Bezier (handles)**: each point has an in and an out handle that shape the curve. **Catmull-Rom (through points)**: the curve runs smoothly through the points by itself and the handles are not used. |
| Closed | Join the last point back to the first (a loop). |
| Up | How the up direction is carried along the curve. **Minimal twist** uses rotation-minimising frames: it never flips, so loops and corkscrews work. **World up** keeps it as close to the object's up axis as the direction allows, for level roads and fences. |
| Handles (per point) | **Auto Smooth**: placed for you from the neighbours. **Aligned**: in and out stay on one line, their lengths are free. **Free (corner)**: each handle moves on its own. **Linear (straight)**: straight towards the neighbours, for sharp corners. |
| Roll (per point) | Degrees around the direction of travel, blended between points: banks a road, twists a rail. Positive turns the up direction towards the curve's right (see below). |
| Scale (per point) | Width (x) and height (y) of a Spline Mesh's cross-section at that point, blended between points. |

The inspector shows how many points there are and the length, then the points
with numeric fields (local position; **+** inserts a point after that one, on
the curve; **x** removes it), and for the picked point its handle mode,
handles, roll and scale. **Add Point** appends a point past the end, **Snap
Points To Ground** drops every point onto the collider or terrain below it and
**Reverse** runs the curve the other way.

### Editing in the Scene view

Select the object. Every spline in the scene is drawn as a thin blue curve; the
selected one is yellow, with a dot at each point and a short green tick that
shows its up direction (and so its roll).

| Do this | To |
|---|---|
| Click a point | Pick it: it gets move arrows (world axes, or the object's axes in Local mode) and, on a Bezier spline, its two handles. |
| Drag the point | Move it in the plane facing the camera. |
| Drag an arrow | Move it along that axis only. |
| Drag a handle | Reshape the curve. On an Aligned point the other handle turns to match; an Auto Smooth point becomes Aligned. |
| Shift + drag a handle | Break the alignment: the point becomes Free, a corner. |
| Hold Ctrl while dragging | Snap to the grid size (always on when the toolbar's snap is on). |
| Click the curve | Insert a point there. On a Bezier spline the shape does not change. |
| Delete | Remove the picked point instead of the object. |
| Esc | Unpick the point. |
| Click another spline's curve | Select that object. |

Every change is an undo step (Ctrl+Z), and a drag is one step. The curves,
dots and handles are editor overlays: the Game view and the built game do not
show them.

### Right, up and forward

Every place on the curve has a frame: the **tangent** (the direction of
travel), **up**, and **right** = `Cross(up, tangent)`. An object riding the
curve with Face Along on has its forward (+Z) on the tangent, its up on up and
its +X (`transform.right`) on right. Side offsets, follower offsets, roll and
the x of a custom profile all use this right.

### Using a spline from C++

`Spline` (`okay/Components/Spline.hpp`) is evaluated two ways. **By
parameter**, `t` runs from 0 to 1 over all the segments, each segment taking
an equal share whatever its length (`EvaluatePosition`, `EvaluateTangent`,
`EvaluateUp`, `EvaluateScale`, `Sample`). **By distance**, an arc-length table,
rebuilt whenever a point changes, maps units along the curve to the parameter,
so even steps are even steps on the curve (`Length`, `SampleAtDistance`,
`ParameterAtDistance`, `DistanceAtParameter`). A `SplineSample` holds the world
position, tangent, up, right, scale, its parameter and its distance, and
`Rotation()` is the rider's rotation. `NearestParameter`, `NearestPoint` and
`NearestDistance` find the closest point of the curve to a world position.

```cpp
Spline* rail = Spline::Resolve(this, "Rail");          // the Spline on the object "Rail"
SplineSample s = rail->SampleAtDistance(rail->Length() * 0.5f);   // halfway, by length
transform->SetPosition(s.position);
transform->SetRotation(s.Rotation());
float along = rail->NearestDistance(player->transform->Position());
```

Editing: `AddPoint`, `InsertPoint`, `SplitAt(t)` (a new point on the curve; a
Bezier spline keeps its exact shape), `RemovePoint` (on a Bezier spline the
neighbours' handles are lengthened so the curve keeps its shape; it exactly
undoes a split), `SetMode` and `SetHandle`.

## Spline Follower

**Add Component > Gameplay > Spline Follower** moves its object along a spline
on another object (the inspector picks the first spline in the scene to start
with).

| Setting | Meaning |
|---|---|
| Spline | The object that holds the Spline. |
| Drive | **Speed**: units per second along the curve (negative runs backwards). **Time (one pass)**: the whole length in Duration seconds. |
| At The End | **Loop**: start over. **Ping-Pong**: turn round. **Once**: stop. |
| Start At | Where it starts, as a fraction of the length. |
| Play On Start | Start moving with the game (off: start it from a script). |
| Face Along | Turn its forward (+Z) the way it moves, with the spline's up. |
| Offset | From the curve, in the rider's own axes: x along right, y up, z forward. |

Each time it reaches an end, the object's scripts get `on_spline_end(which)`
(`which` is `"end"` at the far end and `"start"` when a ping-pong gets back)
and its Action Lists get the message `spline_end`. In Play the inspector shows
how far along it is, with Stop and Play buttons.

A camera looks down its -Z, so a camera on a follower with Face Along looks
backwards: put the Camera on a child turned 180 degrees around Y. For a camera
that glides along a track while it frames a subject, use a Virtual Camera's
dolly (next).

The follower moves the object and nothing else: whatever stands on a moving
platform is not carried along. Parent it to the platform, or move it from a
script.

### A dolly camera on a spline

A **Virtual Camera** with **Dolly** on also takes an object with a Spline as its
Dolly Path (a DollyPath still works as before). Dolly Position is then a
fraction of the spline's length, and Auto Dolly slides the camera to the point
of the curve nearest the Follow target.

## Spline Mesh

**Add Component > Rendering > Spline Mesh** builds a mesh along a spline into
the object's Mesh Renderer (it adds a Spline and a Mesh Renderer to the object
when they are missing). Colour, texture and shader are the Mesh Renderer's.
The mesh is built again whenever the spline or the settings change, in the
editor and in Play. It is never saved: the scene keeps the
settings, and the mesh is built again when the scene loads.

**Extrude a profile** sweeps a cross-section along the curve:

| Profile | Shape |
|---|---|
| Road | A flat top Width wide with sides Thickness deep. The top sits 2 cm above the curve, so a road drawn on the ground never flickers against it. |
| Flat Strip | A flat ribbon Width wide (paths, markings). |
| Tube | A round pipe Diameter across, with Sides around. |
| Rail / Curb | A solid bar Width by Height standing on the curve (curbs, guard rails). |
| Custom | Your own points across (x) and up (y), listed left to right over the top; Closed Profile joins the last back to the first. |

**Section Spacing** is the distance between cross-sections (smaller: smoother
bends, more triangles). Every point of the spline gets a cross-section too, and
sharp corners are mitred so the width stays even. **UV Tile Length** is how
often the texture repeats along the curve (u runs across the profile, v along
the curve). A point's Scale widens and thickens the profile there.

**Repeat a mesh** bends copies of a mesh along the curve: a built-in mesh
(Fence, Cube, Cylinder, Barrel, Crate, Bench, ...) or a `.obj` / `.okaymesh`
file in the project. **Along Axis** is the mesh's axis that runs along the
curve (the Fence prop is long in X), **Spacing** the gap between copies and
**Mesh Scale** their size. **Stretch To Fit** stretches the copies a little so
they fill the whole length; off, the last gap is left empty.

**Generate Collider** keeps a Mesh Collider on the object, so players walk on
the road and bump into the fence.

## Spline Instancer

**Add Component > Gameplay > Spline Instancer** places items along a spline,
as children of its object.

| Setting | Meaning |
|---|---|
| Spline | The object that holds the Spline (this object when empty). |
| Copy Object | A scene object to copy, such as a lamp post you built. It wins over the two below. |
| Prefab | Or a `.okayprefab` file. |
| Mesh | Or a built-in mesh: Tree, Pine, StreetLamp, Rock, Crate, ... |
| Spacing, Start Offset | The distance between items along the curve, and where the first one goes. |
| Side Offset, Both Sides | Across the curve, towards its right (negative: the other side). Both Sides adds a mirrored item on the other side, turned round so both face the curve the same way (lamp arms over a road from both sides). |
| Align | **Upright**: items stand along the object's up axis and face along the curve. **Follow Curve**: they also tilt and roll with it. |
| Rotation | An extra turn for every item (degrees). |
| Jitter, Turn Jitter, Scale Min / Max | A random offset along and across the curve, a random turn about up and a random size. |
| Seed | The same seed places the same items; **New** picks another. |
| Max Items | A cap (500 to start with). |

The items are placed again whenever something changes and when the scene
loads; they are never saved. The hierarchy lists them, but a change made to
one by hand is lost at the next rebuild. **Rebuild** places them again after
you edit the object they copy.

## Line Renderer

**Add Component > Rendering > Line Renderer** draws a ribbon through points: a
laser, a rope, a path on the ground, an aiming arc.

| Setting | Meaning |
|---|---|
| Draw Spline | Draw an object's spline instead of the points below (Samples points along it; a closed spline makes a loop). |
| Points, World Space, Loop | The points, in world space or, with World Space off, in the object's space (they move with it). Loop joins the last point to the first. |
| Width and its curve | The width in world units, times a curve along the line (0 is the start, 1 the end). |
| Colour and gradient | The colour, times a gradient along the line; its alpha fades the line. |
| Alignment | **Face Camera**: the ribbon turns towards the camera. **Flat (object up)**: it lies flat, facing the object's up axis (markings on the ground). |
| Corner Vertices | Round each bend with this many steps (0: mitred). |
| End Cap Vertices | Round both ends with this many steps (0: square). |
| Texture, Texture Mode | An optional PNG or JPG, tinted by the colour. **Stretch**: it runs once along the whole line. **Tile**: it repeats every Tile Length units. |

Scripts can set the points every frame (see Scripts).

## Trail Renderer

**Add Component > Rendering > Trail Renderer** leaves a ribbon behind its
object as it moves.

| Setting | Meaning |
|---|---|
| Time | Seconds each point of the trail lives. |
| Min Vertex Distance | A new point once the object has moved this far. |
| Width and its curve | The width, times a curve over a point's life (0 new, 1 about to vanish). It narrows to nothing to start with. |
| Colour and gradient | The colour, times a gradient over a point's life. White fading out to start with. |
| Emitting | Off: no new points; the trail fades away. |
| Alignment, Texture | As for the Line Renderer. |
| Teleport Distance | A jump longer than this in one frame (a teleport, a respawn) starts a new trail instead of drawing a streak across the scene. 0: never. |

In the editor the trail shows while you move the object with the gizmo, and
**Clear** empties it.

## How lines and trails are drawn

Both build their triangles on the CPU every frame and draw them over the 3D
image, alpha-blended, the way particles are, so they look the same with every
renderer. Ribbons are sorted back to front and clipped where they pass behind
the camera. Where the scene's depth is known they hide behind solid objects:
with the software renderer, and in the player, which runs a half-resolution
depth pass when the scene has particles, lines or trails. A line then gets an
extra vertex every 12 pixels on screen, so only the part behind something
disappears. In a 2D view they are drawn flat, in world units.

## Scripts

The OkayScript builtins act on this object's component, or on another
object's when the first argument is its name:

| Builtin | What it does |
|---|---|
| `spline_point(name, t)` | The world point a fraction `t` (0 to 1) of the way along that object's Spline, by length (`spline_point(t)` for this object's). |
| `spline_length(name)` | Its length in world units. |
| `follower_set_speed([name,] speed)` | Set a Spline Follower's speed. |
| `line_set_points([name,] points)` | Replace a Line Renderer's points with an array of vectors, or with several vectors. |
| `trail_clear([name])` | Remove a Trail Renderer's trail at once. |

```
function start() {
    line_set_points([vec3(-5, 1.2, 2), spline_point("Rail", 0.5)])
}
function on_spline_end(which) {
    if which == "end" { trail_clear("Comet") }
}
```

C#-style scripts get `Spline` (`EvaluatePosition(t)` and `EvaluateTangent(t)`
with `t` a fraction of the length, `Length()`, `NearestPoint(position)`,
`pointCount`, `closed`), `SplineFollower` (`Play()`, `Stop()`, `speed`,
`distance`, `isPlaying`), `LineRenderer` (`SetPosition(i, p)`,
`SetPositions(array or List of Vector3)`, `GetPosition(i)`, `positionCount`,
`width`) and `TrailRenderer` (`Clear()`, `emitting`). A public `OnSplineEnd()`
method (it gets no argument) hears the follower reach an end.

```csharp
var rail = GameObject.Find("Rail").GetComponent<Spline>();
var laser = GetComponent<LineRenderer>();
laser.SetPositions(new Vector3[] { transform.position, rail.NearestPoint(transform.position) });
GetComponent<TrailRenderer>().Clear();
```

Visual scripts: **Get Point On Spline** (in: T, 0 to 1; out: Point) and
**Spline Length** under Data, **Set Line Points** (in: a list of points) and
**Clear Trail** under Actions, each acting on this object or the Target, plus
the generated `Spline.*`, `SplineFollower.*`, `LineRenderer.*` and
`TrailRenderer.*` nodes (see [engine_api.md](engine_api.md)).

## Scene records

Each component is a `key=value` record of its own, so scenes without them load
as before. Generated meshes and placed items are not saved.

```
spline type=0 closed=0 up=0 res=32                  # Bezier / Catmull-Rom, Minimal / World up
splinept x= y= z= ix= iy= iz= ox= oy= oz= mode= roll= sx= sy=   # one line per point
splinefollower spline="Rail" drive= wrap= speed= dur= start= auto= orient= offx= offy= offz=
splinemesh spline="" mode= profile= width= height= sides= custom="x y x y" cclosed= seg= uv= src="Fence" axis= spacing= msx= msy= msz= fit= collider=
splineinstancer spline= tmpl= prefab= mesh= spacing= start= side= both= jitter= rotj= smin= smax= seed= align= rx= ry= rz= max=
linerenderer pts="x y z x y z" world= loop= width= cR= cG= cB= cA= corner= cap= align= texmode= tile= tex= spline= samples= wn= ... gn= ...
trailrenderer time= mindist= width= cR= cG= cB= cA= emit= align= texmode= tile= tex= teleport= wn= ... gn= ...
```

Width curves are `wn` keys followed by `w0t w0v w1t w1v ...` (time, value) and
`ws` (smooth); gradients are `gn` keys followed by `g0t g0R g0G g0B g0A ...`.

## Limits

- Clicking a Catmull-Rom curve inserts a point there, but the curve then has
  to pass through it, so the shape changes a little. Only Bezier splits are
  exact.
- Spline Mesh roads, tubes and strips are open at their two ends (no caps),
  and a road has no underside.
- Lines and trails are not lit and cast no shadows. In the editor, when a GPU
  renderer draws the view, they are not hidden behind objects (particles
  behave the same way there); the player hides them.
- An object saves one component of each kind: one Line Renderer, one Trail
  Renderer, one Spline.
