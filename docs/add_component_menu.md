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

## 3b. Round 2 (2026-11-20): organisation, and new components without edits

Owner again: "can you organize the add component menu better". Re-audited as a new user on the first design:

| Wish | Existing (round 1) | Gap found | Now |
|---|---|---|---|
| Categories the way people think | 13 groups, Gameplay and UI still had 60 to 80 rows each; physics toys, new colliders and the wind and camera parts had no row at all | Flat, uneven, parts missing | 10 categories (Physics, Rendering, Audio, UI, Gameplay, Scripting, Animation, Networking, World, Misc) with sub-groups, none over 12 cards: Physics (Bodies, Colliders 3D, Colliders 2D, Joints, Forces & Toys, Vehicles, Water), Rendering (Meshes & Shapes, Lights, Camera, Particles, Sky & Weather, Post & Volumes), UI (Setup, Basics, Controls, Text Input, Layout, Menus & Dialogs, Navigation & Data, Binding, HUD, Game Screens, Touch, More), Gameplay (Player Controllers, Characters & Input, Health & Combat, Survival Stats, Survival Rules, Inventory & Items, Crafting & Holding, AI & Navigation, Mechanics, Story & Turns, Saving & Menus), Animation (Animators, IK), Networking, World (Terrain, Building, Zones & Volumes, Paths & Splines) |
| Every component has exactly one place | The menu code chose the group per row | A new component needed a per-type edit | `editor/AddComponentTaxonomy.cpp`: rules on the component key (exact, starts with, ends with: any `...Collider3D` is a 3D collider). Unknown types go to a visible "Uncategorised" group, and `add_component_model` fails and names them |
| Registered components appear without an edit | Only hand-written rows | Registry types such as Conveyor Belt, Force Field, Breakable, Explosion had no row | `AddComponentRegistry.*`: after the hand-written rows every `PropertyRegistry` type with no row yet is added automatically (the same list feeds the OkayUI popup) |
| Recently used, Favourites, Suggested at the top | Recent and Favorites, For this object | Names only | "Suggested for this object", "Recently used", "Favourites" come first in the left column; kept by stable component key in `okay_addcomponent.txt` |
| Search by plain words | Aliases ranked under descriptions | "hit points", "bounce", "jump" found nothing useful | Plain words rank above description sentences; added words for health (hit points, hp, lives), bounce (physics material, Physics Surface, Rigidbody), jump (controllers, Jump Pad) and about 40 more |
| Suggestions | Short table by label | Not keyed on the component | By key: a Mesh Renderer suggests a Collider and a Rigidbody; a Collider suggests a Rigidbody; a Camera suggests a rig; a Light suggests nothing |
| Greyed with a reason | Already on this object, Needs a Tilemap | No "needs a Rigidbody" | "Needs a Rigidbody3D first." for Buoyancy, Wind Drag and the Joints (table `kPrereqs`); "Already on this object" for the rest |
| Hover preview | None | | Hover a card for about half a second: what it is, what else it adds (the engine's `RequireComponent` rules and the row's own helpers) and the first settings you can change |
| Fit | Scaled with the font | Fixed 140 px left column | Left column and window are font-scaled and clamp to 94% x 92% of the screen, so 1024x640 is not clipped (computed, not seen) |
| OkayUI popup | A plain category menu from the registry | Different list, no search | `OkayUIHostObjects.cpp` now draws the same model: search, groups, cards with a line of text, keys, greyed reasons, Recently used |

Plain categories are only a table: to add a component, add its key to a rule in `kRules` (or nothing if a pattern already covers it).

## 4. Not done

- OkayUI popup is plainer than the Dear ImGui one (no badges, stars or hover preview); it was only compiled, not run.
- Descriptions are written by hand for about 90 common components; the rest use the bracket text of their row, or a generic line.
- The suggestion and "needs" tables are short lists in `AddComponentModel.cpp`, not read from the engine (the engine has
  `RequireComponent`, but nothing in the editor registers any rule, so there was no data to read).
- Keyboard: Left / Right are left to the text box; Home / End are in the model but not wired to the window.
