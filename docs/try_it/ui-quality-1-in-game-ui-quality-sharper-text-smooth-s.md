---
id: ui-quality-1
row: | ui-quality-1 | In-game UI looks sharper: crisp text at any scale, smooth soft shadows, filtered pictures, even borders, kerning; a "UI Quality" setting (Fast / Good / Best / Old look) | Open `examples/ui_quality/ui_quality.okayscene` (File > Open), Lighting window > UI Font > **UI Quality**; or start with `OKAY_CLASSIC_UI=1` to see the old look | Written and unit-tested; pictures and numbers come from the headless software renderer on Linux only. Never run in a real editor or player window, on Windows, on an OpenGL or Direct3D 11 renderer, or on a high-DPI screen (unverified) |
---
## ui-quality-1. In-game UI: sharper text, smooth shadows, filtered pictures

**What it is.** The UI that games draw (buttons, panels, text, pictures, sliders) looked a little rough. Text was
resampled at half-pixel positions (soft), soft shadows were a stack of hard steps, big pictures shown small were
sampled with skipped pixels, and borders at a 1.5x scale were a pixel and a half wide. They are now drawn on the
real pixel grid. A new **UI Quality** setting chooses how careful the drawing is:

- **Fast**: text sits on whole pixels (crisp), the rest as before.
- **Good** (the default): plus slightly heavier small text, smooth soft shadows, pictures filtered when shrunk, borders and shapes snapped to whole pixels at 1.5x, kerning (AV, To pull together).
- **Best**: plus quarter-pixel text positions (even letter spacing) and dithered gradients (no bands).
- **Old look**: the drawing from before this change, so you can compare.

**Steps.**
1. Start the editor. File > Open, go to the `examples` folder, `ui_quality`, open `ui_quality.okayscene`. You see the same
   card three times, drawn at 1x, 1.5x and 2x scale (a title, small text, a button with a border, a slider, a progress bar,
   two small pictures and a thin line, on a card with a soft shadow).
2. Open **Lighting** (Window menu) and the **UI Font** header. Pick **UI Quality** = **Old look**. Look at the three cards:
   text edges are soft, the shadow shows steps, the small picture shimmers. Switch to **Good**, then **Best**. Text becomes
   sharp, the shadow smooth, the picture clean. Press Play to see the same in the Game view.
3. The other two files show each look without touching the setting: `ui_quality_best.okayscene` (Best) and
   `ui_quality_old_look.okayscene` (Old look).
4. To see the old look in any game without editing it, start the editor, player or your exported game with the
   environment variable `OKAY_CLASSIC_UI=1` (for the quality levels: `OKAY_UI_QUALITY=fast`, `good`, `best` or `old`).
   The startup log is unchanged; the setting is read when a scene is drawn.
5. Old scenes load unchanged: a scene without the setting is **Good**. Scenes you save keep the setting (`uiquality` line).

**What you should see.** Good and Best: text edges are crisp, small text is a little bolder, letters in a title are evenly
spaced (Best), the card shadow fades smoothly with no visible rings, a 1 px border is the same width all round at every
scale, a 128 px picture shown at 24 px is clean not noisy. Old look: the opposite.

**Checked.** `test_ui_quality` (no window needed) measures every claim above with numbers; `ui_quality_shot` writes
headless pictures (`ui_quality_shot out.png 1280 720 good`). Not checked: a real window, Windows, GPU renderers, high-DPI.
Details and the before/after table: `docs/ui/in_game_ui.md`.
