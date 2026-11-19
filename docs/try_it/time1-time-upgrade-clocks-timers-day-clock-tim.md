---
id: time1
row: | time1 | Time upgrade: slow motion with ease, hit stop, pause, timers, day clock and seasons, Project Settings > Time, Stats time lines | Editor: **File > Open Scene** `<install folder>\examples\time\time_playground.okayscene`, press Play. **Edit > Project Settings > Time**. Guide: `docs/time_guide.md` | New in this change. Logic tested headless on Linux; the editor and player windows were not run, nothing on Windows |
---
## time1. Time upgrade: clocks, timers, day clock, Time settings

**What it is.** One small scene that shows each time feature, a new Time page in Project Settings, and time lines in
the Stats view. The plain-words guide is `docs/time_guide.md`; the measurements are in `docs/time_upgrade.md`.

**Steps (the time playground).**
1. Open the editor (`Tools\OkayEngine.exe`). **File > Open Scene** and pick `<install folder>\examples\time\time_playground.okayscene`.
2. Press **Play**. You see a blue cube (turns on game time), an orange cube (turns on real time), a red ball that is dropped
   every 3 seconds, a sun, and text in the top left: the day and clock, a timer counter, and a message line.
3. Press **1**. The game slows to a fifth speed for 3 seconds, easing down and up: the blue cube and the ball slow down, the
   orange cube does not. The message says "slow motion".
4. Press **2**. The game freezes for 0.15 seconds (a hit stop). Press it a few times quickly.
5. Press **3**. The game pauses: the blue cube, the ball and the counter stop, the orange cube keeps turning, the top line
   says PAUSED. Press **3** again to resume.
6. Press **4**. The clock jumps to 6:00 (sleep until morning) and the message says "Sunrise!" (the day clock sent an event). Wait about
   45 seconds: at midnight the message says "A new day". A whole day is one minute long and a season is 2 days.
7. Press **5** to fast forward to 3 times speed over a second, and **6** to go back to normal. The counter "every(1) has run N
   times" counts game time; watch it count faster, slower and stop with the pause.

**Steps (Project Settings > Time).**
1. **Edit > Project Settings**, open the **Time** section.
2. Set **Game speed at start** to 0.5, press Play: everything starts at half speed. Set it back to 1.
3. Set **Longest frame** to 0.02 and press Play: when the editor hitches (drag the window) the game now never jumps ahead.
4. Press **Add audio, physics and network clocks**: three clocks appear; change the speed of one. Play, open **Stats** in
   the Game view: the line `clocks:` lists them with what they follow.
5. Under **Stats** you also see `fixed 50 Hz: ...` (steps this frame, total, alpha) and `timers: N, next in ...`.
6. **Build Settings** > **Frame rate cap** 60 and build: the built game holds 60 exactly (press **F3** in the game for the numbers).
   Set **Frame rate when the window is not active** to 30: click another window while the game runs, it drops to 30.

**What you should see.** The orange cube never slows, the blue cube and ball always do; the day clock shows `Day N HH:MM`; events
change the message line; the Time settings change how Play and the built game behave.

**Status.** Logic tested headless on Linux (`test_time_sim`, `test_time_upgrade` and the existing time tests). The editor page, the Stats lines, the
player's frame limiter and the example scene in a real window were compiled only (syntax check), never run, and nothing was run on Windows.
