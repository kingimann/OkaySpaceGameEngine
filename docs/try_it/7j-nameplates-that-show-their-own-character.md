---
id: 7j
row: | 7j | Nameplates bound to their characters, and a rich-text sign | `examples\ui_text\ui_text.okayscene` (File > Open..., then Play) | New in this change; real window unverified |
---
## 7j. Nameplates that show their own character, and a sign with rich text

**What it is.** A small flat scene with two characters, a Goblin Scout and an Orc Chief. Above each one is a nameplate (a
dark card with the name, a gold level pill and a red health bar). Each plate is a child of its character and shows **that**
character's name, level and health, live. The Goblin loses a little health by itself soon after Play starts, so you can see
its bar drop. Behind them is a wooden sign whose text uses the rich-text tags: a big bold title, a gold line, red bold and
italic words and a small line. Code: `UIOwnerData` (the `owner.` keys), `UIGallery::AddNameplate`, `ui2d::DrawWorldText`
(`docs/ui_runtime.md`, `docs/ui_world_space.md`).

**Steps.**
1. Open the editor (`Tools\OkayEngine.exe`, or start it from the launcher). **File > Open...**, paste
   `<install folder>\examples\ui_text\ui_text.okayscene` (installer: `%LOCALAPPDATA%\OkaySpace\examples\ui_text\ui_text.okayscene`),
   press Open.
2. The Hierarchy lists `Main Camera`, `Ground`, `Sign Post`, `Sign Board`, `Sign Text`, `Goblin Scout`, `Orc Chief`,
   `Canvas` (with `Hints`), `EventSystem` and `Director`. Expand `Goblin Scout`: it has a child `Nameplate`.
3. The scene is flat (2D). In the **Scene** view header the button next to the view name says **3D** or **2D**: if it says
   3D, click it once so it says 2D. Look at the **Game** tab for the plates (see "Known rough").
4. Press **Play**. In the Game view you should see a blue sky, green ground, the brown sign at the top, a green box at the
   left (the Goblin) and a red-brown box at the right (the Orc). Above each box is a plate: the name on the left
   ("Goblin Scout", "Orc Chief"), a gold pill on the right ("Lv 3", "Lv 12"), and a red bar under them, full at the start.
5. About a second after Play the Goblin's bar drops, then drops again every 1.2 seconds, four times. The Orc's bar does not move.
6. Click once in the Game view so it has the keyboard, then press **1** (the Goblin loses 8 health), **2** (the Orc loses 12),
   **3** (the Goblin gains 100 experience: the pill changes to "Lv 4" and its bar fills up again) and **H** (both are healed).
   Each plate changes only for its own character.
7. Stop Play. Expand `Goblin Scout > Nameplate > SwatchPlate`, select `Name` and look at **UI Text Bind** in the Inspector.
   The **Format** row is `{owner.name|Goblin}`; hover it for the list of keys; the line under the rows says
   `Owner: Goblin Scout`. Select `SwatchLevel > Label` (format `Lv {owner.level|1}`) and `Health` (UI Bar Bind, variable
   `owner.healthFrac`).
8. Select `Sign Text`. In its **Text** section **Rich Text** is ticked (hover it for the tags). In the text box, wrap a word
   in `[size=250]` ... `[/size]` and look at the Scene or Game view: the lines under it move down and never overlap (before this
   change the big letters climbed into the line above).
9. Make your own (optional): select any object that has a **Stats** component (**Add Component > Stats** on a new sprite or
   cube works), then **GameObject > UI > Prebuilt Screens > Nameplate (in the world)**. The new `Nameplate` becomes that
   object's child, above its head, and shows its name, level and health when you press Play. With nothing suitable
   selected it is made at the scene root and you drag it onto a character in the Hierarchy yourself.
10. The player draws it too (the flat pass that draws world text and the nameplates): in a command prompt in the install
    folder run `Tools\OkaySpacePlayer.exe examples\ui_text\ui_text.okayscene`. Press **1**, **2**, **3** and **H** as in step 6.

**How you know it worked.** The two plates show different names, levels and bars, and a key press moves only the plate of
the character it hurts. The title line of the sign is on its own line and the lines under it start below it. The level pill is
inside the card at any size of the window.

