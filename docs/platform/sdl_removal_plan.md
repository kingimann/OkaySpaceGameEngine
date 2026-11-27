# Getting the programs off SDL2: the plan and where it ended

> **What this is:** The ordered plan for replacing SDL2 with `okay::platform`, and (top section) the state after the deletion.
>
> **Status:** Internal record. The section "2026-11-27: SDL2 left the desktop programs" is current; everything below it is the
> plan as it was written and is kept as history (Dear ImGui, which steps 4 and 5 talk about, was deleted the same day, #1375).
>
> **Last checked:** 2026-11-27 (`claude/no-sdl2-2`).

## 2026-11-27: SDL2 left the desktop programs (`claude/no-sdl2-2`)

Owner, 2026-11-27: "main goal to remove other third parties that includes the compilers". Rule 17: ours is THE path, no switch.

**What each program uses now.**

| Program | Window, events, input, GL, clipboard, audio, pads | 2D drawing | SDL left |
|---|---|---|---|
| Launcher (`OkaySpace.exe`) | own backend (X11 / Win32) | OpenGL painter, else the software painter (`shared/OwnPainter.hpp`) | none |
| Editor (`OkayEngine.exe`) | own backend | OpenGL painter, else software; 3D view as before (GL / D3D11 on its own device / software) | none |
| Player, desktop (`OkaySpacePlayer.exe`) | own backend | software painter (okayui/soft) | none |
| Browser, Script Editor, Visual Script programs | own backend | OpenGL painter, else software | none |
| Player, web (Emscripten) | sdl2 backend over Emscripten's SDL2 port | sdl2 painter | `SdlBackend.cpp`, `SdlPainter.cpp`, `-sUSE_SDL=2` |

**Deleted.** `UIPainterSDL.hpp`; `WrapSdl2Window`, `WrapSdl2Renderer`, `CreateSdl2SurfacePainter`, `TranslateSdl2Event`,
`Event::native`, `Backend::ReportUnmappedEvents`; OkayUI's `EndFrame(SDL_Renderer*)` / `DrawFinishedFrame` and its SDL batch
types (now `BatchVertex`, `PxRect`, `OkayUI::Tex`), OkayUI's `SdlHost` (`OkayUIRender_SDL.cpp`, `OKAY_UI_OWN_RENDER`) and its test;
`shared/WindowHandles.hpp`; the SDL_Event feed of `shared/OkayUIHost.hpp`; Settings > Experimental "Our own window layer" and
"OkayUI own renderer" (nothing to switch back to); `find_package(SDL2)` everywhere; the SDL2 download and `SDL2.dll` in CI, release,
test builds, `build-windows-dist.sh`, `release-local.sh`, the release assets and Build Game's exports; `okay engine`'s SDL2 search
and `build-local.ps1 -Sdl2Dir`; `test_painter_bridge_vs_sdl`; the sdl2 halves of the platform, painter and render tests.

**Changed.** The apps' renderer handle is the painter (`okay::pb::Renderer` = `okay::platform::Painter`), textures are
`okay::ui2d::Tex`, the launcher's windows are `okay::platform::Window*`. Caps Lock comes from `Backend::ModState` (own X11:
XQueryPointer's mask; Win32: GetKeyState). The Win32 clipboard is not touched before `Init` (as on X11). The UI picture tests
draw on our own software painter (`tests/ui_soft_render.hpp`).

**Checked before the deletion (on main's CI, not re-run here):** `painter_bridge_vs_sdl` (the bridge against raw SDL calls),
`okayui_painter` (software painter vs SDL's software renderer: 2.7 % of the classic demo picture differs by more than 2 levels, all
on shape edges; the launcher look under 3 %), `okayui_render` (painter backend vs reference), `platform_window_sdl2` vs `_own`,
`platform_displays` (own RandR vs SDL), and the launcher run headless on both backends (the same picture but 8 pixels). After the
change the UI goldens that were made on SDL's software renderer (`ui_renderer_golden`) are compared against our painter: see the
branch report for the numbers.

**What remains.**
1. **Real Windows.** The Win32 backend, the OpenGL painter on WGL and every program were compiled with MinGW only; none has run on
   real Windows (Wine is broken here). The next test build is the first Windows run: launcher, editor, player, chat window, tray,
   file drop, IME, two monitors, 125 % / 150 % scaling, sound, pads.
2. **The web.** An own Emscripten backend (canvas, WebGL 2 context, keyboard / mouse / touch / wheel callbacks, Web Audio, the
   Gamepad API, clipboard), then delete `SdlBackend.cpp`, `SdlPainter.cpp`, `PainterKind::Sdl2`, `BackendKind::Sdl2` and `-sUSE_SDL=2`.
3. **The editor's zero-copy D3D11 viewports** needed SDL's Direct3D 11 device; the D3D11 3D renderer now always uses its own device
   with read-back (slower on Windows until the painter can share a device). The code path is still there, unused.
4. Installing an editor version from before 2026-11-27 through the launcher's Versions page needs `SDL2.dll` from the version list;
   the launcher no longer has its own copy to lend.

## Where we were (before 2026-11-27)

| | SDL calls left (`calls / words`) | what they are |
|---|---|---|
| player | 5 / 221 | almost only data types (`SDL_Rect`, `SDL_Color`, `Uint8`); the window, input, GL, audio and 2D drawing already go through `okay::platform` |
| launcher | 743 / 1,199 in its own files | **613 are threads, mutexes and atomics (83%)**; 57 `SDL_Renderer` drawing; 14 Dear ImGui SDL backends; about 60 window, input and other platform calls |
| editor | 229 / 556 | 126 `SDL_Renderer` drawing, 22 GL context, 25 window, 17 mouse, 17 Dear ImGui SDL backends, the rest small |
| engine library | 0 | outside the SDL backend files (`SdlBackend.cpp`, `SdlPainter.cpp`, `UIPainterSDL.hpp`) there is **no SDL word in code** |

Three different things keep SDL2 in the two programs, and they need three different kinds of work:

1. **Platform calls** (windows, input, GL, clipboard, audio). `okay::platform` already covers about 290 of the call sites; about 60
   have no function yet. This is mostly *moving call sites*, not new code.
2. **Drawing.** Both programs draw through `SDL_Renderer`, and Dear ImGui draws through its two SDL backends. Those backends are third-party
   code that read `SDL_Event` and `SDL_Window*`. This is the long pole.
3. **Threads.** Only the launcher. It is not platform work and **cannot use `std::thread` or `std::mutex`** (rule 21a: the Windows build
   cannot compile them in headers). It needs our own wrapper; one already exists in part (`engine/src/Core/CoreThreads.hpp`).

The inventory's section 4 ranks every GAP. The platform GAPs are small; the real work is steps 2 to 6 below.

## The steps, in order

### Step 1. Land the work that already exists

**What.** The branch `origin/claude/platform-gaps2` (tip `33da9e1d`, 2026-11-14, no pull request, contains `claude/platform-gaps`) holds about 6,500 added
lines of seam work that is not on main: X11 drag-and-drop receive, RandR monitors and `Xft.dpi`, cursor shapes, the urgency hint (taskbar flash), an
X11 message box, input-method preedit, Win32 clipboard formats, an OLE drop target, `FlashWindowEx`, IME composition events, `DisplayInfo` and
`DisplayCount`, a pad mapping table, `docs/own_platform_status.md`, and tests. A dry-run merge into `05feef3b` conflicts only in the generated
`docs/feature_map/_index_az.md`. It was **not built or run by us**; its own page says Windows ran only under Wine.
**Why first.** It is the GAP list's answer for displays, flash, cursors, IME events and drop begin / complete, and the next steps need those.
**Size.** Small to medium: merge main into it, regenerate the feature map, build, run its tests under Xvfb and Wine, get CI green, one PR.
**Real Windows.** Nothing changes by default (the own backend is off). To see it: `set OKAY_PLATFORM_BACKEND=own`, run the player (see `docs/try_it.md`
section 11), and check window, keys, mouse, text, clipboard, a file dropped from Explorer, the taskbar flash, 125% and 150% display scaling, two monitors.

### Step 2. Launcher threads, mutexes and atomics (independent: can start now, in parallel)

**What.** 613 calls: `SDL_CreateThread` 67, `SDL_DetachThread` 55, `SDL_WaitThread` 17, `SDL_LockMutex` 178, `SDL_UnlockMutex` 184,
`SDL_CreateMutex` 59, `SDL_DestroyMutex` 6, `SDL_Atomic*` 47. Files: `launcher/main.cpp` 578, `launcher/VoiceChat.hpp` 23, `launcher/VersionsUI.cpp` 12.
**How.** Grow the existing `okay::corethreads` shim (`Mutex`, `CondVar`, `Thread`; Win32 `CRITICAL_SECTION`, `CONDITION_VARIABLE`, `CreateThread`; POSIX on the
standard library) and move it where the launcher can include it. Three things the shim lacks:
* **A recursive mutex.** SDL's mutexes are recursive (the owning thread may lock again; the SDL docs say so). The Win32 critical section is recursive,
  but the POSIX path uses `std::mutex`, which is not. Add a recursive variant, or audit every lock site (178 of them).
* **Detach.** `SDL_DetachThread` is used 55 times (fire and forget); the shim's `Thread` only joins. Add `Detach()` (Win32 `CloseHandle`, POSIX `detach`).
* **Atomics.** `std::atomic<int>` replaces `SDL_atomic_t` (`<atomic>` does not need the thread model).
Then convert the three files mechanically. Do not use `std::mutex` or `std::thread` in anything the Windows build compiles (`scripts/branch_lint.py` fails it).
**Size.** Medium to large: the replacement is small, the 613 edits are many, and a wrong lock site is a hang that tests rarely show.
**Test headless.** A race test for the new mutex (recursive lock, contention) and for detach; the existing `test_voice_races` and the launcher tests.
**Real Windows (default backend; nothing visible should change).** Sign in; chat both ways in the launcher and in the separate chat window; a file or image
sent in chat; download an editor version with progress and cancel; start a voice call and the mic test; playtime sync; quit while a download runs
(no hang, no crash). Watch for freezes: that is how a lock mistake shows.
**Try it (rule 4a).** Invisible by design; the owner uses the launcher as usual and reports hangs.

### Step 3. Move the call sites the seam already covers (the "HAVE" rows)

**What.** About 290 call sites in the launcher and editor are plain SDL calls for something `okay::platform` has a function for: window size, position, show,
hide, raise, restore, fullscreen; mouse state; key state; text input; clipboard; the event loop; init and shutdown. The inventory's section 4b lists them.
**Blocker found by the inventory.** The apps keep a raw `SDL_Window*`, and every `Window` method needs a `Window` object, which only `Backend::MakeWindow` makes.
So until the apps make their windows through the seam, no `Window` method can be called on their windows (60 call sites wait for this).
**Bridge.** `okay::platform::WrapSdl2Window(void*)` (see "Step 0" below): a non-owning `Window` over a window the app made itself. It lets each call site move on
its own, in small safe edits, while the window is still created by SDL. It is temporary and goes away in step 6.
**Size.** Medium for the launcher (small edits, many places, one file other agents also edit). The editor's `main.cpp` is about 48,000 lines and edited by several
agents at once: do it in small pull requests, one region at a time, after asking who is editing it.
**Real Windows.** Behaviour must be identical on the default backend: minimise and restore the launcher and the chat window, a message arriving while minimised
flashes the taskbar, dropping a file on the chat window sends it, window position and size are remembered across a restart.

### Step 4. Drawing: our own renderer backend for Dear ImGui, and the apps' `SDL_Renderer` calls onto `okay::platform::Painter`

**What.** Launcher 57 and editor 126 `SDL_Renderer` calls, plus `ImGui_ImplSDLRenderer2_*` (launcher 7, editor 9).
**How.**
* Move the apps' own `SDL_Renderer` calls onto `Painter` (it can wrap their renderer: `WrapSdl2Renderer`). The inventory lists what `Painter` lacks: render targets (editor 3),
  raw geometry (1), flush (2), reading the draw colour (1), the Direct3D 11 device behind the renderer (1, the editor's interop), the renderer-driver choice (4).
* Write our own ImGui renderer backend: it turns Dear ImGui's draw lists (`ImDrawData`, documented in `imgui.h`) into `Painter::Geometry` and `Painter::Copy` calls with the
  clip rectangle and texture of each command. It is small (a few hundred lines). Write it from the documented `ImDrawData` structure and our `Painter` API, **not from
  `imgui_impl_sdlrenderer2.cpp`** (rule 19a).
**The hard part is speed on Windows.** Today the editor's UI is drawn by SDL's GPU renderer. Our software painter draws on the CPU; it is fine for the player and tests, but a
full editor window at 1080p on the CPU will be slow. The own GPU path needs OkayUI's OpenGL 3.3 or Direct3D 11 render backends (they exist, tested on Mesa and Wine only, wired nowhere,
never run on a real GPU) driven from the ImGui draw data. **This is the biggest unknown on the whole list: very large until measured on real Windows hardware.**
**Test headless.** Draw the same ImGui frame through the SDL renderer and through our backend onto the software painter and compare against a stated tolerance
(the same method `okayui_painter` already uses).
**Real Windows.** Look at the launcher and the editor side by side with the SDL version (fonts, colours, clipping, scroll areas, the 3D view texture), then the frame rate with
the editor's scene view open.

### Step 5. Our own Dear ImGui platform backend (events, cursor, clipboard, text input, window)

**What.** Replace `ImGui_ImplSDL2_*` (launcher 7, editor 8). It feeds `okay::platform::Event` into `ImGuiIO`: mouse, wheel, keys (a table from our `Key` to `ImGuiKey`), typed text,
focus, window size and scale, the cursor shape (needs `SetCursor` from step 1), the clipboard, and drop. About 500 lines plus a test that feeds synthetic events and checks
`ImGuiIO` and the hovered widget. Write it from `imgui.h`'s documented input API, not from `imgui_impl_sdl2.cpp`.
**Open question: the editor's multi-window mode.** The editor sets `ImGuiConfigFlags_ViewportsEnable` when its "multi window" option is on, and has its own SDL code for the extra windows
(`editor/main.cpp` around line 47471). A platform backend for viewports also creates, moves and destroys those windows. Plan: ship the first version without the multi-window option on
the own backend (it stays on SDL), then do viewports as its own sub-step.
**Size.** Large (the editor's keyboard, IME and docking behaviours are where it will take time).
**Real Windows.** Type in every kind of text field, including a non-English layout and an IME; copy and paste text; the mouse cursor shapes over resize edges and text fields; drag a file
into the editor; mouse wheel and touchpad scrolling; two monitors; display scaling 125% and 150%.

### Step 6. The programs create their windows and run their event loops through the seam

**What.** `SDL_CreateWindow` (launcher 2, editor 3), the event loops (`SDL_PollEvent`), the editor's GL contexts (22 calls; needs `CurrentGLWindow` and `CurrentGLContext`, a GAP of 8 call
sites), and removing `WrapSdl2Window`. After steps 3 to 5 this is mostly deletion.
**Size.** Medium. **Real Windows.** The whole programs, on both backends, for a full session: launcher, editor, play mode, a built game.

### Step 7. Real Windows and real desktop runs (every step, not only at the end)

Nothing about the own Win32 backend has run on real Windows (Wine only). That is the largest risk in the whole plan, because a bug found at the end is expensive. So **each** step above
ends with a Windows run of its list, and the own backend gets a first real run (player, own backend, `docs/try_it.md` section 11) as soon as step 1 lands. Also needed once: a real Linux
desktop with a window manager, a file manager drop, an input method, and a second monitor. macOS is parked (rule 14); the web (Emscripten) build still uses SDL2 from Emscripten's ports and
has no own backend, so SDL2 cannot be deleted from the repository until the web path has one or the owner decides to keep it for the web.

### Step 8. Flip and delete (the owner's decision)

Only after the replacement criteria in `docs/status/own_code_roadmap.md` hold: make `okay::platform` own the default per platform with the owner's written approval, announce it one release ahead, delete
SDL2, the CMake `find_package(SDL2)`, the CI and release downloads, `dist/SDL2.dll` and the bundled copy one release later. The build and CI files that name SDL2 are listed in the inventory (section 6).

## What can run in parallel, and the critical path

* Step 2 (threads) touches only the launcher's threading and needs nothing else. Start it now.
* Steps 1 and 3 can run together (3 needs the bridge below).
* The critical path is **4 -> 5 -> 6 -> 7**: drawing and the ImGui backends, then the windows, with a real Windows run at each stage. Within it, the GPU painter for the editor is the one
  piece nobody can size yet.
* Cheap mechanical work for any idle agent (no seam needed): the 23 `SDL_sinf` / `SDL_cosf` / `SDL_sqrtf` / `SDL_floorf` calls in `okayui/src/OkayUI.cpp` become `<cmath>`; the
  `Uint8` / `Sint16` typedefs (333 words) become `<cstdint>` names; `SDL_free` / `SDL_strdup` / `SDL_zero` become libc; the SDL version checks go. (`okayui/` and the editor are edited by other agents: coordinate first.)

## Step 0: what the branch `claude/sdl-inventory` adds (in progress)

Planned and being built with tests first; this section is filled in with the final list and results when the code is done.

* The inventory and its generator (`docs/sdl_inventory.md`, `scripts/sdl_inventory.py`).
* `okay::platform::WrapSdl2Window` (the bridge for step 3) and five small seam functions that the launcher or editor use and that were missing: window state queries
  (`IsMinimized`, `IsVisible`, `HasMouseFocus`), focus queries (`KeyboardFocusWindowId`, `MouseFocusWindowId`, `Window::SetInputFocus`), `IsRelativeMouse`,
  `GlobalMouseState` and `PumpEvents`. Each on the sdl2 backend, the own X11 backend and the Win32 backend (Win32 compile-checked only).
* A handful of launcher call sites moved onto them, only where the behaviour with the default SDL backend is identical.
