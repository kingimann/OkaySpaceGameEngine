# The platform layer (`okay::platform`) and our own backend

Stage 1 of replacing SDL2 with our own code (see the roadmap: seam first, own backend side by side and
non-default, SDL2 stays). This page is the audit of what the engine takes from SDL2, what is behind the
seam now, what is not, and what was and was not tested.

Header: `engine/include/okay/Platform/Platform.hpp`. Sources: `engine/src/Platform/`. CMake: `cmake/OkayPlatform.cmake`.

## Choosing a backend

| | |
|---|---|
| `-DOKAY_PLATFORM_BACKEND=sdl2` | default. `okay::platform::Default()` is the SDL2 backend (a thin wrapper, behaviour as before). |
| `-DOKAY_PLATFORM_BACKEND=own` | `Default()` is our own backend. SDL2 is **not** removed: the editor, launcher and player still need it for windows and `SDL_Renderer` (see "Not behind the seam yet"). |

Both backends are built whenever they can be, and `okay::platform::Get(BackendKind::Sdl2 | Own)` returns either one, so
a test can run the same checks on each. `Default()` falls back to the other backend if the chosen one is not compiled in.

## What the audit found (origin/main before this change)

About 3,000 lines in 67 files mention SDL, in `engine/`, `editor/` (48k-line `main.cpp`), `launcher/`, `player/`, `shared/`,
`okayui/` and `tests/`. Grouped by what SDL is used *for*:

