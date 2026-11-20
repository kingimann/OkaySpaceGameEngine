# Xbox One (UWP, Developer Mode) audit

Internal. Phase 1 of the experimental Xbox export (owner request 2026-11-18). Read from the code on `origin/main` (7d2ad98f) on a Linux
machine with no Windows SDK, Visual Studio or Xbox. Everything about what UWP allows is from Microsoft's public documentation as
remembered and is **unverified**: check each "not allowed" line against Microsoft's "APIs not supported in UWP apps" and
"Alternatives to Windows APIs in UWP apps" pages before relying on it. The pipeline that follows is in [xbox_uwp.md](xbox_uwp.md).

What this target is: a Universal Windows Platform app that an Xbox One in Developer Mode can run. It is not a native console game,
it does not use the restricted GDK, and the public SDK does not build one.

## What exists today, and what a UWP package can reuse

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

## Concrete compatibility problems (ranked)

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

## Parts that need a UWP adapter (and their state on `claude/xbox-uwp-2`)

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

## Update 2026-11-18: first real Windows run

The triangle test package was built, packed with our packer, accepted by MakeAppx `unpack`, signed, installed and started on a Windows 11 PC (Visual Studio 2026, MSVC 14.52.37025, toolset v145, Windows SDK 10.0.26100.0) and showed the triangle through a Direct3D 11 flip-model swap chain on a CoreWindow.
That confirms, for the triangle shell only: the compile with C++/WinRT (C++20, no `/ZW`, no `/await`), the manifest and package layout, the packer, signing and install. It confirms nothing about problems 1, 2, 4 to 8 above for the engine (they are untouched by that run), and nothing about a console. See `docs/platform/xbox_uwp.md`, "What is verified on Windows".

## Third-party code in the UWP target (2026-11-18, own-code round)

The reduced engine target now carries no third-party library: SDL2, Dear ImGui, Assimp, ufbx, libsodium, libcurl and OpenSSL are not compiled or included, and the audit
scanner lists their headers (`sodium.h`, `imgui.h`, `assimp/`, `ufbx.h`, `curl/curl.h`, `openssl/`, `stb_*`, `dr_*`) as forbidden. Current result: 313 sources, 4 excluded
(`ufbx.c` is the new one), 309 scanned, 949 files in the include closure, 0 findings. The table of what replaces what, and what is still blocked, is
[xbox_uwp_thirdparty.md](xbox_uwp_thirdparty.md). Crypto is the one honest exception: SHA-256, random bytes and Ed25519 checks are our own unreviewed `okay::crypto` code (never a release).

