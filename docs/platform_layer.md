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

The environment variable `OKAY_PLATFORM_BACKEND=sdl2|own` set when a program starts overrides the build's choice for that
process (a value that is neither is ignored), so one binary can be tried on either backend. It changes nothing unless set.

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

## Stage 2: the player's window, events, input and GL context moved onto the seam

Stage 1 (above) put timers, audio devices and controllers behind `okay::platform` and wrote the window, event and GL API
without using it. Stage 2 audits what is left per app and moves the smallest self-contained app, the **player**, all the way
through it. SDL2 is then one backend there. The editor and launcher are not moved yet (table below).

### Audit: SDL identifier occurrences per app and family

Counted with `grep` over `editor/`, `launcher/`, `player/`, `shared/`, `engine/` (without `engine/*/Platform/`, which is the
backend itself), `okayui/` and `tests/`; comments removed. A count is every `SDL_*` / `SDLK_*` word, not a call, so a family
that is mostly types (renderer) reads larger than the number of calls. "Before" is `origin/main` before this change.

| family | editor | launcher | player before -> after | shared | engine | okayui | tests | `okay::platform` has it? |
|---|---|---|---|---|---|---|---|---|
| window (create, size, position, fullscreen, icon, min size, flash, flags) | 49 | 84 | 37 -> 2 | 2 | 0 | 0 | 0 | HAVE. Added for the player: fullscreen/maximised at creation, `IsFullscreen`, `SetIcon`, `RefreshRateHz`. GAP: `SDL_FlashWindow` (launcher), `SDL_GetWindowFlags` minimised/mouse-focus queries (launcher) |
| GL context | 33 | 0 | 21 -> 0 | 0 | 0 | 0 | 0 | HAVE (create, make current, swap, interval, proc address). Editor still on SDL |
| events | 30 | 18 | 24 -> 0 | 0 | 0 | 0 | 13 | HAVE (quit, window close/focus/resize, keys, text, mouse, wheel, file drop, controller hot-plug). Added: touch (`FingerDown/Motion/Up`, sdl2 only) and `wheelFlipped`. GAP: `SDL_TEXTEDITING` (IME composition), `SDL_RENDER_*_RESET` (D3D11 device loss), Windows `SDL_SysWMEvent` |
| keyboard, text input, mouse state | 75 | 2 | 58 -> 0 | 0 | 16 -> 1 | 1 | 20 | HAVE. Added: `RelativeMouseState`, `ModState`, `KeyForChar` (layout-aware), `IsCursorShown`. `Input/SdlKeys.hpp` is replaced by `Input/PlatformKeys.hpp` |
| timers | 1 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE, moved in stage 1 |
| audio device | 1 | 12 | 1 -> 0 | 0 | 0 | 0 | 0 | HAVE (queue and callback, playback and capture). GAP: `SDL_LoadWAV` and the format macros (launcher) |
| game controller | 2 | 0 | 2 -> 0 | 0 | 0 | 0 | 0 | HAVE. The editor's remaining 2 words are its `SDL_CONTROLLERDEVICEADDED/REMOVED` event checks |
| clipboard | 0 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE; the apps use ImGui's clipboard, nothing calls ours yet |
| cursors (system cursor shapes) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | GAP (nothing uses them today either; ImGui sets cursors through its SDL backend) |
| message box | 2 | 0 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `ShowErrorBox`: sdl2 and Win32 real; own Linux returns false and logs (no toolkit to draw one with) |
| display / DPI | 5 | 4 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `SetPerMonitorDpiAware` (sdl2 hint, Win32 API; nothing elsewhere) and `Window::RefreshRateHz` (sdl2 only). GAP: display bounds / usable area / DPI queries (launcher, editor) |
| misc runtime (init, log, paths, locales, hints, `SDL_free`, syswm) | 48 | 17 | 37 -> 3 | 2 | 0 | 25 | 5 | Added `BasePath`, `PrefPath`, `PreferredLocales`, `Log`. GAP: `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (launcher tray icon), `SDL_SaveBMP`, hints other than DPI |
| threads, mutexes, atomics | 0 | 705 | 0 | 0 | 0 | 0 | 0 | Not a platform-layer feature; `std::thread` would replace it separately (launcher only) |
| `SDL_Renderer`, textures, surfaces, colours, rects | 250 | 96 | 283 -> 280 | 48 | 92 | 107 | 101 | **Not behind the seam, and the main blocker.** A rendering library, not a platform one; needs an `SDL_Window*`. Needs our own 2D painter (see below) |
| Dear ImGui SDL backends (`imgui_impl_sdl2`, `imgui_impl_sdlrenderer2`) | 2 files | 1 file | 0 | 0 | 0 | 0 | 0 | GAP: third-party code reading `SDL_Event` / `SDL_Window*` directly (editor: 2 files, launcher: 1 file) |
| SDL_image / SDL_ttf / SDL_mixer | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Not used anywhere |

Totals per app (identifier occurrences): editor 496, launcher 938 (705 of them threads and mutexes), player 471 -> 285
(280 of the 285 are `SDL_Renderer` drawing), shared 52, engine 111 -> 95, okayui 133, tests 124 -> 139 (the platform tests
push SDL events on purpose).

Why the player first: 2,795 lines in one file, no ImGui, no threads, and everything it takes from SDL other than drawing was
already in the seam or a handful of small calls. The launcher is 26,000 lines with 705 thread/mutex words and ImGui; the editor
is 48,000 lines with ImGui and 33 GL-context words.

### What was moved (player)

* `main()` opens the platform (`Backend::Init`, video + audio + controllers, or timers only for `--server`), creates the main window
  (`WindowDesc`: high-DPI, resizable, borderless, fullscreen, maximised, created hidden until the boot splash is drawn), sets the minimum size and
  the icon, and creates the hidden GL context window and context for the GPU renderer, all through `okay::platform`.
* The frame loop and the splash loop read `okay::platform::Event` instead of `SDL_Event`: quit, focus, key down (F1, F3, Tab with
  Shift, F11, Alt+Enter, Esc, Shift+Esc, Backspace), text, mouse button and wheel, touch, controller added/removed.
* Held keys (WASD through the layout, digits, arrows, modifiers, F-keys, navigation block), mouse state, relative mouse (mouse-look),
  warp, cursor visibility, window size, fullscreen toggling, refresh rate, base path, per-user save folder, OS locales, error boxes,
  logging, DPI hint, shutdown.
* The window is still an `SDL_Window` for the 2D drawing: `SDL_CreateRenderer` takes it from `Window::NativeHandle()`. That is
  the only SDL window use left in `player/main.cpp` (2 words) plus renderer calls.
* On the `own` backend the player creates its window, finds there is no `SDL_Window`, says so on the log and exits with code 2
  instead of crashing or pretending (see "What was verified").

New API (every addition has a default, so a backend that cannot do it says so; both backends and the tests cover them):

| API | sdl2 | own Linux | own Windows |
|---|---|---|---|
| `WindowDesc::fullscreen`, `maximized`; `Window::IsFullscreen` | yes | yes (`_NET_WM_STATE`; needs a window manager to show) | yes |
| `Window::SetIcon(rgba, w, h)` | yes | yes (`_NET_WM_ICON`) | no (returns false) |
| `Window::RefreshRateHz()` | yes | no (0 = unknown; no RandR) | no (0) |
| `Backend::RelativeMouseState` | yes | yes (summed from motion events) | yes |
| `Backend::ModState`, `KeyForChar` | yes | `ModState` yes; `KeyForChar` follows the X keymap | `ModState` yes; `KeyForChar` assumes a US layout |
| `Backend::IsCursorShown` | yes | yes | yes |
| Touch events (`FingerDown/Motion/Up`) | yes | never fired | never fired |
| `Event::wheelFlipped` | yes | never set | never set |
| `ShowErrorBox` | yes | no (returns false, text goes to stderr) | yes (`MessageBoxW`) |
| `SetPerMonitorDpiAware` | yes (hint) | nothing to do | yes |
| `BasePath`, `PrefPath`, `PreferredLocales`, `Log` | SDL's | shared implementation in `Platform.cpp` (`/proc/self/exe`, `$XDG_DATA_HOME`, `$LANGUAGE`/`$LANG`) | shared implementation (Win32 calls), not run on Windows |

### Known behaviour differences (small, deliberate or unavoidable)

* Esc, Tab, F11 and Enter are now read by physical key (scancode) instead of SDL's key symbol. They differ only on a layout that has
  moved those keys. The WASD and digit feed still follows the layout (`KeyForChar`).
* The mouse wheel keeps SDL's OS "natural scrolling" meaning in the player (`wheelFlipped` lets the app undo the platform's
  "up = +y" normalisation), so scrolling is as before.
* `SDL_Quit()` is now `Backend::Shutdown()` (`SDL_QuitSubSystem` of what the backend started, including audio/controllers opened lazily).

### Not done (and why)

* **Editor and launcher windows, event loops and GL contexts.** Same blocker as before, plus ImGui: `imgui_impl_sdl2` and
  `imgui_impl_sdlrenderer2` need `SDL_Event` and `SDL_Window*`. The editor's window (49), GL-context (33), event (30) and
  keyboard/mouse (75) words and the launcher's windows and tray hook can be moved the same way the player was, one at a time, but the
  drawing still ends in `SDL_Renderer`.
* **The own backend cannot run the player or any app yet.** There is no 2D painter that draws on an own window. The next steps
  are the ones in the roadmap: an own software/GL 2D painter behind `UIPainter` and OkayUI, then an own ImGui platform backend.
* IME composition, display queries, window flashing, WAV loading, the tray-icon hook, system cursors, Wayland, touch and refresh
  rate on the own backend (see the tables).
* Windows (own and sdl2 side by side) and the Emscripten web build of the player were not built or run here; the changed Platform
  sources pass `-fsyntax-only` with MinGW; `player/main.cpp` itself was compiled on Linux only.

## Not behind the seam yet (stage 1 list; see stage 2 above for what changed)

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
| `platform` | registry, key numbering vs SDL scancodes, timers (both backends), `BasePath` (equals the executable's directory), `PrefPath` (created, writable, under `$XDG_DATA_HOME`), `PreferredLocales` (own parsing of `$LANGUAGE` / `$LC_ALL` is exact), error box / DPI on own report "not supported", the US `KeyForChar` table, audio devices on both backends (queue, clear, pause, callback, capture; SDL dummy driver / ALSA `null` device), controllers enumeration, own evdev mapping fed through a pipe | nothing; audio parts print `SKIP` when no device can be opened |
| `platform_window_sdl2`, `platform_window_own` | create / resize / title, GL clear colour read back with `glReadPixels` (and for `own`, the window's pixels read back from the X server with `XGetImage`), keyboard, modifiers, text input (incl. a non-ASCII character), mouse buttons / wheel / motion, relative mouse, clipboard (incl. a second X client reading ours and vice versa). Stage 2 added: `KeyForChar` on the US and the German layout (z and y swap places), `ModState`, `AppendExtendedKeys` (F-keys, Tab, navigation, left/right modifiers) with keys held through xdotool, `RelativeMouseState` sums (12,7 then 0,0 then -5,3), `IsCursorShown`, fullscreen at creation and toggled, maximised, `SetIcon` (valid and invalid), refresh rate, base path; on sdl2 also touch events and flipped / normal wheel events pushed as SDL would | Xvfb, `xdotool` (XTEST input). Skipped (exit 77) when either is missing |
| `platform_default_env_sdl2`, `platform_default_env_own` | `OKAY_PLATFORM_BACKEND` in the environment overrides the build's default backend (`test_platform --print-default`) | nothing |

Also `platform_gamepad`: the controller -> `Input` bridge (`PlatformGamepad.hpp`) driven by a fake `Gamepad`.

Run just these: `ctest -R platform`. `tests/run_under_xvfb.sh <program> [args]` starts a private Xvfb on a free display (and exits 77 when Xvfb is missing).

## What has and has not been verified

### Stage 2 (the player on the seam)

Linux x86-64, Xvfb 1024x768x24 + openbox, own `HOME`, Mesa software rendering (`SDL_RENDER_DRIVER=software`), xdotool input.
The "before" player is the unchanged `origin/main` `player/main.cpp` built against the same libraries; "after" is this branch.
Both ran `examples/node_playground/flow.okayscene` with the same scripted session (start, F3 stats overlay, F1 test panel,
mouse click on the Match and Settings tabs, typing, wheel, held W, F11 fullscreen and back, Esc):

* window title, size (960x600) and position, exit code 0 on Esc, the save folder created, the dev log lines: identical.
* screenshots of the start frame, the held-key frame, the F11 fullscreen frame (window becomes 1024x768 at 0,0) and the return to
  windowed (960x600 at the same place): pixel-identical. The panels that show live frame statistics (F3, F1) differ only in the fps
  numbers and the spinner; a click on the Settings tab switches the panel the same way in both.
* `--server` (headless, no `DISPLAY`): identical log, same exit.
* `OKAY_PLATFORM_BACKEND=own`: the player gets through platform init, base path, user folder, locales, anti-cheat decision, scene
  load and creates its X11 window, then stops with "the player draws through SDL_Renderer, which needs the sdl2 platform backend ... has no 2D painter yet" and exit code 2.
  It does not get further: there is nothing to draw 2D on an own window yet.
* `ctest -R platform`: `platform`, `platform_window_sdl2`, `platform_window_own`, `platform_default_env_*`, `platform_gamepad` pass.

Not verified in stage 2: typing into an OkayUI text field in the running player (the scene has none; the text event path is covered by
`platform_window_*`), touch input on a real touch screen, natural-scrolling mice, an AZERTY keyboard in the running player (`KeyForChar`
is tested on the German layout), fullscreen on a real desktop other than openbox, Windows and web builds, and `-DOKAY_PLATFORM_BACKEND=own` as a *build* (the run-time
environment variable was used instead; the build option only changes which backend `Default()` returns).

### Stage 1

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
