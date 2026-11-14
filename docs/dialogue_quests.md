# Dialogue and quests

Conversations are **dialogue graphs** (`.okaydialog` files in the project's
Assets folder), edited in the **Dialogue** window and played by a **Dialogue
Runner** on an NPC or object. What the story remembers lives in the scene's
**Story State** (global variables and flags). **Quests** come from `.okayquest`
files (or are typed into the **Quest Log**), with objectives that complete
from what happens in the game, rewards, a tracker overlay and a journal.

Everything the player did is saved with the game: the Story State, the Quest
Log's states and progress, and every component's fields are ordinary scene
records, so `save_game()` / `SaveGame.Save()` carries the story along.

## Dialogue graphs

**View > Editors & Tools > Dialogue & Quests** opens the window. The
**Dialogue** tab is a node canvas:

- Drag nodes to move them, drag empty space for a box selection, wheel to
  zoom, middle / right / Space-drag to pan, **F** frames everything, the
  minimap (bottom right) jumps around a big graph.
- **Double-click** empty space to add a Line, or use the **Add** buttons / the
  right-click menu for any node kind. Double-click a Line to edit its text in
  place.
- Drag a node's **right-hand port** onto another node to link them; drop it on
  empty space to add a linked node there. Right-click a port to select its link,
  then Delete removes it.
- Ctrl+C / Ctrl+V / Ctrl+D copy, paste and duplicate; Ctrl+Z / Ctrl+Y undo and
  redo; Ctrl+S saves. **New / Open / Save / Save As** and the file list are in
  the toolbar; double-clicking a `.okaydialog` in the Project panel opens it too.

The side panel edits the selected node (**Node**), the graph's name, start
node, speaker list and locals (**Graph**), lists the **Problems** (click one to
jump to its node), and plays the graph (**Test**).

### Nodes

| Node | What it does |
| --- | --- |
| **Line** | A speaker says the text. Optional portrait image, sound, and an auto-advance time (0 waits for the confirm key or a click). |
| **Choice** | 2 to 6 options. Each has a target and an optional condition (hidden while it fails). With no option showing, the conversation ends. |
| **Condition** | Tests a story variable (`gold >= 10`), a quest's state, an item count, or a flag, then takes the **true** or **false** output. |
| **Set** | Sets a story variable (number, text or true/false), sets or clears a flag, or sets a graph local. |
| **Event** | A named event for scripts: `on_dialogue_event(name)`, the **On Dialogue Event** node. |
| **Give Item / Take Item** | Through the player's inventory (the shared inventory layer: an Inventory on the player or its root, else a Grid Inventory). |
| **Start Quest / Complete Objective** | In the scene's Quest Log. |
| **Jump** | Continues at any node (loops, shared endings). |
| **End** | The conversation ends. |

Variables: **graph locals** last one conversation and start from the values on
the Graph tab; **story variables** and **flags** are global and persist (Story
State). A Condition on a name looks at the locals first.

The **Test** panel plays the graph without the scene: pick choices, see the
transcript, and set the variables, flags, items and quest states it tests
against. Nothing it does touches the scene.

### Validation

The window flags, on the node and in the Problems list: a link to a missing
node, a node unreachable from the start, an empty line or option, a choice with
fewer than 2 or more than 6 options, a Set or Event with no name, an unknown
item (when the Item Database has items) and an unknown quest (when the project
has `.okayquest` files or Quest Logs with quests).

### The file

Plain text, one node per line (`okaydialog 1`):

```
okaydialog 1
name "Smith"
start 0
speaker "Smith"
speaker "Player"
local "visits" "0"
node id=0 kind=line x=80 y=120 speaker="Smith" text="Welcome, traveler." next=1
node id=1 kind=cond x=340 y=120 ck=var cn="gold" co=">=" cv="10" cc=1 t=2 f=3
node id=4 kind=choice x=860 y=120 n=2 c0t="Buy a sword (10 gold)" c0to=5 c0ck=var c0cn="gold" c0co=">=" c0cv="10" c0cc=1 c1t="Just looking." c1to=-1
node id=5 kind=take x=1120 y=60 item="gold" count=10 next=6
node id=6 kind=give x=1360 y=60 item="sword" count=1 next=7
node id=7 kind=event x=1600 y=60 name="sword_sold" next=8
node id=8 kind=end x=1840 y=120
```

Keys are order-free and unknown keys are skipped, so files from a newer
version still load.

## Dialogue Runner

Add **Dialogue Runner** (Add Component > Gameplay) to the NPC or object and
set its **Graph**. The **Edit** button opens the graph in the Dialogue window.

| Setting | What it does |
| --- | --- |
| Starts | **Interact key in range** (E within 3 m by default, like the pickups: the prompt shows on the Hold Prompt line) or **Scripts only**. |
| Pause player | Controllers stop looking and moving while the conversation runs; the cursor is freed for the choice buttons. |
| Typewriter | Characters per second (0 = whole lines at once). The first confirm shows the rest of a line, the next moves on. |
| Auto-advance | Lines without their own time move on after this many seconds. |
| Built-in dialogue box | The box drawn with OkayUI in the launcher look: the speaker as the title, the portrait slot (the image, else an avatar), the typed text, numbered choice buttons and a continue hint. Turn it off to draw your own from `CurrentLine()` and the events. |
| Escape cancels | Esc ends the conversation. |

