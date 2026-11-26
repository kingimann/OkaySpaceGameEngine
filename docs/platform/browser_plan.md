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
   window of its own on the same library. The Windows package carries it in `Tools/` (2026-11-26, a launcher button starts
   it). Still to do: installing it like the launcher and updating it from our own server on its own.
3a. **A regular browser, round 1 (owner, 2026-11-25: "i want the browser to be like a regular browser"; branch
   `claude/browser-full-1`).** Browser basics in the program, real layout, forms. See "Round 1" below.
3b. **The browser in the launcher (owner, 2026-11-26: "can you allow user to launch browser in the launcher"; branch
   `claude/browser-full-1`).** See "The launcher's Browser page" below.
4. **Round 2: JavaScript in pages (branch `claude/browser-js-1`).** The page's scripts run on our own JavaScript engine
   (`engine/include/okay/JavaScript/`, merged in #1322; the browser uses that one, not a second one: its core became the
   library `okay_js`, linked by both the engine and `okay_browser`). See "Round 2" below.
4a. **Round 3: the web platform scripts need (branch `claude/browser-js-2`, on top of round 2).** fetch / XMLHttpRequest,
   cookies, storage, history, key / mouse / focus events, document.write, async / defer, live collections,
   getComputedStyle, dialogs. See "Round 3" below.
5. **Next.** More of the DOM and the web platform (below), then WebAssembly and WebGL, so other engines' web games and normal
   sites work.

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
  WOFF and WOFF2 files are decoded by `Browser/WebFont` (WOFF2 through our Brotli decoder `Core/Brotli`, 2026-11-26, owner:
  "Yes use WOFF2"), but the host does not call it yet (below); OkayUI keeps only a few font files, so a page with many faces
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
(drawn as their first colour), WOFF fonts, synthetic italics (round 3 did tables, margins, `:has()`, counters, gradients and
WOFF 1.0; see below), text selection (Copy text copies the run under the pointer),
dragging tabs, a downloads folder picker, file inputs. Nothing of round 1 was built or run on Windows.

## Round 2: JavaScript in pages

`okay/Browser/Script.hpp` (`PageScripts`, one per shown page, made only when the page has a script or an on...="" attribute):

- **When scripts run.** After the page is read, in page order (like `defer`); a `<script src>` is fetched by the tab's loader
  (`FetchKind::Script`, same origin in the launcher, any http(s) site in the program) and the scripts after it wait for it.
  Then DOMContentLoaded and load. Module scripts and data types (`application/json` ...) are not run. Scripting is on by
  default (`BrowserTab::SetScripting`); `<noscript>` is hidden while it is on.
- **The DOM.** Written from the WHATWG DOM standard (events 2.x, nodes 4.x, DOMTokenList) and the HTML standard (timers 8.6,
  event handler attributes 8.1.8, fragment serialization 13.3); the list of what is bound is in the header and the feature
  map (17.18.17). Our engine runs all code in strict mode, so sloppy-mode pages that assign to undeclared names fail.
- **The host.** `BrowserTab::Tick(ms)` once a frame (TabHost::Pump does it with the real time) runs the timers and follows a
  `location` change by the link rules; the view hands clicks (`Click`: a cancelled click does not follow the link or work the
  control) and typing (`ControlInput`: input, and change for checkboxes, radios and selects) to the page; `SubmitForm` fires
  submit first. The layout is redone after any change. The browser shows the page's console in a strip (F12).
- **Safety.** No file, network, eval or code-string timers are bound (the engine has none of its own). Each host entry (one
  script, one event, one frame's timers) has an instruction budget, and the binding charges its own work (walking the tree,
  building lists, parsing innerHTML) to it, so `for (;;) document.body.childNodes` is stopped too. Caps: 64 MB heap, 200000
  nodes, 64 MB of page text, depth 256, 1000 timers, 1000 listeners per node. A script that runs past them stops with a
  console line; the page stays up. Nodes a script removes are kept until the next layout no longer points at them and are
  freed only when no script holds them (wrappers are weak; a freed slot's old wrappers no longer match).

**Not done in round 2:** XMLHttpRequest / fetch, `document.write`, running scripts while the page is parsed, scripts a script
adds, `<style>` added or changed by a script, keyboard / mouse-move / focus events, live NodeLists, getComputedStyle, canvas,
storage, the history API, cookies. A node whose own listener's closure holds it stays until the page goes (the listener is a
storage, the history API, cookies (all but canvas came in round 3, below). A node whose own listener's closure holds it stays until the page goes (the listener is a
root). The Console strip and the view's click / input wiring were compiled but not run in a window; nothing on Windows.

**Checked so far (2026-11-26):** the browser tests above and `okayui_browser` pass on Linux (ctest); the program was run on Linux
under a virtual display (Xvfb) against a local Python test server: several tabs, the Flexbox / Grid / Floats test pages, find
with highlights, the bookmarks bar, the menu and the context menu, a download asked for and saved to the Downloads folder, a
form typed into (text, password, select, radio, checkbox, textarea) and sent with POST (the server echoed the urlencoded body),
the new-tab and history pages, zoom. Not checked: real web sites, the launcher's page (compiled only), Windows.

