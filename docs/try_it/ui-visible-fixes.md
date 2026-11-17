---
id: ui-visible-fixes
---
## Visible UI fixes: launcher library, update card, Scene view, Project Settings, Console (bug fixes)

Fixes for things seen in screenshots of the default (Dear ImGui) launcher and editor, plus the Console of the OkayUI preview.

1. **Launcher, Community page.** Put two game folders (each with an `.okayscene`) in the `community` folder next to
   `OkaySpace.exe` and open **Community**. You should see each game **once** ("YOUR LIBRARY — 2"; it used to say 4 and show
   each card twice). The **Play** badge counts each built game once.
2. **Launcher, update card.** When a newer version is found, the sidebar card "Update available" shows a full **Install now**
   button (the label's bottom used to be cut off). To see it without a real update: start the launcher with
   `OKAY_UI_DEMO_UPDATE=9.0.0`.
3. **Editor, opening a scene.** Open `examples\physics\physics_playground.okayscene`. It opens in the **3D** view (it used to
   open in 2D, flattened). Open **Game**: the nine station labels ("1 TOWER ..." to "9 DEBRIS ...") are readable, each on its
   own dark box, none on top of another at the default layout. Open `examples\ui_text\ui_text.okayscene`: it opens in 2D.
4. **Editor, Scene view stats line.** Open `examples\own_wav\own_wav_test.okayscene`. The "3D | 3 objects | .. FPS" line sits
   at the **bottom left**, because the scene's instruction text is at the top left (it used to be drawn over that text).
   In a scene with nothing at the top left it stays at the top left.
5. **Editor, Edit > Project Settings.** Under Player, Width and Height show **1280** and **720** in full. The first line wraps;
   the physics Quality box says **Improved** with what it does on the line below.
6. **Editor, Project panel > select a sound.** In the Inspector, Import Settings: "Load the whole file" shows in full and the
   notes wrap instead of running off the right edge.
7. **OkayUI preview Console** (start the editor with `OKAY_UI_BACKEND=okayui`): the Console sits inside its dock (it was drawn
   off by the window's place on the screen), "Search" is the hint inside the box, and in a narrow dock "Auto-scroll" moves
   to the next line instead of being cut to "Auto-scro".

Tests: `ctest -R "library_scan|editor_layout|examples_work|okayui_screens"`. Checked by screenshots on Linux (Xvfb); not
checked on Windows.
