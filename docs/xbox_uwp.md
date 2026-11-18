# Xbox One (experimental, Developer Mode) export

Internal. **EXPERIMENTAL and UNVERIFIED.** Owner request 2026-11-18: a hobby target, not for publishing. Branches `claude/xbox-uwp-1` (packaging) and `claude/xbox-uwp-2` (engine inside UWP, one-click build; this page was updated there).
The audit that led here is [xbox_uwp_audit.md](xbox_uwp_audit.md).

## What is verified on Windows (2026-11-18) and what is not

**Verified by the owner on a Windows 11 PC** with Visual Studio 2026 (Developer Command Prompt v18.12.1, MSBuild 18.12.3), MSVC 14.52.37025 (cl 19.52.37029), platform toolset **v145**, Windows SDK 10.0.26100.0, command
`scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test4 -MinimalTest -SkipInstall` at branch head `422d23b7`:
* The SDK check, the minimal test export, our packer tool, CMake setup (`-G "Visual Studio 18 2026" -T v145,version=14.52.37025`) and the UWP app compile and link all passed. On Visual Studio 2026 the working toolset was **v145 / 14.52**, the one that has the UWP libraries (`lib\x64\store`); the vcxproj check printed
  `LanguageStandard stdcpp20`, no `/ZW` and no `/await`.
* Our own `.appx` (`com.okayspace.test_1.0.0.0_x64.appx`, 40 files: manifest, block map, 14 tile PNGs, `Data\README.txt`, `OkaySpaceXbox.exe`, 17 engine shader files, `tri_ps.cso`, `tri_vs.cso`) was **accepted by Microsoft's MakeAppx `unpack`** (exit code 0, "Package extraction succeeded").
* SignTool signed it with the test certificate `CN=OkaySpaceTest`; `Import-Certificate` into `LocalMachine\TrustedPeople` and `Add-AppxPackage` installed it without error. The installed app "OkaySpace Test" **started on Windows 11 and showed the colour-gradient triangle on a dark background in a normal window**
  (a Direct3D 11 flip-model swap chain on a CoreWindow works there).
* The `Microsoft.VCLibs` install line failed with **0x80073D02** because that framework was in use by running apps (Store, Widgets, Gaming Services). That is harmless: the already installed framework satisfied the dependency. The build script now prints that line as **optional** ("only if the next step reports a missing dependency").

**Still NOT verified:** gamepad input (green background, stick, A, B), suspend and resume, anything on an **Xbox console** (no claim of console support is made), engine mode (running an exported game), the editor's one-click button and its log panel, the MSBuild AppX validation fix beyond this run (the placeholder manifest needed `mp:PhoneIdentity`), shader loading from `package\Shaders` at run time (the engine renderer is not used by the triangle test), the WASAPI audio code, the signing/trust steps on a console.

## What this is, and what it is not

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

## One click (owner request 2026-11-18): the build button runs everything

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

## Where everything is

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

## What you need on the Windows PC (written from Microsoft's pages; check them)

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

## Engine mode, round 2 (2026-11-18, evening): what is new, what the owner runs next (ALL UNVERIFIED)

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

## Engine mode, round 3: UI layer, device loss, suspend save, HTTP (2026-11-18, late; ALL UNVERIFIED)

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

## One command, no editor needed (`-MinimalTest`)

From a source checkout (or the `xbox-src` folder of a release), in PowerShell, with Visual Studio's UWP tools installed:

    powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot . -ExportDir .\xbox-test -MinimalTest -SkipInstall

`-ExportDir` may be a folder that does not exist yet. `-MinimalTest` compiles `okay-xbox-export` (cl.exe, using the `vcvars64.bat` of whichever Visual Studio the SDK check found) and writes a minimal TEST export:
`package\AppxManifest.xml` (publisher `CN=OkaySpaceTest`, version 1.0.0.0), 14 placeholder tile PNGs (labelled placeholders in `PLACEHOLDER_ASSETS.txt`), `README_XBOX.txt`, a `package\Data` with only a note (so the app shows its triangle).
Then the normal steps run, printed as `[n/9]` lines: tool check, export, our packer, CMake setup, UWP compile, copy, offline shaders, pack and check, test certificate and SignTool. `-SkipInstall` is accepted and does nothing (this script has no install step;
the editor's one-click build has the optional install). `-SkipSign` stops before signing. On any failure the script prints the step name, the **last 15 lines** of the failing tool's output and the path of **`<ExportDir>\build.log`** (the full log, paste it to us).
The tool's arguments (also testable alone): `okay-xbox-export --out <folder> [--name] [--company] [--version] [--bundle-id] [--icon]`. Tested on Linux: argument parsing and the folder layout (14 PNGs, manifest with the right publisher and version, folder created from nothing,
a bad version refused). **Never run:** the PowerShell script (re-read by hand for quoting, `$LASTEXITCODE`, `Resolve-Path` on a missing folder and stderr under `$ErrorActionPreference = 'Stop'`), cl.exe on the tool (it compiles four engine image files; MSVC has not seen them).

