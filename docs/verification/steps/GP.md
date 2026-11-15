# Hand-written verification rows: gameplay, story and samples

### V-GP-001 Dialogue and quests window
- **Map:** 10
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Editors & Tools > Dialogue & Quests**.
  2. Make a short dialogue (a line and two choices) and save it.
- **See:** a graph editor for dialogue and a quest list.
- **Partial:** steps beyond opening the window are unverified; see docs/dialogue_quests.md.
- **Link:** [dialogue_quests.md](../../dialogue_quests.md)
- **Grounded:** no

### V-GP-002 Cutscene timeline example
- **Map:** 10
- **Where:** editor
- **Steps:**
  1. Open `examples/cutscene/intro.okayscene` (from the `examples` folder if shipped).
  2. Open **View > Editors & Tools > Timeline (cutscenes)**.
  3. Press Play on the scene.
- **See:** the camera sequence of the intro plays; the Timeline window shows its tracks.
- **Partial:** the release may not ship `examples` yet (PR #887).
- **Link:** [cutscenes.md](../../cutscenes.md)

### V-GP-003 Behavior tree example
- **Map:** 9, 11
- **Where:** editor
- **Steps:**
  1. Open `examples/behavior_trees/subtree_demo.okayscene`.
  2. Open **View > Editors & Tools > Behavior Tree** and press Play.
- **See:** the tree runs and the window shows which node is active.
- **Partial:** the release may not ship `examples` yet (PR #887).
- **Link:** [behavior_trees.md](../../behavior_trees.md)

### V-GP-004 Weapons window and arena
- **Map:** 11
- **Where:** editor
- **Steps:**
  1. Open `examples/weapons/arena.okayscene`.
  2. Open **View > Editors & Tools > Weapons**.
  3. Press Play and fire.
- **See:** the weapon from `weapons.okayweapon` fires; the Weapons window edits its numbers.
- **Partial:** the release may not ship `examples` yet (PR #887).
- **Link:** [../../try_it.md](../../try_it.md)

### V-GP-005 Items, loot and recipes
- **Map:** 11
- **Where:** editor, Project panel and View menu
- **Steps:**
  1. In the **Project** panel right-click and choose **Item**, then **Loot Table** and **Recipe Book**.
  2. Open **View > Editors & Tools > Items, Loot & Recipes**.
- **See:** item, loot and recipe files are created and listed in the window.
- **Partial:** none known.
- **Link:** [inventory.md](../../inventory.md)

### V-GP-006 Save Manager
- **Map:** 35
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Panels > Save Manager**.
- **See:** a panel listing save slots.
- **Partial:** map area 35 is mostly planned; steps beyond opening it are unverified.
- **Grounded:** no

### V-GP-007 Physics and node example scenes
- **Map:** 42
- **Where:** editor
- **Steps:**
  1. Open `examples/physics/step_and_parent.okayscene` and press Play.
  2. Open **File > Examples > Node Playground (flow and variables)** and pick a scene.
- **See:** the physics example runs; the playground scenes open.
- **Partial:** the Examples menu shows a scene only when the `examples` folder is found next to the editor.
