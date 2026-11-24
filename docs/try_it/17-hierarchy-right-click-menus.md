---
id: 17
row: | 17 | Hierarchy right-click menus: Create Child with every category, Add Component, Select Same Type, "Delete 3 Objects" | Right-click in the Hierarchy panel of `Tools\OkayEngine.exe` | New in this change; see section 17 |
---
## 17. Hierarchy right-click menus

Where: the **Hierarchy** panel (left side by default). Map and the Existing / Gap table: [editor_menus.md](../editor/editor_menus.md), section 2.

1. Right-click an object. The menu starts with Copy, Cut, Paste, Paste as Child, then Rename (F2), Duplicate, Delete, Delete Children, then
   Create Empty Child, **Create Child**, Create Parent, Parent To, Unparent, Order, then Select, Expand All Below, then Deactivate, Lock, Focus in
   Scene, Isolate, Transform, then **Add Component...**, Prefab, Save As Prefab..., Rig & IK, Copy Name or Path.
2. **Create Child > 3D Object > Cube** (the submenu used to hold only 7 shapes; it now has 2D Object, 3D Object, Player, Light, Camera,
   Audio, Effects, UI). A cube appears inside the object you clicked, at its origin, and is selected. **Ctrl+Z** removes it.
3. Ctrl+click three objects, right-click one: the menu says **Delete 3 Objects**, **Duplicate 3 Objects**, **Deactivate 3 Objects**,
   **Group 3 Objects**, and Cut is grey with the reason.
4. **Select > Same Type (Light)** (the kind is the row's icon: Camera, Light, Mesh, Sprite, ...): every object of that kind is selected.
   If there is no other one the item is grey and says so.
5. **Add Component...**: the object is selected, the Inspector comes forward and its Add Component browser opens, ready to type in.
6. Right-click the first row: **Move Up** and **Move to Top** are grey ("It is already the first of its siblings."). Right-click an object with
   no children: **Delete Children** is grey ("This object has no children.").
7. Right-click the empty space under the rows: Create Empty, the eight Create categories, Paste, Select All, Deselect, Expand All, Collapse All,
   Sort A-Z. The **[+]** button at the top has Create Empty, Create Empty Child, Group Selected and the same categories.

Screenshots of the real editor (Linux, virtual display): a picture folder (pictures are no longer stored in the repository) (`hierarchy_row_menu.png`,
`hierarchy_create_child.png`, `hierarchy_create_child_result.png`, `hierarchy_three_selected.png`, `hierarchy_same_type_reason.png`,
`hierarchy_add_component.png`, `hierarchy_empty_space.png`).

**Checks run for this change** (menus, rows 16 and 17). Run here, on Linux, and seen:

* `tests/test_menu_bar_model.cpp` (ctest `menu_bar_model`) and `tests/test_hierarchy_context_model.cpp` (`hierarchy_context_model`), built
  with `g++ -std=c++17 -Wall -Wextra` and run: no failures (3079 and 2064 checks). The two tests also read `editor/*.cpp` and fail if a
  menu id has no handler: that was shown by renaming a handler and seeing the test fail, then restoring it.
* `editor/main.cpp` and `editor/UIRuntimeTools.cpp` with the editor's flags and `-fsyntax-only -Wall -Wextra`: no error and no warning in the
  files changed.
* **The real editor**, built from this branch (`okay-editor`, Linux, engine `-O1`, `main.cpp` `-O0`, asserts on) and run under Xvfb with the
  software renderer, started with `--dev`, the mouse driven by `xdotool`: the clicks listed in [editor_menus.md](../editor/editor_menus.md) section 3b
  worked as described (Create Child and its undo, Add Component, Same Type, Cut / Paste, Delete, Create Empty Parent / Child, Isolate, Panels,
  Audio Source, the greyed items and their reasons, also while the game runs). No crash and no
  Dear ImGui assert while clicking, in 15 sessions of this kind. The one assert seen was at shutdown, in two sessions that were closed with a
  menu still open (`ImGui::DestroyPlatformWindow`, a pop-out menu's window still alive when the context is destroyed); asserts are off in a
  release build, and it was not looked into further.
* `python3 scripts/gen_feature_map.py --check`, `scripts/branch_lint.py all`, `scripts/bug_patterns.py`: clean.

**Not seen:** anything on Windows (the editor was never run there); the Windows build (MinGW) of the changed files was not compiled; the
items listed as "not clicked" in editor_menus.md section 3b (among them the Order submenu and Open Recent greyed with no history: the test
project always has its scene in the list); Help > Documentation and Report a Bug open a browser, which this machine does not
have, so the page and the clipboard line were not seen. The software renderer was used, so nothing about the GPU path was seen.
