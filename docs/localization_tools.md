# Localization tools

The tools a translation workflow needs on top of the string tables in
[localization.md](localization.md): a grid of keys by languages with a state for
every cell, CSV and JSON import and export, a checker for broken placeholders and
plurals, a scanner that finds keys the project uses or forgets, language previews
with pseudo-localization, and a report of characters no font draws.

All of it is plain C++ over a `StringTable` (`#include <Okay.hpp>`, or the
headers named below). Nothing here changes the running game: a tool reads a table,
shows a result, and the Localization service (current language, listeners, "missing
key" log) is left alone unless you hand it a table yourself.

| Header | What it has |
|---|---|
| `okay/Core/LocTable.hpp` | `LocTable`: load a folder, the grid, CSV and JSON |
| `okay/Core/LocCheck.hpp` | `LocCheck`, `LocMissingReport`, message parsing, `LocFontCoverage` |
| `okay/Core/LocScan.hpp` | `LocScanProject`: used, unused and undefined keys |
| `okay/Core/LocPreview.hpp` | `LocPreview`, `LocPseudoText`, mirrored right-to-left text |

## Loading and the grid

```cpp
okay::LocTable lt;
std::string err;
lt.LoadFolder("Assets/Locales", &err);        // every .okayloc, .csv and .json in the folder
lt.sourceLanguage = "en";                      // "" = the table's default language

okay::LocGrid grid = lt.Grid();                // rows = keys (sorted), columns = languages
for (int r = 0; r < grid.RowCount(); ++r)
    for (int c = 0; c < grid.ColCount(); ++c) {
        const okay::LocCell& cell = grid.Cell(r, c);   // cell.text, cell.state
    }
```

`lt.table` is the underlying `StringTable`: edit it directly (`Set`, `Add`,
`Rename`) and ask for a new grid.

**Cell states**

| State | Means |
|---|---|
| Translated | has text that differs from the source's (or is the source language's text) |
| Missing | no text |
| SameAsSource | identical to the source text, and the text has letters: probably untranslated. A text without letters (`100`, `{0}%`) can't be untranslated; keys in `lt.sameAsSourceOk` ("OK", names) are exempt |
| PlaceholderMismatch | its `{0}` / `{name}` set differs from the source text's |
| Extra | has text where the source language has none, or belongs to a language code the table doesn't list (`lt.ExtraTexts()`) |

`LocGridOptions` narrows the grid: `languages` (columns), `search` (case-insensitive,
over keys, notes and shown texts), `keyPrefix`, `stateMask` (keep rows with a cell
in those states, built with `LocStateBit`), `sortByIssues` (worst rows first).
`lt.Stats("fr")` counts the states of one language and gives its completion;
`lt.AllStats()` does every language. `lt.PluralGroups()` lists keys that spell a
plural as `coins.one`, `coins.other`.

### Loading a folder

`LoadFolder` reads the `.okayloc`, `.csv` and `.json` files directly in a folder, in
file name order (not its subfolders), and merges them: languages and keys are
added, non-empty texts overwrite earlier ones. A file that fails to read is
reported in `lt.warnings` and skipped; the call fails only when nothing could be
read. A `.json` file of one language (`fr.json`: `{"menu.start": "Commencer"}`) is
that language by its file name. `lt.files` lists what was read.

## CSV

```cpp
std::string csv = lt.ToCsv();                  // key,note,en,fr,ja ...

okay::LocImportOptions o;
o.dryRun = true;                               // work out the diff, change nothing
okay::LocImportResult r = lt.ImportCsv(sheetFromTranslator, o);
// r.ok, r.error, r.rows, r.warnings
// r.diff: addedKeys, addedLanguages, filled, changed, cleared (key, language pairs)
```

