# Browser plan

> **Status:** Internal record (design note). Written 2026-11-25 with step 1 (branch `claude/page-viewer-1`).

The owner asked for our own browser (2026-11-25): an installable program for players, so game developers can make games
"for the browser" and players run them in it. Order chosen by the owner: native first.

## Steps

1. **Page engine (done in step 1).** System "Browser" (feature map 17.18). A library, `okay_browser`
   (`engine/include/okay/Browser/`, standard library only): HTML reader, CSS, layout, addresses, one tab's navigation. The
   launcher uses it for our own pages (`launcher/BrowserPage.cpp`); the OkayUI view is `okayui/screens/BrowserView.cpp`.
   Written from the WHATWG HTML parsing chapter, CSS 2.1, CSS Syntax 3, Selectors 3, CSS Color 3 and RFC 3986; no code
   from any browser or library.
2. **Native OkaySpace web games.** A page holds `<okay-game src="...">` (already laid out as a box, `DrawCmd::Embed`).
   The browser downloads the game's pack and runs it with our own player code inside that box, natively (no WebAssembly
   and no JavaScript). Reuse: the web export's pack format (`OKWPACK1`, `scripts/web_package.py`: a JSON manifest and the
   files, gzip) and our own inflate (`okay/Core/ImageDecode.hpp` `Inflate`); the player's start-up and frame loop
   (`player/main.cpp`), which already reads a game folder. Open questions: where saves go (per site), and what a game from
   another site may do (a permission prompt).
3. **Standalone program.** A browser window of its own (address bar, tabs) on the same library, installed like the
   launcher and updated from our own server.
4. **Later.** Our own JavaScript engine, WebAssembly runner and WebGL, so other engines' web games and normal sites work.

## Decisions in step 1

- Only http and https. A tab stays on the origin it was opened on; other sites go to the system browser.
- Limits on everything a page controls (input size, nodes, depth, attributes, style rules), so a hostile page cannot crash
  or exhaust memory (`browser_hostile`).
- The default style sheet is our own; `img { max-width: 100% }` is added on purpose so images never overflow the view.
- A `#name` link stays on the page even under `<base href>` (the standard would resolve it against the base address).
- Simplified on purpose (listed in `Layout.hpp`): margins collapse between siblings only; no floats or positioning; equal
  table columns; inline boxes draw no borders or backgrounds.
