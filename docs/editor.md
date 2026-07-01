# OkaySpace Editor

A Unity-style desktop editor built with **Dear ImGui (docking) + SDL2**. It opens
a real window with **docked** panels (Hierarchy / Scene / **Game** / Inspector /
Console / Project), a Play·Stop·Step toolbar, and a dark theme, and edits live
`okay::Scene`s. The UI renders through SDL's 2D renderer (Direct3D on Windows,
Metal on macOS), so no OpenGL driver is required.

```
 File  GameObject
+----------------+--------------------------------+------------------+
|  Hierarchy     |             Scene              |   Inspector      |
|  v MainCamera  |            |                   |  Name [Player ]  |
|    Player  *   |        +-------+               |  [x] Active      |
|    Enemy       |        | green |   (drag me)   |  Transform       |
|                |        +-------+               |   Pos  4.0 2.0   |
|                |     ----+----------- x-axis    |   Rot Z  0       |
|                |        |                       |  Sprite Renderer |
+----------------+--------------------------------+   Color [####]   |
| Toolbar:  > Play   | EDIT | 60 FPS              |   Size  1.0 1.0  |
+-------------------------------------------------+------------------+
```

## Building & running

The editor is **off by default** (it needs SDL2 + OpenGL dev libs and, at
configure time, network access to fetch Dear ImGui):

```bash
# Debian/Ubuntu: sudo apt-get install libsdl2-dev libgl1-mesa-dev
cmake -S . -B build -DOKAY_BUILD_EDITOR=ON
cmake --build build -j
./build/bin/OkayEngine
```

Run `./build/bin/OkayEngine --selftest` to exercise the editor's logic without
a window (used in CI where there is no display).

### One self-contained Windows .exe

The editor cross-compiles from Linux into a single `.exe` with SDL2 linked
statically — no DLLs (not even OpenGL) to ship alongside:

```bash
# Needs MinGW-w64 and the SDL2 MinGW devel package (SDL2-devel-*-mingw).
cmake -S . -B build-win-editor \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
  -DOKAY_BUILD_EDITOR=ON -DOKAY_EDITOR_STATIC_SDL=ON \
  -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF \
  -DCMAKE_PREFIX_PATH=/path/to/SDL2-x.y.z/x86_64-w64-mingw32 \
  -DCMAKE_FIND_ROOT_PATH=/path/to/SDL2-x.y.z/x86_64-w64-mingw32 \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
cmake --build build-win-editor -j
# -> build-win-editor/bin/OkayEngine.exe  (a prebuilt copy is in dist/OkayEngine.exe)
```

### Self-updating

The **Engine → Check for Updates** menu pulls the latest engine from GitHub
(`git fetch` + fast-forward `pull`) when the app is run from a source checkout,
then prompts you to rebuild. This is the same updater the standalone launcher
uses, built right into the engine app.

## What you can do

Recent additions (v2.12–2.14):

- **Add Component, Unity-style** — a centered *Component* title + Search box;
  categories (Rendering, Animation, Physics 2D/3D, Lighting, Camera, Scripts,
  Audio, Gameplay, UI) drill into submenus while browsing, collapse to a flat
  filtered list while searching. The **Scripts** category lists every `.okay`
  file in your project (one click attaches it) plus **New Script…**.
- **Component headers** have an **enable/disable checkbox** and a right-click
  **Remove Component** menu; the object header has a **Tag** dropdown and a
  **Static** flag (saved with the scene).
- **Script Editor (VS Code-style)** — line-number gutter, live syntax
  highlighting, **Find** (Ctrl+F), inline compile errors, current-line
  highlight, **zoom** (Ctrl+scroll), **comment toggle** (Ctrl+/), **go-to-line**,
  **duplicate line** (Ctrl+D), **move line** (Alt+↑/↓), and a **Snippets** menu.
  Prefer your own editor? **Open in IDE** launches the script in VS Code / your
  OS default, and with **Live Sync** on (enabled automatically when you open it)
  every save out there reloads in-engine — so you can code entirely outside
  OkaySpace. If you also have unsaved edits in-app when the file changes, a banner
  lets you pick which version to keep. The editor can also **Float** into its own
  window or **Dock** back as a tab.
