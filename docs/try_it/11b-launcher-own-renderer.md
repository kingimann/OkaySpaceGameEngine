---
id: 11b
group: appendix
row: | 11b | Launcher drawn by our own renderer (off by default) | `OKAY_LAUNCHER_RENDERER=own`, then `OkaySpace.exe` (section "Launcher: drawing with our own renderer") | Footer says "own renderer"; the chat opens in its own window and the OkayUI preview works with it too; Linux/Xvfb only, unverified on Windows |
---
## Launcher: drawing with our own renderer as an option (branch `claude2/sdl-launcher-painter`, not merged or released yet)

What changed (SDL removal, stage B): the launcher can draw without `SDL_Renderer` and without Dear ImGui's SDL backends. It is
**off by default**: with no variable the launcher draws exactly as before.

* **Switch.** `OKAY_LAUNCHER_RENDERER=own` (anything else, or no variable, is the old SDL path).
* **How you can tell it is on.** The footer at the bottom of every page reads `OkaySpace v... (own renderer: opengl)`.

Steps:

1. Open a Command Prompt in the folder of `OkaySpace.exe`, run `set OKAY_LAUNCHER_RENDERER=own`, then `OkaySpace.exe`.
2. The splash shows, then the Create page. The footer says `(own renderer: opengl)`. The page should look the same as without the
   variable: fonts, colours, rounded corners, icons.
3. Click into the **Search** box next to "Recent projects" and type a few letters, including Backspace, arrows, Ctrl+A, Ctrl+C and
   Ctrl+V. The mouse pointer turns into a text cursor over the box.
4. Click **Play**, **Friends**, **Settings** in the sidebar; scroll a long page with the mouse wheel; resize the window and make it small.
5. Sign in and open a chat: avatars and images show. With the setting **Open chat in its own window** on (Settings, the default), the chat opens
   in its own window ("OkaySpace Chat"), drawn by our renderer too: type in it, scroll it, drag a file onto it, move and resize it,
   close it with its X and open it again. Minimise the launcher: the chat window keeps going.
5a. Settings > Appearance > UI backend: **OkayUI (preview)**, then open **What's New**: the page is drawn by OkayUI on our renderer.
   Click "Check for updates" and scroll. (Before, `own` switched OkayUI off.)
5b. With a language that uses an input method (Japanese, Chinese, Korean): click into a text box and start typing; the input
   method's candidate list opens next to the text cursor, not in a corner of the screen.
5c. Leave the launcher in the background while a call rings or an update is found: its taskbar button flashes (the chat window's
   button for a call while the chat window is open).
6. Move the launcher to a monitor with 125% or 150% scaling: the text stays sharp and clicks land where you click.
7. Close the window with the X: the "Exit or Minimize" question appears; Minimize, then restore from the taskbar.
8. Close the launcher, start it again **without** the variable: the footer no longer mentions the renderer.

If something looks different, is missing or does not react only with the variable set, that is our renderer: report the step and,
if you can, a screenshot of both. If `own` cannot start (no OpenGL), the launcher falls back to SDL by itself and draws as before.

**Status.** Tested on Linux only, under Xvfb with Mesa's software OpenGL (no real GPU): `launcher_imgui_renderer` (our ImGui renderer
against a reference rasteriser, software painter; the OkayUI host fed from our events gives the same input as from SDL's),
`launcher_imgui_renderer_gl` (the same on our OpenGL painter, sdl2 and own windows; two painters on two windows drawn in turn; the IME
caret), `platform_state` (the taskbar-flash hint read back from the X server), and `scripts/launcher_renderer_check.sh` (screenshots
with both renderers, typing, a click on Play, the chat window and the OkayUI page with both renderers, frame time). The launcher, the
seam, the OpenGL painter and the Win32 flash, IME and message-hook code passed a MinGW (Windows) syntax check only. **Not run on
Windows: every step above is unverified until you run it** (on Linux the tray is not used and the flash is the urgency hint).
