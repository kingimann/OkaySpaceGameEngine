---
id: 2
row: | 2 | First person head bob follows Reduce Motion | New Project > First Person, plus the Settings screen | Shipped (2.873.0 and earlier) |
---
## 2. First person head bob and Reduce Motion

**What it is.** The first-person camera bobs and sways while you walk and dips on a hard landing. With Reduce Motion
on, the bob and sway ease out and the landing dip is gone. Footstep events still fire.

**Where the setting is.** It is a row on the game's **Settings screen** (a UI panel with a **Reduce motion** switch,
saved as `a11y.reduceMotion` in the game's prefs). The editor itself has no accessibility window.

**Steps.**
1. New Project > **First Person**. Press Play, walk with **W A S D**, and watch the camera: it bobs up and down a little
   and sways. Press Stop.
2. **GameObject > UI > Prebuilt Screens > Settings**. A Settings panel is added to the scene (the Console says what it
   hid, if anything).
3. Press Play again. The Settings panel is on screen: click **Reduce motion** so it is on, then click **Done**.
4. Walk again. The bob and sway are gone (the camera glides). Jump off a ledge or from a jump: no dip on landing.
5. Open Settings again and turn Reduce motion off: the bob eases back in.

**How you know it worked.** The same walk, with the switch off, bobs; with it on, the view is steady.

**Status.** Tested on main: the headless test `reduce_motion_bob`. Unverified: steps 2 to 5 in the real editor (the menu
path and that Done hides the panel and gives you the mouse back for walking were read from code, not clicked).
