# Platform support: Windows, Linux and macOS

This page says, per program and per feature, what is supported today, what CI builds, what has never been run, and what is
not written. It comes from reading the code and the workflows (`.github/workflows/ci.yml`, `release.yml`,
`scripts/build-windows-dist.sh`, the CMake files, `docs/platform_layer.md`, `docs/unfinished_engine.md`, feature map 17 and 37).
It was written without a Mac. **Nothing about macOS here has been compiled or run by us.** The new macOS CI job is the first
thing that will, and the macOS column below must be corrected from its logs. Where this page says "builds in CI" for macOS it
means "the job exists and a build is attempted", never "it passes".

Words used in the matrix:

* **Supported**: shipped as a release artifact and used by people.
* **Builds in CI**: a CI job compiles it (and runs what is listed). Not shipped.
* **Untested**: the code has branches for the platform, nobody has run it there.
* **Not written**: there is no code for it.

## The matrix

### Linux (Ubuntu in CI, x86-64 only)

| Part | State | Notes |
|---|---|---|
| Engine library, 560+ tests | Builds in CI, tests run | `build-and-test`, ASan/UBSan job, Release |
| Player runtime (`okay-player`) | Builds in CI; **Supported as the dedicated server** | `release.yml` `linux-server` ships it as `okay-server-linux-x64` (Ubuntu 22.04, glibc 2.35). As a desktop player it builds and runs under Xvfb in `export-check`; it is not shipped for desktop use |
| Editor (`OkayEngine`) | Builds in CI, `--selftest` runs | Headless self-test only; never driven by a person in CI. Not shipped for Linux |
| Launcher | Builds in CI | Not shipped for Linux. Its install and update flow downloads `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe` (`launcher/VersionsManager.cpp`, `launcher/main.cpp`), so on Linux it cannot install or update an editor |
| Dedicated server | **Supported** | Player `--server`; Linux is the main server target (`docs/dedicated-server.md`) |
| SDL2 | System package (`libsdl2-dev`) | All three GUI programs need it for windows, GL and `SDL_Renderer` |
| Rendering | OpenGL 3.2 compatibility profile (GPU path) plus our software renderer | No Vulkan |
| Audio | SDL2 default; our own ALSA backend tested under ALSA's `null` device | Real hardware untested |
| Windows and input without SDL | Own X11/GLX backend, tested under Xvfb | No Wayland (XWayland only), no XDND drop, no touch |
| Game controllers | SDL default; own evdev mapping tested through a pipe | |
| File dialogs | zenity, then kdialog, then the built-in browser | `shared/FileDialog.cpp`; the argument handling is tested |
| App restart and self update | `AppRestart.hpp` uses `/proc/self/exe`; tested in `app_restart`, `self_update` | The *launcher* update download is Windows-only (see above) |
| Paths | XDG (`Core/Paths`, `platform::PrefPath`) | Tested |
| Crash reports | Editor writes `okay_crash.txt` from a signal handler; launcher page reads it | `player/main.cpp` and `launcher/main.cpp` install no crash handler on any platform (no `SIGSEGV` or unhandled-exception code found) |
| Hardware id (anti-cheat) | `/etc/machine-id` plus CPUID | Other anti-cheat probes (debugger, modules, clock) are Windows only |
| HTTP | `curl` as a spawned program (`NativeHttp` is WinHTTP only) | A machine without curl cannot reach accounts or matchmaking |
| Packaging | Bare binary only | **No tarball, AppImage, `.deb` or installer.** `okay-server-linux-x64` is a stripped binary |
| Release | `linux-server` job only | No Linux editor, launcher or player artifact |

### macOS (Apple silicon `macos-latest` in CI; Intel not covered)