## First real Windows build (Visual Studio 2026, MSVC 14.52) and the fixes it forced

The owner's first build log showed errors that no Linux check could catch. Fixed, all still **unverified until the next Windows run**:
* `STL1011` (`<experimental/coroutine>` removed soon): C++/WinRT in C++17 mode uses experimental coroutines. The triangle shell now builds as **C++20**; engine mode stays C++17 (engine headers use `u8string()`, which is `std::u8string` in C++20) and silences the error with
  `_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS` (also set in the C++20 build). `/await` and `/ZW` are removed from the flags and the build script logs what the generated `vcxproj` really contains (`/ZW`, `/await`, `LanguageStandard`).
* `Windows::...` was ambiguous with the global `::Windows` while `using namespace winrt;` is active: every use is now `winrt::Windows::...`, with the includes `Windows.Storage.h`, `Windows.ApplicationModel.Activation.h`, `Windows.UI.Input.h` and `::IUnknown` written with the global scope.
* Platform toolset: a newer Visual Studio's default toolset (v145 in 2026) can differ from the toolset that has the UWP libraries (v143 = MSVC 14.30-14.49). CMake gets `-T v143,version=<MSVC folder with lib\x64\store>` (14.50-14.59 = v145, otherwise only `version=`); the folder with the UWP libraries is the one picked when several toolsets are installed. The mapping is from memory: UNVERIFIED.
* MSBuild wants a `Package.appxmanifest` for a WindowsStore executable: `platform/xbox_uwp/app` has a minimal stand-in (and three 1x1 images) and MSBuild is told not to sign or package; our packer writes the real package.
* VC runtime: the app uses the dynamic runtime, so the generated manifest keeps the `Microsoft.VCLibs.140.00` dependency. **Static CRT (`/MT`) was not chosen** because UWP support for it is unverified; instead the build script prints the exact `Add-AppxPackage` line for the framework package found under `ExtensionSDKs\Microsoft.VCLibs` (administrator PowerShell, once), then the certificate import and the package install. Debug is not supported (warning).
* Tools read arguments as UTF-8 through `wmain` (non-ASCII user folders), batch files are written as UTF-8 with `chcp 65001`, and the shader script builds its helper through a `.bat` (it failed on paths with spaces before).
* On failure the script adds a hint when the text contains `v145`, `ZW`, `appxmanifest` or `VCLibs`.

## The pipeline

