---
id: editor-fps
row: | editor-fps | Editor frame-time overlay and speed switches | Editor menu **View > Rendering**: Frame-time overlay, Frame rate limit, Slow down when idle, Viewport resolution, Performance mode. Heavy test scenes: `examples\perf\` | New in this change. Real Direct3D 11 numbers on a Windows machine are unverified; sent back by the owner |
---
## editor-fps. Editor frame-time overlay and speed switches

**What it is.** A small window inside the editor that shows your FPS, the frame time in milliseconds, a graph of the last
120 frames, and where each frame goes (3D view, panels, ImGui, present, Play mode). Plus a few switches to make the editor
cheaper. All switches are off until you turn them on. Guide: `docs/editor_performance.md`.

**Steps (read the numbers).**
1. Open the editor (`Tools\OkayEngine.exe`). Open a project or **File > Open...** `<install folder>\examples\perf\many_lights.okayscene`.
2. Open the menu **View**, then **Rendering**. Click **Frame-time overlay**. A window called "Frame time" appears at the top right.
   Drag it where you like. It stays open the next time you start the editor.
3. Leave the mouse over the Scene view for a few seconds, then orbit the camera (hold the right mouse button and move).
4. Read the top line (FPS and ms) and the bars below it. The bar that is longest is where the time goes.
5. Press **Copy report** in the overlay. Paste it into a message to us. That is the number to send back. Also say your
   graphics card and the window size, and whether the line "Renderer:" in the overlay says "Direct3D 11 (GPU, direct to the view)".
6. For more detail, press **Open Profiler** (or **View > Rendering > Profiler window**).

**Steps (try the switches).** All in **View > Rendering**, each takes effect at once. Look at the overlay after each one.
1. **Viewport resolution** > **75%** then **50%**: the 3D view gets a little softer and the "3D view render" bar shrinks.
2. **Performance mode (Scene view)**: the Scene view loses ambient occlusion, bloom and edge smoothing; the Game view does not change.
3. **Frame rate limit** > **120 FPS**: the FPS settles near 120 and a new bar "Frame limiter sleep" shows the time slept on purpose.
   The overlay also says what the frame costs without the sleep ("possible FPS").
4. **Slow down when idle**: take your hands off the mouse and keyboard for a second: FPS drops to the idle rate (30 by default)
   and the GPU rests. Move the mouse: full speed again. It never slows down while the game is playing.
5. Turn each switch off again to compare.

**What you should see.** The overlay's numbers move when you change a switch. The sections add up to the frame time
(the line "Not timed" is what no timer covers).

**Status.** Logic tested headless on Linux (`test_editor_perf`: section maths, history numbers, idle and limiter decisions,
scale, performance mode). The window itself was built and run on Linux with a virtual screen and the software renderer only;
nothing was run on Windows or a real GPU, so the Direct3D 11 numbers are unverified until you send the report.