| Part | State | Notes |
|---|---|---|
| Engine library | Builds in CI (**attempted, unverified**) | Portability fixes in this change: `getentropy` header, `<OpenGL/gl.h>`, `/proc/self/exe` replacements. Other code has `__APPLE__` branches written by reading, never compiled |
| Tests | CI job runs a subset (platform, crypto, net, restart, update, crash reports, file dialog, save, math, scene) | The 560-test suite is not run on macOS; a full build would not fit 45 minutes |
| Player runtime | Builds in CI (**attempted**) | Software renderer path expected to work; GPU path see "OpenGL on macOS" |
| Editor | Builds in CI (**attempted**), `--selftest` | ImGui SDL2 + `SDL_Renderer` (Metal). Never run |
| Launcher | Builds in CI (**attempted**) | Feature gaps below |
| Dedicated server | Untested | Same binary as the player; Linux is the supported server platform |
| SDL2 | Homebrew in CI | The only windowing path on macOS |
| OpenGL / Metal / Vulkan | **OpenGL: likely broken for the GPU path. Metal: only inside SDL's renderer. Vulkan: not written** | See "OpenGL on macOS" |
| Audio | SDL2 (CoreAudio underneath) | Own backend not written |
| Own windowing backend | **Not written** | `okay::platform::BackendKind::Own` does not exist on macOS (`cmake/OkayPlatform.cmake` adds sources only for Windows and Linux) |
| File dialogs | `osascript` (`shared/FileDialog.cpp`), else the built-in browser | **Never run on a Mac**; feature map 37.3 says so |
| App restart and self update | `_NSGetExecutablePath` branch exists in `AppRestart.hpp` | Untested. The editor's and player's own copies of "where is my exe" were Linux-only and are fixed here (unverified) |
| Paths | `~/Library/Application Support` in `Core/Paths` | `platform::PrefPath` now matches it (was `~/.local/share`) |
| Crash reports | Editor signal handler has a macOS branch | Untested |
| Hardware id (anti-cheat) | **Empty on Apple silicon** (only CPUID on x86 is read; no machine GUID) | `MachineHash()` returns "" so hardware bans do nothing on a Mac |
| Anti-cheat probes | Not written (Windows only) | |
| HTTP | `curl` (ships with macOS) | |
| Packaging | **Nothing**: no `.app` bundle, no `.dmg`, no `Info.plist`, no icon set | |
| Signing and notarization | **Nothing**; needs owner steps below | |
| Release | No macOS artifact | |

### Windows (for comparison)

Everything ships: launcher, editor, player, installer, Authenticode signing, in-app updates. The own Win32 backend compiles and has
never run on Windows (`docs/platform_layer.md`).

## OpenGL on macOS (a known problem, not fixed here)

The editor and the player ask SDL for an OpenGL **3.2 compatibility** profile (`editor/main.cpp`, `SdlBackend.cpp`). macOS only
offers a legacy 2.1 context or a **core** 3.2+ profile; a compatibility profile request fails. The engine's `GLRenderer` also loads
legacy fixed-function entry points. The probable result on a Mac is "no GPU renderer", and the existing fallback to the software
renderer takes over. That is slow but correct. Supporting the GPU path means either a core-profile port of `GLRenderer` (shaders
and no fixed-function calls; OpenGL is deprecated on macOS and frozen at 4.1), or a Metal renderer. I did not guess at either.

## What this change fixed by reading (all unverified until the macOS job has run)

| File | Problem | Fix |
|---|---|---|
| `engine/src/Render/GLRenderer.cpp` | `#include <GL/gl.h>` does not exist on macOS | `<OpenGL/gl.h>` under `__APPLE__`, `GL_SILENCE_DEPRECATION` |
| `engine/src/Crypto/Random.cpp` | `getentropy` is declared in `<sys/random.h>` on macOS | include it |
| `engine/src/Platform/Platform.cpp` | `BasePath()` read `/proc/self/exe`, so it was **empty on macOS** (the player could not find its game data) | `_NSGetExecutablePath` + `realpath` |
| `engine/src/Platform/Platform.cpp` | `PrefPath()` used `~/.local/share` | `~/Library/Application Support` (what SDL returns) |
| `editor/main.cpp` (`SelfPath`, `OkayExeDir`) | `/proc/self/exe` only: self update and crash reports had no path | `_NSGetExecutablePath` |
| `editor/VsPlayground.hpp` | `/proc/self/exe` only | `_NSGetExecutablePath` |
| `player/main.cpp` (`PlayerExeStem`) | `/proc/self/exe` only | `_NSGetExecutablePath` |
| `editor/GameFiber.hpp` | `<ucontext.h>` is an `#error` on macOS without `_XOPEN_SOURCE` | define it on `__APPLE__` |
| `tests/test_platform.cpp` | `PrefPath` test assumed `XDG_DATA_HOME` | macOS branch checks `~/Library/Application Support` and the exe folder |