- **UI Editor tab** (View ▸ UI Editor) — a dedicated dockable **UI** window: the
  Scene view locked to UI-only, so you can edit the Canvas on a flat screen while
  keeping the 3D Scene view open in another tab. Dragging a UI child now **snaps
  stickily to its parent** (edges, center and thirds), Unity-style, in addition to
  the canvas and sibling smart-guides.
- **Flow Graph** (Window ▸ Flow Graph) — a node view of the selected object's
  **Actions** (visual scripting): the trigger wires to its conditions and a chain
  of instructions. **Add** a condition/instruction and **click any node to edit it
  in place** — pick the op and fill its arguments with the same editor the
  Inspector uses, so both stay in sync. Drag nodes to arrange; the ✕ deletes one.
- **Material inspector** — double-click a `.okaymat` to edit albedo / emissive /
  specular / texture / tiling, then Save or **Apply to Selection**; or drag a
  `.okaymat`/image onto an object to apply it.
- **Save Manager** (Window ▸ Save Manager) — browse and edit `.okaysave` runtime
  save files (typed key/value table); the editor side of `save()` / `load()`.
- **Drag & drop from Project** — drop prefabs/scenes/images/`.obj` into the Scene
  to place them; drop scripts/materials onto the Inspector or Hierarchy.
- **Combine scenes (seamless worlds)** — **File ▸ Merge Scene into Current…**
  (or **drag a `.okayscene` from Project onto the Hierarchy**) folds another
  scene's objects into the open one (the host keeps its own name,
  gravity and lighting). Merged objects are tagged with their source scene, and
  the **Hierarchy** groups them under a labelled **section header** so you can see
  where each combined scene starts and ends. At runtime, `load_scene_additive(
  "Town", x, y[, z])` merges a scene chunk at an offset without unloading the
  current one — build open worlds from tiles. (Engine: `SceneSerializer::Merge
  FromFile`, `Scene::RequestMerge`.)
- **Edit Collider** — on a Box Collider 3D, click **Edit Collider** to show six
  draggable face handles in the Scene view; pull each side to hand-fit the box to
  your model (Unity-style). Adjusts the collider's size + offset; undoable.
- **Scene gizmos** — **snapping** for Move/Rotate/Scale (grid + 15° detents) and
  a **Local/Global** toggle (X); a live **Camera Preview** inset for a selected
  perspective camera; colored **light gizmos** (range sphere / spot cone).
  **Unity-style billboard icons** with name labels mark every camera and light in
  the Scene (a camera glyph; a sun/bulb/spot glyph by light type), so lights and
  cameras are easy to see and pick even with no mesh.
- **Project Settings** (Edit ▸ Project Settings) and a larger **Build** dialog
  (Quit-on-Escape, Master Volume, Show-FPS).
- **Crash-safe autosave** (File ▸ Autosave) writes a `<scene>.autosave` recovery
  copy and offers to restore it after an unclean exit.
- **Keyframe Animator** — add it from *Animation*, then play/scrub and **Record**
  the transform into keyframe tracks.

- **Hierarchy** — see the scene tree (parents/children); click to select.
- **Inspector** — rename, toggle active, edit Transform (position / Z rotation /
  scale), edit the Sprite Renderer (color, size) and Camera (ortho size), and
  add components or delete the object.
- **Scene viewport** — sprites are drawn as colored quads. Left-click to select,
  right-drag to pan, mouse-wheel to zoom. The **Move / Rotate / Scale** tools
  (toolbar or **W / E / R**) set what a left-drag on the selection does; a gizmo
  at the selected object shows the active tool. Works in 2D and 3D.
- **UI editing** — screen-space widgets are clickable/draggable in the Scene
  view with 8 resize handles that track the cursor for any anchor. Toggle
  **Snap** for a pixel grid (set "UI px") plus smart **alignment guides** to the
  canvas edges/center and sibling widgets (magenta lines) while moving or
  resizing. **Arrow keys** nudge the selection (Shift = grid step). A selected
  widget's inspector adds a **3×3 anchor preset** grid (re-anchors without
  moving it), **Bring to Front / Send to Back**, a **Draw Order** override,
  **Center in Canvas**, and **Fill Width / Height / Canvas**. A live `W x H`
  readout and an anchor marker are drawn on the selection.
