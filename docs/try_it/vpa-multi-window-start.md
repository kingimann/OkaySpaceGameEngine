---
id: vpa
row: | vpa | Multi-window panels start cleanly on every video driver | `Tools\OkayEngine.exe`: drag a panel tab outside the editor | Linux only (Xvfb, a build with assertions on); Windows unverified |
---
## vpa. Multi-window panels: set once, before the first frame

What changed: the editor turns on Dear ImGui's multi-window panels ("viewports") once, before its first frame, and only when the
video driver can make extra OS windows and read the mouse anywhere on the desktop. It no longer switches them back on at the end of a
frame after Dear ImGui has turned them off. Up to v2.880 it did, and a build with assertions on stopped at its second frame with
"Please set ViewportsEnable before the first call to NewFrame()". Release builds ignored that check but flipped the option on and
off every frame.

Try it on Windows (the Windows driver always supports it, so nothing should change for you):
1. Start `Tools\OkayEngine.exe` and open any project.
2. Drag the **Console** tab out of the editor window and let go over the desktop. It becomes its own window. Drag it back over the
   editor to dock it again.
3. Open the **Console**: there is no "Multi-window panels are off" line.

What you should see on a system without that support (Linux with `SDL_VIDEODRIVER=wayland`, or `SDL_VIDEODRIVER=offscreen`): the
editor starts, a dragged tab stays inside the editor window, and the Console says once "Multi-window panels are off: the video driver
... cannot open extra windows here."

Verified on Linux with an editor built with assertions on (`-O1`, no `NDEBUG`, Dear ImGui included): with `SDL_VIDEODRIVER=offscreen`
the editor before this change stopped at its second frame every time, in both backends (Dear ImGui and `OKAY_UI_BACKEND=okayui`); with
it, it ran for the whole 25-second check in both and printed the "Multi-window panels are off" line. Under Xvfb (X11) both versions
ran in both backends without stopping, and with the change a Project tab dragged outside the editor still became its own window. The
intermittent stop seen before under Xvfb was SDL falling back to the `offscreen` driver when the X server was not ready yet.
Unverified: Windows (not built or run here), a real Wayland session.
