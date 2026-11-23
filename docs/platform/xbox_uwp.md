# Xbox One export (experimental)

> **What this is:** The experimental Xbox One (UWP, Developer Mode) export, its audit, and where our own code is used in it.
>
> **Status:** combined on 2026-11-20 from 3 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Xbox One (experimental, Developer Mode) export](#xbox-one-experimental-developer-mode-export) (was `docs/xbox_uwp.md`)
- [Xbox One (UWP, Developer Mode) audit](#xbox-one-uwp-developer-mode-audit) (was `docs/xbox_uwp_audit.md`)
- [Xbox UWP build and third-party code: where our own code is used](#xbox-uwp-build-and-third-party-code-where-our-own-code-is-used) (was `docs/xbox_uwp_thirdparty.md`)

## Xbox One (experimental, Developer Mode) export

*Was `docs/xbox_uwp.md` until 2026-11-20.*

Internal. **EXPERIMENTAL and UNVERIFIED.** Owner request 2026-11-18: a hobby target, not for publishing. Branches `claude/xbox-uwp-1` (packaging) and `claude/xbox-uwp-2` (engine inside UWP, one-click build; this page was updated there).
The audit that led here is [xbox_uwp_audit.md](xbox_uwp.md#xbox-one-uwp-developer-mode-audit).

### What is verified on Windows (2026-11-18) and what is not

**Verified by the owner on a Windows 11 PC** with Visual Studio 2026 (Developer Command Prompt v18.12.1, MSBuild 18.12.3), MSVC 14.52.37025 (cl 19.52.37029), platform toolset **v145**, Windows SDK 10.0.26100.0, command
`scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test4 -MinimalTest -SkipInstall` at branch head `422d23b7`:
* The SDK check, the minimal test export, our packer tool, CMake setup (`-G "Visual Studio 18 2026" -T v145,version=14.52.37025`) and the UWP app compile and link all passed. On Visual Studio 2026 the working toolset was **v145 / 14.52**, the one that has the UWP libraries (`lib\x64\store`); the vcxproj check printed
  `LanguageStandard stdcpp20`, no `/ZW` and no `/await`.
* Our own `.appx` (`com.okayspace.test_1.0.0.0_x64.appx`, 40 files: manifest, block map, 14 tile PNGs, `Data\README.txt`, `OkaySpaceXbox.exe`, 17 engine shader files, `tri_ps.cso`, `tri_vs.cso`) was **accepted by Microsoft's MakeAppx `unpack`** (exit code 0, "Package extraction succeeded").
* SignTool signed it with the test certificate `CN=OkaySpaceTest`; `Import-Certificate` into `LocalMachine\TrustedPeople` and `Add-AppxPackage` installed it without error. The installed app "OkaySpace Test" **started on Windows 11 and showed the colour-gradient triangle on a dark background in a normal window**
  (a Direct3D 11 flip-model swap chain on a CoreWindow works there).
* The `Microsoft.VCLibs` install line failed with **0x80073D02** because that framework was in use by running apps (Store, Widgets, Gaming Services). That is harmless: the already installed framework satisfied the dependency. The build script now prints that line as **optional** ("only if the next step reports a missing dependency").

**Still NOT verified:** gamepad input (green background, stick, A, B), suspend and resume, anything on an **Xbox console** (no claim of console support is made), engine mode (running an exported game), the editor's one-click button and its log panel, the MSBuild AppX validation fix beyond this run (the placeholder manifest needed `mp:PhoneIdentity`), shader loading from `package\Shaders` at run time (the engine renderer is not used by the triangle test), the WASAPI audio code, the signing/trust steps on a console.

### What this is, and what it is not

* It makes a **Universal Windows Platform (UWP)** package that an **Xbox One in Developer Mode** can run, using only Microsoft's public
  Windows SDK and public tools. Nothing from the restricted GDKX, no proprietary code.
* It is **not** a native console game. The public SDK does not build native console games, and this does not claim to.
* It was **written on Linux**, which has no Visual Studio, Windows SDK, SignTool or UWP compiler and no Xbox. Nothing that needs those
  has ever run. "Compatibility with any console" is **unknown**.
* The Xbox app shell has **two modes**. Default: it opens a window, draws a test triangle with Direct3D 11 and reads a gamepad (left stick moves
  the triangle, A centres it, B exits); that proves the package pipeline. **Engine mode** (a tick box next to the build button, `-DOKAY_XBOX_APP_ENGINE=ON`)
  links the engine (`okay_engine_uwp`) and runs the exported game in `package\Data`: scene loading, scripts, fixed-step physics, animation, the 3D view
  through a Direct3D 11 swap chain, gamepad and keyboard input, WASAPI audio output. **The 2D / UI layer** (UICanvas elements) is software-painted and blended over the 3D view since round 3 (never seen on a screen),
  because the software painter has no present path wired. Engine mode has **never been compiled with Microsoft's UWP compiler** (the engine is only ever
  built with MinGW today), so expect compile errors on the first try. Nothing in engine mode ever ran.

### One click (owner request 2026-11-18): the build button runs everything

Install Visual Studio 2022 once (workload "Universal Windows Platform development" with its C++ UWP tools and a Windows 10/11 SDK), open the editor,
File > Build Settings > **Xbox (exp.)**, press **Build Xbox package**, accept the one administrator prompt if you ticked "Also install on this PC".
The page shows a step list with a status per step, a live log, and **Cancel build**. Steps, in order (`platform/xbox_uwp/XboxBuildPlan.hpp`):

| # | Step | What runs | Where it is decided |
|---|---|---|---|
| 1 | Check the Windows tools | the SDK check (in the editor process) | `XboxSdkCheck.hpp` |
| 2 | Export game data, manifest, tile images | the editor's export (`package\Data`, `AppxManifest.xml`, 14 PNG) | `XboxExport.hpp` |
| 3 | Set up the UWP app build | Visual Studio's `cmake` (or the one on PATH) with `-G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore` | `PlanBuild` |
| 4 | Compile the UWP app | `cmake --build` | `PlanBuild` |
| 5 | Put the program into the package | copy `OkaySpaceXbox.exe` (and the two triangle `.cso`) | `XboxBuildHost.hpp` |
| 6 | Compile the shaders (offline) | `scripts\xbox-compile-shaders.ps1` (fxc.exe from the SDK) into `package\Shaders` | `PlanBuild` |
| 7 | Pack and check the `.appx` | our own packer and checker, in the editor process (no MakeAppx) | `XboxAppx.hpp` |
| 8 | Sign | `scripts\xbox-sign.ps1`: `New-SelfSignedCertificate` + Microsoft SignTool. The log says which signer was used. The own-signing stages (`claude/own-signing-plan`, `OKAY_OWN_SIGNING`, not independently reviewed) are not on this branch; when they land the plan's `Signer::OwnSigning` switches step 8 to them | `Signer` |
| 9 | optional: Install on this PC | one UAC prompt, then `xbox-install-local.ps1` imports the public `.cer` into `Cert:\LocalMachine\TrustedPeople` and runs `Add-AppxPackage` | `PlanBuild`, `scripts\xbox-install-local.ps1` |

A failure stops the build with a plain message (what usually causes it) and the first error lines. Cancel kills the running program. Anything that looks
like a password, token or key is replaced in the log; the scripts never print one (the test certificate's private key stays in your Windows certificate store).
The runner (`BuildRunner`) is polled once per editor frame: no `<thread>`, `<mutex>` or `std::condition_variable`. Process spawning is `XboxProcess.hpp`
(`CreateProcessW` with a pipe, `PeekNamedPipe`). **Where the sources come from:** the Windows release carries them in `xbox-src` next to the editor
(`scripts\build-windows-dist.sh`); a source checkout works too, or set `OKAY_SOURCE_ROOT`.
**Verified on Linux:** the plan and its command lines (fake Visual Studio layouts, quoting, paths with spaces), the runner (order, stop on failure, cancel, UAC declined, secrets
never in the log, bounded log), log classification and redaction, the Internal steps (collect and pack on a real folder, our checker accepts the package). **Never run:**
all spawned programs, the PowerShell scripts, the Windows process glue (`XboxProcess.hpp`, syntax-checked with MinGW only), the editor page drawing.

### Where everything is

| What | File | Verified |
|---|---|---|
| SDK check, pure decision logic (Linux-testable) | `platform/xbox_uwp/XboxSdkCheck.hpp`, real disk and `vswhere`: `XboxSdkProbe.hpp` | logic: tests on Linux with fake folders. Real probe: **never run on Windows** |
| Same check for PowerShell | `scripts/xbox-check-sdk.ps1` | **never run** (no PowerShell here) |
| Manifest (`AppxManifest.xml`), name, version and publisher validation, tile list | `XboxManifest.hpp` | text generated and checked on Linux; **never accepted by a real Windows install** |
| Tile and logo images (14 PNG files from the project icon, or labelled placeholders) | `XboxAssets.hpp` | produced and read back on Linux; looks not reviewed by eye |
| Package folder (what the editor writes) | `XboxExport.hpp` | tested on Linux |
| Our own `.appx` writer and checker (no MakeAppx) | `XboxAppx.hpp`, command line `tool/okay_appx.cpp` (`okay-appx pack`, `okay-appx verify`) | container, content types and every block hash checked by our own reader **and by Python's `zipfile`/`hashlib` and `unzip -t`** on Linux; **never opened by Windows** |
| Build, pack, test certificate, sign | `scripts/build-xbox-uwp.ps1` | **never run** |
| UWP app shell | `platform/xbox_uwp/app/` (`main.cpp`, two HLSL files, CMake) | **uncompiled**. Only brace balance was checked |
| Editor entry | File > Build Settings > platform **Xbox (exp.)** (`editor/main.cpp`, under `#if OKAY_XBOX_UWP`) | the editor code was compiled (syntax only) with g++ on Linux, with and without the option; **never seen on a screen** |
| Offline shaders | `engine/include/okay/Render/D3D11Hlsl.hpp` (the HLSL text, moved out of `D3D11Renderer.cpp` byte for byte), `ShaderBundle.hpp` (manifest, hashes, loader), `tool/okay_shaders.cpp`, `scripts/xbox-compile-shaders.ps1` | manifest and loader tested on Linux; **fxc never run**; the renderer compiles (syntax) for desktop and UWP with MinGW |
| Swap-chain present | `XboxPresent.hpp`, `XboxDisplay.hpp` | sizing, Present result handling tested; the DXGI calls only syntax-checked |
| UWP platform backend | `XboxUwpBackend.hpp` (a `platform::Backend`, kind Own), `XboxLifecycle.hpp`, `UwpPlatform.cpp` (replaces `Platform.cpp` in the UWP target) | tested on Linux with a fake host; the CoreWindow glue in `app/engine_shell.hpp` is **uncompiled** |
| Input | `XboxInput.hpp` (Windows.Gaming.Input mapping, dead zones, triggers, vibration timer, key table) | tested on Linux with fake readings |
| Files | `XboxPaths.hpp`, `Paths::SetEnvOverride`, `#if OKAY_XBOX_UWP` in `Platform.cpp`, `Achievements.hpp`, `AccountService.hpp` | path mapping tested; the real folders come from `ApplicationData` (glue, uncompiled) |
| Audio | `XboxAudio.hpp` (ring, conversion, resampler, `platform::AudioDevice`), `XboxWasapi.hpp` | conversion tested on Linux; WASAPI glue syntax-checked only |
| Reduced engine | `engine/CMakeLists.txt` + `uwp_sources.cmake` + `uwp_exclude.txt` (target `okay_engine_uwp`), `XboxUwpAudit.hpp`, `tool/okay_uwp_audit.cpp` | source list and API audit tested on Linux; **never compiled** |
| Game run | `XboxGameConfig.hpp` (tested), `XboxGameRuntime.hpp` (syntax-checked, not linked), `app/engine_shell.hpp` (C++/WinRT, uncompiled), `app/engine_bridge.hpp/.cpp` (engine side, MinGW-syntax-checked) | see "Engine mode" |
| One-click build | `XboxBuildPlan.hpp`, `XboxBuildHost.hpp`, `XboxProcess.hpp`, `scripts/xbox-sign.ps1`, `scripts/xbox-install-local.ps1` | see above |
| Tests | `platform/xbox_uwp/tests/test_xbox_uwp.cpp` (ctest `xbox_uwp`) and `test_xbox_uwp_runtime.cpp` (ctest `xbox_uwp_runtime`, plus `xbox_uwp_audit` and `xbox_uwp_sources_cmake`) | run on Linux, see below |

Isolation: everything Xbox is in `platform/xbox_uwp/`. The CMake option `OKAY_BUILD_XBOX_UWP` is OFF by default. It adds the test, the
`okay-appx` tool and, in the editor, one define (`OKAY_XBOX_UWP`) and one include path. `scripts/build-windows-dist.sh` turns it on so the
Windows editor you run has the Xbox entry (rule 4a). To remove the feature: delete the folder, the option and its `add_subdirectory` line in
the top `CMakeLists.txt`, the lines in `editor/CMakeLists.txt` and `scripts/build-windows-dist.sh`, and the `#if defined(OKAY_XBOX_UWP)` blocks in
`editor/main.cpp`. Without the define the editor compiles exactly as before (checked). The existing Windows export is untouched.

### What you need on the Windows PC (written from Microsoft's pages; check them)

All free. Pages are named, not linked, so they cannot go stale: search for the name on microsoft.com.

1. **Visual Studio 2022 or newer** (the Community edition is enough): the "Visual Studio downloads" page.
2. In the Visual Studio Installer choose **Modify**. **Visual Studio 2022:** tick the workload **Universal Windows Platform development** and, under its optional parts,
   **C++ (v143) Universal Windows Platform tools** and a **Windows 10 SDK or Windows 11 SDK, 10.0.17763.0 or newer**. **Newer Visual Studio** (it has no such workload): on the
   **Individual components** tab tick **C++ Universal Windows Platform support for ... build tools** (the one for your newest MSVC build tools; the workload "Desktop development with C++"
   gives the MSVC tools themselves) and a **Windows 10/11 SDK**. ARM64 / ARM64EC build tools are not needed and are ignored (the check wants the **x64** UWP libraries, `lib\x64\store`).
   This gives the compiler's UWP libraries, the Windows SDK's C++/WinRT headers (`cppwinrt`), `WindowsApp.lib`, `d3d11.lib`, `fxc.exe` and `SignTool.exe`.
   (The "Windows SDK Signing Tools for Desktop Apps" feature carries SignTool and MakeAppx.)
3. PowerShell 5.1 (comes with Windows 10/11).
4. For the console: an Xbox One with **Developer Mode** turned on. Microsoft's page "Xbox One Developer Mode" / "Activate Developer Mode on your Xbox"
   describes the **Dev Mode Activation** app from the console's Store and the developer account it needs. Reported cost and account rules
   are on that page; we did not check them.

`scripts/xbox-check-sdk.ps1` and the editor check Visual Studio 2022 or newer (through `vswhere -products * -sort`, every install of any version; the newest by numeric `installationVersion` that has a UWP toolset wins), the UWP toolset (the newest `VC\Tools\MSVC\<any version>\lib\x64\store`
folder; no toolset version or 2022 folder name is hard-coded), the SDK folders (`Windows Kits\10\Include\<v>\winrt`, `cppwinrt`, `um`, `ucrt`; `Lib\<v>\um\x64\WindowsApp.lib`, `d3d11.lib`) and `signtool.exe`.
`makeappx.exe` is reported but **optional** (our packer replaces it). When something required is missing the editor shows **Xbox export
unavailable**, lists what is missing with the install step, and the Export button is disabled. **These folder names are from memory and unverified**; if the
check says "missing" for something that is installed, that is a bug in the check: send the output of `dir "C:\Program Files (x86)\Windows Kits\10\Include"` and the MSVC folder.

**Visual Studio versions (added after the owner reported Visual Studio 2026):** the check also asks `vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (informational). The UWP component ids
the texts refer to (`Microsoft.VisualStudio.ComponentGroup.UWP.VC.BuildTools`, `...ComponentGroup.UWP.VC`, and guessed `...Component.UWP.VC.v143` / `...Component.UWP.VC`) are **from memory of Microsoft's component directory and
UNVERIFIED**; they are only shown, never used to decide: the folders decide. The CMake generator comes from the found major version (15 = 2017, 16 = 2019, 17 = 2022, 18 = "Visual Studio 18 2026", the last **unverified**; unknown = no `-G`,
CMake picks), with `-DCMAKE_GENERATOR_INSTANCE=<install>` and `-T version=<discovered MSVC folder>` instead of a fixed `v143`. Tested on Linux with fake vswhere output and folders for 2022, a newer install, a newer install without the UWP
component and with ARM64-only extras, both installed, and several toolset folders (`14.9` < `14.44` < `14.100`). Never run on Windows; the PowerShell version of the check was not run at all.

### Engine mode, round 2 (2026-11-18, evening): what is new, what the owner runs next (ALL UNVERIFIED)

**Run this (needs Visual Studio with the UWP tools as before; the compile of the engine is long):**

    powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-engine -MinimalTest -EngineMode -SkipInstall

`-EngineMode` builds the app with the real engine (`-DOKAY_XBOX_APP_ENGINE=ON -DOKAY_SOURCE_ROOT=<repo>`) and, with `-MinimalTest`, the test export also contains `package\Data\game.okayconfig` and a tiny scene (a cube and a camera,
`okay-xbox-export --with-scene`). It prints that engine mode is on, whether `Data\game.okayconfig` is in the package and the exe size. Then pack, sign and install exactly as for the triangle. If the engine fails to compile, send `<ExportDir>\build.log`; drop `-EngineMode` to get the triangle test back.
Expected if it works: a window with an orange cube (turned a little) on a blue sky and, top left, a dark UI panel with the text "Hello Xbox (UI layer)", `B` closes it. If the cube shows but the panel does not, the UI overlay is the suspect (the log says "no UI overlay" when its shaders are missing). Whether it even compiles is the open question.

What is new:
* **C++20 shell, C++17 engine.** The shell (`main.cpp`, `engine_shell.hpp`, C++/WinRT) is its own C++20 target; the engine and `engine_bridge.cpp` (UwpBackend, renderer, presenter, WASAPI, game runtime) are C++17 targets (`okay_engine_uwp`, `okay_xbox_engine_bridge`). Only plain C++ crosses, `app/engine_bridge.hpp`
  (no Windows, WinRT or engine header): create, resize/visibility/activation/key/pointer events, suspend/resume, and one `Run` loop. The engine's `u8string()` use no longer matters for the shell.
* **Sweep of every engine source.** All 310 sources of `okay_engine_uwp` (engine + crypto lists) were syntax-checked with MinGW (`-std=c++17 -D_WIN32 -DWINAPI_FAMILY=2 -DOKAY_XBOX_UWP=1`), whose headers hide desktop-only APIs when `WINAPI_FAMILY=WINAPI_FAMILY_APP`; and again with clang (`clang++ --target=x86_64-w64-mingw32 -fms-extensions`, same defines, MinGW headers): all 310 pass with clang, 0 errors. Three failed the MinGW check at first:
  `Net/ClockCheck.cpp` and `Net/RelayServer.cpp` (via `shared/AntiCheatChecks.hpp`: `CheckRemoteDebuggerPresent`, `GetModuleHandleW`, `GetWindowsDirectoryW`, module snapshots) and `Scripting/OkayScriptVM.cpp` (via `GameIpc.hpp`: `_getpid`).
  Fixed behind `OKAY_XBOX_UWP`: the anti-cheat probes stay empty in a UWP app (the guard idles; anti-cheat is not offered there, nothing pretends to pass), `GetCurrentProcessId` replaces `_getpid`. The audit scanner learned those names. All 310 now pass the MinGW check and the audit finds nothing.
  **A MinGW check is not an MSVC compile**: MSVC can still reject things MinGW accepts, and the UWP allow-list can differ from MinGW's header partition.
* **The tiny scene** was loaded once with the engine's real `SceneSerializer` (against a prebuilt engine library of unknown revision found on this machine, so only a sanity check): 2 objects, 1 mesh renderer, 1 camera. The committed Linux test checks the text format (object count, field counts, config resolves) but does not load it.
* **Link line of the engine target:** `WindowsApp`, `d3d11`, `dxgi`, `ws2_32`, `bcrypt`, `mmdevapi`. Not linked: `d3dcompiler`, `d3d12`, `dxguid`, `winhttp`, `dbghelp`, `user32`, `gdi32`, `opengl32`, SDL2, libsodium.
* The Windows release's `xbox-src` folder now also carries `engine/src`, `engine/CMakeLists.txt`, `engine/third_party` and `shared/`, which engine mode needs.

### Engine mode, round 3: UI layer, device loss, suspend save, HTTP (2026-11-18, late; ALL UNVERIFIED)

The owner's command is unchanged (`-MinimalTest -EngineMode`); the tiny test scene now also has a screen-space UI panel with a text label (copied from `examples/ui_text`), so the UI layer is visible.
* **2D / UI layer on UWP.** `XboxSoftRaster.hpp` (pure: premultiplied RGBA surface, fills, lines, textured blits with scale / flip / rotation / colour and alpha mods, triangles with vertex colour, clip, dirty rectangle) and `XboxSoftPainter.hpp`
  (an `okay::platform::Painter` over it: SDL_Renderer is not available, and okayui's software painter would pull in the whole okayui library) draw the engine's own `RenderSceneUI` pass into a CPU buffer. `XboxOverlay.hpp` uploads the buffer as an R8G8B8A8 texture and blends it over the finished 3D frame in the swap chain's
  after-render hook with a full-screen triangle (`VSOverlay` / `PSOverlay` in `D3D11OverlayHlsl.hpp`, compiled offline into the shader bundle with the others: 17 entries now; the desktop HLSL text is unchanged) and One / InvSrcAlpha blending. `XboxOverlayMath.hpp` (buffer size capped at 1080p worth of pixels, layout size, DIP to UI pixel, draw / clear-once state, dirty-row copy) and the rasteriser are tested on Linux. The runtime now publishes the UI size, the world-to-screen hooks and
  UI layout before each update (as the player does) and feeds the mouse. A scene without UI costs nothing. **Checked once on Linux with the engine's real `RenderSceneUI`** (against a prebuilt engine library of unknown revision): the test scene's panel and label painted about 39,000 pixels through this painter. Never composited on a GPU.
* **Device loss.** `XboxRecovery.hpp` (pure, tested): rebuild with 100 / 200 / 400 / 800 / 1600 ms backoff, give up after 5 failed attempts, or when the device is lost 5 times in 20 s even if every rebuild "worked"; a rebuild that stays healthy 5 s clears the count. The bridge, on `DXGI_ERROR_DEVICE_REMOVED / RESET`, releases overlay, swap chain and renderer, makes the device again, reloads the shaders from the bundle, rebuilds the swap chain and overlay and carries on; when it gives up it logs a plain reason and exits. Rebuilt GPU resources of the scene (textures, buffers) come back lazily.
* **Suspend / resume.** On suspend the runtime captures the scene with the engine's save framework (`SaveSystem::CaptureScene`, written with `SaveStore` into `LocalFolder\saves`, slot `xbox_suspend`) and writes a marker file, before the 4.5 s deadline, then the shell trims the GPU. On resume the marker is removed; on a clean exit too. If the next start finds the marker (the system ended the app while suspended) it puts the state back over the freshly loaded scene (`DecideStart`, tested).
  **Only objects marked Saveable are captured** (the framework's rule); a game must mark what matters. Checked once on Linux (against the same prebuilt library): start, a frame, suspend save, marker written, restart consumed the marker. Lifecycle ordering is in the earlier tests.
* **HTTP for content delivery (hook only).** `XboxHttp.hpp` (pure, tested with a fake client): URL checks (https; no user info; host characters; port), percent-encoded joins, `Range: bytes=N-`, `Content-Range` / `Content-Length` / `Retry-After` parsing, redirects (max 5, never https to http), retries of 429 / 503 with the server's wait (the driver never sleeps),
  a 206 whose range is not the asked one is an error, a server that ignores Range is reported as 200, 416 carries the total, cut-short and too-big bodies are errors. `XboxContentTransport.hpp` is an `okaycontent::Transport` adapter that compiles only when `ContentTransport.hpp` of `claude/content-delivery-1` is on the include path (it was compiled once against that branch's header copied to a scratch folder; the branch is NOT merged here);
  `app/xbox_winhttp.hpp` is the Windows.Web.Http client behind it (C++/WinRT, blocking, worker thread only, **uncompiled, never run**, not wired into the bridge). The manifest already has the `internetClient` capability.

### One command, no editor needed (`-MinimalTest`)

From a source checkout (or the `xbox-src` folder of a release), in PowerShell, with Visual Studio's UWP tools installed:

    powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test -MinimalTest -SkipInstall

`-ExportDir` may be a folder that does not exist yet. `-MinimalTest` compiles `okay-xbox-export` (cl.exe, using the `vcvars64.bat` of whichever Visual Studio the SDK check found) and writes a minimal TEST export:
`package\AppxManifest.xml` (publisher `CN=OkaySpaceTest`, version 1.0.0.0), 14 placeholder tile PNGs (labelled placeholders in `PLACEHOLDER_ASSETS.txt`), `README_XBOX.txt`, a `package\Data` with only a note (so the app shows its triangle).
Then the normal steps run, printed as `[n/9]` lines: tool check, export, our packer, CMake setup, UWP compile, copy, offline shaders, pack and check, test certificate and SignTool. `-SkipInstall` is accepted and does nothing (this script has no install step;
the editor's one-click build has the optional install). `-SkipSign` stops before signing. On any failure the script prints the step name, the **last 15 lines** of the failing tool's output and the path of **`<ExportDir>\build.log`** (the full log, paste it to us).
The tool's arguments (also testable alone): `okay-xbox-export --out <folder> [--name] [--company] [--version] [--bundle-id] [--icon]`. Tested on Linux: argument parsing and the folder layout (14 PNGs, manifest with the right publisher and version, folder created from nothing,
a bad version refused). **Never run:** the PowerShell script (re-read by hand for quoting, `$LASTEXITCODE`, `Resolve-Path` on a missing folder and stderr under `$ErrorActionPreference = 'Stop'`), cl.exe on the tool (it compiles four engine image files; MSVC has not seen them).

### First real Windows build (Visual Studio 2026, MSVC 14.52) and the fixes it forced

The owner's first build log showed errors that no Linux check could catch. Fixed, all still **unverified until the next Windows run**:
* `STL1011` (`<experimental/coroutine>` removed soon): C++/WinRT in C++17 mode uses experimental coroutines. The triangle shell now builds as **C++20**; engine mode stays C++17 (engine headers use `u8string()`, which is `std::u8string` in C++20) and silences the error with
  `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS` (also set in the C++20 build). `/await` and `/ZW` are removed from the flags and the build script logs what the generated `vcxproj` really contains (`/ZW`, `/await`, `LanguageStandard`).
* `Windows::...` was ambiguous with the global `::Windows` while `using namespace winrt;` is active: every use is now `winrt::Windows::...`, with the includes `Windows.Storage.h`, `Windows.ApplicationModel.Activation.h`, `Windows.UI.Input.h` and `::IUnknown` written with the global scope.
* Platform toolset: a newer Visual Studio's default toolset (v145 in 2026) can differ from the toolset that has the UWP libraries (v143 = MSVC 14.30-14.49). CMake gets `-T v143,version=<MSVC folder with lib\x64\store>` (14.50-14.59 = v145, otherwise only `version=`); the folder with the UWP libraries is the one picked when several toolsets are installed. The mapping is from memory: UNVERIFIED.
* MSBuild wants a `Package.appxmanifest` for a WindowsStore executable: `platform/xbox_uwp/app` has a minimal stand-in (and three 1x1 images) and MSBuild is told not to sign or package; our packer writes the real package.
* VC runtime: the app uses the dynamic runtime, so the generated manifest keeps the `Microsoft.VCLibs.140.00` dependency. **Static CRT (`/MT`) was not chosen** because UWP support for it is unverified; instead the build script prints the exact `Add-AppxPackage` line for the framework package found under `ExtensionSDKs\Microsoft.VCLibs` (administrator PowerShell, once), then the certificate import and the package install. Debug is not supported (warning).
* Tools read arguments as UTF-8 through `wmain` (non-ASCII user folders), batch files are written as UTF-8 with `chcp 65001`, and the shader script builds its helper through a `.bat` (it failed on paths with spaces before).
* On failure the script adds a hint when the text contains `v145`, `ZW`, `appxmanifest` or `VCLibs`.

### The pipeline

1. **Editor:** File > Build Settings, platform **Xbox (exp.)**, set name, version and icon (Player page), Export. It writes
   `<out>\package\AppxManifest.xml`, `package\Assets\*.png`, `package\Data\` (your game's data through the existing export code), `README_XBOX.txt`,
   `xbox-export.json` and, if there is no icon, `PLACEHOLDER_ASSETS.txt` (the images are then generated stand-ins, not final art).
2. **PC:** `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <OkaySpace source> -ExportDir <out>` runs the SDK check,
   compiles `okay-appx` with `cl.exe`, builds the UWP app (`cmake ... -DCMAKE_SYSTEM_NAME=WindowsStore`), compiles the two shaders to `.cso` (build time; `D3DCompile`
   does not exist inside UWP), copies the exe and shaders into `package\`, runs `okay-appx pack` then `okay-appx verify`, makes a test certificate with
   `New-SelfSignedCertificate`, and signs with `SignTool sign /fd SHA256 /sha1 <thumbprint>`. With `-SkipSign` it stops after the unsigned package.
3. **Console:** see "Deploy".

### Our own packer: what it writes and which rules could not be checked

`okay-appx` writes an **unsigned** `.appx`: a ZIP holding the payload files, `AppxManifest.xml`, `AppxBlockMap.xml` (per 64 KiB block SHA-256, base64) and
`[Content_Types].xml`. Written from Microsoft's "App package structure", "App package block map" and "App package manifest" pages, ECMA-376 part 2
(Open Packaging Conventions) and the ZIP application note. The hash is our `okay::crypto::Sha256`; the CRC-32 table, base64 and ZIP code are written in the file.
No existing zip writer was reused: `builder::WriteZip` stamps the current time, deflates, and has no control of order or of the local-header size the block map
needs (`LfhSize`).

**What the tests prove (Linux):** for a package with a manifest, a PNG, a 130,000-byte file (2 blocks), an empty file, a file with no extension and a space in its name:
our reader finds every entry, every CRC-32 matches, every content type is covered (`Default` per extension, `Override` for no-extension files, manifest and block map),
every file is in the block map once with the right `Size`, `LfhSize` and block count, and **every block hash is recomputed and equal**. A flipped payload byte, a wrong block hash, a
renamed block map, a truncated file, a missing manifest, a duplicate name (ignoring case) and the names `..`, `/abs`, `a:b`, `trailing.` are all caught or refused. The same file
was cross-checked outside our code: `unzip -t` is clean and Python's `zipfile` and `hashlib` recompute the same block hashes and sizes. Output is deterministic.

**Rules we could not check without Windows (all unverified, each is a possible reason Windows refuses the package):**

1. Entry order. We write the manifest first, the payload sorted by name, then `AppxBlockMap.xml`, then `[Content_Types].xml`. We do not know whether Windows requires an order.
2. All entries are **stored** (no deflate). We believe a stored package is allowed (MakeAppx has a no-compression switch) and that a stored file's `<Block>` has no `Size`
   attribute (the attribute is for the compressed size). Windows may want it.
3. `LfhSize` is our reading of "size of the local file header": 30 bytes plus the name length (we write no extra field).
4. Names inside the ZIP are percent-encoded except `[Content_Types].xml`; block map names use `\`. The exact set of characters that must be encoded is a guess (we encode everything except letters, digits, `- . _ ~ /`).
5. The DOS time stamp 1980-01-01, `version needed` 20, no data descriptors, no ZIP64 (we refuse a package over 4 GB).
6. Content types for unknown extensions are `application/octet-stream`; the `Default` list is ours.
7. Whether the extra `scale-200` images and the unscaled base images both being present is accepted (MakeAppx does not enforce image sizes, Visual Studio does).
8. The `AppxManifest.xml` itself (identity, `Windows.Universal` device family, `MinVersion` 10.0.17763.0, `EntryPoint`, the `Microsoft.VCLibs.140.00` dependency with its publisher string) is unverified.
   An Xbox in Developer Mode must also be able to find the VCLibs framework package; the Device Portal install page lets you upload dependencies.
9. Package full-name and publisher-id rules (the signer's subject must equal `Identity/@Publisher` exactly) are enforced only by the validator we wrote from the documentation.

The editor and the script both run `okay-appx verify` on the result and refuse to hand out a package that fails it. A MakeAppx `unpack` cross-check runs in the build script when MakeAppx exists.

### Signing: honest assessment

Windows installs only **signed** packages. The signature is the part `AppxSignature.p7x`: from Microsoft's description, the four letters `PKCX` followed by a PKCS#7 / CMS
`SignedData` whose content is an Authenticode "indirect data" structure carrying digests of the package parts (the entry data, the central directory, `[Content_Types].xml`,
`AppxBlockMap.xml`). The signing certificate must carry the code-signing usage and its subject must equal the manifest publisher.

What writing it ourselves would need:

* **ASN.1 DER writing** (lengths, OIDs, sets, the X.509 `Certificate`, `SignedData`, signed attributes): ordinary parsing and writing, no secrets. We could write it.
* **SHA-256**: we have it.
* **RSA signing** (RSASSA-PKCS1-v1_5 with SHA-256, 2048-bit keys, big-number arithmetic, key generation with primes, padding): this is **cryptography**. libsodium has no RSA. CLAUDE.md rule 18:
  cryptography is not replaced on our word and no outside review is planned. **We wrote no RSA code and will not without the owner's explicit approval.** Doing it would also need
  constant-time big-number code; a self-signed test certificate for a hobby console is a low-stakes use, but that is the owner's call, not ours.

Options, in the order we would take them:

1. **Now (built):** our packer makes the unsigned package. SignTool signs it on the PC (Microsoft's tool, public), with a test certificate from PowerShell's
   `New-SelfSignedCertificate`. No cryptography of ours is involved.
2. **Possible next step, needs the owner's decision:** our code writes all the structures (DER, `SignedData`, the digests) and asks the **operating system's** key store to
   produce only the RSA signature value (Windows CNG `NCrypt` / `BCrypt`). The RSA stays Microsoft's; ours is the container. This is not "own crypto", but it still must be checked on Windows.
3. **Only with explicit approval:** our own RSA. Would be labelled "not independently reviewed" like `okay::crypto`.

### Compile step: what it would take for okay-ms to build the app later

For now **Microsoft's UWP compiler builds the app** (`cl.exe` through CMake and MSBuild). `tools/okayms/` (`docs/platform/own_msvc_plan.md`) is our MSVC-style toolchain; it cannot do this today. What it would take,
unverified and from the plan page plus the UWP rules:

* **A language gap that is large.** The plan page says classes, member functions, templates and exceptions are *not started*. The shell is C++/WinRT (templates, classes, lambdas, exceptions). It would have to be
  rewritten in plain C against COM v-tables. That is possible (WinRT is COM), but it is a different, longer program.
* **Our own declarations of every COM/WinRT interface used**, written from Microsoft's documentation of the interface layouts (v-table order, IIDs, `HSTRING` functions): `IFrameworkViewSource`,
  `IFrameworkView`, `ICoreApplication`, `ICoreWindow`, `IDXGIFactory2`, `ID3D11Device`, `ID3D11DeviceContext`, `IDXGISwapChain1`, `IGamepadStatics`. Not copied from the SDK headers (rule 19a).
* **Import libraries for UWP.** UWP apps link the umbrella `WindowsApp.lib` (plus `d3d11.lib`, `dxgi.lib`). `okay-lib`-style import libraries can be generated from the DLL export lists
  (the plan page says import libraries are written: stage 9), so we can make ours from the export tables of the system DLLs on a Windows PC. Which DLL exports are allowed in UWP is Microsoft's allow-list
  (the umbrella's contents); that list must be read from `WindowsApp.lib` on Windows once.
* **The App Container bit.** A UWP exe must be marked as an app-container executable in its PE header and use only allowed APIs. `okay-link` has no such flag today (only NX is mentioned in `link.hpp`).
* **No CRT of Microsoft's.** The shell uses the VC runtime (hence the `Microsoft.VCLibs.140.00` dependency). With okay-ms we would ship our own small runtime and drop that dependency, but the runtime is a
  few functions today.
* **Shaders** still need an HLSL compiler. `fxc.exe` ships with the SDK; our own HLSL compiler does not exist.
* **Debug and unwind:** `.pdata/.xdata` exist; no PDB (only a CodeView directory), so crash debugging on the console would be by hand.

Realistic order: first make the app C (no WinRT classes), then the three missing language features, then the app-container flag, then generated import libraries. None of it is started in this branch.

### Round 4 (branch `claude/xbox-uwp-3`): review of the merged build, log file, Linux checks (ALL UNVERIFIED on Windows and Xbox)

**Reviewed for a first real run** (read by hand, no PowerShell was available on the Linux machine): `build-xbox-uwp.ps1`, `xbox-check-sdk.ps1`, `xbox-compile-shaders.ps1`, the manifest, the packer. Found and fixed: (1) a dead include path `engine\third_party\stb` in the export-tool compile (harmless to `cl.exe`, but the folder does not exist); (2) a failure only said "see the list above": every step now has a **WHAT TO DO NEXT** line (`$script:Hints`), a wrong `-SourceRoot` and a non-Windows host are caught first with a plain message; (3) every run made a new test certificate, so the PC and the Xbox would have had to trust a new one each time: a valid one with the same subject is re-used; (4) the install lines now say what to do when an older copy is installed (`0x80073CFB`), when the certificate was not imported as administrator (`0x800B0109`) and where the log file is; (5) the packer accepted Windows device names (`CON.txt`, `NUL`): refused now (test in `test_xbox_uwp.cpp`).

**Engine-mode gaps, what already existed and what is new.** The gap list asked for gamepad, audio, file access, save path, window size, suspend/resume and logging. We searched first: gamepad (`Windows.Gaming.Input` in `app/engine_shell.hpp`, mapping in `XboxInput.hpp`, rumble), audio (`XboxWasapi.hpp`, `XboxAudio.hpp`), package / LocalState / saves / temp folders (`XboxPaths.hpp`, `Paths::SetEnvOverride` in `engine_bridge.cpp`, saves in `LocalFolder\saves`), console resolution (`XboxDisplay.hpp`: `ChooseBackBuffer`, 1080p cap) and suspend save/restore (`XboxLifecycle.hpp`, `XboxGameRuntime.hpp`) all exist from round 2 and 3 and are NOT written a second time. XInput was not added: `Windows.Gaming.Input` is the supported pad API for UWP. **New: the log file.** `XboxLog.hpp` (`okay::xbox::FileLog`, pure C++, atomics only, tested by `xbox_uwp_next`) writes `LocalState\logs\okayspace.log` (rotates to `.1.log` past 512 KB, flushed per line). `UwpBackend::logSink` feeds every backend log line into it; `engine_bridge.cpp` logs the start-up folders, pad count, suspend, resume and the reason for an exit; `engine_shell.hpp` catches an exception from the game loop and logs it; the triangle app (`main.cpp`) logs its start, pad connect and device loss. Still **not done**: an on-screen error message (failures go to the log, the app exits), the engine's own `okay::Log` lines were not routed into the file (done 2026-11-22), `SaveFile.cpp` / `Prefs.cpp` use CRT calls (`_wfopen`, `_commit`) that were read, not checked against a real UWP compile, a Windows clipboard, text composition, microphone, anything on a console.

**Linux checks added** (`ctest -R xbox_uwp`): `xbox_uwp_next` (log file), `xbox_uwp_pipeline` and `xbox_uwp_scripts` (both `scripts/check_xbox_uwp.py`: export, pack, verify with our reader and Python's zip reader, manifest rules, damaged packages refused; a PowerShell lexer proves brackets and quotes balance, files are ASCII, repo paths exist, every step has a hint, doc switches exist). These prove the packaging and the script text, not that PowerShell runs them. The owner's one-page steps: `docs/try_it.md` entry `xbox-next` (`docs/try_it/xbox-next-xbox-uwp-next.md`).

### Editor lag when the Xbox platform is chosen (fix, branch `claude/xbox-uwp-4`; UNVERIFIED on Windows)

**Cause (read from the code, not measured):** `CachedSdkReport()` ran the SDK probe on the editor's UI thread the first time the Xbox platform or page was drawn: four `vswhere.exe` calls through `_popen` plus folder walks, each call can take seconds on a PC with Visual Studio. **Fix:** `XboxAsyncProbe.hpp` (`AsyncSdkProbe`): the first `Get()` starts the probe on one worker thread (native `CreateThread` on Windows, `std::thread` only off Windows, no mutex or condition variable; hand-over by one atomic with acquire/release) and returns a report with `checking = true` ("Checking your PC for the Xbox tools..."); a refresh request while it runs is ignored; the editor greys the build button with a tooltip and shows a spinner. The probe makes ONE `vswhere -format json` call (`XboxVswhereJson.hpp` reads it) with a 5 second kill timeout ("vswhere did not answer ...") instead of four; the fourth call (`-requires`) only set an informational flag and is gone. A useful answer is cached in `%LOCALAPPDATA%\OkaySpace\xbox_vswhere_cache.txt`, keyed by vswhere.exe's size and time and the time of Visual Studio's `_Instances` folder; Check again skips the cache. Nothing runs at editor start-up. Also: the build log panel draws only visible lines (`ImGuiListClipper`, it holds up to 3000), the signer lookup no longer copies the whole log every frame, and the duplicate `Tick()` is gone (`Tick` itself is idle unless a build runs). Tests: `xbox_uwp_async` (state machine, "Get returns in under 5 ms while a 300 ms probe runs", refresh ignored while checking, hand-over, a throwing probe, the JSON reader, the cache file; also run under ThreadSanitizer, clean). **Not verified:** the Windows branches (`CreateThread`, `CreateProcessW` + pipe + kill, the vswhere JSON of a real machine, the cache paths), that this really removes the lag, and `editor/main.cpp` was only syntax-checked on Linux (its only errors were Windows-header ones in `AccountService.hpp`, same as before). Also fixed: `UwpPlatform.cpp` lacked `Backend::CharForKey` (added by #1009), so `xbox_uwp_runtime` did not link.

### Controller-only play: B, the title-safe area and menus (2026-11-22, branch `claude2/xbox-win32-platform`; UNVERIFIED on Windows and Xbox)

Three bugs a player with only a controller would hit in engine mode, found by reading the code:

* **B closed the game.** On a console B raises `SystemNavigationManager.BackRequested`; `Lifecycle::BackRequested` turned it into an exit and the backend into `Quit`, so any B press (B is an ordinary game button) ended the game. Now it is a back press: `LifecyclePending::back`, counted by `UwpBackend::TakeBackPresses()`, handed by the bridge to `XboxGameRuntime::BackPressed()`, which is an Escape tap only while the game is paused (it closes a pause menu) and nothing during play. Leaving is the game's own Quit (pause menu) or closing the window. Ignored while suspended. Test `xbox_uwp_runtime` (lifecycle and backend checks; before the fix the test did not compile, after: all passed).
* **Menus could not be used without a mouse.** The desktop player calls `NavigateUI` every frame (d-pad / arrows move the focus, A presses); the Xbox runtime never did. `XboxGameRuntime::Frame` now calls it after the game loop step. Also: a key tap shorter than a frame now counts (`KeyTapped`, the player's `FeedKeyTap` rule). Syntax-checked with g++ against the engine headers only.
* **No title-safe area.** The shell now asks for the whole screen (`ApplicationView::SetDesiredBoundsMode(UseCoreWindow)`) and passes the result and the device family (`AnalyticsInfo.VersionInfo.DeviceFamily`) to `bridge::ScreenBounds`; on a console drawn to the full TV, `XboxTitleSafe.hpp` gives 5% insets per edge (48 x 27 of 960 x 540 effective pixels; checked 2026-11-22 through a web search that showed Microsoft's developer blog "Tailoring your app for Xbox and the TV" with these numbers: the Learn page itself could not be fetched from the build machine, so the 5% stays as it was) and fills `okay::UISafeInsets()`, so widgets under a **UI Safe Area** container stay inside it. A PC gets zero insets. Pure part tested (`TitleSafeTests` in `xbox_uwp_runtime`); the shell lines are uncompiled (C++/WinRT, Windows only).

* **Pads by index: a crash and shifting players.** The bridge made one `PadDevice` per index of `Gamepad::Gamepads()` and deleted them all whenever the count changed, while the open pads still pointed at them (use after free on the next poll); a pad unplugged in the middle also moved every later player down one index, and the hub (`XboxPadHub.hpp`, slots that survive a short disconnect) was never called by the app. Now the shell lists pads by `RawGameController.NonRoamableId` each frame (`padKeys`, `padReadKey`, `padVibrateKey`), and `XboxPadSync.hpp` turns that list into hub slots: a new key gets a device and a slot (`GamepadAdded`), a gone key loses its slot after the 10 s grace time, its device is freed only after the backend let go, a pad refused for lack of slots is offered again next frame. Polling keeps it on the UI thread (the `GamepadAdded` events come on another thread). The old index path only grows now (nothing freed while in use). Test `PadSyncTests` in `xbox_uwp_runtime` (also under ASan: clean). Bridge: MinGW syntax check; shell: uncompiled.

Also: the pause menu (`PauseMenu` component) now opens and closes with **Start (Menu)** on any pad and closes with **B** (test `pause_menu`), so a controller-only game can pause and reach Quit. Written from Microsoft's documentation of `BackRequested`, `ApplicationViewBoundsMode` and `AnalyticsInfo`; our own code.

### Saving at suspend, settings, rumble and battery, typing with a pad (2026-11-22, branch `claude2/xbox-uwp-3`; UNVERIFIED on Windows and Xbox)

What a player would hit, found by reading the code, then fixed. Pure parts tested on Linux (`xbox_uwp_runtime`: `SuspendTests`, `PadPowerTests`, `TextEntryTests`; `ui_focus`); the bridge only syntax-checked with MinGW (`WINAPI_FAMILY=APP`); the shell (C++/WinRT) **not compiled**.

* **Saves went to the read-only install folder, settings were never kept.** The Xbox runtime never called `Save::SetBaseDir`, so a game's own save slots (`GameSaves`, Save Slots screen, `SaveData` files) resolved into the package folder and failed; it never called `Prefs::Load` / `Prefs::Save`, so volume, language, accessibility and key choices were lost at every exit or suspend. Now (`XboxSuspend.hpp` `SaveLayoutFor`): saves and slots under `LocalFolder\saves\`, settings in `LocalFolder\saves\game.okayprefs` (loaded with `Accessibility::Load` before the scene), the suspend snapshot in `LocalFolder\suspend` (its own folder: it no longer shows up in the game's load menu or as "Continue").
* **Suspend uses the real deadline.** The shell passed a fixed 4500 ms; now it passes `SuspendingOperation.Deadline` and the current time (100 ns ticks) and the engine plans with what is left minus 1 s for the GPU trim (`SuspendRemainingMs`, `FlushBudgetMs`). The flush (`SuspendFlush`) runs **settings first**, then the game state, each timed; what does not fit is skipped and the log file says so ("suspend save: settings ok (2 ms), game state ok (40 ms)"). Settings are also written at a clean exit. A game save that was asked for "next frame" right before the suspend is not forced (it runs when the game resumes, or is lost if the system ends the app).
* **Rumble kept going while suspended.** Windows.Gaming.Input holds a vibration until it is changed, and a suspended app never polls to end it. Suspending now stops every open pad's motors (trigger motors too) before the save. The engine's rumble API (`Input::Rumble`, trigger rumble) already reached the pads through `UwpGamepad` (round 2); unchanged.
* **Battery.** `UwpGamepad::PowerLevel()` was always "unknown". Now the shell reads `TryGetBatteryReport` (status and the two capacities) for the pad's key; the level is read on the first poll and then every 5 s: no battery = wired, under 5% empty, under 25% low, under 75% medium, else full (our thresholds). It reaches `PadProfiles::BatteryText` and the script builtin `pad_battery`.
* **Typing did nothing.** `StartTextInput` was never called in the Xbox app, so every typed character (USB keyboard) was dropped. Now it is on for the whole run, as in the desktop player.
* **Text fields with only a pad.** `NavigateUI` could not focus a **UI Input Field** at all (desktop too). Now the d-pad highlights it (focus colour and ring, not editing), A / Enter / Space starts editing, the d-pad belongs to the field while it edits, Enter / Esc / B end it (field option `focusable`, saved in scenes since round 4). On a console (device family `Windows.Xbox`) the field being edited brings up the **on-screen keyboard** (`XboxTextEntry.hpp`): the shell drives a `CoreTextEditContext` (input scope Default / Number / Password from the field's content type) and `InputPane.TryShow / TryHide`; its edits (UTF-16 ranges) go through a mirror into the field, which applies its length and content-type rules, and the keyboard is re-synced when the field refused something; the keyboard's Enter submits; closing the keyboard (B) ends the editing so the pad navigates again. B also ends editing when the keyboard is already closed. A PC never shows it.
* **A failure said nothing on screen.** When the game could not start or stopped with an error, the app closed and the reason was only in the log file. Now the shell shows a message box (`Windows.UI.Popups.MessageDialog`) with the reason and the log file's path (`FailureText` in `XboxLog.hpp`, tested in `xbox_uwp_next`) and closes when the player dismisses it.
* **The engine's own log lines** (`okay::Log`: scene loading, saves, scripts) now go into `LocalState\logs\okayspace.log` too (`Log::sink`), not only the backend's.
* **UWP API check of the save code:** `Prefs.cpp`, `SaveFile.cpp`, `Recovery.cpp`, `SaveFrameworkScene.cpp`, `AccessibilitySettings.cpp` pass a MinGW syntax check with `WINAPI_FAMILY=APP` and `OKAY_XBOX_UWP` (0 errors). Not an MSVC compile.

Written from Microsoft's documentation of `CoreApplication.Suspending`, `SuspendingOperation.Deadline / GetDeferral`, `DateTime`, `IGameControllerBatteryInfo.TryGetBatteryReport`, `BatteryReport`, `BatteryStatus`, `Windows.UI.Text.Core` (`CoreTextEditContext`, its request and updating events, `InputPaneDisplayPolicy`, `CoreTextInputScope`) and `InputPane`; our own code.

**Needs a real Xbox (or the owner's Windows PC) to know:** that the keyboard shows for a `CoreTextEditContext` in a CoreWindow game on a console and which events its Enter and B really raise; that `TryGetBatteryReport` gives capacities for an Xbox pad on AA batteries (it may give none: the level then stays "unknown"); that the save fits the deadline on console storage; that the motors stop. **Needs the owner's account (GDK, later):** nothing here.

### Deploy to an Xbox in Developer Mode (reported steps, unverified)

1. On the console, activate Developer Mode (Dev Mode Activation app). It restarts into the Dev Home screen, which shows the console's address.
2. On the PC, open the **Windows Device Portal** of the console in a browser at the address Dev Home shows (HTTPS; the usual port reported for Xbox is 11443). Accept the certificate warning.
3. Apps > **Add** (the page for uploading an app package): choose the signed `.appx`, add the `Microsoft.VCLibs.140.00` framework package as a dependency (the file is in Visual Studio's
   `Common7\IDE\Extensions\Microsoft\...\Appx` folders or the Visual C++ runtime download page for UWP), and install.
4. Start the app from Dev Home / the console's "Dev Mode" apps. Expected: a dark window with a coloured triangle; the background turns green once a pad is connected; the left stick moves the triangle; B exits.
5. Whether a self-signed test certificate is accepted by the console without trusting it first is **unknown**. Microsoft's usual route is Visual Studio's "Remote Machine" deployment; if the Device
   Portal refuses the package, try that route and report what it said.

### Console test plan (PREPARED, NOT STARTED, UNVERIFIED), in plain words

Goal: see the same triangle on an Xbox One. Nothing here has been tried on a console; every step comes from Microsoft's public pages and may differ. Do not start until you have the signed package from the PC steps above.

1. **Turn on Developer Mode.** On the Xbox, install the free **Dev Mode Activation** app from the Store, sign in with the Microsoft account you registered as a developer, and follow it. The console restarts into a **Dev Home** screen. (Microsoft's page "Activate Developer Mode on your Xbox" is the reference; account rules and cost are on that page. Dev Mode is a separate mode: your normal games are not available while it is on.)
2. **Find the console's address.** Dev Home shows the console's IP address. Write it down. The console and your PC must be on the same network.
3. **Open the Device Portal.** On the PC, open a browser and go to `https://<that address>:11443` (the usual port reported for Xbox; if it does not load, use the exact address and port Dev Home shows). The browser warns about the certificate: accept it. You may be asked for a PIN shown on the console.
4. **Upload the package.** In the portal choose **Apps** (or "My games & apps") > **Add** (Install app). Pick the signed `.appx` from your export folder. If the page offers **dependencies**, add the VC runtime package `Microsoft.VCLibs.x64.14.00.appx` (the build script prints where it is under Program Files (x86) > Microsoft SDKs > Windows Kits > 10 > ExtensionSDKs > Microsoft.VCLibs). Press **Start/Go**.
5. **Trust problem?** Whether the console accepts a package signed with our self-made test certificate is **unknown**. If the portal refuses it, write down the exact message; Microsoft's usual route is Visual Studio's "Remote Machine" deployment, which we have not set up.
6. **Start the app.** In Dev Home open the list of Dev Mode apps and start **OkaySpace Test**. You should see a dark window with a coloured triangle. If you connect a pad: the background turns green, the left stick moves the triangle, A centres it, B closes the app.
7. **Send back:** a photo or description of the screen at each step, the exact text of any error (the portal's message, or the console's error code), the console model (One, One S, One X), the Dev Mode / OS version shown in Dev Home, and whether the pad worked. Do not send passwords, codes or the contents of certificates.
8. **Then:** suspend and resume (press the Xbox button and come back), and only later try "engine mode" on the PC first.

### Verify it yourself on a Windows PC (no Xbox needed for most of it)

A UWP package also installs on Windows 10/11, so steps 1 to 6 test the whole pipeline without a console.

1. Install Visual Studio 2022 with the pieces above.
2. `powershell -ExecutionPolicy Bypass -File scripts\xbox-check-sdk.ps1` . Paste the whole output.
3. Start the editor, File > Build Settings, choose **Xbox (exp.)**. Paste what the Xbox page says (or a photo; screenshots stay out of the repo). Press Export. Tell us whether `package\` has the manifest and 14 images.
4. `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <source folder> -ExportDir <export folder>`. Paste the whole output, including the `okay-appx` lines and the MakeAppx unpack exit code (0 means Microsoft's own tool read our container).
5. As administrator: `Import-Certificate -FilePath <out>\OkaySpaceXbox-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople`, then `Add-AppxPackage <out>\<name>.appx`. Paste any error text.
6. Start the app from the Start menu. Tell us what you see (expected: the triangle) and whether a pad moves it.
7. Only with a console in Developer Mode: the Deploy steps. Tell us exactly what each page showed.

Run `ctest -R xbox_uwp` (needs `-DOKAY_BUILD_XBOX_UWP=ON -DOKAY_BUILD_TESTS=ON`) to run the Linux tests anywhere.

### The engine inside UWP (branch `claude/xbox-uwp-2`): what was done, from what, and what is not

All of it is **unverified on Windows**: written and tested on Linux, never compiled with Microsoft's UWP compiler, never run on a PC or a console. "Tested" below means
the pure logic ran on Linux. Each file says in its header what it was written from; everything is our own code from Microsoft's public documentation and the C++ standard,
no code copied, no new dependency (rule 19a). Everything Xbox-only is behind `OKAY_XBOX_UWP` (a compile define set only by the UWP target) or in `platform/xbox_uwp/`; the
desktop build is unchanged (checked below).

1. **Offline shaders.** `D3DCompile` was called at 8 places in `D3D11Renderer.cpp` for 15 entry points (VSMain, PSMain, VSDepth, VSSkin, VSDepthSkin, VSQuad, PSSky, PSDown, PSBlur, PSUber,
   PSFxaa, PSAoDepth, PSAo, PSAoMul, PSDof). The HLSL text moved byte for byte into `D3D11Hlsl.hpp` (compared with the old string: identical, 23,291 bytes). One helper
   (`CompileShader`) now either calls `D3DCompile` (desktop, unchanged) or, with `OKAY_XBOX_UWP`, reads `shaders.manifest` + `okay3d_<entry>.cso` from `package\Shaders` through
   `ShaderBundle.hpp` (sizes and FNV-1a hashes checked, `DXBC` magic checked, refuses a bundle made from a different HLSL text, required entries VSMain/PSMain, optional ones switch a feature off exactly as
   a desktop compile failure did). `okay-shaders dump|manifest|verify` and `scripts/xbox-compile-shaders.ps1` (fxc `/T <profile> /E <entry> /Fo`) make the bundle at build time. `D3D12Renderer.cpp` also calls D3DCompile; it is excluded from the UWP target.
   Not done: shader debug info / `/O` flags are fxc defaults; the other HLSL users (the UI D3D11 painter `OkayUI_D3D11.cpp`) are not part of the UWP target.
2. **Swap-chain presentation.** `XboxPresent.hpp`: a flip-model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, 2 buffers, `R8G8B8A8_UNORM`, `DXGI_SCALING_STRETCH`) swap chain on the CoreWindow, fed by the
   existing `D3D11Renderer::RenderToTexture` (it already resolves MSAA straight into a texture, no read-back). The back buffer is capped at 1920x1080 by default (`ChooseBackBuffer`, our default, not a
   measured limit). `Present` results map to Ok / Occluded / DeviceLost (`ClassifyPresent`). Not done: device-loss recovery (the shell exits with a message), HDR, 4K.
3. **Window and lifecycle.** `XboxUwpBackend.hpp` implements `okay::platform::Backend` (kind `Own`) over a small `UwpHost` interface: window size/visibility/activation, suspend with a save hook and the 5 s deadline rule
   (`XboxLifecycle.hpp`), resume, B / back request to a single `Quit` event, DPI changes (`DipsToPixels`), keyboard, mouse, pads, audio, clipboard (in-app only). `UwpPlatform.cpp` replaces `Platform.cpp` in the UWP target
   (registry, defaults, timers). Not done: touch, text composition (IME), the system clipboard, Windows-key layouts (a US layout is assumed for `KeyForChar`), multi-window, a splash.
4. **Input.** `XboxInput.hpp`: `GamepadReading` to `platform::GamepadAxis/Button` with XInput's documented dead zones (radial, rescaled), trigger threshold, +Y flipped to SDL's +Y down, Menu to Start, View to Back, d-pad and
   shoulders and thumb clicks; rumble with a duration (the API holds a vibration until changed, so a timer ends it) and trigger motors; `KeyFromVirtualKey`. Button bit values are from the documented enum and checked at compile time by a
   `static_assert` in the shell (unverified). Not done: Guide button (the system keeps it). (Battery and hot-plug identity were added later: see "Saving at suspend..." and "Controller-only play".)
5. **Files.** `XboxPaths.hpp`: `GameData` and `Shaders` map to the read-only install folder, saves/config/cache/logs to `LocalFolder`, temp to `TemporaryFolder`; `..`, absolute paths, drive letters, device names and
   trailing dots are refused; `CanWrite` is false for the install folder. `Paths::SetEnvOverride` (new, nothing calls it on the desktop) lets the shell point `%APPDATA%`-style lookups into `LocalFolder`; `Platform.cpp` `PrefPath`,
   `Achievements.hpp` and `AccountService.hpp` use it under `OKAY_XBOX_UWP`. Not done: `SaveFile.cpp` and `Prefs.cpp` still use `windows.h` calls that were not checked against the UWP allow-list (`MoveFileEx` and friends), game code that writes next to its data fails by design.
6. **Audio.** What the desktop mixer outputs to: `platform::AudioDevice` (SDL's audio on the desktop). `XboxAudio.hpp` is a `platform::AudioDevice` for UWP: a lock-free ring (queue mode) or the engine's callback (callback mode), S16/F32 in, any channel count and sample
   rate out (linear resampler), silence on underrun, paused at start, `ClearQueued`. The mixer is unchanged. `XboxWasapi.hpp`: shared-mode `IAudioClient` through `ActivateAudioInterfaceAsync`, event-driven, one native `CreateThread`, float mix format only. Not done: microphone capture, exclusive mode,
   low-latency `IAudioClient3`, spatial audio, a non-float mix format.
7. **Reduced engine.** `okay_engine_uwp` (`platform/xbox_uwp/engine/`): the source list is **read from `engine/CMakeLists.txt` at configure time** (305 engine sources, 9 crypto), minus `uwp_exclude.txt` (3: the OpenGL renderer, the Direct3D 12 renderer, the Photon adapter). It does **not** link `d3dcompiler`, `d3d12`, `dxguid`, `winhttp`, `user32`,
   `gdi32`, `opengl32`, SDL2 or libsodium; it links `WindowsApp`, `d3d11`, `dxgi`, `ws2_32`, `bcrypt`. `okay-uwp-audit` scans the 302 compiled files and their 927-file include closure for 27 forbidden-API tokens with a small three-valued preprocessor (so `#if !defined(OKAY_XBOX_UWP)` code is
   not reported); it found and I fenced: `winhttp` in `NativeHttp.hpp` (no native HTTP client under UWP: calls report "not reached"), `LoadLibraryA` and `CreateProcessA` in `AccountService.hpp` (random bytes now `BCryptGenRandom`, no curl fallback), `getenv("USERPROFILE")` in `TexturePath.hpp`. It now finds none (ctest `xbox_uwp_audit`).
   The forbidden list is from memory of Microsoft's pages: check it. Libsodium is not available: packet encryption is off and Ed25519 would use our unreviewed `okay::crypto`: this target is never a release (rule 18). 2D drawing: `SDL_Renderer` does not exist; the software painter is the plan and is **not wired** (no UI drawn).
   Not done: it has never been compiled; MSVC may reject GNU-style code in many of the 300 files; the `third_party/stb` and `ufbx` include paths and `_CRT_SECURE_NO_WARNINGS` are guesses; Winsock, `SaveFile.cpp`, `Recovery.cpp`, `Prefs.cpp`, `CoreThreads.hpp` (`CreateThread` is allowed) were not checked against the UWP allow-list.
8. **App shell.** `app/engine_shell.hpp` + `app/engine_bridge.cpp` (+ `main.cpp`, `app/CMakeLists.txt` option `OKAY_XBOX_APP_ENGINE`, off by default): wires CoreWindow events to the backend, Windows.Gaming.Input to pads, starts WASAPI, creates the renderer and presenter, loads `package\Data` through `XboxGameRuntime.hpp` (config parser tested;
   it loads the scene, adds a camera when the scene has none, steps the engine's `GameLoop` with the scene's fixed-step physics, scripts and animation, feeds pads and keys), and draws the 3D view. When there is no `package\Data\game.okayconfig` it runs the triangle test as before. Not done: the 2D/UI layer, a loading screen, error display (failures go to the log and the app exits), device-loss recovery, scene switching UI, networking checks, anything that needs a save
   framework to write outside LocalFolder.

**Desktop unchanged, what was run:** the HLSL string is byte-identical; `D3D11Renderer.cpp`, `NativeHttp.hpp`, `AccountService.hpp`, `Achievements.hpp`, `Platform.cpp` and `Paths.cpp` compile (syntax) with MinGW with and without `OKAY_XBOX_UWP`; `editor/main.cpp` compiles (syntax) with g++ with and without the define;
`ctest`-style runs of `test_xbox_uwp` (existing) and `test_xbox_uwp_runtime` (new), `okay-uwp-audit`, and the CMake source-list script. **Not run:** the full engine test suite, the editor, any Windows or console program.

### Limits to remember

UWP sandbox (no desktop-only APIs, read-only install folder, smaller budgets than a PC), Developer Mode only, no GDK, no publishing, Direct3D 11 only, the engine runs inside
the app only in the EXPERIMENTAL engine mode that has never been compiled, no hardware ever tested, no signing code of ours.

### Our own code instead of third-party libraries (2026-11-18)

Details and the table: [xbox_uwp_thirdparty.md](xbox_uwp.md#xbox-uwp-build-and-third-party-code-where-our-own-code-is-used). Short version, all UNVERIFIED on Windows and a console:

* No SDL2, Dear ImGui, Assimp, ufbx or libsodium in the UWP target (`okay-uwp-audit`: 0 findings; `ufbx.c` is in `engine/uwp_exclude.txt`).
* Models: every `OKAY_OWN_<FMT>` option that is ON in the root `CMakeLists.txt` is turned on for the UWP engine (`engine/uwp_own_readers.cmake`); FBX is always our own reader; a refused file fails with a plain message. Linux test `xbox_uwp_models` loads 17 sample files that way.
* Sound: our own WAV decoder is the WAV reader (`OKAY_OWN_WAV_DEFAULT`); OGG, MP3 and FLAC are always ours.
* Crypto: no libsodium. Packet encryption is OFF by default. `OKAY_XBOX_OWN_CRYPTO=ON` (PowerShell `-OwnCrypto`) is an experiment switch for our own unreviewed `okay::crypto`: for experiments only, do not ship. The README of every export says so and `scripts/check_release_own_stack.sh` refuses the switch in a release (with self-test cases).
* HTTP: `-Http windows-web-http` (default, https) or `-Http own` (our own client over Winsock, plain `http://` only: we have no TLS of our own yet).

## Xbox One (UWP, Developer Mode) audit

*Was `docs/xbox_uwp_audit.md` until 2026-11-20.*

Internal. Phase 1 of the experimental Xbox export (owner request 2026-11-18). Read from the code on `origin/main` (7d2ad98f) on a Linux
machine with no Windows SDK, Visual Studio or Xbox. Everything about what UWP allows is from Microsoft's public documentation as
remembered and is **unverified**: check each "not allowed" line against Microsoft's "APIs not supported in UWP apps" and
"Alternatives to Windows APIs in UWP apps" pages before relying on it. The pipeline that follows is in [xbox_uwp.md](xbox_uwp.md#xbox-one-experimental-developer-mode-export).

What this target is: a Universal Windows Platform app that an Xbox One in Developer Mode can run. It is not a native console game,
it does not use the restricted GDK, and the public SDK does not build one.

### What exists today, and what a UWP package can reuse

| Area | What we have (file) | Reusable on UWP | Not reusable / problem | Needs a UWP adapter |
|---|---|---|---|---|
| Platform layer | `okay::platform` with an SDL2 backend (default) and our own backend: `engine/src/Platform/SdlBackend.cpp`, `OwnWin32.cpp`, `OwnLinux*.cpp` (`docs/platform/platform_layer.md`) | The `Backend` interface, timers (`QueryPerformanceCounter` is allowed), the event and key types | The Win32 backend creates windows with `CreateWindowExW`, `RegisterClassExW` and a `PeekMessageW` loop, and a WGL OpenGL context (`opengl32.dll`). UWP has no `HWND`, no message loop and no OpenGL. SDL2 as built here is the desktop build; SDL's UWP (WinRT) port is a separate third-party product we do not use (rule 19a) | A third backend `own-uwp` on `CoreWindow` + `CoreDispatcher` |
| Rendering abstraction | `Renderer.hpp` and four 3D back ends that all return pixels: `SoftwareRenderer` (CPU), `GLRenderer` (OpenGL), `D3D11Renderer` (isolated device, offscreen target, **read back to RGBA, then shown through `SDL_Renderer`**), `D3D12Renderer` (same contract, preview). WebGL is the Emscripten build of the player. | `SoftwareRenderer` is plain C++ and works anywhere. The D3D11 renderer's pipeline objects, HLSL and draw code are the closest thing to a UWP renderer | **`D3DCompile`** is used at run time (`D3D11Renderer.cpp:698`, `770`, `785`, `engine/CMakeLists.txt` links `d3dcompiler`); runtime HLSL compiling is not available to UWP apps, the shaders must become `.cso` at build time. No OpenGL on UWP or Xbox, so `GLRenderer` and WebGL are out. The "render offscreen, read back, upload to an SDL texture" design costs a GPU stall per frame and has no swap chain, so it cannot present to a `CoreWindow`. `D3D12Renderer` is a labelled preview | A present path: swap chain on the `CoreWindow` (`CreateSwapChainForCoreWindow`) fed by the D3D11 renderer directly, and offline shader compilation |
| 2D and UI | `okayui` (`SDL_Renderer` toolkit, software painter `okay_ui_soft`, a D3D11 backend `OkayUI_D3D11.cpp`); the player draws its HUD through `SDL_Renderer` (about 280 SDL drawing words in `player/main.cpp`) | The software painter and the D3D11 painter (no SDL types) | `SDL_Renderer` is the main blocker: the player cannot draw 2D without an `SDL_Window` | Present the software painter's pixels through the swap chain (works, slower), later the D3D11 painter |
| Player | `player/main.cpp` (2,973 lines): platform seam for window, events, input, audio, timers; SDL2 for 2D | The game loop (`GameLoop`), scene loading, ECS, physics, scripting, all in `okay_engine` | `main()` is a desktop `main`; `okay_engine` links `ws2_32 winhttp d3d11 d3dcompiler d3d12 dxgi dxguid` and pulls in desktop code (anti-cheat hardware checks, crash handler with `dbghelp`, `LoadLibrary`) that cannot all be compiled for UWP | A UWP `App` shell (written: `platform/xbox_uwp/app`, draws a triangle only). Linking `okay_engine` into a UWP exe is not attempted yet |
| Export and packaging | `builder::Build` / `ExportForPlatform` in `editor/main.cpp` (Desktop folder + zip, Web and Android as data + script + README, Server); `scripts/build-windows-dist.sh`; NSIS installer | The data half: scenes, config, stripped assets are already platform-independent (`ExportForPlatform` writes them for Web and Android) | No UWP target, no manifest, no package format (`.appx` / `.msix`), no signing | Written in this branch: manifest, tile images, own `.appx` writer, SDK check, build script |
| Input | `platform::Gamepad` over XInput (own Win32: `LoadLibraryA("xinput1_4.dll")` in `OwnWin32.cpp:505`) or SDL controller; `Input/PlatformGamepad.hpp` | The engine side (`PadSet`, actions, rumble model) is backend-independent | `LoadLibraryA` is not allowed in UWP (`LoadPackagedLibrary` is the rule). XInput 1.4 is documented as available to UWP, but `Windows.Gaming.Input` is the Xbox-native API and is what the app shell uses. Keyboard and mouse: only through `CoreWindow` events | Gamepad adapter over `Windows.Gaming.Input.Gamepad` (written in the app shell as a reader, not yet behind `platform::Gamepad`) |
| Audio output | `platform::AudioDevice` (queue or callback) on SDL's audio; our own Linux backend (ALSA path); the own Win32 backend has no audio (no WinMM or WASAPI call found in `OwnWin32.cpp`) | The mixer (`AudioMixer::RenderStereo`) produces plain 16-bit stereo | SDL's WinMM/WASAPI backend is a desktop one. UWP needs WASAPI through `ActivateAudioInterfaceAsync` (no `IMMDeviceEnumerator`) or XAudio2 | A UWP audio device (WASAPI render client in shared mode, event driven) |
| Files, saves, assets | `Core/Paths.cpp` (`APPDATA`, `LOCALAPPDATA` from the environment), `Platform.cpp` (`APPDATA`), `SaveFile.cpp` (atomic replace using `<io.h>`), `Core/Prefs.cpp`, `Achievements.hpp` (`LOCALAPPDATA`) | `std::filesystem` and `fstream` work inside the app's own folders | The install folder (`Package.Current.InstalledLocation`) is **read-only**; writes go to `ApplicationData.LocalFolder`. The environment variables are not a reliable source of those folders in UWP. `_commit`/`MoveFileEx` style replace code needs the allowed variants (`MoveFileExW` is available, `LockFile` style tricks may not be). Game data in `package/Data` is read-only: a game that writes beside its data fails | One function that returns the save/config folder from `ApplicationData`, used by `Paths`, `Platform`, `SaveFile` |
| Networking | `Net/UdpSocket.cpp` (Winsock), `NativeHttp` (WinHTTP), `NetCrypto` (libsodium or own crypto) | Winsock UDP/TCP (allowed with the `internetClient` capability; local-network use also needs `privateNetworkClientServer` and loopback is blocked for UWP), our crypto, `BCryptGenRandom` | WinHTTP availability to UWP is **unverified** (the supported pair is `Windows.Web.Http`); a dedicated server cannot run as a UWP app | Probably none for UDP; an HTTP adapter if WinHTTP is refused |
| Threads | `Core/CoreThreads.hpp` (native `CreateThread`) | Allowed (the Windows build already avoids `<thread>`, rule 21a) | Thread budgets on a console app are small | none |
| Anti-cheat, crash handler, Steam, launcher features | `shared/AntiCheat*.hpp`, `shared/CrashHandler.hpp` (`dbghelp`, `LoadLibrary`), Steam | none | Desktop-only; keep out of the UWP build (`#if`) | none (excluded) |

### Concrete compatibility problems (ranked)

1. **Window and loop.** Every window, event and GL context call in `OwnWin32.cpp` is Win32-only. SDL2 is the desktop build. Needs a `CoreWindow` backend.
2. **Run-time HLSL compiling.** `D3DCompile` is used by `D3D11Renderer` (and the `D3DPostHLSL` post effects). UWP forbids it; every shader has to be compiled to bytecode when the project is built. The `.cso` files cannot be made on Linux here.
3. **No swap chain.** The D3D11 renderer renders offscreen and reads back. A UWP window needs a swap chain on the `CoreWindow`.
4. **`SDL_Renderer` everywhere in 2D.** About 280 words in the player, 250 in the editor, 107 in `okayui`. The software painter is the only SDL-free path today.
5. **`okay_engine` is one big desktop library.** It links `d3d12`, `d3dcompiler`, `winhttp`, uses `LoadLibraryA` (XInput, GL), `getenv("APPDATA")`, `dbghelp`. A UWP build needs a reduced target and `#if` fences; this was not attempted.
6. **Writable paths.** The package folder is read-only; saves and config must move to `LocalFolder`.
7. **Audio.** No UWP-capable device exists.
8. **Gamepad.** `LoadLibraryA` for XInput; the Xbox-native API is `Windows.Gaming.Input`.
9. **Sandbox and budgets.** UWP apps on a console run in a sandbox with a smaller memory and CPU budget than a PC and Direct3D 11 (feature level 11_x) as the graphics API; the exact Developer Mode numbers are on Microsoft's "UWP on Xbox One" pages and were not checked.
10. **MinGW cannot build UWP.** Our release is cross-compiled with MinGW (win32 threads). UWP needs the MSVC toolset with the Windows SDK's WinRT headers and import libraries. So this target cannot come out of `build-windows-dist.sh`.

### Parts that need a UWP adapter (and their state on `claude/xbox-uwp-2`)

| Adapter | State |
|---|---|
| App shell: `CoreApplication`, `IFrameworkView`, suspend and resume, `CoreWindow`, swap chain, clean exit | triangle shell written, **uncompiled**; engine mode (`app/engine_shell.hpp` + `app/engine_bridge.cpp`) written, **uncompiled** |
| Window, lifecycle, events | **done as pure code** (`XboxUwpBackend.hpp`, `XboxLifecycle.hpp`), tested on Linux with a fake host; the CoreWindow glue is uncompiled |
| Gamepad through `Windows.Gaming.Input` | **behind `platform::Gamepad`** (`XboxInput.hpp`), mapping tested on Linux; the WinRT reader is uncompiled |
| Offline shader compile | **done** (`ShaderBundle.hpp`, `okay-shaders`, `xbox-compile-shaders.ps1`); the renderer loads `.cso` under `OKAY_XBOX_UWP`; fxc never run |
| Present path from `D3D11Renderer` | **written** (`XboxPresent.hpp`, over the existing `RenderToTexture`); syntax-checked only |
| Save folder | **done as pure code** (`XboxPaths.hpp`, `Paths::SetEnvOverride`); engine users fenced; `SaveFile`/`Prefs` unchecked |
| Audio | **written** (`XboxAudio.hpp`, `XboxWasapi.hpp`); conversion tested on Linux; WASAPI syntax-checked only |
| 2D painting | **not started** (the software painter has no present path) |
| Reduced engine target | **written** (`okay_engine_uwp`), audited for forbidden API names on Linux, never compiled |
| Package manifest, tile images, packer, signing steps | written, tested on Linux (manifest, images, packer); signing is Microsoft's SignTool on Windows, started from the editor |
| One-click build | written (`XboxBuildPlan.hpp`...), plan and runner tested on Linux; the spawned programs never ran |

Problems 2 to 8 in the ranked list above now have code. None of it is verified on Windows. What stays open: compiling the engine with Microsoft's compiler, the UI layer, device-loss recovery, WinHTTP-or-`Windows.Web.Http`, libsodium for UWP, every real run.

### Update 2026-11-18: first real Windows run

The triangle test package was built, packed with our packer, accepted by MakeAppx `unpack`, signed, installed and started on a Windows 11 PC (Visual Studio 2026, MSVC 14.52.37025, toolset v145, Windows SDK 10.0.26100.0) and showed the triangle through a Direct3D 11 flip-model swap chain on a CoreWindow.
That confirms, for the triangle shell only: the compile with C++/WinRT (C++20, no `/ZW`, no `/await`), the manifest and package layout, the packer, signing and install. It confirms nothing about problems 1, 2, 4 to 8 above for the engine (they are untouched by that run), and nothing about a console. See `docs/platform/xbox_uwp.md`, "What is verified on Windows".

### Third-party code in the UWP target (2026-11-18, own-code round)

The reduced engine target now carries no third-party library: SDL2, Dear ImGui, Assimp, ufbx, libsodium, libcurl and OpenSSL are not compiled or included, and the audit
scanner lists their headers (`sodium.h`, `imgui.h`, `assimp/`, `ufbx.h`, `curl/curl.h`, `openssl/`, `stb_*`, `dr_*`) as forbidden. Current result: 313 sources, 4 excluded
(`ufbx.c` is the new one), 309 scanned, 949 files in the include closure, 0 findings. The table of what replaces what, and what is still blocked, is
[xbox_uwp_thirdparty.md](xbox_uwp.md#xbox-uwp-build-and-third-party-code-where-our-own-code-is-used). Crypto is the one honest exception: SHA-256, random bytes and Ed25519 checks are our own unreviewed `okay::crypto` code (never a release).

## Xbox UWP build and third-party code: where our own code is used

*Was `docs/xbox_uwp_thirdparty.md` until 2026-11-20.*

Owner request 2026-11-18: see if the experimental Xbox UWP build can run on our own replacement code. This page says, item by item, what the
UWP target uses today, which own replacement it has, how far that is wired, and what still blocks it. Everything here is
checked on Linux only (text scanner, syntax sweeps with MinGW, unit tests); **nothing was compiled with Microsoft's compiler or run on
Windows or a console: UNVERIFIED.** It follows CLAUDE.md rules 17, 18 and 19a. The rest of the Xbox story is in [xbox_uwp.md](xbox_uwp.md#xbox-one-experimental-developer-mode-export).

### The table

| Item | What the UWP target uses today | Our own replacement | Status in the UWP build | What blocks it on UWP |
|---|---|---|---|---|
| **SDL2** (window, input, audio, 2D) | Nothing. The UWP files (`UwpPlatform.cpp`, `XboxUwpBackend.hpp`, the shell) use `CoreWindow`, `Windows.Gaming.Input`, WASAPI and the swap chain | `okay::platform` seam with the UWP backend (reports kind "own") | **Done.** `okay-uwp-audit` reports no SDL include or `SDL_` word in the 309 compiled engine sources and their 949-file include closure (the scanner also lists `SDL.h` and `SDL_` as forbidden) | Nothing for SDL. The window, gamepad and audio backends themselves are unverified |
| **Dear ImGui / okayui toolkit** | Nothing. The bridge draws the 2D / UI layer with the software painter and blends it over the 3D frame | `XboxSoftPainter.hpp` (premultiplied RGBA) over the engine's `okay::platform::Painter` seam; D3D11 overlay shader `PSOverlay` | **Done** for the engine's own UI (`okay::ui2d`). The scanner forbids `imgui.h` | The editor and launcher still use ImGui, but they are not in the UWP target. Pixel match with the desktop painter is unverified |
| **Models: Assimp** | Not linked, `OKAY_HAVE_ASSIMP` never defined | Our own readers: OBJ, glTF/GLB, FBX, Collada, 3DS, BVH, STL, PLY, OFF, SMD, MD2/MD3/MD5, MS3D, ASE, Blender, 3MF, AMF, X3D, DXF, LWO/LXO/LWS, B3D, X, AC3D, OGRE, XGL, IRR, NFF, Terragen, Q3D, IQM, COB, MDC, HMP, CSM, PMX, ZAE, XML | **Done.** `engine/uwp_own_readers.cmake` reads the root `CMakeLists.txt` and turns every `OKAY_OWN_<FMT>` option that is ON into `OKAY_OWN_<FMT>_DEFAULT` for `ModelImport.cpp` (38 reader switches today, FBX is forced on separately; none edited by hand). A file our reader refuses fails with a plain log line and `ok=false` | Formats we have no reader for (`.ndo` is a seam that refuses every file; anything else Assimp read) cannot be loaded. Real-world files our readers reject (see `docs/content/model_corpus.md`) cannot fall back to Assimp |
| **Models: ufbx** | Not compiled: `third_party/ufbx/ufbx.c` is in `engine/uwp_exclude.txt`; the `ufbx.h` include and every ufbx call in `ModelImport.cpp` are compiled out under `OKAY_XBOX_UWP`; `okay::fbx::OwnReaderEnabled()` is always true there (the `OKAY_OWN_FBX=0` switch cannot select ufbx) | `okay::fbx` (`FbxReader.cpp`, `FbxScene.cpp`) | **Done.** If our FBX reader refuses a file the log says "the own FBX reader could not read this file (the Xbox build has no other FBX reader)" | Odd FBX files that only ufbx reads fail here |
| **Audio files** | `AudioClip::LoadWAV` goes through our WAV decoder (`OKAY_OWN_WAV_DEFAULT=1` for the UWP engine; PCM, float, A-law, mu-law, MS and IMA ADPCM). OGG Vorbis, MP3 and FLAC always go through `VorbisDecoder.cpp`, `Mp3Decoder.cpp`, `FlacDecoder.cpp` | `okay::wav`, Vorbis, MP3, FLAC decoders (ours) | **Done in code.** No `SDL_LoadWAV`, `SDL_mixer`, `stb_vorbis` or `dr_*` is reachable (scanner tokens `stb_vorbis.c`, `dr_wav.h`, `dr_mp3.h`). Output is `XboxWasapi.hpp` through the `AudioDevice` seam | Sound on a device is unverified; the own WAV decoder is OFF by default in desktop builds and only on here |
| **Crypto: libsodium** | Not linked. See the next section | `okay::crypto` (`engine/src/Crypto`) | **Packet encryption OFF by default; hashes, random bytes and Ed25519 checks use `okay::crypto`.** Not independently reviewed | Rule 18: own crypto cannot be trusted on our word and is never in a release |
| **HTTP: libcurl, WinHTTP** | No native HTTP client in `NativeHttp.hpp` under `OKAY_XBOX_UWP` (calls say "not reached"). The content-delivery transport is `Windows.Web.Http` (`app/xbox_winhttp.hpp`) | `okay::ownhttp` (HTTP/1.1, our code). Here: `XboxOwnHttp.hpp`, own client over Winsock | **Choice at build time:** `OKAY_XBOX_HTTP=windows-web-http` (default, https works) or `own` (plain `http://` only). Both UNVERIFIED | We have **no TLS of our own** (`okay::ownhttp` uses the system OpenSSL loaded at run time, which does not exist in a UWP app). So `own` refuses every `https://` URL; it is only for a LAN or local test server. A CDN needs `windows-web-http` |
| **Fonts** | Overlay and UI text use our TTF rasteriser (`TtfFont.cpp`, `TtfRaster.cpp`) and the embedded fonts in our OKF1 container (`EmbeddedFont.cpp`, `shared/*FontOwn.h`) plus the built-in bitmap font (`Font.cpp`) | Ours (code). The font files are assets with licences (`docs/status/THIRD_PARTY.md`) | **Done in code.** `stb_truetype.h` is a forbidden token and is not reachable | The Roboto licence text is still missing in the repository (asset record in `docs/status/THIRD_PARTY.md`, the owner's call) |
| **Images** | `ImageDecode.cpp`, `ImageDecodeJpeg.cpp`, `Image.cpp` (PNG, JPEG, writer) | Ours | **Done in code.** `stb_image.h` is a forbidden token and is not reachable | Nothing known; decoding on Windows is unverified |
| **Release folder `xbox-src`** | `build-windows-dist.sh` copies `platform/xbox_uwp`, `engine/include`, `engine/src`, `engine/CMakeLists.txt`, `shared/` and now the root `CMakeLists.txt` (the UWP engine reads it for the reader defaults) | | **Done.** `engine/third_party` is no longer shipped (nothing in it is compiled for UWP) and the script stops if it, the root `CMakeLists.txt` or `uwp_own_readers.cmake` is wrong | |

### Crypto, plainly (rule 18)

* **libsodium does not exist in the UWP build.** `NetCrypto.hpp` picks `okay::crypto` only with `OKAY_OWN_CRYPTO`, libsodium only with `OKAY_HAVE_SODIUM`, and neither
  means no packet encryption (`OKAY_NET_CRYPTO` undefined). The UWP target defines neither by default, so `sodium.h` is never included.
* What `okay_crypto_uwp` still contains and uses on UWP: SHA-256 and SHA-512, ChaCha20, X25519, Ed25519, AEAD and the random-bytes function (`BCryptGenRandom`
  with the system RNG, which UWP allows). They are always our own code: file checksums and update-signature checks call them directly
  (`shared/AntiCheatCrypto.hpp`, `shared/UpdateSignature.hpp`). **This is own crypto, not independently reviewed.**
* **The explicit choice:** `OKAY_XBOX_OWN_CRYPTO` (CMake option of `platform/xbox_uwp/engine`, default **OFF**; PowerShell `-OwnCrypto`). ON defines
  `OKAY_OWN_CRYPTO`, so packet encryption works through `okay::crypto`. CMake then prints a warning and the PowerShell script prints and logs
  "own crypto, not independently reviewed". It is for experiments only: do not ship.
* **Visible:** the export `README_XBOX.txt` says "own crypto, not independently reviewed; for experiments only; do not ship", names the switch and says libsodium is absent.
* **A release can never carry it.** `scripts/check_release_own_stack.sh` (run by `build-windows-dist.sh`, `release.yml`, `dev-build.yml`) now refuses
  `OKAY_XBOX_OWN_CRYPTO` set to anything but OFF in the environment, in a `-D` argument (also `-D NAME=ON`) or in a CMake cache, exactly like `OKAY_OWN_CRYPTO`.
  Its self-test (`ctest -R release_refuses_own_crypto`) has cases for each of the three. An own-crypto compile definition in a release tree was already refused.

### HTTP choice

| `OKAY_XBOX_HTTP` | Client | https | Where |
|---|---|---|---|
| `windows-web-http` (default) | `Windows.Web.Http` through `app/xbox_winhttp.hpp` | yes | the expected path |
| `own` | `okay::ownhttp` over a Winsock TCP stream (`ws2_32` is linked; `internetClient` is in the manifest) | **refused** with "this build has no TLS of its own" (never sent in the clear) | `XboxOwnHttp.hpp` (`PlainConnector`, `OwnHttpClient`, `WinsockDialer`) |

The pure parts (the choice, the connector, the `http::Client` adapter, the Range header, redirects, an https refusal) are tested on Linux with an in-memory TCP
in `tests/test_xbox_uwp_runtime.cpp` (`OwnHttpTests`). The Winsock dialer compiles only under `_WIN32` and has **never been compiled with Microsoft's compiler or run**.
Written from RFC 9112, the Microsoft Winsock pages and our own `okay::ownhttp`; no outside code.

### How this was checked, and how to repeat it

* Source scan: `okay-uwp-audit .` (ctest `xbox_uwp_audit`): 313 engine sources, 4 excluded (`GLRenderer`, `D3D12Renderer`, `PhotonTransport`, `ufbx.c`), 309 scanned,
  949 files in the include closure, 0 findings. New forbidden tokens: `sodium.h`, `imgui.h`, `assimp/`, `ufbx.h`, `curl/curl.h`, `openssl/`, `stb_*`, `dr_wav.h`, `dr_mp3.h`.
* Models: ctest `xbox_uwp_models` compiles `ModelImport.cpp` and `FbxScene.cpp` the way the UWP engine does (`OKAY_XBOX_UWP`, the 38 reader defaults, no `ufbx.h`,
  no Assimp) and loads 17 sample files (OBJ, STL, PLY, 3DS, OFF, SMD, MD5, MS3D, ASE, Collada, Blender, FBX binary and ascii, GLB) through them, checks that
  `OKAY_OWN_FBX=0` does not switch FBX away from our reader, and that garbage under six extensions fails cleanly.
* Syntax sweep: the 310 UWP sources (309 engine sources plus `Painter.cpp`) with MinGW `x86_64-w64-mingw32-g++-win32 -fsyntax-only -DWINAPI_FAMILY=2` (a proxy for the UWP headers, not Microsoft's compiler).
* Crypto guard: `scripts/check_release_own_stack.sh --self-test`.

### Written from

`platform/xbox_uwp/XboxOwnHttp.hpp`: RFC 9112 and Microsoft's Winsock documentation. `engine/uwp_own_readers.cmake`, the CMake parts of `engine/CMakeLists.txt`,
`tests/test_xbox_uwp_models.cpp`, the fences in `ModelImport.cpp` and `FbxScene.cpp`, the scanner tokens and the guard cases: our own repository code, from the
repository's own options. No code was copied from another project and no library was added.

### Still not done

* Everything above on a real Windows build and a console (UNVERIFIED).
* Round 4 (`claude/xbox-uwp-4`), pure logic with Linux tests (`xbox_uwp_pieces`), written from Microsoft's public documentation and keyboard layout charts; the Windows glue that would use them is NOT written and nothing ran on Windows or a console: `XboxPendingSave.hpp`, wired into `XboxGameRuntime` (`Pending().Request`; `Frame()` runs it next frame, `OnSuspend` adds a "pending saves" step after the settings; a "save next frame" request is forced through on the Suspending event inside the deadline budget; a write whose last measured time is longer than the time left is not started and is reported, failed or skipped writes stay pending), `XboxSplash.hpp` (splash until the first frame and a minimum time, then fade; stall report), `XboxKeyLayout.hpp` (physical key to character for US, French, German, Swedish / Finnish and Russian; inverse `KeyForChar`, key names, language tag to layout; dead keys are not composed; Norwegian, Danish, Swiss and Canadian layouts fall back to US), `XboxClipboard.hpp`, now the clipboard of `UwpBackend` (`SetClipboardHost`; was an in-app string) (`okay::Clipboard` over an async host: never blocks, drops a read that started before our own copy, says when the text is in-app only). The Input Field's `focusable` is now saved in scenes (old scenes load as focusable).
* Still reachable without a console but not done: wiring the splash and the key layouts into the shell and backend (`engine_shell.hpp`, `UwpPlatform`; `KeyForChar` still assumes US), the real `Clipboard` / `GetTextAsync` glue, microphone capture.
* Needs real hardware: everything in "Saving at suspend, settings, rumble and battery, typing with a pad" above (keyboard events on a console, battery reports, suspend timing), title-safe on a real TV, the Guide button (the system keeps it).
* Needs the owner's developer account: GDK features (not used: UWP Developer Mode only).
* Our own TLS (needed before `own` HTTP can talk to a CDN) and an independent review of `okay::crypto` (rule 18, owner decided not to hire one).
* A fallback for model files only Assimp or ufbx read: by design none on UWP; the fix is a better own reader (`docs/status/own_code_roadmap.md`).
