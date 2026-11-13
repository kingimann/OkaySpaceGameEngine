# Navigation meshes

NPCs, click-to-move players and `NavMeshAgent`s path over a **navigation mesh**:
convex polygons baked from the scene's static colliders, joined by portals. A
scene that never baked one keeps working: the same calls fall back to the
probe grid (`NavGrid3D`), which samples the colliders lazily around each query.
The mesh is faster, knows about ledges, slopes, head room and floors above
floors, and gives smooth string-pulled paths.

## What exists and what obstacle carving added

Audit of the navigation code before the carving work, and where each item stands now.

| Item | Status | Where |
|---|---|---|
| Baked mesh from static colliders, portals, A*, funnel | HAVE | `AI/NavMesh*.cpp`, `AI/NavMeshBake.cpp` |
| Areas, per-area costs, per-agent area masks | HAVE | Areas and costs, below |
| Off-mesh links (jump, drop, ladder) | HAVE | Off-mesh links, below |
| Agent that follows corners, avoids agents and obstacles locally, replans on a timer | HAVE | `Components/NavMeshAgent.hpp` |
| Moving box (`NavMeshObstacle`): local avoidance, velocity, never baked, editor box gizmo, inspector, registry and scene records | HAVE (box only, `size` / `center`) | `Components/NavMeshObstacle.hpp` |
| Obstacle cuts the mesh so the PATH SEARCH goes around it | GAP, now built | `NavMesh::Carve`, `AI/NavMeshCarve.cpp` |
| Capsule obstacle | GAP, now built | `NavMeshObstacle::Shape::Capsule` |
| Rotated boxes carve their true footprint | GAP, now built | `NavCarveRects` (strips along the footprint) |
| Carve only when stationary, time to stationary | GAP, now built | `carveOnlyWhenStationary`, `timeToStationary` |
| Carve while moving (hole follows, rebuilt past a distance) | GAP, now built | `moveThreshold` |
| Agents already walking replan when a hole appears, moves or goes | GAP, now built | `Navigation::CarveGeneration`, `NavMeshAgent::Update` |
| NPC and click-to-move controllers go round carved obstacles | GAP, now built (on their own repath timer, 0.6 s) | `Navigation::Mesh` |
| Editor: carve footprint gizmo, inspector fields, Add menu | GAP, now built | `editor/NavigationWindow.cpp` |
| Validator rules, property registry entries, scene records, save-coverage entry | GAP, now built | `navobstacle.*` codes, `navobstacle` record |
| Carving in the editor outside Play | NOT built | obstacles only decide to carve while the scene runs; the gizmo previews the footprint |
| Per-agent carving (an obstacle only some agents respect) | NOT built | a carve is a hole in the shared mesh; use area masks for per-agent rules |
| Carve cost / "walk through at a price" (soft obstacle) | NOT built | a hole is blocked outright |
| Bake settings per agent size, mesh tiles (partial rebuild of a huge mesh) | NOT built | the carved mesh is rebuilt whole when the carving set changes |

## Baking

1. Open **View > Editors & Tools > Navigation** (or add a **Nav Mesh Surface**
   from Add Component > Gameplay). The window offers **Add Nav Mesh Surface**
   when the scene has none; one surface per scene is the normal case.
2. Set the agent size and the limits, press **Bake**. The bake runs on a
   background thread with a progress bar; the editor stays responsive. Stats
   (polygons, links, bake time, file) appear when it is done and the Scene
   view shows the mesh (translucent blue polygons; turn it off under Scene
   View in the window).
3. The result is written to a `.okaynav` file next to the scene
   (`Level.okayscene` -> `Level.okaynav`) and the scene file only references
   it (`navsurface ... path=...`). Build Game copies it with the scene. The
   file is text and diffs cleanly.

**Bake before Play** on the surface rebakes every time Play starts, so the mesh
always matches what you just moved. **Clear** drops the mesh and its file.