1. **Editor:** File > Build Settings, platform **Xbox (exp.)**, set name, version and icon (Player page), Export. It writes
   `<out>\package\AppxManifest.xml`, `package\Assets\*.png`, `package\Data\` (your game's data through the existing export code), `README_XBOX.txt`,
   `xbox-export.json` and, if there is no icon, `PLACEHOLDER_ASSETS.txt` (the images are then generated stand-ins, not final art).
2. **PC:** `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <OkaySpace source> -ExportDir <out>` runs the SDK check,
   compiles `okay-appx` with `cl.exe`, builds the UWP app (`cmake ... -DCMAKE_SYSTEM_NAME=WindowsStore`), compiles the two shaders to `.cso` (build time; `D3DCompile`
   does not exist inside UWP), copies the exe and shaders into `package\`, runs `okay-appx pack` then `okay-appx verify`, makes a test certificate with
   `New-SelfSignedCertificate`, and signs with `SignTool sign /fd SHA256 /sha1 <thumbprint>`. With `-SkipSign` it stops after the unsigned package.
3. **Console:** see "Deploy".

## Our own packer: what it writes and which rules could not be checked

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

## Signing: honest assessment

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

## Compile step: what it would take for okay-ms to build the app later

For now **Microsoft's UWP compiler builds the app** (`cl.exe` through CMake and MSBuild). `tools/okayms/` (`docs/own_msvc_plan.md`) is our MSVC-style toolchain; it cannot do this today. What it would take,
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

## Deploy to an Xbox in Developer Mode (reported steps, unverified)

1. On the console, activate Developer Mode (Dev Mode Activation app). It restarts into the Dev Home screen, which shows the console's address.
2. On the PC, open the **Windows Device Portal** of the console in a browser at the address Dev Home shows (HTTPS; the usual port reported for Xbox is 11443). Accept the certificate warning.
3. Apps > **Add** (the page for uploading an app package): choose the signed `.appx`, add the `Microsoft.VCLibs.140.00` framework package as a dependency (the file is in Visual Studio's
   `Common7\IDE\Extensions\Microsoft\...\Appx` folders or the Visual C++ runtime download page for UWP), and install.
4. Start the app from Dev Home / the console's "Dev Mode" apps. Expected: a dark window with a coloured triangle; the background turns green once a pad is connected; the left stick moves the triangle; B exits.
5. Whether a self-signed test certificate is accepted by the console without trusting it first is **unknown**. Microsoft's usual route is Visual Studio's "Remote Machine" deployment; if the Device
   Portal refuses the package, try that route and report what it said.

## Console test plan (PREPARED, NOT STARTED, UNVERIFIED), in plain words

Goal: see the same triangle on an Xbox One. Nothing here has been tried on a console; every step comes from Microsoft's public pages and may differ. Do not start until you have the signed package from the PC steps above.

1. **Turn on Developer Mode.** On the Xbox, install the free **Dev Mode Activation** app from the Store, sign in with the Microsoft account you registered as a developer, and follow it. The console restarts into a **Dev Home** screen. (Microsoft's page "Activate Developer Mode on your Xbox" is the reference; account rules and cost are on that page. Dev Mode is a separate mode: your normal games are not available while it is on.)
2. **Find the console's address.** Dev Home shows the console's IP address. Write it down. The console and your PC must be on the same network.
3. **Open the Device Portal.** On the PC, open a browser and go to `https://<that address>:11443` (the usual port reported for Xbox; if it does not load, use the exact address and port Dev Home shows). The browser warns about the certificate: accept it. You may be asked for a PIN shown on the console.
4. **Upload the package.** In the portal choose **Apps** (or "My games & apps") > **Add** (Install app). Pick the signed `.appx` from your export folder. If the page offers **dependencies**, add the VC runtime package `Microsoft.VCLibs.x64.14.00.appx` (the build script prints where it is under Program Files (x86) > Microsoft SDKs > Windows Kits > 10 > ExtensionSDKs > Microsoft.VCLibs). Press **Start/Go**.
5. **Trust problem?** Whether the console accepts a package signed with our self-made test certificate is **unknown**. If the portal refuses it, write down the exact message; Microsoft's usual route is Visual Studio's "Remote Machine" deployment, which we have not set up.
6. **Start the app.** In Dev Home open the list of Dev Mode apps and start **OkaySpace Test**. You should see a dark window with a coloured triangle. If you connect a pad: the background turns green, the left stick moves the triangle, A centres it, B closes the app.
7. **Send back:** a photo or description of the screen at each step, the exact text of any error (the portal's message, or the console's error code), the console model (One, One S, One X), the Dev Mode / OS version shown in Dev Home, and whether the pad worked. Do not send passwords, codes or the contents of certificates.
8. **Then:** suspend and resume (press the Xbox button and come back), and only later try "engine mode" on the PC first.

## Verify it yourself on a Windows PC (no Xbox needed for most of it)

A UWP package also installs on Windows 10/11, so steps 1 to 6 test the whole pipeline without a console.

1. Install Visual Studio 2022 with the pieces above.
2. `powershell -ExecutionPolicy Bypass -File scripts\xbox-check-sdk.ps1` . Paste the whole output.
3. Start the editor, File > Build Settings, choose **Xbox (exp.)**. Paste what the Xbox page says (or a photo; screenshots stay out of the repo). Press Export. Tell us whether `package\` has the manifest and 14 images.
4. `powershell -ExecutionPolicy Bypass -File scripts\build-xbox-uwp.ps1 -SourceRoot <source folder> -ExportDir <export folder>`. Paste the whole output, including the `okay-appx` lines and the MakeAppx unpack exit code (0 means Microsoft's own tool read our container).
5. As administrator: `Import-Certificate -FilePath <out>\OkaySpaceXbox-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople`, then `Add-AppxPackage <out>\<name>.appx`. Paste any error text.
6. Start the app from the Start menu. Tell us what you see (expected: the triangle) and whether a pad moves it.
7. Only with a console in Developer Mode: the Deploy steps. Tell us exactly what each page showed.

Run `ctest -R xbox_uwp` (needs `-DOKAY_BUILD_XBOX_UWP=ON -DOKAY_BUILD_TESTS=ON`) to run the Linux tests anywhere.

## The engine inside UWP (branch `claude/xbox-uwp-2`): what was done, from what, and what is not

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
   `static_assert` in the shell (unverified). Not done: Guide button (the system keeps it), battery, hot-plug identity beyond an instance id.
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

## Limits to remember

UWP sandbox (no desktop-only APIs, read-only install folder, smaller budgets than a PC), Developer Mode only, no GDK, no publishing, Direct3D 11 only, the engine runs inside
the app only in the EXPERIMENTAL engine mode that has never been compiled, no hardware ever tested, no signing code of ours.
