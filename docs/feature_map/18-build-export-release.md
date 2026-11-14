# 18. Build, export & release

How OkaySpace is built, shipped to players and kept up to date: the cross-compiled Windows distribution and its installer,
web and mobile export, game export from the editor, the dedicated-server export, the five CI jobs, the release workflow, the
Supabase and GitHub update feed, and the Steam upload kit. Files: `scripts/`, `.github/workflows/` (read-only here), `steam/`,
`cmake/`, and the export code in `editor/main.cpp` (namespace `builder`). The CMake and tool plumbing itself is mapped in 41.
Release 2.851.0 is live (tag `v2.851.0`, 2026-11-09); the 2.852.0 release PR (#588, draft, branch `release-2852`, touches only `CMakeLists.txt`, `ROADMAP.md` and `dist/CHANGELOG.txt`) is being cut and will hold #585, #586 and #587. The automated-testing runner of #572 (`okay-playtest`) is merged and runs as ctests.

## 18. Build, export & release

### 18.1 Windows build and installer
- **What:** Cross-compiles the whole Windows distribution (launcher, editor, player, relay and `SDL2.dll`) from Linux with MinGW-w64 and wraps it in a one-click installer that installs or updates in place.
- **Parts:**
  - 18.1.1 Distribution build script - `scripts/build-windows-dist.sh` - configures with the MinGW toolchain, builds launcher, editor, player (Assimp on), copies the layout into `build-win-dist/`; env `SDL2_MINGW_PREFIX` (required), `OKAY_SODIUM_MINGW_PREFIX`, `OKAY_AC_TRUSTED_KEYS`, `OKAY_STEAM_APP_ID` (default 1172560), `STEAMWORKS_SDK_PATH`, `OKAY_IMGUI_SRC`, `JOBS`
  - 18.1.2 Distribution layout - `scripts/build-windows-dist.sh` - `OkaySpace.exe` and `SDL2.dll` at the top; `Tools/` holds `OkayEngine.exe`, `OkaySpacePlayer.exe`, `okayspace-relay.exe`, a second `SDL2.dll`, `VERSION.txt`, `accounts.md`; starter `Assets/Textures` and `Assets/Fonts`; `README.txt`; `VERSION.txt` is read from `CMakeLists.txt`
  - 18.1.3 Split debug info - `scripts/build-windows-dist.sh` - the editor's DWARF moves to `build-win-symbols/OkayEngine.debug` so the exe fits Supabase's 50 MB limit; a debuglink stays in the exe
  - 18.1.4 NSIS installer - `scripts/okayspace-installer.nsi` - per-user (`$LOCALAPPDATA\OkaySpace`, no admin), finds a prior install (registry, Apps entry, the folder it runs from) and updates it in place, waits for running exes to close, makes Start Menu and desktop shortcuts, an uninstaller and an Apps & features entry; user data is never touched
  - 18.1.5 Quick sandbox exe - `scripts/build-windows-exe.sh` - an older helper that builds only the sandbox demo and copies it to `dist/OkaySpace.exe`
  - 18.1.6 Toolchain and exe metadata - `cmake/mingw-w64-x86_64.toolchain.cmake`, `cmake/OkayWinVersion.cmake` - see 41.2
  - 18.1.7 Third-party notices in the package - `scripts/gen_third_party_notices.py`, `scripts/third_party_notices_data.json`, `scripts/check_dist_notices.py`, `THIRD_PARTY_NOTICES.txt` - the notices file is generated from a hand-written data file (licence text copied only from licence files in the repo); the dist script stages it at the top of the folder plus the licence files in `Licenses/`, and the check fails the build or the zip when they are missing; the uninstaller removes them. Five items still say "licence text missing: owner to supply" (`docs/asset_licences.md`)
- **Code:** `scripts/build-windows-dist.sh`, `scripts/okayspace-installer.nsi`, `scripts/build-windows-exe.sh`, `cmake/mingw-w64-x86_64.toolchain.cmake`, `CMakeLists.txt`
- **API:** `scripts/build-windows-dist.sh`, `makensis -DVERSION= -DDISTDIR= -DOUTFILE= scripts/okayspace-installer.nsi`, `OkaySpace-Setup.exe`
- **Editor:** none (File > Build Settings builds a game, not the engine: 18.6)
- **Docs:** `docs/packaging.md`, `docs/asset_licences.md`
- **Tests:** ctest `third_party_notices` (notices file not stale) and `dist_notices_check` (the package check accepts a good folder and zip, rejects bad ones); no archive is built in ctest; the `windows-cross` CI job builds it on every PR (18.3) and the release workflow builds the installer (18.4)
- **Status:** Works - built by CI on every PR and by the release workflow on every release
- **Open:** "Stop committing dist binaries" (old map): `dist/` still holds June 2026 exes and a `VERSION.txt` of 2.412.0; `build-windows-exe.sh` copies the sandbox demo as `dist/OkaySpace.exe`, which is not the launcher (possible stale script); the Windows build is never executed in CI beyond six test exes (18.3.4)
- **Related:** 18.4, 18.5, 41.2, 41.3, 19.6
- **Words:** Windows build, MinGW, cross-compile, build-win-dist, distribution, dist, installer, NSIS, OkaySpace-Setup.exe, SDL2.dll, Tools folder, symbols, OkayEngine.debug
- **Seen as:** the installer asks to close OkaySpace; an exe is missing SDL2.dll; the editor exe is huge; "no MinGW" or "SDL2_MINGW_PREFIX not set" errors
- **Ask me like:** "build the Windows folder locally", "why does the installer skip the folder question?"

### 18.2 Web and mobile export
- **What:** Putting a game in a browser (WebAssembly through Emscripten) or on a phone. Web has a real build script and a CI job; Android and iOS are a data export plus instructions.
- **Parts:**
  - 18.2.1 Web build script - `scripts/build-web.sh` - `emcmake` builds the player without libsodium and Assimp (none for WebAssembly), outputs `build-web/web/okay-player.html`, `.js`, `.wasm`; an optional scene argument is preloaded as `game.okayscene`; serve over HTTP
  - 18.2.2 Emscripten player target - `player/CMakeLists.txt` - the `EMSCRIPTEN` branch uses `-sUSE_SDL=2`, memory growth and an `.html` output (see 42.6)
  - 18.2.3 Editor export for Web and Android - `editor/main.cpp` (`builder::ExportForPlatform`, near L3890) - writes `game/` (Web) or `assets/` (Android) with the scene, config and referenced assets, plus `build-web.sh` or an Android README
  - 18.2.4 Mobile guides - `docs/web_mobile.md` - Android from SDL2's project template, iOS from the Xcode template, touch arrives as mouse events
- **Code:** `scripts/build-web.sh`, `editor/main.cpp`, `player/CMakeLists.txt`
- **API:** `scripts/build-web.sh [scene]`, Build Settings platform Web or Android
- **Editor:** File > Build Settings > Platform: Web (WebGL / WASM), Android (project)
- **Docs:** `docs/web_mobile.md`
- **Tests:** none as ctest; the `web-build` CI job (18.3.5)
- **Status:** Partial - web builds and is checked in CI; Android and iOS are instructions only (the export writes `assets/` and a README, not a Gradle project)
- **Open:** the Build Settings tooltip says "Export an Android Studio project" but the code writes assets and a README only (possible mismatch); the web build excludes the relay and OkayUI's Direct3D backend. Touch gestures and on-screen controls are fed from SDL finger events on desktop and web (`docs/input.md`)
- **Related:** 18.3, 15.7, 42.6
- **Words:** web, WebGL, WASM, WebAssembly, Emscripten, emsdk, Android, iOS, mobile, APK, touch, okay-player.html
- **Seen as:** the web page is blank or the game doesn't load from a file:// URL; Android export gives no APK; a feature missing on the web build (networking relay, encryption)
- **Ask me like:** "build the web version and serve it", "how do I get this on Android?"

### 18.3 CI jobs
- **What:** The five GitHub Actions jobs in `.github/workflows/ci.yml` that every pull request must pass (and every push to main runs). A new push to a PR cancels that PR's older run.
- **Parts:**
  - 18.3.1 Build & test (Ubuntu) - `.github/workflows/ci.yml` - installs cmake, g++, ninja, libsdl2-dev; Release build and the whole `ctest`; also builds the player, builds the editor and launcher and runs `OkayEngine --selftest` headless, then exports a game and runs `tests/export/verify.sh`
  - 18.3.2 Scripting and robustness tests (ASan + UBSan) - `.github/workflows/ci.yml` - Debug `-g1` with `address,undefined,float-cast-overflow`, libsodium off, 45 min limit; builds and runs 38 chosen test binaries directly, not through ctest (19.2.5)
  - 18.3.3 Cross-compile Windows build (MinGW) - `.github/workflows/ci.yml` - apt packages cached, SDL2 2.30.9 and libsodium 1.0.20 fetched, ccache; runs `scripts/build-windows-dist.sh`, builds six test exes, uploads artifact `windows-ci-build` (3 days)
  - 18.3.4 Windows export check - `.github/workflows/ci.yml` - on `windows-latest`: runs `test_vs_acceptance`, `test_vs_debug`, `test_game_fiber`, `test_polymesh`, `test_mesh_asset`, `test_poly_ops`, then `tests/export/verify.ps1` with the cross-built editor
  - 18.3.5 WebGL build (Emscripten) - `.github/workflows/ci.yml` - emsdk 3.1.51, `bash scripts/build-web.sh`, checks the `.html` and `.wasm` exist, uploads `okayspace-webgl`
  - 18.3.6 PR template - `.github/PULL_REQUEST_TEMPLATE.md` - summary, changes, and two ticks: warning-clean build and passing ctest
- **Code:** `.github/workflows/ci.yml`, `.github/PULL_REQUEST_TEMPLATE.md`, `tests/export/verify.sh`, `tests/export/verify.ps1`
- **API:** jobs `build-and-test`, `sanitizers`, `windows-cross`, `windows-export`, `web-build`; env `OKAY_SANITIZE`, `ASAN_OPTIONS`, `UBSAN_OPTIONS`
- **Editor:** none (`OkayEngine --selftest` is the headless editor check)
- **Docs:** `docs/qa_workflow.md`, `ROADMAP.md` ("every PR merges once its five CI jobs are green")
- **Tests:** CI-specific runs: `vs_acceptance`, `vs_debug`, `game_fiber`, `polymesh`, `mesh_asset`, `poly_ops` (the six that also run on Windows), and the 38-test sanitizer subset listed in 19.2.5 (including `net_hardening`, `net_races`, `loaders_robust`, `physics_nan`)
- **Status:** Works - the workflow file is read in full; all five jobs run on each PR
- **Open:** possible gaps: the Ubuntu job installs no libsodium-dev, so encryption tests may run their stub branch (the ASan job builds with sodium off on purpose); `net_races` needs ThreadSanitizer but there is no TSan job; only six tests ever run on real Windows; no macOS job; nothing builds `-DOKAY_WITH_PHOTON`, `-DOKAY_WITH_STEAM`, Lua or C#; PR #572 changes `tests/CMakeLists.txt`
- **Related:** 19.2, 19.7, 41.8, 18.4
- **Words:** CI, GitHub Actions, workflow, ci.yml, ASan, UBSan, sanitizers, MinGW, windows-export, WebGL, emsdk, selftest, ctest, build-and-test, red CI
- **Seen as:** CI is red on a PR; the sanitizer job reports a leak or undefined behaviour; the Windows export job fails after the cross build passed; a run is cancelled by a newer push
- **Ask me like:** "which CI job failed and why?", "add this test to the sanitizer job"

### 18.4 Release workflow
- **What:** `.github/workflows/release.yml` turns a version into a GitHub Release (and, when a secret exists, an update on Supabase) plus the Linux game server. A release PR bumps the version first.
- **Parts:**
  - 18.4.1 Trigger and version - `.github/workflows/release.yml` - runs on a `v*` tag or "Run workflow"; the version is read from `build-win-dist/VERSION.txt`, which comes from `CMakeLists.txt`; a release PR ("Release 2.849.0", #573; "Release 2.851.0", #583; "Release 2.852.0", #588 in draft) bumps `project(VERSION)`, adds a `ROADMAP.md` row and a `dist/CHANGELOG.txt` entry
  - 18.4.2 Windows job - `windows-exe` - 45 min limit; warns if the repository variable `OKAY_AC_TRUSTED_KEYS` is empty; installs MinGW, NSIS, ccache with apt retries and cache; fetches SDL2 2.30.9 and libsodium 1.0.20; runs `scripts/build-windows-dist.sh`
  - 18.4.3 Packaging - `windows-exe` - zip `OkaySpace-windows-v<ver>.zip`, installer `OkaySpace-Setup.exe` via `makensis`, workflow artifact
  - 18.4.4 GitHub Release - `windows-exe` - `softprops/action-gh-release` uploads the zip, installer and the individual files the in-app updater downloads by name (`OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `SDL2.dll`, `VERSION.txt`, symbols, `dist/CHANGELOG.txt`); tags the commit the run built
  - 18.4.5 Supabase publish step - `windows-exe` - runs `scripts/publish-supabase.sh` only when the secret `SUPABASE_SERVICE_KEY` exists (18.5)
  - 18.4.6 Linux game server job - `linux-server` - Ubuntu 22.04, libsodium on, builds `okay-player`, strips it and attaches `okay-server-linux-x64` to the release (what the editor's Server export downloads, 18.7)
- **Code:** `.github/workflows/release.yml`, `dist/CHANGELOG.txt`, `ROADMAP.md`
- **API:** tags `v<major>.<minor>.<patch>`, assets `OkaySpace-Setup.exe`, `okay-server-linux-x64`, secret `SUPABASE_SERVICE_KEY`, variable `OKAY_AC_TRUSTED_KEYS`
- **Editor:** none
- **Docs:** `docs/releasing.md`, `docs/packaging.md`
- **Tests:** none in the workflow (no ctest); `publish_supabase_checks` tests the publish script
- **Status:** Works - the workflow file is read in full; 2.851.0 is the latest release (published 2026-11-09)
- **Open:** the header comment says the manual run releases "the version in dist/VERSION.txt" but the code uses `build-win-dist/VERSION.txt` (stale comment; `dist/VERSION.txt` says 2.412.0); the release runs no tests of its own; real Steamworks is not built in; a missing `OKAY_AC_TRUSTED_KEYS` is only a warning (planned to become a failure); the 2.852.0 release (PR #588) is not cut yet
- **Related:** 18.5, 18.1, 41.8, 41.9, 39.7
- **Words:** release, release.yml, tag, v2.848.0, GitHub Release, installer, changelog, softprops, Run workflow, linux-server, CHANGELOG.txt, ROADMAP
- **Seen as:** a release has no installer or is missing an exe the updater wants; the changelog in the launcher is empty; the Linux server asset is missing
- **Ask me like:** "cut release 2.852.0", "what does a release PR change?"

### 18.5 Update feed
- **What:** What the launcher's and editor's self-updaters read: a public Supabase Storage bucket `releases` (checked first) and GitHub Releases (fallback). They use whichever has the newer version.
- **Parts:**
  - 18.5.1 Supabase publish script - `scripts/publish-supabase.sh` - uploads `v<ver>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `CHANGELOG.txt`, then `NOTES.txt`, then `VERSION.txt` last; checks each file is a Windows program under 50 MB; keeps the newest 3 versions (`KEEP`); keys from the environment only; `DRY_RUN=1`
  - 18.5.2 GitHub fallback - `.github/workflows/release.yml` - the individual files as release assets at `releases/latest/download/<name>`
  - 18.5.3 Client side - `launcher/main.cpp` (L810-1031), `editor/main.cpp` (updater near L1593) - launcher picks the newer of Supabase and GitHub; the editor's update dialog reads `CHANGELOG.txt`
  - 18.5.4 Notes - `scripts/publish-supabase.sh` - `NOTES.txt` is the launcher's "what's new" card (default: the last 6 non-merge commit subjects; `NOTES_FILE` to choose)
- **Code:** `scripts/publish-supabase.sh`, `.github/workflows/release.yml`, `launcher/main.cpp`, `editor/main.cpp`, `docs/sql/official_game_admin.sql`
- **API:** `SUPABASE_ACCESS_TOKEN` or `SUPABASE_SERVICE_KEY` (environment only), `DIST_DIR`, `NOTES_FILE`, `KEEP`, `DRY_RUN`, `SUPABASE_URL`, `OKAY_UPDATE_SERVER`
- **Editor:** Engine > Check for Updates... (editor updater dialog with the changelog)
- **Docs:** `docs/releasing.md`
- **Tests:** `publish_supabase_checks`
- **Status:** Works - script read; its early failure messages are tested; the live upload is only exercised by real releases
- **Open:** remove the hardcoded Supabase URL and key from CMake (old map); possible bug: the script looks for `$DIST_DIR/CHANGELOG.txt` but `build-windows-dist.sh` never copies one, so Supabase gets the commit-subject notes and GitHub gets `dist/CHANGELOG.txt` (the script's comment calling that file "years stale" is itself stale); `git log -6` on a shallow Actions checkout may list one commit (unverified)
- **Related:** 17.1, 17.14, 18.4, 41.6
- **Words:** update feed, updater, Supabase Storage, releases bucket, VERSION.txt, NOTES.txt, CHANGELOG.txt, publish-supabase, KEEP, DRY_RUN, auto update
- **Seen as:** the launcher doesn't see a new version; launcher shows GitHub's version instead of Supabase's; "what's new" shows commit messages
- **Ask me like:** "publish 2.852.0 to Supabase with a dry run", "why does the editor changelog differ from the launcher's?"

### 18.6 Desktop game export (Build Game) and the export check
- **What:** Turning the open project into a shippable game folder (`<Game>.exe`, `SDL2.dll`, a `<Game>_Data/` folder with the scenes, config, `build.json`, only the assets the scenes use and the project's StreamingAssets; or the older compact `Data/` layout; or one zip), with a report of what went in and what was left out, a dry run, and the CI checks that export a small game and run it.
- **Parts:**
  - 18.6.1 Build - `editor/main.cpp` (`builder::Build`) - refuses dangerous folders (a drive root, home, the project), cleans only files it writes, scenes in build order, `game.okayconfig`, player copied as `<Game>.exe`, SDL2, the dependency walk's assets, StreamingAssets, icon, splash, localization, `build.json`, the report, the archive; `Data/manifest.json` only with anti-cheat
  - 18.6.2 Player lookup and download - `editor/main.cpp` (`builder::FindPlayer`) - looks beside the editor and in a few sibling folders, downloads the runtime from the latest release if missing
  - 18.6.3 Command line - `editor/main.cpp` (`RunBuildGameCli`) - `OkayEngine --build-game SCENE OUT_DIR [--name GAME] [--layout folder|compact|zip] [--dry-run] [--report FILE] [--no-strip] [--no-compress] [--zip] [--version V] [--build-number N] [--archive-name PATTERN] [--dev]`, starting from the project's saved Build Settings
  - 18.6.4 Optional data obfuscation - `Core/DataPack` - scenes and config packed so they can't be read in a text editor (39.10)
  - 18.6.5 Export check - `tests/export/verify.sh`, `tests/export/verify.ps1` - exports `vs_export.okayscene` with the editor's real Build, runs it headless and requires the output to equal `expected.txt`; assets `tests/export/*.okayvs`, `Tower.okaymesh`
  - 18.6.6 Build pipeline logic - `engine/include/okay/Core/BuildPipeline.hpp`, `engine/src/Core/BuildPipeline.cpp` - layout planner, the player's `FindDataDir`, the dependency walk (asset stripping), `build.json`, the report, the zip writer and reader
  - 18.6.7 Layout check - `tests/export/verify_layout.sh` - exports a project with an unused asset and StreamingAssets, checks the Folder, flat (old), Compact, no-strip, Zip and dry-run outputs, and runs the player from the Folder, flat and Compact layouts
  - 18.6.8 Build report - `editor/main.cpp` (Build Result window), `.okay/build_report.txt` - by kind, by type, files with why, left out with reason, warnings, time, archive size
  - 18.6.9 Player build info - `player/main.cpp` - `--build-info`, the development overlay and log line, `Paths::StreamingAssetsDir`
- **Code:** `editor/main.cpp`, `editor/ProjectState.hpp`, `engine/src/Core/BuildPipeline.cpp`, `player/main.cpp`, `Core/DataPack`, `tests/export/verify.sh`, `tests/export/verify.ps1`, `tests/export/verify_layout.sh`
- **API:** `builder::Build`, `okay::buildpipe::*`, `Paths::StreamingAssetsDir`, `OkayEngine --build-game`, `<Game> --build-info`, `OKAY_AC_SIGNING_KEY_FILE`
- **Editor:** File > Build Settings or Engine > Build Settings (Ctrl+B): scenes in build, window, platform, Output page (layout, build number, stripping, compression, archive name), Dry Run, anti-cheat, build profiles
- **Docs:** `docs/build_pipeline.md`, `docs/making_a_game.md`, `docs/editor.md`, `docs/packaging.md`
- **Tests:** `build_pipeline` (ctest; layout, data-folder lookup, dependency walk, manifest, report, zip, settings text); the layout check and the export check run in CI (18.3)
- **Status:** Works - the layout check exports and runs real builds; no pack file, no `Plugins/`, no separate symbol files (docs/build_pipeline.md, known gaps)
- **Open:** the build code lives inside the huge `editor/main.cpp` (split, see 15.10); the export check covers visual scripts and one mesh, not UI, audio or networking; anti-cheat forces the compact layout; Web, Android and server exports ignore the new Output settings
- **Related:** 15.7, 18.3, 39.4, 42.6
- **Words:** Build Game, export, Build Settings, Data folder, _Data, Folder layout, Compact layout, Zip archive, build.json, build number, StreamingAssets, asset stripping, dependency walk, dry run, build report, game.okayconfig, player runtime, scenes in build, --build-game, --build-info, verify.sh, manifest
- **Seen as:** the exported game is missing a texture or sound; the build refuses the folder; game starts but a scene doesn't load; the exe has the wrong name; a file you did not expect is (or is not) in the build
- **Ask me like:** "build the game from the command line", "why isn't my audio file in the build?", "what is in the build and how big is it?"

### 18.7 Dedicated server export and Linux server build
- **What:** The editor's "Server" platform writes a ready-to-run server folder for Linux or Windows; the release workflow builds and attaches the Linux runtime it needs.
- **Parts:**
  - 18.7.1 Server export - `editor/main.cpp` (`builder::BuildServer`, near L4007) - game data, runtime, `server.cfg`, start scripts, `deploy.sh` and `deploy.bat` (copy over SSH and install), `install-server.sh` (systemd, firewall), `Dockerfile`, `docker-compose.yml`, README
  - 18.7.2 Linux runtime asset - `.github/workflows/release.yml` (`linux-server`) - `okay-server-linux-x64`, built on Ubuntu 22.04 for glibc 2.35 or newer; the export downloads it
  - 18.7.3 Server settings page - `editor/main.cpp` (Build Settings, Server page, near L16170) - name, port, players, password, listing and the anti-cheat basics
- **Code:** `editor/main.cpp`, `.github/workflows/release.yml`
- **API:** `server.cfg`, `deploy.sh user@host [ssh-port]`, `okay-server-linux-x64`
- **Editor:** File > Build Settings > Platform: Server
- **Docs:** `docs/dedicated-server.md`
- **Tests:** `dedicated_server` covers the runtime, not the export
- **Status:** Works - export code read; no automated test of the export itself
- **Open:** no test of `BuildServer`; no Windows server runtime job
- **Related:** 16.5, 18.4, 41.5
- **Words:** server export, dedicated server, Platform: Server, deploy.sh, install-server.sh, Docker, systemd, okay-server-linux-x64, server.cfg
- **Seen as:** "couldn't download the Linux runtime"; deploy.sh can't reach the VPS; the server folder has no Docker files
- **Ask me like:** "export a Linux server for my game", "deploy it to my VPS"

### 18.8 Steam release (SteamPipe)
- **What:** Publishing the whole OkaySpace application (launcher, editor, player, relay) to Steam so installed copies update themselves. Manual: upload a build, then set it live on the Steamworks dashboard.
- **Parts:**
  - 18.8.1 Build scripts - `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf` - app 1172560, depot 1172561, content root `../build-win-dist/`, `setlive` empty (upload only), `*.pdb` and `*.log` excluded
  - 18.8.2 Upload wrappers - `steam/upload.sh`, `steam/upload.bat` - run `steamcmd +login $STEAM_USER +run_app_build`; build the dist first if it is missing
  - 18.8.3 Steam app id and SDK - `scripts/build-windows-dist.sh` - bakes the app id (`OKAY_STEAM_APP_ID`); with `STEAMWORKS_SDK_PATH` links real Steamworks and ships `steam_api64.dll`
- **Code:** `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf`, `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`, `scripts/build-windows-dist.sh`
- **API:** `STEAM_USER=<builder> steam/upload.sh`, `"setlive" "default"`, `"preview" "1"`
- **Editor:** none
- **Docs:** `docs/steam_release.md`, `steam/README.md`
- **Tests:** none
- **Status:** Basic - scripts and docs exist; no CI job, nothing automated
- **Open:** needs a Steamworks builder account and Valve's one-time review before the first public release; the GitHub release does not build the real Steamworks backend (17.10)
- **Related:** 17.10, 18.1, 41.7
- **Words:** Steam, SteamPipe, steamcmd, depot, app build, setlive, Steamworks, upload.sh, 1172560, auto-update
- **Seen as:** Steam players don't get an update (not set live); steamcmd asks for Steam Guard
- **Ask me like:** "upload a new build to Steam and leave it on the beta branch"
