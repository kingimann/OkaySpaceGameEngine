# Navigation meshes

NPCs, click-to-move players and `NavMeshAgent`s path over a **navigation mesh**:
convex polygons baked from the scene's static colliders, joined by portals. A
scene that never baked one keeps working: the same calls fall back to the
probe grid (`NavGrid3D`), which samples the colliders lazily around each query.
The mesh is faster, knows about ledges, slopes, head room and floors above
floors, and gives smooth string-pulled paths.

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

**Nav Mesh Obstacle** - a moving box agents steer around locally (a cart, a
sliding door). It is never baked, so it may move every frame. Carving holes
into the mesh is not supported; bake walls that stand still.

The Scene view draws (with gizmos on) the mesh polygons, every agent's path as
a cyan polyline with the current corner marked in pink and a ring at the
destination, and obstacles as orange boxes. **Send Selected Agent To Click**
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

Unknown records are skipped, so a newer build's file still loads.

## Limits

- Polygons are axis-aligned rectangles at cell resolution: walls at an angle
  get a staircase edge one cell wide. Lower the cell size where it shows.
- One agent radius per bake. A game with both rats and ogres bakes for the
  ogre or keeps a second surface in another scene.
- Off-mesh links (jumps, ladders) and obstacle carving are not implemented;
  the grid fallback does not know about them either.
