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
   window of its own on the same library. Still to do: installing it like the launcher and updating it from our own server
   (the release packaging is the coordinator's and the owner's decision).
3a. **A regular browser, round 1 (owner, 2026-11-25: "i want the browser to be like a regular browser"; branch
   `claude/browser-full-1`).** Browser basics in the program, real layout, forms. See "Round 1" below.
4. **Next round.** Our own JavaScript engine (it lives on `claude/javascript-1`, `engine/include/okay/JavaScript/`; the browser will
   use that one, not a second one), then WebAssembly and WebGL, so other engines' web games and normal sites work.

## Decisions in step 1

- Only http and https. A tab stays on the origin it was opened on; other sites go to the system browser.
- Limits on everything a page controls (input size, nodes, depth, attributes, style rules), so a hostile page cannot crash
  or exhaust memory (`browser_hostile`).
- The default style sheet is our own; `img { max-width: 100% }` is added on purpose so images never overflow the view.
- A `#name` link stays on the page even under `<base href>` (the standard would resolve it against the base address).
- Simplified on purpose (listed in `Layout.hpp`): margins collapse between siblings only; equal table columns; inline boxes
  draw no borders or backgrounds. (Floats and positioning came in round 1.)

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

## Round 1: a regular browser (2026-11-26)

Written from the WHATWG HTML standard (forms, 4.10), CSS 2.1 (floats, positioning, the painting order, overflow), CSS Flexible
Box Layout 1, CSS Grid Layout 1, Selectors 4, CSS Values 4 (calc, min, max, clamp, the viewport units), CSS Custom Properties
1, Media Queries 4, CSS Fonts 4 (@font-face), CSS Backgrounds 3, RFC 6266 / 8187 (download names), RFC 9110 (headers) and the
WHATWG URL standard (the urlencoded serializer). No code from any browser or library.

**A. The program** (`browser/main.cpp`, model in `okay/Browser/Profile.hpp`, tested in `browser_profile`):
tabs (Ctrl+T, Ctrl+W, Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PageUp / PageDown, Ctrl+1..9, Ctrl+Shift+T reopens; a middle click or
Ctrl+click on a link opens it in a background tab after the current one; a middle click on a tab closes it), per-tab history
(Back / Forward, Alt+Left / Right), a history of every visit (`about:history`, Ctrl+H), bookmarks (the star or Ctrl+D; a
bookmarks bar, Ctrl+Shift+B; `about:bookmarks`), downloads (an answer that is not a page is offered with its name, type and
size, saved to the Downloads folder under a cleaned, unique name, with a progress shelf and `about:downloads`, Ctrl+J), the
address bar (an address or words to search), zoom per site (Ctrl + / - / 0), find on the page (Ctrl+F, F3 / Shift+F3, Enter),
a context menu (open link in a new tab, copy link, copy text, back, forward, reload, bookmark), a new-tab page (bookmarks and
recent pages) and a Home setting, Stop (Esc), the page title on the tab and the window, a loading spinner on the tab and a
loading bar over the page (the last page stays while the next loads). What lasts (history, bookmarks, zoom, settings) is in
`profile.json` in the browser's own settings folder (`PrefPath("OkaySpace", "Browser")`), written to a temporary file first.

- **Search.** The owner chose Google (2026-11-26: "use google for search"). The default search address is Google's documented
  query form `https://www.google.com/search?q=%s`; the words are encoded with the URL standard's urlencoded serializer (space
  as `+`, `&`, `#` and every non-ASCII byte as `%XX`). It is a plain address opened like any page (no SDK, no API, no tracking
  parameters) and a setting (Menu > Settings; empty = no search, the bar then only takes addresses). The empty bar says
  "Search Google or type an address".
- **What counts as an address:** a scheme (`https://...`, `about:...`), `localhost` or an IP address (opened with http), or one
  word that looks like a host name (`example.com/x`, opened with https). Anything with a space, or one word without a dot, is
  a search.
