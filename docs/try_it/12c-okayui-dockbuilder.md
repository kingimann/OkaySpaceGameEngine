---
id: 12c
group: switches
row: | 12c | OkayUI DockBuilder, SetNextWindowDockID, Shortcut and eleven small calls (library only, nothing uses them yet) | none in the Windows release; headless tests only: `ctest -R "okayui_dockbuilder|okayui_internals"` | **Cannot be tried in the Windows release yet**; Linux headless tests only |
---
### 12c. OkayUI: DockBuilder, SetNextWindowDockID, Shortcut and small calls

**Cannot be tried in the Windows release yet.** These are calls in the OkayUI library (`okayui/`) that ported editor code will use
(the editor's layout presets, its keyboard shortcuts, its drag and drop). No editor or launcher screen calls them yet, so there is
no menu, switch or sample scene that shows them. What is missing for that: a ported editor dock space (the editor's own dock space
still runs on Dear ImGui). The first screen that will show them is the OkayUI dock space when the editor's layout presets are ported.

**What you can run (Linux or a Windows build with the OkayUI tests on):**

    ctest -R "okayui_dockbuilder|okayui_internals" --output-on-failure

**What you should see.** Both tests print `PASS`. `okayui_dockbuilder` builds the editor's "Default" layout (Hierarchy on the left,
Inspector on the right, Console / Problems / Project / ... as seven tabs along the bottom, Scene and Game as tabs in the middle) with
the same call sequence the editor's `BuildLayoutPreset` uses, draws it, and prints the measured widths and heights next to the expected
ones (for 1280 x 720: left 255.2, right 427.1, bottom 157.5 px). `okayui_internals` covers Shortcut (Ctrl+S style chords), resizing a
window by title, the drag-and-drop payload, and the state store.

**Not verified:** nothing here was run in the Windows editor or launcher, and the preset call sequence was copied by hand from the
editor source, not run through it. See `docs/own_ui_toolkit.md` section 6.9.
