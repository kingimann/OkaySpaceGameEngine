# Hand-written verification rows: in-game UI, scripting and visual scripting

### V-UI-001 UI Dropdown: drag scrolls a long list
- **Map:** 12
- **Where:** editor
- **Steps:**
  1. Choose **GameObject > UI > Controls > Dropdown**. In the Inspector set **Max Visible** to 4 and press **Add Option** until it has 10 or more options.
  2. Press Play. Click the dropdown to open it. Press on a row, move the mouse more than 10 pixels up or down while holding, and let go.
  3. Open it again and click a row without moving.
- **See:** the list scrolls row by row under the drag and nothing is picked on release; a plain click picks that row.
- **Partial:** no momentum (no flick); touch only on a touch screen.
- **Link:** [../../try_it.md](../../try_it.md)

### V-UI-002 Buttons and text on a canvas
- **Map:** 12
- **Where:** editor, GameObject menu
- **Steps:**
  1. Choose **GameObject > UI > Basic > Button** (a Canvas is made for you), then **GameObject > UI > Basic > Text**.
  2. Select the button and edit its text, size and colour in the **Inspector**.
  3. Press Play and click the button.
- **See:** the button shows and reacts (hover, press) in the Game panel.
- **Partial:** none known.

### V-UI-003 Prebuilt screens
- **Map:** 12, 32
- **Where:** editor, GameObject menu
- **Steps:**
  1. Open **GameObject > UI > Prebuilt Screens** and add **Main Menu**, then **Pause Menu**, **Settings** and **Save Slots**.
  2. Press Play.
- **See:** each screen is added as editable objects (the Console says what was hidden); the Settings screen has a Reduce motion switch.
- **Partial:** none known.

### V-UI-004 Inventory UI
- **Map:** 12, 11
- **Where:** editor, GameObject menu
- **Steps:**
  1. Choose **GameObject > UI > Inventory (Minecraft-style)**.
  2. Press Play and open the inventory.
- **See:** a slot grid you can drag items between (you need items from **View > Editors & Tools > Items, Loot & Recipes**).
- **Partial:** steps beyond the menu are unverified.
- **Grounded:** no

### V-UI-005 UI editing mode and theme
- **Map:** 12
- **Where:** editor, View menu
- **Steps:**
  1. With a Canvas in the scene open **View > Editors & Tools > UI Editing Mode**.
  2. Open **View > Editors & Tools > UI Theme** and change a colour.
- **See:** a flat canvas view for editing; theme colours change the widgets.
- **Partial:** none known.

### V-UI-006 Script Editor
- **Map:** 13
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Editors & Tools > Script Editor**.
  2. Choose **New Script...** and type a few lines; press Ctrl+S.
  3. Try Ctrl+F (**Find / Replace**), Ctrl+/ (**Toggle Comment**) and Ctrl+Alt+L (**Format Document**).
- **See:** syntax colours, find and replace work, and the file is saved.
- **Partial:** this is the Dear ImGui text editor; our own OkayUI editor widget is built but not used (see V-OC-010).
- **Link:** [../../scripting.md](../../scripting.md)

### V-UI-007 Run a script in Play
- **Map:** 13
- **Where:** editor
- **Steps:**
  1. Add a cube and **Add Component**, then pick a script (or create one in the **Project** panel with **Script**).
  2. Write a script that prints a line on start, and press Play.
  3. Read the **Console**.
- **See:** the printed line appears in the Console; a compile error shows in the Problems panel.
- **Partial:** steps unverified for the exact script syntax; see **Help > Scripting Reference**.
- **Grounded:** no

### V-UI-008 Visual script graph
- **Map:** 14
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Editors & Tools > Visual Script**.
  2. Press **Add node** and add a node, wire it, and press Play.
  3. Read the Console.
- **See:** nodes appear on the canvas and run when you play.
- **Partial:** see docs/visual_script_coverage.md for nodes with weak coverage.
- **Link:** [../../visual_scripting_nodes.md](../../visual_scripting_nodes.md)

### V-UI-009 Visual script node playground
- **Map:** 14, 42
- **Where:** editor, File > Examples
- **Steps:**
  1. Open **File > Examples > Visual Script Node Playground** and pick a demo.
  2. Press Play and follow the text on the screen.
- **See:** each demo explains and runs one group of nodes.
- **Partial:** the other **Node Playground (flow and variables)** list needs the `node_playground` examples to be found next to the editor.
- **Link:** [../../visual_scripting.md](../../visual_scripting.md)

### V-UI-010 Localization window
- **Map:** 31
- **Where:** editor, View menu
- **Steps:**
  1. Open **View > Editors & Tools > Localization**.
- **See:** a window for string tables and languages.
- **Partial:** map area 31 is mostly planned; steps beyond opening the window are unverified.
- **Grounded:** no

### V-UI-011 Test UI panel
- **Map:** 12, 40
- **Where:** editor, View menu (needs the developer flag)
- **Steps:**
  1. Open **View > Rendering & Performance** and look for **Test UI**.
- **See:** the entry shows only with the developer variable `OKAY_DEV` set; it opens the F1 test panel.
- **Partial:** developer-only.
