# Loop and time

How a frame runs and how the engine keeps time. Headers: `okay/Core/Time.hpp`, `Game.hpp`, `GameLoop.hpp`, `Clock.hpp`, `FrameRecord.hpp`, `FrameStats.hpp`, `TickPacer.hpp`, `Random.hpp`, `RandomExtras.hpp`, `Application.hpp`. Tests: `test_game_loop`, `test_clock`, `test_time_extras`, `test_game_state`, `test_random_extras`, `test_application_loop`, and from the second pass `test_loop_audit2`, `test_frame_step`, `test_capture_mode`, `test_frame_record`, `test_frame_stats`, `test_tick_pacer`, `test_loop_player_style`, plus the older `test_time_pause`, `test_random`, `test_utf8`.

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

Options (`GameLoop::Options`): `maxDelta`, `hitchReplacement` (a clamped frame steps this long instead of `maxDelta`; `Application` uses its target frame time), `hitchThreshold`, `fixedStep` (0 = `Time::FixedDeltaTime()`), `maxFixedSteps`, `smoothDelta` + `refreshHz`, `firstFrameDelta` (the first measurement usually holds loading time: give it a value to ignore what the host passes), `logHitches`, `tickClocks`. From the second pass: `captureFps`, `frameBudget`, `stepDelta`, `quantizeMicros`, `stopAtPlaybackEnd`, `statsWindow` (below).

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

Neither `player/main.cpp` nor the editor's Play loop was changed (other people are working in them). Each already does the same four things by hand: `if (dt > 0.1f) dt = 0.1f`, `Time::SmoothDelta`, `Time::Step(tick ? stepDt : 0)`, `if (tick) scene.Update(Time::DeltaTime())`, then a `Game::QuitRequested()` check. `test_loop_player_style` copies the player's per-frame code and runs it next to a configured `GameLoop` on the same synthetic frame sequences (steady at 30, 60 and 144 Hz, spikes at and just over the limit, an alt-tab gap, paused ticks with two reasons, slow motion, hit stops, NaN and negative times, a quit), with and without smoothing and vsync snapping, and compares every `Time` value of every frame for exact equality. To adopt it:

```cpp
GameLoop::Options o;
o.maxDelta = 0.1f;                  // what the player clamps to today
o.smoothDelta = cfg.smoothDt;  o.refreshHz = cfg.vsync ? s_refreshHz : 0.0f;
o.logHitches = true;
GameLoop loop(o);
bool tick = true;
loop.OnUpdate = [&](float dt) { if (tick) scene.Update(dt); };    // the editor: TickGame(ed, dt)
loop.BeginSession();                                              // where they call Game::Reset() today

// each frame, instead of the clamp + SmoothDelta + Time::Step + scene.Update + quit check:
tick = windowFocused || cfg.runInBackground;
if (!loop.BeginFrame(tick ? realDt : 0.0)) running = false;
```

The test pins every place where the two differ, all of them on purpose:

- **Identical** with focus kept, in every sequence and every smoothing / vsync setting, and also with `runInBackground` on.
- **`OnUpdate` runs every frame** (UI keeps ticking; the player skipped `scene.Update` when unfocused), so the host gates it on its own `tick` flag as above. Without the gate the scene would get a 0 s update on every unfocused frame.
- **After an alt-tab the smoothing history is cleaner.** The player calls `SmoothDelta` on every frame, including unfocused ones, so after a gap its 4-frame average is dragged by the clamped gap frames and the first resumed frames step too long. `GameLoop`, fed 0 s, does not feed `SmoothDelta` those frames: it is bit-identical to the player's code with that one call skipped while unfocused. The two agree to float rounding a few frames after the window comes back (the history ring is aligned differently, so four numbers are summed in another order).
- **A measured infinity** is a 0 s frame in `GameLoop` (any non-finite measurement is) and 0.1 s in the player (`inf > 0.1f` clamps). No timer produces one.
- **Pause on focus loss** (`Game::SetPauseOnFocusLoss(true)` plus `Game::NotifyFocus`, instead of the `tick` flag) gives the same game time, but real time keeps running in the unfocused frames, so `UnscaledDeltaTime`, the "ui" clock and UI animation continue. That is the point of it, and it differs from `Step(0)`, which zeroes unscaled time too.
- **`Game::Reset()` keeps a script's time scale** into the next session (the player calls it today); `loop.BeginSession()` does not.
- Extras that change no value: fixed steps, hitch counters, the clocks, statistics, recording.

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

`Game::Reset()` clears every pause reason, the quit request (reason, code), the quit guard, hit stops and ramps. It deliberately keeps the time scale, as before. That means a script that set `Time.timeScale = 0` during one editor Play leaves the **next** Play frozen. Hosts that start a session should call `Game::BeginSession()` (or `GameLoop::BeginSession()`), which is `Reset()` plus time scale 1. A window that is still unfocused with pause-on-focus-loss on starts paused.

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

Not changed: frame pacing still sleeps for the remaining time, so on platforms with coarse sleep granularity the frame rate can land under `targetFps`. A better limiter is `Core/FramePacing` in the pending frame-pacing change; it was not duplicated here.

## Random

`Random` (xorshift128+) keeps its sequences. Added to it: `Chance(percent)` (0 never, 100 always), `Fork()`, `HashSeed(text)`, `SeedFromString(text)`, `FromString(text)`, `ThreadLocal()`. In `RandomExtras.hpp`, everything works with `Random` and with `Rng` (the PCG generator from the math wave, which already has `Fork(id)`, `Chance(p)`, `Shuffle`, `Choose` and `WeightedIndex`; those were not duplicated):

```cpp
Random rng = Random::FromString("world-42");          // the same stream on every platform and build
Random loot = rng.Fork();                              // an independent child stream; advances rng by one draw
Random& mine = Random::ThreadLocal();                  // for a worker thread: Shared() has no lock

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

Seen, not changed: `Game::Reset()` keeps the time scale (an existing test pins that; use `BeginSession`), `Random::Shared()` is not thread safe (documented, `ThreadLocal()` added), `Application` pacing granularity (see above), and `ElapsedTime()` is a float by design (`ElapsedTimeD()` is the exact one). `Game::TogglePause()` toggles "is the game paused", so while another system holds a pause it cannot add a manual one (it does nothing); use `PushPause` / `PopPause` for your own pause. `WeightedTable::At` and `Weight` do not check the index (use the one `Draw*` returned). A `Stopwatch` keeps every lap it is asked for.

## Not covered here

- `player/main.cpp`, the editor's Play loop and `launcher` were left alone; "Adopting `GameLoop`" above is what to change in them, and `test_loop_player_style` is the safety net for it. The player's real window, SDL timer and `SDL_Delay` pacing were not run.
- `Okay.hpp` was not touched: include `okay/Core/GameLoop.hpp`, `Clock.hpp`, `FrameRecord.hpp`, `FrameStats.hpp` and `RandomExtras.hpp` directly.
- Frame pacing: `Core/FramePacing` (`FrameLimiter`, `FrameTimer`) has since reached `main`; this work was built before it and does not depend on it. It paces by sleeping, which is its own job; `FrameTimer`'s clamp is the same rule as `GameLoop::maxDelta`, so use one or the other, not both.
- No editor window shows `Stats()`, the step buttons or the debug time scale yet (they are plain data and calls, so a Profiler window or an overlay can); `Profiler.cpp` and `Log.cpp` were not touched.
- A recording does not carry the loop's options, the debug time scale or the game's inputs, only frame times, flags and markers.
