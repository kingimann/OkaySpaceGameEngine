# 9. AI & navigation

Baked navigation meshes with agents and obstacles, the older probe grid and 2D grid A*, behavior trees with a blackboard, and
the NPC Controller with built-in perception. Code is `engine/include/okay/AI/*` (6 headers), `engine/src/AI/*` (about 4.5k lines),
seven navigation components and two editor windows. Docs: `docs/navigation.md`, `docs/behavior_trees.md`, `docs/npcs_and_crafting.md`.
No open PR touches it; the `nav_*`, `bt_*` and `npc_*` script builtins live in `Scripting/OkayScriptVM` (area 13).

## 9. AI & navigation

### 9.1 Nav mesh bake and queries
- **What:** Bakes the scene's static colliders and terrain into a mesh of walkable polygons (on a background thread), saves it to a `.okaynav` file, and answers path, nearest-point, raycast, random-point and height queries through one facade that falls back to a probe grid when no mesh was baked.
- **Parts:**
  - 9.1.1 Bake - `AI/NavMeshBake` - copies colliders and heightmap terrains, rasterizes them into a heightfield, applies slope, step, head room and agent radius, merges cells into rectangles; runs on a worker thread with progress
  - 9.1.2 Mesh data and queries - `AI/NavMesh` - polygons and portals, A* over the polygon graph, funnel string pulling, `SamplePosition`, `Raycast`, `RandomPoint`, `HeightAt`, text save and load
  - 9.1.3 Surface component - `Components/NavMeshSurface`, `engine/src/AI/NavMeshSurface.cpp` - bake settings, `.okaynav` path next to the scene, `Bake`, `StartBake`, `PollBake`, bake before Play
  - 9.1.4 Facade and grid fallback - `AI/Navigation`, `AI/NavGrid3D` - `Navigation::FindPath` uses the baked mesh, else lazily sampled grid A* over probe spheres; NPCs, click-to-move and agents all go through it
  - 9.1.5 Navigation window - `editor/NavigationWindow.*` - bake settings, Bake and Clear with progress and stats, Scene view overlay, send-agent-to-click, navigation component inspectors
- **Code:** `AI/NavMesh`, `AI/NavMeshBake`, `AI/Navigation`, `AI/NavGrid3D`, `Components/NavMeshSurface`, `engine/src/AI/NavMeshSurface.cpp`, `editor/NavigationWindow.*`
- **API:** `NavMeshSurface`, `NavMesh`, `NavMeshBuildSettings`, `Navigation::FindPath`, `Navigation::SamplePosition`, `Navigation::Raycast`, `Navigation::RandomPoint`, `NavGrid3D::FindPath`, script `nav_path_exists`, `nav_path_length`, `nav_point_walkable`, `nav_nearest_point`
- **Editor:** View > Editors & Tools > Navigation (Add Nav Mesh Surface, Bake, Clear); Scene view mesh overlay; Add Component > Gameplay > Nav Mesh Surface
- **Docs:** `docs/navigation.md`
- **Tests:** `navmesh`, `script_nav`
- **Status:** Works - NavMesh.cpp, NavMeshSurface.cpp, Navigation.cpp and the facade header read; bake and path behaviour (walls, doorways, ramps, islands) covered by `navmesh` and `script_nav`; a `.okaynav` with a non-finite vertex is refused and the lookup grid is capped at 2048 cells a side (`navmesh`)
- **Open:** per docs: polygons are axis-aligned rectangles (angled walls get a one-cell staircase); one agent radius per bake; no obstacle carving (area types and off-mesh links are 9.6); Navigation uses only the first enabled surface in a scene; with no mesh `FindPath` always returns a path (a straight line when the grid finds none), so the grid cannot report an unreachable goal
- **Related:** 9.2, 9.5, 4.4, 6.2, 2.6, 27.4
- **Words:** navmesh, nav mesh, NavMeshSurface, bake, .okaynav, pathfinding, funnel, walkable, navigation window, NavGrid3D, probe grid, agent radius, max slope, step height
- **Seen as:** enemies walk straight into walls or cannot find a way around; the mesh is missing near a doorway; enemies ignore a ramp; the Bake button does nothing; the blue overlay does not match the level; a character cannot reach a platform
- **Ask me like:** "bake a navmesh for my level"; "my guard will not walk through the narrow door"