- **UI layering** — all UI draws in one pass ordered by: owning **Canvas Sort
  Order** (higher on top), then each widget's **Draw Order** (0 = its default
  type order; any non-zero value layers it freely against widgets of *any* other
  type, higher on top), then the **hierarchy** (a child draws above its parent;
  **Bring to Front / Send to Back** reorder a widget among its siblings). The
  editor preview uses the exact same order as the built game.
- **Sprite textures** — set a Sprite Renderer's *Texture* to a PNG/JPG/BMP path
  (loaded via `okay::Image`/stb_image). The built game draws the image, tinted by
  the sprite color; the editor viewport still shows the colored quad. Keep the
  image next to the built `.exe` (relative paths resolve there).
- **Mesh Renderer (3D)** — pick a built-in primitive (Cube, Pyramid, Quad,
  Wedge, Plane, Sphere, Cylinder, Cone, Tube, Torus, Capsule, Icosphere, Grid)
  from the *Primitive* dropdown. (More procedural shapes are available in C++:
  `Mesh::Extrude` a 2D outline into a prism, `Mesh::Lathe` a profile into a
  vase/column, `Mesh::Rotated` to orient a part before `Combine`, and
  `Mesh::FlipWinding` to fix inside-out imports.) The **Modeling** panel is
  organized into tabs — **Shape / Edit / Modifiers / Edit Mesh / Import** — so
  the toolset is easy to navigate. *Subdivide* splits every triangle into four
  (more detail) — or, when you've picked faces in **Edit Mesh**, only the selected
  face(s); *Smooth* subdivides then re-projects onto a sphere, *Weld* merges
  coincident vertices, and *Recenter* / *Fit 1u* normalize an imported model's
  position and scale; or type an *OBJ File*
  path and click *Load* to import a Wavefront `.obj` model (positions + faces,
  polygons fan-triangulated, `v/vt/vn` and negative indices handled). The
  inspector shows the live vertex/triangle count. Imported `.obj` files are
  remembered on the component and bundled next to the built `.exe` by Build Game,
  so the player reloads them at runtime. Meshes draw through the active
  perspective camera — as a wireframe when *Wireframe* is checked, otherwise as
  flat-shaded, depth-sorted, back-face-culled solids (a fixed key light, shared
  by the editor preview and the built game so they match). You can also build
  compound models in C++ with
  `Mesh::Transformed(scale, offset)` and `Mesh::Combine`/`Combined`, and export
  any mesh with `Mesh::SaveOBJ`.
- **UI anchors** — every UI widget has an *Anchor* (Top-Left … Center …
  Bottom-Right). The position becomes an offset from that screen point, so a
  Bottom-Right pause button or a Centered menu stays put at any window size or
  resolution. Hit-testing and the built game honor the anchor. Three **stretch
  anchors** (Stretch Horizontal / Vertical / Fill) make a widget fill the canvas
  on that axis — its Position/Size fields then read as **margins** (Unity's
  offsetMin/offsetMax: Left/Top/Right/Bottom), so a full-bleed backdrop or a
  top bar reflows with the window. The **Game view resolution** menu now scales a
  Constant-Pixel-Size HUD to the picked resolution, like Unity's CanvasScaler.
- **Panel & image styling** — *UI Panel* adds a **gradient direction** (vertical,
  horizontal, or either diagonal), an outer **outline** ring (focus keylines /
  neon accents, distinct from the inset border), and an inner **top highlight**
  sheen (the glossy "glass" look), on top of corner radius, border, shadow and
  17 shapes. *UI Image* adds **Flip X/Y**, **Preserve Aspect** (letterbox the
  texture without stretching), a **frame** border and a **drop shadow**. Both
  support **per-corner rounding** — toggle TL/TR/BL/BR to leave a corner square
  (tab headers, speech bubbles, one-sided cards).
