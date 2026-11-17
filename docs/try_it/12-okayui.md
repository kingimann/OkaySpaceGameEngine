---
id: 12
group: switches
row: | 12 | OkayUI instead of Dear ImGui (off by default) | `OKAY_UI_BACKEND=okayui` | Unverified, only a few screens |
---
### 12. OkayUI

`OKAY_UI_BACKEND=okayui` (or `OKAY_OWN_STACK=1`) starts the launcher and editor on OkayUI where it is compiled in. Only
the screens that have an OkayUI port change; everything else stays Dear ImGui. The About popup shows `ui=okayui` when it
took effect. Whether the Windows release contains OkayUI is unverified: if not, the line stays `ui=imgui`. Nothing was
run in a real window.

To turn it on for the editor: open the Command Prompt in the folder with `Tools\OkayEngine.exe`, type `set OKAY_DEV=1`, then
`set OKAY_UI_BACKEND=okayui`, then `Tools\OkayEngine.exe`. To compare, start it again with `set OKAY_UI_BACKEND=imgui`.

The editor screens with an OkayUI port (`docs/own_ui_toolkit.md` sections 6, 9, 10 and 11):

| Where | How to open it | What to try |
|---|---|---|
| Console, Crash Log, Audio Mixer | View > Panels | as before |
| Problems | View > Panels > Problems | type in the search box (a magnifier at the left; the cross at the right empties it), right-click a row: the menu looks like Dear ImGui's |
| Stats, History | View > Panels > Stats, History | click a culling or batching switch (it changes the real setting); right-click the census or a history row; the current history row has a flat blue tint |
| Profiler | View > Panels > Profiler | tick Pause, drag the "frame" slider (it shows "frame N"), Copy CSV; the hot section is red |
| Project Settings > Tags and Layers | Edit > Project Settings..., open "Tags and Layers" | add a tag, Rename (a small window under the pointer), Remove; type a layer name and click elsewhere or press Enter: it is saved once; Reset |
| Project Settings > Physics | Edit > Project Settings... | drag the gravity numbers and "Steps per second" (it shows whole numbers), switch Quality between Classic and Improved, drag Solver iterations, Apply to current scene |
| Project Settings > Player, Rendering | Edit > Project Settings... (both open at the top) | type a Company and Version, click the - and + next to Width and Height, drag Ambient, untick Skybox, Apply to current scene; Save, close and reopen: the values stay |
| Lighting > Post-processing | View > Panels > Lighting (a tab next to the Inspector) | choose a Look Preset, Reset, tick Bloom and Colour Grading (more rows appear), drag Exposure and the three Lift numbers; the Scene view changes as you drag |
| Import settings | click a picture, a model or a sound in the Project panel: its settings show in the Inspector | change "Largest side", a model's Normals and Scale, or "Mix down to one channel": Apply and Revert become clickable; Apply saves next to the asset. The Project panel's own "Import Settings" popup stays Dear ImGui |

| Script editor (code widget) | open any script | click in the text and press Ctrl+/ (comment the line), Alt+Up / Alt+Down (move it), Ctrl+Shift+D (duplicate it) |

Tab reordering, the table column hide menu and the dock layout builder are library pieces with headless tests only; no editor
screen uses them yet, so there is nothing to click.

You should see the same panels as with Dear ImGui, with small differences listed in `docs/own_ui_toolkit.md` 11.7 and 12.2
(mostly text edges). Rows are as tall and as far apart as Dear ImGui's in the same window (also in the Inspector, which uses its own
spacing), and History and the Profiler open at the size Dear ImGui opens them at. To compare: open Edit > Project Settings with
`OKAY_UI_BACKEND=okayui` and again with `imgui`; the section headers (Player, Physics, Rendering, Tags and Layers) should be at the same
heights. Check boxes (a frame with a tick), the combo arrows and the scroll bars now look like Dear ImGui's. If a panel looks broken, set `OKAY_UI_BACKEND=imgui` (or untick the
setting) to go back. Verified in a real editor window on Linux (Xvfb, `docs/own_ui_toolkit.md` section 12: every ported panel under 8%
of pixels clearly different from Dear ImGui's). Unverified: Windows.
