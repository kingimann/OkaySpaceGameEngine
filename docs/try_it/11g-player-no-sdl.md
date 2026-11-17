---
id: 11g
group: appendix
row: | 11g | Player with no SDL, game resolutions on our own code | `Tools\OkaySpacePlayer.exe` (section "Player with no SDL") | Run on Linux under Xvfb only; Windows unverified |
---
## Player with no SDL, game resolutions on our own code (branch `claude2/sdl-player-no-sdl`, not merged or released yet)

What changed (SDL removal, `docs/sdl_removal_plan.md`, "Player, display modes and a build with no SDL"): with
`OKAY_PLATFORM_BACKEND=own` the game player makes **no call into SDL at all**, from start to exit. Our own platform code now also lists
a monitor's **display modes** (resolutions and refresh rates) and switches the monitor to one for **exclusive fullscreen**. It is
**off by default**: with no variable nothing changes. One fix is on for everyone: the player's window **close button** (X) now ends the
game; before, it did nothing while the GPU renderer was on.

* **Switch.** `OKAY_PLATFORM_BACKEND=own` (the player only needs this one).
* **How you can tell it is on.** The first log line says `platform=own`, then `2D painter: software`.

Steps (Windows, from the release, once this branch is in one):

1. Open a Command Prompt and run: `cd %LOCALAPPDATA%\OkaySpace`, `set OKAY_PLATFORM_BACKEND=own`, then
   `Tools\OkaySpacePlayer.exe examples\vehicle\vehicle_test.okayscene`.
2. Expect the loading screen, then the road scene. Walk with **W A S D**, hold **Shift** to run, move the mouse to look, press
   **Space** and the mouse buttons. Press **F3**: the performance panel opens; F3 again closes it.
3. Press **F11**: the game goes fullscreen; F11 again: back to a window.
4. Resolutions: in the editor add **GameObject > UI > Prebuilt Screens > Settings** to a scene, save it, and run that scene the same
   way. In the Settings panel the **Resolution** and **Refresh rate** lists show your monitor's real modes (before, our own code
   offered only the desktop size). Pick **Fullscreen** and a smaller resolution, **Apply**: the monitor switches to it. **Done**, then
   switch back to **Windowed**: the monitor's normal resolution comes back. Closing the game also puts it back.
5. Close the game with the **X** of its window: it closes (the fix above; also without the variable).

The editor's Play gets the same resolution list with its three switches (section "Editor with no SDL at all").

**Status.** Run on Linux only, under Xvfb with software rendering, with a trap that stops the program on any SDL call
(`scripts/player_no_sdl_check.sh`: 15 seconds of keys and mouse, F3, F11, then the close button: no SDL call, exit 0). The display
modes were tested under Xvfb with modes the test adds (`platform_display_modes_own`). The Windows parts (`EnumDisplaySettingsW`,
`ChangeDisplaySettingsExW`) were only compiled: **steps 1 to 5 are unverified on Windows until you run them.** Not possible yet: a
build of the editor, launcher and player with no SDL at all exists (`-DOKAY_WITH_SDL=OFF`, built and run on Linux) but it is a
developer build, never a release, so there is nothing to try in the Windows release for it.
