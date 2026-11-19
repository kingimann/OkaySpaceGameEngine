# Time in your game: a plain guide

Everything about time in one place, in simple words. If you want the measurements and the list of what
changed, read [time_upgrade.md](time_upgrade.md). If you want to try each thing, read the entry `time1` in
[try_it.md](try_it.md) and open the example scene `examples/time/time_playground.okayscene`.

## Three kinds of time

| Name | What it is | Use it for |
|---|---|---|
| **Game time** | Real time, slowed by slow motion, stopped by pause and hit stops | things in the world: movement, physics, enemies, `every()` and `after()` timers |
| **Real time** | The clock on the wall. Nothing slows it | menus, a pause screen that still animates, a stopwatch for the player |
| **Clocks** | Your own named timers that run at their own speed (see below) | "slow only the enemies", "keep the music going while the game is paused" |

In a script: `dt` (or `Time.deltaTime`) is game time for this frame, `unscaled_dt()` (or `Time.unscaledDeltaTime`) is real time.

## Slow motion, hit stop, speed

| You want | In a script | What happens |
|---|---|---|
| Bullet time | `slow_motion(0.2, 3)` | the game runs at a fifth speed for 3 real seconds, then goes back by itself |
| Bullet time that eases in and out | `slow_motion(0.2, 3, 0.5)` | it slides down to a fifth over half a second, holds, then slides back up |
| A freeze frame on a punch | `hit_stop(0.15)` | the game stops for 0.15 seconds |
| A slow-down on impact | `hit_stop(0.2, 0.3)` | 0.2 seconds at 30 percent speed |
| Fast forward | `ramp_timescale(3, 1)` | the speed slides to 3 times over 1 second |
| Normal again | `ramp_timescale(1, 0.5)` | |
| Pause the game | `push_pause("menu")` ... `pop_pause("menu")` | the game stops; real time and the UI clock keep going |

Several effects at once do not fight: the strongest slow-down wins and each ends on its own. In Visual Script use the
nodes **Slow Motion**, **Hit Stop**, **Ramp Time Scale**, **Pause** (the eased slow motion is script-only for now).

## Timers

| You want | Script | Visual Script |
|---|---|---|
| Run something once later | `after(2, "boom")` or `Invoke("Boom", 2)` | **Delay** / **Wait** |
| Run something again and again | `every(0.5, "tick")` or `InvokeRepeating("Tick", 1, 0.5)` | **On Timer** |
| Stop them | `cancel_timers()` or `CancelInvoke("Tick")` | |

All of these run on **game time**, so they stop in a pause and slow in slow motion. They keep their rhythm: a timer
that should run every 0.1 seconds runs 10 times a second at 30, 60 or 144 frames per second. If the computer stalls
for 5 seconds the timer runs the missed turns at once (up to a limit) and carries on. A timer that is cancelled by
another timer in the same frame does not run. A timer you start inside a timer starts counting in the next frame.

Behind the scenes there is one timer service for the whole engine (`okay::Scheduler`, `docs/timers_and_tweens.md`);
script timers use it. C++ and plugin code can also use `NextFrame`, `At(time)`, `EndOfFrame`, jitter (each run waits a
little random extra, so 100 torches do not flicker in step) and **timers that are saved** with the game.

## Clocks

A clock is a timer you name. The game starts with three: `game` (stops when paused), `ui` (never stops) and
`cinematic` (stops when paused, not slowed). You can add more in **Edit > Project Settings > Time > Clocks**, or in a script.
A clock can sit **under** another: it follows it, so when the parent slows, the child slows too.

Example: `enemies` under `game` at speed 0.5 means enemies always move at half speed; slow motion slows them
further; a pause stops them; the `ui` clock does not care.

## Time of day and seasons

Add the **Day Night Cycle** component (Add Component > Environment). It is the game's clock and calendar:

- **Day Length** is how many seconds a whole day takes. **Speed** multiplies it (0 stops the day).
- **Day**, **Days Per Season**: the day counter and the four seasons (Spring, Summer, Autumn, Winter).
- **Clock Name**: leave empty to follow game time. Type `ui` to keep the day moving while the game is paused.
- It drives the sun light and the sky colours. The saved values `hour`, `minute`, `day`, `season`, `seasonName`
  and `clock` ("08:30") can be shown on screen: put `{clock}` in a UI text.
- It sends events to every object: **On Hour**, **On Day**, **On Sunrise**, **On Sunset**, **On Season** (in Visual
  Script: **On Custom Event** with that name; in a script: a function called `OnEvent_OnHour(hour)`, `OnEvent_OnDay(day)`,
  `OnEvent_OnSunrise(v)`, `OnEvent_OnSunset(v)`, `OnEvent_OnSeason(season)`). A frame that passes several hours sends
  all of their events in order. Setting the time by hand sends nothing.
- Scripts: `day_number()`, `season()`, `clock_text()`, `time_of_day(6)` (jump), `skip_to_hour(6)` (sleep until 6:00 and
  send the events on the way).

## Fixed steps and smooth drawing

Physics and `FixedUpdate` run in equal steps (50 a second unless you change **Edit > Project Settings > Physics >
Steps per second**), however fast the screen draws. So the same drop lands in the same place at 30 and at 240
frames per second. The leftover part of a step is `Time.FixedAlpha` (0 to 1): draw things that many percent of
the way between their last two physics positions and movement looks smooth. Rigidbodies do this by themselves.

If the computer stalls, physics may catch up some of the missed time (default one third of a second, at most 16 steps
in one frame). What is left is **forgotten** (the game falls a little behind, which is safe) or, if you choose
**Catch up over the next frames**, paid back a few steps per frame. Both are in **Project Settings > Time**.

## Project Settings > Time

| Setting | What it does |
|---|---|
| Game speed at start | the speed the game begins with (1 normal) |
| Longest frame | a frame longer than this counts as this long, so a stall never makes things jump (0.1 s) |
| Catch up after a stall | how much missed physics time may run in one frame (0.33 s) |
| Time lost in a stall | forget it, or catch up over the next frames |
| Frame rate when the window is not active | saves battery when you are in another window (0 = no extra limit) |
| Clocks | your own clocks, and which clock each one follows |

The frame rate limit itself is in **Build Settings** (Frame rate cap). It now holds the rate exactly (it used to
overshoot by up to 4 percent); with vsync on, a cap at or above the screen's refresh rate is left to vsync.

## Watching time while you play

In the editor, press **Stats** in the Game view while playing. Under the frame numbers you see the fixed step
(rate, steps this frame, total, alpha), one line of clocks (name, speed, what they follow, paused), and the next timer.
In a built game press **F3**. The **Pause** and **Step** buttons stop the game and advance one frame at a time:
clocks, timers and physics all advance by one frame (not the unscaled UI clock).

## Which time for which system

| System | Time it uses | Why |
|---|---|---|
| Physics, FixedUpdate, movement, AI, `every()` / `after()` | game time | slow motion and pause must affect them |
| Animation, particles | game time | they should freeze with the world |
| Cutscenes (Sequence Player) | the **cinematic** clock | stops in a pause, ignores slow motion if you want |
| Audio | real time unless you slow it on purpose | music should not drop in pitch in a pause |
| UI, menus, tooltips | real time (`ui` clock) | a pause menu must still animate |
| Networking | real time | the server does not wait for your slow motion |
