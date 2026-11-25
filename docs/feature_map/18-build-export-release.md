# 18. Build, export & release

How OkaySpace is built, shipped to players and kept up to date: the cross-compiled Windows distribution and its installer,
web and mobile export, game export from the editor, the dedicated-server export, the five CI jobs, the release workflow, the
Supabase and GitHub update feed, and the Steam upload kit. Files: `scripts/`, `.github/workflows/` (read-only here), `steam/`,
`cmake/`, and the export code in `editor/BuildGame.cpp` (namespace `builder`, moved out of `editor/main.cpp`, 15.10). The CMake and tool plumbing itself is mapped in 41.
Release 2.851.0 is live (tag `v2.851.0`, 2026-11-09); the 2.852.0 release PR (#588, draft, branch `release-2852`, touches only `CMakeLists.txt`, `ROADMAP.md` and `dist/CHANGELOG.txt`) is being cut and will hold #585, #586 and #587. The automated-testing runner of #572 (`okay-playtest`) is merged and runs as ctests.

## 18. Build, export & release

### 18.1 Windows build and installer
- **System:** Windows Build and Installer - Builds the Windows programs (launcher, editor, player, relay) and the installer and zip.
  Home: `scripts/build-windows-dist.sh`, `scripts/okayspace-installer.nsi`. Entry: `scripts/build-windows-dist.sh`. Also called: installer, NSIS, Windows zip, build-windows-dist.
- **What:** Cross-compiles the whole Windows distribution (launcher, editor, player, relay and `SDL2.dll`) from Linux with MinGW-w64 and wraps it in a one-click installer that installs or updates in place.
- **Parts:**
  - 18.1.1 Distribution build script - `scripts/build-windows-dist.sh` - configures with the MinGW toolchain, builds launcher, editor, player (our own model readers only; Assimp was deleted 2026-11-24), copies the layout into `build-win-dist/`; env `SDL2_MINGW_PREFIX` (required), `OKAY_SODIUM_MINGW_PREFIX`, `OKAY_AC_TRUSTED_KEYS`, `OKAY_STEAM_APP_ID` (default 1172560), `STEAMWORKS_SDK_PATH`, `OKAY_IMGUI_SRC`, `JOBS`
  - 18.1.2 Distribution layout - `scripts/build-windows-dist.sh` - `OkaySpace.exe` and `SDL2.dll` at the top; `Tools/` holds `OkayEngine.exe`, `OkaySpacePlayer.exe`, `okayspace-relay.exe`, a second `SDL2.dll`, `VERSION.txt`, `accounts.md`; starter `Assets/Textures` and `Assets/Fonts`; `README.txt`; `VERSION.txt` is read from `CMakeLists.txt`
  - 18.1.3 Split debug info - `scripts/build-windows-dist.sh` - the editor's DWARF moves to `build-win-symbols/OkayEngine.debug` so the exe fits Supabase's 50 MB limit; a debuglink stays in the exe
  - 18.1.4 NSIS installer - `scripts/okayspace-installer.nsi` - per-user (`$LOCALAPPDATA\OkaySpace`, no admin), finds a prior install (registry, Apps entry, the folder it runs from) and updates it in place, waits for running exes to close, makes Start Menu and desktop shortcuts, an uninstaller and an Apps & features entry; user data is never touched
  - 18.1.5 Quick sandbox exe - `scripts/build-windows-exe.sh` - an older helper that builds only the sandbox demo and copies it to `dist/OkaySpace.exe`
  - 18.1.6 Toolchain and exe metadata - `cmake/mingw-w64-x86_64.toolchain.cmake`, `cmake/OkayWinVersion.cmake` - see 41.2
- **Code:** `scripts/build-windows-dist.sh`, `scripts/okayspace-installer.nsi`, `scripts/build-windows-exe.sh`, `cmake/mingw-w64-x86_64.toolchain.cmake`, `CMakeLists.txt`
- **API:** `scripts/build-windows-dist.sh`, `makensis -DVERSION= -DDISTDIR= -DOUTFILE= scripts/okayspace-installer.nsi`, `OkaySpace-Setup.exe`
- **Editor:** none (File > Build Settings builds a game, not the engine: 18.6)
- **Docs:** `docs/release/packaging.md`
- **Tests:** none as ctest; the `windows-cross` CI job builds it on every PR (18.3) and the release workflow builds the installer (18.4)
- **Status:** Works - built by CI on every PR and by the release workflow on every release
- **Open:** "Stop committing dist binaries" (old map): `dist/` still holds June 2026 exes and a `VERSION.txt` of 2.412.0; `build-windows-exe.sh` copies the sandbox demo as `dist/OkaySpace.exe`, which is not the launcher (possible stale script); the Windows build is never executed in CI beyond six test exes (18.3.4)
- **Related:** 18.4, 18.5, 41.2, 41.3, 19.6
- **Words:** Windows build, MinGW, cross-compile, build-win-dist, distribution, dist, installer, NSIS, OkaySpace-Setup.exe, SDL2.dll, Tools folder, symbols, OkayEngine.debug
- **Seen as:** the installer asks to close OkaySpace; an exe is missing SDL2.dll; the editor exe is huge; "no MinGW" or "SDL2_MINGW_PREFIX not set" errors
- **Ask me like:** "build the Windows folder locally", "why does the installer skip the folder question?"

### 18.2 Web and mobile export
- **System:** Web, Mobile and Console Export - Runs games in a web browser, on phones, and (experimental, off by default) on an Xbox in Developer Mode.
  Home: `platform/`. Entry: `scripts/build-web.sh`. Also called: WebGL, Emscripten, Android, iOS, Xbox, UWP, okay_web.js, web shell, okaypack.
- **What:** Putting a game in a browser (WebAssembly through Emscripten) or on a phone. Web has a build script, our own page and loader, browser saves, a packager with an itch.io zip, Run in browser in the editor, and a CI job that plays a test game in headless Chromium; Android and iOS are a data export plus instructions.
- **Parts:**
  - 18.2.1 Web build script - `scripts/build-web.sh` - `emcmake` builds the player without libsodium (none for WebAssembly) into `build-web/web/okay-player.html`, `.js`, `.wasm`; `--game DIR` (or one scene file) then packs a game beside it into `build-web/site/` (`--out`), `--zip` writes an itch.io zip, `--fit fill|aspect`, `--no-compress`, `--debug`
  - 18.2.2 Emscripten player target - `player/CMakeLists.txt` - the `EMSCRIPTEN` branch: `-sUSE_SDL=2`, memory growth, `-sENVIRONMENT=web`, IDBFS, a 1 MB stack, our shell and pre-js; release links with `-Oz` and Closure (`OKAY_WEB_CLOSURE`); the game is no longer baked in, so one player build serves every game (see 42.6)
  - 18.2.3 Editor export for Web and Android - `editor/BuildGame.cpp` (`builder::ExportForPlatform`) - writes `game/` (Web) or `assets/` (Android) with the scene, config and referenced assets; for Web a `build-web.sh` that calls the engine's script into `web/` and `<Name>-web.zip`
  - 18.2.4 Mobile guides - `docs/platform/web_mobile.md` - Android from SDL2's project template, iOS from the Xcode template (the web part of that page predates 18.2.5 to 18.2.9)
  - 18.2.5 Web page and loader - `platform/web/shell.html`, `platform/web/okay_web.js` - the loading screen (the player's own cube and colours, a progress bar from the real byte counts, errors in plain words: file://, a missing file, no WebAssembly, no WebGL, an old browser, a lost WebGL context, a crash), the program and the game pack downloaded at once and unpacked with the browser's DecompressionStream, "Click to play" until the first gesture, a full-screen button, the canvas filling the page or a fixed aspect, high-DPI through SDL's pixel ratio
  - 18.2.6 Browser saves - `platform/web/okay_web.js`, `platform/web/WebPlayer.hpp`, `player/main.cpp` - an IndexedDB folder per game (`/okaysave/<company_title>/`, Emscripten's IDBFS) read before `main()`; every closed, renamed or deleted file there is stored 250 ms later and when the page is hidden; the player saves into it (`OKAY_WEB_SAVE_DIR`) and writes the prefs 0.5 s after the last change (`PendingWrite`, `okay_web_flush`)
  - 18.2.7 Browser input and audio - `platform/web/okay_web.js` - no context menu on the canvas, `touch-action: none`, focus on click, every AudioContext resumed on the first click, key or touch; keyboard, mouse, pointer lock (SDL re-asks on a click), touch and the Gamepad API come through SDL's Emscripten port into the engine's input; the page cannot scroll
  - 18.2.8 Web packager - `scripts/web_package.py` - our pack format (`OKWPACK1`, a JSON manifest, the files), gzip of the program and the pack with fixed time stamps, `index.html` with the game's config, size report, itch.io zip (index.html at its root, the .gz files stored)
  - 18.2.9 Run in browser - `editor/WebRun.cpp`, `editor/LaunchFile.cpp` (`OpenLocalUrl`) - Build Settings > Web shows the web build's file sizes and serves `web/` on 127.0.0.1 (GET and HEAD, no listing, nothing outside the folder, polled once a frame) and opens the default browser
- **Code:** `scripts/build-web.sh`, `scripts/web_package.py`, `platform/web/`, `editor/BuildGame.cpp`, `editor/WebRun.cpp`, `player/CMakeLists.txt`, `player/main.cpp`
- **API:** `scripts/build-web.sh [--game DIR] [--out DIR] [--zip FILE] [--fit fill|aspect]`, `scripts/web_package.py`, Build Settings platform Web or Android, `OKAY_WEB_SAVE_DIR`, `Module.okayWeb` (stats, saveSyncs, flushSaves) in the page
- **Editor:** File > Build Settings > Platform: Web (WebGL / WASM): web build sizes, Run in browser, Stop; Android (project)
- **Docs:** `docs/platform/web_mobile.md`
- **Tests:** `web_export` (sizes, request parsing, path rules, a loopback GET of 3 MB, an idle connection, the save folder and prefs timing), `web_package_script`, `launch_file` (OpenLocalUrl); the `web-build` CI job (18.3.5) packs `tests/web/webtest_game` and runs `tests/scripts/test_web_player.js` in headless Chromium (first frame, key, right-click, gamepad, save and prefs after a reload, no scroll in an iframe, audio after the first click)
- **Status:** Partial - web builds, loads, saves and takes input in headless Chromium; Android and iOS are instructions only (the export writes `assets/` and a README, not a Gradle project)
- **Open:** no networking in the browser (UDP; a WebSocket transport through the relay is the plan, not built); the 3D view is the software renderer (no WebGL 3D path); asset packs are not lazy-loaded (the whole pack comes before the game starts); the editor cannot run Emscripten itself (build-web.sh is run by hand); Run in browser and the Web page were not opened in a real editor window; no real phone, touch screen or gamepad tried; the Build Settings tooltip says "Export an Android Studio project" but the code writes assets and a README only (possible mismatch); the web build excludes the relay and OkayUI's Direct3D backend
- **Related:** 18.3, 15.7, 42.6
- **Words:** web, WebGL, WASM, WebAssembly, Emscripten, emsdk, Android, iOS, mobile, APK, touch, okay-player.html, index.html, itch.io, IndexedDB, IDBFS, loading screen, Run in browser, okaypack
- **Seen as:** the web page is blank or the game doesn't load from a file:// URL; saves gone after a reload; the page scrolls when pressing space; right-click opens the browser menu; no sound until a click; Android export gives no APK; a feature missing on the web build (networking relay, encryption)
- **Ask me like:** "build the web version and serve it", "make an itch.io zip of my game", "how do I get this on Android?"

### 18.3 CI jobs
- **System:** CI - The automatic checks GitHub runs on every change (build, tests, linters).
  Home: `.github/workflows/`. Entry: `.github/workflows/ci.yml`. Also called: GitHub Actions, workflows, checks.
- **What:** The five GitHub Actions jobs in `.github/workflows/ci.yml` that every pull request must pass (and every push to main runs). A new push to a PR cancels that PR's older run.
- **Parts:**
  - 18.3.1 Build & test (Ubuntu) - `.github/workflows/ci.yml`, `cmake/OkayTestShard.cmake`, `tests/scripts/check_test_shards.py` - three shards side by side (`-DOKAY_TEST_SHARD=K/3`, each builds and runs a third of the tests) and a gate job of that name that needs all three; installs cmake, g++, ninja, libsdl2-dev; Release build and the whole `ctest`; also builds the player, builds the editor and launcher and runs `OkayEngine --selftest` headless, then exports a game and runs `tests/export/verify.sh`
  - 18.3.2 Scripting and robustness tests (ASan + UBSan) - `.github/workflows/ci.yml` - Debug `-g1` with `address,undefined,float-cast-overflow`, libsodium off, 45 min limit; builds and runs 38 chosen test binaries directly, not through ctest (19.2.5)
  - 18.3.3 Cross-compile Windows build (MinGW) - `.github/workflows/ci.yml` - apt packages cached, SDL2 2.30.9 and libsodium 1.0.20 fetched, ccache; runs `scripts/build-windows-dist.sh`, builds six test exes, uploads artifact `windows-ci-build` (3 days)
  - 18.3.4 Windows export check - `.github/workflows/ci.yml` - on `windows-latest`: runs `test_vs_acceptance`, `test_vs_debug`, `test_game_fiber`, `test_polymesh`, `test_mesh_asset`, `test_poly_ops`, then `tests/export/verify.ps1` with the cross-built editor
  - 18.3.5 WebGL build (Emscripten) - `.github/workflows/ci.yml` - emsdk 3.1.51, `bash scripts/build-web.sh`, checks the `.html`, `.js` and `.wasm` exist, packs `tests/web/webtest_game` and plays it in headless Chromium (`tests/scripts/test_web_player.js`, Playwright as a test tool), uploads `okayspace-webgl`
  - 18.3.6 PR template - `.github/PULL_REQUEST_TEMPLATE.md` - summary, changes, and two ticks: warning-clean build and passing ctest
- **Code:** `.github/workflows/ci.yml`, `.github/PULL_REQUEST_TEMPLATE.md`, `tests/export/verify.sh`, `tests/export/verify.ps1`
- **API:** jobs `build-and-test`, `sanitizers`, `windows-cross`, `windows-export`, `web-build`; env `OKAY_SANITIZE`, `ASAN_OPTIONS`, `UBSAN_OPTIONS`
- **Editor:** none (`OkayEngine --selftest` is the headless editor check)
- **Docs:** `docs/process/qa_workflow.md`, `ROADMAP.md`, `docs/process/automated_testing.md`
- **Tests:** CI-specific runs: `vs_acceptance`, `vs_debug`, `game_fiber`, `polymesh`, `mesh_asset`, `poly_ops` (the six that also run on Windows), and the 38-test sanitizer subset listed in 19.2.5 (including `net_hardening`, `net_races`, `loaders_robust`, `physics_nan`)
- **Status:** Works - rechecked 2026-11-24: `ci.yml` now has about 20 jobs (also `player`, `editor`, `export-check`, `assimp-models`, `own-http`, `own-stack`, `windows-restart`, `msvc-build`, `sql-tests` and others); since 2026-11-23 a pull request runs only the Windows cross-compile here (plus branch-lint and the PR-body check), the author runs `scripts/precheck.sh --build <dir> --tests` before the push, and the whole suite runs on every push to main and nightly (`CLAUDE.md` rules 6 and 7a); #1219 made the MinGW install reliable and cached (`scripts/ci-install-mingw.sh`) and let ccache reuse `editor/main.cpp`; #1222 did the same for `release.yml` and moved the exact build stamp into `editor/BuildStamp.cpp`
- **Open:** possible gaps: the Ubuntu job installs no libsodium-dev, so encryption tests may run their stub branch (the ASan job builds with sodium off on purpose); `net_races` needs ThreadSanitizer but there is no TSan job; only six tests ever run on real Windows; no macOS job; nothing builds `-DOKAY_WITH_PHOTON`, `-DOKAY_WITH_STEAM`, Lua or C#; PR #572 changes `tests/CMakeLists.txt`
- **Related:** 19.2, 19.7, 41.8, 18.4
- **Words:** CI, GitHub Actions, workflow, ci.yml, ASan, UBSan, sanitizers, MinGW, windows-export, WebGL, emsdk, selftest, ctest, build-and-test, red CI
- **Seen as:** CI is red on a PR; the sanitizer job reports a leak or undefined behaviour; the Windows export job fails after the cross build passed; a run is cancelled by a newer push
- **Ask me like:** "which CI job failed and why?", "add this test to the sanitizer job"

### 18.4 Release workflow
- **System:** Release Pipeline - Turns a version number into a published release, and publishes test builds.
  Home: `.github/workflows/release.yml`. Entry: `.github/workflows/release.yml`. Also called: release.yml, releases, publish, CHANGELOG.
- **What:** `.github/workflows/release.yml` turns a version into a GitHub Release (and, when a secret exists, an update on Supabase) plus the Linux game server. A release PR bumps the version first.
- **Parts:**
  - 18.4.1 Trigger and version - `.github/workflows/release.yml` - runs on a `v*` tag or "Run workflow"; the version is read from `build-win-dist/VERSION.txt`, which comes from `CMakeLists.txt`; a release PR ("Release 2.849.0", #573; "Release 2.851.0", #583; "Release 2.852.0", #588 in draft) bumps `project(VERSION)`, adds a `ROADMAP.md` row and a `dist/CHANGELOG.txt` entry
  - 18.4.2 Windows job - `windows-exe` - 45 min limit; warns if the repository variable `OKAY_AC_TRUSTED_KEYS` is empty; installs MinGW, NSIS, ccache with apt retries and cache; fetches SDL2 2.30.9 and libsodium 1.0.20; runs `scripts/build-windows-dist.sh`
  - 18.4.3 Packaging - `windows-exe` - zip `OkaySpace-windows-v<ver>.zip`, installer `OkaySpace-Setup.exe` via `makensis`, workflow artifact
  - 18.4.4 GitHub Release - `windows-exe` - `softprops/action-gh-release` uploads the zip, installer and the individual files the in-app updater downloads by name (`OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `SDL2.dll`, `VERSION.txt`, symbols, `dist/CHANGELOG.txt`); tags the commit the run built
  - 18.4.5 Supabase publish step - `windows-exe` - runs `scripts/publish-supabase.sh` only when the secret `SUPABASE_SERVICE_KEY` exists (18.5)
  - 18.4.6 Linux game server job - `linux-server` - Ubuntu 22.04, libsodium on, builds `okay-player`, strips it and attaches `okay-server-linux-x64` to the release (what the editor's Server export downloads, 18.7)
  - 18.4.7 Fast release (one dispatch, off until the owner sets `FAST_RELEASE_ENABLED=1`) - `.github/workflows/fast-release.yml`, `scripts/fast_release.py` - plans the next version from what is published (rule 12), refuses a commit without a fully green CI run on main (rule 6), assembles the notes from `dist/changelog.d/<id>.txt`, calls `release.yml` with the version stamped into the runner's `CMakeLists.txt`, then checks the release, tag, latest and the three SHA-256 in `versions.json`; `dry_run=true` only prints the plan; built and tested offline, never run on GitHub (`docs/release/releasing.md`)
- **Code:** `.github/workflows/release.yml`, `dist/CHANGELOG.txt`, `ROADMAP.md`
- **API:** tags `v<major>.<minor>.<patch>`, assets `OkaySpace-Setup.exe`, `okay-server-linux-x64`, secret `SUPABASE_SERVICE_KEY`, variable `OKAY_AC_TRUSTED_KEYS`
- **Editor:** none
- **Docs:** `docs/release/releasing.md`, `docs/release/packaging.md`
- **Tests:** none in the workflow (no ctest); `publish_supabase_checks` tests the publish script
- **Status:** Works - the workflow file is read in full; 2.851.0 is the latest release (published 2026-11-09)
- **Open:** the header comment says the manual run releases "the version in dist/VERSION.txt" but the code uses `build-win-dist/VERSION.txt` (stale comment; `dist/VERSION.txt` says 2.412.0); the release runs no tests of its own; real Steamworks is not built in; a missing `OKAY_AC_TRUSTED_KEYS` is only a warning (planned to become a failure); the 2.852.0 release (PR #588) is not cut yet
- **Related:** 18.5, 18.1, 41.8, 41.9, 39.7
- **Words:** release, release.yml, tag, v2.848.0, GitHub Release, installer, changelog, softprops, Run workflow, linux-server, CHANGELOG.txt, ROADMAP
- **Seen as:** a release has no installer or is missing an exe the updater wants; the changelog in the launcher is empty; the Linux server asset is missing
- **Ask me like:** "cut release 2.852.0", "what does a release PR change?"

### 18.5 Update feed
- **System:** Launcher Updater
- **What:** What the launcher's and editor's self-updaters read: a public Supabase Storage bucket `releases` (checked first) and GitHub Releases (fallback). They use whichever has the newer version.
- **Parts:**
  - 18.5.1 Supabase publish script - `scripts/publish-supabase.sh` - uploads `v<ver>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `CHANGELOG.txt`, then `NOTES.txt`, then `VERSION.txt` last; checks each file is a Windows program under 50 MB; keeps the newest 3 versions (`KEEP`); keys from the environment only; `DRY_RUN=1`
  - 18.5.2 GitHub fallback - `.github/workflows/release.yml` - the individual files as release assets at `releases/latest/download/<name>`
  - 18.5.3 Client side - `launcher/main.cpp` (L810-1031), `editor/Updater.cpp` (`namespace updater`) - launcher picks the newer of Supabase and GitHub; the editor's update dialog reads `CHANGELOG.txt`
  - 18.5.4 Notes - `scripts/publish-supabase.sh` - `NOTES.txt` is the launcher's "what's new" card (default: the last 6 non-merge commit subjects; `NOTES_FILE` to choose)
- **Code:** `scripts/publish-supabase.sh`, `.github/workflows/release.yml`, `launcher/main.cpp`, `editor/main.cpp`, `docs/sql/official_game_admin.sql`
- **API:** `SUPABASE_ACCESS_TOKEN` or `SUPABASE_SERVICE_KEY` (environment only), `DIST_DIR`, `NOTES_FILE`, `KEEP`, `DRY_RUN`, `SUPABASE_URL`, `OKAY_UPDATE_SERVER`
- **Editor:** Help > Check for Updates... (editor updater dialog with the changelog)
- **Docs:** `docs/release/releasing.md`
- **Tests:** `publish_supabase_checks`
- **Status:** Works - script read; its early failure messages are tested; the live upload is only exercised by real releases
- **Open:** remove the hardcoded Supabase URL and key from CMake (old map); possible bug: the script looks for `$DIST_DIR/CHANGELOG.txt` but `build-windows-dist.sh` never copies one, so Supabase gets the commit-subject notes and GitHub gets `dist/CHANGELOG.txt` (the script's comment calling that file "years stale" is itself stale); `git log -6` on a shallow Actions checkout may list one commit (unverified)
- **Related:** 17.1, 17.14, 18.4, 41.6
- **Words:** update feed, updater, Supabase Storage, releases bucket, VERSION.txt, NOTES.txt, CHANGELOG.txt, publish-supabase, KEEP, DRY_RUN, auto update
- **Seen as:** the launcher doesn't see a new version; launcher shows GitHub's version instead of Supabase's; "what's new" shows commit messages
- **Ask me like:** "publish 2.852.0 to Supabase with a dry run", "why does the editor changelog differ from the launcher's?"

### 18.6 Desktop game export (Build Game) and the export check
- **System:** Game Export - Turns a project into a game folder you can ship (`<Game>.exe`), checks it, and lets shipped games download signed content updates.
  Home: `editor/BuildGame.cpp`, `engine/include/okay/Core/BuildPipeline.hpp`. Entry: `editor/BuildGame.hpp`. Also called: Build Game, BuildPipeline, export game, content updates.
- **What:** Turning the open project into a shippable game folder (`<Game>.exe`, `SDL2.dll`, a `<Game>_Data/` folder with the scenes, config, `build.json`, only the assets the scenes use and the project's StreamingAssets; or the older compact `Data/` layout; or one zip), with a report of what went in and what was left out, a dry run, and the CI checks that export a small game and run it.
- **Parts:**
  - 18.6.1 Build - `editor/BuildGame.cpp` (`builder::Build`; types and declarations in `editor/BuildGame.hpp`) - refuses dangerous folders (a drive root, home, the project), cleans only files it writes, scenes in build order, `game.okayconfig`, player copied as `<Game>.exe`, SDL2, the dependency walk's assets, StreamingAssets, icon, splash, localization, `build.json`, the report, the archive; `Data/manifest.json` only with anti-cheat
  - 18.6.2 Player lookup and download - `editor/BuildGame.cpp` (`builder::FindPlayer`, `builder::EnsurePlayer`) - looks beside the editor and in a few sibling folders, downloads the runtime from the latest release if missing
  - 18.6.3 Command line - `editor/main.cpp` (`RunBuildGameCli`) - `OkayEngine --build-game SCENE OUT_DIR [--name GAME] [--layout folder|compact|zip] [--dry-run] [--report FILE] [--no-strip] [--no-compress] [--zip] [--version V] [--build-number N] [--archive-name PATTERN] [--dev]`, starting from the project's saved Build Settings
  - 18.6.4 Optional data obfuscation - `Core/DataPack` - scenes and config packed so they can't be read in a text editor (39.10)
  - 18.6.5 Export check - `tests/export/verify.sh`, `tests/export/verify.ps1` - exports `vs_export.okayscene` with the editor's real Build, runs it headless and requires the output to equal `expected.txt`; assets `tests/export/*.okayvs`, `Tower.okaymesh`
  - 18.6.6 Build pipeline logic - `engine/include/okay/Core/BuildPipeline.hpp`, `engine/src/Core/BuildPipeline.cpp` - layout planner, the player's `FindDataDir`, the dependency walk (asset stripping), `build.json`, the report, the zip writer and reader (one writer, `WriteZipEntries`, for files in memory or on disk in a fixed order; `WriteZip` packs a folder through it with the same bytes as before, test `build_pipeline`; #422 finding 11: the Xbox package writer in `platform/xbox_uwp` keeps its own for now, that lane is not ours)
  - 18.6.7 Layout check - `tests/export/verify_layout.sh` - exports a project with an unused asset and StreamingAssets, checks the Folder, flat (old), Compact, no-strip, Zip and dry-run outputs, and runs the player from the Folder, flat and Compact layouts
  - 18.6.8 Build report - `editor/main.cpp` (Build Result window), `.okay/build_report.txt` - by kind, by type, files with why, left out with reason, warnings, time, archive size
  - 18.6.9 Player build info - `player/main.cpp` - `--build-info`, the development overlay and log line, `Paths::StreamingAssetsDir`
  - 18.6.10 Build Settings window upgrade - `editor/BuildSettingsModel.hpp` (logic, no ImGui), `editor/BuildSettingsWindow.cpp` (drawing), `engine/include/okay/Core/BuildConfigExtra.hpp` (the extra `game.okayconfig` keys the player reads) - search box over every setting, changed-from-default dots, Reset page to defaults, Game page (game speed, physics steps, start volumes, starting language, log level and file, description, environment variables), Advanced page (pre and post build commands behind an Allow switch, `.gitignore`, build history), a "What will happen" summary, checks with Fix buttons, Debug and Release starts, duplicate profile, `.okaybuild` files; see `docs/editor/build_settings_upgrade.md`
- **Code:** `editor/BuildGame.cpp`, `editor/BuildGame.hpp`, `editor/main.cpp`, `editor/ProjectState.hpp`, `editor/BuildSettingsModel.hpp`, `editor/BuildSettingsWindow.cpp`, `engine/include/okay/Core/BuildConfigExtra.hpp`, `tests/test_build_settings.cpp`, `engine/src/Core/BuildPipeline.cpp`, `player/main.cpp`, `Core/DataPack`, `tests/export/verify.sh`, `tests/export/verify.ps1`, `tests/export/verify_layout.sh`
- **API:** `builder::Build`, `okay::buildpipe::*`, `Paths::StreamingAssetsDir`, `OkayEngine --build-game`, `<Game> --build-info`, `OKAY_AC_SIGNING_KEY_FILE`
- **Editor:** File > Build Settings (Ctrl+B): scenes in build, window, platform, Output page (layout, build number, stripping, compression, archive name), Dry Run, anti-cheat, build profiles
- **Docs:** `docs/release/build_pipeline.md`, `docs/gameplay/making_a_game.md`, `docs/editor/editor.md`, `docs/release/packaging.md`
- **Tests:** `build_pipeline` (ctest; layout, data-folder lookup, dependency walk, manifest, report, zip, settings text); the layout check and the export check run in CI (18.3)
- **Status:** Works - the layout check exports and runs real builds; no pack file, no `Plugins/`, no separate symbol files (docs/release/build_pipeline.md, known gaps)
- **Open:** the build code now lives in `editor/BuildGame.cpp` (move 1 of the split, 15.10, `docs/engine/modularization_plan.md`); the Build Result window and the build dialogs are still in `editor/main.cpp`; the export check covers visual scripts and one mesh, not UI, audio or networking; anti-cheat forces the compact layout; Web, Android and server exports ignore the new Output settings
- **Related:** 15.7, 18.3, 39.4, 42.6
- **Words:** Build Game, export, Build Settings, Data folder, _Data, Folder layout, Compact layout, Zip archive, build.json, build number, StreamingAssets, asset stripping, dependency walk, dry run, build report, game.okayconfig, player runtime, scenes in build, --build-game, --build-info, verify.sh, manifest
- **Seen as:** the exported game is missing a texture or sound; the build refuses the folder; game starts but a scene doesn't load; the exe has the wrong name; a file you did not expect is (or is not) in the build
- **Ask me like:** "build the game from the command line", "why isn't my audio file in the build?", "what is in the build and how big is it?"

### 18.7 Dedicated server export and Linux server build
- **System:** Dedicated Server
- **What:** The editor's "Server" platform writes a ready-to-run server folder for Linux or Windows; the release workflow builds and attaches the Linux runtime it needs.
- **Parts:**
  - 18.7.1 Server export - `editor/BuildGame.cpp` (`builder::BuildServer`) - game data, runtime, `server.cfg`, start scripts, `deploy.sh` and `deploy.bat` (copy over SSH and install), `install-server.sh` (systemd, firewall), `Dockerfile`, `docker-compose.yml`, README
  - 18.7.2 Linux runtime asset - `.github/workflows/release.yml` (`linux-server`) - `okay-server-linux-x64`, built on Ubuntu 22.04 for glibc 2.35 or newer; the export downloads it
  - 18.7.3 Server settings page - `editor/main.cpp` (Build Settings, Server page, near L16170) - name, port, players, password, listing and the anti-cheat basics
- **Code:** `editor/BuildGame.cpp`, `editor/main.cpp`, `.github/workflows/release.yml`
- **API:** `server.cfg`, `deploy.sh user@host [ssh-port]`, `okay-server-linux-x64`
- **Editor:** File > Build Settings > Platform: Server
- **Docs:** `docs/online/dedicated-server.md`
- **Tests:** `dedicated_server` covers the runtime, not the export
- **Status:** Works - export code read; no automated test of the export itself
- **Open:** no test of `BuildServer`; no Windows server runtime job
- **Related:** 16.5, 18.4, 41.5
- **Words:** server export, dedicated server, Platform: Server, deploy.sh, install-server.sh, Docker, systemd, okay-server-linux-x64, server.cfg
- **Seen as:** "couldn't download the Linux runtime"; deploy.sh can't reach the VPS; the server folder has no Docker files
- **Ask me like:** "export a Linux server for my game", "deploy it to my VPS"

### 18.8 Steam release (SteamPipe)
- **System:** Steam Integration
- **What:** Publishing the whole OkaySpace application (launcher, editor, player, relay) to Steam so installed copies update themselves. Manual: upload a build, then set it live on the Steamworks dashboard.
- **Parts:**
  - 18.8.1 Build scripts - `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf` - app 1172560, depot 1172561, content root `../build-win-dist/`, `setlive` empty (upload only), `*.pdb` and `*.log` excluded
  - 18.8.2 Upload wrappers - `steam/upload.sh`, `steam/upload.bat` - run `steamcmd +login $STEAM_USER +run_app_build`; build the dist first if it is missing
  - 18.8.3 Steam app id and SDK - `scripts/build-windows-dist.sh` - bakes the app id (`OKAY_STEAM_APP_ID`); with `STEAMWORKS_SDK_PATH` links real Steamworks and ships `steam_api64.dll`
- **Code:** `steam/app_build_1172560.vdf`, `steam/depot_build_1172561.vdf`, `steam/upload.sh`, `steam/upload.bat`, `steam/README.md`, `scripts/build-windows-dist.sh`
- **API:** `STEAM_USER=<builder> steam/upload.sh`, `"setlive" "default"`, `"preview" "1"`
- **Editor:** none
- **Docs:** `docs/release/steam_release.md`, `steam/README.md`
- **Tests:** none
- **Status:** Basic - scripts and docs exist; no CI job, nothing automated
- **Open:** needs a Steamworks builder account and Valve's one-time review before the first public release; the GitHub release does not build the real Steamworks backend (17.10)
- **Related:** 17.10, 18.1, 41.7
- **Words:** Steam, SteamPipe, steamcmd, depot, app build, setlive, Steamworks, upload.sh, 1172560, auto-update
- **Seen as:** Steam players don't get an update (not set live); steamcmd asks for Steam Guard
- **Ask me like:** "upload a new build to Steam and leave it on the beta branch"

### 18.9 Test builds (the dev channel)
- **System:** Release Pipeline
- **What:** A build of any branch that the coordinator publishes to the `dev/` folder of the Supabase release bucket, so the owner can try unmerged work in the launcher in about 15 to 30 minutes, without a pull request, a GitHub Release or a normal release. Off by default in the launcher; never touches "latest" or LTS.
- **Parts:**
  - 18.9.1 Test build workflow - `.github/workflows/dev-build.yml` - `workflow_dispatch` only (inputs `ref`, `note`), from `main` only; a build job (a copy of release.yml's build steps, no secrets, numbered `<newest release>.<run number>`) and a publish job (the reviewed scripts, one at a time); no release, tag, installer, Linux server or Windows signing
  - 18.9.2 Test build publisher - `scripts/publish-supabase-dev.sh` - writes only under `dev/` (versions in `dev/v<ver>/` uploaded without overwrite, then `dev/versions.json`, its signature, `dev/NOTES-DEV.txt`, and `dev/DEV.txt` last); refuses an existing version, leftovers, a damaged list, a stale base release, a missing checksum; keeps the newest `DEV_KEEP` (3); refuses to run traced with a key set
  - 18.9.3 Test build list - `scripts/dev_index.py` - merge, notes, prune-list, check and the job summary for `dev/versions.json` (schema 1, four-number versions, branch, commit, note, checksums); self-test
  - 18.9.4 The number and the rules - `shared/TestBuild.hpp` - a four-number version (2.876.0.17) sorts just above its release and below every later one; `ChooseTarget` (newer of release and test build, the way back); the cleaned labels; the installed-test-build marker
  - 18.9.5 The launcher - `launcher/VersionsManager.cpp`, `launcher/main.cpp` - `LoadDevIndex` (reads only `dev/`, same signature rule), the real reader skips four-number versions, `Settings::testBuilds` (`test_builds=1`), Settings > Behavior > Get test builds and Go back to the stable version, labels in About, the update window, the sidebar and the editor
- **Code:** `.github/workflows/dev-build.yml`, `scripts/publish-supabase-dev.sh`, `scripts/dev_index.py`, `shared/TestBuild.hpp`, `shared/SelfUpdate.hpp`, `launcher/VersionsManager.cpp`, `launcher/VersionsManager.hpp`, `launcher/main.cpp`, `launcher/VersionsUI.cpp`, `editor/main.cpp`
- **API:** `gh workflow run dev-build.yml --ref main -f ref=<branch> -f note=<line>`, `DEV_BRANCH`, `DEV_SHA`, `DEV_NOTE`, `DEV_KEEP`, `DRY_RUN`, `okay::testbuild::ChooseTarget`, `okayversions::LoadDevIndex`, `okayversions::IndexKind`, `launcher.cfg` `test_builds`
- **Editor:** Help > About and the window title say "(test build)"; the launcher has Settings > Behavior > Startup & updates > Get test builds
- **Docs:** `docs/release/release_channels.md` (Test builds), `docs/release/updater.md` (Test builds), `docs/try_it.md` (7v)
- **Tests:** `dev_channel`, `dev_index`, `dev_publish_script`, `dev_workflow_lint`, `self_update`
- **Status:** Basic - rules, list reader, publisher (against a fake storage server) and workflow lint are tested on Linux; the workflow never ran on GitHub, nothing was written to the real storage, nothing ran on Windows
- **Open:** the first real run (GitHub Actions, the real bucket, Windows launcher screens, a real restart and rollback of a four-number version); the workflow can only be started once it is on `main`; no screenshots; the OkayUI preview What's New page shows the plain number; a branch that is behind `main` builds without main's newest changes (the notes do not say how far behind); only the three programs are delivered (a new example scene is not in a test build); once launchers trust an update key the signing seed must also be a CI secret
- **Related:** 18.4, 18.5, 17.1, 41.6
- **Words:** test build, dev channel, dev folder, Get test builds, dev-build, publish-supabase-dev, DEV.txt, NOTES-DEV.txt, four-number version, try a branch, go back to the stable version
- **Seen as:** the owner wants to try unmerged work without waiting for a release; a "Test build of ..." message in the launcher; a version with four numbers; "(test build)" after a version
- **Ask me like:** "start a test build of walk-vs-run for the owner", "how do I turn test builds off?"

### 18.10 Xbox One export (experimental, Developer Mode, UWP)
- **System:** Web, Mobile and Console Export
- **What:** An EXPERIMENTAL, OFF-by-default export that lays down a Universal Windows Platform package for an Xbox One in Developer Mode, using only the public Windows SDK. Foundation written, unverified on Windows: nothing was built, signed or run on Windows or a console. It is not a native console game (no GDK) and the app shell does not run the game yet (a test triangle and a gamepad reader).
- **Parts:**
  - 18.10.1 SDK check - `platform/xbox_uwp/XboxSdkCheck.hpp`, `XboxSdkProbe.hpp`, `scripts/xbox-check-sdk.ps1` - finds Visual Studio 2022 (vswhere), the UWP C++ toolset, a Windows SDK 17763 or newer with UWP headers and libraries, SignTool (MakeAppx optional); pure decision logic tested with fake folders; the editor disables Export with the reason
  - 18.10.2 Manifest and images - `XboxManifest.hpp`, `XboxAssets.hpp`, `XboxExport.hpp` - AppxManifest.xml (x64, Windows.Universal), name, version and publisher validation, 7 required tile sizes at 100% and 200% made from the project icon with our own area-average resize, or labelled placeholders
  - 18.10.3 Own APPX packer - `XboxAppx.hpp`, `tool/okay_appx.cpp` - unsigned `.appx` (stored ZIP, content types, block map with SHA-256 per 64 KiB block) and a reader that re-checks every hash; no MakeAppx
  - 18.10.4 Build and sign script - `scripts/build-xbox-uwp.ps1` - builds the app and packer, packs, test certificate, SignTool; never run
  - 18.10.5 UWP app shell - `platform/xbox_uwp/app/` - CoreApplication, suspend and resume, D3D11 swap chain triangle, Windows.Gaming.Input; never compiled
  - 18.10.6 Editor entry - `editor/main.cpp` (`OKAY_XBOX_UWP`) - File > Build Settings > Xbox (exp.): SDK list, limits text, identity preview, Export through the existing `ExportForPlatform`
  - 18.10.7 Controller-only play - `XboxLifecycle.hpp`, `XboxUwpBackend.hpp`, `XboxTitleSafe.hpp`, `XboxGameRuntime.hpp` - B is a back press (closes a pause menu, never quits), d-pad menu navigation (`NavigateUI`), title-safe insets on a full-screen TV fill `UISafeInsets`; pure parts tested in `xbox_uwp_runtime`, the WinRT lines uncompiled
  - 18.10.8 Suspend, settings, pads, typing - `XboxSuspend.hpp`, `XboxTextEntry.hpp`, `XboxInput.hpp`, `XboxGameRuntime.hpp` - saves and Prefs under LocalFolder, settings then game state flushed inside the real suspend deadline (timed, skipped steps logged), rumble off at suspend, pad battery level (`PowerLevel`), text input on, the on-screen keyboard follows the edited UI Input Field (`CoreTextEditContext`, shell uncompiled), engine log lines in the log file; tested in `xbox_uwp_runtime` (Suspend / PadPower / TextEntry tests)
- **Code:** `platform/xbox_uwp/`, `scripts/xbox-check-sdk.ps1`, `scripts/build-xbox-uwp.ps1`, `editor/main.cpp`, `CMakeLists.txt` (`OKAY_BUILD_XBOX_UWP`, OFF)
- **API:** `okay::xbox::EvaluateSdk`, `okay::xbox::GenerateManifest`, `okay::xbox::WritePackage`, `okay::xbox::appx::Pack`, `okay::xbox::appx::Verify`, `okay-appx pack|verify`
- **Editor:** File > Build Settings > Xbox (exp.)
- **Docs:** `docs/platform/xbox_uwp.md`, `docs/platform/xbox_uwp.md`, `docs/try_it.md` (xbox-uwp)
- **Tests:** `xbox_uwp`, `xbox_uwp_runtime`, `ui_focus` (pad reaches an Input Field)
- **Status:** Basic - pure parts tested on Linux (and the package cross-checked with `unzip` and Python); everything needing Windows, UWP, signing or hardware is unverified
- **Open:** never built or run on Windows; the packer's Windows rules (entry order, stored files and block Size, name encoding, LfhSize) unchecked; no signing code of ours by design (rule 18; SignTool signs); the app shell does not load the engine (no offline shaders in the engine, no swap-chain present path, no UWP audio, saves or window backend); okay-ms cannot compile it yet
- **Related:** 18.6, 37, 38
- **Words:** Xbox, Xbox One, UWP, Developer Mode, appx, msix, package, AppxManifest, MakeAppx, SignTool, Device Portal, console
- **Seen as:** the owner wants to try the game on an Xbox One; "Xbox (exp.)" in Build Settings; a missing-tools message
- **Ask me like:** "make an Xbox One Developer Mode package", "why is Xbox export greyed out?"

### 18.11 Downloadable game content (content delivery)
- **System:** Game Export
- **What:** An exported game can download signed updates of its own data and scripts (models, textures, animations, scenes, audio, dialogue, Visual Script and OkayScript files) from a folder or a server, resume partial downloads, keep the previous version for rollback and look each file up in the downloaded content first, then in the installed files. Never code or programs. Off unless a content URL is configured.
- **Parts:**
  - 18.11.1 Manifest and signature - `shared/ContentManifest.hpp` - strict text list `{path, size, sha256, kind, group, min-app}` with path, case, size and count limits; ed25519 signature with its own domain, checked through `shared/UpdateSignature.hpp` key and signature parsing; no key means refused
  - 18.11.2 Sync engine - `shared/ContentSync.hpp`, `shared/ContentTransport.hpp` - state machine over an abstract `Transport` (GET with Range): diff by SHA-256, resume, retry with backoff, size and hash check before the file is visible, budget, replay floor, commit record last, previous version kept, `Rollback`
  - 18.11.3 Overlay lookup - `shared/ContentOverlay.hpp` - `Overlay::Resolve(name, installRoot)`: downloaded first, then installed; path-safe, case-insensitive, verified-manifest names only
  - 18.11.4 Transports - `shared/ContentHttpTransport.hpp` - `file://` folder, HTTP(S) over `NativeHttp` (WinHTTP; own client on Linux), and a clearly named Xbox/UWP stub that only fails
  - 18.11.5 Game wiring - `shared/GameContent.hpp`, `shared/GameContentValidators.hpp`, `player/main.cpp` - `content.okayconfig` or `OKAY_CONTENT_URL`; one sync at start-up; graph and script checks before a file is visible; overlay used for scenes, items, weapons, strings, UI images and `.obj` meshes
  - 18.11.6 Tools - `tools/okay-content/main.cpp`, `scripts/publish-content.sh` - make-manifest, verify, sync, status, rollback, pubkey; signed upload to Supabase Storage, manifest last
- **Code:** `shared/ContentManifest.hpp`, `shared/ContentSync.hpp`, `shared/ContentTransport.hpp`, `shared/ContentOverlay.hpp`, `shared/ContentHttpTransport.hpp`, `shared/GameContent.hpp`, `shared/GameContentValidators.hpp`, `tools/okay-content/main.cpp`, `scripts/publish-content.sh`, `player/main.cpp`
- **API:** `okaycontent::Sync`, `okaycontent::Overlay::Resolve`, `okaycontent::StartGameContent`, `okaycontent::CheckSignature`, `content.okayconfig`, `OKAY_CONTENT_URL`, `OKAY_CONTENT_GAME`, `OKAY_CONTENT_KEYS`, `OKAY_CONTENT_GROUPS`, `OKAY_CONTENT_BUDGET_MB`, `OKAY_CONTENT_ALLOW_UNSIGNED`, `OKAY_CONTENT_SIGNING_SEED`, `okay-content make-manifest|verify|sync|status|rollback|pubkey`
- **Editor:** none yet (Build Game does not write `content.okayconfig`)
- **Docs:** `docs/release/content_delivery.md`, `docs/try_it/content-delivery.md`
- **Tests:** `content_delivery`, `content_validators`, `content_cli`, `publish_content_script`
- **Status:** Basic - tested on Linux against a fake server, a local folder and a loopback HTTP server; nothing run on Windows or a real bucket
- **Open:** Windows, `https`, a real Supabase bucket and the player on screen are unverified; the Xbox transport is a stub; no Visual Script block, no progress screen (start-up waits); audio, `DataAsset`, character clips, glTF and FBX models, prefabs and fonts do not look in the downloaded content yet; no PowerShell publisher
- **Related:** 18.5, 17.14, 39.4
- **Words:** content delivery, downloadable content, DLC, content update, asset bundle, patch, manifest, content.manifest, okay-content, overlay, content.okayconfig, OKAY_CONTENT_URL, publish-content, game content, models textures scripts download
- **Seen as:** the owner wants new models, maps or scripts in a shipped game without a new build; a game shows old content after a publish; "content refused: not signed"
- **Ask me like:** "how do I ship a new map without rebuilding the game?", "can the Xbox app download scripts?", "why was my content update refused?"
