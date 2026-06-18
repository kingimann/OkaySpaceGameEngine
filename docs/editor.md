# OkaySpace Editor

A Unity-style desktop editor built with **Dear ImGui (docking) + SDL2**. It opens
a real window with **docked** panels (Hierarchy / Scene / Inspector / Console /
Project), a Play·Stop·Step toolbar, and a dark theme, and edits live
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
./build/bin/okay-editor
```

Run `./build/bin/okay-editor --selftest` to exercise the editor's logic without
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
# -> build-win-editor/bin/okay-editor.exe  (a prebuilt copy is in dist/OkaySpaceEngine.exe)
```

### Self-updating

The **Engine → Check for Updates** menu pulls the latest engine from GitHub
(`git fetch` + fast-forward `pull`) when the app is run from a source checkout,
then prompts you to rebuild. This is the same updater the standalone launcher
uses, built right into the engine app.

## What you can do

- **Hierarchy** — see the scene tree (parents/children); click to select.
- **Inspector** — rename, toggle active, edit Transform (position / Z rotation /
  scale), edit the Sprite Renderer (color, size) and Camera (ortho size), and
  add components or delete the object.
- **Scene viewport** — sprites are drawn as colored quads. Left-click to select,
  left-drag to move, right-drag to pan, mouse-wheel to zoom.
- **Sprite textures** — set a Sprite Renderer's *Texture* to a PNG/JPG/BMP path
  (loaded via `okay::Image`/stb_image). The built game draws the image, tinted by
  the sprite color; the editor viewport still shows the colored quad. Keep the
  image next to the built `.exe` (relative paths resolve there).
- **Text** — add a *Text* component for score counters, labels, and HUD using
  the built-in 8x8 font (no font file needed). Use *Screen Space* for a fixed
  HUD or world space to anchor it to the GameObject. Renders in the built game.
- **GameObject menu** — create Empty / Sprite / Camera objects.
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