| Feature | SDL calls (count) | Where | Now |
|---|---|---|---|
| **Timers** | `SDL_GetTicks` 154, `SDL_GetTicks64` 6, `SDL_Delay` 18, `SDL_GetPerformanceCounter/Frequency` 23 | launcher 140, editor 32, player 13, okayui 3, shared 1 | **behind the seam** (`platform::TicksMs32/TicksMs/PerfCounter/PerfFrequency/DelayMs`) |
| **Audio device** | `SDL_OpenAudioDevice` 11, `Pause` 12, `Queue` 5, `GetQueuedAudioSize` 5, `Clear` 3, `Dequeue` 1, `Close` 8, `GetNumAudioDevices` / `GetAudioDeviceName` 2, capture + callback mode (voice chat) | player, editor, launcher (voice notes, calls, mic test, chat chime, device lists) | **behind the seam** (`AudioDevice`, queue and callback mode, playback and capture). `SDL_LoadWAV` (one WAV decode in the launcher) stays |
| **Game controller** | `SDL_GameController{Open,Close,GetAxis,GetButton,Rumble,GetJoystick}`, `SDL_JoystickInstanceID`, `SDL_NumJoysticks`, `SDL_IsGameController` | `engine/.../SdlGamepad.hpp` (now `PlatformGamepad.hpp`), editor Play mode, player | **behind the seam** (`Gamepad`, `Backend::OpenGamepad`). The hot-plug *events* (`SDL_CONTROLLERDEVICEADDED/REMOVED`) still arrive through each app's SDL event loop |
| **Windows** | `SDL_CreateWindow` 7, `DestroyWindow` 7, `Get/SetWindowSize/Position`, `Show/Hide/Raise/Minimize/Restore`, `SetWindowFullscreen`, `SetWindowIcon`, `FlashWindow`, `SetWindowMinimumSize`, `GetDisplayDPI/UsableBounds`, `SDL_WINDOW_*` flags | the three `main.cpp` files | seam API written and tested; **call sites not moved** (see below) |
| **Events, keyboard, mouse, text** | `SDL_PollEvent` 5, `SDL_PumpEvents`, `SDL_PushEvent`, `SDL_GetKeyboardState`, `SDL_GetMouseState`, relative mouse, `SDL_StartTextInput`, `SDL_ShowCursor`, `SDL_WarpMouseInWindow`; event types QUIT, WINDOWEVENT (close, focus, resize), KEYDOWN/UP, TEXTINPUT, MOUSE*, DROPFILE, FINGER*, CONTROLLERDEVICE*, RENDER_*_RESET; `SdlKeys.hpp` scancode table | same | seam API written and tested; **call sites not moved**. Touch (`SDL_FINGER*`) is not in the seam at all |
| **OpenGL context** | `SDL_GL_SetAttribute` 8, `CreateContext` 2, `MakeCurrent` 16, `GetCurrentContext/Window` 8, `DeleteContext` 2, `SDL_GL_GetProcAddress` | editor, player (`GLRenderer`) | seam API written and tested; **call sites not moved** |
| **Clipboard** | none. The editor uses ImGui's clipboard, which goes through `imgui_impl_sdl2` | editor, launcher | seam API written and tested; nothing calls it yet |
| **2D renderer** | `SDL_Renderer`, `SDL_Texture`, `SDL_Surface`: about 330 calls (`SDL_RenderCopy`, `SDL_RenderGeometry`, `SDL_UpdateTexture`, ...) | editor, player, launcher, `okayui` (whole toolkit), `UIPainterSDL.hpp`, tests (golden images use SDL's software renderer) | **not part of the seam.** This is a rendering library, not a platform one, and it needs an `SDL_Window` (or a surface) to draw to |
| **Dear ImGui backends** | `imgui_impl_sdl2` (takes `SDL_Event*` and `SDL_Window*`), `imgui_impl_sdlrenderer2` | editor, launcher | **not behind the seam.** They are third-party code that reads SDL types directly |
| **Threads, mutexes, atomics** | `SDL_CreateThread` 60, `DetachThread` 47, `WaitThread` 17, `Create/Lock/UnlockMutex` ~370, `SDL_Atomic*` ~30 | launcher (downloads, chat, voice), `VoiceChat.hpp` | **not touched.** Not a platform feature in the roadmap's list; `std::thread` would replace it, separately |
| **Misc** | `SDL_Log` 9, `SDL_GetBasePath` 4, `SDL_GetPrefPath`, `SDL_GetPreferredLocales`, `SDL_ShowSimpleMessageBox` 3, `SDL_SetHint` 7, `SDL_SaveBMP`, `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (Windows tray icon), `SDL_sinf`-style math, `SDL_free` | all apps | not touched |
| **Build** | `find_package(SDL2)`, static `SDL2::SDL2-static` option, `SDL2.dll` copy, MinGW toolchain, Emscripten `-sUSE_SDL=2` | CMake | `okay_platform` links SDL2 the same way as the apps; Emscripten uses `-sUSE_SDL=2` for the SDL backend (**not built here**) |

## What is behind the seam

`Backend` (`Platform.hpp`) has: init, timers, windows (create, size, position, title, show/hide/raise, minimize, fullscreen,
focus), an event queue, the GL context (create, make current, swap, swap interval, `GLProcAddress`), keyboard state, text input,
mouse (state, relative mode, cursor visibility, warp), clipboard text, audio devices (playback and capture, queue or
callback), and game controllers (open, axes, buttons, rumble). `Key` is the USB HID usage numbering, which is also SDL's scancode numbering.

Call sites moved in this change (each commit builds):

1. Timers everywhere (`okay::platform::TicksMs32()` is a drop-in for `SDL_GetTicks()`).
2. Audio devices: player, editor Play mode, launcher voice notes / chime / device lists, `VoiceChat.hpp` (calls and mic test).
   A voice note in a WAV layout the platform layer does not play (8-bit, 32-bit integer, big-endian) is converted to 16-bit
   once at load; SDL used to convert at the device.
3. Controllers: `Input/SdlGamepad.hpp` became `Input/PlatformGamepad.hpp` (`FeedPad`, `PadSet`; no SDL types); the editor and player open pads through the backend.

## Not behind the seam yet (NOT DONE, and why)

The window, event loop and GL context of the editor, launcher and player still call SDL directly. Moving them needs more than
a search and replace: `SDL_Renderer` (330 calls, the whole `okayui` toolkit, every golden-image test) and Dear ImGui's
`imgui_impl_sdl2` / `imgui_impl_sdlrenderer2` both require an `SDL_Window*`, so a window that is not an SDL window cannot
draw anything in those apps today. The next steps are an own software/GL 2D painter behind `UIPainter` and an own ImGui
platform backend (the roadmap's ImGui item); only then can `OKAY_PLATFORM_BACKEND=own` drive the apps' windows. Until then
`own` changes the timers, audio devices and controllers the apps use, and is the full platform for the tests and for new
code that does not need `SDL_Renderer`.

## The own backend

### Linux (written, tested under Xvfb)

* **Windowing: X11 loaded with `dlopen("libX11.so.6")`, nothing linked.** `libX11` is not a build dependency of this repo
  (SDL2 loads it at run time itself), so linking it would add one to every Linux build. The X11/GLX header files are used
  for types and constants only (`check_include_file_cxx`); without them the own backend builds with no windowing and says so.
  Raw Xlib rather than XCB: XCB would have no text-input story without more libraries (`xkbcommon`), and Xlib has
  `XFilterEvent`/`Xutf8LookupString` for input methods.
* Window create, destroy, resize (+ events), title (`_NET_WM_NAME`, UTF-8), position, min size, show/hide, raise (+ input
  focus, so it works with no window manager), minimize, fullscreen (`_NET_WM_STATE`, needs a window manager), `WM_DELETE_WINDOW`.
* Events: keys with the physical (evdev -> HID) layout and auto-repeat detection, text input through an input method with a
  keysym fallback, mouse buttons (incl. double-click counts), wheel, motion, relative mouse (grab + warp), cursor hiding.
* **GLX** (`libGL.so.1` through `dlopen`): FBConfig visual, `glXCreateContextAttribsARB` (3.2 compatibility profile, as the
  engine asks) with the legacy path as fallback, swap, swap interval, `glXGetProcAddressARB`.
* Monotonic timers (`CLOCK_MONOTONIC`), `nanosleep`.
* **Clipboard**: owns the `CLIPBOARD` selection and answers `SelectionRequest` while events are polled; reads other clients' text via `ConvertSelection`.
* **Audio: ALSA through `dlopen("libasound.so.2")`**, no ALSA headers (only `snd_pcm_open/set_params/writei/readi/recover` are
  declared by hand). One thread per device, queue and callback mode, playback and capture, device listing. PulseAudio is
  not used: ALSA's `default` device is routed to PulseAudio / PipeWire on desktops that run them. `OKAY_ALSA_DEVICE` overrides the device name.
* **Controllers: evdev** (`/dev/input/event*`, kernel header `linux/input.h` only): detection by `BTN_GAMEPAD` + `ABS_X`,
  Xbox-style mapping, normalised axes, triggers from axes or `BTN_TL2/TR2`, d-pad from hat or buttons, force-feedback rumble, hot-plug rescan once a second.

Not written for Linux: Wayland (X11 only; under Wayland it runs through XWayland), XDND file drop (`EventType::DropFile`
never fires), touch, DPI / multi-monitor queries, window icons, IME composition display, PulseAudio-native or PipeWire-native audio.

### Windows (compiles, untested)

`OwnWin32.cpp`: Win32 window and input, `WM_CHAR` text, WGL context, Win32 clipboard, `QueryPerformanceCounter`, XInput pads,
`WM_DROPFILES`. **Only syntax-checked with MinGW (`-fsyntax-only`) on Linux. Never run on Windows.** No audio device
(`OpenAudio` returns null).

### macOS (not written)

Needs Objective-C++ (Cocoa, NSOpenGLContext/Metal, CoreAudio, GameController). It cannot even be syntax-checked with the
MinGW cross-compiler, so nothing was written rather than writing code that was never compiled.

## Tests

| ctest name | What | Needs |
|---|---|---|
| `platform` | registry, key numbering vs SDL scancodes, timers (both backends), audio devices on both backends (queue, clear, pause, callback, capture; SDL dummy driver / ALSA `null` device), controllers enumeration, own evdev mapping fed through a pipe | nothing; audio parts print `SKIP` when no device can be opened |
| `platform_window_sdl2`, `platform_window_own` | create / resize / title, GL clear colour read back with `glReadPixels` (and for `own`, the window's pixels read back from the X server with `XGetImage`), keyboard, modifiers, text input (incl. a non-ASCII character), mouse buttons / wheel / motion, relative mouse, clipboard (incl. a second X client reading ours and vice versa) | Xvfb, `xdotool` (XTEST input). Skipped (exit 77) when either is missing |

Also `platform_gamepad`: the controller -> `Input` bridge (`PlatformGamepad.hpp`) driven by a fake `Gamepad`.

Run just these: `ctest -R platform`. `tests/run_under_xvfb.sh <program> [args]` starts a private Xvfb on a free display (and exits 77 when Xvfb is missing).

## What has and has not been verified

Verified (Linux x86-64, Xvfb 1024x768x24, Mesa llvmpipe software GL, xdotool/XTEST for input):
the whole `platform`, `platform_window_sdl2` and `platform_window_own` tests pass, repeatedly; the GL clear colour is read
back with `glReadPixels` on both backends and, for `own`, from the X server's copy of the window (`XGetImage`); keys, modifiers,
ASCII and German-layout text, mouse, wheel, relative motion and the clipboard between two X clients all arrive as events through
the real server input path. `Win32` backend: `x86_64-w64-mingw32-g++ -fsyntax-only -Wall -Wextra -Wpedantic` is clean.
The editor, launcher and player sources were compile-checked (`-fsyntax-only`) after each change; the engine library was
not fully built and linked in that session (the machine was shared and a full build takes hours), so the three apps
were **not linked or run** on top of the new layer.

Not verified: real audio hardware (only SDL's dummy driver and ALSA's `null` device), real game controllers (no
`/dev/input` here; the evdev mapping is tested through a pipe, rumble and hot-plug are not), Wayland, window-manager behaviour
(fullscreen, minimise, the frame offset of window positions: Xvfb has no window manager), HiDPI, multi-monitor, a real input method
(the text path is tested with the keysym fallback), Windows at run time, Emscripten, a static-SDL Windows build.
