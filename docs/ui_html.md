# HTML / CSS UI (UIDocument)

A **UIDocument** can be authored two ways: the line-based **OkayUI** markup, or a
small **HTML + CSS** subset. Both build the *same* native widgets, so an HTML menu
renders identically in the editor, the player, and the web/mobile builds — there is
**no browser embedded**. Use whichever you prefer; the document auto-detects HTML
when its first non-space character is `<`.

## Quick example

```html
<style>
  .card   { background: #1e2433; border-radius: 8px; }
  button  { background: rgb(60,90,150); color: #ffffff; border-radius: 6px; }
  #title  { color: yellow; font-size: 5; }
</style>

<div class="card" style="left:40; top:40; width:360; height:260;">
  <h1 id="title" style="left:30; top:24;">MY GAME</h1>
  <button style="left:30; top:120; width:300; height:60;"
          onclick="load_scene('level1')">Play</button>
  <button style="left:30; top:190; width:300; height:60;"
          onclick="quit()">Quit</button>
</div>
```

## Tags → widgets

| HTML | Widget |
|------|--------|
| `<div>` `<panel>` `<section>` `<form>` `<ul>` `<nav>` | panel (container) |
| `<button>` `<a>` | button |
| `<p>` `<span>` `<h1>`–`<h6>` `<label>` `<li>` `<text>` | text |
| `<img src="...">` | image |
| `<input>` | input (or `type="range"` → slider, `type="checkbox"` → toggle) |
| `<select><option>…</select>` | dropdown |
| `<progress value="0.5">` | progress bar |

Only `<div>`-family elements nest children as widgets; text inside a `<button>`/`<p>`
becomes its label.

## CSS support

Selectors: **tag** (`button`), **class** (`.card`), **id** (`#title`), and inline
`style="..."` (highest priority). Supported properties map onto widget fields:

| CSS | Effect |
|-----|--------|
| `background` / `background-color` | fill color |
| `color` | text color |
| `width` / `height` | size (px or `%` of canvas) |
| `left` / `top` | position (px or `%`) |
| `border-radius` | rounded corners |
| `border` / `border-width`, `border-color` | outline |
| `font-size` | text/label size |
| `text-align` | left / center / right |

Colors accept `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()/rgba()`, and a few names
(white, black, red, green, blue, yellow, gray, orange, transparent). Lengths accept
`12px` or `50%`.

## Behavior (the "JS")

`onclick`, `onchange`, `ontoggle`, `onsubmit` run **OkayScript** — the same engine
script API used everywhere (`load_scene`, `prefs_set`, `ui_set_text`, `set_active`,
`send_message`, calling your own functions, …). Because attributes are double-quoted,
use single quotes (or `&quot;`) for string arguments: `onclick="load_scene('game')"`.

Attributes: `id` names the widget (addressable by the `ui_*` script API and CSS
`#id`), `class` applies styles, `placeholder`, `value`, `min`/`max`, `checked`,
`src`, `tooltip`/`title` map to the matching widget fields.

## Notes / limits

- It's a pragmatic subset for game menus/HUDs, not a web browser — no flexbox/grid
  auto-layout (position with `left`/`top`/`width`/`height`), no media queries, no
  external CSS/JS files. For flow layout, nest under an OkayUI `layout` group.
- On the **web (WASM) build only**, if you need 100% real HTML/CSS/JS you can also
  overlay a normal DOM on top of the game canvas — the browser is already there.