- **The tab in the program goes to any site** (`BrowserTab::SetAnyOrigin`), like a regular browser; the launcher's tab still
  keeps to one site. Our own pages (`about:newtab`, `about:history`, `about:bookmarks`, `about:downloads`) are made by the
  program and may only link to each other: a web page cannot open them (they show the person's history).
- **Redirects.** The page's address is the final one after redirects (`ExResult::finalUrl`, filled by both HTTP clients), so
  relative links on a redirected page resolve correctly; a redirected POST becomes a GET.
- **Downloads** are decided by the answer: `Content-Disposition: attachment`, or a type that is not HTML, text or an image;
  an answer too big to be a page (4 MB) is offered as a download too. The file name comes from `filename*` / `filename` or the
  address, cleaned (no folders, no `..`, no reserved characters or Windows device names, at most 120 bytes).

**B. Layout** (`Layout.cpp`, `Css.cpp`; tested in `browser_layout2` with boxes worked out by hand from the specifications):
floats and clear (lines wrap around floats; boxes that make their own formatting context go beside them and hold their
floats), relative / absolute / fixed / sticky (as relative) positioning with containing blocks, static positions and
shrink-to-fit, z-index layers, overflow hidden / clip / scroll / auto (clip regions and scrollable areas the view scrolls with
the wheel and draws bars for), flexbox (direction, wrap, grow / shrink / basis with min and max violations, the automatic
minimum, justify-content, align-items / -self / -content, auto margins, gaps, order, the reverse directions), grid (template
columns / rows with px, %, fr, auto, min-content, max-content, minmax(), repeat() with a count or auto-fill / auto-fit, named
lines and template areas, explicit and auto placement, sparse and dense, row and column flow, spans, negative lines, gaps,
justify / align items and self), inline-block (and inline-flex / -grid) with baselines, replaced boxes as blocks, min- /
max- width and height, box-sizing, percentages, em / rem / vw / vh / vmin / vmax, calc() / min() / max() / clamp(), custom
properties with var() and fallbacks, @media (types, width / height / orientation / prefers-color-scheme, ranges), @supports,
@import, linked style sheets (`<link rel=stylesheet>`, url() resolved against the sheet), more selectors (attribute, sibling
combinators, structural pseudo-classes, :not / :is / :where, :hover, :focus, :focus-within, :checked, :disabled ...),
::before / ::after with content, border-radius, box-shadow, background images (size, position, repeat), opacity, visibility,
text-transform, text-indent, vertical-align middle / top / bottom for inline boxes. Parsed sheets are kept per page, not read
again for every layout.

- **Web fonts.** @font-face TrueType / OpenType files are fetched and given to OkayUI's TrueType store (our own rasterizer);
  text whose font-family names them uses them, with the closest weight and style (a real bold face when the page has one).
  WOFF / WOFF2 are not read yet (they need their own decoders); OkayUI keeps only a few font files, so a page with many faces
  gets the first ones. Italic without an italic face is drawn upright (OkayUI cannot slant text yet).
- **Work caps for hostile pages.** Every block layout counts against a budget; flex and grid measure their items through a
  cache, so nested flex / grid boxes are not exponential; var() expansion is capped in size and depth; selector matching has a
  step budget; grids are capped at 1000 columns and 20000 rows. `browser_hostile` has a case for each.

**C. Forms** (`okay/Browser/Forms.hpp`, tested in `browser_forms`): text, password, email, search, url, tel, number, hidden,
checkbox, radio, submit, reset and button inputs, `<button>`, `<textarea>`, `<select>`, `<label>` (for="" and wrapping);
radio groups per form, disabled controls and fieldsets, maxlength; typing goes through an OkayUI text field placed over the
page's field (password fields hide the text), a select opens a list; GET and POST (application/x-www-form-urlencoded, and
text/plain) with the submitter's name and value, formaction / formmethod / formenctype, Enter submits (implicit
submission), reset. multipart/form-data (files) is not done: such a form is sent without its files and says so.

**Not done in round 1:** JavaScript (next round, with the engine on `claude/javascript-1`), cookies (so sites that need a login
session do not keep it), tables with real column widths, margin collapsing through parents, `:has()`, counters, gradients
(drawn as their first colour), WOFF fonts, synthetic italics, text selection (Copy text copies the run under the pointer),
dragging tabs, a downloads folder picker, file inputs. Nothing of round 1 was built or run on Windows.

**Checked so far (2026-11-26):** the browser tests above and `okayui_browser` pass on Linux (ctest); the program was run on Linux
under a virtual display (Xvfb) against a local Python test server: several tabs, the Flexbox / Grid / Floats test pages, find
with highlights, the bookmarks bar, the menu and the context menu, a download asked for and saved to the Downloads folder, a
form typed into (text, password, select, radio, checkbox, textarea) and sent with POST (the server echoed the urlencoded body),
the new-tab and history pages, zoom. Not checked: real web sites, the launcher's page (compiled only), Windows.