**Known rough.** World text (the sign) is drawn in flat scenes only; a 3D scene does not draw it. The nameplate is a
World Space UI object: the Game view and the player draw it; whether the Scene view previews it is unverified. The sign uses
the 8x8 pixel font (no new font system). A big `[size]` run does not change how a line is wrapped or centred.

**Pictures** (headless renders through the same UI pass as the game, not a real window; they are in a picture folder (pictures are no longer stored in the repository);
the test `ui_text_shots` makes the "after" ones again, the two "before" ones are the unmerged branches' own renders):

| File | What it shows |
|---|---|
| `before_nameplate_overhang.png` | Before: the level pill sticks out of the card, the texts sit at the top-left of their boxes, the card is smaller than its contents |
| `after_nameplate_scaled_with_distance.png` | After, same camera and scale (90 design pixels per unit, scaling with distance): everything inside the card, the level centred in its pill |
| `after_nameplate_default.png` | After, with the default Constant size: the plate keeps 220 x 62 pixels, the text stays readable |
| `before_world_text_overlap.png` | Before: the big word "larger" climbs into the line above it |
| `after_world_text.png` | After: the line with the big word is as tall as it, the lines below start under it |
| `sample_scene_start.png` | `examples\ui_text` at the start of Play: two plates with full bars, the sign, the hints |
| `sample_scene_after_hits.png` | The same after about six seconds and a press of 2: the Goblin's bar is at 40 percent, the Orc's at 90 |

**Status.** Tested here on Linux, headless (no window), every run with 0 failures:
* `ui_nameplate_bound` 158 checks (where every piece of the plate is drawn, at three distances and with two fonts, a long
  name, a three-digit level; the `owner.` keys; save and load), `world_text_rich` 272 (the line height and the rich-text tags),
  `ui_text_example` 35 (loads `examples/ui_text/ui_text.okayscene` from disk and plays it: ticks, the script, key presses, the
  level-up), `ui_text_shots` 15 (the pictures above), `runtime_ui` 1303.
* The older UI suites that draw through the changed renderer: `ui` 178, `ui_extra` 150, `uiwidgets` 28, `ui_templates` 152,
  `ui_widgets_more_shots` 9, `accessibility_render` 17, `ui_focus_render` 7, `ui_system` 84, `uidocument` 83. ctest on the
  eleven suites that touch this change (`text`, `runtime_ui`, `ui_renderer_options`, `save_coverage`, `worldcanvas`,
  `ui_renderer_golden`, `ui_text_shots`, `accessibility_wired`, `ui_nameplate_bound`, `world_text_rich`, `ui_text_example`):
  11 of 11 passed.
* Reproduced before fixing: the same two new tests, linked with the renderer from before this change, fail 24 of 158
  (`ui_nameplate_bound`) and 2 of 272 (`world_text_rich`) checks; with the fix both are 0.
* All of the above ran on the branch before two merges: `main` (one SHA-256, vehicles, owner rules; no conflicts) and Claude 2's
  nine newer commits of `claude2/world-text-rich` (its own fix for the same line-overlap problem, which was compared with
  mine and left out of the code, plus its overlap test, its picture block and its text; conflicts in `docs/ui_runtime.md`,
  `editor/main.cpp` and `tests/test_world_text_rich.cpp`). After those merges only the cheap checks were repeated: a configure
  of the merged tree, the feature map check, the branch lint, the MinGW win32 syntax check and a syntax-only compile of the
  editor, the player, the renderer and the tests. **The suites were not re-run on the merged tree here** (the shared build
  queue was too long and the disk too full for a second build tree); CI does that. Claude 2's new overlap test was checked
  by hand against the merged layout only.

**Unverified:** the real editor window and menus (steps 1, 3, 7, 8, 9), the Game view drawing of the plates in the editor, the
player run in step 10 (the player's drawing code was only syntax-checked, never run), the Windows exe and the installer copy of
the folder (the example-copy block of `scripts/build-windows-dist.sh` was run alone and does put `examples/ui_text/ui_text.okayscene`
in the output; `bash -n` of the whole script is clean). The editor code for the new Inspector rows and
menu entry was only syntax-checked, not run. The pictures are headless renders, not screenshots of a window.
