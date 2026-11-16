---
id: 7r
row: | 7r | The character walks when you walk and runs when you hold Shift | New Project > Third Person (or First Person, press V), Play, W then Shift+W; older project: Player Controller > Use natural walk and run speeds | New in this change; the stride is measured by a test and drawn in pictures, the feel in the real editor and on Windows is unverified |
---
## 7r. The walk looks like a walk, Shift looks like a run

Why: "the character walking is basically running". The player used to move at 4.5 m/s for plain W, which is a jog, and at 8 m/s for
Shift, which is a sprint, so both looked like running (the legs stepped about 270 times a minute for W and 250 for Shift). Now W is a
real walk (2 m/s, about 125 steps a minute, one foot always on the ground, arms hanging and swinging a little) and Shift is a real run
(5.5 m/s, about 185 steps a minute, both feet leave the ground, elbows bent, chest leaning forward). The full numbers, the targets and
the pictures are in [walk_vs_run.md](walk_vs_run.md).

**A new project.**
1. Open the launcher (`OkaySpace.exe`) > **Create** > **New Project**. On the first step pick **Third Person** (under **All templates** if the starter
   cards do not show it), press **Next**, name the project, press **Next**, then **Create project**. (**First Person** works too: the body is not drawn from
   the eyes, so press **V** in Play to switch to the third-person view and watch it.)
2. In the editor press **Play** (the triangle at the top). Click the Game view once so it has the keyboard.
3. Hold **W**. You should see the character walk: an easy stride, the arms hanging with a small swing, the body upright, never both feet
   off the ground. The camera moves slowly: 2 m/s.
4. Now hold **Shift + W**. You should see it run: a faster step, the elbows bent, the chest leaning forward, and both feet leaving the ground
   for a moment each step; the camera moves much faster. Let go of Shift: it goes back to the walk with no flicker.
5. Select the Player in the Hierarchy and look at **Player Controller** (Third Person settings): **Walk Speed 2**, **Run Speed 5.5**.

**An older project** (made before this change). It keeps the speeds it saved, so it will look the same until you opt in:
1. Open the project (the editor asks about upgrading it first: answer as you like; this change does not touch the speeds).
2. Select the Player. In the Inspector, in the **Player Controller** panel (or the First-person / Third-person settings of a bare controller,
   or the Third Person Shooter Controller), press **Use natural walk and run speeds**. Hover it for what it sets.
   Walk Speed becomes 2 and Run Speed 5.5 (5 for the shooter); Crouch and Prone Speed become 1 and 0.5 only if they were still 2.2 and 1.1.
3. Press **Ctrl+Z**: the old numbers come back. Press Play to compare.
4. If you set Walk Speed above 3, an amber line under it says the legs have to jog (that is real: a walk does not fit that speed).

**Bring the old look back to compare** (same build, no reinstall). Close the editor, then in a Command Prompt:
`set OKAY_OLD_GAIT=1` and `Tools\OkayEngine.exe` (in PowerShell: `$env:OKAY_OLD_GAIT = "1"` then `.\Tools\OkayEngine.exe`).
Make a new Third Person project and Play: W is the hurried 4.5 m/s jog again. **How to tell the old one is on:** a new Player Controller
shows Walk Speed 4.5 and Run Speed 8 (the natural one shows 2 and 5.5), and the Inspector shows an amber "The OLD walk and run is on"
line under the button. Close the editor and start it again without the variable to get back to the natural one.

**Status.** Verified here (Linux, headless): `walk_vs_run` (the walk and the run on every measure, in a new project's Animator setup and with the
Character on its own, plus no flicker, no foot skating and the button's rules), `walk_vs_run_old` (the same program with the old gait on, showing the old
walk was not clearly different), and the existing locomotion, jitter, mixamo, proxy, NPC and controller tests. The pictures are software renders from
Linux, not screenshots of Windows. **Unverified:** the feel in the real editor and in the Windows player, the Inspector button and hint drawing
(compile-checked only), a real Mixamo model (the checks use the test rigs), and whether 2 m/s feels right for your game. The default speeds are one number in
`NaturalGait.hpp` and two fields in the Inspector if you want them faster (above about 2.4 m/s the legs start to jog).
