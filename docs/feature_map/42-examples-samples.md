# 42. Examples, samples & sandbox

What a new person can run to see the engine do something: the console sandbox, two console feature demos, a reference account
server, two editor plugins, the standalone player that runs every built game, and the starter assets. Files: `sandbox/`,
`examples/`, `player/` and `assets/`. The sandbox and the two C++ demos draw in the terminal; they are not the editor. The
player is the real runtime, with about 2,670 lines of its own. Nobody is touching these now.

## 42. Examples, samples & sandbox

### 42.1 Sandbox demo (solar system)
- **What:** A tiny console game that doubles as a feature tour: a sun, a planet with a moon parented to it, an inner planet and a ship you fly, drawn as text in the terminal.
- **Parts:**
  - 42.1.1 Scene and scripts - `sandbox/main.cpp` - `Orbit` and `Spin` behaviours and a `ShipController` driven by WASD; the moon is a child of the planet to show the Transform hierarchy
  - 42.1.2 Run loop and frame count - `sandbox/main.cpp` - 70 by 28 characters at 30 FPS; an explicit frame count wins, otherwise it runs until Q in a terminal or 150 frames when piped, so headless runs stop
  - 42.1.3 Build target - `sandbox/CMakeLists.txt` - target `sandbox` in `bin/`, built when `OKAY_BUILD_SANDBOX` is on (default)