What is baked: every enabled, non-trigger `BoxCollider3D` / `MeshCollider3D`
(oriented boxes), `SphereCollider3D`, `CapsuleCollider3D` /
`CylinderCollider3D`, and heightmap `Terrain`s. Left out: objects that move -
anything with a `NavMeshAgent` or `NavMeshObstacle`, the player and NPC
controllers, `Character`s, projectiles, vehicles - and, unless **Include
Dynamic Bodies** is on, bodies with a dynamic `Rigidbody3D`. A layer mask on
the surface can exclude more.

| Setting | Meaning |
|---|---|
| Agent Radius | The walkable area is shrunk by this much from every wall and ledge. Bake with your widest agent; a doorway narrower than twice the radius closes. |
| Agent Height | Head room a floor needs. The floor under a low bridge is not walkable. |
| Max Slope | Steepest walkable surface, from the collider's surface normal (a tilted box ramp, a terrain hillside). |
| Max Step | Tallest step or ledge an agent walks over. Neighbouring cells further apart in height are not connected. |
| Cell Size | Bake resolution in world units. Walls thinner than a cell can be missed; doorways are resolved to a cell. Keep it below `Max Step / tan(Max Slope)` so ramps stay connected cell to cell (0.25 is fine for 0.4 / 45 deg). |
| Cell Height | Height tolerance: how far the ground may bend before a polygon is split. Flat floors and planar ramps are exact; terrain is approximated within this. |
| Auto Bounds | Bake everything the colliders cover (padded). Off: only the given box. |

How the bake works (engine/src/AI/NavMeshBake.cpp): every collider is
rasterized into a heightfield of solid spans per column (so floors can stack:
a bridge deck and the floor under it are separate spans), the top of each
span with enough head room becomes a floor cell, cells steeper than Max
Slope are dropped, neighbours within Max Step that share head room are
connected, the walkable area is eroded by the agent radius with a chamfer
distance transform, and the remaining cells are merged into axis-aligned
rectangles that stay within Cell Height of a plane. Each rectangle is a
convex polygon with per-corner heights; neighbouring rectangles are joined by
portals (the shared part of their common edge), which is what A* and the
funnel use.

## Areas and costs

Every baked polygon carries one of 32 **areas**. Built in: 0 Walkable, 1 Not Walkable, 2 Water, 3 Road;
4..31 are yours to name (Navigation window > Areas, or `NavMeshSurface.areaName`). Put a **Nav Mesh
Modifier** on an object to choose the area its colliders bake as (Add Component > Gameplay > Nav Mesh
Modifier). It also covers child objects that have no modifier of their own (**Apply To Children**).
Where two colliders overlap with level tops (a road slab laid flush in a swamp), the higher area
number wins; otherwise the surface on top decides. A **Not Walkable** collider cuts a hole in the
mesh (a pit, a lava pool). Only cells of one area merge into a polygon, so the area is exact to a
cell. Bake again after changing an area; the polygons in the `.okaynav` file keep it (`p v0 v1 v2 v3 area`;
files from before areas load as all Walkable).

What an area **costs** lives on the surface (`areaCost[32]`, the Areas table), not in the bake, so it
can be tuned without rebaking. The path search charges `distance * cost` for the stretch inside each
polygon: defaults are Water 3 and Road 0.5, every other area 1. Below 1 is preferred, above 1 avoided.
The funnel pass only straightens the route inside the chosen polygons, so it never cuts across a
costly area the search went round. The search measures each polygon from portal midpoint to portal
midpoint, so on a few very large polygons it is approximate: a detour around a small swamp only wins
when the swamp's cost outweighs the extra walking, so give strongly avoided areas a clearly higher cost.

Which areas an agent may enter is its **Area Mask** (`NavMeshAgent.areaMask`, default every area
but Not Walkable). A forbidden area is never entered; a goal inside one, or one only reachable through
one, has no path (the agent gives up, as for any unreachable goal). The start polygon may be
forbidden, so an agent standing in a forbidden area can walk out. Other callers
(`Navigation::FindPath`, the NPC and click-to-move controllers) use the default mask.

