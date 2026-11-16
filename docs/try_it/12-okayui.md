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

The editor screens with an OkayUI port (`docs/own_ui_toolkit.md` sections 6, 9 and 10):

| Where | How to open it | What to try |
|---|---|---|
| Console, Crash Log, Audio Mixer | View > Panels | as before |
| Problems | View > Panels > Problems | type in the search box (a magnifier at the left; the cross at the right empties it), right-click a row: the menu looks like Dear ImGui's |
| Stats, History | View > Panels > Stats, History | click a culling or batching switch (it changes the real setting); right-click the census or a history row; the current history row has a flat blue tint |
| Profiler | View > Panels > Profiler | tick Pause, drag the "frame" slider (it shows "frame N"), Copy CSV; the hot section is red |
| Project Settings > Tags and Layers | Edit > Project Settings..., open "Tags and Layers" | add a tag, Rename (a small window under the pointer), Remove; type a layer name and click elsewhere or press Enter: it is saved once; Reset |
| Project Settings > Physics | Edit > Project Settings... | drag the gravity numbers and "Steps per second" (it shows whole numbers), switch Quality between Classic and Improved, drag Solver iterations, Apply to current scene |

| Script editor (code widget) | open any script | click in the text and press Ctrl+/ (comment the line), Alt+Up / Alt+Down (move it), Ctrl+Shift+D (duplicate it) |

Tab reordering, the table column hide menu and the dock layout builder are library pieces with headless tests only; no editor
screen uses them yet, so there is nothing to click.

You should see the same panels as with Dear ImGui, with small differences listed in `docs/own_ui_toolkit.md` 10.6
(text edges, check marks, the Quality box's arrow). If a panel looks broken, set `OKAY_UI_BACKEND=imgui` (or untick the
setting) to go back. Unverified: none of this was opened in a real editor window.