### 9.2 Nav agents and obstacles
- **What:** A Nav Mesh Agent walks its object to a destination along the mesh path, steers around other agents and moving obstacles, replans when stuck and reports arrival; a Nav Mesh Obstacle is a moving box agents dodge.
- **Parts:**
  - 9.2.1 Agent movement - `Components/NavMeshAgent`, `engine/src/AI/NavMeshAgent.cpp` - `SetDestination`, `Stop`, `Resume`, `Warp`, speed, acceleration, turn rate, stopping distance, auto braking, drives a Rigidbody3D by velocity or the Transform
  - 9.2.2 Local avoidance - `Components/NavMeshAgent` - reciprocal velocity sampling against other agents and obstacles, avoidance priority and range
  - 9.2.3 Replanning and arrival - `Components/NavMeshAgent` - repath interval, stuck replans, `nav_arrived` message to Action Lists; hands the destination to an NPCController or ClickToMoveController when one is on the object
  - 9.2.4 Obstacle - `Components/NavMeshObstacle` - a moving box that is never baked and tracks its own velocity
- **Code:** `Components/NavMeshAgent`, `engine/src/AI/NavMeshAgent.cpp`, `Components/NavMeshObstacle`, `editor/NavigationWindow.*`
- **API:** `NavMeshAgent`, `NavMeshObstacle`, `SetDestination`, `Arrived`, `RemainingDistance`, `HasPath`, script `nav_move_to`, `nav_arrived`, `nav_stop`, visual-script Nav Move To, Nav Stop, Nav Arrived
- **Editor:** Add Component > Gameplay > Nav Mesh Agent and Nav Mesh Obstacle; Navigation window Send Selected Agent To Click; paths drawn as cyan lines in the Scene view
- **Docs:** `docs/navigation.md`
- **Tests:** `navmesh`, `script_nav`
- **Status:** Works - NavMeshAgent.hpp and the function layout of NavMeshAgent.cpp read (Replan, Avoid, Move, Face, Animate, Update); behaviour covered by `navmesh` per its agent cases
- **Open:** agents only avoid each other and obstacles locally; there is no crowd-wide planner; obstacles do not carve the mesh; area costs and links are 9.6
- **Related:** 9.1, 9.4, 9.5, 4.4
- **Words:** NavMeshAgent, agent, avoidance, steering, crowd, obstacle, destination, stopping distance, SetDestination, repath, stuck
- **Seen as:** enemies bunch up in a doorway; an agent jitters when it arrives; two agents block each other; an agent slides instead of walking; an agent gives up on a path
- **Ask me like:** "make the guard walk to the player and stop 2 m away"; "enemies keep stacking on each other"

### 9.3 Grid pathfinding (older)
- **What:** A* over a rectangular grid of walkable flags or directly over a Tilemap, with optional diagonal moves, for 2D games and tower-defense routing.
- **Parts:**
  - 9.3.1 Grid A* - `AI/Pathfinding` - `AStar` over a width by height walkable array, returns cells start to goal; diagonal moves never squeeze between two walls
  - 9.3.2 Tilemap paths - `AI/Pathfinding` - `OnTilemap` treats tile id 0 as walkable and any other tile as a wall
- **Code:** `AI/Pathfinding`
- **API:** `Pathfinding::AStar`, `Pathfinding::OnTilemap`, `Pathfinding::Cell`
- **Editor:** none
- **Docs:** `docs/navigation.md`
- **Tests:** `pathfinding`
- **Status:** Works - Pathfinding.hpp and Pathfinding.cpp read in full (A* with Manhattan or octile cost); covered by `pathfinding` (detours, no path, diagonals, tilemap)
- **Open:** 2D grid only; no weighted cells, no path smoothing, no moving obstacles; a start or goal on a wall returns no path; a grid over `Pathfinding::kMaxCells` (4096 x 4096) returns no path; not used by the 3D agents, which go through `Navigation` (9.1.4)
- **Related:** 9.1, 4.3, 2.6
- **Words:** A*, astar, grid pathfinding, tilemap path, walkable grid, tower defense, 2D pathfinding, Pathfinding
- **Seen as:** 2D enemies cut through walls or cannot find a route on a tilemap; a path zig-zags; no route is found when one exists
- **Ask me like:** "make 2D enemies path around tilemap walls"; "tower defense units follow a path to the base"