```cpp
std::vector<Vec3> corners; std::vector<NavMeshLink*> legs;
const uint32_t noWater = kNavDefaultAreaMask & ~(1u << NavArea::Water);
bool ok = Navigation::FindPathForAgent(scene, from, to, corners, legs, noWater);
```

## Off-mesh links

A **Nav Mesh Link** joins two points the ground does not: a jump over a gap, a drop off a ledge, a
ladder. Points are in the object's local space and must lie on the baked mesh (within 1.5 m; the
Inspector says when an end is off it). Fields: **Type** (Jump arcs `arcHeight` above the straight line,
Drop falls slow then fast, Ladder climbs a straight line), **Bidirectional** (off: start to end only),
**Cost** (multiplier on the crossing distance, on top of the link area's cost), **Area** (checked
against the agent's mask, so an agent can opt out of a kind of link), **Speed** (0 = the agent's own).
A disabled component or inactive object is not used. The Scene view draws each link as two rings, the
crossing arc and an arrow head per direction (grey when unusable), and the mesh in area colours.

Planning treats each usable link as one more edge (two for a bidirectional link), so a link is taken
only when it makes the route cheaper. The path from `Navigation::FindPathForAgent` has a parallel `legs`
vector: a non-null entry means the leg ending at that corner is that link's crossing. A
`NavMeshAgent` walks to the link's start, crosses to the end over `distance / speed` seconds (it is
placed exactly on the end point), and walks on; it does not stop mid-crossing and repaths only after
landing. `agent.onLinkStart` / `onLinkEnd` (`std::function<void(NavMeshAgent&, NavMeshLink&)>`) fire at the
two ends in that order, for an animation hook; Action Lists get `nav_link_start` and `nav_link_end`.
`IsTraversingLink()`, `CurrentLink()` and `LinkProgress()` report the crossing. An agent driven by an
NPC or click-to-move controller does not use links.

## Obstacle carving

A **Nav Mesh Obstacle** with **Carve** on cuts its footprint out of the navigation mesh while the scene
runs, so the path search itself goes around it. Without Carve an obstacle is only dodged up close; the planned
route still runs through it, so a parked cart across a corridor can wedge an agent. With it, the route
bends around the cart, and if the cart seals the only way the goal has no path (the agent gives up, as for
any unreachable goal; an NPC or click-to-move controller steers straight at the goal as it does for any
failed search).

| Field | Meaning |
|---|---|
| Shape | Box (`size`, scaled by the Transform; a rotated box carves its true footprint, not its bounding square) or Capsule (`capsuleRadius`, `capsuleHeight`, an upright capsule; a circle on the ground). |
| Center | Offset from the Transform, local units. |
| Carve | Cut a hole in the mesh. Off by default on the component (old scenes are unchanged); the editor's Add Component item turns it on. |
| Carve Only When Stationary | On (default): the hole exists once the obstacle has not moved for **Time To Stationary** seconds (default 0.5) and closes the moment it moves, so a patrolling cart does not rebuild the mesh every frame and is only dodged locally while it moves. Off: the hole follows the obstacle. |
| Time To Stationary | Seconds without moving before it counts as stationary. 0 carves on the first still frame. |
| Move Threshold | Follow mode only: the hole is rebuilt once the obstacle has moved this far (default 0.1) from where it was last carved. 0 rebuilds every frame it moves (the validator warns). |

How it works (engine/src/AI/NavMeshCarve.cpp): the obstacle's footprint (the hull of a box's eight corners, or a
circle for a capsule) is covered by axis-aligned strips one cell wide, each grown by the bake's agent radius so
agents keep their body clear. Every polygon whose floor the obstacle stands on (it must reach above the floor
and start below head room, so a thing hovering above the agents or buried under the floor carves nothing,
and a bridge deck is cut only where the obstacle pierces it) has the strips subtracted, leaving up to four
rectangles with heights taken from the original polygon; the portals between the pieces are rebuilt from the
original ones. The result is a copy of the mesh on the surface (`NavMeshSurface::CarvedMesh()`); the baked
mesh and the `.okaynav` file never change, and `Navigation::Mesh` (what every query uses: paths, raycasts,
random points, sampling) returns the carved copy while anything carves, `Navigation::BakedMesh` the original.
The copy is rebuilt only when the set of carving volumes changes. Obstacles only carve while enabled and
active. Bake or Clear on the surface drops the carve and the next query carves the new mesh.