What was checked here: the changed files compile on Linux with the project's warning flags (`-fsyntax-only`), and, with
`__APPLE__` defined and a stub `<mach-o/dyld.h>`, the Apple branches of `Platform.cpp`, `Random.cpp`, `GLRenderer.cpp`,
`VsPlayground.hpp` and `test_platform.cpp` parse. That only proves syntax against Linux headers. The real macOS SDK may reject
something else. No full build or test run was possible (the machine had under 1.5 GB of disk free).

Known macOS risks found but not fixed (no confident fix without a Mac): `SDL_GL` compatibility profile (above);
`ucontext` is deprecated and `swapcontext` on arm64 is poorly supported by the SDK, so the visual-script debugger fiber
(`GameFiber`) may need a thread-based replacement; `editor/main.cpp` is 48,000 lines and may hold more Linux-only calls than
grep finds; ImGui's SDL renderer backend on Metal is untested with our fonts and textures.

## CI

| Job | Where | Runs | Required? |
|---|---|---|---|
| `build-and-test`, `player`, `editor`, `export-check`, `sanitizers`, `assimp-models` | Ubuntu | full suite, player, editor `--selftest`, export, ASan/UBSan | existing |
| `windows-cross`, `windows-export`, `windows-restart` | Ubuntu (MinGW), Windows | Windows build, export, restart tests | existing |
| `web-build` | Ubuntu + Emscripten | WebGL player | existing |
| `linux-release-flags` (new) | Ubuntu 22.04 | the release's flags (Release, libsodium) for player, editor and launcher; player `--help`, editor `--selftest` | **no**, `continue-on-error`, 45 min |
| `macos` (new) | `macos-latest`, Homebrew `sdl2 libsodium cmake ninja` | matrix `core` (engine, player, test subset) and `apps` (editor, launcher, `--selftest`); `ninja -k 0` so one run lists every failing file | **no**, `continue-on-error`, 45 min each |

Neither new job is a required check; do not make the macOS jobs required until they have been green for a while.

## Plan: replace SDL2 per platform with `okay::platform` backends

SDL2 stays the default everywhere (`docs/own_code_roadmap.md`). The seam, the own Linux and Windows backends and the painter
exist (`docs/platform_layer.md`). Per platform, what removing SDL2 takes:

| Platform | Own backend | What is missing |
|---|---|---|
| Linux | X11/GLX, ALSA, evdev: written, tested under Xvfb | Wayland, XDND drop, touch, DPI/RandR, window icons, PulseAudio/PipeWire; the editor and launcher still need an `SDL_Window` for `SDL_Renderer` and ImGui's SDL backends |
| Windows | Win32/WGL/XInput: compiles, never run | Run and test on Windows; WASAPI audio (none yet); Direct3D present |
| macOS | **Not written** | Everything below |

macOS backend, in the order I would write it (Objective-C++, `.mm` files, `-framework Cocoa OpenGL CoreAudio AudioToolbox GameController IOKit`):

