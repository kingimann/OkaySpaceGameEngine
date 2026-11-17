---
id: 12
group: switches
row: | 12 | OkayUI instead of Dear ImGui (off by default) | `OKAY_UI_BACKEND=okayui` | Unverified, only a few screens |
---
### 12. OkayUI

`OKAY_UI_BACKEND=okayui` (or `OKAY_OWN_STACK=1`) starts the launcher and editor on OkayUI where it is compiled in. Only
the screens that have an OkayUI port change; everything else stays Dear ImGui. In the launcher: What's New, Tips, Downloads,
Achievements and the Notifications panel (the bell). In the editor: Console, Crash Log, Audio Mixer, Problems, Stats, History,
Tags and Layers, Physics, Profiler, Import settings, Post-processing, and the Player and Rendering settings.
The About popup shows `ui=okayui` when it took effect. Whether the Windows release contains OkayUI is unverified: if not,
the line stays `ui=imgui`.

To see the three newest launcher screens with sample data, in a Command Prompt:

```
set OKAY_UI_BACKEND=okayui
set OKAY_UI_DEMO_USER=demo
OkaySpace.exe
```

- **Downloads** (sidebar): four sample downloads (completed, downloading, queued, failed). Expect bordered rows with a yellow
  progress bar, the status in colour, and Open folder, Install to Community, Cancel or Remove; Remove takes a finished row away.
- **Achievements** (sidebar): eight sample achievements with medals. Click "This month", type in the search, change the sort,
  right-click a tile for Add to showcase, Remove from showcase or Copy id.
- **Notifications** (the bell at the bottom of the sidebar): a sample feed. Click the chips, hover a row for its dismiss button,
  click a row to open its page.

Run the same without `set OKAY_UI_BACKEND=okayui` to compare with the Dear ImGui versions: the two should look the same
(same positions, sizes, icons and tooltip place; only edge shading and single text pixels differ). The Dear ImGui Downloads rows
now show their buttons in full (they used to be cut off at the bottom). Clicking a chip or button right after moving onto it
works on the first click. Add `set OKAY_UI_TIMING=1` to see, after
300 frames on one of these screens, a toast (and a stdout line) with the median CPU time per frame of that screen on the backend
that drew it, and how many triangle indices each backend drew. Run in a real window only on Linux under Xvfb so far; not run
on Windows.

What to look for after the anti-aliasing step (docs/own_ui_toolkit.md section 8.7), same steps as above:
- **Smooth edges**: the medals, the level ring, the round dots, the chips and the rounded rows have soft one-pixel edges,
  as on the Dear ImGui pages (before, they were stair-stepped).
- **Achievements**: the search field brightens a little under the mouse and while typing. Open the sort list ("Newest first"):
  it is a rounded box right under the combo with the current choice highlighted, as the Dear ImGui one. Right-click a tile:
  the menu is a rounded box with a hairline edge; the entry under the mouse is highlighted.
- **Speed**: with `set OKAY_UI_TIMING=1`, compare the "build + render" figure of each screen with and without
  `set OKAY_UI_BACKEND=okayui`: OkayUI's should be lower (on Linux it was 12% to 18% lower).

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