The format is RFC 4180: a field with a comma, semicolon, tab, quote or line break
(or space at either end) is quoted, a quote inside is doubled, rows end with CRLF,
and the file starts with a UTF-8 byte order mark so spreadsheet programs keep
accents. Reading is lenient: a mark or none, CRLF, LF or CR rows, comma, semicolon or
tab separated (detected from the header), quoted line breaks, and UTF-16 text (a
spreadsheet's "Unicode text") is converted. A cell's CRLF reads as a plain line
break. The header row names the columns: `key` (or `id`), an optional `note`
(`notes`, `comment`, `context`), then one column per language code.

Plural forms need nothing special: `coins.one` and `coins.other` are ordinary keys
and survive every round trip, as do plural blocks inside a text
(`{0, plural, one {# coin} other {# coins}}`).

`LocCsvOptions` shapes an export: `languages`, `note`, `bom`, `crlf`, `delimiter`
(`,` `;` or tab) and `missingIn` (only the keys with no text in that language: a
hand-off sheet for one translator).

`LocImportOptions`: `mode` is `Overwrite` (non-empty cells replace, empty cells keep
the table's text; the default), `FillEmpty` (only cells the table has no text for)
or `Replace` (every cell is written, an empty one clears the text); `addKeys` and
`addLanguages` can be turned off to refuse new keys or languages. Repeated keys,
rows without a key, short rows and repeated columns come back in `r.warnings`.

## JSON

`lt.ToJson()` writes the whole table:

```json
{
  "okayloc": 1,
  "default": "en",
  "languages": [ { "code": "en", "name": "English" }, { "code": "ja", "name": "日本語", "font": "Fonts/Noto.ttf" } ],
  "keys": {
    "menu.start": { "note": "The first button", "text": { "en": "Start", "ja": "スタート" } },
    "coins.one": { "text": { "en": "{0} coin" } }
  }
}
```

`lt.ToJsonFlat("fr")` writes one language as `{"key": "text"}`. `lt.ImportJson(json, options)`
reads either form. A flat file, or nested objects (`{"menu": {"start": "Start"}}`
reads as `menu.start`, `{"coins": {"one": "..."}}` as `coins.one`), is one language:
set `options.language`. Numbers and booleans read as their text; anything else is
skipped with a warning. `\u` escapes, including surrogate pairs, are decoded.

`SaveCsv`, `SaveJson` and `SaveOkayloc` write files (the first two write aside and
swap the file in, so a crash leaves the old one).

## The checker

```cpp
okay::LocCheckOptions o;
o.sameAsSource = true;
okay::LocReport rep = okay::LocCheck(lt.table, o);
std::puts(rep.ToText().c_str());     // "error  fr  hud.score  {0} is missing; {1} isn't in the en text"
rep.ToCsv();                         // severity,kind,language,key,message
```

| Issue | Severity | When |
|---|---|---|
| MissingText | warning | a key has no text in a language |
| PlaceholderMismatch | error | the set of `{0}` / `{name}` names differs from the source text's (plural blocks and their nested placeholders count) |
| UnbalancedBraces | error | a `{` without `}` or a `}` without `{` (write `{{` and `}}` for literal braces) |
| PluralFormMissing | error | a `{n, plural, ...}` block lacks a form the language needs (`other` always) |
| PluralKeyMissing | warning | a `coins.one` / `coins.other` group has no key for a form the language needs: add `coins.few` |
| TrailingWhitespace | warning | leading or trailing whitespace differs from the source text |
| LengthGrowth | warning | wider than the source by more than `growthWarn` (default 1.5) and by more than `growthSlack` (6) cells |
| SameAsSource | info | identical to the source text (off unless `sameAsSource`) |
| ExtraText | info | text for a key the source has no text for |

Width is in character cells: wide (CJK, full width) characters count 2, combining
marks 0, an argument 3, a plural block its longest form, a multi-line text its
widest line. The slack keeps short labels from being flagged ("OK" to "D'accord").

**Plural forms a language needs**: `ja zh ko vi th id ms km lo my` just `other`;
`ru uk be pl cs sk lt` `one few many other`; `sl` `one two few other`;
`hr sr bs ro` `one few other`; `lv` `zero one other`; `he` `one two other`;
`ga` `one two few many other`; `ar cy` all six; everything else `one other`. Any
form the installed `Localization::SetPluralRule` for the language produces is
needed too. `LocRequiredPlurals("ru")` gives the list.

`LocMissingReport(table)` lists, per language, the keys with no text and the
completion; `LocMissingToText` formats it. `LocParseMessage`, `LocPlaceholders`,
`LocPluralSelectors` and `LocMessageWidth` are the building blocks, usable on
their own.

## Which keys the project uses

```cpp
okay::LocScanResult s = okay::LocScanProject("MyGame", lt.table);
s.unused;       // table keys nothing mentions
s.undefined;    // mentions of keys the table lacks (key, file, line)
```

`LocScanProject(root, table, options)` reads the files under `root` (not folders
named `build`, `bin`, `obj`, `Library`, `Temp`, `node_modules`, `dist` or starting
with a dot; files over 8 MB are skipped) and collects:

| File | Mention |
|---|---|
| `.okayscene`, `.okayprefab` | the `loc` record of an object: `loc text="menu.title" button="menu.start"` (Text, Button, Toggle, Tooltip, World UI, Input Field), plus calls in script text inside the file |
| `.okayvs` | `node 5 Localize menu.title` |
| `.okay`, `.cs`, `.lua`, `.okayui`, `.cpp`, `.hpp`, `.h` | `loc("key")`, `Loc("key")`, `T("key")`, `tr("key")`, `Localize("key", ...)`, `Localization.Get("key")` / `Localization::Get`, `GetOr`, `TryGet`; single or double quotes |
| `.okaydialog`, `.okayquest` | the keys the engine derives (`dlg.<graph>.<node>`, `quest.<id>.title`...), marked optional: a table without them is fine, the text typed in the asset shows |

`callNames` in `LocScanOptions` changes the function names (a name with a dot must
be qualified at the call). A key built from a literal and more, `loc("shop." + item)`,
marks every key starting with `shop.` as used instead of reporting anything. A call
on a plural base (`loc("coins", n)`) uses `coins.one`, `coins.other` and the rest.
Keys starting with `pause.`, `hold.`, `loading.`, `dialogue.`, `quest.`, `dlg.` or
`speaker.` are read by built-in components and never reported unused
(`engineKeyPrefixes`). `atKeys = true` also reads `"@menu.title"` strings (the
engine doesn't resolve these itself; off by default). `LocScanFiles` scans
(path, text) pairs you hand it.

The scan matches text, not a parse: a call in a comment counts as a use, and a key
passed through a variable can't be seen. Treat "unused" as a list to review.

## Previews and pseudo-localization

```cpp
okay::LocPreview preview(lt.table);       // reads the table, never changes it
preview.SetLanguage("fr");                // "" = the default; Localization::kPseudo works too
preview.SetPseudo(true);
std::string s = preview.Localize("hud.score", 120);   // "Score: {0}" -> "[!!! Šçöŕé: 120~~~ !!!]"
```

A preview applies a language and pseudo-localization to what it returns and nothing
else: no language switch, no listener fires, no "missing key" is logged, nothing is
saved. Fallbacks are the runtime's: the shown language, then the default, then the key.

Pseudo-localization (`LocPseudoText`) turns `Start` into `[!!! Šţåŕţ~~ !!!]`: letters
become accented look-alikes (text drawn from the wrong font shows), `~` adds
`expand` (default 30%) more length (text that doesn't fit shows), the `[!!! ... !!!]`
markers show text that never went through the table (it stays plain). `{0}` and
`{name}` placeholders and plural syntax keep working; words inside a plural form are
accented. `LocPseudoOptions` turns the accents, markers, expansion and mirroring on
and off.

Mirrored text (`LocMirrorText`, or `mirror = true`) reverses each line's characters
(combining marks stay with their letter, brackets swap sides, `{...}` placeholders
stay whole): how right-to-left text looks when a left-to-right renderer that
doesn't shape or reorder draws it. A preview in an Arabic, Hebrew, Persian or Urdu
language mirrors by default (`SetMirrorRtl(false)` shows the stored order).
`LocIsRtl(code)` tells which languages those are.

`preview.Bake()` returns a one-language copy of the table with the preview's texts.
`Localization::SetTable(preview.Bake())` shows the preview in a running game; the
project's own table is untouched, and the editor's **Pseudo-localize** keeps working
as before.

## Font coverage

```cpp
okay::LocFontReport fonts = okay::LocFontCoverage(lt.table);
std::puts(fonts.ToText().c_str());
```

For each language, the report goes through every visible character of its texts
(not whitespace, controls or zero-width marks) and asks `TtfFont::Has` of a chain
of fonts: the language's own font from the table (a relative path is looked up as
written, then next to `options.baseDir`), the built-in UI font, then the installed
system fallback fonts (`SystemFallbackFonts()`), the same chain
`FontForText` draws with. It lists:

* `missing`: characters no font in the chain draws, with a count and the first key using each,
* `fallbacks`: characters the first font lacks but a later one draws (the engine switches font for those texts),
* `undrawableTexts`: keys whose text no single font draws completely (the engine draws a text with one font),
* `warnings`: a language font that couldn't be loaded.

`LocFontOptions::fonts` replaces the chain with fonts you pick; `useSystemFonts =
false` leaves the installed fonts out, which gives the same answer on every machine
(useful in tests and CI: installed fonts differ).

## In the Localization window

**View > Editors & Tools > Localization** calls these tools (see
[localization.md](localization.md)):

* the grid is a `LocTable` grid: search, cell states and the state filters,
* the line under the filter is `LocCheck` plus `LocMissingReport`; **Show report**
  lists missing translations and placeholder mismatches, and a click selects the key,
* **Scan project** is `LocScanProject` (plus the open scene) for unused and undefined keys,
* the selected key's preview line is `LocPreview`: pseudo-localization, and mirrored
  text for a right-to-left language,
* CSV import still uses the string table's importer (so a column headed `FR` fills
  `fr`) and then `LocDiffTables`, and the status line says what changed.

Right-to-left text is still drawn left to right in the Scene view, unshaped; the
mirrored preview only shows how a layout would behave. JSON import and export, a
confirm step before applying a CSV, and the font coverage report are not in the
window yet.
