---
id: nav-crowd-hierarchy
---
## Many NPCs without a NavMesh, and a long Hierarchy, stay smooth (Findings 107 and 99)

Before: in a scene with no baked NavMesh, every NPC's path search tested every collider in the level thousands of times, and
all NPCs searched on the same frame, so a crowd in a rocky level froze the game for seconds every half second. And the
Hierarchy drew every row of a big scene every frame. Now a path search only tests the colliders near each probe (same paths),
at most 4 timer replans run per frame, and the Hierarchy draws only the rows you can see.

1. Windows editor: **File > Open**, pick `examples\navigation\nav_crowd.okayscene` from the install folder. It has a floor,
   600 rocks in 12 groups ("Rocks 1" to "Rocks 12"), a yellow ball named **Player** that wanders, and 40 blue **Followers**
   (in the "Crowd" group) that follow it with pathfinding on. There is no NavMesh in the scene on purpose.
2. Open **View > Panels > Stats** (and **View > Panels > Profiler** if you like), then press **Play**.
3. You should see the followers stream after the ball and walk around the rocks, with no pauses. The frame time should stay
   steady instead of spiking every half second.
4. To see the old behaviour for comparison: close the editor, set the environment variables `OKAY_NAV_PROBE_INDEX=0`,
   `OKAY_NAV_REPLAN_BUDGET=0` and `OKAY_NAV_REPLAN_STAGGER=0` (for example in a Command Prompt: `set OKAY_NAV_PROBE_INDEX=0` and
   `set OKAY_NAV_REPLAN_BUDGET=0` and `set OKAY_NAV_REPLAN_STAGGER=0`, then start `OkayEngine.exe` from that window), and
   repeat steps 1 to 3. The game should now hitch. `OKAY_NAV_REPLAN_BUDGET=8` allows 8 timer replans per frame instead of 4.
5. Hierarchy: with the scene open (not playing), scroll the Hierarchy up and down, click rows, Shift+click a range, use the
   Up/Down arrows, open and close groups with the arrows and with right-click **Expand / Collapse All Below**, double-click a
   row to rename it, drag a rock onto another group, and type `rock` in the search box. Everything should work as before. In
   the Profiler the `UI.Hierarchy` row should stay small however far you scroll. Selecting a follower in the Scene view
   should open its group and scroll the Hierarchy to it.
6. Tests: `ctest -R "perf_nav_fallback|hierarchy_row_model"`. `test_perf_nav_fallback` prints the search times with and
   without the index and the most searches in one frame with and without the budget.

What was not checked: this was built and tested on Linux only (headless tests); the editor change was only compiled for a
syntax check, so the Hierarchy behaviour and the frame times in the real Windows editor are **unverified** until you try the
steps above.

