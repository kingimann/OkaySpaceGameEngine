# Loop and time

How a frame runs and how the engine keeps time. Headers: `okay/Core/Time.hpp`, `Game.hpp`, `GameLoop.hpp`, `Clock.hpp`, `FrameRecord.hpp`, `FrameStats.hpp`, `TickPacer.hpp`, `Random.hpp`, `RandomExtras.hpp`, `Application.hpp`, from the third pass `Countdown.hpp`, `TimeSnapshot.hpp`, `LoopConsole.hpp`, and from the fourth `FrameOverlay.hpp` (with `Utf8.hpp` and `FramePacing.hpp`, which this page also covers). Tests: `test_game_loop`, `test_clock`, `test_time_extras`, `test_game_state`, `test_random_extras`, `test_application_loop`, from the second pass `test_loop_audit2`, `test_frame_step`, `test_capture_mode`, `test_frame_record`, `test_frame_stats`, `test_tick_pacer`, `test_loop_player_style`, from the third pass `test_loop_audit3`, `test_countdown`, `test_time_snapshot`, from the fourth `test_loop_audit4`, `test_frame_overlay`, `test_random_wave4`, from the fifth `test_loop_audit5`, `test_random_threads`, `test_script_time`, plus the older `test_time_pause`, `test_random`, `test_utf8`, `test_frame_pacing`, `test_timescale`. The third pass is summarised in "Third pass", the fourth in "Fourth pass" and the fifth in "Fifth pass" at the end.

## Which one do I use?

| I want to... | Use |
| --- | --- |
| Run a whole frame the same way in every host (clamp a stall, step time, pause, quit, fixed steps) | `GameLoop` |
| Read this frame's time | `Time::DeltaTime()` (scaled), `Time::UnscaledDeltaTime()` |
| Run gameplay at a steady rate (AI ticks, replays, rhythm) | `GameLoop::OnFixedUpdate`, or `FixedTimestep` on its own |
| Freeze for a hit, ease into slow motion | `Time::HitStop`, `Time::RampTimeScale` |
| Keep a menu animating while the game is paused | `Clock::Get("ui").Delta()` |
| Pause from two places without them fighting | `Game::PushPause("menu")` / `PopPause("menu")` |
| Quit with a reason, an exit code, or "are you sure?" | `Game::RequestQuit(reason, code)`, `Game::SetQuitGuard` |
| Time a timer or a tween | `Scheduler` (see [timers_and_tweens.md](timers_and_tweens.md)) |
| Roll dice, draw loot, pick a point in a circle | `RandomExtras.hpp` |
| Advance a paused game one frame at a time | `GameLoop::RequestStep` |
| Watch the game in slow motion or fast forward while debugging | `GameLoop::SetDebugTimeScale` |
| Render screenshots or video at a fixed frame rate whatever the machine does | `GameLoop::SetCapture(fps, frames)` |
| Keep a hitchy session or a bug report's timing and play it again | `FrameRecorder`, `FramePlayer` |
| Show average FPS, 1% low, worst frame, hitches | `GameLoop::Stats()` (`FrameStats`) |
| Pace a headless fixed-rate loop (a server) and test it with a fake clock | `TickPacer` |
| A weapon or ability that may be used again after N seconds; a jump buffer; a fuse | `Cooldown`, `Countdown` (poll them each frame; `Scheduler` is the one that calls you back) |
| Put the clocks and the random streams back when a save game loads | `TimeSnapshot` |
| Export frame times to a spreadsheet, or ask the console for them | `FrameStats::ToCsv` / `SaveCsv`, `LoopConsole` (`time.stats`, `time.stats_csv`, `time.scale`...) |
| Draw from the seeded stream on a worker thread | `Random::LockShared()` (or `Random::ThreadLocal()` for a stream of its own) |
| A pause button that always works, even under a cutscene's pause | `Game::TogglePause()` |
| "Seconds / frames since this Play began" (the process clocks never rewind) | `Time::SessionTime()`, `Time::SessionFrameCount()` |
| Run N frames, then pause (a debugger button; reproducible with an exact frame length) | `GameLoop::RunFrames(n, seconds)` |
| Slower / faster buttons for the debug time scale | `GameLoop::NextDebugTimeScale(current, +1 / -1)` |
| The same performance text in the player and the editor | `FrameOverlay::Lines(loop)` |
| Pick 3 different spawn points out of 40; scatter trees with no two too close | `rnd::SampleIndices`, `rnd::PoissonDisk2D` |
| Save a half-used shuffle bag or reward pool with the game | `ShuffleBag::Save/Load`, `WeightedTable::Save/Load` |
| Read "since this Play began", real frame time, FPS, the pause from a script | `Time.sessionTime`, `Time.unscaledDeltaTime`, `Time.fps`, `Time.paused` (see "Fifth pass") |
| Bullet time for two seconds, a freeze frame, an eased time scale from a script | `Time.SlowMotion(0.2, 2)`, `Time.HitStop(0.08, 0)`, `Time.RampTimeScale(0.2, 0.5)` |
| Show a real FPS number (a 5 s stall must read as one) | `FrameOverlay::FpsLabel(loop)`, `FrameStats::AverageFps(n)` |
| Blend two fixed states when drawing, with no handle on the loop | `Time::FixedAlpha()` (`GameLoop::Alpha()` for code that has the loop) |
| Draw from the seeded stream on any thread | `Random::Shared()` (now safe), `Random::LockShared()` for several draws that must stay together |
| Spend at most 2 ms a frame on streaming, baking or a path field | `TimeBudget budget(0.002); budget.Begin(); while (work && budget.Allow()) Step();` |
| Replay a recording that had a pause, or play the same random numbers again | `GameLoop::Options::replayPauses`, `replayRandom` (the recorder stores the seed) |

## GameLoop: one frame driver

The console `Application`, the player and the editor's Play mode used to each clamp the frame time, step `Time`, and look at `Game` in their own way. `GameLoop` does it once:

```cpp
#include "okay/Core/GameLoop.hpp"

GameLoop loop;                                              // clamp to 0.25 s, up to 5 fixed steps a frame
loop.OnFixedUpdate = [&](float dt) { Simulate(dt); };       // equal steps (Time::FixedDeltaTime(), 50 a second)
loop.OnUpdate      = [&](float dt) { scene.Update(dt); };   // once a frame, dt = Time::DeltaTime()
loop.OnLateUpdate  = [&](float dt) { camera.Follow(dt); };
loop.OnPause       = [&] { audio.Duck(true); };
loop.OnResume      = [&] { audio.Duck(false); };

bool running = true;
while (running) {
    double real = MeasureRealFrameTime();                    // your timer
    running = loop.BeginFrame(real);                         // false once Game asks to quit
    Draw(loop.Alpha());                                      // see "Smooth drawing" below
}
```

`BeginFrame(realSeconds)` does these, in this order, every frame:

0. Does nothing and returns `false` when the frame budget is used up or a replay has ended; returns at once, without a second frame, when a callback calls `BeginFrame` itself (a modal dialog that pumps the loop).
1. Chooses the frame's real time. A replayed frame (`AttachPlayer`) first, else a debug step (`RequestStep`, only while paused), else the capture rate (`Options::captureFps`), else what the host passed: NaN, infinity and negative numbers count as a 0 s frame.
2. Counts a **hitch** when the real time is above `hitchThreshold` (0.1 s): `HitchCount()`, `LastHitch()`, `WorstHitch()`, the `OnHitch(real, stepped)` callback, and a rate-limited log line with `logHitches`. Records the frame (`AttachRecorder`) and feeds `Stats()`.
3. **Clamps** it to `maxDelta` (0.25 s). A breakpoint, a window drag or an alt-tab no longer becomes one giant step. `FrameDeltaClamped()` is the clamped time, `ClampedFrames()` and `LostRealTime()` say how often and how much was thrown away. With `smoothDelta` it then evens the step out (`Time::SmoothDelta`). A time that was *set* rather than measured (capture, debug step) is exact: not clamped, smoothed or counted.
4. Multiplies it by the debug time scale (1 unless you set one).
5. Fires `OnPause` / `OnResume` when `Game::Paused()` changed since last frame, and `OnStep` for a debug step.
6. Steps `Time` and ticks the clocks (`ClockSet::Global()`).
7. Runs `OnFixedUpdate` once per fixed step that is due (at most `maxFixedSteps`; the surplus is **dropped, never owed**, so one slow frame cannot snowball into slower and slower frames). `FixedStepsThisFrame()`, `DroppedFixedTime()`.
8. Runs `OnUpdate`, then `OnLateUpdate`, both with `Time::DeltaTime()`, then `OnStepsFinished` after the last requested debug step.

Everything is optional. A host that runs its own update just calls `BeginFrame` and reads `FixedStepsThisFrame()`, `Alpha()` and `Time::DeltaTime()`. Callbacks are plain `std::function`; do not replace a callback from inside itself. The frame always runs to the end, then `BeginFrame` returns `false` if a quit was requested (`loop.QuitRequested()`, `loop.ExitCode()`), or if you called `loop.Stop()`.

Options (`GameLoop::Options`): `maxDelta`, `hitchReplacement` (a clamped frame steps this long instead of `maxDelta`, never longer than it was; `Application` uses its target frame time), `hitchThreshold`, `fixedStep` (0 = `Time::FixedDeltaTime()`), `maxFixedSteps` (capped at `GameLoop::kMaxFixedStepsPerFrame`, 10000), `smoothDelta` + `refreshHz`, `firstFrameDelta` (the first measurement usually holds loading time: give it a value to ignore what the host passes), `logHitches`, `tickClocks`. From the second pass: `captureFps`, `frameBudget`, `stepDelta`, `quantizeMicros`, `stopAtPlaybackEnd`, `statsWindow` (below).

A callback may call `Reset()` or `SetOptions()` on the loop mid-frame: the frame's fixed steps keep the count and length they were due with. A fixed update that throws counts as a step that ran; the steps after it are lost, not owed, and the exception goes up to the caller.

The fixed steps run on **scaled** time, so slow motion slows them, a hit stop stops them, and a pause stops them. For an unscaled fixed loop use a separate `FixedTimestep` fed with `Time::UnscaledDeltaTime()`.

### Smooth drawing

Fixed steps and frames do not line up. After the steps, `Alpha()` (always in [0, 1)) says how far the leftover time is into the next step. Draw the blend of the previous and current simulated state:

```cpp
draw = Lerp(previousState, currentState, loop.Alpha());
```

### Inside a fixed step

`Time::InFixedStep()` is true while an `OnFixedUpdate` callback runs (also reset if it throws), and `Time::FixedTime()` is the simulated clock of the fixed steps: the start of the step being run, or the total so far outside one. `Time::FixedDeltaTime()` was already there. Only a host using `GameLoop` advances `FixedTime()`. Scene physics keeps its own accumulator (`Scene::fixedTimeStep`), untouched.

### A new Play session

`loop.BeginSession()` is the clean slate for the editor's Play button or a scene load: `Game::BeginSession()` (below), every clock back to zero, the fixed clock at 0, `Time::SmoothDelta`'s history forgotten, the loop's counters cleared. Your callbacks stay. If the loop had announced `OnPause` and the new session starts un-paused, `OnResume` fires, so audio that was ducked on pause is not left ducked.

### Frame stepping and the debug time scale

For a paused game (any pause reason), `RequestStep` runs exactly N frames of simulation and leaves the game paused:

```cpp
Game::SetPaused(true);                // or any PushPause: the debugger's "pause" button
loop.RequestStep();                   // the next BeginFrame advances one fixed step (0.02 s)
loop.RequestStep(3, 1.0f / 60.0f);    // then three frames of 1/60 s
loop.OnStep         = [&](float dt, int left) { ... };   // before the step's Time update
loop.OnStepsFinished = [&] { ... };                      // after the last requested step's frame
loop.IsStepping();  loop.StepsRemaining();  loop.WasStepFrame();  loop.CancelStep();
```

- A step frame is **exact**: `stepDelta` seconds, whatever the host measured, never clamped or smoothed, never a hitch. The default length is one fixed step (so one `OnFixedUpdate`), or one captured frame in capture mode; `Options::stepDelta` or the request's argument overrides it.
- It goes through the **gameplay time scale and hit stops** (they live in `Time`): at scale 0.5 a 0.02 s step advances 0.01 s; a hit stop's real-time clock runs on the step length.
- The freeze is lifted for that one frame, so `Time::DeltaTime()`, the fixed steps, `OnUpdate` and the "game" and "cinematic" clocks all see it; `Game::Paused()` still reads true, no `OnPause` / `OnResume` fire, and the freeze is back when `BeginFrame` returns (also if a callback throws). The "ui" clock follows the step time.
- `RequestStep` returns `false` (and does nothing) when the game is not paused or `frames` < 1. Pending steps add up (to 1 000 000), and are dropped when the game resumes. A scene that checks `Game::Paused()` to skip its own work still sees "paused": the host asks `loop.WasStepFrame()` when it needs to know.
- Order of events in one frame, always: `OnHitch`, `OnPause` / `OnResume`, `OnStep`, [Time and the clocks step], `OnFixedUpdate` x n, `OnUpdate`, `OnLateUpdate`, `OnStepsFinished`.