While talking: the interact key, Enter, Space or a click continues; **1**–**6**
or a click picks a choice. While it runs, E is not a pickup.

From a script on the NPC or anywhere:

```
start_dialogue("Smith")        // an object with a Dialogue Runner, else a graph name; "" = this object's
if dialogue_active(): ...
stop_dialogue()
```

```csharp
Dialogue.Start("Smith");  Dialogue.IsActive();  Dialogue.Stop();
```

Events reach scripts on the NPC and on the player: `on_dialogue_event(name)`
and `on_dialogue_end()` (`OnDialogueEvent` / `OnDialogueEnd` in C#-style
scripts, **On Dialogue Event** / **On Dialogue End** in visual scripts, with
Event Name set).

C++: `StartDialogue()`, `Advance()`, `Choose(i)`, `Stop()`, `IsActive()`,
`CurrentLine()` (speaker, text, the part shown so far, portrait, choices).

## Story State

One per scene, created on demand on an object called **Story**. The inspector
lists the variables (editable in the editor, read-only while playing) and the
flags.

```
set_story("gold", 10)        get_story("gold")        has_story("gold")
set_story_flag("met_smith")  story_flag("met_smith")
```

```csharp
Story.Set("gold", 10); Story.Get("gold"); Story.SetText("hero", "Ann"); Story.GetText("hero");
Story.Has("gold"); Story.SetFlag("met_smith", true); Story.HasFlag("met_smith");
```

Visual scripts: **Set Story Var** and **Get Story Var**.

## Quests

Quests are defined in a `.okayquest` file (the **Quests** tab of the Dialogue
window: New / Save, a list, an inspector, problems) or typed into the Quest
Log's inspector. A quest has an id, a title and description, **prerequisites**
(quests that must be completed first), **objectives**, **rewards** and a
**repeatable** flag.

| Objective | Completes when |
| --- | --- |
| **Talk to** | A conversation on the named object (or with the named graph) ends, or a dialogue's **Complete Objective** node runs. |
| **Collect** | The player's inventory holds N of the item (counted every frame while the quest is active). |
| **Kill** | N objects with the tag (or name) die (their Health Stat). |
| **Reach** | The player enters a **Quest Trigger** whose zone has that name. |
| **Custom** | A script says so: `complete_objective(id, index)`. |

Optional objectives don't hold the quest up. Rewards are granted once per
completion: items into the player's inventory, story variables, and an event
(`on_quest_event(name)`).

**Quest Log** (one per scene; Add Component > Gameplay, or made on demand) keeps
every quest's state (inactive / active / completed / failed) and progress. It
draws, with OkayUI in the launcher look: a **tracker** (the tracked quest and
its objectives, top right), **notifications** on start, progress and completion
(the scene's UI Toast when it has one, else the OkayUI toast) and the
**journal** (**J**, configurable) with Active / Completed / All tabs. Turn the
overlays off to draw your own from the records and events.

**Quest Trigger** (Add Component > Gameplay) is a zone: a trigger collider and
a zone name (the object's name unless set). Entering it completes "reach"
objectives that name it, and optionally one objective of a named quest.

```
start_quest("gather")            complete_objective("gather", 0)      add_quest_progress("gather", 0, 1)
complete_quest("gather")         fail_quest("gather")
quest_state("gather")            // "inactive" / "active" / "completed" / "failed"
quest_active("gather")           quest_completed("gather")            toggle_journal()
```

```csharp
Quest.Start("gather"); Quest.CompleteObjective("gather", 0); Quest.AddProgress("gather", 0, 1);
Quest.Complete("gather"); Quest.Fail("gather"); Quest.State("gather"); Quest.IsActive("gather");
```

Events on the Quest Log's object and on the player: `on_quest_started(id)`,
`on_quest_completed(id)`, `on_quest_failed(id)`, `on_objective_completed(id, index)`,
`on_quest_event(name)`. Visual scripts: **Start Quest**, **Complete
Objective**, **Quest State** and the **On Quest Started / Completed / Failed**,
**On Objective Completed** and **On Quest Event** entries.

The file (`okayquest 1`): a `quest` line followed by its `objective` and
`reward` lines.

```
okayquest 1
quest id="gather" title="Ore run" desc="The smith needs ore." repeatable=0
objective kind=collect desc="Collect 3 ore" target="ore" count=3 optional=0
reward item="coin" count=5
reward var="reputation" value="10"
reward event="gather_done"
quest id="hunt" title="Wolf hunt" repeatable=0 pre="gather"
objective kind=kill desc="Kill 2 wolves" target="Wolf" count=2 optional=0
```

## Multiplayer

Dialogues are local to each client: the box, the pause and the events happen
on the machine of the player who talks. Quests are per client in this version:
each client's Quest Log tracks its own player's objectives and rewards. A
shared, host-validated quest state is not part of this version.

## Save games

A save holds the Story State, the Quest Log's records (states, progress, the
tracked quest) and the Dialogue Runner's settings like any other component, so
loading a save puts the story back exactly. A conversation in progress is not
saved; it ends with the load.
