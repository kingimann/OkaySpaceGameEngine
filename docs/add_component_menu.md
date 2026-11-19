# Add Component menu: what was wrong, what changed

Owner feedback (2026-11-19): "improve add component menu, i dont like the interface and how things are layout".
How to try it in the editor: [try_it.md](try_it.md), entry `add-component-menu`.

## 1. What was wrong (read from the old code in `editor/main.cpp`, `DrawInspector`)

I read the old code and counted its rows; I did not run the old editor headless (no screenshot tool for this popup exists).

| # | Problem in the old menu | Detail |
|---|---|---|
| 1 | Long flat lists | **UI** had 80 rows and **Gameplay** 59 in one scrolling list. 12 groups, some with one row (Lighting) or split in two (Physics 2D, Physics 3D, Camera, RPG, No-Code). |
| 2 | No description | One text line per row. The "description" was a bracket stuck on the name: `Player Input (a local player: device, control scheme, action maps)`. |
| 3 | No icon or badge | Rows were plain text; groups were told apart only by a text tint. |
| 4 | Weak title and layout | Fixed 640 px wide, height from the content, a tiny grey "Component" title, row height 24, a 400 px body. It did not follow the editor font size. |
| 5 | Search was a plain substring | No ranking (results came in menu order), no letters highlighted, no other words (typing `sound` did not find Audio Source, `hitbox` did not find colliders). Enter added the first row. |
| 6 | Dead end on no results | Only "No component matches that." No suggestion. |
| 7 | Recent only lived in memory | Lost when the editor closed. No Favorites. |
| 8 | Silent hiding | A component already on the object, or one that needs another (Tilemap Collider 2D, UI Inventory Bind) just vanished. Nothing said why. |
| 9 | Almost no keyboard | Type, then Enter. No Up / Down, no way to leave the search box. |
| 10 | No suggestions | A Rigidbody did not suggest a Collider; a Camera did not suggest a camera rig. |
| 11 | Silent extras | Adding LOD Group quietly added a Mesh Renderer too. Nothing told you. |
| 12 | 2000 lines inside `DrawInspector` | The drawing, search, recents and every row were in one block in `main.cpp`, so nothing could be tested. |

## 2. Existing / Gap

| Wish | Existing before | Gap | Now |
|---|---|---|---|
| Wide, well spaced popup, big search with focus | 640 px popup, small search with focus | Not scaled, cramped | About 520 x 420, scales with the font, big search box, focus on open |
| Plain categories with counts | 12 groups with counts | Too many, uneven | 13 plain groups (Rendering, Effects, Physics, Cameras, Animation, Audio, Scripts, Controllers, Gameplay, Survival, Multiplayer, No-Code, UI) plus For this object, Recent, Favorites; counts count what can still be added |
| Cards with description and badge | Text rows | No description, no badge | Name, one plain line, letter badge in the group colour, "+" and star |
| Keyboard | Type + Enter | No moving | Up / Down / PageUp / PageDown, Enter adds, Tab switches column, Esc closes |
| Smart search | Substring | No ranking, highlight, aliases | Name start, word start, substring, letters in order, description words, other words; highlight; group shown beside results |
| No results help | Text only | No suggestions | "Did you mean..." buttons (edit distance) |
| Recent, Favorites | Recent in memory | Not saved, no favorites | Both saved in `okay_addcomponent.txt` |
| Suggestions | None | Everything | "For this object" from what is on the object (table in `AddComponentModel.cpp`) |
| Greyed with reason | Hidden | No reason | Greyed card with the reason; click or Enter explains in the footer |
| Add-and-fix | Engine rules and a few rows add helpers silently | No message | "Added X. It needs A, so that was added too." in the Console and under the button; the list is found by comparing the object's components before and after, so it is true for every row |
| Same model for other menus | Hierarchy "Add Component..." already opens this popup | None | Still one popup; the GameObject > Create menu makes new objects (a different job) and shares no list |
| OkayUI version | The Inspector has no OkayUI path at all (`OKAY_UI_BACKEND` only covers the launcher and a few panels) | The model is ready to reuse, nothing to attach it to | Not done (see below) |

## 3. How it is built

- `editor/AddComponentModel.hpp/.cpp`: no ImGui, no scene. Entries, plain categories, descriptions, aliases, search ranking,
  Did-you-mean, Recent / Favorites and their file, suggestions, greyed reasons, the key state machine, the "Added" text.
- `editor/AddComponentMenu.hpp/.cpp`: the popup (`Session`). The Inspector calls `Begin`, then every row calls `Item` (it only
  records the row), then `End` draws. A pick is remembered and `Item` returns true for it on the **next** frame, so the old add
  code in `main.cpp` runs unchanged. Helpers in other files (`SplineAddComponentItems`, ...) are untouched.
- `editor/main.cpp`: the old popup code (about 120 lines) became about 15 lines; the component rows stay where they were.
- Tests: `tests/test_add_component_model.cpp` (`add_component_model` in ctest).

Written from the owner's request and from general ideas (prefix and substring matching, edit distance). No code from another project.

## 4. Not done

- OkayUI version: there is no OkayUI Inspector to put it in. The model has no ImGui in it, so it can be reused when one exists.
- Descriptions are written by hand for about 90 common components; the rest use the bracket text of their row, or a generic line.
- The suggestion and "needs" tables are short lists in `AddComponentModel.cpp`, not read from the engine (the engine has
  `RequireComponent`, but nothing in the editor registers any rule, so there was no data to read).
- Keyboard: Left / Right are left to the text box; Home / End are in the model but not wired to the window.