```cpp
loop.SetDebugTimeScale(0.25f);        // slow motion for looking at a bug; 4.0 for fast forward
```

The debug scale multiplies the frame time **before** `Time` sees it, so the whole game runs as if the machine were that much slower or faster: it composes with the gameplay time scale and with hit stops (whose durations stretch with it), and `Time::UnscaledDeltaTime()` and the "ui" clock follow it (`loop.FrameDeltaReal()` is still the real time). It belongs to the loop, not to the game: it is never saved, and `Game::Reset`, `Game::BeginSession`, `Time::SetTimeScale`, `Time::ResetForTesting`, a script, `loop.Reset()` and `loop.BeginSession()` all leave it alone. Debug steps ignore it. A fast forward raises the fixed-step cap in proportion (8x lifts 5 steps a frame to 40), so it really runs faster. NaN, infinity and negative values are refused (`false`), the scale is capped at 32, and 0 stops the clock without pausing the game.

### Capture mode: a fixed frame rate whatever the machine does

```cpp
loop.SetCapture(60.0f, 600);          // 60 fps, stop after 600 frames (ten seconds)
while (loop.BeginFrame(0.0)) {        // the argument is ignored
    scene.Render(...);  SavePng(frame++);
}
```

Every `BeginFrame` takes exactly `1/fps` seconds (an exact frame: not clamped, smoothed, or counted as a hitch even at 2 fps), `Time::FrameCount()` advances by one, and hit stops, pause reasons, the time scale, the clocks, fixed steps, the recorder and the statistics all follow it like any other frame. Two runs with different real timing give the same `Time` values frame for frame, which is what golden-image tests need. `SetCapture` refuses a rate that is not in (0, 1000]; `ClearCapture()` goes back to the host's time.

`frames` (or `SetFrameBudget(n)` / `Options::frameBudget`) is a budget counted from the call, `Reset()` or `BeginSession()`: the Nth `BeginFrame` runs and returns `false`, later calls run nothing (so exactly N frames ever run), `FramesRemaining()` counts down (-1 when there is none). The budget works without capture mode too.

### Recording and replaying a session

```cpp
#include "okay/Core/FrameRecord.hpp"

FrameRecorder rec;                         // records until rec.Stop()
loop.AttachRecorder(&rec);
loop.SetFrameMarker(inputBatchNumber);     // optional: one integer per frame, for your own input log
...
rec.Save("session.okayframes");            // atomic: a crash leaves the old file whole

FrameRecording r;  std::string why;
if (!FrameRecording::Load("session.okayframes", r, &why)) Log::Warning(why);
FramePlayer player(r);
loop.AttachPlayer(&player);                // BeginFrame now ignores the host's time
while (loop.BeginFrame(0.0)) { ... }       // the last recorded frame runs and returns false
Log::Info(r.Summary().ToText());           // or .ToJson()
```

A frame is its real time in whole microseconds (what the loop measured, or the capture or step time that replaced it, before the clamp), flags (`kFramePaused`, `kFrameHitch`, `kFrameStepped`, `kFrameSynthetic` for capture) and a marker. While a recorder is attached the loop rounds the measured time to whole microseconds, so a live run and its replay step `Time` by the same numbers: replaying through a `GameLoop` with the **same options** gives identical `Time::DeltaTime`, `ElapsedTimeD`, `UnscaledTime`, `FixedTime`, fixed step counts, alpha and clock values, bit for bit (the tests do this with smoothing and vsync snapping, hit stops, pauses, debug steps, ramps and a debug time scale). The replay's hitches are clamped and counted again exactly as they were. What is *not* in the recording: the loop's options, the debug time scale, and the game's own inputs (replay those from your own log, lining them up with the marker or the frame index); recorded step frames are replayed as steps, so the game must be paused at the same frames.

`FramePlayer` also has `Rewind()`, `Seek(frame)`, `SetLooping(true)` (a benchmark loop), `Remaining()`. With `Options::stopAtPlaybackEnd = false` the host's own time is used again after the last frame.

The text form is small and diffable:

```
okayframes 1
frames 126
micros 2353373
16667*120 16666 250000/h 16667/p@7*2 20000/ps 33333/c@-42
checksum 34c4491ed3039bc2
```

A token is `micros[/flags][@marker][*count]` (flags `p`aused, `h`itch, `s`tepped, `c`apture), so a million-frame capture is one token. Limits: 1 000 000 frames (about 4.6 hours at 60 fps), one hour per frame, 64 MiB of text. Loading is strict (version, header totals and an FNV-1a checksum of every byte above it must agree; every number is range checked; CRLF is accepted) and returns an error message instead of crashing on garbage, truncation or a hostile count; the target is only replaced on success. `Summary()` gives the frame count, average, min, max, median, 95th and 99th percentile frame time, average FPS, hitches (frames over 100 ms by default) and the longest, and the counts of paused, stepped, capture and marked frames, as text or JSON.

### Frame statistics

`loop.Stats()` is a `FrameStats`: the last `Options::statsWindow` (240) frames' real times, fed every frame without allocating, read as plain data when a Profiler window or overlay wants it:

```cpp
const FrameStatsSnapshot s = loop.Stats().Snapshot();   // once per UI refresh: it sorts a copy of the window
Draw("%.0f fps, 1%% low %.0f, 0.1%% low %.0f, worst %.1f ms, %d hitches", s.fps, s.low1PercentFps, s.low01PercentFps, s.worstMs, s.hitches);
```

"1% low" is the average frame time of the slowest 1% of the window (at least one frame) and its FPS; "0.1% low" the same for 0.1%. It records what was *measured* (a 5 s stall reads 5000 ms even though the game only stepped 0.25 s) or the captured, stepped or replayed time. It reuses `Debug/FrameHistory`: the average, min, max and percentiles come from its `Distribution`, and `loop.Stats().SyncTo(history)` copies new frames into a `FrameHistory` (as `totalMs` / `cpuMs`), so `FindSpikes`, `SeriesStats` and the JSON and Chrome-trace exports work on loop frames. `FrameHistory` itself, which is the CPU profiler's scope timeline, was not duplicated.

### The dedicated server's tick

`DedicatedServer`'s loop is not a measured-frame loop: it advances one tick of simulated time (`Time::Step(1 / tickRate)`) per tick and sleeps to the next due time, never stepping by the real elapsed time, so a long stall (a disk hiccup, a suspended VM) costs the server some game time instead of one giant step, and there is nothing for `GameLoop`'s accumulator to catch up. What it needed was its pacing arithmetic under test, so that is now `Core/TickPacer.hpp`, and the server uses it (same tick rate, same order of work, same decisions: a randomized test compares it with the old code on 32 000 ticks of random work and stalls, and `test_dedicated_server` runs the real loop): the next tick is due one tick after the last one *was due*, so the rate does not drift; up to 4 late ticks run back to back, a tick more than 4 late forgets the backlog (`Resyncs()`); and it never schedules a tick more than 4 ticks ahead of the clock, so a clock that jumps backwards cannot make it sleep for the size of the jump (`BackJumps()`; the steady clock the server reads never does it).

### Adopting `GameLoop` in the player and the editor

**The player has adopted it** (third pass). `player/main.cpp` used to clamp, smooth and step time by hand: `if (dt > 0.1f) dt = 0.1f`, `Time::SmoothDelta`, `Time::Step(tick ? stepDt : 0)`, `if (tick) scene.Update(Time::DeltaTime())`, then a `Game::QuitRequested()` check. It now builds one `GameLoop` where the scene starts a session and calls it once a frame:

```cpp
okay::GameLoop::Options loopOpt;
loopOpt.maxDelta = 0.1f;                    // the player's hitch clamp
loopOpt.smoothDelta = cfg.smoothDt;
okay::GameLoop loop(loopOpt);               // refreshHz follows cfg.vsync and the display: SetOptions when it changes
loop.OnUpdate = [&](float dt) { if (loopTick) scene.Update(dt); };
loop.BeginSession();                        // where it called Game::Reset()

// each frame, instead of the clamp + SmoothDelta + Time::Step + scene.Update + quit check:
loopTick = windowFocused || cfg.runInBackground;
if (!loop.BeginFrame(loopTick ? (double)measuredDt : 0.0)) running = false;
```

The FPS readouts showed the measured time cut to 0.1 s (`dt`) in the third pass; both now read the loop's real frame times (fourth and fifth pass). `test_loop_player_style` still copies the *old* per-frame code and runs it next to a configured `GameLoop` on the same synthetic frame sequences (steady at 30, 60 and 144 Hz, spikes at and just over the limit, an alt-tab gap, paused ticks with two reasons, slow motion, hit stops, NaN and negative times, a quit, vsync switched on and off while playing), with and without smoothing and vsync snapping, and compares every `Time` value of every frame for exact equality. The player still behaves as it did in every way that test pins; where the two differ it is on purpose:

- **Identical** with focus kept, in every sequence and every smoothing / vsync setting, and also with `runInBackground` on.
- **`OnUpdate` runs every frame** (UI keeps ticking; the old player skipped `scene.Update` when unfocused), so the player gates it on its own `loopTick` flag as above and an unfocused frame is still a 0 s step with no scene update.
- **After an alt-tab the smoothing history is cleaner.** The old code called `SmoothDelta` on every frame, including unfocused ones, so after a gap its 4-frame average was dragged by the clamped gap frames and the first resumed frames stepped too long. `GameLoop`, fed 0 s, does not feed `SmoothDelta` those frames: it is bit-identical to the old code with that one call skipped while unfocused. The two agree to float rounding a few frames after the window comes back.
- **A measured infinity** is a 0 s frame in `GameLoop` (any non-finite measurement is) and was 0.1 s in the old player (`inf > 0.1f` clamped). No timer produces one.
- **Pause on focus loss** (`Game::SetPauseOnFocusLoss(true)` plus `Game::NotifyFocus`, instead of the `loopTick` flag) gives the same game time, but real time keeps running in the unfocused frames, so `UnscaledDeltaTime`, the "ui" clock and UI animation continue. The player does not use it.
- **A new session starts at time scale 1.** `Game::Reset()` kept a script's time scale into the next session; `loop.BeginSession()` does not. At the player's one call (process start) the two are the same unless something set a scale earlier.
- **New and invisible to the game:** the `Clock` channels now tick in the player (nothing ticked them there before, so `Clock::Get("ui").Delta()` read 0 in a built game), `loop.Stats()` holds frame statistics, `OnFixedUpdate` is available, and hitch logging is off (`logHitches` stays false so the player's log does not change). The F3 overlay read the player's own `dt` then; the fourth pass moved it to `FrameOverlay` (see "Fourth pass"), and the fifth pass moved the `show_fps` readout there too (`FrameOverlay::FpsLabel(loop)`: real frame times, see "Fifth pass"). What was run: the player built from this source and started headless (SDL's dummy video and audio drivers, no display here): a scene whose Action List quits on a one second interval exited with code 0 after about 1.2 s (so `Time` is stepped, the scene updates by the scaled step and the quit is seen), and SIGINT ended a running game cleanly. The real window, a GPU renderer and the `SDL_Delay` pacing were not run; the per-frame numbers are the ones the test pins.

**The editor has adopted it too** (editor hooks). Before, the editor's Play loop clamped to 0.1 s, smoothed, and called `ed.Tick(Game::Paused() ? 0 : playDt)`, which runs `scene.Update(dt)`, but nothing called `Time::Step`: during Play `Time::DeltaTime()` stayed 0 (the Action List "move towards" steps multiply by it), `Time::ElapsedTime()` and `FrameCount()` stood still, and `Time.timeScale`, hit stops, ramps and the `Clock` channels did nothing (the built player has all of them). Now:

- `EditorState` owns one `GameLoop` (`ed.playLoop()`), options as the player's: `maxDelta = 0.1f`, `smoothDelta = g_smoothFrameTime`, `refreshHz = g_vsync ? display refresh : 0` (passed to `ed.PlayFrame` each frame; the loop's options change only when one of them did). Its `OnUpdate` runs the editor's game tick (`ed.playTick`, which `editor/main.cpp` points at `TickGame`, so the visual-script debugger's fiber still wraps it; empty means `ed.Tick`).
- The frame block in `main.cpp` calls `ed.PlayFrame((double)dt, g_smoothFrameTime, refreshHz)` in place of the clamp, `SmoothDelta` and `TickGame(ed, Game::Paused() ? 0 : playDt)` lines. A game paused by its own menu gives `Time::DeltaTime() == 0`, so the scene still ticks with 0 and its UI still updates, as before. **Editing never steps `Time`**: outside Play `PlayFrame` does nothing and `main.cpp` runs the same edit-mode tick as before (`TickGame(ed, clampedDt)`, which does not touch the scene).
- The editor's Pause button is `Game::PushPause("editor")` (`ed.SetPaused(g_paused)`, repeated every frame so `g_paused` stays the one flag the toolbar reads). While it is held the loop still runs (`Time::DeltaTime()` is 0, `Game::Paused()` is true) but the scene is **not updated at all**, as before; the game's own pause menu is the case that still ticks with 0.
- "Step one frame" is `ed.StepFrame()`: it pauses first, then `GameLoop::RequestStep(1, 1/60)`, so the next Play frame advances exactly one frame of 1/60 s (the old step's length), through the time scale and hit stops. For that one frame the scene sees a running game: the editor's own pause reason is lifted around `OnUpdate`, because the controllers (`FirstPersonController`, `ThirdPersonController` and others) return early while `Game::Paused()` and the old Step moved them. A pause the game itself holds (a pause menu) is left alone. Two clicks are two steps; a step still pending when Pause is released is dropped by the loop. The step runs at the next frame's tick (the toolbar is drawn after it), so the result shows one frame after the click.
- `EditorState::Play()` and `Stop()` call `m_loop.BeginSession()`: `Game::BeginSession()` (no pause, no quit, no hit stops or ramps, **time scale back to 1**), the clocks back to zero, the fixed clock at 0 and `SmoothDelta`'s history forgotten. A script that set `Time.timeScale = 0` in one Play no longer freezes the next one, and a Play always starts the `Clock` channels at 0. `Time::ElapsedTime()` and `FrameCount()` are the process's own and keep counting across Play sessions (as in a player that loads another scene); `BeginSession` does not rewind them, on purpose: `Time::SessionTime()` and `SessionFrameCount()` (fourth pass) are the ones that start again at 0.
- The first frame of a Play is the measured one (clamped to 0.1 s), as in the player: the time the Play button spent snapshotting the scene lands in it. Set `ed.playLoop().SetOptions` with `firstFrameDelta` if that ever matters.
- Optional and still free: `ed.playLoop().Stats()` for the Game view's Stats overlay and a Profiler "frame times" graph, `LoopConsole` for `time.stats` and `time.scale` in the Console panel, and `SetDebugTimeScale` / `SetCapture` behind a toolbar menu. None is wired yet.

Tests: `tests/test_editor_hooks.cpp` (ctest `editor_hooks`, it links the ImGui-free `EditorState`): editing does not step Time; Play steps `DeltaTime`, `ElapsedTime`, `FrameCount` and the scene's delta; the 0.1 s clamp; NaN frames; time scale and hit stops reach the scene; the `game` and `ui` clocks tick and a new Play zeroes them; smoothing is the old `SmoothDelta` of the clamped time; Pause freezes the scene and `Time`; a pause menu still ticks with 0; Step is exactly 1/60 s (scaled by the time scale), a running game for that frame, paused again after, and two clicks are two steps; Play and Stop clear a stale pause, quit and time scale. The real window, ImGui and the visual-script debugger's fiber were not run (no display here).

## Time

### Bad numbers can no longer poison the clocks

`Time::Step(NaN)` used to turn `ElapsedTime()` into NaN for the rest of the run, a negative step ran time backwards, and `SetTimeScale(infinity)` followed by a 0 s frame gave `0 * inf = NaN`. Now a frame time that is not a finite number >= 0 counts as 0 s (it is still a frame), and a scale is capped at `Time::kMaxTimeScale` (100). `Time::SmoothDelta` ignores NaN and infinity too.

### Hit stop

A brief slow-down for an impact. It runs on **real** time, restores itself, and **composes** with the time scale, so a script's own `SetTimeScale` is never overwritten or lost:

```cpp
Time::HitStop(0.08f);             // freeze frame: no game time for 80 ms, then back
Time::HitStop(0.25f, 0.2f);       // a quarter second at 20% speed
```

Several at once: the strongest (lowest scale) applies and each ends on its own clock (up to 8 running at once). `DeltaTime()` integrates the scale over the frame, so a 0.1 s stop removes exactly 0.1 s of game time whatever the frame length. `Time::HitStopActive()`, `HitStopRemaining()`, `CancelHitStop()`.

`Time::TimeScale()` still returns the scale you set (and 0 while the game is paused), so code that reads it and writes it back is safe. `Time::EffectiveTimeScale()` is what `DeltaTime` was really multiplied by (scale times hit stop, 0 when frozen). The scene's and the scheduler's "unscaled" tweens and timers divide by that one, so a UI timer keeps ticking through a hit stop.

### Time-scale ramps

```cpp
Time::RampTimeScale(0.2f, 0.5f);   // ease down to slow motion over half a second
Time::RampTimeScale(1.0f, 1.0f);   // and back up over a second
```

Straight line, in real seconds, exact over any frame length. `SetTimeScale` (or another ramp) takes over from wherever the scale is; zero seconds sets it at once. `Time::IsRamping()`, `RampTarget()`, `CancelRamp()`.

`Game::Reset()` cancels hit stops and ramps (a new Play never inherits them) but leaves the scale itself: see "Reset or BeginSession".

### Real and wall-clock time

`Time::DeltaTime()`, `ElapsedTime()` and friends only move when a frame is stepped. These read the machine:

| Call | Meaning |
| --- | --- |
| `Time::RealtimeSinceStartup()` | monotonic seconds since the process started; ignores scale, pause and hitch clamping |
| `Time::UnixSeconds()` | wall clock, seconds since 1970 UTC (can jump if the user changes the clock) |
| `Time::FormatDuration(3723)` | `"1h 02m 03s"` (also `"7s"`, `"2m 05s"`, `"2d 03h 04m 05s"`) |
| `Time::FormatClock(62.5, true)` | `"01:02.500"` (`"1:02:03"` from an hour up) |

Both formatters drop the fraction instead of rounding up, so a timer never shows a time it has not reached; negatives get a leading `-`, NaN gives `"--"`. For tests, `Time::SetClockSources(monotonicFn, unixFn)` replaces the two clocks with your own `double (*)()` functions (`nullptr` puts the real one back).

### Which clocks are monotonic

Everything that decides how far the game moves reads a monotonic clock: `Time::RealtimeSinceStartup()` and `Stopwatch` (`steady_clock`), the console `Application` (`steady_clock`), the windowed player (the SDL performance counter), and the dedicated server's tick (`steady_clock`, and it never steps by elapsed time anyway). A system clock set back by the user cannot make a frame negative or a server sleep for an hour. The wall clock (`Time::UnixSeconds`, `std::time`, `system_clock`) is only used for stamps and expiry: save and backup headers, recovery slots, session tickets, "online" checks, asset-database file times, log lines, and `Random`'s default seed. The player and editor use the 32-bit `SDL_GetTicks()` for double-click, presence and rescan intervals with unsigned subtraction (`now - last`), which survives its 49 day wrap. A custom source that can go backwards (a `Stopwatch` given one) now reads 0 time for the step instead of a negative one.

### Smaller fixes

- `SmoothedFPS()` read 100000 for about a hundred frames when the first frame was a few microseconds (the console loop's first frame always is). The first measured frame is now skipped and the average warms up quickly; steady-state numbers are unchanged.
- `Stopwatch::GameTime()` read the float `ElapsedTime()`, which cannot show half a second after a few weeks of play time; it uses `ElapsedTimeD()`.
- `Time::ResetForTesting()` also clears effects and the fixed clock.
- `Time::ResetSmoothing()` forgets `SmoothDelta`'s history and vsync carry (`GameLoop::BeginSession` calls it); `Time::UnscaledFor(dt)` is the real frame time a scaled step stands for (see the bug table).

## Clocks: which time do I use?

The question the feature map asks: "Pause freezes the timers but my menu animation should keep running." Give each system its own clock:

```cpp
#include "okay/Core/Clock.hpp"

menu.Animate(Clock::Get("ui").Delta());              // keeps running while the game is paused
enemy.Update(Clock::Get("game").Delta());            // frozen by the pause, slowed by slow motion
cutscene.Update(Clock::Get("cinematic").Delta());    // real speed, but holds while the game is paused
Clock::Get("game").SetScale(0.5f);                   // only things on "game" slow down
Clock::Get("cinematic").Pause();                     // hold a cutscene without touching the game
```

| Channel | Follows | Frozen by `Game` pause | Slowed by time scale / hit stop |
| --- | --- | --- | --- |
| `"game"` | `Time::DeltaTime()` | yes | yes |
| `"ui"` | real frame time | no | no |
| `"cinematic"` | real frame time | yes | no |

Any other name creates a channel on first use, on real time, with its own `Scale()` and `Paused()` (`SetHoldsWhileGamePaused(true)` or `SetSource(Clock::Source::Scaled)` change that). Each clock has `Delta()`, `Elapsed()` (a double), `Scale()`, `Paused()`, `SourceDelta()` (what it read before its own scale and pause) and `Ticks()`. `GameLoop` ticks `ClockSet::Global()` once a frame; a host that has no loop calls `ClockSet::Global().TickFromTime()` after `Time::Step`, or `TickOncePerFrame()` when it cannot tell whether the loop already did. `ClockSet` can also be built privately for tests.

## Game: pause, focus and quit

### Pause reasons

`Game::SetPaused(true/false)` is one reason (`"manual"`). Add your own:

```cpp
Game::PushPause("menu");        // the pause menu opens
Game::PushPause("cutscene");    // a cutscene starts under it
Game::PopPause("menu");         // the menu closes: still paused, the cutscene holds it
Game::PopPause("cutscene");     // now it resumes
```

Before this, two systems that paused with `SetPaused` un-paused each other: the first `SetPaused(false)` resumed the game. Pushes of one reason count (two `PushPause("menu")` need two `PopPause("menu")`); `ClearPause(reason)` drops all of one reason; `IsPausedBy(reason)`, `PauseReasons()` (oldest first). The time scale is left alone by every kind of pause.

### Focus loss

```cpp
Game::SetPauseOnFocusLoss(true);   // default off
Game::NotifyFocus(false);          // the platform layer calls this from its focus-lost / focus-gained events
```

The pause is the reason `"focus"`, so a menu opened before an alt-tab is still paused after it.

### Quit with a reason and an exit code

```cpp
Game::RequestQuit("level cleared", 0);
Game::RequestQuit("fatal: out of memory", 3);     // later: return Game::ExitCode() from main
Game::QuitReason();  Game::ExitCode();
```

`RequestQuit()` with no arguments still works and now returns `bool`. Asking twice keeps the first reason, but a non-zero code replaces a zero one, so an error is never hidden by a clean quit that came after. `CancelQuit()` withdraws a request. `ForceQuit(reason, code)` skips the guard.

### The quit guard

```cpp
Game::SetQuitGuard([&] { return !document.HasUnsavedChanges(); });   // false vetoes
if (!Game::RequestQuit("window closed")) ShowSaveDialog();           // the dialog's Quit button calls Game::ForceQuit
```

A vetoed request leaves the game running (`QuitVetoCount()`). The guard runs on the calling thread. A guard that asks to quit itself does not recurse, and one that throws allows the quit, so a bug in it cannot trap the player.

### Reset or BeginSession

`Game::Reset()` clears every pause reason, the quit request (reason, code), the quit guard, hit stops and ramps. It deliberately keeps the time scale, as before. That would mean a script that set `Time.timeScale = 0` during one editor Play leaves the **next** Play frozen (the editor now starts every Play and Stop with `GameLoop::BeginSession()`). Hosts that start a session should call `Game::BeginSession()` (or `GameLoop::BeginSession()`), which is `Reset()` plus time scale 1. A window that is still unfocused with pause-on-focus-loss on starts paused.

### Threads

`Game` is a main-thread API. The exceptions: `QuitRequested()`, `ForceQuit()`, `QuitReason()` and `ExitCode()` are safe from any thread (a worker, a network callback).

## Application

`Application::Run` is now a thin loop on `GameLoop`: with the default `Config` it behaves as it did. New:

- `Config::maxDelta` (0.25), `fixedStep` (0 = `Time::FixedDeltaTime()`), `maxFixedSteps` (5), `catchExceptions` (false). `targetFps` was already there (30; 0 = uncapped).
- `app.loop()` gives the `GameLoop`: set `OnFixedUpdate`, `OnPause` or `OnResume` before `Run`, read the hitch counters after. `OnUpdate` is Run's (it updates the scene).
- **Exception safe.** The terminal session is now closed by a destructor, so a component that throws no longer leaves the terminal raw (the shell echoed nothing and read nothing). By default the exception continues up to the caller after cleanup; with `catchExceptions` it is logged, `LastError()` holds the message, `ExitCode()` is 1 and `Run` returns.
- `app.ExitCode()` is what `Game::RequestQuit(reason, code)` asked for.
- The quit key is ignored while `Input::UICaptured()` (a text field has the keyboard).
- `Quit()` is safe from another thread.

- **Frame pacing** (third pass): `Config::pacing` is `Pacing::Sleep` (the default, unchanged: sleep for what is left of the frame) or `Pacing::Limiter`, which uses `Core/FramePacing`'s `FrameLimiter`. Sleeping rounds every late wake-up into the frame, so the rate lands under `targetFps` (about 29.1 fps at a 30 target when each sleep overshoots by a millisecond; the coarse timers of an older Windows do worse). The limiter schedules each frame from when the last one was *due*, sleeps most of the wait and yields through the last 1.5 ms, so an overshoot is paid back by the next frame and the average is the target. The price is a short yield-spin at the end of each frame. `test_loop_audit3` pins both numbers on a fake clock that sleeps late (old arithmetic under 29.5 fps, limiter within 0.05 of 30) and, on the real clock, only what load cannot break (20 frames at 200 fps take at least 19 waits). Why it is not the default: the old behaviour is what every existing game has, and a busy wait that does nothing for a game that never overshoots is a cost for no gain; flip it when frame rate matters.
- **Options set on `loop()` before `Run` now stand.** `Run` used to replace the loop's options with a fresh struct built from `Config`, silently dropping capture mode, a frame budget, `statsWindow`, hitch logging and the hitch threshold. It now starts from `loop().options()` and overrides only the five that `Config` has a field for (`maxDelta`, `hitchReplacement`, `smoothDelta`, `fixedStep`, `maxFixedSteps`).

## Random

`Random` (xorshift128+) keeps its sequences. Added to it: `Chance(percent)` (0 never, 100 always), `Fork()`, `HashSeed(text)`, `SeedFromString(text)`, `FromString(text)`, `ThreadLocal()`. In `RandomExtras.hpp`, everything works with `Random` and with `Rng` (the PCG generator from the math wave, which already has `Fork(id)`, `Chance(p)`, `Shuffle`, `Choose` and `WeightedIndex`; those were not duplicated):

```cpp
Random rng = Random::FromString("world-42");          // the same stream on every platform and build
Random loot = rng.Fork();                              // an independent child stream; advances rng by one draw
Random& mine = Random::ThreadLocal();                  // a stream of its own per thread (Shared() is safe on any thread since the fifth pass)
{ auto shared = Random::LockShared(); int roll = shared->Range(1, 7); }   // or take the lock for a few draws of the one seeded stream

Vec2 spawn = rnd::PointInCircle(rng, 5.0f, center);    // even by area; also PointOnCircle, PointInRing(rng, inner, outer)
Vec3 dir   = rnd::Direction3D(rng);                    // every direction equally likely; Direction2D too
Vec3 pos   = rnd::PointInBox(rng, min, max);           // also PointInSphere, PointOnSphere, PointInRect

ShuffleBag<std::string> enemies;                       // every item once before any repeats
enemies.Add("grunt", 3);  enemies.Add("brute");        // "grunt" three times a round
std::string next = enemies.Draw(rng);

WeightedTable<std::string> drops;                      // weighted, with and without replacement
drops.Add("potion", 8);  drops.Add("sword", 1);
const std::string* one = drops.Draw(rng);              // nullptr when nothing can be drawn
auto three = drops.DrawDistinct(rng, 3);               // three different entries (indices)

int damage = RollDice(rng, "2d6+3");                   // parse and roll; 0 (or your fallback) on a typo
DiceParse d = ParseDice("4d6kh3");                     // never throws
if (!d.ok) Log::Warning(d.message);                    // "expected the number of sides after 'd' at position 2"
int roll = d.expr.Roll(rng);   d.expr.Min();  d.expr.Max();  d.expr.Average();  d.expr.ToString();
```

- **Seeds from text** use FNV-1a over the bytes and a final mix, so the result does not depend on the platform, the compiler or the locale. Tests pin the values.
- **ShuffleBag**: a single-copy entry never comes twice in a row, also across rounds. An entry added with several copies may repeat (forcing it would pin the rare entries to the front of every round).
- **WeightedTable**: zero, negative, NaN and infinite weights make an entry undrawable; they are not errors. `DrawWithout` / `DrawDistinct` take entries out until `Refill()`.
- **Dice notation**: `d20`, `2d6+3`, `1d20-2`, `4d6kh3` (keep the 3 highest; `kl` the lowest), `d%`, `1d6+1d4+2`, spaces around `+` and `-`. Limits: 1000 dice a term, 100000 sides, 16 terms, constants up to 1000000. Errors (`DiceError`) say what and where: `Empty`, `TooLong`, `UnexpectedCharacter`, `ExpectedNumber`, `ExpectedSides`, `NumberTooLarge`, `ZeroDice`, `ZeroSides`, `TooManyDice`, `TooManySides`, `BadKeep`, `TooManyTerms`.

## Bugs found and fixed

Each has a test that failed before the fix.

| Where | Bug | Repro |
| --- | --- | --- |
| `Time::Step` | NaN made `ElapsedTime` and `UnscaledTime` NaN for good; a negative time ran time backwards; infinity jumped it to infinity | `Time::Step(NaN); Time::Step(0.016f);` then read `ElapsedTimeD()` |
| `Time::SetTimeScale` | infinity was accepted; a 0 s frame then made `DeltaTime` NaN (`0 * inf`) | `SetTimeScale(INFINITY); Step(0)` |
| `Time::SmoothDelta` | an infinite time came back as an infinite step and entered the history | `SmoothDelta(INFINITY)` |
| `Time::SmoothedFPS` | a tiny first frame read as 100000 FPS and took ~100 frames to settle (after 60 normal frames it still read 242) | `Step(1e-5f)` then sixty `Step(0.016f)` |
| `Stopwatch::GameTime` | read the float `ElapsedTime`, so it lost resolution on long sessions | `Step(1e7f)`, start it, `Step(0.5f)`: it read 0 |
| `Random::Range(float, float)` | a span wider than a float overflowed to infinity, so `Range(-FLT_MAX, FLT_MAX)` always returned the top; NaN limits returned NaN | `Range(-3.4e38f, 3.4e38f)` |
| `utf8::Append` | a surrogate (U+D800..DFFF) was written as its three bytes, which no decoder reads back as one character | `Append(s, 0xD800)` then `Length(s)` is 3 |
| `utf8::Next` | at or past the end it read one byte beyond the string; the string form walked its index past the end | `i = s.size() + 5; Next(s, i)` |
| `Game::SetPaused` | two systems pausing un-paused each other (the first resume resumed the game) | pause menu and a cutscene both call `SetPaused` |
| `Application::Run` | a throwing component left the terminal in raw, non-blocking mode (noted in `core_audit_notes.md`) | throw from any `Update` with stdin on a terminal |
| `Application::Run` | the quit key quit while a UI text field had the keyboard | type `q` into a field with `Input::SetUICaptured(true)` |

The second pass added these. Each has a case in `test_loop_audit2` that failed before the fix (the two header-only ones pin the old formula next to the new one):

| Where | Bug | Repro |
| --- | --- | --- |
| `ClockSet::TickOncePerFrame` | ticked a second time in a frame `GameLoop` had already ticked, so a component that called it doubled every clock | `loop.BeginFrame(0.016); ClockSet::Global().TickOncePerFrame();` then `Clock::Get("ui").Elapsed()` reads 0.032 |
| `GameLoop::BeginFrame` | a callback that called `loop.Reset()` (or `SetOptions`) cut the frame's fixed steps short, or changed the length of the rest | `OnFixedUpdate` calling `loop.Reset()` on its first call, 3 steps due: 1 ran |
| `GameLoop::BeginFrame` | a throwing fixed update counted the steps after it as run (`TotalFixedSteps`, while `Time::FixedTime` said otherwise) | throw on the 2nd of 3 steps: `TotalFixedSteps()` read 3 |
| `GameLoop::BeginFrame` | a callback that pumped `BeginFrame` (a modal dialog) ran a second whole frame inside the first; from `OnUpdate` that was endless recursion | `OnUpdate = [&]{ loop.BeginFrame(0.1); }` crashed with a stack overflow |
| `GameLoop::BeginSession` | the pause was dropped without telling `OnResume`, so audio ducked by `OnPause` stayed ducked; a game still paused by a focus loss announced `OnPause` twice | `PushPause("menu")`, one frame, `BeginSession()`: no `OnResume` |
| `GameLoop::BeginSession` | `SmoothDelta`'s history of the last scene was averaged into the first frames of the next | four 0.1 s frames, `BeginSession()`, a 0.01 s frame: stepped 0.02 |
| `Game::RequestQuit` | the quit guard ran from the `std::function` it was stored in, so a guard that cleared or replaced itself ran destroyed code | `SetQuitGuard([keep]{ Game::SetQuitGuard(nullptr); ... })`: its captures were already destroyed |
| `Time::HitStop` | with 8 running, a new one evicted the shortest-lived, which could be the one strong freeze among long weak slow-downs | one 0.05 s freeze, seven 5 s half-speed ones, then a weak 0.01 s one: the freeze vanished |
| `Scene` / `Scheduler` unscaled timers | divided the scaled step by the time scale as it stands *after* the frame, wrong in the frame a hit stop ends or a ramp is moving (a 0.016 s frame counted 0.014 s; during a ramp 0.075 s instead of 0.05 s) | `HitStop(0.05)`, four 0.016 s frames, an unscaled timer of 0.0635 s fired late |
| `FixedTimestep::Alpha` | read above 1 after `SetStep` shortened the step, until the next `Advance` (the loop clamped its own copy) | `FixedTimestep f(0.1f); f.Advance(0.09f); f.SetStep(0.01f);` `Alpha()` read 5 |
| `ParseDice` | the message for an unexpected character put one raw byte of a multi-byte character, or a raw control character, into the text | `ParseDice("2d6\xC3\xA9")`: invalid UTF-8 in `message` |
| `ShuffleBag::Add` | no limit on the total number of copies: 100 entries x 100000 copies allocated 80 MB of slots (1000 entries, 800 MB) | `for (i < 100) bag.Add(i, 100000)` |
| `Stopwatch` | on a source that went backwards (a wall clock set back) `ElapsedSeconds` was negative, and `Stop` kept the negative; a NaN source gave NaN | source 10 then 4 |

Checked and found fine (they have cases in `test_loop_audit2` so they stay fine): `GameLoop` with `dt` exactly one step, 10 microsecond frames, `maxFixedSteps = INT_MAX`, `fixedStep` changed mid-run, a 5 s stall while paused (the accumulator and `Alpha()` stand still), callbacks that pause or quit mid-frame, a quit guard that re-requests the quit or throws, 1000 pause reasons, an empty reason, popping a reason that is not held, a NaN or negative clock scale, a paused channel's `Elapsed`, channels created while a set is being read, hit stops of 0 / negative / NaN length, a ramp to 0, a ramp cut short by `SetTimeScale`, `"1000000d1000000"` (rejected in microseconds), 16 terms of `1000d100000` (the maximum `1 600 000 000` fits an `int`), `ShuffleBag` with no items or one, `WeightedTable` with NaN, infinite, negative and near-maximal weights and with more draws than entries.

Checked in the dedicated server and found fine: a very long frame costs only game time (one tick of `Time::Step` per tick; the schedule restarts after 4 late ticks), and every time source is monotonic (see above). Its pacing is now `TickPacer`.

Checked and found fine in the first pass: the first frame in `Application` (the previous-time stamp is taken after `Start`), `Random` with seed 0 and the all-zero state, `Range` with equal or reversed bounds and `INT_MIN..INT_MAX`, Gaussian at `log(0)` (clamped), weighted picks with zero, negative, NaN and empty lists, Shuffle bias (fair over 60000 shuffles), the maximal-subpart UTF-8 decoding, `Game::Reset` clearing the quit flag, `UnscaledDeltaTime` while paused.

Seen, not changed: `Game::Reset()` keeps the time scale (an existing test pins that; use `BeginSession`), `Random::Shared()` is not thread safe (documented, `ThreadLocal()` added; the third pass added `LockShared()` for threads that must share the seeded stream, see below), `Application` pacing granularity (see above; the third pass added `Pacing::Limiter`), and `ElapsedTime()` is a float by design (`ElapsedTimeD()` is the exact one). `Game::TogglePause()` used to toggle "is the game paused" and did nothing while another system held a pause (fixed in the fourth pass: it flips the manual hold). `WeightedTable::At` and `Weight` do not check the index (use the one `Draw*` returned). A `Stopwatch` keeps every lap it is asked for.

## Not covered here

- `launcher` was left alone. (The player was moved in the third pass, the editor's Play loop in the editor hooks pass: see "Adopting `GameLoop`" above.) The player's real window, SDL timer and `SDL_Delay` pacing were not run, and neither was the editor's window.
- `Okay.hpp` was not touched: include `okay/Core/GameLoop.hpp`, `Clock.hpp`, `FrameRecord.hpp`, `FrameStats.hpp` and `RandomExtras.hpp` directly.
- Frame pacing: `Core/FramePacing` (`FrameLimiter`, `FrameTimer`) paces by sleeping, which is its own job; `Application` can use the limiter (`Config::pacing`). `FrameTimer`'s clamp is the same rule as `GameLoop::maxDelta`, so use one or the other, not both.
- No editor window shows `Stats()`, the step buttons or the debug time scale yet (they are plain data and calls, so a Profiler window or an overlay can); `Profiler.cpp` and `Log.cpp` were not touched.
- A recording does not carry the loop's options, the debug time scale or the game's inputs, only frame times, flags and markers.

## Third pass

What the third pass did: the player runs on `GameLoop` (see "Adopting `GameLoop`" above), `Application` can pace with `FrameLimiter` and keeps the options you set, poll-style timers and per-clock ramps, a save snapshot of the clocks and random streams, frame-time export and console commands, a lock for the shared random stream, cone and triangle helpers, more of UTF-8, and four more bugs. Tests: `test_loop_audit3`, `test_countdown`, `test_time_snapshot`, and cases added to `test_utf8`, `test_random_extras` and `test_loop_player_style`.

### Cooldowns and countdowns

`Core/Countdown.hpp`. Poll-style timers: you give them time each frame, they say what happened. No callback, no clock, no thread, so the same `dt` sequence gives the same answers everywhere. (`Scheduler` is the one that calls you back; `FixedTimestep` is the one for "every N seconds, however long the frame was".)

```cpp
Cooldown fire(0.25f);                          // four shots a second
fire.Tick(dt);                                 // or fire.Tick(Clock::Get("game")): that clock's scale and pause apply
if (Input::Fire() && fire.TryUse()) Shoot();   // fire.Progress() is the fill of an ability icon, fire.Reduce(0.1f) is haste

Countdown jumpBuffer;                          // "pressed up to 0.12 s before landing still jumps"
if (JumpPressed()) jumpBuffer.Start(0.12f);
jumpBuffer.Tick(dt);
if (grounded && jumpBuffer.Consume()) Jump();  // true once; a press that expired does nothing

Countdown fuse(3.0f);
if (fuse.Tick(dt)) Explode();                  // true on the one tick that takes it to zero; fuse.Overshoot() is the part of dt it did not need
```

- `Countdown`: `Start(seconds)`, `Restart()`, `Stop()`, `Pause()` / `Resume()`, `Extend(seconds)` (a stun that gets longer), `Tick(dt)` / `Tick(clock)`, `Done()`, `Active()`, `Running()`, `Remaining()`, `Elapsed()`, `Duration()`, `Progress()` (0 to 1), `Overshoot()`, `Consume()`. A zero-length timer fires on the next tick whatever `dt` is.
- `Cooldown`: `TryUse()`, `Trigger()`, `Ready()`, `Reset()`, `Reduce(seconds)`, `SetDuration`, `SetPaused`, `Tick`, `Remaining()`, `Progress()`. **Held fire is frame-rate independent**: when the tick that made it ready had time left over, a use in that same frame starts the next wait that much shorter, so 0.25 s fires four times a second at 144 fps and at 30 fps alike (without the credit a poll-style cooldown rounds up to a whole number of frames: 3.75 a second at 30 fps). The credit lasts one frame; a weapon that sat ready gets the full wait. Call `Tick` before `TryUse` in a frame.
- A bad number is never an error: a duration that is not a finite number >= 0 counts as 0 (infinity is the cap, `kMaxSeconds`, about 31 years), a `dt` that is not a finite number > 0 adds no time.
- Saving: `timer.Save(writer.Scope("fire"))` / `timer.Load(reader.Scope("fire"))` with the save framework's `SaveWriter` / `SaveReader` (templates, so the header does not include the framework). Floats round-trip exactly; a field an older save never wrote leaves the timer as built; hostile values are cleaned.
- `Stopwatch::OnClock("ui")` is a stopwatch that follows a named game clock (its own scale and pause), looked up on every read.

### Per-clock time-scale ramps

```cpp
Clock::Get("enemies").RampScale(0.2f, 0.5f);   // ease to a crawl over half a second of that clock's time
Clock::Get("enemies").RampScale(1.0f, 1.0f);   // and back up
```

`Clock::RampScale(target, seconds)`, `Ramping()`, `RampTarget()`, `RampSecondsLeft()`, `CancelRamp()`. Straight line, exact over any frame length (a frame's step is the integral of the scale over it, tested against a fine-grained run). A paused clock, or a clock held by the game pause, stands still and so does its ramp. `SetScale`, another ramp or `Reset()` takes over; `seconds` <= 0 or NaN sets the scale at once. It is the same idea as `Time::RampTimeScale`, for one channel. `Clock::Restore(elapsed, ticks)` puts a saved elapsed time back.

### Saving the clocks and the random streams

`Core/TimeSnapshot.hpp`: the loop's own state as one block of a save document, written with the save framework's `SaveWriter` and read back with `SaveReader` (so it travels through `SaveStore`, migrations and the text form like any other block; the framework's formats were reused, nothing new was invented).

```cpp
SaveDocument doc = SaveSystem::CaptureScene(scene);
TimeSnapshot::Capture().AddRandom("loot", lootRng).Write(doc.Writer("time"));

TimeSnapshot snap;
if (TimeSnapshot::Read(doc.Reader("time"), snap)) { snap.Apply(); snap.ApplyRandom("loot", lootRng); }
```

It holds every `Clock` of the global set (scale, pause, elapsed time, a ramp in progress, and for a clock the process does not have yet its source and "holds while paused"), the gameplay time scale (`Time::BaseTimeScale()`), and named `Random::State`s (`"shared"` is `Random::Shared()`, captured by default). After a load the world's own clock is where the save left it, an effect that slowed a clock still slows it, and the next roll is the roll the player would have had (reloading cannot re-roll a chest that was opened; a bug report's save plays out the same).

What it does **not** touch, on purpose: `Time::ElapsedTime()`, `UnscaledTime()` and `FrameCount()` are the process's own clocks and keep going forward (restoring them backwards would make `SaveSystem::PlayTime` stall and a `Stopwatch::GameTime` read nothing; play time has its own field, `SaveSystem::SetPlayTime`); pause reasons, hit stops, the quit request and the loop's debug scale are session state; a built-in clock keeps its code-defined source (a save cannot turn "ui" into a scaled clock). `ApplyOptions` leaves out any part (`clocks`, `timeScale`, `random`). Keys, all optional on read: `v`, `timeScale`, `clocks.n`, `clocks.<i>.name|source|holds|paused|scale|elapsed|ticks|rampTarget|rampLeft`, `randoms.n`, `randoms.<i>.name|s0|s1`. Doubles are text with 17 digits (exact), generator state is a signed 64-bit integer. Reading caps the counts at 1000, cleans every number and skips entries without a name. Main thread only.

### Frame statistics, exported

`FrameStats::ToCsv(header = true)` and `SaveCsv(path, &error)` write the window as `frame,start_ms,frame_ms,fps` (frame numbers count from the last `Clear`, so a window that has moved on starts at its first frame's real number; `start_ms` is where the frame began on the sum of all frame times; locale-independent numbers; the file is written atomically). `LoopConsole` puts the loop on the console (`Core/Console.hpp`):

```cpp
GameLoop loop;
LoopConsole commands(loop);              // time.stats, time.stats_csv <file>, time.scale [x], time.debug_scale [x], time.pause [on|off], time.step [n], time.run [n] [fps], time.overlay
Console::Execute("time.stats");          // "240 frames: 59.8 fps avg (16.72 ms), 1% low 41.2 fps, ..." and the hitch counters
Console::Execute("time.scale 0.25");
```

The prefix is a constructor argument (a client and a server in one process each get their own), the commands go when the object does (declare it after the loop), bad input is an `error:` line and changes nothing (`time.scale nan`, `inf`, `-1`, `0.5x`). `Cvar` was not used: a console variable is a stored setting, and these are actions on a running loop.

### Threads and the shared random stream

(Third pass text, superseded: `Random::Shared()` is now locked inside the generator, see "Fifth pass".) `Random::Shared()` was the plain, fast, unlocked generator (a replay or a seeded test reads it from the main thread). A thread that must draw from that same stream took the lock for a few draws: `{ auto rng = Random::LockShared(); a = rng->Range(0, 6); }`. It is a short spin lock (a header here may not include `<mutex>`), do not nest it, and code that calls `Shared()` directly does not take it. The test has six threads draw 24 000 numbers: the sum and the generator's end state equal the sequential run, and it is clean under a thread sanitizer. `Random::ThreadLocal()` (a stream of its own per thread) is still the cheap answer when you do not need the shared sequence. A debug assert on `Shared()` was not added: the editor ticks the game on a fiber and tests move between threads, so "one thread" cannot be checked without false alarms.

### More random helpers

`rnd::DirectionInCone(rng, axis, halfAngle)` (a weapon's spread, every direction in the cone equally likely by solid angle; `axis` need not be a unit vector and a zero or non-finite one means +Z), `rnd::DirectionInCone2D`, `rnd::PointInTriangle(rng, a, b, c)` (`Vec2` or `Vec3`, even by area), `rnd::Sign(rng)`. Noise (Perlin, Simplex) belongs to Math (36.7) and is not here.

### UTF-8

`Core/Utf8.hpp` gained `IsValid`, `Sanitize` (broken bytes become U+FFFD, a valid string comes back unchanged), `Prev` (a backwards walk, for a text field's cursor and backspace: it visits exactly the pieces a forward walk with `Next` does, tested on thousands of random byte strings), `Substr` / `Truncate` by code point, `TruncateBytes(s, maxBytes)` (the longest prefix that fits and does not cut a character: a name for a 32 byte network field), `ToUtf16` / `FromUtf16`, `ToUtf32` / `FromUtf32` and `ToWide` / `FromWide` (UTF-16 where `wchar_t` is 16 bits, for Windows API calls). A lone surrogate or broken byte becomes U+FFFD in every direction. Case folding and normalisation stay out of scope.

### Bugs found and fixed

Each has a case in `test_loop_audit3` that failed before the fix (checked by putting the old code back).

| Where | Bug | Repro |
| --- | --- | --- |
| `GameLoop::BeginFrame` | with `Options::hitchReplacement` above the frame's real time, a "clamped" frame stepped **more** than it measured and `LostRealTime()` went negative; an infinite replacement made `LostRealTime()` `-infinity` and the frame a 0 s step | `maxDelta = 0.1, hitchReplacement = 1`, `BeginFrame(0.2)` stepped 1.0 s |
| `GameLoop::BeginFrame` | a measured time beyond float range was cast to infinity (undefined behaviour), so `FrameDeltaReal()` and `OnHitch` read infinity | `BeginFrame(DBL_MAX)` |
| `GameLoop::BeginFrame` | a tiny `fixedStep` with `maxFixedSteps = INT_MAX` kept one call busy for billions of callbacks (a config typo hung the game) | `fixedStep = 1e-9`, `maxFixedSteps = INT_MAX`, `BeginFrame(0.1)` |
| `Application::Run` | replaced the loop's options with a fresh struct, silently dropping capture mode, a frame budget, `statsWindow`, hitch logging and the hitch threshold set on `loop()` before `Run` | `app.loop().SetCapture(50, 6)` then `Run`: 30 real-time frames, not 6 captured ones |

### Caller audit: who should call `Game::BeginSession()` instead of `Game::Reset()`

`Reset()` keeps a script's time scale (an existing test pins it); `BeginSession()` is `Reset()` plus scale 1. Every caller in `engine`, `player`, `editor` and `tests`:

| Caller | Today | Should be |
| --- | --- | --- |
| `player/main.cpp` (scene start) | `Game::Reset()` | **done**: `loop.BeginSession()` |
| `editor/EditorState.cpp` `Play()` | `Game::Reset()` | **done**: `m_loop.BeginSession()`, the editor's own `GameLoop`: a script's `Time.timeScale = 0` froze the next Play, and the clocks, fixed clock and smoothing history start clean |
| `editor/EditorState.cpp` `Stop()` | `Game::Reset()` | **done**: `m_loop.BeginSession()` too (the same call, so the loop's counters and the editor's pause reason are cleared with the rest and the edit-mode viewport does not inherit a scale a script left) |
| `engine/src/Net/DedicatedServer.cpp` (server start) | `Game::Reset()` | **done**: `Game::BeginSession()`. `RunDedicatedServer` is a whole-run entry whose options carry no time scale and which steps `Time` itself by a fixed tick, so a scale already set is only stale state (a second run in one process, the tests) that would slow the world and desynchronise the tick from the clients'; a scene script's own `Start()` runs after it and can still set one. `test_dedicated_server` sets a scale before the run and checks it is 1 after |
| `engine/src/Core/Application.cpp` `Run` | `Game::Reset()` | left alone on purpose: a host may set `Time::SetTimeScale` before `Run` (a fast-forward demo), and `BeginSession` would silently undo it. A host that wants a clean slate calls `loop().BeginSession()` first |
| `engine/src/Testing/PlayTest.cpp` (2 calls) | `Game::Reset()` then `Time::ResetForTesting()` | fine: the reset right after sets the scale to 1 |
| `tests/*` (about 20 calls) | `Game::Reset()` | fine: they pin `Reset`'s behaviour or call `Time::ResetForTesting()` too |

### Checked and found fine in the third pass

`GameLoop` with NaN, infinite, negative, denormal and huge frame times (clamp on and off, smoothing on and off, with a recorder attached); every callback calling `BeginFrame` (a pumped dialog) from `OnFixedUpdate`, `OnUpdate`, `OnLateUpdate`, `OnPause`, `OnResume`, `OnHitch`, `OnStep` and `OnStepsFinished` (one call each, never nested); every way of calling into the loop (`Reset`, `BeginSession`, `SetOptions`, `Stop`, `SetCapture`, `AttachRecorder`, `SetFrameBudget`, `RequestStep`, `SetDebugTimeScale`, clearing the statistics) from the fixed update, the update, the late update and `OnPause`; hostile option values (NaN, infinity, negative, tiny and huge, for every float option, plus `INT_MAX` windows and budgets); options changed mid-frame apply from the next frame; `Stop()` inside a frame; `Application` with a throwing `Start`, `Render` or fixed update (rethrown with the terminal restored, or contained), `Quit()` from another thread, a quit from inside `Update`, a second `Run` after a failed one.

### Not done, and why

- **Per-system `Cvar`s** for the time scale and the debug scale: a cvar is a stored setting with change callbacks; the console commands above are actions on a running loop and cover the use. Easy to add if a settings screen wants it.
- **A `Time` snapshot of `ElapsedTime`** (see above): restoring the process clock backwards breaks more than it fixes.
- **`Interval` timer**: `FixedTimestep` already is "equal steps however long the frame was" with an alpha, a cap and dropped-time accounting; a second class would be a second copy.
- **Editor changes**: done in the editor hooks pass ("Adopting `GameLoop`").
- **A frame-time graph or overlay** in the player: done in the fourth pass (`FrameOverlay`, F3).
- **`Game::TogglePause()` while another system holds a pause**: fixed in the fourth pass.

### What changed (third pass)

- `GameLoop`: a clamp never lengthens a frame and ignores an invalid replacement; frame times are held in float range; `kMaxFixedStepsPerFrame` (10000).
- `Application`: `Config::pacing` (`Sleep` default, `Limiter`); options set on `loop()` before `Run` stay.
- `player/main.cpp`: on `GameLoop`, session started with `loop.BeginSession()`, the clocks tick in a built game.
- New: `Countdown`, `Cooldown`, `Clock::RampScale` and friends, `Clock::Restore`, `Stopwatch::OnClock`, `TimeSnapshot`, `FrameStats::ToCsv` / `SaveCsv`, `LoopConsole`, `Random::LockShared`, `rnd::DirectionInCone`, `DirectionInCone2D`, `PointInTriangle`, `Sign`, and the `utf8` additions above.

## Fourth pass

What the fourth pass did: the pause button always works, a script can ask for "time since this Play began", a debugger can "run N frames then pause", the player's F3 panel and the editor can show the same performance text, `Random` and `RandomExtras` were audited and gained `SampleIndices`, `PoissonDisk2D` and saved state for bags and tables, and thirteen more edge-case bugs are fixed. Tests: `test_loop_audit4`, `test_frame_overlay`, `test_random_wave4`.

### The pause button: `Game::TogglePause()`

It used to flip "is the game paused". While a cutscene or a menu held a pause that was already true, so a press called `SetPaused(false)`, which cannot release somebody else's hold: the button did nothing. It now flips the **manual** hold (the reason `SetPaused` uses) and returns whether that hold is on:

```cpp
Game::PushPause("cutscene");
Game::TogglePause();        // true: the player's own hold is added (PauseReasons(): cutscene, manual)
Game::PopPause("cutscene"); // the cutscene ends and the game stays paused, as the player asked
Game::TogglePause();        // false: released, the game runs
```

With nothing else holding a pause it behaves as before. After a press that released the manual hold `Paused()` can still be true: `Game::PauseReasons()` says who else holds it. (`LoopConsole`'s `time.pause` with no argument is the same toggle.)

### What a script sees across Stop and Play

`Time::ElapsedTime()`, `UnscaledTime()` and `FrameCount()` are the **process's** clocks. `BeginSession()` does not rewind them, and that is deliberate: a lot of code keeps a stamp taken from them (an action list's cooldown `now + 5`, `SaveSystem::PlayTime`'s base, the schedulers' "ticked this frame" number, a `Stopwatch::GameTime()`), and a clock that jumped back to 0 would make every such stamp read as the future (a cooldown that never ends until time catches up). So a script that reads `Time.time` in the second Play of an editor session sees a continuous value (120 s, not 0).

What a script that wants "since this Play began" (an intro that shows for the first five seconds of every Play) reads is new:

| Call | Meaning |
| --- | --- |
| `Time::SessionTime()` | scaled game seconds since the last `Game::BeginSession()` (double) |
| `Time::SessionUnscaledTime()` | real seconds since then |
| `Time::SessionFrameCount()` | frames stepped since then |
| `Time::MarkSessionStart()` | what `Game::BeginSession()` calls; moves no process clock |

`GameLoop::BeginSession()` (the editor's Play and Stop, the player's scene start) and `Game::BeginSession()` (the dedicated server) mark the start; `Game::Reset()` alone does not; before the first mark they equal the process clocks; `Time::ResetForTesting()` zeroes the mark with the clocks. Script bindings are not in this pass (they live in `Script/EngineApi.cpp` and `Scripting/OkayScriptVM.cpp`, see "Editor and script hooks" below).

### Run N frames, then pause; slow-motion buttons

`RequestStep` only works on a game that is already paused. The missing half is a game that is running:

```cpp
loop.OnRunFramesDone = [&] { banner.Show("paused after 10 frames"); };
loop.RunFrames(10, 1.0f / 60.0f);        // ten frames of exactly 1/60 s, then the game is paused
loop.RunFrames(10);                      // ten frames of whatever the host measures
loop.RunFrames(10, 0.0f, "editor");      // pause under your own reason (see the editor hook below)
```

- With a length (`frameSeconds` > 0) each run frame is **exact**, like a capture or step frame: the host's measurement is ignored, nothing is clamped, smoothed or counted as a hitch, and a recorder stores it as a synthetic frame. It takes precedence over capture mode for those frames; without a length capture mode (if on) or the host's time is used.
- The pause happens **at the end of the Nth frame**: `Game::Paused()` already reads true when `BeginFrame` returns, `OnRunFramesDone` fires, and the next frame is an ordinary paused one (`OnPause` fires at its start). The reason is `"manual"` by default (so `Game::SetPaused(false)` or `TogglePause` resumes) or the one you pass; it is not pushed twice.
- Only frames that **began with the game running** count: if a menu pauses the game in between, the count waits for the resume.
- A second `RunFrames` replaces the first; `CancelRunFrames()`, `Reset()` and `BeginSession()` clear it; `IsRunningFrames()`, `RunFramesRemaining()`. `frames` is capped at `kMaxStepRequest`, the length at `kMaxStepDelta`.
- On a game that is **already paused** `RunFrames` is `RequestStep(frames, frameSeconds)`: the steps run and the pause stays (`OnStepsFinished` tells you), so one button does the right thing in both states.

The slow-motion button already existed (`SetDebugTimeScale`); what a "slower / faster" pair needs is the next step:

```cpp
loop.SetDebugTimeScale(GameLoop::NextDebugTimeScale(loop.DebugTimeScale(), -1));   // 1 -> 0.5 -> 0.25 -> 0.1 -> 0.05
loop.SetDebugTimeScale(GameLoop::NextDebugTimeScale(loop.DebugTimeScale(), +1));   // 1 -> 2 -> 4 -> 8 -> 16 -> 32
```

`GameLoop::kDebugScalePresets` are 0.05, 0.1, 0.25, 0.5, 1, 2, 4, 8, 16, 32; the ends stay; a scale between presets goes to the next one in that direction; NaN counts as 1. `SetDebugTimeScale`, `SetCapture(fps, frames)` and `RequestStep` themselves were checked again and needed no change.

### The performance text: `FrameOverlay`

`Core/FrameOverlay.hpp` turns the loop's `FrameStats` plus `Time` and `Game` into the lines an overlay shows, so the player's F3 panel and the editor's Game view print the same words:

```cpp
for (const std::string& line : FrameOverlay::Lines(loop)) DrawText(line);       // once per UI refresh (it sorts a copy of the window)
std::vector<float> plot = FrameOverlay::RecentFps(loop.Stats(), 90);             // a fixed-size series for a graph; RecentMs too
```

```
59.8 fps  16.72 ms avg
min 15.90  max 22.10  p99 20.30 ms
1% low 48.2 fps  0.1% low 40.1 fps
hitches 2 (5 total)  worst 130 ms
dropped 1 frame (0.15 s lost)
fixed steps dropped 0.04 s
time x0.50  hit stop 0.08 s  ramp to x1.00  debug x0.25  capture 60 fps  run 7 more  step 3
paused by: menu, cutscene
```

The frame numbers are **real** frame times (`GameLoop::Stats()`), so a 5 s stall reads 5000 ms though the game stepped 0.25 s; "dropped" is the clamp's lost real time (`ClampedFrames()`, `LostRealTime()`), "fixed steps dropped" is `DroppedFixedTime()`. Lines with nothing to say are left out (the time line is always there). `FrameOverlay::Format(snapshot, state)` is the pure form (no global read) for tests; `StateOf(loop)` fills the state; numbers use a dot in every locale. `player/main.cpp`'s F3 panel now draws `FrameOverlay::Lines(loop)` and `RecentFps(loop.Stats(), 90)` instead of its own `dt` smoothing and ring buffer.

### Random: audit, and what was added

Every `Random` and `RandomExtras` function was run against NaN, infinity, zero, negative, empty and `FLT_MAX` arguments, against a state save and restore, and the documented thread entry points against threads.

- **Bad numbers.** `Gaussian(mean, stddev)` with a NaN or infinite argument returned NaN or infinity (a NaN or infinite mean now counts as 0, a spread as 0; a result beyond float range is held at its edge; it always uses two draws). The position helpers (`PointInCircle`, `PointOnCircle`, `PointInRing`, `PointInSphere`, `PointOnSphere`, `PointInRect`, `PointInBox`) returned NaN for a NaN radius or corner; a NaN or infinite radius, corner or centre now counts as 0, `PointInRect(-FLT_MAX, FLT_MAX)` is worked out in double (it returned infinity) and `PointInRing` no longer overflows in the square of a huge radius. They use the same number of draws whatever the arguments, so a stream stays in step. Ordinary arguments give the same numbers as before.
- **Determinism after a state restore.** `Random::GetState` / `SetState` reproduce every draw and helper in one test tour (ranges, Gaussian, shuffle, weighted pick, circle and sphere points, `Fork`, cone, `SampleIndices`, dice). The all-zero state is nudged and never sticks at zero. **A `ShuffleBag` or `WeightedTable` mid-round was not saved by anything**, so a save game that kept the generator but not the bag re-rolled what the player was about to get (see below).
- **Threads.** `Random::Shared()` is still the plain unlocked generator (main thread); `LockShared()` is the one-stream-under-a-lock form (four threads, 4000 draws: the sum and the end state equal the sequential run); `ThreadLocal()` gives each live thread a stream of its own (distinct objects, different sequences). `Random` instances, `Rng`, `ShuffleBag`, `WeightedTable` and `PoissonDisk2D` keep no global state: one per thread is safe, sharing one is not.

New, picked because games need them and nothing in the engine had them (grep for `Poisson`, `Bridson`, `blue noise`, `Sample` found only a shadow filter and unrelated code):

```cpp
std::vector<std::size_t> spawn = rnd::SampleIndices(rng, points.size(), 3);   // three DIFFERENT points, in random order; O(k) memory
std::vector<Vec2> trees = rnd::PoissonDisk2D(rng, 100, 60, 4.0f);            // blue noise: none closer than 4, no big gaps
std::vector<Vec2> rocks = rnd::PoissonDisk2D(rng, 20, 20, 2.0f, /*attempts*/ 30, /*maxPoints*/ 50, /*origin*/ {100, -50});

ShuffleBag<std::string> bag;  /* Add... */   bag.Save(w.Scope("loot"));  bag.Load(r.Scope("loot"));   // mid-round, with the SaveWriter / SaveReader
auto st = bag.SaveState();    bag.RestoreState(st);                                                  // or hold the state yourself
WeightedTable<std::string> deck;               deck.Save(w.Scope("deck")); deck.Load(r.Scope("deck")); // which entries DrawWithout took out
```

- `SampleIndices(g, n, k)`: every set of k and every order equally likely (tested against all ordered pairs of 2 from 4 and from 5); k above n gives n; a pick of 5 from a billion allocates five entries (Floyd's algorithm, then a shuffle); k is capped at 16 777 216.
- `PoissonDisk2D(g, width, height, minDistance, attempts = 30, maxPoints = 100000, origin = {})`: Bridson's algorithm; points in `[origin, origin + size)`, no two closer than `minDistance` (checked pairwise in the test), about 58% of the hexagonal packing (the usual figure for this algorithm), and nearly every probe point has a sample within `2 * minDistance`. It is for areas several times wider than the distance in both directions: a strip thinner than about `2 * minDistance` holds only a few points (the search around a point mostly lands outside), so scatter along a line another way. Deterministic for a generator state; works with `Random` and `Rng`. Empty for a size or distance that is not a finite number > 0, for `maxPoints` 0 and for a grid beyond `kMaxPoissonCells` (4 194 304 cells: a tiny distance in a huge area allocates nothing).
- `ShuffleBag::SaveState / RestoreState` (the entries left this round in draw order, the last drawn, the round count) and `Save / Load` for the save framework; `WeightedTable::DrawnMask / RestoreDrawn / Drawn` and `Save / Load`. The **entries stay yours**: rebuild the bag or table with the same `Add` calls, then load. A state that does not fit the bag (an entry in the round more often than it has copies, an unknown entry, another number of entries or copies, garbage text) is refused, `false`, and the bag is left as it was. A block with no state is `false` too, so an older save leaves the bag fresh. Test: draw mid-round, save, restore into a rebuilt bag with the generator state put back, and the next 25 draws are identical (and differ without the bag state).

### Bugs found and fixed

Each has a case in `test_loop_audit4` or `test_random_wave4`. The failure was seen on the old code (the old file compiled in a scratch program) for every row except `TickPacer`, whose overflow is undefined behaviour: its case checks the saturated result and is there for the sanitizer runs.

| Where | Bug | Repro |
| --- | --- | --- |
| `Game::TogglePause` | did nothing while another system held a pause | `PushPause("cutscene"); TogglePause();` manual hold absent |
| `Time::Step` | a huge frame at a scale above 1 overflowed to infinity and poisoned `ElapsedTime` for good | `SetTimeScale(2); Step(3e38f)` |
| `Clock::Tick` | step times scale overflowed the same way (a clamp turned off, scale 100) | `SetScale(100); Tick(0, 1e37f, false)` |
| `GameLoop::BeginFrame` | the debug scale multiplied a huge frame past float range (undefined conversion, infinity) | `SetDebugTimeScale(32)`, `maxDelta = 0`, `BeginFrame(1e38)` |
| `Time::SmoothDelta` | for a frame time above half the float range, the sum or twice the frame overflowed and the step came back as infinity (a fuzz walk over the loop's controls found it) | `maxDelta = 0`, `smoothDelta = true`, `BeginFrame(1e300)` |
| `Time::SmoothedFPS` | a denormal frame time made `1 / dt` infinite and the average NaN (`inf - inf`) for the rest of the run | `Step(1e-40f)` twice, then normal frames |
| `TimeSnapshot::Apply` | a block with no `timeScale` set the scale to 1 instead of leaving it (`hasTimeScale` is false then) | read a block with only `v` |
| `FrameRecording` | a marker of `INT_MIN` was written and then refused as out of range by the loader | `SetFrameMarker(INT_MIN)`, save, load |
| `Application::Run` | a tiny `targetFps` asked the pacing sleep for 1e30 seconds (overflow of the sleep's clock type); the longest paced frame is now one second (`Application::TargetFrameSeconds`) | `targetFps = 1e-30f` |
| `TickPacer::FinishTick` | clock values near the ends of the 64-bit range overflowed (undefined behaviour); the arithmetic saturates | `Start(INT64_MAX); FinishTick(INT64_MIN)` |
| `FixedTimestep` | a step limit of `INT_MAX` kept one `Advance` busy for billions of steps; capped at `kMaxStepsCap` (100000) | `FixedTimestep f(0.02f, INT_MAX); f.Advance(1e9f)` |
| `Random::Gaussian` | NaN or infinite mean or spread came back as NaN or infinity | `Gaussian(0, NaN)` |
| `rnd::Point*` | a NaN radius or corner gave NaN; a span wider than a float gave infinity; a huge ring radius overflowed | `PointInCircle(rng, NaN)`, `PointInRect(-FLT_MAX, FLT_MAX)` |

Checked and found fine: `GameLoop` with the time scale at 0 and fixed steps (no steps, `Alpha()` stands still, nothing drops), 1000 frames of 0 and negative times, clock ramps that end at 0 or start above 1 (the integral of the scale over any frame length, exact), `Time::RampTimeScale` across a 3 s frame, a replay into a game that is not in the recorded state (a recorded step frame replays as an ordinary frame, hitches are clamped and counted again, a finished or empty recording runs nothing, the host's clock takes over when `stopAtPlaybackEnd` is off), a snapshot block written by a newer build (read) and one with no version (refused), UTF-8 against the Unicode standard's own ill-formed examples (overlong, surrogate, above U+10FFFF, truncated), every prefix of a 2, 3 and 4 byte character, every 1 to 3 byte string over a pool of boundary bytes (forward and backward walks agree, `Sanitize` makes a valid string), `TruncateBytes` at every limit.

### Editor and script hooks (not wired: `editor/*` and the script bindings are outside this pass)

These are the exact call sites, all on `ed.playLoop()` (`EditorState::m_loop`), which `Play()` / `Stop()` already `BeginSession()`:

1. **Stats overlay in the Game view.** While `ed.isPlaying()`, once per UI refresh: `for (const std::string& l : okay::FrameOverlay::Lines(ed.playLoop())) ImGui::TextUnformatted(l.c_str());` and `auto fps = okay::FrameOverlay::RecentFps(ed.playLoop().Stats(), 90); ImGui::PlotLines("##fps", fps.data(), (int)fps.size());` (include `okay/Core/FrameOverlay.hpp`). A "Stats" toggle can sit by the PLAYING / PAUSED pill in `editor/main.cpp` (about line 43327).
2. **Slow motion buttons.** In the toolbar next to Step (`stepHit`, about line 6436): "Slower" is `ed.playLoop().SetDebugTimeScale(GameLoop::NextDebugTimeScale(ed.playLoop().DebugTimeScale(), -1))`, "Faster" the same with `+1`, "1x" is `SetDebugTimeScale(1.0f)`; the label is `ed.playLoop().DebugTimeScale()`. The debug scale survives Play and Stop (by design), so call `SetDebugTimeScale(1.0f)` in `EditorState::Stop()` if it should not.
3. **Run N frames, then pause.** Once, where `ed.playTick` is set (about line 44630): `ed.playLoop().OnRunFramesDone = [] { g_paused = true; };`. The button calls `ed.playLoop().RunFrames(n, 1.0f / 60.0f, "editor")`: the reason must be `EditorState::kPauseReason` (`"editor"`), the one `ed.SetPaused(g_paused)` holds, so when the run ends the loop's pause and `g_paused` agree and the Resume button releases it. While the game is paused the same call steps `n` frames and stays paused (the editor lifts its own pause around the scene tick for step frames already).
4. **Scripts: `Time.sessionTime` and `Time.sessionFrameCount`.** (Done in the fifth pass, see "What a script can reach".) Next to `Time.time` / `Time.frameCount` in `Script/EngineApi.cpp` (the `Time` type, about line 891) and `Scripting/OkayScriptVM.cpp` (`Time.time` about line 2078, builtin `time` about line 3719), return `Time::SessionTime()` and `Time::SessionFrameCount()`; regenerate `docs/engine_api.md`. Until then a script that needs "since Play" can store `Time.time` in `Start`.
5. **Console.** `LoopConsole(ed.playLoop())` (declared after the loop) gives `time.stats`, `time.scale`, `time.debug_scale`, `time.pause`, `time.step`, and from this pass `time.run [n] [fps]` (`RunFrames`: with `fps` every frame is exactly 1/fps s) and `time.overlay` (the `FrameOverlay` text); they do what the buttons do. Without the editor hook 3 the pause that `time.run` ends with is the reason `"manual"`.

### What changed (fourth pass)

- `Game::TogglePause()` flips the manual hold and returns whether it is on.
- `Time`: `SessionTime`, `SessionUnscaledTime`, `SessionFrameCount`, `MarkSessionStart` (called by `Game::BeginSession`); a scaled step is held in float range; `SmoothedFPS` ignores a denormal frame (`kMaxFps`); `FixedTimestep::kMaxStepsCap`.
- `GameLoop`: `RunFrames`, `OnRunFramesDone`, `CancelRunFrames`, `IsRunningFrames`, `RunFramesRemaining`, `NextDebugTimeScale`, `kDebugScalePresets`; the debug-scaled step is held in float range.
- `Clock::Tick` holds its step in float range. `TimeSnapshot::hasTimeScale`. `FrameRecording` accepts `INT_MIN` markers. `Application::TargetFrameSeconds`. `TickPacer` saturates.
- `LoopConsole`: `time.run [n] [fps]`, `time.overlay`.
- New `Core/FrameOverlay.hpp/.cpp`: `FrameOverlay::Lines`, `Format`, `StateOf`, `Text`, `RecentFps`, `RecentMs`. `player/main.cpp`: F3 uses it.
- `Random::Gaussian`, the `rnd::Point*` helpers: bad numbers. New `rnd::SampleIndices`, `rnd::PoissonDisk2D`, `ShuffleBag::SaveState / RestoreState / Save / Load`, `WeightedTable::DrawnMask / RestoreDrawn / Drawn / Save / Load`.
- Not done then (the script bindings were done in the fifth pass): `Time.sessionTime` in the scripts (hook 4), a `PseudoRandom` "bad luck protection" chance (a streak-aware crit chance; small, but nothing asked for it yet), `Rng` was left alone (it belongs to the math wave).

## Fifth pass

What the fifth pass did: a game script can read the session clocks, real time and the real frame rate and can start a hit stop, bullet time or a time-scale ease; the player's `show_fps` readout shows real frame times; `Random::Shared()` is safe on any thread; a time-scale ramp survives a save; a replay can reproduce the recorded run's pauses and its random numbers; a time budget helper; and a dozen more bugs and edge cases are fixed. Tests: `test_loop_audit5` (207 checks), `test_random_threads` (20), `test_script_time` (96).

### What a script can reach

Before this pass a script could read `Time.time` and `Time.deltaTime` and set `Time.timeScale`, and nothing else about the loop (the OkayScript builtin `fps()` was `1 / dt`). Added to both script languages (the typed one lives in `Script/EngineApi.cpp`, so the visual-script nodes and `docs/engine_api.md` list them too; OkayScript is `Scripting/OkayScriptVM.cpp`):

| Typed / dotted | OkayScript builtin | Meaning |
| --- | --- | --- |
| `Time.sessionTime` | `session_time()` | game seconds since this Play began (`Time.time` is the process's clock and keeps counting across Plays) |
| `Time.sessionUnscaledTime` | `session_unscaled_time()` | real seconds since this Play began |
| `Time.sessionFrameCount` | `session_frame_count()` | frames since this Play began |
| `Time.unscaledDeltaTime` | `unscaled_dt()` | real seconds of the last frame: a pause, slow motion and hit stops do not change it |
| `Time.unscaledTime` | - | real seconds since the game started |
| `Time.fps` | `fps()` | frames per second over the last 30 REAL frame times of the running loop (`GameLoop::Current()->Stats().AverageFps(30)`); 0 / the last frame's rate with no loop |
| `Time.hitchCount` | `hitch_count()` | frames over 0.1 s since this Play began |
| `Time.paused` | `is_paused()` | the game is paused (any reason) |
| `Time.PushPause(reason)`, `Time.PopPause(reason)`, `Time.IsPausedBy(reason)` | `push_pause`, `pop_pause`, `is_paused_by` | pause with a reason of your own (`Game::PushPause`): the game stays paused until every reason is let go, so two scripts that pause never resume each other (`pause_game(bool)` stays the one shared "manual" reason); a hold a script left is cleared by the next `BeginSession` |
| `Time.HitStop(seconds, scale)` | `hit_stop(seconds [, scale])` | a freeze frame (scale 0) or a brief slow-down; real time, ends by itself |
| `Time.SlowMotion(scale, seconds)` | `slow_motion(scale, seconds)` | bullet time: the same effect with the arguments the way a game reads them (`Time.SlowMotion(0.2, 2)`); it composes with `Time.timeScale` and ends by itself |
| `Time.RampTimeScale(target, seconds)` | `ramp_timescale(target, seconds)` | ease the time scale in a straight line over real seconds |

Two answers that were wrong are fixed: OkayScript's `Time.unscaledDeltaTime` and `Time.fixedDeltaTime` both returned this frame's SCALED delta (so the "unscaled" one read 0 in a pause and half of itself at `timeScale` 0.5); they now read `Time::UnscaledDeltaTime()` and `Time::FixedDeltaTime()`. `fps()` was `1 / the scaled delta` (0 in a pause, double at half speed); it now reads the loop's real frame times. What a script still cannot do: per-clock `Clock` channels and their scales, the debug time scale, frame statistics beyond `fps` and `hitchCount`. Those belong to the host, not to game logic.

`GameLoop::Current()` is how the script layer (and any code with no handle on the loop) reaches the numbers: the loop that ran the latest frame, `nullptr` when none has or it was destroyed (the destructor clears it). With two loops in one process it is the one that ran last. Main thread only.

### The show_fps readout, and real FPS

`player/main.cpp`'s `show_fps` readout smoothed `dt`, the frame time cut to 0.1 s, so it never read under 10 FPS and a stall hardly moved it. It now draws `FrameOverlay::FpsLabel(loop)` ("FPS 60"): `FrameStats::AverageFps(30)`, the frames in the newest 30 divided by how long they really took. A 5 s stall shows as 2 FPS for the next 30 frames; with no frame yet it reads "FPS --". `FrameStats::AverageFps(n)` is O(n) with no allocation, so a script or a HUD may call it every frame. The editor's Game view stats (not wired, see the hooks below) can use the same call.

### `Random::Shared()` is safe on any thread

It was a plain generator: two threads that drew from it raced on its two state words (a thread sanitizer reports it on the first draw) and a `Seed` or `GetState` from another thread could see half of an update. The fix keeps what code relies on: the seed API (`Random::Shared().Seed(42)`), the sequence on one thread (a test pins the first numbers against a plain `Random` of the same seed) and the type (`Random&`, so every `rnd::` helper still takes it).

- Every step of the shared generator, and `Seed`, `SetState` and `GetState` on it, is taken under a short recursive spin lock (atomics only: this header may not include `<mutex>`). Any other generator (your own, a copy of `Shared()`, `ThreadLocal()`) has no lock and costs one predictable branch more per draw.
- It is one stream shared by all threads, so the numbers the threads draw are the sequential numbers in some interleaving: the sum of N draws and the generator's end state are the same whoever drew what (the test), but which thread gets which number is the scheduler's. A replay that needs the same numbers at the same events draws on one thread or gives each thread a `ThreadLocal()` stream. A helper that makes several draws (`Gaussian`, a point in a circle) can be interleaved with another thread's draws between them: hold `Random::LockShared()` across it when the draws must stay together.
- `LockShared()` is now recursive on one thread (a nested handle, or a draw made while holding one, used to wait for itself forever).
- A thread-local default was the other option; it would have made `Shared().Seed(...)` on the main thread silently not apply to a worker. The lock keeps one seed for the process.
- Tested with `std::thread` (`test_random_threads`: six threads draw 120 000 numbers straight from `Shared()` and the sum and end state equal the sequential run; a reader thread takes `GetState()` snapshots while four threads draw and every snapshot is a real state of the sequence; `Seed` / `SetState` racing draws; `LockShared` pairs stay consecutive; nested locks) and built standalone with `-fsanitize=thread` (the file includes only `Random.hpp`): clean. The unlocked header reports a data race on the first draw in the same program, and its nested `LockShared` test never finishes.

### A ramp survives a save; Cooldown credit

`TimeSnapshot` saved `Time::BaseTimeScale()`, which during a `Time::RampTimeScale` is the half-way value, and nothing about where the ramp was heading: a game saved while easing out of slow motion loaded stuck at the half-way scale for good. It now also stores the ramp (`timeRampTarget`, `timeRampLeft`, written only while one runs; a block without them loads as before) and `Apply` restarts it from the saved scale in the same straight line, so the game time the loaded run gets equals what the saved run got. Hit stops stay session state (they last fractions of a second). The clock ramps and groups were checked too: a fresh `ClockSet` restored from a snapshot taken mid-ramp, with a paused clock holding its ramp, a game-clock held by the pause and custom clocks the new set did not have, runs the next 23 frames to the same elapsed times and scales as the original (`test_loop_audit5`).

`Cooldown::Save` left out the one-frame held-fire credit, so a save taken after the tick that made the cooldown ready and before the shot gave a longer wait than the same save a moment earlier. It is written only while a credit is pending (`credit`; older saves have none and load as before; a credit on a cooldown that is still counting down is ignored).

### Replaying a run that had a pause; seeds in a recording

- **`GameLoop::Options::replayPauses`** (default false, the old behaviour). A replay only reproduced the pauses its host reproduced itself, so a pause the recorded run got from somewhere the replay has no copy of left the replayed game moving through those frames: the pause `RunFrames` ends with, the editor's Pause button, a menu the inputs are not replayed for. With it on, a replayed frame the recording marks paused is run paused (the loop holds `Game::PushPause(GameLoop::kReplayPause)`, "replay", only when the game is not paused already, and lets go on the next frame that was running, when the replay ends, on `Reset()`, `BeginSession()` or `AttachPlayer(nullptr)`). Recorded debug-step frames then replay as steps on a game that is not paused. A test records a `RunFrames(3)` that ends in a pause, four frames while paused, a resume, a menu pause, and replays it: without the option the Time values part ways at the first paused frame, with it all 17 frames equal the live ones bit for bit.
- **The shared random stream in a recording.** `FrameRecorder` stores `Random::Shared()`'s state with the first frame it records (`SetRecordRandom(false)` turns that off), the text form carries it as one optional line (`random <16 hex digits> <16 hex digits>` between `micros` and the frames; a recording without it, every file written so far, loads and saves as before; the version stays 1), and `GameLoop` puts it back when the replay starts, and at each lap of a looping replay (`Options::replayRandom`, default true). `FrameRecording::HasRandom()`, `RandomState()`, `SetRandom()`, `ClearRandom()`. With the same inputs the replayed game then rolls the numbers the recorded one rolled.

### Smaller additions

- **`Time::FixedAlpha()`**: the loop's `Alpha()` (how far the leftover time is into the next fixed step) for code that has no handle on the loop. Published by `GameLoop` each frame, reset by `BeginSession`, cut to [0, 1). `Time::RampSecondsLeft()` is the remaining real time of a running ramp.
- **`TimeBudget`** (`Core/TimeBudget.hpp`, header only): a slice of time for work that must not blow a frame. `TimeBudget budget(0.002); budget.Begin(); while (work && budget.Allow()) Step();` or `budget.Run([&]{ return Step(); })`. At least one item per slice (a step longer than the budget still finishes), the clock is `Time::RealtimeSinceStartup()` or a function you give (a test is deterministic), a budget that is not a finite number >= 0 is 0, a clock that goes back or is NaN is no time passing.
- **`FrameStats::AverageFps(n)`**, **`FrameOverlay::FpsLabel(stats | loop, n = 30)`**.

### Bugs found and fixed

| Where | Bug | Test |
| --- | --- | --- |
| `Random::Shared()` | data race between threads; torn `GetState`; the lock handle deadlocked when nested | `test_random_threads` |
| OkayScript `Time.unscaledDeltaTime` / `Time.fixedDeltaTime` | answered this frame's scaled delta (0 in a pause) | `test_script_time` |
| OkayScript `fps()` | `1 / scaled delta`: 0 in a pause, double at half speed | `test_script_time` |
| player `show_fps` | read the frame time cut to 0.1 s: never under 10 FPS | `test_loop_audit5` (the label numbers) |
| `TimeSnapshot` | a running `Time::RampTimeScale` was not saved: the load stuck at the half-way scale | `test_loop_audit5` |
| `Cooldown::Save` | the held-fire credit pending at the save was dropped | `test_loop_audit5` |
| `Countdown`, `Cooldown` | the remaining time was a float: a tick of 1/60 s is not a whole number of float steps once the timer is long, so a one day countdown ran 6% off after ten minutes of ticks and a one week one never counted down at all (a tick is under half a float step there). The time is kept in double now (the API still reads and writes floats; short timers read the same floats as before) | `test_loop_audit5` |
| `LoopConsole` | `time.scale 1e300` and `time.debug_scale 1e300` cast a double beyond float range (undefined); `time.step 2.5` / `time.run 2.5` silently ran 2 frames; `time.run 3 1e-300` started a run whose frame length was dropped | `test_loop_audit5` |
| `GameLoop` replay | a pause the replay's host did not reproduce was not replayed (the game kept moving, the `Time` values parted ways) | `test_loop_audit5` (`replayPauses`) |
| `Application::Run` | did not start a session: `Time::SessionFrameCount()` and `SessionTime()` read on from an earlier run (6 after two 3 frame runs) | `test_loop_audit5` |
| `FrameTimer::Tick(now)` | a NaN or infinite reading became the new "last" and every later delta was NaN: a frame time of 0 for the rest of the run | `test_loop_audit5` |
| `FrameLimiter` | a tiny target rate (1e-30 fps) planned a sleep of 1e30 seconds; a frame is now paced at most an hour (`kMaxFrameSeconds`) | `test_loop_audit5` |

### Checked and found fine in the fifth pass

`Application::Run`: a second run on the same object, a time scale set before `Run` (kept), pacing arithmetic for NaN, infinite, negative and sub-1 target rates, the frame budget, `Quit()`; the player's `fpsCap` sleep and the focus paths (an unfocused frame is a 0 s step, the first frame after refocus is a normal one); `FrameRecording` replay of capture-mode frames (exact, not clamped, the recorded options are not needed) and of run-frame and step frames; `TimeSnapshot` of a block with no `v`, a newer `v`, no `timeScale`, no ramp keys and hostile ramp keys; `Countdown` save and load of every state; clock ramps across a save.

### Editor and script hooks (not wired: `editor/*` is outside this pass)

1. **`show_fps` in the editor's built-game settings** needs nothing: it is the player. The editor's Game view can draw `okay::FrameOverlay::FpsLabel(ed.playLoop())` for a one-line FPS (include `okay/Core/FrameOverlay.hpp`), next to the PLAYING pill in `editor/main.cpp` (about line 43327).
2. **The editor's script help** (`editor/main.cpp`, the `{"Time.deltaTime", ...}` table at about line 12526) can list `Time.sessionTime`, `Time.sessionFrameCount`, `Time.unscaledDeltaTime`, `Time.fps`, `Time.paused`, `Time.HitStop(seconds, scale)`, `Time.SlowMotion(scale, seconds)`, `Time.RampTimeScale(target, seconds)`, `Time.PushPause(reason)`, `Time.PopPause(reason)` and `Time.IsPausedBy(reason)`, and the function list line `fapi("time() / dt() / fps()", "timing")` (about line 11536) the new builtins (`session_time()`, `unscaled_dt()`, `is_paused()`, `push_pause`, `hit_stop`, `slow_motion`, `ramp_timescale`). The text of every row is in `docs/engine_api.md` and `docs/scripting_reference.md`.
3. **Replaying a recording in the editor**: when a recorded session is replayed (a bug report's timing), set `ed.playLoop().SetOptions(o)` with `o.replayPauses = true` so the Pause button's frames replay as paused. `replayRandom` is already on.
4. **Frame recorder button**: `FrameRecorder` now stores the random state itself; nothing extra is needed to get deterministic random numbers in a replay.

### What changed (fifth pass)

- New: `Core/TimeBudget.hpp`, `Time::FixedAlpha` / `SetFixedAlpha` / `RampSecondsLeft`, `GameLoop::Current()` / `kReplayPause` / `Options::replayPauses` / `replayRandom`, `FrameStats::AverageFps`, `FrameOverlay::FpsLabel`, `FrameRecording::HasRandom` / `RandomState` / `SetRandom` / `ClearRandom`, `FrameRecorder::SetRecordRandom` / `RecordRandom`, `TimeSnapshot::timeRamping` / `timeRampTarget` / `timeRampSecondsLeft`, `FrameLimiter::kMaxFrameSeconds`.
- Script surface: the `Time` members and builtins above (`Script/EngineApi.cpp`, `Scripting/OkayScriptVM.cpp`), `docs/engine_api.md` regenerated, rows in `docs/scripting.md` and `docs/scripting_reference.md`.
- Changed: `Countdown` / `Cooldown` keep their time in double, `Random` (locked `Shared()`, copy constructor and assignment that never copy the lock), `Application::Run` marks the session start, `LoopConsole` number handling, `Cooldown::Save` / `Load`, `FrameTimer::Tick`, `FrameLimiter::SetTargetFps`, `player/main.cpp` (`show_fps`).
- Not done: Cvars for the scales, per-system random streams in `TimeSnapshot` for `ThreadLocal()` (there is no name to save them under), a frame-time overlay in the editor (hook 1).