## Round 3: rendering from the CSS specs, and browser plugins (2026-11-26, branch `claude/browser-render-3`)
Written from CSS 2.1 (chapter 17 tables, 8.3.1 margins, 12.4 counters, 12.6.2 list styles), CSS Images 3, CSS Text Decoration 3,
CSS Overflow 3, CSS Transforms 1, Selectors 4 (`:has()`), the HTML standard (table presentational attributes, 15.3.8), the W3C
WOFF 1.0 recommendation and the OpenType table directory, RFC 8259 (plugin manifests). No browser or library code.

- **Tables** (`Layout.cpp` `BuildTable` / `ColumnWidths` / `Table`): anonymous rows and cells, thead first and tfoot last,
  captions (top / bottom, inside the table's border: simplified), colspan (1-1000) and rowspan (0 = to the end of the group),
  automatic and fixed column widths, `<col>` / `<colgroup>` widths, percentage columns, auto-width tables shrink to their columns,
  row heights, vertical-align top / middle / bottom / baseline, row and row-group backgrounds, border-spacing, border-collapse
  (simplified: a cell's left / top border gives way to its neighbour's; no width or style contest), `border` / `cellspacing` /
  `cellpadding` / `bgcolor` / `width` / `align` / `valign` / `nowrap` attributes. At most 1000 columns and 1 000 000 slots.
- **Margins** collapse through a parent's top and bottom and through empty boxes (8.3.1); clearance is still simplified.
- **Gradients**: linear (angles, `to` sides and corners), radial (circle / ellipse, the four size keywords, explicit sizes, `at`),
  repeating, colour stops with one or two positions; drawn as bands of Fill commands (any host draws them); at most 60 000 such
  commands per page, a gradient past that is one Fill. Not done: border-radius on gradient bands, colour hints, conic gradients,
  several background layers.
- **Counters** (`counter-reset / -increment / -set`, `counter()`, `counters()`) and list styles decimal-leading-zero, lower-greek.
- **`:has()`** with `>`, `+`, `~` and descendant relative selectors (at most 4096 elements looked at per test).
- **Text**: text-decoration line / style (solid, double, dotted, dashed, wavy) / colour and overline, drawn as Fill commands when
  not a plain line; `text-overflow: ellipsis` in boxes that clip; white-space nowrap / pre-wrap checked by tests.
- **Images**: object-fit (contain, cover, none, scale-down) and object-position; an image that overflows its box becomes a
  BgImage command clipped to the box.
- **Transforms**: translate and positive scale are folded into the draw commands; rotate, skew and mirrors stay in
  `DrawCmd::xform` for the host. Transitions show their final value at once (the property is read and ignored).
- **WOFF 1.0** (`Browser/WebFont`): rebuilt into the sfnt file through `Core/Inflate`; @font-face takes a WOFF file only when there
  is no TrueType / OpenType file.
- **Browser plugins** (`Browser/Plugins`, `shared/BrowserPluginCommands.hpp`): see feature map 17.18.19. (Dropped from this round by
  the owner on 2026-11-26 after the model and its tests were written; another agent takes it on from here, not wired into the program.)
- **Pages styled properly** (owner, 2026-11-26): CSS Nesting 1; inherit / initial / unset / revert (revert as unset); logical
  properties (inline-size, margin- / padding- / inset- / border-inline and -block); border line styles dashed, dotted, double;
  letter-spacing, word-spacing, text-align: justify, text-shadow (blur as a lighter copy), font-variant: small-caps (capitals at
  0.7 size), ::first-letter (drop caps), ::marker (colour and size); outline and outline-offset; inset box shadows (bands);
  aspect-ratio (a minimum height); filter grayscale() / brightness() on the box's colours (images and blur not done);
  `<img srcset>` (x and w descriptors; sizes taken as 100vw) and `<picture>` sources (media, type); the default sheet's
  details / summary (closed details hide their content), quotes on `<q>`, monospace text at 13 px, h1 in sectioning elements.
- **Page corpus**: twelve pages of our own in `tests/data/browser_pages/`, each with a golden picture
  (`okayui/tests/golden/page_*.png`, test `okayui_browser_pages`).

**Not done in the styling round:** position: sticky that sticks (the layout does not know the scroll), @container queries,
clip-path, blur(), ::first-line, unlayered-over-layered order in @layer (layers are read as plain blocks), conic gradients,
generic font families mapped to our shipped fonts (the view picks fonts; serif / monospace only switch `mono`), glyphs the
shipped font lacks (the summary triangle, a check mark, Greek letters are not drawn: the font has no such glyphs), outer box
shadows under a transparent box (the view fills the shadow's whole rectangle).

**Not done in round 3:** synthetic italics (the slant is
drawn by the view: OkayUI has no sheared text call yet); the host calls below. Nothing of round 3 was built or run on Windows.

**Calls the browser program still has to make** (files of another agent, so not changed here):
- `shared/BrowserHost.cpp`, font fetches: call `okay::browser::DecodeWebFont(bytes, size, sfnt, why)` and hand `sfnt` to the font
  loader instead of the TrueType / OpenType check (WOFF and WOFF2 fonts then load).
- `okayui/screens/BrowserView.cpp` `DrawBrowserPage`: draw a command whose `xform` is not the identity through it (page
  coordinates before scroll and zoom); slant text when `font.italic` and the web font has no italic face.
- `shared/BrowserApp`: keep one `okay::browser::BrowserPlugins`; load manifests from `<data>/plugins/*.json`; ask the person to grant
  `PermissionNames(requested)`, keep the answer in the profile and call `Grant`; before each fetch `Blocked(url)` (skip it);
  `StylesFor(pageUrl)` parsed and appended to the tab's sheets (and lay out again after `Press` returns `StyleOn` / `StyleOff`);
  draw `ToolbarButtons()` and `PageActionsFor(pageUrl)` and call `Press`, opening `PluginEffect::url` for `OpenUrl`;
  append `NewTabHtml()` to the new-tab page; `browserplugins::SyncCommands(plugins, registry, ...)` after every load, grant or
  switch, so the buttons are commands of the browser's `pluginreg::PluginRegistry`.
## Round 3: fetch, cookies, storage, history, input events

What a page's scripts can do now that they could not in round 2 (all in `okay/Browser/Script.hpp`, `Browser.hpp`, and two new
stores, `Cookies.hpp` and `Storage.hpp`):

- **fetch() and XMLHttpRequest** go through the tab: `BrowserTab::TakeScriptRequests` / `DeliverScriptResponse`, same rules
  as a `<script src>` (in the launcher only the page's own site). fetch is Promise-based (`Response` with status, ok, headers,
  text(), json(), clone(); `Headers`); XMLHttpRequest has readyState 1-4, onload / onerror / onreadystatechange and
  addEventListener, getResponseHeader, responseType "json". Both are written in JavaScript (`kPrelude` in Script.cpp) on one
  native request function. In the browser program a request to another site carries no cookies and an `Origin` header; its
  answer is read only when the site allows it (`Access-Control-Allow-Origin`); a same-site request redirected to another site
  is checked the same way. Only "simple" requests go to another site (a CORS preflight is not done yet: those are refused).
  Set-Cookie headers are stored and never shown to the page. Limits: 64 waiting requests, 16 MB request body, 16 MB answer.
  Synchronous XMLHttpRequest is refused (InvalidAccessError).
- **Cookies** (`CookieStore`, one per profile, `BrowserTab::SetCookieStore`): RFC 6265 dates, Domain (no public suffix
  list yet: a short built-in list of shared suffixes like co.uk is refused), Path, Expires / Max-Age (at most 400 days),
  Secure, HttpOnly, SameSite (no attribute = Lax; None needs Secure; a cross-site top-level GET gets Lax ones), `__Secure-` /
  `__Host-` names. Sent on the page's fetches by the tab; for the host's own fetches it asks `CookieHeader(url)` and hands back
  `ReceiveCookies(url, setCookieValues)`. `document.cookie` reads and writes the same store without HttpOnly ones. Saved as
  JSON (`ToJson` / `FromJson`, persistent cookies only). Caps: 4096 bytes a cookie, 50 per domain, 3000 in all.
  The engine's other cookie jar (`ownhttp::CookieJar` in `Platform/OwnHttp.hpp`) is a strict per-call jar for one request
  and its redirects, with no disk and no Domain cookies; it was left as it is (a different job, another system's file).
- **localStorage and sessionStorage** (`WebStorage`, per origin; localStorage per profile via `SetLocalStorage`, saved with
  `ToJson`; sessionStorage per tab). 5 MB per origin (QuotaExceededError past it), at most 1000 origins. Property access
  (`localStorage.x = 1`) works; `delete localStorage.x` does not (use removeItem). No `storage` event to other tabs yet.
- **History**: pushState / replaceState (same origin only, state as JSON, 640 KB), back / forward / go, length, state.
  Back and forward between one page's own entries do not load anything; the page gets popstate. Leaving the page and coming
  back loads the entry's address again (no page cache), with its state.
- **Input events**: `BrowserTab::KeyEvent` (keydown / keyup / keypress with key, code, keyCode, modifiers; cancelling one
  tells the host not to type it), `PointerEvent` (mousedown / mouseup / mousemove ... with clientX / Y, button), `SetHover`
  fires mouseover / mouseout, `SetFocus` fires blur, focusout, focus, focusin; `el.focus()` / `el.blur()` move the tab's
  focus and `document.activeElement` follows. No mouseenter / mouseleave, no wheel, no pointer events yet.
- **Scripts while the page is read**: the page is still read in one go, then its scripts run in order, but
  `document.write` from a page script puts its text right after that script (so it shows where it would), and scripts in it
  run next. `defer` scripts run after the others and before DOMContentLoaded; `async` ones when they arrive (load waits for
  them). A script added by a script runs (inline at once, src when it arrives, with load / error on the element); a
  `<style>` or style link a script adds, removes or changes is applied at the next layout. `document.currentScript` works.
  Difference from a browser: a script sees the whole page, also the part after it. `document.write` after the page was read
  is ignored with a console line (a browser would wipe the page).
- **Live collections**: children, childNodes, getElementsByTagName / ClassName follow the tree (item, namedItem, length,
  index, for...of, forEach); querySelectorAll stays a fixed list, as in the standard. **dataset** writes set data-*
  attributes. **getComputedStyle** gives the cascade's values (display, colours, font, sizes, margins, padding, position ...;
  widths are the computed ones, not the laid-out pixels).
- **alert / confirm / prompt** become dialogs the host shows (`TakeDialog`). The script does not stop for them: confirm
  answers false and prompt null at once (what the standard returns when the browser does not show a modal dialog). At most 3
  dialogs per script run and 8 waiting; the rest go to the console.

**What the host calls (done in round 3 of the host, `claude/browser-host-3`):** written from RFC 6265 (5.4: each request carries
the cookies of its own address, so the host follows redirects itself, one hop at a time; RFC 9110 15.4 for the method a redirect
keeps), the Fetch standard (a request is sent with the method, headers and body the page gave), UI Events (keyboard and mouse
events, their key / code values, keyCode and buttons) and the HTML standard (8.4 "simple dialogs"). No code from any browser.
- **One cookie store and one localStorage per profile** (`SiteData` in `Browser/Profile.hpp`): `cookies.json` and
  `local_storage.json` in the profile's folder (`PrefPath("OkaySpace", "Browser")`), written to a temporary file and renamed
  (`WriteFileReplacing`, shared with profile.json). Every tab, new tabs included, gets both (`TabHost::SetSiteData`, set again
  when the tab is made anew). They are saved about once a second after `TakeChanged()`. **The program and the launcher at once:**
  like profile.json (`SharedFile` remembers size, time and hash), a file the other program replaced is read first; with nothing
  unsaved here it replaces this program's data (its session cookies stay), with unsaved changes this program keeps its own and
  adds what only the file has, then writes. Nothing one program set is lost; a cookie or key removed in one program at the same
  moment as the other saved can come back (the safe default, written down here).
- **Cookies on every load:** each page, image, style, script and font request gets `Cookie: <tab.CookieHeader(url)>`; the
  answer's Set-Cookie headers (also those of each redirect hop) go to `tab.ReceiveCookies(url, values)`. Both HTTP clients give
  the raw head (`ExResult::rawHead`); `followRedirects = false` now works on our own client too (it was WinHTTP only), and
  `callerCookies` turns WinHTTP's own per-session cookies off for these requests, so only our store decides. Cookie values and
  bodies are never logged. A subresource or fetch of an https page is never redirected to plain http.
- **The page's fetch() / XMLHttpRequest:** `TakeScriptRequests()` each frame; each is sent as given on the tab host's worker
  threads (now up to 4 per tab, so a slow fetch does not hold up the page) and answered with `DeliverScriptResponse` (status,
  reason, every header, body, final address). A redirect hop to another site carries no cookies. Request ids are now the tab's
  own (they were the page's, so an old page's late answer could reach the next page's request with the same number).
- **Keys and mouse:** the view (`BrowserView.cpp`) sends keydown (and keypress for a key that makes a character), keyup,
  mousedown / mouseup for each button and mousemove to the page before it uses them. A cancelled key is not typed into the
  field (the typed text is dropped and the field is read-only for that frame) and does not scroll or move the focus; a
  cancelled mousedown starts no text selection and gives no field the keyboard. The browser's own shortcuts
  (`IsBrowserShortcut`: Ctrl+T, W, L, Tab, 1-9, Alt+Left ...) are never sent to the page, and they are handled before the
  page sees anything, so a page cannot stop them.
- **Dialogs:** `TakeDialog` each frame for the shown tab; shown over the page in the browser's dialog style as "<site> says",
  with OK (Enter / Esc). A confirm / prompt says the page went on without the answer (the script never waits, see above).
- **Back / Forward history list:** a right click on Back or Forward, or a press held half a second, lists that side's entries
  (nearest first, at most 16; the page's title, else the history's title, else the address); a click goes there with
  `BrowserTab::GoTo(index)` (`HistoryEntries`, `HistoryIndex`): a pushState entry of the shown page gets popstate, any other
  is loaded.
- **The wheel glides** like the key scrolls (100 px a notch, over a few frames; a scrolling area under the pointer still
  takes the wheel first).

**Checked (2026-11-26):** `browser_script` (279 checks) and the other browser tests pass on Linux. Nothing was run in the
program or the launcher (the host wiring is not done) or on Windows.

## The launcher's Browser page (2026-11-26)

- **One browser, two hosts.** Everything the program drew (tabs, toolbar and address bar, bookmarks bar, menus, find, zoom, the
  download prompt and shelf, settings, our own pages, Play for games, the shortcuts) moved out of `browser/main.cpp` into
  `shared/BrowserApp.cpp` (`BrowserApp::Frame(x, y, w, h)`), unchanged apart from drawing inside a rectangle. The program
  draws it over its window (Quit and Ctrl+Q only there); the launcher draws it in the page area of a new sidebar entry,
  MENU > Browser (after Downloads, the globe icon, page id 18, remembered like the other pages). Nothing is copied.
- **One profile.** The launcher uses the program's folder (`PrefPath("OkaySpace", "Browser")`) and its `profile.json`, so history,
  bookmarks, zoom and settings are the same in both, also with both open at once: `ProfileStore` keeps each change until it is
  saved, looks at the file once a second and reads it again when the other program replaced it (size or time changed, and the
  text differs), then applies its own unsaved changes on top; Save does the same before it writes. Each writer writes its own
  temporary file and renames it over the old one. Two saves within the same few milliseconds can still race (the later rename
  wins; both see the result at their next look). Zoom and the bookmark star are kept as the value they lead to, so they apply
  the same on a re-read profile.
- **Links from chat.** With Settings > Experimental > Browser on (the default; it needs the OkayUI screens), a web link's "Open"
  opens a new tab of the Browser page and shows the page, for our own server's pages and other sites alike (step 1's overlay
  for our own pages is gone). Off, without OkayUI, or for an address that is not http(s): the system browser, as before.
  `okl::RouteLink` is the rule (tested in `browser_url`).
- **Responsive.** Pages, images, downloads and game packs are fetched and unpacked on worker threads (`TabHost`, `DownloadHost`,
  okay::thr); the launcher's frame only hands results over and draws. The browser is pumped every frame, whichever launcher page
  is shown, so a download or a game keeps going on another page.
- **Keys.** On the Browser page the launcher's own 1..9 page keys are off (the browser has Ctrl+1..9 for its tabs) and Ctrl+Shift+B
  is the bookmarks bar, not the sidebar fold.
- **Checked (2026-11-26):** `okayui_browser_app` (the whole browser at the launcher page's offset, golden picture, keys, the
  profile seen by a second browser, the download prompt, the switch off), `browser_url` (where links open), `browser_profile`
  (two programs on one profile file), the launcher_* tests and own_switches, on Linux. The launcher was run under a virtual
  display (Xvfb) with the OkayUI screens on, against a local test server: the Browser entry in the sidebar, four tabs, a click on
  another tab, the menu, find with highlights. The program was run again after the move: pages, Ctrl+Q and SIGTERM end it
  cleanly (the frame loop now ends before the painter; before, the program aborted at exit with "pure virtual method called").
  Not run: a chat link clicked in the launcher (the routing rule is tested, the click path was not driven), a download or a
  game from the launcher's page, two programs writing the profile at the same moment. Nothing was run on Windows.


## Round 2 of the look: like a regular browser (2026-11-26)

Chrome and Edge were the reference for how it looks and behaves (watched, not copied: no code, no pictures). The icons are
drawn with lines by our own code. What changed (`shared/BrowserApp.cpp`, `okayui/screens/BrowserView.cpp`, the model in
`okay/Browser/Profile.hpp`):

- **Tab strip.** Rounded tabs; the shown tab and the toolbar are one surface; a
  site badge on each tab (a round badge with the site's first letter in a colour of its own, a globe for our own pages, a
  warning sign for a page that failed, a turning arc while loading); the close button on the shown tab, the tab under the
  pointer, and every tab wide enough; thin lines between tabs; a round + button; tabs shrink as more open (a narrow tab shows
  only its badge); a tooltip with the whole title and the host after the pointer rests half a second; dragging a tab moves it
  (`TabOrder::Move`).
- **Toolbar.** Round icon buttons (Back, Forward, Reload / Stop, Home, the menu) with hover and pressed circles and tooltips; a
  rounded address bar with a lock for https, "Not secure" for http and a sign for our own pages (`BadgeOf`); while the person is
  not typing in it, the address is shown with the host bright and the rest dim, without "https://" (`ShowAddress`); the
  bookmark star inside the bar's right end.
- **Suggestions while typing** (`SuggestAddresses`): the first row is what Enter does (go to the address, or search Google),
  then open tabs ("Switch to this tab"), bookmarks and history whose title or address holds every typed word; a match at the
  start of the host ranks first, then the start of a word, then anywhere; pages visited more often first. Up / Down move the
  highlight, Enter opens it, Esc closes the list, a click picks a row.
- **Status bubble.** The pointer over a link (or the link Tab focused) shows its full address, resolved against the page, in a
  rounded bubble at the bottom-left.
- **Keyboard on the page** (`PageKeys`, while no field has the keyboard): Space / Shift+Space and Page Down / Page Up scroll by
  7/8 of the view, the arrow keys by 40 px, Home / End to the top and the bottom; these glide over a few frames. Tab /
  Shift+Tab move the focus over the links and form fields in reading order, with a blue ring, and bring it into view; Enter
  follows the focused link; Space or Enter works a focused checkbox, radio button, list or button; Tab out of a text field
  goes on to the next.
- **Text selection.** Dragging over the page's text selects it (drawn under a see-through blue); Ctrl+C copies it, Ctrl+A
  selects all the page's text, the context menu's Copy copies it. Lines are joined with a new line. Text inside scrolling areas
  and fixed boxes is not selectable yet.
- **Error pages** (`DescribeLoadError`): "This site cannot be reached", "This page was not found" (404 / 410), "The site has a
  problem" (5xx), "This page is not open to you" (401 / 403), "This address cannot be opened here", "too large", "Loading was
  stopped", each with what to do, the host and the code in small print, a Try again button and Go back.
- **New-tab page.** A search box in the middle that sends to the search address (a GET form; a search address with other
  query parts keeps them as hidden fields; none when no search is set), and up to 8 shortcut tiles (bookmarks first, then the
  pages visited most often), and small links to History, Bookmarks and Downloads.

**Tests:** `browser_profile` (suggestions, badge, address shown, error texts, the new-tab search box and tiles),
`okayui_browser` (text selection by dragging, Ctrl+C, Ctrl+A, a link press selects nothing; golden `browser_select`),
`okayui_browser_app` (suggestions picked with Down + Enter switch tabs; End / Home / Space glide; Tab, Tab, Enter follows the
second link; the error page; dragging a tab to the front; goldens `browser_app`, `browser_app_suggest`, `browser_app_focus`,
`browser_app_error`).

**Not done in this round:** the list of earlier pages on a right click or long press on Back / Forward (it needs the tab to
give its history entries: `BrowserTab` has no call for that yet), smooth scrolling for the mouse wheel (only keys glide), a
page-wide UI scale setting (the browser draws at OkayUI's text size; 150% was not checked), double-click to select a word,
selecting text in scrolling areas, drag a tab out into a new window. Nothing was run on Windows.

## Real pages, round 1 (2026-11-26, branch `claude/browser-real-1`)

Checked against saved copies of real pages (Google, three Wikipedia articles, Hacker News, BBC, W3C, Python docs, MDN,
craigslist and others), rendered headless before and after; the copies stay outside the repository. What changed:

- **Character encodings** (`Encoding.hpp`, from the WHATWG Encoding standard and HTML 13.2.3): a BOM, then the server's
  Content-Type charset, then `<meta charset>` / `http-equiv` in the first 1024 bytes, then well-formed UTF-8, else windows-1252.
  Decoders: UTF-8, UTF-16LE/BE, x-user-defined, "replacement" and 27 single-byte encodings (ISO-8859-x, windows-125x, KOI8,
  IBM866, macintosh, windows-874, x-mac-cyrillic). Not yet: Shift_JIS, EUC-JP, ISO-2022-JP, GBK, gb18030, Big5, EUC-KR.
  `BrowserTab::DeliverPage` takes the Content-Type; the tab host does not pass it yet (follow-up in `BrowserHost.cpp`).
- **User-Agent**: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) OkaySpaceBrowser/1.0" (`BrowserApp.hpp`). With the old
  "OkaySpace Browser" Google answered `charset=ISO-8859-1`; now it answers UTF-8. No other browser is named.
- **Tables with percentage columns** (a "25% | content | 25%" row): the table's max-content width lets the percentages hold,
  and percentage columns give way so no column drops below its minimum (Google's "Advanced search" no longer overlaps).
- **Presentational hints**: `<font size color face>`, `align` on div / p / headings, `<body marginwidth ...>`, `<img align
  hspace vspace border>`, `<hr width size color noshade align>`; `<center>` and `align` move narrower blocks inside them
  (HTML 15.3.3 "align descendants").
- **sub / sup** move the text's baseline (vertical-align sub / super / length), through links inside them.
- **Quirks mode** (no or legacy DOCTYPE): tables start their fonts and text-align afresh (HTML 15.3.8).
- **Hidden things stay hidden**: a form control with opacity 0 is not drawn (its box still takes clicks); an element with a
  `mask-image: url()` is not drawn (mask images are not loaded, which CSS Masking treats as transparent).

**Tests:** `browser_html` (labels, decoders, the prescan, the sniffing order, a tab given ISO-8859-1 bytes, quirks from the
DOCTYPE), `browser_layout3` (the cases above, worked out by hand), `okayui_browser_pages` (four new pages and goldens:
`legacy_charset` stored in windows-1252, `search_home`, `encyclopedia_article`, `quirks_board`).

**Seen on the real pages, not fixed here (other lanes):** text is drawn about 15% smaller than its CSS size (`BrowserView`
scales by the font's ascent-to-descent height, not its em size); a text run with one glyph the launcher font lacks (≠, €, ™,
IPA) is not drawn at all in headless renders; icon fonts (Material Icons ligatures) show their names; images were not
loaded in these renders. Nothing was run on Windows.

## Site check: real sites, failures ranked (2026-11-26, branch `claude/site-check-1`)

Owner: "i want all sites to load properly". `okay-sitecheck` (`tools/sitecheck/`, feature map 17.20) loads the 61 public home
pages of `tools/sitecheck/sites.txt` through the browser program's own tab host, page engine and view (with main as of #1342 and
its User-Agent), one at a time, and ranks the causes by how many sites each hits. Run on Linux on 2026-11-26 (our own HTTP
client with the system OpenSSL, not WinHTTP as on Windows); 58 of 61 shown. Reports and screenshots stay outside the
repository. The order to fix things in, by sites hit:

- JavaScript language: syntax we do not read: `__proto__: value` in object literals (18), a construct with `in` that stops
  the parser ("unexpected 'in'", 17 + 16 sites; the exact construct is not found yet), tagged templates (3), and single sites
  with BigInt literals, regex lookbehind, `for await`, `static {}`, `import`. Missing globals: `Date` (14), `decodeURIComponent`
  (4), `encodeURIComponent`, `WeakMap`, `Intl` (2 each). Most of the 20 sites with "a global of the page is not defined"
  follow from an earlier script that failed to parse.
- DOM API: `navigator` (8), `URL` (4), `performance` (4), `Element`, `Image`, `MutationObserver`, `matchMedia` (2 each).
- Images: SVG (20), WebP (5). Fonts: WOFF2 (15; no Brotli yet) and WOFF (6; the decoder exists, the tab host does not call it).
- Layout: 18 screenshots are nearly one flat colour and 7 pages drew almost nothing, mostly pages built by their scripts
  (Twitch, Reddit, Pinterest, Dailymotion, Mastodon) or sites that answered 403.
- Network: 12 sites answered the home page with 403 (bot checks; the Guardian loaded with the earlier User-Agent), a TLS
  handshake failure with DuckDuckGo (2 runs of 3), a timeout with Best Buy, two scripts over the 16 MB limit (YouTube,
  Bluesky), fonts answered 404 (6).
- CSS: properties the cascade does not use, on most sites: `cursor` (35), `-webkit-text-size-adjust` (28), `transition` (26),
  `pointer-events` (24), `fill` (23), `-webkit-appearance` (22), `user-select` (20), `animation` (18); selectors with
  `::-webkit-scrollbar` (21) or `::-moz-focus-inner` (19) are dropped. Many of these change nothing visible.

## Round 4: the site check's list (2026-11-26, branch `claude/browser-round4`)

Driven by the site check's ranking above. What was done, what each part was written from, and what is left:

- **The host wirings that waited for #1336.** `TabHost` hands the page answer's Content-Type to `BrowserTab::DeliverPage` (its
  charset decides the page's encoding, Encoding.hpp); font answers go through `DecodeWebFont` (WOFF and WOFF2 become the
  TrueType / OpenType file the rasterizer reads); `BrowserView`'s `TextScaleFor` sizes text by the font's em (CSS Fonts 4
  font-size) instead of its line (ascent - descent): `FontEmOf` reads head.unitsPerEm and the hhea ascent / descent (OpenType
  spec), the launcher look's font gives about 1.17 lines per em, so text was about 15% too small, and the drawn baseline is put
  where the layout puts it (0.8 em). Web fonts carry their own em (`RegisterWebFont(..., FontEm)`).
