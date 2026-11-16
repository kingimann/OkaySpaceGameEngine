---
id: 18
group: switches
row: | 18 | Own font rasteriser in the editor and launcher (off by default) | `OKAY_OWN_FONT_RASTER=1` | Tested headless on Linux; never run in a real window or on Windows |
---
### 18. Own font rasteriser in the editor and launcher

* **What it is.** The editor's and launcher's text is drawn by Dear ImGui, whose letters come from a rasteriser built
  into Dear ImGui (third-party code). With this switch every letter comes from OkaySpace's own TrueType rasteriser
  instead. The layout does not change: the same sizes, line heights and letter widths. Details and measurements:
  `docs/own_font_raster.md`.
* **Steps.**
  1. Open a Command Prompt in the folder with `OkaySpace.exe`. Type `set OKAY_OWN_FONT_RASTER=1` and press Enter.
  2. Start `OkaySpace.exe` from that same window. The launcher looks the same; the letters should be **slightly
     sharper** (Dear ImGui's rasteriser oversamples and blurs edges a little; ours draws each letter exactly at its
     size on whole pixels). Accented letters (é, ç, ß) and the chat's `code` font should look normal.
  3. Start the editor (`Tools\OkayEngine.exe`) from the same window, or from the launcher. Open **Console**: the first
     lines should include `Fonts: OKAY_OWN_FONT_RASTER=1, the editor's text is drawn by OkaySpace's own TrueType
     rasteriser`. Open the Script Editor: the code font (JetBrains Mono) should line up in columns as before.
  4. Close both, start them again from a new window without the variable: the old rasteriser is back.
* **What you should not see.** Text moved up or down by a pixel, letters cut off, boxes instead of letters, or wider
  or narrower text. Any of those is a bug: please tell us which text and at what Windows scaling.
* **Not in this switch.** Kerning (letter pairs like "AV" or "To" moving closer) is applied in the game's UI text and
  in OkayUI, not in the editor's and launcher's Dear ImGui text, whose layout has no kerning step.
* **Verified here (Linux, headless):** `test_font_raster_compare` draws the same strings at 12 to 48 px with both
  rasterisers through Dear ImGui's real atlas: equal widths and line metrics, ours 0.2 to 0.5 levels (of 255) from
  exact coverage on average, Dear ImGui's 2.7 to 5.8; the whole UI font atlas bakes in about 0.9 times the time with a
  quarter of the memory. **Unverified:** everything on Windows and in a real window (the editor and launcher were only
  compiled for a syntax check).
