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

The editor screens with an OkayUI port (`docs/own_ui_toolkit.md` sections 6, 9, 10, 11 and 12):

| Where | How to open it | What to try |
|---|---|---|
| Hierarchy | always open (left side) | click, Ctrl+click and Shift+click rows; click a row's arrow to fold it; click the eye (hide) and the padlock left of it; double-click a row (or press F2) and type a new name in the row, Enter keeps it, Escape gives up; drag a row onto the middle of another (it goes inside), onto its top or bottom edge (above or below), or onto the empty space under the list (to the top level); drag a picture or a prefab from the Project panel onto a row; right-click a row (Order and Parent To open to the side) and the empty space; type in the search box; Up, Down, Left, Right and Ctrl+A with the panel clicked. Open `examples\navigation\nav_crowd.okayscene` (650 objects) and scroll: it should stay smooth. Create sub-menus (2D Object, 3D Object, ...) still open as the old Dear ImGui list at the pointer |
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

You should see the same panels as with Dear ImGui, with small differences listed in `docs/own_ui_toolkit.md` 11.7 and 12.6
(mostly text edges; the Hierarchy renames in the row instead of a "Rename Object" box). Check boxes (a frame with a tick), the combo arrows and the scroll bars now look like Dear ImGui's. If a panel looks broken, set `OKAY_UI_BACKEND=imgui` (or untick the
setting) to go back. Unverified: none of this was opened in a real editor window.
