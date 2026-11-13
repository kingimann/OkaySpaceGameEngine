# OkaySpaceGameEngine

A C++17 game engine with a desktop editor, a launcher, and a player that runs
your game on Windows, the web and mobile. Scenes are `GameObject`s made of
`Component`s, with scripts that hook into an `Awake` / `Start` / `Update`
lifecycle. Everything is written from scratch; the core needs only a C++17
compiler and threads.

## Get started

**Windows:** download the latest build from the GitHub Releases page, unzip it
and run **`OkaySpace.exe`**, the launcher. Create a project there and the editor
opens. Make a scene, press **Play**, then **File → Build Settings…** (Ctrl+B) to
export a standalone `<Game>.exe`.

New to the editor? Follow [docs/making_a_game.md](docs/making_a_game.md), from an
empty editor to a shipped game.

**From source:** see [Build from source](#build-from-source).

## What's in it

Each line links to its guide. The full, grouped list is in
[docs/README.md](docs/README.md).

- **Editor:** docked Hierarchy, Scene, Inspector, Console, Project and Script
  Editor panels; prefabs, materials, undo, autosave recovery, plugins.
  [editor](docs/editor.md), [plugins](docs/editor-plugins.md)
- **Scenes and rendering:** 2D sprites and 3D meshes, lighting with cascaded
  shadows, post-processing, a procedural sky, fog and weather, terrain, voxel
  terrain, water, world streaming. [rendering](docs/rendering.md),
  [lighting](docs/lighting.md), [modeling](docs/modeling.md)
- **Characters and animation:** one Player Controller for first and third
  person, imported FBX/GLB characters with built-in animation on any humanoid
  rig, an Animator with state machines and blend trees, inverse kinematics,
  ragdolls. [animation](docs/animation.md), [ik](docs/ik.md),
  [ragdolls](docs/ragdolls.md), [input](docs/input.md)
- **Scripting:** OkayScript, a small C#-style language built into every build
  (Lua and C# are optional), and a visual scripting graph with a no-code Action
  List. [scripting](docs/scripting.md),
  [function reference](docs/scripting_reference.md),
  [visual scripting](docs/visual_scripting.md)
- **Gameplay systems:** navigation meshes, behavior trees, dialogue and quests,
  cutscenes, splines, save games, inventory, survival stats, crafting, build
  mode, vehicles. [docs index](docs/README.md#gameplay-systems)
- **UI:** a canvas with buttons, sliders, inputs and more, HTML/CSS-style
  documents, in-world UI, localization. [game UI](docs/game_ui.md),
  [localization](docs/localization.md)
- **Audio:** a stereo mixer, WAV/OGG/MP3, music player. [audio](docs/audio.md)
- **Multiplayer and accounts:** host or join with UDP and optional libsodium
  encryption, a relay for players behind routers, dedicated servers, voice,
  accounts and cloud saves, anti-cheat.
  [multiplayer](docs/launcher-multiplayer.md), [relay](docs/relay.md),
  [accounts](docs/accounts.md), [anti-cheat](docs/anticheat.md)
- **Targets:** Windows, WebAssembly and mobile builds, Steam integration.
  [web and mobile](docs/web_mobile.md), [Steam](docs/steam_release.md)

There is also a console (ASCII) renderer, so the engine builds and runs headless
for servers and CI.

## Core concepts

| Concept              | OkaySpace type                                      |
|----------------------|-----------------------------------------------------|
| Game object          | `okay::GameObject`                                  |
| Component            | `okay::Component`                                   |
| Script base class    | `okay::Behaviour` (alias `okay::OkaySource`)        |
| Transform            | `okay::Transform` (full parent/child hierarchy)     |
| Scene                | `okay::Scene`                                       |
| Camera               | `okay::Camera` (orthographic and perspective)       |
| Renderers            | `okay::SpriteRenderer` / `okay::MeshRenderer`       |
| Player prefs         | `okay::Prefs`                                       |
| Prefab               | `okay::SceneSerializer` `.okayprefab`               |
| Math types           | `okay::Vec2/Vec3/Quat/Mat4`                         |
| Statics              | `Mathf`, `Time`, `Input`, `Color`                   |
| Components           | `AddComponent<T>()`, `GetComponent<T>()`            |
| Lifecycle            | `Awake/Start/Update/LateUpdate/OnDestroy`           |

## A taste of the API

```cpp
#include <Okay.hpp>
using namespace okay;

// A component script. (There's also a built-in okay::Spinner.)
class MySpin : public Behaviour {
public:
    float speed = 90.0f; // degrees/second
    void Update(float dt) override {
        transform->Rotate({0, 0, speed * dt});
    }
};

int main() {
    Application app({/*title*/ "My Game", /*w*/ 80, /*h*/ 30});
    Scene scene("Main");

    auto* cam = scene.CreateGameObject("Camera")->AddComponent<Camera>();
    cam->orthographicSize = 8.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->glyph = '@';
    player->AddComponent<MySpin>();

    app.Run(scene); // pumps Time, Input, Update, and Render every frame
}
```

Multiplayer is built in. From OkayScript, any player can host and others join:

```c
// Press H to host, J to join the machine at 127.0.0.1.
update(dt) {
  if (key_down("h")) net_host(45000)
  if (key_down("j")) net_join("127.0.0.1", 45000)
  net_send("pos", pos_x() + "," + pos_y());          // tell everyone where you are
  while (net_poll()) {
    if (net_msg_channel() == "pos") { /* net_msg_data(), net_msg_from() */ }
  }
}
```

No code needed: add a **Network Manager** component, set **Auto Start**, and
press Play. Players behind routers connect through the bundled relay. See
[launcher-multiplayer.md](docs/launcher-multiplayer.md) and
[relay.md](docs/relay.md).

## Build from source

Requirements: CMake 3.16 or newer and a C++17 compiler (CI builds with the
compiler Ubuntu's `g++` package provides, and with MinGW for Windows). The core build needs nothing else.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/sandbox                          # the demo (WASD to move, Q to quit)
cd build && ctest --output-on-failure        # the tests
```

**The editor** needs SDL2 and OpenGL
(Debian/Ubuntu: `sudo apt-get install libsdl2-dev libgl1-mesa-dev`):

```bash
cmake -S . -B build -DOKAY_BUILD_EDITOR=ON
cmake --build build -j
./build/bin/OkayEngine --dev     # the editor normally starts from the launcher
```

**The launcher** builds, updates and runs the game in one step:
`./launch.sh` (Linux/macOS) or `launch.bat` (Windows).

**Windows builds** are cross-compiled from Linux with MinGW-w64:

```bash
sudo apt-get install -y mingw-w64 cmake ninja-build
export SDL2_MINGW_PREFIX=/path/to/SDL2-<ver>/x86_64-w64-mingw32
./scripts/build-windows-dist.sh              # -> build-win-dist/
```

CI does the same on every `v*` tag and attaches the zip to the GitHub Release
([release.yml](.github/workflows/release.yml)). The result is laid out so the
launcher is the one thing to double-click and the rest sits in `Tools/`
(editor, player, relay); see [docs/packaging.md](docs/packaging.md).

**Optional backends** are off by default to keep the core dependency-free:

```bash
cmake -S . -B build \
  -DOKAY_WITH_STEAM=ON -DSTEAMWORKS_SDK_PATH=/path/to/sdk   # Steamworks
```

Without them the same APIs run on built-in or simulated backends. Packet
encryption (libsodium) is on by default; turn it off with `-DOKAY_USE_SODIUM=OFF`.

## Project layout

```
engine/       the engine: include/okay/<area>/ (Animation, Audio, Components,
              Core, Input, Inventory, Math, Net, Physics, Render, Scene,
              Scripting, UI, VisualScript, ...), src/, third_party/
editor/       the Dear ImGui + SDL2 editor
launcher/     the graphical launcher (sign in, library, updates)
player/       the runtime that plays an exported game
relay/        the multiplayer NAT relay
okayui/       the immediate-mode UI toolkit and its demo
shared/       code shared by the launcher, editor and player
sandbox/      the example "solar system" game
examples/     editor plugins, an account server, network and physics demos
tests/        unit tests (CTest)
docs/         guides (start at docs/README.md)
scripts/      build, Windows packaging and web build scripts
cmake/        toolchain and build helpers
supabase/     the anti-cheat session-ticket Edge Function
dist/         the changelog and prebuilt launcher and player
```

## Documentation

Everything is indexed in **[docs/README.md](docs/README.md)**, grouped by what
you want to do. Release history is in [ROADMAP.md](ROADMAP.md) and
[dist/CHANGELOG.txt](dist/CHANGELOG.txt).

## Extending it

To render somewhere new (your own GPU or window layer), implement
`okay::IRenderer` and pass it to `Application::SetRenderer(...)`. The scene,
components and scripts need no changes.

## License

Proprietary, all rights reserved. You may make and sell games with the engine without royalty; see `LICENSE`. Earlier versions published under MIT stay MIT for those copies.
