---
id: 7zt
row: | 7zt | UI library slice 4: styles, themes, icons, ready-made screens, the UI Gallery and the cleaned-up GameObject > UI menu | GameObject > UI (eight groups) and GameObject > UI > Browse All UI...; File > Examples > UI library > ui_library_4 and ui_starter_pack | New in this change; headless tests, pictures and a syntax check of the editor code only, real window and editor menus unverified |
---
## 7zt. UI library slice 4: styles, themes, icons, screens and the UI Gallery

**What it is.** The in-game UI now has a very large choice, all of it data and not hand-written classes: 111 named styles (made from 14
shapes, 12 fills, 9 borders, 9 effects and 4 groups of state looks, 5 sizes, 4 densities), 54 themes, 371 vector icons drawn by our own
code, and 56 ready-made screens in 9 sections. **GameObject > UI** is now eight short groups with plain names, and its last entry **Browse
All UI...** opens the **UI Gallery**: search everything, preview it, add it with one click. The exact numbers and how they are counted are
in [ui_catalogue.md](ui_catalogue.md); the design is in [ui_library.md](ui_library.md) ("Slice 4").

**Steps.**
1. Open the editor from its install folder. Open the menu **GameObject > UI**. You should see eight groups: **Basics, Input, Layout, Game,
   Menus & Screens, Data & Lists, Windows & Pop-ups, Ready-made Pieces**, then a line, then **Browse All UI...**. Hold the pointer over any
   item: a one-sentence tip says what it does. Nothing needs a Canvas first: click **Basics > Button** in an empty scene and a Canvas and
   an Event System appear by themselves.
2. **GameObject > UI > Menus & Screens > Starter Pack (a whole game UI)**. Press **Play**. Only the Main Menu shows. Click **Settings**
   (four tabs: Video, Audio, Controls, Accessibility; drag the Master volume slider), **Done**, then **Play**: the game HUD shows.
   Click **Pause**: the pause menu opens. **Resume** goes back; **Give up** opens Game Over; **Try again** goes back to the game;
   **Quit to menu** returns to the Main Menu. No script was written.
3. Stop Play. **GameObject > UI > Browse All UI...** The UI Gallery opens. Type `inventory` in the search box. Pick **Inventory Grid** on
   the left. A preview appears on the right. Pick another **Layout** (Centered, Dimmed backdrop). Pick a **Theme** in the drop-down at the top:
   the preview changes colour. Click **Add to scene**: the screen appears under the Canvas, in that theme.
4. In the Gallery choose **Themes** in the second drop-down, pick **Neon**, click **Apply to the whole scene**: every button, panel and
   slider in the scene changes look. Choose **Styles**, pick **Glass Pill**, select a button in the Hierarchy, click **Apply to selected**
   (try the Size and Density boxes: XS to XL, compact to spacious). **Randomise selected** gives it a random style.
5. Choose **Icons**, type `sword`. Click an icon: a large preview with a Tint colour and a Size slider. Select an Image or a Button in the
   Hierarchy and click **Use on selected Image / Button**. The icon shows crisp at any size.
6. **File > Examples > UI library > ui_library_4**, press **Play**. Click a theme on the left (Neon, Retro Pixel, Candy, Paper...): the whole
   page restyles, including the twelve style buttons in the middle (each stays in its own style, in the new colours). Close and reopen
   Play: the theme you picked is remembered. **File > Examples > UI library > ui_starter_pack** is the Starter Pack as a ready scene.
7. In a script, `ui_set_theme("Cyber Pink")` switches the theme, `ui_set_style("Play", "Neon Pill")` styles one widget, and
   `ui_theme_sound("click")` reads the sound name of the theme. A widget whose **UI Style Variant** has a **Theme Name** keeps that theme.

**What you should see.** A short menu that explains itself, a Gallery that finds things as you type, previews in the chosen theme, a
Starter Pack whose buttons all work, and pages that change look when a theme is picked. Pictures are in `docs/images/ui_library/slice4/`
(`themes-side-by-side.png`, `icons-all.png`, `styles-sheet.png`, `screens-contact-sheet.png`, one picture per screen under `screens/`);
they are headless software renders, not a real window.

**Status.** The registries, the style and theme code, the icon pixels, every screen (builds, stays on screen and inside its card at
16:9, 4:3, ultrawide and portrait, saves and loads back, no two buttons overlap), the gallery search, the menu data and the two example
scenes are tested headless on Linux (`ui_library4`, `ui_library4_example`, `ui_library4_shots`, `menu_bar_model`). The editor code
(menu, UI Gallery window) was only syntax-checked: the editor was not built or run, and nothing ran on Windows. Not done: stripe, noise and
image fills and dashed borders (the renderer cannot draw them), theme sounds are named but nothing plays them yet, the Gallery previews of
screens are close drawings of the real widgets and not the renderer's pixels.