- **Code:** `sandbox/main.cpp`, `sandbox/CMakeLists.txt`
- **API:** `./build/bin/sandbox [frames]`, `Application::Config`, `Behaviour`, `SpriteRenderer`, `Camera`
- **Editor:** none
- **Docs:** `README.md` (build and run)
- **Tests:** none (it is built by CI's first job but never run)
- **Status:** Works - source read; it is the first thing `scripts/build.sh` tells you to run
- **Open:** none; it exercises only 2D console rendering, not 3D or the editor
- **Related:** 41.3, 1.1, 1.3
- **Words:** sandbox, demo, solar system, console renderer, Orbit, ShipController, first run, hello world
- **Seen as:** I want to see the engine run without the editor; the demo prints text frames
- **Ask me like:** "run the sandbox for 300 frames", "make the sandbox ship fire"

### 42.2 Network demo
- **What:** Two or more terminals each fly a ship and see every other connected peer move in real time. It shows the smallest possible NetworkManager setup.
- **Parts:**
  - 42.2.1 Server and client modes - `examples/netdemo/main.cpp` - `netdemo server [port]` or `netdemo client <host> [port]`, default port 45000
  - 42.2.2 Local avatar and remote factory - `examples/netdemo/main.cpp` - `SetLocalAvatar` and `SetRemoteFactory` so each peer's ship is replicated and others show as cyan sprites
  - 42.2.3 Pilot - `examples/netdemo/main.cpp` - WASD when interactive, otherwise a lazy orbit so the demo animates on its own
- **Code:** `examples/netdemo/main.cpp`, `examples/CMakeLists.txt`
- **API:** `./build/bin/netdemo server`, `./build/bin/netdemo client 127.0.0.1`, `NetworkManager::StartServer`, `StartClient`
- **Editor:** none
- **Docs:** `docs/launcher-multiplayer.md`
- **Tests:** none (the networking tests of area 16 cover the same calls)
- **Status:** Works - source read; same API the `net` test drives
- **Open:** uses the avatar API rather than NetworkSync or authoritative movement, so it doesn't show prediction or lag compensation (16.2)
- **Related:** 16.1, 16.3, 41.1
- **Words:** netdemo, network demo, multiplayer demo, server, client, StartServer, StartClient, avatar, peers
- **Seen as:** I want a minimal two-window multiplayer test
- **Ask me like:** "run the network demo server and a client", "add chat to the network demo"

### 42.3 Physics demo
- **What:** Boxes fall under gravity, bounce and settle on a floor between two walls, in the terminal. It shows Rigidbody2D, box colliders and the physics world driving a scene.
- **Parts:**
  - 42.3.1 Scene - `examples/physicsdemo/main.cpp` - a static floor and two walls, six dynamic boxes with different bounciness and glyphs, gravity -12
  - 42.3.2 Respawner - `examples/physicsdemo/main.cpp` - a box that falls below the floor goes back to the top at a random x so the demo keeps going
- **Code:** `examples/physicsdemo/main.cpp`
- **API:** `./build/bin/physicsdemo [frames]` (default 200), `Rigidbody2D`, `BoxCollider2D`, `scene.physics().gravity`
- **Editor:** none
- **Docs:** -
- **Tests:** none (the physics tests are 6.x)
- **Status:** Works - source read
- **Open:** 2D only; no 3D physics example
- **Related:** 6.1, 41.1
- **Words:** physicsdemo, physics demo, Rigidbody2D, BoxCollider2D, bounce, gravity, console
- **Seen as:** I want a quick check that 2D physics works
- **Ask me like:** "run the physics demo", "add a ramp to the physics demo"

### 42.4 Account server example
- **What:** A tiny, dependency-free Python server that speaks the exact contract the engine's account client expects, so you can try the online account backend end to end locally. Not for production.
- **Parts:**
  - 42.4.1 Register and login - `examples/account-server/server.py` - `POST /register` and `POST /login` with JSON, return `{"token": ...}`; PBKDF2-HMAC-SHA256 with 200,000 rounds, accounts in `accounts.json`, tokens in memory
  - 42.4.2 Cloud saves - `examples/account-server/server.py` - `/cloud` list, `/cloud/<key>` read, write (POST) and delete
  - 42.4.3 Leaderboards and profile - `examples/account-server/server.py` - `POST /leaderboard/<board>` and `GET /leaderboard/<board>?count=N`, `GET /verify`, `GET /profile`
  - 42.4.4 Running it - `examples/account-server/server.py` - `python3 examples/account-server/server.py` on port 8080 (`OKAY_ACCOUNT_PORT`, `OKAY_ACCOUNT_DB`, `OKAY_LEADERBOARD_DB`), then `OKAY_ACCOUNT_SERVER=http://localhost:8080`
- **Code:** `examples/account-server/server.py`
- **API:** `OKAY_ACCOUNT_SERVER`, `OKAY_ACCOUNT_HOST`, `OKAY_ACCOUNT_PORT`, endpoints above
- **Editor:** none
- **Docs:** `docs/accounts.md` (Your own server, Try it locally), `docs/supabase-backend.md`
- **Tests:** none (the remote path of `account` needs a live server)
- **Status:** Basic - reference implementation read; the docs say it is not production-ready
- **Open:** possible issues: no request-size cap or rate limit, tokens are lost on restart; a compiled `__pycache__/server.cpython-311.pyc` is committed (19.6); no test runs the server against the engine's client
- **Related:** 17.2, 39.11, 19.6
- **Words:** account server, reference server, server.py, custom backend, /register, /login, /cloud, /leaderboard, OKAY_ACCOUNT_SERVER
- **Seen as:** I want to run my own account server; sign-in says it can't reach the server
- **Ask me like:** "start the example account server and point the launcher at it"

### 42.5 Editor plugin examples
- **What:** Two ready-to-copy editor plugins in OkayScript: a window that aligns, spaces and drops the selected objects, and a one-click command that strips trailing numbers like "Cube (3)" from names.
- **Parts:**
  - 42.5.1 Align Tools - `examples/editor-plugins/AlignTools.okay` - a window (`@window`, `on_gui`) with X, Y, Z align to the first selected object, distribute along X with a gap slider, drop to y 0; each action sets an undo point
  - 42.5.2 Clean Up Names - `examples/editor-plugins/CleanupNames.okay` - no window, an `on_menu` command in the Plugins menu
  - 42.5.3 Where to put them - `docs/editor-plugins.md` - copy into `<project>/EditorPlugins/` or `<editor>/plugins/`
- **Code:** `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay`
- **API:** `// @name`, `// @window`, `on_gui()`, `on_menu()`, `editor_selection_count`, `editor_set_pos`, `editor_undo_point`, `editor_rename`
- **Editor:** Plugins menu, Plugins > Plugin Manager...
- **Docs:** `docs/editor-plugins.md`
- **Tests:** `editor_plugins` (runs both shipped examples against a tiny scene, per its source)
- **Status:** Works - exercised by `editor_plugins`
- **Open:** only two examples; no sample of a plugin that reacts to selection
- **Related:** 15.8, 24
- **Words:** editor plugin, AlignTools, CleanupNames, .okay, Plugins menu, on_gui, on_menu, editor_* builtins
- **Seen as:** I want an example plugin to copy; objects are named "Cube (3)"
- **Ask me like:** "write a plugin that randomizes rotation of the selection"

### 42.6 Standalone player runtime
- **What:** `okay-player` (shipped as `OkaySpacePlayer.exe` or `<Game>.exe`): the runtime that plays a built game with only SDL2 (no ImGui). It loads `game.okayscene` next to it, draws 2D and 3D, plays audio, reads keyboard, mouse and gamepad, and also runs headless as the dedicated server.
- **Parts:**
  - 42.6.1 Startup and game files - `player/main.cpp` (`main` near L1110) - finds `Data/` or the flat layout, reads `game.okayconfig` (window, scene list, fullscreen toggle on Alt+Enter and F11), picks the startup scene (command line, config, `game.okayscene`), save folder, language
  - 42.6.2 Rendering - `player/main.cpp` (L1495-1640) - hidden window plus a branded boot splash, optional GPU renderer (Direct3D 11 on Windows, OpenGL elsewhere) with a software fallback, in-engine UI pass, Z-buffered 3D
  - 42.6.3 Multiplayer entry points - `player/main.cpp` (L1187, L1214) - `--join LINK`, `--connect HOST:PORT`, `--password`, `--room`, `--find-match`, `--server` (dedicated), `--login`, `--logout`, `--log FILE`
  - 42.6.4 In-game windows - `player/main.cpp` (L371, L545) - "Find a server" browser window (`net_browser_open()`), "Finding a match" banner
  - 42.6.5 Launcher link - `player/main.cpp` (L1674-1780) - once a second writes presence and join link to the launcher through GameIpc, accepts join requests, Shift+Tab asks the launcher for the invite window
  - 42.6.6 Anti-cheat start - `player/main.cpp` (L1405-1480, L1750) - manifest check, runtime checks and the one client report (39.4, 39.5)
  - 42.6.7 Item and inventory drawing - `player/main.cpp` (L158-1100) - icons, tooltips, grid inventory, crafting, chests drawn with SDL
  - 42.6.8 F1 test panel and OkayUI - `player/main.cpp`, `shared/OkayScriptUIBridge.hpp`, `shared/OkayTestPanel.hpp` - F1 toggles a panel with every OkayUI widget; script `ui_*` calls draw OkayUI widgets
  - 42.6.9 Build targets - `player/CMakeLists.txt` - `okay-player` (renamed `OkaySpacePlayer` and a GUI app on Windows), links OkayUI when present; a separate Emscripten branch builds `okay-player.html` (18.2)
- **Code:** `player/main.cpp`, `player/CMakeLists.txt`
- **API:** `okay-player [scene.okayscene] [--join LINK | --connect HOST:PORT] | --server [options]`, `game.okayconfig`, `OKAY_ANTICHEAT`, F1, Alt+Enter, F11, Shift+Tab
- **Editor:** File > Build Settings builds the folder that contains it (18.6); Play in the editor is a different code path
- **Docs:** `docs/packaging.md`, `docs/web_mobile.md`, `docs/dedicated-server.md`
- **Tests:** `dedicated_server` covers the `--server` argument parsing it uses; nothing else tests `main.cpp`; the CI "Build player runtime" step compiles it and the export check (18.6) runs it
- **Status:** Works - the shipping runtime; compiled by CI on every PR and run by the export check
- **Open:** 2,674 lines in one file, with UI drawing, matchmaking windows and server startup mixed in (19.4); behaviour differs slightly from Play in the editor (separate code paths); no automated test of its command line
- **Related:** 18.2, 18.6, 16.5, 17.15, 39.4, 15.3
- **Words:** player, okay-player, OkaySpacePlayer, runtime, standalone, game.okayconfig, --server, --join, boot splash, fullscreen toggle, F1 test panel, Find a server window
- **Seen as:** a built game behaves differently from Play in the editor; the game window flashes black at start; fullscreen toggle doesn't work; no console output on Windows (use `--log`)
- **Ask me like:** "what flags does the player take?", "why does my built game start in report mode?"

### 42.7 Starter assets
- **What:** The small asset pack shipped in the Windows distribution under `Assets/`: four generated seamless textures and a font, so a new project has something to drop onto meshes and text.
- **Parts:**
  - 42.7.1 Textures - `assets/textures` - `grass.png`, `dirt.png`, `stone.png`, `grid.png`, made by `tools/gen_textures.cpp` (41.4)
  - 42.7.2 Font - `assets/fonts` - DejaVu Sans with its licence file; set a Text or Button font to this path
- **Code:** `assets/textures`, `assets/fonts`, `tools/gen_textures.cpp`
- **API:** texture and font path properties on MeshRenderer, Text, Button
- **Editor:** Project panel (Assets/Textures, Assets/Fonts after install)
- **Docs:** `docs/packaging.md`
- **Tests:** none
- **Status:** Basic - four textures and one font
- **Open:** no sample project, sample scenes or sample audio ship in the repo; the starter templates are in code (1.6)
- **Related:** 41.4, 2.2, 1.6
- **Words:** starter assets, textures, grass, dirt, stone, grid, DejaVu Sans, sample assets
- **Seen as:** I need a quick texture; text looks like the pixel font
- **Ask me like:** "add a starter water texture to the pack"
