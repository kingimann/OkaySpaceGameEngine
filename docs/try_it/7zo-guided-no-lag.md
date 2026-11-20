---
id: 7zo
row: | 7zo | Guided: no more lag when you change a slider, style or Detail; a live drag preview; a "Last change" time line | Modeling > **Guided**, select a guided model (File > Examples > Guided modeling > `guided_vehicles`), drag any slider, press Ultra | New in this change; timings measured headless on Linux (`test_guided_perf`), the editor panel compiles but was **never opened in a real window** (unverified) |
---

## 7zo. Guided: changing settings no longer lags

**What it is.** The Guided page used to freeze for a moment (a second or more on a car or a person at Ultra) when you moved a slider, picked a design or
changed Detail, and it cost a little on every frame just by being open. It is fixed: sliders show a cheap live preview while you drag, the real model is built
once when you let go (one undo step), and a time line under the Undo buttons tells you what the last change cost. Numbers and causes: `docs/editor/guided_perf.md`.

**Status.** The engine side is tested on Linux (`ctest -R guided_perf`, about 1,240 checks, plus the existing guided tests). The panel changes
(`editor/GuidedPanel.cpp`) compile but nobody has opened them in a real window yet (unverified). Nothing here ran on Windows.

**Try it.**

1. Open **File > Examples > Guided modeling > guided_vehicles**, click a car, open **View > Editors & Tools > Modeling** and press **Guided**.
2. Under **Detail** press **Ultra** (about 100,000 triangles). Under the Undo buttons you now see a line like
   `Last change: 300 ms (build 40, mesh 40, save 190) ... 107272 triangles`. That is the real cost of the change.
3. Open **Size** and drag a slider such as **Length**. While you hold the mouse the car follows at Medium detail (the line shows `Drag preview: 1 ms`).
   Let go: the car is rebuilt at Ultra once. **Undo guided step** goes back one step for the whole drag, not one per tick.
4. Leave the panel open and do nothing: the editor should stay smooth (before, a big model cost several milliseconds every frame).
5. Hand edits: in Edit Mesh move one wheel, go back to Guided and drag a slider. There is no preview for a model with hand edits (it cannot keep them); it is
   rebuilt when you let go and keeps your edit. The time line then says it did the full check.
6. Buses, trucks and buildings at Ultra no longer come apart (the windows used to float off the body); the tipper truck bed now sits on the chassis.

**Not done / unverified.** The panel was not run in a window, so the preview feel and the real frame times are unverified. The cape of the Hero human still hangs
a few centimetres off the back (a design choice); `guided_pictures` renders were not re-made.