### 9.4 Behavior trees
- **What:** NPC brains you author as a tree (`.okaybt`) of composites, decorators, tasks and services with a typed blackboard; a Behavior Tree Runner plays it on an NPC and moves the body, and a node editor window edits it with a live view.
- **Parts:**
  - 9.4.1 Tree asset and node catalogue - `AI/BehaviorTree`, `engine/src/AI/BehaviorTree.cpp` - node types, parameter definitions, `BtValue` and `Blackboard`, text file load and save, validation issues
  - 9.4.2 Runner - `Components/BehaviorTreeRunner`, `engine/src/AI/BehaviorTreeRunner.cpp` - ticking, abort, services, composites, decorators and all tasks (move, patrol, wander, chase, flee, look, animate, attack, events, script call, sound, start dialogue)
  - 9.4.3 Body control - `Components/BehaviorTreeRunner` - moves through a NavMeshAgent, else an NPCController in external-brain mode, else navigation directly
  - 9.4.4 Behavior Tree window - `editor/BehaviorTreeWindow.*` - canvas, palette, node and blackboard tabs, issues tab, runner tab with live node status, copy and paste of subtrees, own undo
- **Code:** `AI/BehaviorTree`, `engine/src/AI/BehaviorTreeRunner.cpp`, `Components/BehaviorTreeRunner`, `editor/BehaviorTreeWindow.*`
- **API:** `BehaviorTree`, `BehaviorTreeRunner`, `Blackboard`, `BtValue`, `BtStatus`, `BtNodeType`, script `bt_set`, `bt_get`, `bt_run`, `bt_stop`, `on_bt_event`, C# `BehaviorTree.Set`, visual-script Set Blackboard, Get Blackboard, Run Tree
- **Editor:** View > Editors & Tools > Behavior Tree; Add Component > Gameplay > Behavior Tree Runner; double-click a `.okaybt` in the Project panel
- **Docs:** `docs/behavior_trees.md`
- **Tests:** `behavior_tree`
- **Status:** Works - BehaviorTreeRunner.hpp and the tick structure in BehaviorTreeRunner.cpp read; node behaviour covered by the 826-line `behavior_tree` test
- **Open:** the window has a live view but no breakpoints or stepping; the node catalogue has no node that runs another tree asset (copy and paste of subtrees only); Start Dialogue is the only task that links to story (10.2)
- **Related:** 9.2, 9.5, 10.2, 11.7, 7.3
- **Words:** AI, behavior tree, BT, blackboard, .okaybt, selector, sequence, decorator, service, patrol, chase, flee, Guard sample, BehaviorTreeRunner, AI brain, bt_set
- **Seen as:** an NPC stands still with a tree assigned; the tree restarts every frame; nodes do not light up in Play; the guard patrols but never notices the player; the Issues tab shows missing waypoints
- **Ask me like:** "make a guard that patrols and chases the player"; "add a node that plays a sound then waits"

### 9.5 NPCs and perception
- **What:** The NPC Controller is a ready-made brain for wildlife, enemies, guards and companions: base behaviours (idle, wander, patrol, guard, follow, flee, chase) with sight cone, line of sight, hearing, detection time, search and leash, plus combat numbers and health.
- **Parts:**
  - 9.5.1 Base behaviours - `Components/NPCController` - Idle, Wander, Patrol, Guard, Follow, Flee, Chase with waypoints, speeds, acceleration and turning
  - 9.5.2 Perception - `Components/NPCController` - sight range and field of view, optional physics line of sight from eye height, hearing range, detection time (stealth), lose-sight and search timers
  - 9.5.3 Combat and health - `Components/NPCController` - attack range, damage, interval, windup, flee at low health, provoke, `Damage`, death broadcast
  - 9.5.4 Pathfinding use and animation - `Components/NPCController` - routes through `Navigation` when Use Pathfinding is on; drives a sibling Character's idle, walk, run and head look; broadcasts `npc_alert`, `npc_lost`, `npc_died` and more to Action Lists
  - 9.5.5 Behavior-tree brain mode - `Components/NPCController` - `SetExternalBrain` makes it take orders (`CommandGoTo`, `AttackNow`) from a BehaviorTreeRunner
  - 9.5.6 Follow target (2D chase) - `Components/FollowTarget2D` - no-code chase: moves toward a named object each frame, stops within a stop distance, drives a sibling Rigidbody2D when present (enemies seeking the player, homing pickups, companions)