Agents already walking: every rebuild bumps `Navigation::CarveGeneration`, and a `NavMeshAgent` that planned under
an older value replans on its next update (no waiting for the repath timer, and it works with Auto Repath off).
NPC and click-to-move controllers repath on their own interval (0.6 s) and so pick the hole up within that.
A destination inside a hole, or cut off by one, is unreachable; the agent stops rather than walking into the obstacle.

```cpp
auto* cart = go->AddComponent<NavMeshObstacle>();
cart->size = {1, 2, 8};
cart->carve = true;                  // stationary-only, 0.5 s, by default
cart->timeToStationary = 0.25f;
// ... later: cart->Carving() says whether the hole is in the mesh right now
```

Validator codes (`docs/automated_testing.md`): `navobstacle.zero_size`, `navobstacle.carve_no_surface`,
`navobstacle.carve_no_mesh`, `navobstacle.carve_every_frame`.

## Queries (`okay/AI/NavMesh.hpp`, `okay/AI/Navigation.hpp`)

`Navigation` is the facade every caller goes through; it uses the baked mesh
when the scene's `NavMeshSurface` has one and the grid otherwise:

```cpp
std::vector<Vec3> path = Navigation::FindPath(scene, from, to);   // corners, start first
bool  onMesh = Navigation::SamplePosition(scene, p, 2.0f, snapped);
bool  blocked = Navigation::Raycast(scene, a, b, hit);           // straight line along the mesh
bool  ok = Navigation::RandomPoint(scene, home, 6.0f, seed, wanderTo);
float y = Navigation::SampleHeight(scene, p, p.y);
```

On the `NavMesh` itself: `FindPath` (A* over the polygon graph with portal
midpoints as node positions, then the funnel algorithm across the portals; pass
`smooth = false` to get the polygon-centre path instead), `FindNearestPoly` /
`SamplePosition` (prefers the polygon closest in height, so a point on a
bridge finds the deck), `Raycast`, `RandomPoint` / `RandomPointNear`,
`HeightAt` / `SampleHeight`. Queries reuse their scratch buffers, so a dozen
agents can plan every frame. An unreachable destination (another island) is a
`false`, not a path to the nearest point.

## Components

**Nav Mesh Surface** - the bake settings, the `.okaynav` path, `Bake()` /
`StartBake()` + `PollBake()`, `Mesh()`. Loads the file the first time the
mesh is needed (scene load, first query, the editor overlay).

**Nav Mesh Agent** - `SetDestination(point)`, `Stop()`, `Resume()`,
`HasPath()`, `RemainingDistance()`, `IsStopped()`, `Arrived()`, `Velocity()`,
`Warp(point)`. Fields: radius, height, speed, acceleration, angular speed,
stopping distance, auto repath + interval, avoidance priority (0 = others
yield, 99 = yields to everyone), avoidance range, auto braking, face movement,
drive animation (a sibling `Character`), base offset (origin height above
the feet - half the height for a cube). Each frame it follows the next corner
of its path, steers around other agents and `NavMeshObstacle`s by sampling
velocities (reciprocal: each side takes its share of the dodge, more for the
lower-priority one), replans on the interval and when wedged, and gives up
after a few stuck replans. It drives a sibling `Rigidbody3D` by velocity, or
moves the Transform projected to the mesh height. On an object that has an
`NPCController` or `ClickToMoveController` it hands the destination to that
controller (`CommandGoTo` / `MoveTo`) and only reports. Broadcasts
`nav_arrived` to Action Lists when it gets there.

