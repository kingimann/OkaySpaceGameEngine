# 41. Build, tools & infrastructure

The plumbing around the engine: CMake (root, engine and per-program files, the `cmake/` modules and the MinGW toolchain), the
scripts and developer tools, the deploy kits for the relay, the Supabase Edge function and SQL schema, the Steam upload kit,
the two GitHub workflows, versioning and the pinned dependencies. What a release does for a player is area 18; this page is
where the files live and how they are wired. Nobody is touching it now apart from the 2.852.0 release PR (#588, draft); 2.851.0 is live.

## 41. Build, tools & infrastructure

### 41.1 CMake project layout and options
- **What:** One CMake project builds everything: the engine library, the unit tests, the launcher, the player, the editor, the relay, the sandbox and examples, and the developer tools. Options turn parts on or off; defaults suit a plain developer build.
- **Parts:**
  - 41.1.1 Root options - `CMakeLists.txt` - `OKAY_BUILD_SANDBOX` (on), `OKAY_BUILD_TESTS` (on), `OKAY_BUILD_LAUNCHER` (on), `OKAY_BUILD_EDITOR` (off), `OKAY_BUILD_PLAYER` (off), `OKAY_USE_SODIUM` (on), `OKAY_USE_ASSIMP` (off), `OKAY_BUILD_TOOLS` (off), `OKAY_SANITIZE`; C++17; `-Wall -Wextra -Wpedantic`
  - 41.1.2 Engine library - `engine/CMakeLists.txt` - `okay_engine` (alias `okay::engine`) built from an explicit source list (nothing is globbed, so a new `.cpp` must be added; all `engine/src` files are listed today); optional backends: libsodium, Lua, C# (Mono), Steamworks, Photon, Assimp; four network sources include `shared/` headers (ed25519, clock heartbeat, request builder, HMAC)
  - 41.1.3 Per-program files - `launcher/CMakeLists.txt`, `player/CMakeLists.txt`, `relay/CMakeLists.txt`, `sandbox/CMakeLists.txt`, `examples/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt` - each makes its target and sets `bin/` as the output folder; `editor/CMakeLists.txt` and `okayui/CMakeLists.txt` are the editor's and OkayUI's
  - 41.1.4 Baked defaults - `CMakeLists.txt` - default account server URL and key (`OKAY_DEFAULT_ACCOUNT_URL`, `OKAY_DEFAULT_ACCOUNT_KEY`), `OKAY_DEFAULT_VOICE_RELAY`, `OKAY_STEAM_APP_ID`, `OKAY_AC_TRUSTED_KEYS`, `OKAY_AC_PLAYER_TRUSTED_KEYS`, `OKAY_AC_HW_SALT` as compile definitions
  - 41.1.5 SDL2.dll copy - `CMakeLists.txt` - on Windows builds a target that copies `SDL2.dll` beside the binaries unless static SDL is on
  - 41.1.6 Relay target - `relay/CMakeLists.txt` - `okayspace-relay`, always built except for Emscripten (no raw UDP in browsers)
  - 41.1.7 Umbrella header and generated API page - `engine/include/Okay.hpp`, `docs/engine_api.md` - `<Okay.hpp>` pulls in most engine headers and must not expose `okay::Clock` (a program with its own `Clock` and `using namespace okay` broke `test_save_scene` once), so `Core/Clock.hpp`, `GameLoop`, `Countdown`, `TimeSnapshot` and `LoopConsole` are included directly and `ClockCursor::Take` takes numbers instead of a `Clock`; `docs/engine_api.md` is generated from the engine binding table by `test_vs_types` (`OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` rewrites it and the test fails when it is stale)
- **Code:** `CMakeLists.txt`, `engine/CMakeLists.txt`, `launcher/CMakeLists.txt`, `player/CMakeLists.txt`, `relay/CMakeLists.txt`, `sandbox/CMakeLists.txt`, `examples/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`
- **API:** `cmake -S . -B build`, `-DOKAY_BUILD_EDITOR=ON`, `-DOKAY_BUILD_PLAYER=ON`, `-DOKAY_USE_SODIUM=OFF`, `-DOKAY_WITH_STEAM=ON -DSTEAMWORKS_SDK_PATH=`, `-DOKAY_WITH_PHOTON=ON`, `-DOKAY_WITH_LUA=ON`, `-DOKAY_WITH_CSHARP=ON`, `-DOKAY_SODIUM_PREFIX=`
- **Editor:** none
- **Docs:** `README.md` (build section), `docs/packaging.md`
- **Tests:** none (the CI jobs configure with these options, 18.3)
- **Status:** Works - read the CMake files; CI configures five different option sets
- **Open:** the default Supabase URL and publishable key are written in `CMakeLists.txt` (old map: remove; release CI deliberately doesn't override them); two similarly named switches, `OKAY_STATIC_SDL` (toolchain and root) and `OKAY_EDITOR_STATIC_SDL` (editor, launcher, player); the engine's explicit source list must be edited for every new file
- **Related:** 18.1, 19.7, 41.2, 41.10
- **Words:** CMake, CMakeLists.txt, options, OKAY_BUILD_*, configure, build type, engine library, okay::engine, source list, definitions, build tree
- **Seen as:** "undefined reference" after adding a .cpp (not in the list); a target silently missing (SDL2 not found warning); an option has no effect
- **Ask me like:** "which CMake flag builds the editor?", "add my new source file to the engine build"

### 41.2 CMake modules, Windows resources and toolchain
- **What:** The small CMake helpers: Windows exe name, description and version (what Task Manager shows), the MinGW cross toolchain, and the anti-cheat key-list check.
- **Parts:**
  - 41.2.1 Windows exe metadata - `cmake/OkayWinVersion.cmake`, `cmake/okay_version.rc.in` - `okay_win_version(target description filename)` generates a `VERSIONINFO` resource from `PROJECT_VERSION` and embeds the manifest; no-op off Windows
  - 41.2.2 Application manifest - `cmake/okayspace.exe.manifest` - runs as the invoking user (never asks for admin) and declares Windows 7 to 11 support
  - 41.2.3 MinGW toolchain - `cmake/mingw-w64-x86_64.toolchain.cmake` - `x86_64-w64-mingw32` compilers, find-root settings, a copy of `libwinpthread.a` without its own VERSIONINFO member (so Task Manager doesn't show "POSIX WinThreads"), optional static SDL
  - 41.2.4 Trusted-key check - `cmake/OkayAntiCheatKeys.cmake` - validates `OKAY_AC_TRUSTED_KEYS` (39.7)
- **Code:** `cmake/OkayWinVersion.cmake`, `cmake/okay_version.rc.in`, `cmake/okayspace.exe.manifest`, `cmake/mingw-w64-x86_64.toolchain.cmake`, `cmake/OkayAntiCheatKeys.cmake`
- **API:** `okay_win_version()`, `okay_ac_check_trusted_keys()`, `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake`
- **Editor:** none
- **Docs:** `docs/packaging.md`, `docs/anticheat.md`
- **Tests:** `anticheat_keys_ok`, `anticheat_keys_bad_*` (the key check only)
- **Status:** Works - used by every Windows build (2.847.0 added the exe names and versions, #545)
- **Open:** no test checks the generated resource; the toolchain's pthread workaround falls back silently
- **Related:** 18.1, 39.7
- **Words:** toolchain, MinGW, cross-compile, VERSIONINFO, resource, rc, manifest, Task Manager, winpthread, windres
- **Seen as:** Task Manager shows the wrong program name; the exe asks for admin; MinGW configure can't find windres
- **Ask me like:** "why does Task Manager say POSIX WinThreads?"

### 41.3 Local build and launch scripts
- **What:** Convenience scripts for a first build and run on Linux, macOS and Windows, plus the launch shortcuts at the repo root.
- **Parts:**
  - 41.3.1 Native build - `scripts/build.sh`, `scripts/build.bat` - configure Release, build, and print how to run the sandbox demo and the tests
  - 41.3.2 Cross-build a Windows exe - `scripts/build-windows-exe.sh` - MinGW build of the sandbox into `dist/OkaySpace.exe` (see 18.1.5)
  - 41.3.3 Launch shortcuts - `launch.sh`, `launch.bat` - build `okayspace-launcher` once, then run it with any arguments
  - 41.3.4 Other scripts - `scripts/build-windows-dist.sh`, `scripts/build-web.sh`, `scripts/publish-supabase.sh`, `scripts/publish-official-game.sh`, `scripts/okayspace-installer.nsi` - mapped in 18.1, 18.2, 18.5, 17.14
- **Code:** `scripts/build.sh`, `scripts/build.bat`, `scripts/build-windows-exe.sh`, `launch.sh`, `launch.bat`, `scripts/build-windows-dist.sh`, `scripts/build-web.sh`, `scripts/publish-supabase.sh`, `scripts/publish-official-game.sh`, `scripts/okayspace-installer.nsi`
- **API:** `scripts/build.sh`, `./launch.sh`, `./build/bin/sandbox`, `cd build && ctest --output-on-failure`
- **Editor:** none
- **Docs:** `README.md`, `docs/packaging.md`
- **Tests:** `publish_official_cleanup`, `publish_supabase_checks` (the two publish scripts only)
- **Status:** Works - short scripts, read in full; only the publish scripts have tests
- **Open:** `launch.sh` and `launch.bat` say "update from GitHub, build, and run" but only build and run (stale comment); `build.sh` places `build/` in the repo root
- **Related:** 18.1, 18.5, 41.1
- **Words:** build.sh, build.bat, launch.sh, quick start, first build, scripts folder
- **Seen as:** the first build fails for a missing SDL2; "launcher not found"
- **Ask me like:** "what is the fastest way to build and run the sandbox?"

### 41.4 Developer tools
- **What:** Two small tools that generate files the repository ships.
- **Parts:**
  - 41.4.1 Texture generator - `tools/gen_textures.cpp`, `tools/CMakeLists.txt` - deterministic 512 by 512 seamless grass, dirt, stone and grid PNGs for `assets/textures/` (target `okay_gen_textures`, run `./gen_textures <dir>`); built only with `-DOKAY_BUILD_TOOLS=ON`
  - 41.4.2 Test rig generator - `tools/make_test_rigs.py` - pure-Python, no network: writes the skinned glTF rigs in `tests/assets/rigs/` (25-joint humanoid under many naming and axis conventions, plus a quadruped)
  - 41.4.3 Starter assets - `assets/textures`, `assets/fonts` - the generated textures and a DejaVu Sans font that the Windows dist copies into `Assets/` (42.7)
- **Code:** `tools/gen_textures.cpp`, `tools/CMakeLists.txt`, `tools/make_test_rigs.py`
- **API:** `gen_textures <output-dir>`, `python3 tools/make_test_rigs.py [out_dir]`
- **Editor:** none
- **Docs:** -
- **Tests:** `rig_sweep` consumes the generated rigs
- **Status:** Works - deterministic generators; the committed outputs are what tests read
- **Open:** no doc page for the tools; nothing checks the generated files stay in sync with the generators
- **Related:** 19.3, 42.7
- **Words:** gen_textures, make_test_rigs, procedural textures, starter textures, test rigs, generator
- **Seen as:** a starter texture looks different after regeneration (shouldn't, fixed seeds)
- **Ask me like:** "regenerate the starter textures"

### 41.5 Server deployment kits
- **What:** Ready-made ways to run the relay (which also serves voice calls) on a VPS, and the relay program itself.
- **Parts:**
  - 41.5.1 Docker image - `deploy/voice-server/Dockerfile` - Debian bookworm build of `okayspace-relay` (no libsodium), runs as an unprivileged user, UDP 45100
  - 41.5.2 Compose file - `deploy/voice-server/docker-compose.yml` - `docker compose up -d --build`, restart unless stopped
  - 41.5.3 Native install - `deploy/voice-server/install.sh`, `deploy/voice-server/okayspace-voice.service` - one-shot setup on Ubuntu or Debian: builds, installs to `/usr/local/bin`, systemd unit (`DynamicUser`, `NoNewPrivileges`, strict filesystem), opens the port in ufw; `PORT=` overrides
  - 41.5.4 The relay program - `relay/main.cpp` - see 16.14
  - 41.5.5 Game server kits - exported by the editor (18.7), not in this folder
- **Code:** `deploy/voice-server/Dockerfile`, `deploy/voice-server/docker-compose.yml`, `deploy/voice-server/install.sh`, `deploy/voice-server/okayspace-voice.service`, `relay/main.cpp`, `relay/CMakeLists.txt`
- **API:** `docker build -f deploy/voice-server/Dockerfile -t okayspace-voice .`, `sudo bash deploy/voice-server/install.sh`
- **Editor:** none
- **Docs:** `docs/voice-server.md`, `docs/relay.md`
- **Tests:** `relay_security`
- **Status:** Basic - short, plausible scripts read in full; never run here (no Docker or root)
- **Open:** the Docker build copies the whole repository (`COPY . .`) and builds without libsodium on purpose; no health check or log shipping; no CI job builds the image
- **Related:** 16.14, 16.7, 18.7
- **Words:** deploy, Docker, docker-compose, systemd, install.sh, VPS, voice server, relay, ufw
- **Seen as:** the relay container exits at once; UDP port blocked by a firewall
- **Ask me like:** "deploy the relay on my server", "run the relay in Docker"

### 41.6 Supabase functions and SQL schema
- **What:** The server side of the online platform. SQL files run by hand in the Supabase SQL editor, plus one Edge function. There is no migration runner: each file is idempotent and says which files must have run first.
- **Parts:**
  - 41.6.1 Edge function - `supabase/functions/session-ticket/index.ts` - issues session tickets (39.3), with `ticket.ts`, `make_key.ts` and `ticket_test.ts`
  - 41.6.2 Core social schema - `docs/sql/social.sql` - profiles, friendships, activity, messages, reactions, forums, workshop, community servers and the signup trigger
  - 41.6.3 Social add-ons - `docs/sql/profile_showcase.sql`, `docs/sql/profile_extras.sql`, `docs/sql/follows.sql`, `docs/sql/group_chats.sql`, `docs/sql/chat_extras.sql`, `docs/sql/chat_media.sql`, `docs/sql/forum_sections.sql`, `docs/sql/parties.sql`
  - 41.6.4 Staff and anti-cheat - `docs/sql/admin.sql`, `docs/sql/anticheat.sql`, `docs/sql/app_config.sql`, `docs/sql/official_game_admin.sql` (all need `admin.sql` first)
  - 41.6.5 Multiplayer tables - `docs/sql/game_servers.sql`, `docs/sql/matches.sql`, `docs/sql/matchmaking.sql` (matches needs social; matchmaking uses game_servers, matches, parties and app_config when present)
  - 41.6.6 Local SQL test - `tests/scripts/test_anticheat_sql.sh` - only `anticheat.sql` has one (39.6)
- **Code:** `supabase/functions/session-ticket/index.ts`, `supabase/functions/session-ticket/ticket.ts`, `supabase/functions/session-ticket/make_key.ts`, `supabase/functions/session-ticket/ticket_test.ts`, `docs/sql/*` (all 16 files, named one by one in the Parts above)
- **API:** `supabase functions deploy session-ticket`, `supabase secrets set`, the SQL editor; Storage buckets `releases`, `chat`, `workshop`
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`, `docs/accounts.md`, `docs/anticheat.md`
- **Tests:** `anticheat_sql` (local Postgres, skipped in CI), `anticheat_backend`
- **Status:** Works - the files are in use by the shipping launcher; only one has a local test
- **Open:** no migration tracking (re-run by hand); 15 of 16 SQL files have no test; the Edge function is not deployed and its self-test is not in CI; run order from the headers: `social`, `profile_showcase`, `profile_extras`, `forum_sections`, `follows`, `group_chats`, `chat_extras`, `chat_media`, `admin`, then `anticheat`, `app_config`, `official_game_admin`, and `game_servers`, `matches`, `parties`, `matchmaking`
- **Related:** 17.2, 17.9, 39.3, 39.6, 16.4, 16.9
- **Words:** Supabase, SQL, schema, RLS, SECURITY DEFINER, Edge function, Deno, storage bucket, migration, SQL editor, pg_cron
- **Seen as:** a launcher feature is hidden until a SQL file is run; "permission denied for table"; a Supabase function isn't found
- **Ask me like:** "which SQL file do I run for parties?", "what is the run order for a fresh Supabase project?"

### 41.7 Steam upload kit
- **What:** The SteamPipe scripts that upload the Windows distribution to Steam. Described in 18.8.
- **Parts:**
  - 41.7.1 Build scripts - `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf` - app and depot definitions
  - 41.7.2 Wrappers - `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`, `steam/.gitignore` - run `steamcmd`; `output/` is ignored
- **Code:** `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf`, `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`
- **API:** `STEAM_USER=<builder> steam/upload.sh`
- **Editor:** none
- **Docs:** `docs/steam_release.md`, `steam/README.md`
- **Tests:** none
- **Status:** Basic - see 18.8
- **Open:** see 18.8
- **Related:** 18.8, 17.10
- **Words:** SteamPipe, steamcmd, depot, vdf, Steamworks upload
- **Seen as:** see 18.8
- **Ask me like:** "upload to Steam"

### 41.8 GitHub workflows and templates
- **What:** Everything under `.github/`: two workflows and a PR template. Nothing else (no Dependabot, CODEOWNERS or issue templates).
- **Parts:**
  - 41.8.1 CI - `.github/workflows/ci.yml` - on push to main and every pull request; five jobs `build-and-test`, `sanitizers`, `windows-cross`, `windows-export`, `web-build`; a newer push cancels an older run of the same PR (18.3)
  - 41.8.2 Release - `.github/workflows/release.yml` - on a `v*` tag or manual run; jobs `windows-exe` and `linux-server`; needs `contents: write` (18.4)
  - 41.8.3 PR template - `.github/PULL_REQUEST_TEMPLATE.md` - summary, changes, build warning-clean and ctest passes
  - 41.8.4 Secrets and variables - repository secret `SUPABASE_SERVICE_KEY` (optional publish), repository variable `OKAY_AC_TRUSTED_KEYS` (public keys); no other secrets are read
- **Code:** `.github/workflows/ci.yml`, `.github/workflows/release.yml`, `.github/PULL_REQUEST_TEMPLATE.md`
- **API:** workflow triggers, `workflow_dispatch`, `actions/checkout@v6`, `actions/cache@v5`, `softprops/action-gh-release@v3`, `mymindstorm/setup-emsdk@v15`
- **Editor:** none
- **Docs:** `docs/releasing.md`, `docs/qa_workflow.md`
- **Tests:** -
- **Status:** Works - both files read in full
- **Open:** see 18.3 and 18.4 for the gaps (no libsodium in the Ubuntu job, no TSan, no tests in the release); action versions are pinned by tag, not by commit
- **Related:** 18.3, 18.4
- **Words:** GitHub Actions, workflow, ci.yml, release.yml, secrets, variables, PR template, concurrency
- **Seen as:** a workflow run is cancelled; the release workflow lacks a secret
- **Ask me like:** "list what CI runs", "add a job"

### 41.9 Versioning and release prep
- **What:** One number, set in one place, flows everywhere. A release is a small pull request.
- **Parts:**
  - 41.9.1 Source of truth - `CMakeLists.txt` - `project(OkaySpaceGameEngine VERSION 2.851.0)`; releases increment the middle number (2.850.0, 2.851.0, next 2.852.0)
  - 41.9.2 Where it goes - `CMakeLists.txt` - `OKAY_ENGINE_VERSION` is defined only for the editor and launcher targets (so a bump doesn't recompile everything), the Windows `VERSIONINFO` of every exe, `build-win-dist/VERSION.txt` (written by `scripts/build-windows-dist.sh` from CMakeLists), the installer's `VERSION`
  - 41.9.3 Release PR - `CMakeLists.txt`, `ROADMAP.md`, `dist/CHANGELOG.txt` - "Release X.Y.Z (#N)" changes exactly these three files (PR #573 for 2.849.0, #580 for 2.850.0, #583 for 2.851.0, #588 for 2.852.0)
  - 41.9.4 Update comparison - `launcher/main.cpp`, `editor/main.cpp` - updaters update only when the published version is strictly newer
  - 41.9.5 Stale copy - `dist/VERSION.txt` - says 2.412.0 and is not used by the build
- **Code:** `CMakeLists.txt`, `dist/VERSION.txt`, `dist/CHANGELOG.txt`, `ROADMAP.md`
- **API:** `project(... VERSION ...)`, `OKAY_ENGINE_VERSION`, `VERSION.txt`
- **Editor:** Engine > Check for Updates...
- **Docs:** `docs/releasing.md`
- **Tests:** none
- **Status:** Works - the 2.849.0 diff (PR #573) and the open 2.852.0 PR (#588) touch exactly those three files (per the PR file lists)
- **Open:** `dist/VERSION.txt` is stale and `release.yml`'s comment still points at it (19.6); the changelog is hand-written
- **Related:** 18.4, 18.5, 19.6
- **Words:** version, VERSION.txt, semver, 2.848.0, bump, changelog, release PR, OKAY_ENGINE_VERSION
- **Seen as:** the launcher shows an old version; an exe's Properties show the wrong version
- **Ask me like:** "what files does a version bump touch?"

### 41.10 Dependencies and pinned versions
- **What:** What the build needs and which versions are pinned, so surprises are findable.
- **Parts:**
  - 41.10.1 System libraries - SDL2 (system package; MinGW 2.30.9 fetched in CI and release; Emscripten's own port on the web), libsodium (system package, or MinGW prebuilt 1.0.20, optional)
  - 41.10.2 Fetched at configure time - Dear ImGui docking commit `036bf939...` (editor and launcher; the tag is written in both `CMakeLists.txt` and `launcher/CMakeLists.txt`), Assimp v5.4.3 (only with `OKAY_USE_ASSIMP=ON`, on in the Windows dist)
  - 41.10.3 Vendored single files - `engine/third_party` (stb image, truetype, vorbis; dr_mp3; font8x8; tinyfiledialogs; ufbx) and the editor's ImGuiColorTextEdit copy in `editor/vendor/TextEditor.*`
  - 41.10.4 Tool versions in CI - Emscripten 3.1.51, ubuntu-latest, ubuntu-22.04 for the Linux server, windows-latest for the export check
- **Code:** `CMakeLists.txt`, `engine/CMakeLists.txt`, `launcher/CMakeLists.txt`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`
- **API:** `OKAY_IMGUI_TAG`, `FETCHCONTENT_SOURCE_DIR_IMGUI`, `OKAY_IMGUI_SRC`, `SDL2_VERSION`
- **Editor:** none
- **Docs:** `docs/packaging.md`
- **Tests:** none
- **Status:** Works - versions read from the CMake and workflow files
- **Open:** the ImGui tag is duplicated in two files and must be changed together; configuring the editor, launcher or Assimp needs network access (offline: point `FETCHCONTENT_SOURCE_DIR_IMGUI` at a checkout)
- **Related:** 41.1, 18.3
- **Words:** dependencies, SDL2, libsodium, ImGui, Assimp, FetchContent, third_party, vendored, pinned version, emsdk
- **Seen as:** configure fails offline; ImGui API mismatch after changing the tag
- **Ask me like:** "which ImGui commit do we pin and where?"
