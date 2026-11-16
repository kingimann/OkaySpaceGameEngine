---
id: 7zr
row: | 7zr | UI library slice 3: app parts (menu bar, toolbar, tree view, data table, wizard, form group, colour picker upgrade) | File > Examples > UI library > ui_library_3 (then Play), or GameObject > UI > App; file `examples\ui_library\ui_library_3.okayscene` | New in this change; headless tests and pictures only, real window and editor menus unverified |
---
## 7zr. UI library slice 3: app parts

**What it is.** A UI screen for the app parts added in slice 3 of the in-game UI component library
([ui_library.md](ui_library.md); [ui_component_audit.md](ui_component_audit.md) says what already existed): a menu bar, a toolbar with
toggles and one-of-a-group buttons, a tree view, a sortable data table that only draws the rows on screen, a wizard with a form group on
its steps, and the colour picker with a hue / saturation square plus value and alpha sliders.

**Steps.**
1. Open the editor from its install folder. **File > Examples > UI library (every in-game UI part) > ui_library_3**. (Or **File > Open...**
   with `<install folder>\examples\ui_library\ui_library_3.okayscene`.)
2. Press **Play**. The card "What you chose" at the top right reports what every part picked, so you can see each click do something.
3. Menu bar: click **File**, then **Open**. "Menu: File/Open" shows on the right. Slide the pointer over **Edit** while a menu is open: it
   switches. **Export (soon)** is greyed out and does nothing. Click anywhere else, or press Escape, to close.
4. Toolbar (under the title): click **B**, **I**, **U**: each stays pressed, and "Bold 1  Italic 1" shows. **L**, **C**, **R** are
   one-of-a-group: pressing one releases the others.
5. Tree: click the little arrow next to **Castle** to open it, click **Gate** to select it, then press Up, Down, Left, Right.
6. Table: click the **Level** header (sorted ascending, click again for descending), click a row to select it, click **Add a row** a few
   times and scroll with the mouse wheel. Click the table first, then press Up, Down, PageDown, Home, End.
7. Wizard: click **Next** with the boxes empty: red sentences show and it stays on step 1. Type a name and an email like `ann@site.com`,
   click **Next**. On step 2 try an age of `12` (refused), then `29`. Step 3 greets you by name; **Finish** ends it. **Back** goes back and the
   numbered circles above go back too.
8. Colour picker: click a swatch, then click and drag in the square (hue across, saturation down) and drag the two sliders (brightness,
   see-through): the chip at the right shows the colour. **Advanced on / off** hides and shows the square and sliders.
9. Stop Play. **GameObject > UI > App** adds each part to your own scene (Menu Bar, Toolbar, Tree View, Data Table, Wizard, Form Group);
   **Color Picker** has an **Advanced** tick in its Inspector. Select a part to see its Inspector.

**What you should see.** Menus dropping down over the other cards, toggles staying pressed, a tree opening and closing, rows reordering when a
header is clicked and the same person staying selected, a wizard that refuses to go on until its form is valid, and a colour square you can
drag in. Pictures are in `docs/images/ui_library/` (`menubar-open.png`, `toolbar*.png`, `tree.png`, `table.png`, `form-errors.png`,
`wizard.png`, `color-advanced.png`, `example3-*.png`; headless renders, not a real window).

**Status.** Logic, saving and loading, the pointer and key paths and the shipped scene are tested headless on Linux (`ui_library3`,
`ui_library3_example`, `ui_library3_shots`; see the report for the numbers). Nothing ran on Windows or in a real window. The editor
Inspector and menu entries were written but the whole editor was not run. Gamepad focus-ring navigation does not reach the new parts yet.
