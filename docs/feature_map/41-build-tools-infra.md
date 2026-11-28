# 41. Build, tools & infrastructure

The plumbing around the engine: CMake (root, engine and per-program files, the `cmake/` modules and the MinGW toolchain), the
scripts and developer tools, the deploy kits for the relay, the Supabase Edge function and SQL schema, the Steam upload kit,
the two GitHub workflows, versioning and the pinned dependencies. What a release does for a player is area 18; this page is
where the files live and how they are wired. Nobody is touching it now apart from the 2.852.0 release PR (#588, draft); 2.851.0 is live.

## 41. Build, tools & infrastructure

### 41.1 CMake project layout and options
- **System:** Build System - The CMake project, build scripts, small generator tools and pinned dependency versions that build everything.
  Home: `cmake/`. Entry: `CMakeLists.txt`. Also called: CMake, build scripts, toolchain.
- **What:** One CMake project builds everything: the engine library, the unit tests, the launcher, the player, the editor, the relay, the sandbox and examples, and the developer tools. Options turn parts on or off; defaults suit a plain developer build.
- **Parts:**
  - 41.1.1 Root options - `CMakeLists.txt` - `OKAY_BUILD_SANDBOX` (on), `OKAY_BUILD_TESTS` (on), `OKAY_BUILD_LAUNCHER` (on), `OKAY_BUILD_EDITOR` (off), `OKAY_BUILD_PLAYER` (off), `OKAY_USE_SODIUM` (on), `OKAY_OWN_<FMT>` (on, one per model reader; NDO off; Assimp and its `OKAY_USE_ASSIMP` / `OKAY_PREFER_OWN_READERS` were deleted 2026-11-24), `OKAY_BUILD_TOOLS` (off), `OKAY_SANITIZE`; C++17; `-Wall -Wextra -Wpedantic`
  - 41.1.2 Engine library - `engine/CMakeLists.txt` - `okay_engine` (alias `okay::engine`) built from an explicit source list (nothing is globbed, so a new `.cpp` must be added; all `engine/src` files are listed today); optional backends: libsodium, Steamworks, Photon; four network sources include `shared/` headers (ed25519, clock heartbeat, request builder, HMAC)
  - 41.1.3 Per-program files - `launcher/CMakeLists.txt`, `player/CMakeLists.txt`, `relay/CMakeLists.txt`, `sandbox/CMakeLists.txt`, `examples/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt` - each makes its target and sets `bin/` as the output folder; `editor/CMakeLists.txt` and `okayui/CMakeLists.txt` are the editor's and OkayUI's
  - 41.1.4 Baked defaults - `CMakeLists.txt` - default account server URL and key (`OKAY_DEFAULT_ACCOUNT_URL`, `OKAY_DEFAULT_ACCOUNT_KEY`; read from `config/public-account.json`, -D and environment override), `OKAY_DEFAULT_VOICE_RELAY`, `OKAY_STEAM_APP_ID`, `OKAY_AC_TRUSTED_KEYS`, `OKAY_AC_PLAYER_TRUSTED_KEYS`, `OKAY_AC_HW_SALT` as compile definitions
  - 41.1.5 SDL2.dll copy - `CMakeLists.txt` - on Windows builds a target that copies `SDL2.dll` beside the binaries unless static SDL is on
  - 41.1.6 Relay target - `relay/CMakeLists.txt` - `okayspace-relay`, always built except for Emscripten (no raw UDP in browsers)
  - 41.1.7 Umbrella header and generated API page - `engine/include/Okay.hpp`, `docs/engine_api.md` - `<Okay.hpp>` pulls in most engine headers and must not expose `okay::Clock` (a program with its own `Clock` and `using namespace okay` broke `test_save_scene` once), so `Core/Clock.hpp`, `GameLoop`, `Countdown`, `TimeSnapshot` and `LoopConsole` are included directly and `ClockCursor::Take` takes numbers instead of a `Clock`; `docs/engine_api.md` is generated from the engine binding table by `test_vs_types` (`OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` rewrites it and the test fails when it is stale)
- **Code:** `CMakeLists.txt`, `engine/CMakeLists.txt`, `launcher/CMakeLists.txt`, `player/CMakeLists.txt`, `relay/CMakeLists.txt`, `sandbox/CMakeLists.txt`, `examples/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`
- **API:** `cmake -S . -B build`, `-DOKAY_BUILD_EDITOR=ON`, `-DOKAY_BUILD_PLAYER=ON`, `-DOKAY_USE_SODIUM=OFF`, `-DOKAY_WITH_STEAM=ON -DSTEAMWORKS_SDK_PATH=`, `-DOKAY_WITH_PHOTON=ON`, `-DOKAY_SODIUM_PREFIX=`
- **Editor:** none
- **Docs:** `README.md` (build section), `docs/release/packaging.md`
- **Tests:** none (the CI jobs configure with these options, 18.3)
- **Status:** Works - read the CMake files; CI configures five different option sets
- **Open:** the default Supabase URL and publishable key are written in `CMakeLists.txt` (old map: remove; release CI deliberately doesn't override them); two similarly named switches, `OKAY_STATIC_SDL` (toolchain and root) and `OKAY_EDITOR_STATIC_SDL` (editor, launcher, player); the engine's explicit source list must be edited for every new file
- **Related:** 18.1, 19.7, 41.2, 41.10
- **Words:** CMake, CMakeLists.txt, options, OKAY_BUILD_*, configure, build type, engine library, okay::engine, source list, definitions, build tree
- **Seen as:** "undefined reference" after adding a .cpp (not in the list); a target silently missing (SDL2 not found warning); an option has no effect
- **Ask me like:** "which CMake flag builds the editor?", "add my new source file to the engine build"

### 41.2 CMake modules, Windows resources and toolchain
- **System:** Build System
- **What:** The small CMake helpers: Windows exe name, description and version (what Task Manager shows), the MinGW cross toolchain, and the anti-cheat key-list check.
- **Parts:**
  - 41.2.1 Windows exe metadata - `cmake/OkayWinVersion.cmake`, `cmake/okay_version.rc.in` - `okay_win_version(target description filename)` generates a `VERSIONINFO` resource from `PROJECT_VERSION` and embeds the manifest; no-op off Windows
  - 41.2.2 Application manifest - `cmake/okayspace.exe.manifest` - runs as the invoking user (never asks for admin) and declares Windows 7 to 11 support
  - 41.2.3 MinGW toolchain - `cmake/mingw-w64-x86_64.toolchain.cmake` - `x86_64-w64-mingw32` compilers, find-root settings, a copy of `libwinpthread.a` without its own VERSIONINFO member (so Task Manager doesn't show "POSIX WinThreads"), optional static SDL
  - 41.2.4 Trusted-key check - `cmake/OkayAntiCheatKeys.cmake` - validates `OKAY_AC_TRUSTED_KEYS` (39.7)
- **Code:** `cmake/OkayWinVersion.cmake`, `cmake/okay_version.rc.in`, `cmake/okayspace.exe.manifest`, `cmake/mingw-w64-x86_64.toolchain.cmake`, `cmake/OkayAntiCheatKeys.cmake`
- **API:** `okay_win_version()`, `okay_ac_check_trusted_keys()`, `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake`
- **Editor:** none
- **Docs:** `docs/release/packaging.md`, `docs/online/anticheat.md`
- **Tests:** `anticheat_keys_ok`, `anticheat_keys_bad_*` (the key check only)
- **Status:** Works - used by every Windows build (2.847.0 added the exe names and versions, #545)
- **Open:** no test checks the generated resource; the toolchain's pthread workaround falls back silently
- **Related:** 18.1, 39.7
- **Words:** toolchain, MinGW, cross-compile, VERSIONINFO, resource, rc, manifest, Task Manager, winpthread, windres
- **Seen as:** Task Manager shows the wrong program name; the exe asks for admin; MinGW configure can't find windres
- **Ask me like:** "why does Task Manager say POSIX WinThreads?"

### 41.3 Local build and launch scripts
- **System:** Build System
- **What:** Convenience scripts for a first build and run on Linux, macOS and Windows, plus the launch shortcuts at the repo root.
- **Parts:**
  - 41.3.1 Native build - `scripts/build.sh`, `scripts/build.bat` - configure Release, build, and print how to run the sandbox demo and the tests
  - 41.3.2 Cross-build a Windows exe - `scripts/build-windows-exe.sh` - MinGW build of the sandbox into `dist/OkaySpace.exe` (see 18.1.5)
  - 41.3.3 Launch shortcuts - `launch.sh`, `launch.bat` - build `okayspace-launcher` once, then run it with any arguments
  - 41.3.4 Other scripts - `scripts/build-windows-dist.sh`, `scripts/build-web.sh`, `scripts/publish-supabase.sh`, `scripts/publish-official-game.sh`, `scripts/okayspace-installer.nsi` - mapped in 18.1, 18.2, 18.5, 17.14
- **Code:** `scripts/build.sh`, `scripts/build.bat`, `scripts/build-windows-exe.sh`, `launch.sh`, `launch.bat`, `scripts/build-windows-dist.sh`, `scripts/build-web.sh`, `scripts/publish-supabase.sh`, `scripts/publish-official-game.sh`, `scripts/okayspace-installer.nsi`
- **API:** `scripts/build.sh`, `./launch.sh`, `./build/bin/sandbox`, `cd build && ctest --output-on-failure`
- **Editor:** none
- **Docs:** `README.md`, `docs/release/packaging.md`
- **Tests:** `publish_official_cleanup`, `publish_supabase_checks` (the two publish scripts only)
- **Status:** Works - short scripts, read in full; only the publish scripts have tests
- **Open:** `launch.sh` and `launch.bat` say "update from GitHub, build, and run" but only build and run (stale comment); `build.sh` places `build/` in the repo root
- **Related:** 18.1, 18.5, 41.1
- **Words:** build.sh, build.bat, launch.sh, quick start, first build, scripts folder
- **Seen as:** the first build fails for a missing SDL2; "launcher not found"
- **Ask me like:** "what is the fastest way to build and run the sandbox?"

### 41.4 Developer tools
- **System:** Build System
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
- **System:** Relay
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
- **Docs:** `docs/audio/voice-server.md`, `docs/online/relay.md`
- **Tests:** `relay_security`
- **Status:** Basic - short, plausible scripts read in full; never run here (no Docker or root)
- **Open:** the Docker build copies the whole repository (`COPY . .`) and builds without libsodium on purpose; no health check or log shipping; no CI job builds the image
- **Related:** 16.14, 16.7, 18.7
- **Words:** deploy, Docker, docker-compose, systemd, install.sh, VPS, voice server, relay, ufw
- **Seen as:** the relay container exits at once; UDP port blocked by a firewall
- **Ask me like:** "deploy the relay on my server", "run the relay in Docker"

### 41.6 Supabase functions and SQL schema
- **System:** Online Backend - The Supabase database tables and server functions behind accounts, chat, forums and updates.
  Home: `supabase/`, `docs/sql/`. Entry: `supabase/functions/session-ticket/index.ts`. Also called: Supabase, database, SQL schema, edge functions. Fix: SQL schema in docs/sql/ and server functions in supabase/: move the schema under supabase/ (scripts and tests that read docs/sql/ change with it).
- **What:** The server side of the online platform. SQL files run by hand in the Supabase SQL editor, plus one Edge function. There is no migration runner: each file is idempotent and says which files must have run first.
- **Parts:**
  - 41.6.1 Edge function - `supabase/functions/session-ticket/index.ts` - issues session tickets (39.3), with `ticket.ts`, `make_key.ts` and `ticket_test.ts`
  - 41.6.2 Core social schema - `docs/sql/social.sql` - profiles, friendships, activity, messages, reactions, forums, workshop, community servers and the signup trigger
  - 41.6.3 Social add-ons - `docs/sql/profile_showcase.sql`, `docs/sql/profile_extras.sql`, `docs/sql/follows.sql`, `docs/sql/group_chats.sql`, `docs/sql/chat_extras.sql`, `docs/sql/chat_receipts.sql` (typing and read marks; run after `social.sql`), `docs/sql/chat_media.sql`, `docs/sql/forum_sections.sql`, `docs/sql/parties.sql`
  - 41.6.4 Staff and anti-cheat - `docs/sql/admin.sql`, `docs/sql/anticheat.sql`, `docs/sql/app_config.sql`, `docs/sql/official_game_admin.sql` (all need `admin.sql` first)
  - 41.6.5 Multiplayer tables - `docs/sql/game_servers.sql`, `docs/sql/matches.sql`, `docs/sql/matchmaking.sql` (matches needs social; matchmaking uses game_servers, matches, parties and app_config when present)
  - 41.6.6 Local SQL test - `tests/scripts/test_anticheat_sql.sh`, `tests/scripts/test_chat_receipts_sql.sh`, `tests/scripts/test_blocks_sql.sh` - `anticheat.sql` (39.6), `chat_receipts.sql` and `blocks.sql` have one
  - 41.6.7 Blocks and privacy - `docs/sql/blocks.sql` - `blocks` and `privacy_settings` tables, rules on messages, calls and friend requests, the `profiles_visible` view (needs `social.sql` first; re-run after a later file adds a `profiles` column)
- **Code:** `supabase/functions/session-ticket/index.ts`, `supabase/functions/session-ticket/ticket.ts`, `supabase/functions/session-ticket/make_key.ts`, `supabase/functions/session-ticket/ticket_test.ts`, `docs/sql/*` (all 18 files, named one by one in the Parts above)
- **API:** `supabase functions deploy session-ticket`, `supabase secrets set`, the SQL editor; Storage buckets `releases`, `chat`, `workshop`
- **Editor:** none
- **Docs:** `docs/online/supabase-backend.md`, `docs/online/accounts.md`, `docs/online/anticheat.md`
- **Tests:** `anticheat_sql`, `chat_receipts_sql` and `blocks_sql` (local Postgres, skipped in CI), `anticheat_backend`
- **Status:** Works - the files are in use by the shipping launcher; only one has a local test
- **Open:** no migration tracking (re-run by hand); 15 of 18 SQL files have no test; the Edge function is not deployed and its self-test is not in CI; run order from the headers: `social`, `profile_showcase`, `profile_extras`, `forum_sections`, `follows`, `group_chats`, `chat_extras`, `chat_media`, `admin`, then `anticheat`, `app_config`, `official_game_admin`, and `game_servers`, `matches`, `parties`, `matchmaking`
- **Related:** 17.2, 17.9, 39.3, 39.6, 16.4, 16.9
- **Words:** Supabase, SQL, schema, RLS, SECURITY DEFINER, Edge function, Deno, storage bucket, migration, SQL editor, pg_cron
- **Seen as:** a launcher feature is hidden until a SQL file is run; "permission denied for table"; a Supabase function isn't found
- **Ask me like:** "which SQL file do I run for parties?", "what is the run order for a fresh Supabase project?"

### 41.7 Steam upload kit
- **System:** Steam Integration
- **What:** The SteamPipe scripts that upload the Windows distribution to Steam. Described in 18.8.
- **Parts:**
  - 41.7.1 Build scripts - `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf` - app and depot definitions
  - 41.7.2 Wrappers - `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`, `steam/.gitignore` - run `steamcmd`; `output/` is ignored
- **Code:** `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf`, `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`
- **API:** `STEAM_USER=<builder> steam/upload.sh`
- **Editor:** none
- **Docs:** `docs/release/steam_release.md`, `steam/README.md`
- **Tests:** none
- **Status:** Basic - see 18.8
- **Open:** see 18.8
- **Related:** 18.8, 17.10
- **Words:** SteamPipe, steamcmd, depot, vdf, Steamworks upload
- **Seen as:** see 18.8
- **Ask me like:** "upload to Steam"

### 41.8 GitHub workflows and templates
- **System:** CI
- **What:** Everything under `.github/`: two workflows and a PR template. Nothing else (no Dependabot, CODEOWNERS or issue templates).
- **Parts:**
  - 41.8.1 CI - `.github/workflows/ci.yml` - on push to main and every pull request; five jobs `build-and-test`, `sanitizers`, `windows-cross`, `windows-export`, `web-build`; a newer push cancels an older run of the same PR (18.3)
  - 41.8.2 Release - `.github/workflows/release.yml` - on a `v*` tag or manual run; jobs `windows-exe` and `linux-server`; needs `contents: write` (18.4)
  - 41.8.3 PR template - `.github/PULL_REQUEST_TEMPLATE.md` - summary, changes, build warning-clean and ctest passes
  - 41.8.4 Secrets and variables - repository secret `SUPABASE_SERVICE_KEY` (optional publish), repository variable `OKAY_AC_TRUSTED_KEYS` (public keys); no other secrets are read
- **Code:** `.github/workflows/ci.yml`, `.github/workflows/release.yml`, `.github/PULL_REQUEST_TEMPLATE.md`
- **API:** workflow triggers, `workflow_dispatch`, `actions/checkout@v6`, `actions/cache@v5`, `softprops/action-gh-release@v3`, `mymindstorm/setup-emsdk@v15`
- **Editor:** none
- **Docs:** `docs/release/releasing.md`, `docs/process/qa_workflow.md`
- **Tests:** -
- **Status:** Works - both files read in full
- **Open:** see 18.3 and 18.4 for the gaps (no libsodium in the Ubuntu job, no TSan, no tests in the release); action versions are pinned by tag, not by commit
- **Related:** 18.3, 18.4
- **Words:** GitHub Actions, workflow, ci.yml, release.yml, secrets, variables, PR template, concurrency
- **Seen as:** a workflow run is cancelled; the release workflow lacks a secret
- **Ask me like:** "list what CI runs", "add a job"

### 41.9 Versioning and release prep
- **System:** Release Pipeline
- **What:** One number, set in one place, flows everywhere. A release is a small pull request.
- **Parts:**
  - 41.9.1 Source of truth - `CMakeLists.txt` - `project(OkaySpaceGameEngine VERSION 2.851.0)`; releases increment the middle number (2.850.0, 2.851.0, next 2.852.0)
  - 41.9.2 Where it goes - `CMakeLists.txt` - `OKAY_ENGINE_VERSION` is defined only for the editor and launcher targets (so a bump doesn't recompile everything), the Windows `VERSIONINFO` of every exe, `build-win-dist/VERSION.txt` (written by `scripts/build-windows-dist.sh` from CMakeLists), the installer's `VERSION`
  - 41.9.3 Release PR - `CMakeLists.txt`, `ROADMAP.md`, `dist/CHANGELOG.txt` - "Release X.Y.Z (#N)" changes exactly these three files (PR #573 for 2.849.0, #580 for 2.850.0, #583 for 2.851.0, #588 for 2.852.0)
  - 41.9.4 Update comparison - `launcher/main.cpp`, `editor/main.cpp` - updaters update only when the published version is strictly newer
  - 41.9.5 Stale copy - `dist/VERSION.txt` - says 2.412.0 and is not used by the build
- **Code:** `CMakeLists.txt`, `dist/VERSION.txt`, `dist/CHANGELOG.txt`, `ROADMAP.md`
- **API:** `project(... VERSION ...)`, `OKAY_ENGINE_VERSION`, `VERSION.txt`
- **Editor:** Help > Check for Updates...
- **Docs:** `docs/release/releasing.md`
- **Tests:** none
- **Status:** Works - the 2.849.0 diff (PR #573) and the open 2.852.0 PR (#588) touch exactly those three files (per the PR file lists)
- **Open:** `dist/VERSION.txt` is stale and `release.yml`'s comment still points at it (19.6); the changelog is hand-written
- **Related:** 18.4, 18.5, 19.6
- **Words:** version, VERSION.txt, semver, 2.848.0, bump, changelog, release PR, OKAY_ENGINE_VERSION
- **Seen as:** the launcher shows an old version; an exe's Properties show the wrong version
- **Ask me like:** "what files does a version bump touch?"

### 41.10 Dependencies and pinned versions
- **System:** Build System
- **What:** What the build needs and which versions are pinned, so surprises are findable.
- **Parts:**
  - 41.10.1 System libraries - SDL2 (system package; MinGW 2.30.9 fetched in CI and release; Emscripten's own port on the web), libsodium (system package, or MinGW prebuilt 1.0.20, optional)
  - 41.10.2 Fetched at configure time - nothing since 2026-11-27 (Dear ImGui, the last one, left with the editor's switch to OkayUI)
  - 41.10.3 Vendored single files - none left: `engine/third_party` is gone (ufbx was deleted on 2026-11-23; the stb image code earlier) and the editor's ImGuiColorTextEdit copy was deleted on 2026-11-23 (replaced by `OkayUI::CodeEdit`)
  - 41.10.4 Tool versions in CI - Emscripten 3.1.51, ubuntu-latest, ubuntu-22.04 for the Linux server, windows-latest for the export check
- **Code:** `CMakeLists.txt`, `engine/CMakeLists.txt`, `launcher/CMakeLists.txt`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`
- **API:** `SDL2_VERSION`
- **Editor:** none
- **Docs:** `docs/release/packaging.md`, `docs/status/THIRD_PARTY.md`, `docs/status/own_code_roadmap.md` (every third-party library is planned for removal, with criteria), `docs/platform/crypto_review_package.md`, `docs/platform/crypto_review_request.md`, `scripts/make_crypto_review_bundle.sh`
- **Tests:** none
- **Status:** Works - versions read from the CMake and workflow files
- **Open:** the ImGui tag is duplicated in two files and must be changed together; configuring the editor or launcher needs network access (offline: point `FETCHCONTENT_SOURCE_DIR_IMGUI` at a checkout)
- **Related:** 41.1, 18.3
- **Words:** dependencies, SDL2, libsodium, ImGui, FetchContent, third_party, vendored, pinned version, emsdk
- **Seen as:** configure fails offline; ImGui API mismatch after changing the tag
- **Ask me like:** "which ImGui commit do we pin and where?"

### 41.11 Own-stack dogfood switch and startup line
- **System:** Replacement Switch - One switch that runs the engine with all our own replacements for third-party code turned on, to try them.
  Home: `shared/OwnStack.hpp`, `shared/OwnSwitches.hpp`. Entry: `shared/OwnStack.hpp`. Also called: Own-Stack Switch, OKAY_OWN_STACK, own stack, dogfood switch, OwnSwitches.
- **What:** One switch builds and runs the engine with every own replacement we can safely try (own model readers, own HTTP client, own platform backend on Linux, OkayUI), and each program logs one line saying which parts are ours and which are third-party. Cryptography is never part of it.
- **Parts:**
  - 41.11.1 Build switch - `CMakeLists.txt` - `-DOKAY_OWN_STACK=ON` sets every `OKAY_OWN_<FMT>` option (there is no `OKAY_OWN_FBX` any more: our FBX reader is the only one), the own platform backend as the default on Linux, and OkayUI as the launcher and editor default; `-DOKAY_OWN_STACK_CRYPTO_TEST=ON` (test builds only) adds `OKAY_OWN_CRYPTO`
  - 41.11.2 Run-time switch - `shared/OwnStack.hpp` - `OKAY_OWN_STACK=1` sets the run-time variables (`OKAY_OWN_<FMT>`, `OKAY_PLATFORM_BACKEND=own` on Linux, `OKAY_UI_BACKEND=okayui`) when they are not already set; never crypto
  - 41.11.3 Startup line - `shared/OwnStack.hpp` - `[okay] stack: platform=... http=... readers=... crypto=... ui=...` written to stderr by the player, launcher and editor, and shown in the editor's About popup
  - 41.11.4 Release guard - `scripts/check_release_own_stack.sh` - fails a release build when own crypto or the own-stack switch is on, or when libsodium is not really in the build (cache `OKAY_SODIUM_IN_BUILD`, compiler flags, `--binary` startup line `crypto=libsodium`); `-DOKAY_REQUIRE_SODIUM=ON` makes CMake fail too; the libsodium download is SHA-256 pinned (`scripts/fetch-tarball.sh --sha256`, `scripts/libsodium-mingw.pin`); called by `scripts/build-windows-dist.sh`, `release-local.sh` and `release.yml`
  - 41.11.5 CI job - `.github/workflows/ci.yml` - `own-stack` (not required): builds with the switch on and no libsodium and runs the own pieces' tests; green on main
  - 41.11.7 Settings switches - `shared/OwnSwitches.hpp` - table of the own parts chosen at run time (OkayUI screens, OkayUI Inspector, OkayUI own renderer, and on Linux the own window layer; the own WAV reader item went when it became the only WAV reader, 2026-11-23, and the own HTTP client item on 2026-11-25 when curl was removed), all ON by default since 2026-11-23 (untick to go back); saved in `own_switches.cfg` (`off=` lists what was turned off), applied as environment variables at the top of `main()` (a variable the person set wins); launcher Settings > Experimental and editor View > Rendering & Performance > Experimental share it; the startup line gets `switches=<ids>`; a new seam is one line in `Items()`
  - 41.11.6 Flip proposal - `docs/status/own_flip_proposal.md` - which own replacement could become the default, in what order (one group per release), the readiness rating of each, the prep work (Step 0), a checklist for every flip an agent can run, what blocks deleting each third-party library and how to try each replacement; no default is flipped by it
- **Code:** `CMakeLists.txt`, `engine/CMakeLists.txt`, `shared/OwnStack.hpp`, `shared/OwnSwitches.hpp`, `scripts/check_release_own_stack.sh`, `scripts/fetch-tarball.sh`, `scripts/fetch-libsodium-mingw.sh`, `.github/workflows/ci.yml`, `cmake/OkayPlatform.cmake`
- **API:** `OKAY_OWN_STACK`, `OKAY_OWN_STACK_CRYPTO_TEST`, `OKAY_REQUIRE_SODIUM`, `OKAY_PRINT_STACK_AND_EXIT`, `okay::ownstack::Startup`, `okay::ownstack::Describe`
- **Editor:** Help > About OkaySpace (the stack line); View > Rendering & Performance > Experimental: try our own code; launcher: Settings > Experimental
- **Docs:** `docs/status/own_stack_rollout.md`, `docs/status/own_code_roadmap.md`, `docs/status/own_flip_proposal.md`, `docs/try_it.md`
- **Tests:** `own_stack`, `own_stack_env`, `release_refuses_own_crypto`
- **Status:** Works - tested on Linux; the CI job `own-stack` is green on main (it prints the stack line and runs 131 tests); nothing of it has run on real Windows
- **Open:** the switch and the line cover only FBX (always ours, always listed) and 19 reader switches, not the newer readers (they are on by default anyway); the line cannot know a UI backend saved in a developer build's settings at the top of `main()`; the job builds without Assimp and no test covers the readers' automatic fallback to the old reader
- **Related:** 41.1, 41.8, 41.10
- **Words:** own stack, dogfood, OKAY_OWN_STACK, startup line, stack line, own backends, third-party, own crypto test
- **Seen as:** a line starting `[okay] stack:` in the log; the About popup lists the stack
- **Ask me like:** "how do I run everything on our own code?"

### 41.12 Collaboration tooling (lanes, claims, find-existing, collision check, branch lint)
- **System:**
  - Collaboration Tools - Scripts that keep the three Claudes from doing the same work twice: lanes, claims, find-existing and branch lint. Code: `scripts/collab_lib.py`, `scripts/find_existing.py`, `scripts/collision_check.py`, `scripts/branch_lint.py`, `scripts/make_brief.py`, `scripts/work_board.py`, `.github/workflows/branch-lint.yml`
    Home: `scripts/collab_lib.py`, `scripts/find_existing.py`, `scripts/collision_check.py`, `scripts/branch_lint.py`. Entry: `scripts/collab_lib.py`. Also called: lanes, claims, find_existing, collision check, branch lint.
  - Duplicate Guard - Finds pasted code and new copies of shared helpers and fails CI when it finds them (`scripts/dup_lint.py`). Code: `scripts/dup_lint.py`, `scripts/dup_baseline.json`, `scripts/install_hooks.sh`, `.github/workflows/dup-scan.yml`, `docs/engine/shared_code.md`
    Home: `scripts/dup_lint.py`. Entry: `scripts/dup_lint.py`. Also called: dup_lint, duplicate check, copy-paste check.
- **What:** The working agreement between the three Claudes (coordinator `claude/*`, Claude 2 `claude2/*`, Claude 3 `claude3/*`), made automatic: a lanes table with each lane's branch prefix, one claim format on issue #422, a search for what already exists, a collision report over all open branches, cheap checks on every branch push, a "How to try it" check on PRs and a brief builder that pastes the current rules into every agent brief.
- **Parts:**
  - 41.12.1 Agreement and lanes - `docs/process/collaboration.md`, `docs/lanes.json`, `docs/process/agent_brief_template.md`, `docs/process/claude3_start.md` - who owns which paths, the claim protocol, who merges and who releases, hand-over, the readiness checklist, the weekly dedupe audit
  - 41.12.2 Find existing - `scripts/find_existing.py` - ranked "Existing:" report (feature map, notes, code names, tests, examples, open branches, PRs, claims) to paste into a claim
  - 41.12.3 Collision check - `scripts/collision_check.py` - file and line-range overlaps between branches, claims table, stale claims, out-of-lane edits, generated files edited by hand, duplicate test names; Markdown or JSON; also in `WORK_BOARD.md` via `scripts/work_board.py`
  - 41.12.4 Branch lint - `.github/workflows/branch-lint.yml`, `scripts/branch_lint.py` - on pushes to every lane's branches and on pull requests from any other branch or fork: forbidden thread includes (rule 21a), secrets, docs changed, feature map pages valid (`gen_feature_map.py --validate`, warning), a warning when a branch commits generated docs, MinGW syntax check
  - 41.12.5 Bug patterns - `scripts/bug_patterns.py` - warnings for the bug classes reviews keep finding
  - 41.12.6 PR hygiene and briefs - `.github/workflows/pr-hygiene.yml`, `scripts/check_pr_body.py`, `scripts/make_brief.py`
  - 41.12.7 Duplicate-code guard - `scripts/dup_lint.py`, `scripts/dup_baseline.json`, `scripts/install_hooks.sh`, `docs/engine/shared_code.md` - pasted blocks (exact copies of 80+ tokens fail, renamed copies warn) and new copies of shared helpers (FNV, CRC-32, Base64, Trim/Split, JSON escape ...) in every language, with a shrink-only baseline; runs in branch-lint on every branch, main and fork PRs; optional pre-push hook; similar-name warnings; retired names (`OKAY_DEPRECATED`, `engine/include/okay/Core/Deprecated.hpp`) cannot be used again; copies and similar names between open branches (in `collision_check.py`); weekly scan `.github/workflows/dup-scan.yml`
  - 41.12.8 Code index - `scripts/gen_code_index.py`, `docs/engine/code_index.md` - every public class, struct, enum and free function in the public headers with file:line and its doc line, one page per folder; searched by `find_existing.py`; rebuilt on main by `regen-docs.yml`
  - 41.12.10 Generated docs rebuilt on main - `.github/workflows/regen-docs.yml`, `scripts/precheck.sh` (`--list-generated`, `--regen-generated`, `--restore-generated`) - after every push to main the feature map outputs, the code index and `docs/try_it.md` are regenerated and committed back with `[skip ci]` (one run at a time, only the listed paths, starts again from the new main once if main moved), so PRs never commit them and never conflict in them
  - 41.12.9 PR body "Existing code" - `scripts/check_pr_body.py`, `.github/workflows/pr-hygiene.yml`, `.github/PULL_REQUEST_TEMPLATE.md` - a PR names the code it reuses or replaces, or what it searched
  - 41.12.11 System registry and names (rule 19e) - `scripts/gen_feature_map.py`, `docs/SYSTEMS.md`, `scripts/find_existing.py`, `scripts/dup_lint.py` - every system's Home, Entry, Doc, lane and old names ("Also called") from the System lines; find_existing searches names and old names first; dup_lint refuses new files and folders named Own/New/Improved/Classic/V2 and new system folders with no registry Home; the "Still duplicated" table
- **Code:** `scripts/collab_lib.py`, `scripts/find_existing.py`, `scripts/collision_check.py`, `scripts/branch_lint.py`, `scripts/bug_patterns.py`, `scripts/check_pr_body.py`, `scripts/make_brief.py`, `scripts/work_board.py`, `scripts/dup_lint.py`, `scripts/install_hooks.sh`, `scripts/gen_code_index.py`, `.github/workflows/branch-lint.yml`, `.github/workflows/dup-scan.yml`, `.github/workflows/pr-hygiene.yml`, `.github/workflows/pr-hygiene.yml`, `.github/workflows/work-board.yml`, `.github/workflows/regen-docs.yml`, `scripts/precheck.sh`
- **Docs:** `docs/process/collaboration.md`, `docs/process/agent_brief_template.md`, `docs/process/review_log.md`, `docs/process/no_duplicates.md`, `docs/engine/shared_code.md`
- **Tests:** `collab_scripts` (`scripts/tests/test_collab.py`: throw-away git repos, no GitHub), `dup_lint_script` (`scripts/tests/test_dup_lint.py`), `precheck_generated` (`scripts/tests/test_precheck_generated.py`: precheck's generated-docs options and the regen-docs.yml commit-back step against a throw-away bare repository)
- **Status:** Works - the scripts and their tests were run on Linux against the real repo; the two new workflows have not run on GitHub yet
- **Open:** heuristics can give false positives (see the "false positives" notes in `docs/process/collaboration.md`); the weekly dedupe audit and the answers to claims are by hand
- **Related:** 41.1, 41.8
- **Words:** collaboration, claim, lane, duplicate, collision, branch lint, find existing, brief, how to try it check, copy paste, clone detection, dup lint, shared code, system registry, system names, aliases, also called, still duplicated, banned qualifier, regen docs, generated docs, restore generated
- **Seen as:** `WORK_BOARD.md` on the `work-board` branch, a red "Branch lint" check on a pushed branch, a red "PR hygiene" check on a PR
- **Ask me like:** "how do the three Claudes avoid doing the same work?"

### 41.13 Engine modules (stage 0: groups, test labels, module list, dependency check)
- **System:** Build System
- **What:** The engine split into 40 named modules on paper: each has a manifest saying what it requires and which engine files and tests are its own. The build groups the engine's files by module and labels every test with its module, without changing what is built. First step of the modular engine plan (`docs/design/modular_gameplay_contracts.md`, section 9, PR 1).
- **Parts:**
  - 41.13.1 Manifests - `engine/modules/<name>/module.okaymodule` - name, requires, sources (file patterns), tests (test name patterns), planned (files other branches are adding)
  - 41.13.2 Build groups - `cmake/OkayModules.cmake` - `okay_modules_read`, `okay_add_module`, `okay_modules_group(okay_engine)` (writes `okay_modules.tsv`), configure fails on an unknown requirement or a cycle
  - 41.13.3 Test labels - `okay_modules_label_tests` - every test gets `module:<name>` (or `module:core`); `ctest -L module:physics3d`
  - 41.13.4 Module list at run time - `Modules/ModuleManifest`, `Modules/ModuleRegistry` - the manifests embedded by `okay_modules_generate` (`modules_enabled.cpp`), `ModuleRegistry::Get()`, `Validate`, `DependencyOrder`, register hooks (`ModuleContext`); the player logs `[okay] modules: 40 modules, 40 on`
  - 41.13.5 Dependency check - `scripts/check_modules.py` - forbidden includes between modules, manifest problems, the umbrella header; about 800 known includes in `scripts/module_deps_baseline.txt` (shrink only); report mode in CI
  - 41.13.6 Size per module - `scripts/module_sizes.py` - bytes of the player per module from the linker map (`maps/okay-player.map`), written to `module_sizes.txt` beside the player after every link; Build Game's report has a Modules tab (`buildpipe::Report::AddModules`)
  - 41.13.7 Project Settings > Modules - `editor/ModulesModel.hpp`, `editor/ModulesPanelUI.hpp` - presets and a tick per module, requirements added and kept, what the open scene uses; saved as `modules=` in project.okayproj; the Build report marks modules that are off and warns when the scene uses one (every module still ships)
  - 41.13.8 New Project modules - `editor/ModulesPanelUI.hpp` `DrawNewProjectModules`, `PresetForTemplate` - a folded Modules line in the New Project form; a few templates start with a smaller preset, the rest with every module
- **Code:** `cmake/OkayModules.cmake`, `engine/modules/*/module.okaymodule`, `engine/include/okay/Modules/ModuleManifest.hpp`, `engine/include/okay/Modules/ModuleRegistry.hpp`, `scripts/check_modules.py`
- **Docs:** `docs/engine/modular_engine.md`, `docs/design/modular_gameplay_contracts.md`, `docs/design/modular_status.md`
- **Tests:** configure itself (a bad manifest fails it); `modules` (parser, registry, the generated list); `check_modules_script` (`tests/scripts/test_check_modules.py`, the check on a made-up tree); `module_sizes_script` (`tests/scripts/test_module_sizes.py`, a made-up linker map); `build_pipeline` (the report's Modules section); `modules_model` (the settings page's model and the project file line)
- **Status:** Partial - grouping and labels only; nothing is stripped yet
- **Open:** the registries that let a module register its own records, functions and nodes (stage 1), per-module libraries and stripping (stage 2); the check becomes blocking in PR 14
- **Related:** 41.1
- **Words:** module, modular engine, manifest, okaymodule, module group, ctest label, strip
- **Seen as:** `-- OkaySpace: 40 engine modules; ... files of okay_engine in modules` while configuring; `[okay] modules: 40 modules, 40 on` in the player log; a Modules tab in Build Game's report; Edit > Project Settings > Modules; the Modules line in File > New Project; a "Modules" warning on a pushed branch
- **Ask me like:** "which tests belong to the physics module?"

### 41.14 okay command line (with Git underneath)
- **System:** Okay CLI - One program, `okay` (`okay.exe`), that makes, opens, plays, runs, builds and tests projects and keeps them in version control with the person's own Git.
  Home: `tools/okay-cli/`. Entry: `tools/okay-cli/Cli.cpp`. Also called: okay, okay.exe, okay_cli, okay vc, command line, CLI, OKAY_BUILD_CLI, Command Line panel, CommandLinePanel, okay_cli_panel, CommandJob.
- **What:** A command line for the engine: `okay new`, `open`, `play`, `run`, `build`, `test`, `info`, `doctor`, `help`, and `okay vc` (init, clone, status, save, resolve, history, undo, branch, switch, sync, remote, who) on top of the installed `git` program. It wraps what exists (project creation, the editor's flags, the player, okay_run, Build Game, SceneMerge, SceneDiff) instead of copying it. Messages are short plain sentences.
- **Parts:**
  - 41.14.1 Commands and help - `tools/okay-cli/Cli.cpp` - one table of commands and their help; `okay::CommandLine` parses each command; `--project FOLDER` before the command (never taken from after it); the project is found upward from the current folder (`project.okayproj`, else `Assets/`)
  - 41.14.2 One process seam - `tools/okay-cli/Process.cpp` - the only code that starts a program, always by its full path from absolute PATH folders (never the current folder; `.cmd`/`.bat` refused): CreateProcessW with `lpApplicationName` on Windows (quoting from `shared/AppRestart.hpp`, stderr to a temp file, no threads, the standard handles passed on), fork/execv with poll on Linux; a missing program is "not started", not a crash
  - 41.14.3 Projects - `okay new` (an empty project made headless from `shared/ProjectCreate.hpp`, `ProjectState.hpp` and the `engine_version` stamp; any other template is made by the editor's `--new-project --template`), `okay open` (editor `--open`), `okay play` (player), `okay build` (editor `--build-game`), `okay run` (`scripts/okay_run.py`), `okay test` (every scene and prefab loads and has no clash marks; `--engine` runs ctest), `okay info` / `okay doctor`
  - 41.14.4 Git underneath - `tools/okay-cli/Git.cpp`, `tools/okay-cli/Vc.cpp` - the only code that runs git: `okay vc init` writes a project-aware `.gitignore` (lines anchored to the project's top: the Build Settings output folder, `/build/`, `/.okay/` caches and per-person files, scene `.bak` copies) and a `.gitattributes` (scenes and prefabs merge and diff with the engine; pictures, sounds, video and models binary), refuses a project inside another repository unless `--separate` or `--outer`; `clone` joins a project; status grouped into scenes and prefabs, scripts, assets and project settings; save (refuses while a clash is not settled), history, undo (one file; the current copy is kept in `.okay/backups/undo` under its path and a number), branch, switch (refuses unsaved changes unless `--carry`), sync (fetch, merge, push to any Git remote; prints the clash list), who (this project, or `--global`)
  - 41.14.5 Scene merge and diff drivers - `okay vc merge-driver %O %A %B %P`, `okay vc diff-driver` - registered in `.git/config` by `okay vc init` and pointed at a stable okay (PATH, or the launcher-level Tools) by every `okay vc` command; both sides are upgraded with `SceneMigration` first, merged with `SceneMerge` (the result must load into a scratch scene); a real clash leaves the file unmerged and loadable, and lists every clash with both values in plain words and in `.okay/clashes/<file>.txt` (the words follow the person's side also in a rebase); a file that is not a scene is merged as text by `git merge-file`
  - 41.14.6 Settling clashes - `okay vc resolve <file> --mine | --theirs | --as-is` - merges again from git's three stored versions with every clash settled on the chosen side, keeping all changes that did not clash (a file that is not a scene is taken whole); `--as-is` keeps the file as fixed by hand
  - 41.14.7 Building the engine itself - `tools/okay-cli/Engine.cpp` - `okay engine build [editor|launcher|player|all] [--debug] [--jobs N] [--clean] [--run] [--dir BUILD] [--source DIR] [--sdl2 DIR]` builds `OkayEngine.exe`, `OkaySpace.exe` and `OkaySpacePlayer.exe` from an engine checkout (found upward from the current folder by `project(OkaySpace` in `CMakeLists.txt`) with the same CMake options, targets and `<build>/bin` as `scripts/build-local.ps1` (the test checks the two agree; the .ps1 stays to build okay itself on a PC without it); cmake, ninja, g++/gcc, mingw32-make and Visual Studio (vswhere) are found by full path and passed to CMake; SDL2 is found in `--sdl2`, `SDL2_DIR`, the checkout's `third_party`, next to it, `C:\SDL2`, `C:\libs`, `C:\dev` or MSYS2, else the exact download is named; `--clean` deletes only a folder with `CMakeCache.txt`; `okay engine doctor` lists what is missing and where to get it
  - 41.14.8 Project content - `tools/okay-cli/Content.cpp` - `okay project info | upgrade | settings [get|set]` (ProjectState's parser and writer, the stamp kept; the editor's `UpgradeJob` with its backup), `okay scene list | new | check | diff | info` (SceneValidator, SceneDiff, SceneSerializer, the asset references), `okay script run | check | new` (the engine's OkayScript VM from `CreateScriptVM`; `print` comes out on the terminal; `StarterScriptSource`), `okay vs list | check` (`okay::vs::ValidateGraphText`, what the editor's Problems button lists), `okay asset list [--type] | find | used-by | uses | import [--to]` (the Project panel's model `editor/ProjectBrowser.cpp` on AssetDatabase's graph; import copies under a free name and writes the `.okaymeta` with `EnsureMeta`)
  - 41.14.9 Help, completion and JSON - `tools/okay-cli/Cli.cpp` - `okay help` lists every command and sub-command in groups, one line each, from one table; `okay completion bash | powershell` prints a Tab-completion script made from the same table; `--json` on `info`, `vc status`, `project info`, the `list` commands and the `check` commands; `okay build --list-targets` (Build Settings' platforms, `bsm::PlatformName`) and `okay export` (`build --zip`)
  - 41.14.10 okay inside a window: the Command Line panel - `tools/okay-cli/CommandJob.cpp`, `tools/okay-cli/CommandInput.cpp`, `shared/CommandLinePanel.cpp` - `okaycli::RunInProcess` runs one command from its words in this program (the same `okaycli::Main`; `Io::sink` takes each message, nothing is printed, nothing exits; an exception becomes a problem line) with the project folder as `--project`; `okaycli::CommandJob` runs it on an `okay::thr` worker thread, hands the lines over once a frame, and Stop ends it at its next message (`Io::cancel` throws `Cancelled`) and stops a program it started (`Process.hpp` `AttachedOutput`: while set on the thread, `Run(..., capture = false)` starts the program as a `Child` with no console and streams its output); inside a host, git's merge driver is the `okay` program beside it, never the host. `CommandInput.hpp` holds the typed-line rules both windows share: words (no shell), `clear`, a leading "okay" dropped, Tab completion from the command table, the history, the output lines with their kinds. The panel (`okaycli::CommandLinePanel`, OkayUI only): the project's name, Stop, Copy (every line, to the host's clipboard) and Clear; the output with problems in red and the panel's notes dimmed, kept scrolled to the end; the choices when Tab finds several; the input line (Enter, Up / Down, Tab) and Run. Hosted in the editor as View > Panels > Command Line (`editor/OkayPanels.cpp` `DrawCommandLineOkayUI`, a dockable window; `okay_cli_panel` linked when it exists)
- **Code:** `tools/okay-cli/Cli.cpp`, `tools/okay-cli/Vc.cpp`, `tools/okay-cli/Engine.cpp`, `tools/okay-cli/Content.cpp`, `tools/okay-cli/Git.cpp`, `tools/okay-cli/Process.cpp`, `tools/okay-cli/CommandInput.cpp`, `tools/okay-cli/CommandJob.cpp`, `tools/okay-cli/main.cpp`, `shared/CommandLinePanel.cpp`, `CMakeLists.txt` (`okay_cli`, `okay_cli_lib`, `okay_cli_panel`, `OKAY_BUILD_CLI`), `scripts/build-windows-dist.sh` (copies `okay.exe` into `Tools/`)
- **API:** `okay help`, `okay new <name> [--template X]`, `okay vc init`, `okay vc save "message"`, `okay vc resolve <file> --mine`, `okay engine build editor`, `okay engine doctor`, `okay scene check --json`, `okay asset used-by <file>`, `okay project settings set <key> <value>`, `okay completion bash`, `okaycli::Main`, `okaycli::SceneMergeDriver`, `okaycli::Run`, `okaycli::RunInProcess`, `okaycli::CommandJob`, `okaycli::CommandLinePanel`
- **Editor:** View > Panels > Command Line (the commands run inside the editor, in the open project); otherwise a separate program that starts the editor and the player
- **Docs:** -
- **Tests:** `okay_cli` (`tests/test_okay_cli.cpp`: arguments, help, program lookup, .gitignore/.gitattributes, status grouping, empty project and `okay test`, the merge and diff drivers on files, and with git installed init/clone/who/save/resolve/history/undo/branch/switch/remote/sync, a clean merge, clashes settled with --mine, --theirs and --as-is, a rebase clash, a sync clash, a stale driver path and a project inside another repository, on temporary repositories; exit 77 when git is missing); `build_settings` (the anchored ignore lines); `okay_cli_run` (`tests/test_okay_cli_run.cpp`: words in, lines and exit codes out, the folder as --project, a worker-thread job that streams and stops inside a started program, git never told to run the host); `command_line_panel` (`tests/test_command_line_panel.cpp`: the panel model, keys through OkayUI and the golden `okayui/tests/golden/command_line_panel.png`)
- **Status:** Partial - built and tested on Linux; the Windows build is compiled with MinGW (syntax only) and not run; `okay open`, `play` and `build` with a real editor and player are not tested; the Command Line panel is built and tested headless on Linux, the editor hosting it is not built or run here
- **Open:** Windows run unverified; no editor Git panel yet (28.1); a clash is settled per file (all clashes of a file on one side, plus `--as-is`), not per object; `okay run` needs the engine source folder (Python script)
- **Related:** 28.1, 28.2, 28.3, 28.4, 18.6, 41.4
- **Words:** okay, okay.exe, command line, CLI, terminal, okay new, okay open, okay play, okay build, okay vc, git, version control, merge driver, diff driver, gitignore, gitattributes
- **Seen as:** I want to make and build a game from a terminal; scenes clash after a git merge; autosaves and build output in my commits
- **Ask me like:** "add a command to the okay CLI"; "save my project with okay vc save"

### 41.15 Engine automation commands and the MCP server
- **System:** Automation - One table of commands that scripts, tests and AI agents run to work the engine and the editor (scenes, objects, components, undo, Play, modeling, UI, visual scripts, assets, builds), served over MCP by `okay mcp`.
  Home: `engine/include/okay/Automation/`, `engine/src/Automation/`. Entry: `engine/include/okay/Automation/Automation.hpp`. Doc: `docs/engine/automation.md`. Also called: automation API, MCP server, Model Context Protocol, okay mcp, okay automation, JSON-RPC, tools/list, tools/call, Automation Link, editor link, EditorHost, LinkServer, McpServer, OKAY_AUTOMATION_LINK_FILE.
- **What:** Every command has a name (`scene.hierarchy`), a plain summary, a parameter schema (JSON Schema for MCP), a result shape, error codes and flags, and calls the system that already does the work. Edits are one step of the editor's own Undo history. `okay mcp` serves the table to an MCP client over stdio, headless or in front of a running editor (View > Automation Link); `okay automation list | describe | call | run | link` runs it from scripts.
- **Parts:**
  - 41.15.1 The table - `Automation/Automation` - `Registry` (sorted by name; a name or MCP tool name used twice is refused), `Command`, `Param` types (string, number, integer, boolean, vec3, object, array, any; options and defaults), `Registry::Run` checks the parameters against the schema and refuses editor-only commands without the editor and edits while playing; every engine error and C++ exception becomes `{code, message, details}` (unknown_command with similar names, invalid_params, not_found, ambiguous, refused, unavailable, failed, internal); `Host::BeginEdit` / `EndEdit` bracket each Undoable command; `AddProvider` lets a system add its commands where its code lives
  - 41.15.2 Engine commands - `engine/src/Automation/EngineCommands.cpp` - automation.list / describe / host; api.functions (the shared script table) and api.classes (the generated bindings); script.call (any shared script function, its error reported); scene.info / hierarchy / find / validate (Scene::Validate and every visual script graph); object.inspect (every component's properties); component.types / describe / get / set (the property table: checked, clamped, hooks fired) / call (any bound method) / add / remove; transform.get / set; physics.raycast (3D or 2D, first or all) / overlap / settings (gravity, rate) / bodies; anim.list / sample / set_key / set_param (Animator, AnimStateMachine); viewport.screenshot (the software renderer's headless path from the main camera, a camera object, the editor's Scene view or an eye and target; PNG file, optional inline image); log.read (a ring of the engine log)
  - 41.15.3 Editor commands - `editor/AutomationEditor.cpp`, `editor/AutomationVs.cpp` - on `EditorState`: scene.new / open / save / state (unsaved changes protected); object.create (empty, primitives, camera, sprite) / delete / duplicate / rename / reparent / set_active; editor.select / selection; prefab.instantiate; undo.undo / redo / history; play.start / stop / step (frames with keys, taps and the mouse fed as PlayTest feeds them, through the editor's Play loop) / state; model.ops / apply (the Modeling window's tools and Subdivide, Smooth, Weld ... buttons) / info; ui.tree (widget rectangles on a screen size) / create; vs.node_types / inspect / create / add_node / connect (refused when the pins do not fit, with the conversion) / set_node / remove_node, each an undo step that puts the .okayvs file back
  - 41.15.4 okay command line commands - `tools/okay-cli/AutomationCli.cpp` - project.info / settings / test, scene.list / check, script.check, vs.list / check, asset.list / find / used_by / uses / import, build.targets / run (dry run by default), cli.run (any okay command line but open, play, mcp): `okaycli::Main` with `--json`, its output as data
  - 41.15.5 MCP - `Automation/Mcp` - JSON-RPC 2.0 (requests, notifications, batches, -32700 / -32600 / -32601 / -32602 / -32603) and MCP initialize (versions 2024-11-05 to 2025-11-25), ping, tools/list (name with '.' as '_', title, description, inputSchema, read-only and destructive hints), tools/call (text and structuredContent, image content for screenshots, isError for a command's own failure); `ServeStdio` one message per line
  - 41.15.6 Hosts and the editor link - `editor/AutomationEditor.hpp` (`EditorHost`), `Automation/Link`, `tools/okay-cli/Automation.cpp` - one host type for both places: the editor wraps its own state, `okay mcp --headless` and `okay automation` a windowless one; View > Automation Link (off by default, remembered in the editor's `automation_link.txt`) starts a server on 127.0.0.1 with a fresh 32-byte token from the operating system, written with the port to a file only this user can read (`automation/editor_link.json` in the per-user config folder; `OKAY_AUTOMATION_LINK_FILE` overrides), answered between frames on the main thread; `okay mcp` connects to it when it is there (`--editor` requires it, `--headless` never uses it); `okay automation link` keeps a windowless host with the link on (several agents share one scene)
- **Code:** `Automation/Automation`, `Automation/Mcp`, `Automation/Link`, `engine/src/Automation/EngineCommands.cpp`, `editor/AutomationEditor.hpp`, `editor/AutomationEditor.cpp`, `editor/AutomationVs.cpp`, `tools/okay-cli/Automation.cpp`, `tools/okay-cli/AutomationCli.cpp`, `Core/Json` (Write, WriteNumber, the Value builders), `Testing/Regression` (SceneRenderOptions::View), `Scripting/ScriptApiRegistry` (CallScriptApiChecked: a call that reports its error)
- **API:** `okay mcp`, `okay automation list`, `okay automation call <command> '<json>'`, `okay automation run <file.jsonl>`, `okay automation link`, `okay::automation::Commands()`, `Registry::Run`, `AddProvider`, `McpServer::Handle`, `ServeStdio`, `LinkServer`, `LinkClient`, `oked::automation::EditorHost`, `LinkFrame`
- **Editor:** View > Automation Link (a toggle; the log says the port and the file)
- **Docs:** `docs/engine/automation.md`
- **Tests:** `automation` (`tests/test_automation.cpp`: the table, schemas and errors, MCP in process, the link and its token file, and each command group on the real systems), `automation_mcp` (`tests/scripts/test_automation_mcp.py`: a real `okay mcp` process: initialize, tools/list, a scene round trip with undo, redo, save and reopen, a screenshot, a play step, errors, `okay automation run`, and `okay mcp --editor` through `okay automation link`)
- **Status:** Partial - the table, the engine, editor and okay commands, MCP over stdio and the link are built and tested on Linux with a windowless editor state; the editor itself (the View menu toggle and the per-frame call in editor/main.cpp) is not built or run here; Windows unverified
- **Open:** the live editor link is not run in a real editor yet; screenshots are the software renderer's meshes and lights (no sprites, UI, particles or sky, not the Scene view's own pixels); a command on the link runs inside the editor's frame, so a long play.step or build.run holds the window until it ends (no job or progress reporting yet); when the editor closes mid-session okay mcp answers with an error and ends; MCP resources, prompts, progress and cancellation are not offered; no hand-written commands yet for prefab mode (open, apply, revert), mesh edit-mode operations (loop cut, bevel edges, knife), UV and sprite editors, materials and textures, terrain and voxel sculpting, navigation bakes, light baking, the Timeline, Dialogue, Behavior Tree and Animator graph editors (states and transitions), input action and localization tables, audio mixing, networking: they are reached only through component.get / set / call and script.call where their components and functions are bound
- **Related:** 41.14, 13.1, 14.1, 15.1, 1.3
- **Words:** automation, MCP, Model Context Protocol, AI agent, Claude, JSON-RPC, okay mcp, okay automation, tools, scripting the editor, remote control, headless editor, screenshot, Automation Link
- **Seen as:** I want an AI agent to build my level; I want a script to drive the editor; okay mcp does not see my open editor
- **Ask me like:** "add an automation command for the Timeline"; "let Claude operate the editor"