- **Code:** `Components/NPCController`, `Components/FollowTarget2D`, `editor/main.cpp` (NPC Controller inspector)
- **API:** `NPCController`, `NPCController::Behavior`, `CommandGoTo`, `SetExternalBrain`, `Damage`, script `npc_goto`, Action List messages `npc_alert`, `npc_lost`, `npc_died`
- **Editor:** Add Component > Gameplay > NPC Controller; Inspector groups for movement, perception, aggression, combat
- **Docs:** `docs/npcs_and_crafting.md`
- **Tests:** `npc_ai`, `npc_crafting`
- **Status:** Works - NPCController.hpp read in full (states, perception, external-brain hooks); behaviours covered by `npc_ai` (sight cone, search, patrol, flee, leash)
- **Open:** perception code lives inside this component, and per docs the behavior tree has its own Perception service (two copies of the idea, no shared Perception component); health is a built-in `health` field here, separate from HealthStat and Stats (11.4, 11.8); no group tactics beyond separation radius
- **Related:** 9.1, 9.2, 9.4, 11.4, 11.7, 5.8
- **Words:** AI, NPC, NPCController, enemy, wildlife, guard, patrol, wander, chase, flee, perception, sight cone, field of view, hearing, stealth, detection meter, leash, aggro
- **Seen as:** an enemy sees through walls; an enemy never notices the player or notices from behind; an NPC returns to its spawn after a chase; the NPC dies but nothing drops; enemies stack on each other
- **Ask me like:** "make a wolf that wanders and attacks when it sees me"; "stealth guard who needs two seconds to notice the player"

### 9.6 Nav areas and off-mesh links
- **What:** Each baked polygon has one of 32 areas (walkable, not walkable, water, road, custom) set per object with a Nav Mesh Modifier; the path search charges distance times the area's cost and each Nav Mesh Agent has an area mask. Nav Mesh Links (jump, drop, ladder) are extra edges the search can use and the agent crosses over time, with start and end callbacks.
- **Parts:**
  - 9.6.1 Area types - `Components/NavMeshModifier`, `AI/NavMeshBake`, `AI/NavMesh` - area per collider (own or ancestor's), tops win, not-walkable cuts a hole, cells merge per area, area saved in the `.okaynav` poly record
  - 9.6.2 Costs and masks - `AI/NavMesh` (`NavQueryFilter`), `Components/NavMeshSurface` (`areaCost`, `areaName`), `Components/NavMeshAgent` (`areaMask`) - A* cost is distance times area cost, forbidden areas never entered, no rebake to change costs
  - 9.6.3 Off-mesh links - `Components/NavMeshLink`, `AI/Navigation` (`FindPathForAgent`) - start and end points, bidirectional flag, cost, area, enabled state; edges in the A* graph, path legs report the link
  - 9.6.4 Link crossing - `engine/src/AI/NavMeshAgent.cpp` - walk to the start, arc or fall or climb to the end over time, `onLinkStart` / `onLinkEnd`, `nav_link_start` / `nav_link_end` messages
  - 9.6.5 Editor - `editor/NavigationWindow.*` - Areas table, area-coloured mesh overlay, link gizmos with arrows, Inspectors for modifier, link, agent mask
- **Code:** `Components/NavMeshModifier`, `Components/NavMeshLink`, `AI/NavMesh`, `AI/NavMeshBake`, `AI/Navigation`, `engine/src/AI/NavMeshAgent.cpp`, `editor/NavigationWindow.*`
- **API:** `NavMeshModifier`, `NavMeshLink`, `NavArea`, `NavQueryFilter`, `NavMeshAgent::areaMask`, `NavMeshAgent::onLinkStart`, `NavMeshAgent::onLinkEnd`, `NavMeshAgent::IsTraversingLink`, `Navigation::FindPathForAgent`, `NavMeshSurface::areaCost`
- **Editor:** Add Component > Gameplay > Nav Mesh Modifier and Nav Mesh Link; Nav Mesh Agent > Area Mask; Navigation window > Areas, Show Links; Scene view colours the mesh by area and draws links
- **Docs:** `docs/navigation.md`
- **Tests:** `navmesh_areas`, `navmesh`
- **Status:** Works - covered by `navmesh_areas` (cheaper road over shorter swamp, blocked area never entered, per-agent mask, link used / ignored when disabled, masked or too costly, one-way and two-way, crossing ends on the end point with events in order, scene round trip)
- **Open:** area costs and masks apply to the baked mesh only (not the grid fallback, NPC or click-to-move controllers); crossings are not checked against geometry; link ends must be within 1.5 m of the mesh; no obstacle carving
- **Related:** 9.1, 9.2
- **Words:** nav area, area cost, area mask, NavMeshModifier, NavMeshLink, off-mesh link, jump link, drop, ladder, swamp, mud, water, road, traversal cost, nav_link_start
- **Seen as:** NPCs wade through mud instead of walking round; NPCs will not jump a gap; an enemy cannot climb a ladder; a path ignores the road; an agent walks into lava
- **Ask me like:** "make enemies avoid the water"; "let the guard jump down off the ledge"
