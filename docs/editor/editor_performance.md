# Editor speed: seeing it, measuring it, tuning it

> **What this is:** The editor shows how long each frame takes and where the time goes, and has a few switches to make it cheaper.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `editor/`, paths and links checked; the text itself was not re-checked against the code).

The editor shows how long each frame takes and where the time goes, and has a few switches to make it cheaper.
Everything here is **off by default** except the numbers you turn on yourself. Settings are remembered in
`okay_settings.txt` next to the editor.

## See the numbers

**View > Rendering > Frame-time overlay** opens a small window:

- FPS, average frame time, the 95th percentile (19 frames in 20 are faster than this), the worst frame and the "1% low".
- A graph of the last 120 frames.
- Where the frame goes, averaged over the last 30 frames, biggest first:
  3D view render (the GPU draw, or the CPU raster; on the read-back path this includes the wait for the GPU),
  3D view upload (CPU picture to texture), viewport gizmos and overlays, panels (Hierarchy, Inspector, Project ...),
  ImGui draw lists and submit, Present (swap, GPU wait, vsync), Play mode (scripts, physics, animation), the limiter's
  own sleep, and "not timed" (reading events and waits between scopes). The parts add up to the frame time.
- **Copy report** puts the same numbers, plus the renderer name, view size and which switches are on, on the clipboard as text.

The window times only while it is open (the profiler is off otherwise), so it costs nothing when closed.
**View > Rendering > Profiler window** opens the bigger profiler (pause, scrub, per-section min / average / max).
The numbers come from `OKAY_PROFILE` scopes, so a new panel shows up under "Panels" as `UI.<name>`.
`OKAY_PERF_DUMP=1` still prints the same sections to the console every two seconds; in the launcher it prints the ms per
frame before the UI, building the UI, drawing it and presenting it, every 120 frames.
`OKAY_STARTUP_REPORT=1` prints the start-up milestones of the editor (main entered, window and painter made, each loading
screen step, main loop, first frame, interactive) and of the launcher (main entered, window and painter made, loading screen,
fonts, games found, main loop, first page built, painted and shown), each with the time since the process started and the
heap in use, and appends them to `logs/startup_report.txt`. `OKAY_MEMORY_REPORT` is described in `docs/engine/core_runtime.md`.

Measured on Linux under Xvfb with Mesa's software OpenGL (llvmpipe) on a loaded 4-core machine (2026-11-28, claude/memory-1):
the editor's own UI work is about 1 ms a frame (panels under 1 ms, OkayUI draw 3 to 4 ms), the rest is llvmpipe (3D view
80 to 100 ms, Present 120 to 180 ms); the launcher's own work is under 1 ms a frame (UI 0.2 to 0.4 ms) and llvmpipe's
present about 100 ms. The first editor frame takes about 1.3 s, almost all in the first 3D render (llvmpipe compiling
shaders) and the heap grows by about 80 MB there; the OpenGL driver also holds about 40 MB of the launcher's 58 MB heap
(with `OKAY_OWN_PAINTER=software` the launcher's private memory is 22 MB instead of 53 MB and its first frame comes 250 ms
sooner). Windows numbers are unverified.

## Switches (View > Rendering)

| Switch | What it does | Default |
|---|---|---|
| Frame rate limit | Sleeps out the rest of each frame (60, 90, 120, 144, 240). Frees the GPU and CPU; with VSync on it is not needed. | Off |
| Slow down when idle | When nothing is clicked or typed for 0.75 s, drops to the idle rate (15 / 20 / 30 / 60 FPS). Any input brings full speed back on the same frame. It stays at full speed while the game plays, a mouse button or key is held, an animation, timeline or model preview plays, the visual-script debugger is on, the loading screen shows, or for 0.75 s after a new Console line (a build, download, import, file reload or background job reporting) (`perf::IdleWake`). Saved as `idle_throttle`; the old `idlethrottle` line is not read, because it was written for everyone. The idle sleep is an idle wait (`Backend::WaitEvents`: poll on the X connection, `MsgWaitForMultipleObjectsEx` on Windows) that a mouse move, key, window event or finished background thread (`okay::thr::ThreadFinishedHook` calls `WakeMainLoop`) ends at once; the frame rate limit's sleep is a plain sleep (`tests/test_platform_wait.cpp`). | On, 30 FPS |
| Viewport resolution | Draws the 3D view at 100 / 75 / 50 / 35 % and stretches it (same setting as Lighting > Rendering > Render scale). | 100 % |
| Performance mode (Scene view) | The Scene view only (the Game view and builds keep the scene's own look): no ambient occlusion, bloom, FXAA or supersampling, render scale at most 75 %, and while the Game panel is closed one shadow cascade at 1024 texels at most. | Off |

Why the shadow change waits for the Game panel to close: the Direct3D renderer reallocates its shadow texture whenever the
size changes, and the Scene and Game views render in the same frame, so different sizes would reallocate twice a frame.

## How it is built

- `editor/EditorPerf.hpp` holds the logic without any window or GPU code: `Break` (a profiler frame split by self time into
  sections), `Stats` (uses `okay::FrameStats`, the same window the game loop uses), `TargetFps` and `SleepSeconds` (the
  limiter and idle decisions), `EffectiveScale` and `RenderSize`, and `Cheaper` (the performance-mode preset). Tests:
  `tests/test_editor_perf.cpp`.
- `editor/main.cpp`: `DrawPerfOverlay`, the menu items in `DrawRenderingMenuItems`, a `PerfModeGuard` that puts the renderer switches back
  after the Scene view is drawn, and the sleep just before the profiler's frame ends (it shows as "Frame limiter sleep").
- Written from the plain definitions (frame rate is 1000 divided by milliseconds; a percentile is read off a sorted list; a limiter sleeps
  out the rest of a frame budget). Nothing is taken from another project.
- Earlier editor speed work that this builds on: the Scene view renders straight into the texture the panel shows on Direct3D 11
  (no read-back), the 1920-pixel cap on the offscreen buffer, auto performance, and the software renderer work in `docs/engine/perf_log.md`.

See `docs/try_it.md` ("Editor frame-time overlay") for the owner steps in the Windows release.
