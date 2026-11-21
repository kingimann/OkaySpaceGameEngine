---
id: own-switch
row: | own-switch | One place to switch our own code on and off (launcher Settings > Experimental, editor View menu) | Launcher: Settings > Experimental. Editor: View > Rendering & Performance > Experimental | Linux build only; Windows unverified |
---
## own-switch. One place to try our own code (no environment variables)

**Read this first: built and unit-tested on Linux; nothing of this was run on Windows.** The screens and the file logic are
new; the own parts they switch on are the ones described in `docs/status/own_code_roadmap.md`.

What it is: a list of switches. Each one swaps a third-party part for our own. **All are off until you turn them on.** The
choice is saved in one small file, `own_switches.cfg` (Windows: `%LOCALAPPDATA%\OkaySpace\own_switches.cfg`; Linux:
`~/.config/okayspace/own_switches.cfg`). The launcher and the editor read the same file.

What to do:

1. Start `OkaySpace.exe` (the launcher). Click **Settings**, then **Experimental** in the list on the left.
2. You see one row per switch (our own WAV reader, plus OkayUI screens where the build has OkayUI, and two more on Linux), each with a
   one-line description and a word on the right: **off**. Click a switch. The word changes to **on after restart**.
3. Close the launcher and start it again. The word is now **on now**. The launcher also printed one line at start
   (Linux: in the terminal; Windows: **Help > About** in the editor shows the same text), and it ends with
   `switches=wav` (the ids that are on). With nothing on, the line has no `switches=` part.
4. Open the editor from the launcher. It inherits the choice. In the editor open **View > Rendering & Performance >
   Experimental: try our own code**. The same switches are there, with the same words. Clicking one there changes the
   same file.
5. Click **Back to normal (all off)** (launcher page or editor menu). Every switch goes to off and the words say
   **off after restart**. After a restart everything is as in a normal release.
6. To check a switch really does something: turn on **Our own WAV reader**, restart, add a `.wav` file to a scene and press
   Play. It plays through our decoder (`docs/audio/own_wav_decoder.md` lists the formats). **Our own web client** and **Our own
   window layer** are not shown on Windows (Windows uses WinHTTP and its own window code).
7. An environment variable you set yourself always wins. Start the launcher from a Command Prompt with
   `set OKAY_OWN_WAV=1`: the row says **set by an environment variable** and its switch is greyed out.

Good to know:

* The numbered model readers (FBX, COLLADA, 3DS ...) are already our own code by default since 2026-11-17 and do not need a
  switch. `OKAY_THIRD_PARTY=1` sends them back to Assimp and ufbx.
* Options chosen only when the program is built (`-DOKAY_OWN_CRYPTO`, `-DOKAY_OWN_STACK`, `-DOKAY_UI_DEFAULT_BACKEND`) are not
  in this list. Cryptography is never on here (owner rule 18).
* The old developer row **UI backend** (shown only with `OKAY_DEV=1`) still works. The **OkayUI screens** switch sets
  `OKAY_UI_BACKEND=okayui`, which already overrides that row.
* To add a switch later (okay::platform events, our own ImGui renderer, the okayui host): add one line to `Items()` in
  `shared/OwnSwitches.hpp`.

Tested on Linux: the `own_switches` test (reading and writing the file, master off, an environment variable you set winning,
the status words, items hidden where they cannot work) and the `own_stack` test (the `switches=` part of the line).
Unverified: the Settings page and the editor menu were compiled but not looked at on a desktop; Windows not run.
