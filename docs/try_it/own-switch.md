---
id: own-switch
row: | own-switch | One place to switch our own code on and off, all ON by default since 2026-11-23 (launcher Settings > Experimental, editor View menu) | Launcher: Settings > Experimental. Editor: View > Rendering & Performance > Experimental | Linux build only; Windows unverified |
---
## own-switch. One place to switch our own code on and off (all on by default)

**Read this first: built and unit-tested on Linux; nothing of this was run on Windows.** The own parts it switches are the ones
described in `docs/status/own_code_roadmap.md`. **Since 2026-11-23 every switch is ON by default** (owner: "i want our stuff to be
on by default, all of them"). Some of them are previews (the OkayUI Inspector, the OkayUI own renderer): if one looks wrong, untick it.

What it is: a list of switches. Each one swaps a third-party part for our own. **All are on until you turn one off.** The choice is
saved in one small file, `own_switches.cfg` (Windows: `%LOCALAPPDATA%\OkaySpace\own_switches.cfg`; Linux:
`~/.config/okayspace/own_switches.cfg`). The launcher and the editor read the same file. The file lists what you turned off
(`off=wav`); anything not listed is on. A file saved by an older version (only an `on=` line) does not turn anything off.

What to do (fresh install, or delete `own_switches.cfg` first):

1. Start `OkaySpace.exe` (the launcher). Click **Settings**, then **Experimental** in the list on the left.
2. You see one row per switch: **OkayUI screens**, **OkayUI Inspector**, **OkayUI own renderer** and **Our own WAV reader** on
   Windows (two more on Linux). **Every switch is ticked** and the word on the right says **on now**.
3. Untick one, for example **Our own WAV reader**. The word changes to **off after restart**. Close the launcher and start it
   again: the switch is still unticked and the word says **off**. The file now holds `off=wav`.
4. The launcher prints one line at start (Linux: in the terminal; Windows: **Help > About** in the editor shows the same text). It
   ends with `switches=` and the ids that are on, for example `switches=okayui,inspector,ownrender,wav` with everything on, and
   without `wav` after step 3.
5. Open the editor from the launcher. It inherits the choice. In the editor open **View > Rendering & Performance >
   Experimental: try our own code**. The same switches are there, ticked the same way. Clicking one there changes the same file.
6. **All off (third-party parts)** unticks every switch (after a restart everything is as in an older release). **All on
   (default)** ticks them all again.
7. To check a switch really does something: with **OkayUI screens** on, the What's New page and the editor Console are drawn by
   our toolkit; with **Our own WAV reader** on, a `.wav` in a scene plays through our decoder (`docs/audio/own_wav_decoder.md` lists
   the formats). **Our own web client** and **Our own window layer** are not shown on Windows (Windows uses WinHTTP and its own window code).
8. An environment variable you set yourself always wins. Start the launcher from a Command Prompt with
   `set OKAY_UI_BACKEND=imgui` (or `set OKAY_OWN_WAV=0`): the row says **set by an environment variable**, its switch is greyed out,
   and the part stays third-party for that run.

Good to know:

* The model readers (FBX, COLLADA, 3DS ... and since 2026-11-23 also glTF 1.0, OpenGEX, MDL and Quake III maps) are our own code
  by default and do not need a switch. `OKAY_THIRD_PARTY=1` sends them back to Assimp and ufbx.
* Options chosen only when the program is built are not in this list. Still off on purpose: our own cryptography (owner rule 18),
  `-DOKAY_OWN_STACK` (a developer switch releases refuse), the Nendo seam, and the extra OkayUI programs (`OKAY_LAUNCHER_ON_OKAYUI` /
  `OKAY_EDITOR_ON_OKAYUI`, not built by CI yet). `OKAY_UI_DEFAULT_BACKEND` stays `imgui` so that unticking **OkayUI screens** really goes back.
* To add a switch later: add one line to `Items()` in `shared/OwnSwitches.hpp`. A new item starts on for everyone, because
  only the items in `off=` are off.

Tested on Linux: the `own_switches` test (no file = all on, `off=wav` = only wav off, the old `on=` file = all on, the file round trip,
all on / all off, an environment variable you set winning, the default reaching the OkayUI screens and Inspector without `OKAY_DEV`,
items hidden where they cannot work) and the `own_stack` test (the `switches=` part of the line).
Unverified: the Settings page and the editor menu were compiled but not looked at on a desktop; Windows not run.
