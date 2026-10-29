# Behavior trees

NPC AI you author visually: a **behavior tree** decides what an NPC does every
tick, a **blackboard** holds what it knows (its target, where it last saw it, its
health), and the **Behavior Tree Runner** component plays the tree on an object
and moves the body through the scene's navigation. The `NPCController`'s fixed
states (wander, patrol, chase...) keep working for NPCs without a tree; with a
runner on the same object the tree gives the orders instead.

Trees are text assets (`Assets/Guard.okaybt`), edited in **View > Editors &
Tools > Behavior Tree**, and shared by every NPC that points at the file.

## Quick start

1. Open the window and press **New > Guard sample**, or select an NPC, add a
   **Behavior Tree Runner** (Add Component > Gameplay) and press its **Guard
   sample** button. The sample patrols between `Waypoint 1..3`, notices the
   `Player` in a sight cone, chases it, attacks within reach and goes back to
   the patrol 5 seconds after losing it.
2. Put three empty objects named `Waypoint 1`, `Waypoint 2`, `Waypoint 3` where
   the guard should walk. Give the NPC a **Nav Mesh Agent** (and bake a navmesh,
   see [navigation.md](navigation.md)) so it walks around walls.
3. **Save** the tree (Ctrl+S) and press Play. The nodes light up as they run,
   the Blackboard tab shows the live values, and the Runner tab lists the
   active path.

## How a tree runs

The runner ticks the **root** every frame (or at its **Tick Rate**). Each node
reports **Success**, **Failure** or **Running**; a running node is ticked again
next time until it finishes. When the root finishes, the tree starts over on
the next tick.

