# Hand-written verification rows: scene and components

### V-SC-001 Create every kind of object from the GameObject menu
- **Map:** 1, 36
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > 3D Object** and add a **Cube**, a **Sphere** and a **Capsule**.
  2. Add a **Directional Light** from **GameObject > Light**, and **Create Camera**.
  3. Select each in the **Hierarchy** and look at the **Inspector**.
- **See:** each object appears in the Scene view and the Hierarchy; the Inspector shows its Transform and components.
- **Partial:** none known.

### V-SC-002 Move, rotate and scale in the Inspector
- **Map:** 1, 36
- **Where:** editor, Inspector panel
- **Steps:**
  1. Select a cube.
  2. Change its position, rotation and scale numbers in the **Inspector**.
  3. Press Ctrl+Z.
- **See:** the cube moves, turns and resizes as the numbers change; undo restores the old values.
- **Partial:** none known.

### V-SC-003 Prefab: make, place, variant
- **Map:** 21
- **Where:** editor, Project panel
- **Steps:**
  1. Make a prefab from an object (drag it from the Hierarchy into the **Project** panel).
  2. Right-click the `.okayprefab` file and choose **Place in Scene**.
  3. Right-click it again and choose **Create Variant**, then **Validate Prefab**.
- **See:** a second copy appears in the scene; a variant file is created; Validate reports no problems or lists them.
- **Partial:** map area 21 is marked planned in parts: expect gaps (nested prefab overrides, for example).
- **Link:** [prefabs.md](../../engine/prefabs.md)

### V-SC-004 Merge another scene into this one
- **Map:** 1, 20
- **Where:** editor, File menu and Project panel
- **Steps:**
  1. Save two scenes with different objects.
  2. Open one, then choose **File > Merge Scene into Current...** and pick the other (or right-click it in the **Project** panel and choose **Add to Open Scene**).
- **See:** the objects of the second scene are added to the first; Ctrl+Z removes them.
- **Partial:** none known.

### V-SC-005 Scene Manager panel
- **Map:** 1
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Panels > Scene Manager**.
  2. Add a second scene to the list and press Play.
- **See:** the list shows your scenes; scene loading in play follows the list order.
- **Partial:** steps unverified beyond opening the panel; read the panel's own tooltips.
- **Grounded:** no

### V-SC-006 Volumes (checkpoint, kill, force)
- **Map:** 44, 1
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > Volume** and pick a kind to draw (Checkpoint, Kill and so on), then drag a box in the Scene view.
  2. Make a player (**GameObject > Player > Player Controller (first / third person)**) and a floor, and put the volume where the player will walk.
  3. Press Play and walk into the volume.
- **See:** a Kill volume sends the body back to its last checkpoint; a Checkpoint volume sets the respawn point. Volumes are drawn as coloured translucent shapes in the Scene view (toggle with **View > Scene Overlays > Volumes**).
- **Partial:** see docs/engine/volumes.md for kinds that need other components (Ambience needs an Audio Source with a clip and Loop).
- **Link:** [volumes.md](../../engine/volumes.md)

### V-SC-007 Place nature, props and structures
- **Map:** 44, 2
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > 3D Object > Nature** and choose a **Tree**.
  2. Open **GameObject > 3D Object > Props** and choose a **Crate**.
- **See:** a premade mesh appears at the origin, selected.
- **Partial:** these are simple generated shapes.

### V-SC-008 Duplicate along a line
- **Map:** 44
- **Where:** editor, GameObject menu
- **Steps:**
  1. Select a cube.
  2. Choose **GameObject > Array Duplicate...**, set a count and an offset, and confirm.
  3. Also try **Snap Position to Grid**.
- **See:** a row of copies is made; Snap moves the selected object onto the grid.
- **Partial:** none known.

### V-SC-009 Asset Manager and dependencies
- **Map:** 20
- **Where:** editor, Project panel and View menu
- **Steps:**
  1. Right-click a file in the **Project** panel and choose **Show Dependencies** and **Find References**.
  2. Open **View > Panels > Asset Manager**.
- **See:** a list of what the file uses and who uses it; the Asset Manager lists loaded assets.
- **Partial:** map area 20 is mostly planned; only the basic parts exist.