**Nav Mesh Obstacle** - a box or capsule agents go around (a cart, a sliding door, a barrel). It is never
baked, so it may move every frame; agents always steer around it locally. With **Carve** on it also cuts a hole
in the mesh (next section).

The Scene view draws (with gizmos on) the mesh polygons, every agent's path as
a cyan polyline with the current corner marked in pink and a ring at the
destination, and obstacles as orange boxes or capsules (with a red footprint while they carve). **Send Selected Agent To Click**
in the Navigation window (or on the agent's inspector) orders the selected
agent to the ground point you click in the Scene view, which is the quickest
way to try a bake in Play.

## Scripts

OkayScript, on the object's own agent (or `nav_move_to("Guard", x, y, z)` for
another object):

```
nav_move_to(12, 0, -4)
if nav_arrived() { say("here") }
var left = nav_remaining()
nav_stop()
```

C#-style scripts: `NavMeshAgent` with `SetDestination(Vector3)`, `Stop()`,
`Resume()`, `HasPath()`, `RemainingDistance()`, `Arrived()`, and the
properties `speed`, `stoppingDistance`, `isStopped`, `velocity`,
`destination`:

```csharp
var agent = GetComponent<NavMeshAgent>();
agent.SetDestination(target.position);
if (!agent.HasPath()) Debug.Log("arrived");
```

Visual scripts: **Nav Move To** (in: Point), **Nav Stop** and **Nav Arrived**
(out: Bool) under Actions / Data, plus the generated `NavMeshAgent.*` nodes.

NPC Controllers with **Use Pathfinding** and Click To Move Controllers route
over the mesh automatically once the scene has one; nothing to change on
them. Without a mesh they keep the grid.

## The `.okaynav` file

```
okaynav 1
settings <radius> <height> <slope> <step> <cell> <cellHeight>
bounds <minX> <minY> <minZ> <maxX> <maxY> <maxZ>
verts N
v x y z
polys M
p i0 i1 i2 i3          # corner vertices: (minX,minZ) (maxX,minZ) (maxX,maxZ) (minX,maxZ)
links K
l from to ax ay az bx by bz   # a portal: the part of the shared edge both polygons own
```

Unknown records are skipped, so a newer build's file still loads. A file whose vertices are
not finite numbers (`nan`, `inf`) is refused with "non-finite vertex" and loads nothing. The
lookup grid behind the queries is at most 2048 cells a side, so a mesh far larger than 16 km
gets bigger cells rather than a bigger grid. A mesh whose extent overflows a float loads but
has no grid, and every query on it finds nothing.

## Limits

- The older grid pathfinder (`Pathfinding::AStar`, `OnTilemap`) takes at most
  `Pathfinding::kMaxCells` cells (width times height, 4096 x 4096); a larger grid
  returns no path.
- Polygons are axis-aligned rectangles at cell resolution: walls at an angle
  get a staircase edge one cell wide. Lower the cell size where it shows.
- One agent radius per bake. A game with both rats and ogres bakes for the
  ogre or keeps a second surface in another scene.
- Carving needs a baked mesh: in a scene that only has the probe grid an obstacle is dodged locally
  only. The grid fallback knows nothing of areas, links or carving. Carving runs while the scene runs;
  in the editor outside Play the obstacle only previews its footprint. A carve is a hole in the shared
  mesh (every agent respects it), not a cost, and the carved copy is rebuilt whole when the set of
  carving obstacles changes (fine for a handful; a crowd of hundreds of carving obstacles that each move
  every second would want mesh tiles, which do not exist).
- Carving is exact to the strip grid (one cell): a diagonal edge is a staircase, and the hole is grown by
  the agent radius on all sides (a square at the corners of a rotated box), so a very narrow gap between
  an obstacle and a wall can close.
- Area costs and masks apply to the baked mesh only; an agent driven by an NPC or
  click-to-move controller uses the default mask and no links. A link whose ends are
  more than 1.5 m from the mesh is skipped. Link crossings are not blocked by geometry
  (the arc passes through walls); place links where the way is clear.