1. `OwnMacOS.mm`: `NSApplication` + `NSWindow`/`NSView` events mapped to `okay::platform::Event` and the HID `Key` numbering; text input through `NSTextInputClient`; clipboard through `NSPasteboard`; timers via `mach_absolute_time`. Needs a Mac to run at all.
2. Present path: a CPU pixel present (`CGImage` into a layer) for the software painter first, exactly as the Win32 backend did. That alone lets the player run with no SDL on a Mac.
3. GL context: `NSOpenGLContext` with a **core** 3.2 profile, after `GLRenderer` has a core-profile path. Or a Metal layer, if a Metal renderer is written instead.
4. Audio: CoreAudio AudioQueue for playback and capture (queue and callback modes).
5. Controllers: `GameController.framework` (rumble through `GCDeviceHaptics`).
6. Then the shared items on the roadmap that every platform needs: our own 2D painter on the editor and launcher, replacing ImGui's SDL backends with OkayUI.

This work cannot be verified without a Mac in CI, so the macOS CI job comes first.

## Other gaps by area (from the audit)

* **HTTP**: spawned `curl` on Linux and macOS; an in-process HTTPS client needs system TLS (`Network.framework`/`SecureTransport` on macOS, OpenSSL or none on Linux). Roadmap row "curl".
* **Crash reports**: only the editor has a Linux/macOS signal handler. The player and launcher have none on any platform.
* **Anti-cheat**: probes are Windows only; hardware id is Linux (machine-id) and x86 CPUID only. A macOS id (`IOPlatformUUID` through IOKit) was not added because it needs a Mac to check, and a hash that silently changes would break ban matching.
* **Launcher install/update**: Windows file names and `.exe` checksums throughout (`launcher/VersionsManager.cpp`), so a Linux or macOS launcher can run but not install or update an editor. This is the biggest functional gap after packaging.
* **Fonts**: only fixed system font paths; no bundled CJK font.
* **Packaging and release**: `release.yml` has two jobs: Windows (zip, installer, signing) and the Linux server binary.

## Owner-needed steps for macOS releases

These cannot be done by code or by an assistant; none of them needs sharing a secret in a chat or an issue. Secrets go into
GitHub Actions repository secrets, the way the Windows signing certificate already does (`docs/packaging.md`).

1. **Apple Developer Program membership** (paid, per year) for the account or organization that will publish.
2. **A Developer ID Application certificate** (for apps distributed outside the Mac App Store), created in the Apple developer portal and exported as `.p12` with a password. Needed to sign the `.app` and the `.dmg`.
3. **Notarization credentials**: an App Store Connect API key (preferred) or an app-specific password, used by `xcrun notarytool submit --wait` and then `xcrun stapler staple`. Hardened runtime must be on when signing (`codesign --options runtime`), and the app needs entitlements for the things it does (network client, JIT is not used).
4. **A bundle identifier** (for example `com.okayspace.launcher`) and a decision on one or three bundles (launcher, editor, player), and an icon set (`.icns`).
5. **Decide the architecture**: universal (arm64 + x86-64) needs both builds merged with `lipo`; Apple silicon only is simpler. The CI job above builds whatever `macos-latest` is (arm64).
6. **Linux**: nothing needs an account. Decide AppImage versus tarball; an AppImage needs `libfuse2` on user machines, a tarball with a `launch.sh` has no such dependency (a `launch.sh` already exists in the repository root).

What would then be written (code and workflow, no secrets in the repository): a macOS release job that builds on `macos-latest`,
assembles `OkaySpace.app` (`Contents/MacOS`, `Info.plist`, `Resources/*.icns`, bundled `libSDL2` rewritten with
`install_name_tool`/`dylibbundler`), signs, notarizes, staples, wraps in a `.dmg`, and uploads it, plus the launcher changes so it
can install `.app` and `.tar.gz` editor builds instead of `.exe` files.

## Recommended order

1. Run the new macOS and Linux CI jobs and fix what they report; update the matrix here from their logs. (Blocks everything else on macOS.)
2. Launcher install and update for non-Windows asset names, plus a Linux tarball release artifact (no owner steps needed).
3. A core-profile or software-only decision for the macOS GPU path; ship the software path first.
4. Hardware id and crash handler for the player and launcher on Linux and macOS.
5. macOS packaging (`.app`, `.dmg`), then signing and notarization after the owner steps above.
6. The own macOS backend, after the own Windows backend has been run on real Windows.