| Kind | What it does |
|---|---|
| **Composites** run their children left to right (the order on the canvas). |
| Sequence | Runs each child; stops with Failure at the first that fails, Success when all did. |
| Selector | Runs each child until one succeeds (Success); Failure when all failed. |
| Parallel | Runs every child at once. **Succeed When** all / one, **Fail When** one / all. |
| Random Selector | A selector that tries its children in a random order; a child's **Weight** makes it more likely to go first (0 = last). |
| **Decorators** wrap one child. |
| Inverter | Swaps Success and Failure. |
| Succeeder | Always Success once the child finished. |
| Repeat | Runs the child **Count** times (0 = forever), then Success. |
| Until Fail | Runs the child again and again until it fails, then Success. |
| Cooldown | After the child finished, fails for **Seconds** instead of running it again. |
| Time Limit | Stops the child and fails when it runs longer than **Seconds**. |
| Condition | Runs its child only while a test holds (see below); without a child it is the test itself. |
| Guard | A Condition that keeps watching: it stops its subtree when the test turns false (**Abort** self / both), and takes over from a later sibling of the same composite when the test turns true (lower / both). |
| **Tasks** are the leaves. |
| Wait | Runs for **Seconds** (random between it and **Max** when Max is larger), then Success. |
| Move To | Walks to a blackboard position or object (**Target**: a key or an object name) or the fixed **Position**. Success at **Stop Distance**, Failure when there is no way there (an island on the navmesh). |
| Patrol | Visits the named **Waypoints** in turn (loop or ping-pong), waiting **Wait** seconds at each. Keeps running; **Once** succeeds after a full round. The next waypoint is remembered when the patrol is interrupted. |
| Wander | Random points within **Radius** of the home point (on the navmesh when there is one), with a pause between hops. Keeps running; **Once** succeeds at the first point. |
| Chase | Follows the target as it moves, planning again on the way. Success within **Stop Distance**; Failure without a target, with no way there, or past **Give Up Past**. |
| Flee | Runs away from the target; Success once **Safe Distance** away. |
| Look At | Turns to face the target at **Turn Speed** (and looks with the head on a Character). Success once facing it. |
| Play Animation | Plays a Character **Clip** or built-in animation (`Wave`), sets an Animator **Trigger** or a **Param**. **Wait For Clip** keeps running until the clip ends. |
| Attack | Hits the target in **Range**: a Melee Attacker on the NPC swings, else the NPC Controller attacks (its damage, windup and interval), else the target's health takes **Damage**. Sends the `attack` event. Failure out of range. |
| Set Blackboard | Writes **Value** into **Key** (in the key's notation: `3.5`, `true`, `1 2 3`, a name). |
| Send Event | Tells the NPC's scripts (`on_bt_event(name)`) and visual scripts (On Custom Event) that something happened; **Target Object** sends it elsewhere, **Broadcast** to everyone and the Action Lists. |
| Run Script Function | Calls **Function** of the NPC's scripts with **Argument**. Failure when no script has it or it returns `false`. |
| Play Sound | Plays the NPC's Audio Source (a **File** when given); **Wait** runs until it ends. |
| Start Dialogue | Starts a conversation on the NPC's Dialogue Runner ([dialogue and quests](dialogue_quests.md)), playing **Graph** when given; **Wait** (on by default) runs until the conversation ends. A conversation the player already started is waited on rather than restarted. Failure without a Dialogue Runner or when the graph cannot start; when the task is aborted (a Guard above it turns false) the conversation it started ends. The tree loops like any other, so put a Condition or Cooldown in front of it when the NPC should talk once. |
| Custom | A task written in a script: **Function** is called with `"enter"` on the first tick, `"tick"` after, `"abort"` when stopped, and returns `"running"`, `"success"` or `"failure"`. |
| **Services** run alongside a node while it runs (attach them with right-click > Attach service). |
| Perception | Looks for **Target Object** in a sight cone (**Sight Range**, **Field Of View**, a physics ray from **Eye Height** when **Line Of Sight** is on) and a **Hearing Range**, every **Interval**. Writes the target (object), can-see (bool), last-seen position (vector) and seconds-since-seen (number) into the keys it names. |
| Health Watcher | Copies the NPC's health (NPC Controller, Health Stat or Survival Stats) into **Health Key**, **Fraction Key** and **Low Key** (below **Low Threshold**). |

**Tests** (Condition and Guard): a *blackboard* compare (`key == / != / < / <= / > / >= value`, or `set` / `unset` for an
object key), the *distance* to a target closer or farther than a value, a clear *line of sight* to a
target within a range, or the NPC's *health* below a fraction.

**Targets** are either a blackboard key (an object or vector key) or the name of
a scene object.

## The blackboard

The tree declares its keys with a type and a default: **number**, **bool**,
**string**, **vector** or **object** (an object reference, by name or the live
instance). A runner starts from these defaults, then applies its own
**Blackboard overrides** (Inspector), so the same tree serves many NPCs. Tasks,
services, the editor's live view and scripts all read and write the same
values; writing an existing key keeps its declared type.

## The runner

| Setting | Meaning |
|---|---|
| Tree | The `.okaybt` file, relative to the project folder. Runners sharing a file share one loaded copy. |
| Tick Rate | Ticks per second (0 = every frame). Movement keeps going between ticks. |
| Run On Start | Start the tree when the scene starts (scripts can `bt_run` / `bt_stop` it). |
| Debug Log | Logs every node that finishes and every run of the tree. |
| Home Point | Where Wander roams around (off: where the NPC started). |
| Blackboard overrides | Values that replace the tree's defaults on this NPC. |

How the body moves depends on what else is on the object:

- A **Nav Mesh Agent**: the runner sets its destination; the agent follows the
  navmesh with local avoidance and reports arrival.
- An **NPC Controller** (no agent): the controller's own perception and states
  stand down and it walks the runner's orders with its steering, pathfinding
  (when **Use Pathfinding** is on), speeds and Character animation. `Attack`
  uses its damage, windup and interval. Without a runner the controller behaves
  as before.
- Neither: the runner plans over the navigation itself (the baked navmesh, else
  the probe grid), moves a sibling Rigidbody3D by velocity or the Transform,
  turns toward the travel direction and drives a Character's idle / walk / run.

The runner ticks before the controllers (execution order -20), so an order
given this tick moves the body this frame.

## Scripts

OkayScript on the NPC (or anywhere, naming the NPC):

```
bt_set("alerted", true)            // bt_set("Guard", "alerted", true) for another object
var seen = bt_get("sinceSeen")     // a number, bool, string, vector or object
bt_run("Assets/Guard.okaybt")      // load and start a tree (adds a runner when missing); bt_run() starts over
bt_stop()

function on_bt_event(name) { }      // a Send Event task (or an Attack) fired
function my_task(phase) {           // a Custom task: "enter", "tick" or "abort"
  if (phase == "enter") { return "running"; }
  return "success";
}
```

C#-style scripts use the `BehaviorTree` statics: `BehaviorTree.Set("key", value)`,
`BehaviorTree.Get("key")` (any value; `GetFloat` / `GetBool` / `GetString` /
`GetVector` / `GetObject` for a typed one), `BehaviorTree.Run("Assets/Guard.okaybt")`,
`BehaviorTree.Stop()`, `BehaviorTree.isRunning`.

Visual scripts have **Set Blackboard**, **Get Blackboard** and **Run Tree**
(Engine group), and hear Send Event tasks with **On Custom Event**.

## The editor

- **Canvas**: the root at the top, children left to right in run order. Drag a
  node to move it; its new place among its siblings is its new run order. Drop
  a node on another to make it that node's child. Box-select on empty space,
  wheel to zoom, middle / right drag to pan, double-click to rename, F frames
  the tree, **Tidy** re-arranges after every edit. Right-click a node for Add
  / Attach service / Wrap in decorator / Set as root / Copy / Duplicate /
  Delete; right-click empty canvas for free nodes and Auto layout.
- **Palette** (left): click a node type to add it under the selected node
  (beside a selected task), or drag it onto the canvas.
- **Node** tab: the selected node's parameters, with pickers for blackboard
  keys, scene objects and waypoints; a child of a Random Selector shows its
  Weight.
- **Blackboard** tab: keys, types and defaults (rename follows through every
  node); live values in Play.
- **Issues** tab: a composite without children, a decorator without a child, a
  task with children, a key the tree does not declare, waypoints and objects
  missing from the scene, an empty function or event. Nodes with a problem
  carry a glyph on the canvas.
- **Runner** tab: pick the NPC the live view follows, Run / Stop / Restart it,
  see the active path and the blackboard.
- **New / Open / Save / Save As** manage the `.okaybt` file; **Use on selected
  object** gives the selection a runner that plays it. Double-click a
  `.okaybt` in the Project panel to open it. Undo (Ctrl+Z / Ctrl+Y) is the
  window's own; the scene's undo covers the runner component.

## The file

`.okaybt` is text and diffs cleanly:

```
okaybt 1
name Guard
key target object ""
key sinceSeen number 9999
root 0
node 0 Selector parent=-1 x=321 y=0 name=Guard
node 1 Perception parent=0 x=321 y=0 angle=120 hearing=4 range=12 targetName=Player
node 3 Guard parent=0 x=197 y=156 name="Seen recently" abort=both key=sinceSeen op=< value=5
...
```

`key` lines declare the blackboard (name, type, default); `node` lines carry
the id, type, parent, canvas position, an optional label and the parameters
that differ from the defaults. Children run in file order under their parent.
Services are children of the node they are attached to.

The runner is saved in the scene as its own `btrunner` record (the tree file,
tick rate, flags, home point and overrides); scenes saved before behavior trees
existed load unchanged. Build Game copies the tree files a scene references.
