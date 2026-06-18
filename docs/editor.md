# OkaySpace Editor

A Unity-style desktop editor built with **Dear ImGui + SDL2 + OpenGL**. It opens
a real window with movable panels and edits live `okay::Scene`s.

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

The editor can be cross-compiled from Linux into a single `.exe` with SDL2
linked statically (no DLLs to ship alongside):

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
- **GameObject menu** — create Empty / Sprite / Camera objects.
- **File menu** — New / Open / Save scenes using the engine's `SceneSerializer`
  (`.okayscene` text files).
- **Toolbar** — **Play** runs the scene's real lifecycle (Awake/Start/Update +
  scripts, physics, etc.); **Stop** restores the exact pre-play edit state;
  **Step** advances a single frame.

## Notes

This editor renders the scene with ImGui's 2D draw lists, which is perfect for
the engine's 2D sprite model. A 3D scene view would swap that panel for an
OpenGL framebuffer using the same `IRenderer` abstraction the engine already
exposes — the rest of the editor (selection, inspector, serialization) is
unchanged.