- **UI widgets** — build menus and HUDs from screen-space components: *UI Panel*
  (background/overlay), *UI Image* (logos/icons/title art from a PNG/JPG, tinted;
  a colored rect when no texture; optional *Nine-slice* keeps a bordered frame
  undistorted while the edges/center stretch — resizable panels from one image), *UI Button* (calls the script's `on_click()`; has hover / pressed / disabled
  state colors, an *Interactable* toggle that greys it out — script:
  `set_interactable(bool)` — and a *Focusable* toggle for **keyboard/gamepad menu
  navigation**: arrows/WASD or the D-pad move focus, Enter/Space or gamepad A
  activates), *UI
  Progress Bar* / *UI Radial Progress* (`set_progress(0..1)`), *UI Slider* (drag
  to pick a value in a min/max range; calls `on_change()`, read with
  `slider_value()` / set with `set_slider(v)`), *UI Stepper* ([-] value [+] for a
  quantity) and *UI Rating* (click a row of stars), both firing `on_change()`,
  and *UI Toggle* (a labelled checkbox; calls `on_toggle()`, read with
  `toggle_on()` / set with `set_toggle(true)`). Box widgets share a **shape**
  picker (17 silhouettes: circle, pill, hexagon, pentagon, squircle, arrows, …)
  that drives both how they're drawn and where they're clickable. All are
  previewed live in the Scene viewport and render in the built game.
- **Text** — add a *Text* component for score counters, labels, and HUD using
  the built-in 8x8 font (no font file needed). Use *Screen Space* for a fixed
  HUD (with an *Anchor* so a centered title or bottom-right score adapts to the
  window size) or world space to anchor it to the GameObject. Toggle *Shadow*
  for a drop shadow (color + offset) that keeps text legible over busy
  backgrounds. Renders in the built game.
- **Game view** — a separate *Game* panel renders the scene through its **main
  camera** with no editor chrome (grid, gizmos, selection) — what the built game
  shows. 2D or 3D follows the camera's projection; press **Play** to make it live.
- **GameObject menu** — create Empty / Sprite / Camera / 3D primitives, and a
  **UI** submenu that adds each UI element (Button, Panel, Image, Text, Progress
  Bar, Slider, Toggle) as its own GameObject.
- **Add Component** is grouped into sections (Rendering, Physics, Camera,
  Scripting, Audio, Gameplay, UI); the search box filters across all of them.
- **3D meshes render solid** (flat-shaded, depth-sorted) by default in both the
  Scene/Game views and the built game; tick a Mesh Renderer's *Wireframe* for an
  edges-only view, or *Double-sided* to render both faces (planes, flags,
  foliage, tube interiors).
- **Projects** — **File → New Project** creates a `<Location>/<Name>` folder with
  an `Assets/` subfolder and saves the starting scene into it. The **Project**
  panel is an asset browser rooted at the project's `Assets/`: navigate folders,
  tick **All** to list the whole subtree (Unity-style), and click a `.okayscene`
  to open or a `.okayprefab` to instantiate.
- **File menu** — New / Open / Save scenes using the engine's `SceneSerializer`
  (`.okayscene` text files), and **Build Game…** (Ctrl+B) to export a
  standalone, double-clickable game.
- **Toolbar** — **Play** runs the scene's real lifecycle (Awake/Start/Update +
  scripts, physics, etc.); **Stop** restores the exact pre-play edit state;
  **Step** advances a single frame.

### Building a standalone game

**File → Build Game…** (Ctrl+B) exports the current scene as a self-contained
game you can ship. Give it a name and an output folder and it will:

1. Serialize the open scene to `game.okayscene` in that folder, and
2. Copy the **player runtime** (`OkaySpacePlayer.exe`, which ships next to the
   editor) into the folder, renamed to `<GameName>.exe`.

3. Copy every **asset the scene references** (sprite textures, audio WAVs,
   sprite-animator frames) into the folder, preserving relative paths.

The result is a folder containing `<GameName>.exe` + `game.okayscene` + assets.
Double-clicking the exe runs the scene through the same engine lifecycle the
editor's **Play** uses — sprites in 2D (orthographic camera), wireframe meshes
in 3D (perspective camera), keyboard input fed into `Input`, and audio mixed
through `AudioMixer`. Press **Esc** to quit.

The player (`player/`) is a tiny SDL2-only program (no editor/ImGui). Build it
on its own with `-DOKAY_BUILD_PLAYER=ON`; it is also built automatically with
the editor so Build Game always has a runtime to copy. Pass a scene path as the
first argument (`OkaySpacePlayer.exe my.okayscene`) or it loads `game.okayscene`
from beside the executable.

## Notes

This editor renders the scene with ImGui's 2D draw lists, which is perfect for
the engine's 2D sprite model. A 3D scene view would swap that panel for an
OpenGL framebuffer using the same `IRenderer` abstraction the engine already
exposes — the rest of the editor (selection, inspector, serialization) is
unchanged.
