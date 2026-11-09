# Loop and time

How a frame runs and how the engine keeps time. Headers: `okay/Core/Time.hpp`, `Game.hpp`, `GameLoop.hpp`, `Clock.hpp`, `Random.hpp`, `RandomExtras.hpp`, `Application.hpp`. Tests: `test_game_loop`, `test_clock`, `test_time_extras`, `test_game_state`, `test_random_extras`, `test_application_loop`, plus the older `test_time_pause`, `test_random`, `test_utf8`.

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

1. Cleans the measurement. NaN, infinity and negative numbers count as a 0 s frame.
2. Counts a **hitch** when the real time is above `hitchThreshold` (0.1 s): `HitchCount()`, `LastHitch()`, `WorstHitch()`, the `OnHitch(real, stepped)` callback, and a rate-limited log line with `logHitches`.
3. **Clamps** it to `maxDelta` (0.25 s). A breakpoint, a window drag or an alt-tab no longer becomes one giant step. `FrameDeltaClamped()` is the clamped time, `ClampedFrames()` and `LostRealTime()` say how often and how much was thrown away. With `smoothDelta` it then evens the step out (`Time::SmoothDelta`).
4. Fires `OnPause` / `OnResume` when `Game::Paused()` changed since last frame.
5. Steps `Time` and ticks the clocks (`ClockSet::Global()`).
6. Runs `OnFixedUpdate` once per fixed step that is due (at most `maxFixedSteps`; the surplus is **dropped, never owed**, so one slow frame cannot snowball into slower and slower frames). `FixedStepsThisFrame()`, `DroppedFixedTime()`.
7. Runs `OnUpdate`, then `OnLateUpdate`, both with `Time::DeltaTime()`.

Everything is optional. A host that runs its own update just calls `BeginFrame` and reads `FixedStepsThisFrame()`, `Alpha()` and `Time::DeltaTime()`. Callbacks are plain `std::function`; do not replace a callback from inside itself. The frame always runs to the end, then `BeginFrame` returns `false` if a quit was requested (`loop.QuitRequested()`, `loop.ExitCode()`), or if you called `loop.Stop()`.

Options (`GameLoop::Options`): `maxDelta`, `hitchReplacement` (a clamped frame steps this long instead of `maxDelta`; `Application` uses its target frame time), `hitchThreshold`, `fixedStep` (0 = `Time::FixedDeltaTime()`), `maxFixedSteps`, `smoothDelta` + `refreshHz`, `firstFrameDelta` (the first measurement usually holds loading time: give it a value to ignore what the host passes), `logHitches`, `tickClocks`.

The fixed steps run on **scaled** time, so slow motion slows them, a hit stop stops them, and a pause stops them. For an unscaled fixed loop use a separate `FixedTimestep` fed with `Time::UnscaledDeltaTime()`.

### Smooth drawing

Fixed steps and frames do not line up. After the steps, `Alpha()` (always in [0, 1)) says how far the leftover time is into the next step. Draw the blend of the previous and current simulated state:

```cpp
draw = Lerp(previousState, currentState, loop.Alpha());
```

### Inside a fixed step

`Time::InFixedStep()` is true while an `OnFixedUpdate` callback runs (also reset if it throws), and `Time::FixedTime()` is the simulated clock of the fixed steps: the start of the step being run, or the total so far outside one. `Time::FixedDeltaTime()` was already there. Only a host using `GameLoop` advances `FixedTime()`. Scene physics keeps its own accumulator (`Scene::fixedTimeStep`), untouched.

### A new Play session

`loop.BeginSession()` is the clean slate for the editor's Play button or a scene load: `Game::BeginSession()` (below), every clock back to zero, the fixed clock at 0, the loop's counters cleared. Your callbacks stay.

### How the player and the editor can adopt it

Neither `player/main.cpp` nor the editor were changed (other people are working in them). Each already does the same three things by hand: `if (dt > 0.1f) dt = 0.1f`, `Time::SmoothDelta`, `Time::Step`, then `scene.Update(Time::DeltaTime())` and a `Game::QuitRequested()` check. To adopt `GameLoop`:

```cpp
GameLoop::Options o;
o.maxDelta = 0.1f;                  // what they clamp to today
o.smoothDelta = cfg.smoothDt;  o.refreshHz = cfg.vsync ? s_refreshHz : 0.0f;
o.logHitches = true;
GameLoop loop(o);
loop.OnUpdate = [&](float dt) { scene.Update(dt); };     // the editor: TickGame(ed, dt)
loop.BeginSession();                                     // where they call Game::Reset() today

// each frame, instead of the clamp + SmoothDelta + Time::Step + scene.Update + quit check:
if (!loop.BeginFrame(tick ? realDt : 0.0)) running = false;
```

For "pause when the window loses focus" call `Game::SetPauseOnFocusLoss(!cfg.runInBackground)` once and `Game::NotifyFocus(windowFocused)` from the focus events, instead of the `tick` flag: the frame still runs with a 0 s step, so UI keeps ticking.

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

### Smaller fixes

- `SmoothedFPS()` read 100000 for about a hundred frames when the first frame was a few microseconds (the console loop's first frame always is). The first measured frame is now skipped and the average warms up quickly; steady-state numbers are unchanged.
- `Stopwatch::GameTime()` read the float `ElapsedTime()`, which cannot show half a second after a few weeks of play time; it uses `ElapsedTimeD()`.
- `Time::ResetForTesting()` also clears effects and the fixed clock.

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

Checked and found fine: the first frame in `Application` (the previous-time stamp is taken after `Start`), `Random` with seed 0 and the all-zero state, `Range` with equal or reversed bounds and `INT_MIN..INT_MAX`, Gaussian at `log(0)` (clamped), weighted picks with zero, negative, NaN and empty lists, Shuffle bias (fair over 60000 shuffles), the maximal-subpart UTF-8 decoding, `Game::Reset` clearing the quit flag, `UnscaledDeltaTime` while paused.

Seen, not changed: `Game::Reset()` keeps the time scale (an existing test pins that; use `BeginSession`), `Random::Shared()` is not thread safe (documented, `ThreadLocal()` added), `Application` pacing granularity (see above), and `ElapsedTime()` is a float by design (`ElapsedTimeD()` is the exact one).

## Not covered here

- `player/main.cpp`, the editor's Play loop and `launcher` were left alone; the adoption sketch above is what to change in them.
- `Okay.hpp` was not touched: include `okay/Core/GameLoop.hpp`, `Clock.hpp` and `RandomExtras.hpp` directly.
- Frame pacing (`FrameLimiter`), jobs and memory live in the pending `Core/FramePacing`, `Core/Jobs` and `Core/Memory` change.