- **DOM and web APIs** (`Script.cpp`, `InstallRound4` and the round-4 prelude; written from the WHATWG DOM, HTML, URL, Fetch
  standards, CSSOM View, Intersection Observer, Resize Observer, UI Events, High Resolution Time, User Timing, Geometry
  Interfaces, Cooperative Scheduling of Background Tasks and Web IDL): `navigator` (user agent = the host's, language,
  platform, onLine, cookieEnabled, hardwareConcurrency 4, sendBeacon; no serviceWorker, so pages take their plain path), `URL`
  and `URLSearchParams` (on `ParseWebUrl`, Url.cpp: the WHATWG basic URL parser reduced to what pages use, on top of the RFC 3986
  split; no IDNA, no IPv4 number forms), `performance` (now, timeOrigin, marks and measures; `PerformanceObserver` reports
  nothing), `matchMedia` with change events (the tab's media query evaluator), the interface objects (`Node`, `Element`,
  `HTMLElement`, the HTML element interfaces by tag, `SVGElement`, `Text`, `Comment`, `Document`; `instanceof` and
  `[object HTMLDivElement]` work; `new Image()`, `new Option()`, `new Audio()`, `new Text()`), `Event` / `CustomEvent` / the UI
  Events interfaces as constructors and `dispatchEvent` on nodes and window, `EventTarget` for a script's own objects,
  `MutationObserver` (childList, attributes, characterData, subtree, old values, attributeFilter, takeRecords; records are
  delivered in a microtask), `requestIdleCallback`, `getBoundingClientRect` / `getClientRects`, `offset*`, `client*` and the
  root's `scroll*` from the last layout (an inline element's box is the union of its runs), `scrollTo` / `scrollBy` /
  `scrollIntoView`, `innerWidth` and the other window sizes, `screen` (the view's size), resize and scroll events,
  `IntersectionObserver` (the root is the view or an element's box; clipping by scrolling boxes between is not looked at) and
  `ResizeObserver` (the content box is the border box less the borders), `before` / `after` / `replaceWith` /
  `replaceChildren` / `insertAdjacentHTML` / `insertAdjacentElement` / `insertAdjacentText` / `toggleAttribute` /
  `getAttributeNames` / `getRootNode`, `atob` / `btoa`, `CSS.escape`, `document.fonts` (nothing waits for fonts),
  `postMessage` to the page itself. Element boxes come from the last layout (`BrowserTab::BoxOf`, a map made once per layout):
  a script never forces a layout, so an element added in the same script has no box until the next frame.
- **Why pages were blank or one colour.** Ours, fixed: `</dd>` and `</dt>` popped every element up to `<html>` (the rest of the
  page landed after `<body>`: every definition list, MDN's menus); `grid-column: content` did not find the implicit
  `content-start` / `content-end` lines (MDN's text was placed off the right edge); a script whose compiling runs out of the
  page's heap ended the whole browser (it now stops with a console line); pages that fade their content in with a
  `forwards` animation stayed at opacity 0 (see below). Not ours or not this round, listed for the owners: the JavaScript
  language gaps (sloppy-mode assignment to an undeclared name such as React's `$RB=[]`, which keeps Pinterest's and
  Nintendo's streamed content hidden; `__proto__:` literals, the `in` construct, tagged templates, `Date`,
  `decodeURIComponent`, `Intl`, typed arrays, `ReadableStream`) for `claude/js-compat-1`; `customElements` and shadow DOM
  (archive.org, GOG); inline SVG drawing; sites that answer a bot check (Reddit's challenge page, the 403s).
- **CSS with a visible effect** (Css.cpp, Layout.cpp, BrowserView.cpp; CSS UI 4, CSS Animations 1, CSS Transitions 1):
  `pointer-events: none` (the box, its links and controls take no clicks or hover; the view looks under them), `cursor` (the view
  sets the mouse pointer; url() images are not loaded, the keyword after them is used), `user-select: none` (not selected, not
  copied), `appearance: none` and its prefixed forms (no native check mark, radio dot or list arrow), and the end state of
  animations: a finite animation that fills forwards shows its last keyframe (`@keyframes`, `@-webkit-keyframes`; the first
  keyframe when it runs in reverse). Transition and animation timing properties are read and need no value (the end state is
  shown at once).
- **Network.** Every request carries the standard set and nothing else: Accept by destination (Fetch 4.1), Accept-Language,
  Accept-Encoding `gzip, deflate, br` (Brotli through `okay::brotli`, handed to the HTTP client as `Limits::brotli`), Sec-Fetch-Dest /
  -Mode / -Site / -User (Fetch Metadata Request Headers), Upgrade-Insecure-Requests on page loads, Referer by the default
  policy strict-origin-when-cross-origin; no client hints, no other browser's names. The 12 sites that answer 403 still do from
  this machine: curl with and without the standard set got the same 403s, so their bot checks look at something other than
  these headers (unverified what; it was not probed further).
- **The site check before and after** (the same 61 sites, Linux, 2026-11-26; before = the site check's branch with main as of
  #1336, after = this branch): distinct causes 612 -> 548; sites with a DOM API cause 26 -> 3 (left: `customElements` on 2,
  `Blob` on 1); with a fonts cause 18 -> 1 (one WOFF2 file our decoder refuses); the CSS causes `cursor` (35 sites),
  `transition` (26), `pointer-events` (25), `-webkit-appearance` (22), `appearance` (19), `user-select` (20), `animation` (17)
  are gone from the list; "nearly one flat colour" 18 -> 16 (MDN now draws its page; Google is plain white by design); "drew
  almost nothing" stays at 7; HTTP 403 stays at 12. Sites with a JavaScript-language cause went up, 40 -> 49: scripts that
  used to stop at a missing DOM API now run on and stop at a language gap instead. The top of the list is now
  `-webkit-text-size-adjust` (30, no effect on a desktop browser), `fill` (24, SVG), the JavaScript-language gaps above,
  `::-webkit-scrollbar` (21) and SVG images (20).
