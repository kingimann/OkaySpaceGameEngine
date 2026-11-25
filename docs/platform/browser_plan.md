# Browser plan

> **Status:** Internal record (design note). Written 2026-11-25 with step 1 (branch `claude/page-viewer-1`); steps 2 and 3
> added the same day (branch `claude/browser-app-1`).

The owner asked for our own browser (2026-11-25): an installable program for players, so game developers can make games
"for the browser" and players run them in it. Order chosen by the owner: native first.

## Steps

1. **Page engine (done in step 1).** System "Browser" (feature map 17.18). A library, `okay_browser`
   (`engine/include/okay/Browser/`, standard library only): HTML reader, CSS, layout, addresses, one tab's navigation. The
   launcher uses it for our own pages (`launcher/BrowserPage.cpp`); the OkayUI view is `okayui/screens/BrowserView.cpp`.
   Written from the WHATWG HTML parsing chapter, CSS 2.1, CSS Syntax 3, Selectors 3, CSS Color 3 and RFC 3986; no code
   from any browser or library.
2. **Native OkaySpace web games (done in step 2, first version).** A page holds `<okay-game src="...">` (already laid out as a box, `DrawCmd::Embed`).
   The browser downloads the game's pack and runs it with our own player code inside that box, natively (no WebAssembly
   and no JavaScript). Reuse: the web export's pack format (`OKWPACK1`, `scripts/web_package.py`: a JSON manifest and the
   files, gzip) and our own inflate (`okay/Core/ImageDecode.hpp` `Inflate`); the player's start-up and frame loop
   (`player/main.cpp`), which already reads a game folder. Open questions: where saves go (per site), and what a game from
   another site may do (a permission prompt).
3. **Standalone program (first version done).** `okay-browser` (`browser/`, `OkaySpaceBrowser.exe` on Windows): a browser
   window of its own on the same library. Still to do: tabs, bookmarks, installing it like the launcher and updating it from
   our own server (the release packaging is the coordinator's and the owner's decision).
4. **Later.** Our own JavaScript engine, WebAssembly runner and WebGL, so other engines' web games and normal sites work.

## Decisions in step 1

- Only http and https. A tab stays on the origin it was opened on; other sites go to the system browser.
- Limits on everything a page controls (input size, nodes, depth, attributes, style rules), so a hostile page cannot crash
  or exhaust memory (`browser_hostile`).
- The default style sheet is our own; `img { max-width: 100% }` is added on purpose so images never overflow the view.
- A `#name` link stays on the page even under `<base href>` (the standard would resolve it against the base address).
- Simplified on purpose (listed in `Layout.hpp`): margins collapse between siblings only; no floats or positioning; equal
  table columns; inline boxes draw no borders or backgrounds.

## Decisions in steps 2 and 3

- **The game runs as its own process, not inside the page's box.** The player's start-up and frame loop own the whole
  process: one window, one GL context, global engine state (scene, input, audio, saves), and its own exit. Hosting it inside
  the browser would first mean turning `player/main.cpp` into a library with no globals; that is a large change to shared
  packaging code. A process also keeps a game that crashes or hangs from taking the browser with it. The player gets two
  flags and nothing else changes in it: `--game-dir DIR` (the same data-folder lookup, in DIR) and `--save-dir DIR`.
- **Where a game comes from.** `<okay-game src="game.okaypack.gz">`, or the `<canvas>` of a page the web export wrote (its
  `window.OKAY_WEB_CONFIG` names the pack). Only http(s), only the page's own origin (like images).
- **One folder per site.** `<browser data>/sites/<site>/games/<game>/` holds the unpacked game (downloaded again on each
  Play and replaced), `.../saves/<game>/` its saves (kept). The player is started there with `okaycli::Child` (no shell,
  arguments one by one), and nothing from the page reaches its command line except through those two folder names, which
  are made only of letters, digits, '.', '-' and '_'.
- **The pack is untrusted.** Read by our own reader (`Browser/GamePack`): caps on every size, gzip checked (CRC-32 and
  length), names through `PathSafety`, written to `<folder>.part` and renamed when complete. The format
  (`scripts/web_package.py`, Claude 3's lane) has no per-file hash and no signature, so the browser can tell a damaged
  pack from a good one but not who made it, the same as the web player. Adding a SHA-256 per file to the manifest would be
  a small change there; the reader would then check it.
- **What is NOT the same as a web page yet.** A web game in a real browser cannot touch the disk or open raw sockets. Here
  the game is our own player running our own engine: its scripts only reach files by relative names inside the game and save
  folders (`PathSafety`), and no native code from the pack is loaded (the player loads no plugins; C# is off by default). But
  the process itself has the user's rights, and its network (multiplayer) is not limited. An operating-system sandbox for
  the player (a restricted token on Windows, namespaces on Linux) is the next safety step.
- **Settings > Experimental > Browser (`OKAY_BROWSER`).** The launcher's switch is reused: `OKAY_BROWSER=0` switches the
  browser program off too (it then loads nothing); unset or `1` is on.
- **Shared, not copied.** The launcher's fetch worker moved into `shared/BrowserHost.cpp` (`TabHost`), which the launcher's
  page and the program both use; the program draws with OkayUI's window mode (`shared/OkayUIFrameLoop.hpp`) on
  `okay::platform`, like the editor's window mode. The JSON in the manifest is read by `okay/Core/Json.hpp`, the shared JSON
  reader that `docs/engine/shared_code.md` planned (the older private readers are not moved yet).
