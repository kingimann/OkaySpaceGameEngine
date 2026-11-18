---
id: 7pl
row: | 7pl | Project panel: One column / Two columns | Editor: **View > Panels > Project**, buttons at the right end of the Assets / Favourites / Recent row | New in this change; unverified in the real window (only syntax-checked) |
---
## 7pl. Project panel: One column / Two columns

**What it is.** The Project panel can be shown two ways. Pick with the two buttons at the right end of the Assets / Favourites / Recent row.
Two columns is the old look and the default: folders on the left, what is inside the chosen folder on the right.
One column is a single list: click the small arrow in front of a folder and its contents open right beneath it.
The choice is remembered when you close the editor (it is saved in `okay_settings.txt` as `projectcolumns`; an old file with no such line opens as two columns).
Code: `editor/ProjectLayout.hpp` (the decisions), `DrawProject` in `editor/main.cpp`. Written from the panel's own behaviour; no outside code.

**Steps.**
1. Open the editor, open a project, make sure **View > Panels > Project** is on.
2. Click **One column**. The folder pane on the left goes away and everything is one list.
3. Click the arrow in front of a folder. Its files and folders appear under it, moved to the right. Click the arrow again to close it.
4. Click a row, then press Down / Up to move, Right to open a folder (press Right again to step into it), Left to close it (Left again goes to the folder that holds it), Enter to open the item, Delete to delete (as before).
5. Click the Favourites and Recent tabs: they show their lists in both layouts. Click Assets to come back.
6. Click **Two columns**. Drag the line between the two sides: it stops before the left side is too thin or the right side too small. Double-click the line to reset it.
7. Close and open the editor: the layout you picked is still on.

**What you should see.** Big readable labels on the two buttons; the picked one is highlighted. Nothing is drawn as a picture of text.

**Verified and not.** The decisions are tested headless (`test_project_layout`, run by `ctest -R project_layout`): the divider clamp and widths,
which rows an open-in-place list shows, what every key does, and that old settings files load as two columns. `DrawProject` was only
checked for syntax. Nothing was run in a real editor or on Windows, so every "you should see" line above is unverified. Not in One column: the Smart Filters list (use the type filter button).
