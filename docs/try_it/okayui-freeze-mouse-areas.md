---
id: okayui-freeze
row: | okayui-freeze | OkayUI Inspector: every component answers clicks again; the slow-frame log | Editor with Settings > Experimental "OkayUI screens" + "OkayUI Inspector" on; `okay_editor_perf.log` beside `OkayEngine.exe` | Fixed and verified on Linux; Windows unverified |
---
## OkayUI Inspector: lower components ignored clicks; the slow-frame log (bug fix, 2026-11-23 report)

The report: "with experimental on, the editor freezes a lot, it's non-responsive".

**What was wrong.** With the OkayUI Inspector on, each part of the Inspector (the object card, the Transform, and for every
component its header bar and its body) tells the editor "the mouse is mine here". The editor kept only **8** of those areas per
frame. An object with many components shows more than 8 parts (the First Person template's Player shows 12 when its components
are folded), and every part after the 8th never got the mouse: clicks, folds and typing there did nothing, so the Inspector looked
frozen. Now the editor keeps up to 96 areas and counts any it had to drop.

**Try it (Windows release, after this change ships):**

1. Turn on **Settings > Experimental > OkayUI screens** and **OkayUI Inspector** (in the launcher), start the editor.
2. **File > New Project...**, template **First Person**. Select **Player** in the Hierarchy.
3. On the **Character** bar click **...** and pick **Collapse All Components**. You see about 11 folded bars
   (Character, Mesh Renderer, Animator, Rigidbody3D, Box Collider 3D, Player Controller, First Person Controller,
   Third Person Controller, Player View ...).
4. Click the fold arrow of the **lowest** bars (Third Person Controller, Player View). **You should see** each one open.
   Before this fix nothing happened when you clicked them.

**The slow-frame log (to find any freeze that is left).** Every editor frame that takes 250 ms or more now writes one line to
**`okay_editor_perf.log` in the same folder as `OkayEngine.exe`** (the `Tools` folder of your install, where `okay_update.log` is).
The line says how long the frame took, which part of the editor took the time (for example "Panels" with the scope
`UI.Inspector`, "Present", "ImGui.OkayUI" for OkayUI's own drawing, "Editor.Events" for reading input), and which experimental
switches were on (OkayUI screens, OkayUI Inspector, own renderer Direct3D 11 / OpenGL, own WAV). The first five slow frames after
start-up also show in the Console as `[perf] slow frame ...` warnings. The first frames after start always load the scene and are
slow; that is normal. **If the editor still freezes, send that file.** Set the environment variable `OKAY_SLOW_FRAME_MS` to change
the limit (for example `500`), or to `0` to turn the log off. At most 300 lines are written per run.

**Verified here (Linux, Xvfb, real mouse clicks with xdotool):** with the old limit the editor reported "8 kept, 4 without the
mouse" for the folded Player and clicks on the lowest bars did nothing; with the fix "12 kept, 0 without the mouse" and the
Third Person Controller bar opened on click. Frame times while selecting objects, folding, scrolling and typing stayed flat
(the OkayUI part about 2.5 ms a frame, the Inspector 0.3 to 0.6 ms); no frame over 150 ms came from the UI.

**Unverified:** everything on Windows, including the Direct3D 11 path of "own renderer" (it cannot run on Linux; it was read
and compile-checked with MinGW only). If a freeze remains on Windows, the log line names the part.

Tests: `ctest -R "okayui_frame_budget|okayui_host$|editor_perf"`.
