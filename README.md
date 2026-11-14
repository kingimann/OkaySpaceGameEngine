# OkaySpace Game Engine

A C++17 game engine with a desktop editor, a launcher and a player. You build
scenes out of game objects and components, add scripts, press Play, and export
a standalone game.

Our goal is to write every part ourselves. Today most of the engine is our own
code, and a few outside libraries are still in use while we replace them. The
list and the plan are in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) and
[docs/own_code_roadmap.md](docs/own_code_roadmap.md).

## Get started

**Windows:** download the latest build from the GitHub Releases page, unzip it
and run **`OkaySpace.exe`** (the launcher). Create a project, make a scene, press
**Play**, then **File → Build Settings…** (Ctrl+B) to export a standalone game.

**New to the editor?** Follow [docs/making_a_game.md](docs/making_a_game.md),
from an empty editor to a finished game.

**From source:** see [Build from source](#build-from-source).

## Platforms

| Platform | State |
|---|---|
| Windows | Supported. Launcher, editor and player ship in every release. |
| Linux | Builds from source. The dedicated server ships in each release. |
| macOS | In progress, not tested on a Mac yet. |
| Web and mobile | Web (WebAssembly) builds work. Mobile is limited. |

Details: [docs/platform_layer.md](docs/platform_layer.md) and
[docs/web_mobile.md](docs/web_mobile.md).

## What's in it

- **Editor:** dockable panels, prefabs, materials, undo, autosave recovery and
  plugins. [editor](docs/editor.md)
- **Rendering:** 2D sprites, 3D meshes, lighting and shadows, post-processing,
  sky, fog and weather, terrain and water. [rendering](docs/rendering.md),
  [lighting](docs/lighting.md)
- **Models:** glTF and OBJ are read by our own code. Our own STL, PLY, FBX,
  COLLADA, 3DS, BVH and Blender readers exist but are off by default until they
  are verified; outside libraries read those formats for now.
  [model import](docs/model_import.md)
- **Characters and animation:** one Player Controller for first and third
  person, an Animator with state machines, inverse kinematics and ragdolls.
  [animation](docs/animation.md), [ik](docs/ik.md)
- **Scripting:** OkayScript, a small C#-style language, and a visual scripting
  graph. [scripting](docs/scripting.md),
  [visual scripting](docs/visual_scripting.md)
- **Gameplay:** navigation, behavior trees, dialogue and quests, cutscenes,
  save games, inventory, crafting, build mode, hinge limits, and an editor for
  items, loot and recipes.
  [all systems](docs/README.md#gameplay-systems)
- **UI:** a canvas with the usual widgets, frames that turn and scale, a
  keyboard and gamepad focus ring, on-screen touch controls, HTML/CSS-style
  documents, in-world UI and localization. [game UI](docs/game_ui.md)
- **Audio:** a mixer, WAV, OGG and MP3, a music player, and optional 3D echo,
  pitch that follows speed, and muffling with walls and distance.
  [audio](docs/audio.md)
- **Multiplayer:** host or join over UDP with optional encryption, a relay for
  players behind routers, dedicated servers, voice, accounts, cloud saves and
  anti-cheat. [multiplayer](docs/launcher-multiplayer.md),
  [anti-cheat](docs/anticheat.md), [accounts](docs/accounts.md)

The complete list is in [docs/README.md](docs/README.md).

## A taste of the API

```cpp
#include <Okay.hpp>
using namespace okay;

class MySpin : public Behaviour {
public:
    float speed = 90.0f; // degrees per second
    void Update(float dt) override { transform->Rotate({0, 0, speed * dt}); }
};

int main() {
    Application app({"My Game", 80, 30});
    Scene scene("Main");

    scene.CreateGameObject("Camera")->AddComponent<Camera>()->orthographicSize = 8.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->glyph = '@';
    player->AddComponent<MySpin>();

    app.Run(scene);
}
```

The same thing in OkayScript, with multiplayer:

```c
update(dt) {
  if (key_down("h")) net_host(45000)                 // H hosts
  if (key_down("j")) net_join("127.0.0.1", 45000)    // J joins
}
```

## Build from source

You need CMake 3.16 or newer and a C++17 compiler. The core needs nothing else.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/sandbox                       # the demo (WASD to move, Q to quit)
cd build && ctest --output-on-failure     # the tests
```

**The editor** also needs SDL2 and OpenGL (Debian/Ubuntu:
`sudo apt-get install libsdl2-dev libgl1-mesa-dev`):

```bash
cmake -S . -B build -DOKAY_BUILD_EDITOR=ON
cmake --build build -j
./build/bin/OkayEngine --dev
```

**The launcher** builds, updates and runs the game in one step:
`./launch.sh` (Linux and macOS) or `launch.bat` (Windows).

**Windows builds** are cross-compiled from Linux with MinGW-w64:
`./scripts/build-windows-dist.sh`. How a release is built and published is in
[docs/releasing.md](docs/releasing.md) and [docs/packaging.md](docs/packaging.md).

Packet encryption (libsodium) is on by default. Turn it off with
`-DOKAY_USE_SODIUM=OFF`. Steam support is optional (`-DOKAY_WITH_STEAM=ON`).

## Project layout

```
engine/     the engine (include/okay/<area>/, src/, third_party/)
editor/     the editor
launcher/   the launcher (sign in, library, updates)
player/     the runtime that plays an exported game
relay/      the multiplayer relay
okayui/     our own UI toolkit (off by default)
shared/     code shared by the launcher, editor and player
sandbox/    the example game
examples/   plugins, an account server, network and physics demos
tests/      unit tests (CTest)
docs/       guides (start at docs/README.md)
scripts/    build, packaging and release scripts
supabase/   the anti-cheat session-ticket function
```

## More

- All documentation: [docs/README.md](docs/README.md)
- What is not finished: [docs/UNFINISHED.md](docs/UNFINISHED.md)
- Releases and plans: [ROADMAP.md](ROADMAP.md) and
  [dist/CHANGELOG.txt](dist/CHANGELOG.txt)
- To render somewhere new, implement `okay::IRenderer` and pass it to
  `Application::SetRenderer(...)`.

## License

Proprietary, all rights reserved. You may make and sell games with the engine
without royalty; see `LICENSE`. Earlier versions published under MIT stay MIT
for those copies.
