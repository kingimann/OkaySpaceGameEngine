---
id: 12b
group: switches
row: | 12b | OkayUI Crash Log (visible rows only) and Audio Mixer (meter tooltip), off by default | `OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`, editor View menu | Headless tests and images only; never run in the Windows editor |
---
### 12b. OkayUI: Crash Log and Audio Mixer panels

**What it is.** Two editor windows can be drawn by our own UI toolkit (OkayUI) instead of Dear ImGui. The Crash Log now uses the
new `ListClipper` (only the rows you can see are laid out, so a report of 100,000 lines scrolls as fast as one of 10), and the
Audio Mixer meters now show a small tooltip box (the meter name, then the level in dB on its own line) built with the new
`BeginTooltip` / `EndTooltip`. With the switch off (the default) you get exactly the old Dear ImGui windows.

**Switch (developer only, off by default).** In a Windows command prompt:

    set OKAY_DEV=1
    set OKAY_UI_BACKEND=okayui
    OkayEngine.exe

(Without `OKAY_DEV=1` the switch is ignored on purpose: owner rule, no own code is turned on for users yet.)

**How to tell it is on.** The startup stack line in the console says `ui=okayui` (`[okay] stack: ... ui=okayui`), Help > About shows
`ui=okayui`, and View > Rendering & Performance > UI backend has "OkayUI (preview)" ticked and says "OKAY_UI_BACKEND is set and decides this."

**Steps: Crash Log.**
1. Open **View > Debug & Data > Crash Log**. With no reports you see "No crash reports - nice." in green.
2. To get something to look at, copy any long text file to `okay_crashlog.txt` next to `OkayEngine.exe` (for example a few thousand lines
   of a log) and press **Refresh**. You should see the newest line at the bottom, "Newest report is at the bottom", and a scroll bar. Scroll with
   the wheel to the top: the window stays smooth however long the file is.
3. **Copy All** puts the text on the clipboard, **Open Folder** opens the folder, **Clear** asks first (Cancel keeps the file, Delete removes it).
4. Unset `OKAY_UI_BACKEND` and start again: the same window, drawn by Dear ImGui. It should behave the same (differences below).

**Steps: Audio Mixer.**
1. Open **View > World & Gameplay > Audio Mixer** and play a scene with sound (Play) so the meters move.
2. Hover the mouse over the green "Output" bars. A box appears with `Left` or `Right` and, under it, the level in dB (for example `-12.3 dB`).
3. Drag the volume sliders, click the mute boxes, tick "Duck music under voice" (three more sliders appear) and press **Reset**.

**Differences from the Dear ImGui versions (said plainly).** Report text cannot be selected with the mouse (use Copy All); a very long line is
cut at the window edge; the meter tooltip has two lines, not one (`Left` / `-12.3 dB`, was `Left: -12.3 dB`); sliders show no number text.

**What was checked.** `ctest -R "okayui_panels|okayui_scopes"` (headless): a 5,000-line report lays out fewer than 60 rows, scrolls to the oldest and
newest line, and its picture is pixel for pixel the same as before the change (the three Crash Log golden images did not change); the meter
tooltip has a golden image (`okayui/tests/golden/audiomixer_meter_tip.png`, a headless software render, not a screenshot of the editor window).
**Not verified:** the Windows editor was not built or run with these changes; a real GPU and real mouse were not used.
