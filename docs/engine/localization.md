# Localization

> **What this is:** The string table, the localization tools, and the formats / typed Prefs / fixed timestep record.
> **Status:** combined on 2026-11-20 from 3 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Localization](#localization-1) (was `docs/localization.md`)
- [Localization tools](#localization-tools) (was `docs/localization_tools.md`)
- [Localization formats, typed Prefs, fixed timestep](#localization-formats-typed-prefs-fixed-timestep) (was `docs/localization_prefs_time.md`)

## Localization

*Was `docs/localization.md` until 2026-11-20.*

A project keeps every piece of text players read in one string table,
`strings.okayloc` at the project root. Each entry has a key (`menu.start`), an
optional note for translators, and a text per language. Texts in the scene, the
pause menu, the hold prompt, the loading screen, dialogue and quests show the
text of their key in the current language, and change the moment the language
does. Scripts and visual scripts read keys too.

The language a game starts in is the one the player picked last time, else the
operating system's language when the table has it, else the table's default.

### The string table

`strings.okayloc` is UTF-8 text:

```
okayloc 1
# Lines starting with # are comments.

language en "English" default
language fr "Français"
language ja "日本語" font "Assets/Fonts/NotoSansJP-Regular.ttf"

key menu.start "The main menu's first button"
key shop.coins

text en menu.start "Start"
text fr menu.start "Commencer"
text ja menu.start "スタート"
text en shop.coins "{0, plural, one {# coin} other {# coins}}"
text fr shop.coins "{0, plural, one {# pièce} other {# pièces}}"
```

- `language <code> "<name>"` adds a language. `default` marks the language
  shown for keys a language has no text for. `font "<path>"` names a font with
  the language's characters (see [Fonts](#fonts)); a relative path is relative
  to the table.
- `key <key> ["note"]` adds a key with an optional note for translators.
- `text <language> <key> "<text>"` is one translation. In quoted text `\"` is a
  quote, `\\` a backslash and `\n` a line break; a quoted text may also run over
  several lines.

Keys are any run of letters, digits, `.`, `_` and `-` (quote a key that has
anything else). Records the engine doesn't know are skipped, so newer tables
still load. Windows line endings and a byte order mark are fine.

#### What players see when a text is missing

`Localization::Get("key")` returns the key's text in the current language. When
that language has no text, it shows the default language's text; when that is
missing too, it shows the key itself. Each miss is logged once (per language and
key), as a warning: `Localization: "menu.quit" has no fr text; showing the en one`.

### Arguments and plurals

Placeholders are filled from the arguments a text is asked for with:

- `{0}`, `{1}`... are the arguments in order: `loc("score", 120)` with
  `"Score: {0}"` gives `Score: 120`.
- `{name}` is a named argument: in OkayScript pass a map,
  `loc("greet", map_set(map(), "player", "Ana"))` with `"Hello, {player}!"`.
  In C++, `LocArg::Named("player", "Ana")`.
- `{0, plural, one {...} other {...}}` (or `{count, plural, ...}` with a named
  argument) picks a form by the argument's number: `#` inside it is the number.
  `=0 {...}`, `=1 {...}` match exact numbers first. The forms are `zero`, `one`,
  `two`, `few`, `many` and `other`.
- `{{` and `}}` are literal braces. A placeholder without an argument stays as
  written, so a missing argument is easy to spot.
- `{0, number}` writes a number the language's way: `1,234,567` in English,
  `1.234.567` in German, `1 234 567` (narrow no-break spaces) in French, with up to
  three decimals (`{0, number, integer}`, `{0, number, percent}` and a pattern such as
  `{0, number, #,##0.00}` choose another style). `{0, currency, USD}` writes money
  (`$1,234.50`, `1.234,50 €`).
- `{who, select, male {He} female {She} other {They}}` picks a form by the argument's
  text (the `other` form when nothing matches), for words that change with a name or a
  gender. Forms may hold other placeholders.

Built-in plural rules: English and most languages use `one` for exactly 1;
French uses `one` for 0 and 1; Japanese, Chinese and Korean always use `other`;
Russian, Ukrainian, Polish, Czech, Slovak, Arabic, Croatian, Serbian, Bosnian,
Slovenian, Lithuanian, Latvian, Romanian, Hebrew, Hindi, Welsh, Irish, Maltese
and more have their own (the table is in
[localization_prefs_time.md](localization.md#localization-formats-typed-prefs-fixed-timestep)). A game sets the rule of any
other language in C++:

```cpp
okay::Localization::SetPluralRule("ru", [](double n) {
    using F = okay::Localization::PluralForm;
    const long i = (long)n;
    if (i % 10 == 1 && i % 100 != 11) return F::One;
    if (i % 10 >= 2 && i % 10 <= 4 && (i % 100 < 12 || i % 100 > 14)) return F::Few;
    return F::Many;
});
```

### The Localization window

**View > Editors & Tools > Localization** shows the table as a grid: a row per
key, a column for its note and one per language.

- Click a cell and type; the scene's texts follow as you type. Leaving the cell
  saves the table and makes one undo step: Ctrl+Z and Ctrl+Y undo and redo table
  edits while the window has focus. A line break shows as `\n` in a cell.
- The bars above the grid show how much of each language is translated; hover
  one for the count (including missing texts and placeholder mismatches), click
  it for the language's name, font and default, double-click it to preview it.
- **+ Key** adds a key with its text in the default language. Double-click a key
  (or right-click it) to rename it; texts in the scene that use the key follow
  the rename. Right-click > Delete removes it.
- **+ Language** adds a language by its code (`fr`, `de`, `ja`, `pt-BR`) and name.
- The search box matches keys, notes and texts. The filter shows every key, the
  keys **missing in** one language, **placeholder mismatches** (`{0}` / `{name}`
  different from the source text), text that is **same as source**, **unused**
  keys, or **reported issues**.
- Cells are coloured: red is missing, amber is a placeholder mismatch, blue is
  the same as the source, purple is extra text. Hover a coloured cell for the
  reason.
- Under the filter, a line reports missing translations and placeholder
  mismatches (and any other check warning: plurals, braces, length, whitespace).
  **Show report** lists them, with one line per language that still has gaps;
  click a row to select that key. **Scan project** (and **Rescan**) reads
  scripts, scenes, prefabs, dialogue and quests, plus the open scene. After a
  scan, **Unused** hides keys something mentions, and the report adds keys the
  project uses that the table does not have. Built-in keys (`pause.`, `dlg.`,
  `quest.` and the rest) are not called unused.
- Select a key to see **Find in Scene**: a button per object that uses it, which
  selects the object. When **Pseudo-localize** is on, or the preview language is
  written right to left, the same row shows that key's preview: accented and
  longer, in `[!!! ... !!!]` markers, and mirrored for Arabic, Hebrew, Persian
  and Urdu. The preview does not change the table. The Scene view still follows
  the toolbar language (its pseudo form uses plain brackets) and still draws
  right-to-left text left to right.
- **Add Dialogue & Quest Text** adds a key for every dialogue line, choice and
  speaker and every quest title, description and objective in the project, with
  the text typed in the graph or quest (see [Dialogue and quests](#dialogue-and-quests)).
- **Pseudo-localize** previews every localized text in the Scene view, accented
  and about 30% longer, in brackets: `[Šţåŕţ~~]`. Text that gets cut off, and
  text that never went through the table (it stays plain), stand out. The
  selected key's own line is the marked form from the preview tool
  (`[!!! Šţåŕţ~~ !!!]`), placeholders included.

The toolbar's language menu (beside **Build**) switches the preview language of
the Scene and Game views, and of Play.

#### CSV for translators

**Export CSV...** writes the table as a spreadsheet: a header row
`key,note,en,fr,ja` and a row per key, UTF-8 with a byte order mark so
spreadsheet programs keep accents. **Import CSV...** reads one back: the header
row names the columns (`key`, `note` and language codes, in any order),
separated by commas, semicolons or tabs. Fields with separators, quotes or line
breaks are quoted, with `""` for a quote. Importing adds new keys and languages
and replaces the texts it has; an empty cell keeps the text the table has. The
status line says what the import did (filled, changed, new keys). A column
headed `FR` or `pt_br` fills the table's `fr` or `pt-BR` instead of adding a second
language, and the spreadsheet's "Unicode text" (UTF-16, tab separated) imports too.

### Localized texts in the scene

In the Inspector, **Text** (2D and 3D), **World UI**, **UI Button** (a button
without a child Text), **UI Toggle**, **UI Tooltip** and **UI Input Field** (its
placeholder) have a **Localized** box. Ticking it uses the key whose default
text is the component's text, or adds a key named after the object
(`ui.score_label`) with that text. The key field has a picker listing the
table's keys, and shows the key's default-language text under it.

Three components localize their own words with a **Localized** switch, which
also adds their keys to the table with the words they had:

| Component | Keys |
|---|---|
| Pause Menu | `pause.title`, `pause.resume`, `pause.restart`, `pause.main_menu`, `pause.quit`, `pause.volume`, `pause.music`, `pause.effects`, `pause.hint` |
| Hold Prompt | `hold.pickup` (`{0} to pick up`), `hold.drop` (`{0} to drop`), `hold.either` (`{0} or {1}`); `{0}` and `{1}` are the keys |
| Loading Screen | its title, subtitle and tips become keys (`loading.<object>.title`...) |

Localized texts are saved in the scene file as a `loc` record after the
components; scenes without one load as before.

### Dialogue and quests

Dialogue lines, choices and speaker names, and quest titles, descriptions and
objectives, use keys made from the graph and the quest:

| Text | Key |
|---|---|
| A line (node 12 of the graph "Smith") | `dlg.smith.12` |
| Its second choice | `dlg.smith.12.2` |
| A speaker | `speaker.smith` |
| A quest's title / description | `quest.<id>.title` / `quest.<id>.desc` |
| Its first objective | `quest.<id>.obj1` |

Until the table has a key, players see the text typed in the graph or quest, so
a game can be written first and translated later. The dialogue box's own words
are `dialogue.talk` (`{0} to talk`), `dialogue.continue` and `dialogue.title`;
the quest tracker, journal and notifications use `quest.started`,
`quest.completed`, `quest.failed`, `quest.objective`, `quest.objective_done`,
`quest.optional`, `quest.tracker`, `quest.journal`, `quest.tabs`
(`Active|Completed|All`), `quest.badge.active` / `tracked` / `completed` /
`failed`, `quest.track`, `quest.none_active`, `quest.none` and `quest.close`.
**Add Dialogue & Quest Text** adds all of them with their English.

### Scripting

OkayScript:

```
start() {
    title = loc("menu.start")
    coins = loc("shop.coins", 3)   // "3 coins"
    hello = loc("greet", map_set(map(), "player", "Ana"))   // "Hello, {player}!" -> "Hello, Ana!"
    for c in languages() { log(c + " " + language_name(c)) }
    set_language("fr")   // true when the table has it; remembered for the next run
}

function on_language_changed(code) {
    log("now in " + code + ": " + loc("menu.start"))
}
```

C#-style scripts:

```csharp
class LanguageMenu : Behaviour {
    void Start() {
        string title = Localization.Get("menu.title");
        string coins = Localization.Get("shop.coins", 3);   // "3 coins"; up to three arguments
        foreach (string code in Localization.Languages)
            Debug.Log(code + " " + Localization.LanguageName(code));
        Localization.SetLanguage("ja");   // true when the table has it; remembered
    }
    public void OnLanguageChanged() {   // event methods take no arguments: read Localization.Language
        Debug.Log("Language: " + Localization.Language);
    }
}
```

Visual scripts: **Localize** (setting: the key; inputs `{0}`, `{1}`, `{2}`; out
Text), **Get Language** (out Code), **Set Language** (a code typed in, or wired
into Code) and the event **On Language Changed** (Event Name is the new code).

C++: `okay::Localization::Get(key)`, `okay::Localize(key, args...)`,
`Localization::SetLanguage(code)`, `Localization::Language()`,
`Localization::Languages()`, and `Localization::AddListener(fn)` to hear about
changes (`okay/Core/Localization.hpp`). A component that shows text of its own
keeps an `okay::LocalizedText` (`okay/Core/Localization.hpp`) and calls
`loc.Apply(field)` before drawing: it fills the field when the key, the
language or the table changed.

### The player's language

Build Game copies `strings.okayloc` (and the fonts its languages name) into the
game's data folder. At start the player loads it and picks the language: the
one saved in the player's preferences (key `loc.language`, written by
`set_language` / `Localization.SetLanguage`), else the first of the operating
system's preferred languages the table has (`fr-CA` matches `fr-CA`, then `fr`, then
`fr-FR`), else the default language. Chinese keeps its script: `zh-TW`, `zh-HK`, `zh-MO`
and `zh-Hant-TW` pick `zh-Hant`, `zh-CN`, `zh-SG` and `zh-Hans-CN` pick `zh-Hans`
(a table with one plain `zh` serves both).

### Fonts

Text is drawn from TrueType fonts whose glyphs are baked on demand, so any
script the font has works: accented Latin, Greek, Cyrillic, Japanese, Chinese,
Korean... When the font a text uses lacks a character the text needs, the engine
draws that text with the first font that has them all:

1. the font of the current language (`font "..."` in the table, set in the
   Localization window by clicking the language's bar),
2. the built-in UI font (Latin, Greek, Cyrillic),
3. a font installed on the machine (Segoe UI, Yu Gothic, Microsoft YaHei and
   Malgun Gothic on Windows; Arial Unicode, Hiragino and PingFang on macOS;
   DejaVu Sans, Noto Sans CJK, Droid Sans Fallback and WenQuanYi on Linux).

The switch is logged once (`Text: the 8x8 pixel font lacks characters this text
(ja) needs; drawing it with ...`); when no font has a character, a warning names
it once. Installed fonts differ between machines, so a game that ships in
Japanese, Chinese or Korean should give those languages a font (a font file
inside the project is copied by Build Game).

- The 8x8 pixel font has ASCII only; texts using it switch to a TrueType font
  for other characters.
- The dialogue box, quest tracker, journal and toasts draw with an atlas of the
  built-in UI font that has ASCII and Latin-1; a text with other characters is
  drawn with the fallback font instead.
- Right-to-left scripts (Arabic, Hebrew) draw left to right, unshaped. The engine has no text
  shaping or reordering; `LocMirrorText` (`okay/Core/LocPreview.hpp`) only previews how a
  layout would look.

### More

[localization_tools.md](localization.md#localization-tools) covers the tools around the tables:
the translation grid model, CSV and JSON import and export, the table checker
(placeholders, plurals, whitespace, length), the unused and undefined key scanner,
language previews with pseudo-localization, and the font coverage report.

### What changed (second pass)

- **Keys for names in other scripts.** `Localization::KeyPart` turned every byte of a non-ASCII
  letter into `_`, so all Cyrillic or Chinese names of one length gave the same key
  (`speaker.______`) and a dialogue showed another character's name. Letters of other
  scripts now stay (UTF-8 unchanged); ASCII names give the same keys as before. A table that
  already holds keys made from such names needs them renamed (**Add Dialogue & Quest Text**
  adds the new ones).
- **Language matching.** `PickLanguage` gave a `zh-TW` player the first Chinese table
  language, usually Simplified. It now reads script and region: Traditional and Simplified are
  never swapped, a plain `fr` beats another region's `fr-FR` for `fr-CA`, and `Get` walks the
  parents of a language (`zh-Hant-TW` -> `zh-Hant` -> `zh`) before the default.
- **Plural rules** for Croatian, Serbian, Bosnian, Slovenian, Lithuanian, Latvian, Romanian,
  Hebrew, Hindi and the languages that count 0 as one, Welsh, Irish, Maltese, Icelandic,
  Macedonian, Danish, and no plural for Lao, Burmese, Khmer, Javanese. They fell back to
  one/other, which is wrong for "21 coins" in Croatian.
- **Placeholders.** `{0, number}`, `{0, number, integer|percent|#,##0.00}`,
  `{0, currency, CODE}` and `{x, select, ...}` (see above). `{0, number}` used to print the
  raw digits.
- **CSV.** String-table import reads UTF-16 ("Unicode text") and matches language headers that
  differ in case or separator (`FR`, `pt_br`); it uses the same reader as the tools'
  `LocTable` (one parser instead of two).
- **Files.** `StringTable::Save` and the `LocTable` CSV and JSON writers use the shared
  crash-safe write: a crash or full disk leaves the old file.

Not changed: right-to-left shaping and reordering (needs a shaping library and glyphs from a font
that has them) and the Latin-1-only atlas of the dialogue and journal drawing in the player and
editor hosts, which falls back to a TrueType font for other characters as described above.

## Localization tools

*Was `docs/localization_tools.md` until 2026-11-20.*

The tools a translation workflow needs on top of the string tables in
[localization.md](localization.md#localization-1): a grid of keys by languages with a state for
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

### Loading and the grid

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

#### Loading a folder

`LoadFolder` reads the `.okayloc`, `.csv` and `.json` files directly in a folder, in
file name order (not its subfolders), and merges them: languages and keys are
added, non-empty texts overwrite earlier ones. A file that fails to read is
reported in `lt.warnings` and skipped; the call fails only when nothing could be
read. A `.json` file of one language (`fr.json`: `{"menu.start": "Commencer"}`) is
that language by its file name. `lt.files` lists what was read.

### CSV

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

### JSON

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

### The checker

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

### Which keys the project uses

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

### Previews and pseudo-localization

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

### Font coverage

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

### In the Localization window

**View > Editors & Tools > Localization** calls these tools (see
[localization.md](localization.md#localization-1)):

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

## Localization formats, typed Prefs, fixed timestep

*Was `docs/localization_prefs_time.md` until 2026-11-20.*

Core additions in `okay/Core/Localization.hpp`, `Prefs.hpp` and `Time.hpp`. Tests: `tests/test_loc_prefs_time.cpp` and `tests/test_core_audit.cpp`.

### Plural rules

`Localization::Plural(lang, n)` returns a CLDR category (zero, one, two, few, many, other) on the absolute value of `n`.

| Language | Rule |
| --- | --- |
| en, de, nl, sv and others | 1 is one, everything else other |
| fr, pt (pt-BR) | 0 up to (not including) 2 is one; whole millions many |
| pt-PT | exactly 1 is one; whole millions many |
| es, it, ca | 1 is one; whole millions many |
| ru, uk, be | one (1, 21...), few (2-4, 22-24...), many (0, 5-20, 25...), fractions other |
| pl | 1 one; 2-4 (not 12-14) few; other whole numbers many; fractions other |
| cs, sk | 1 one; 2-4 few; fractions many; else other |
| ar | 0 zero, 1 one, 2 two, 3-10 (mod 100) few, 11-99 many, else other |
| hr, sr, bs | one (1, 21, 101...), few (2-4, 22-24...), other (0, 5-20, 11-14, 25...); fractions other |
| sl | one (1, 101...), two (2, 102...), few (3, 4, 103... and fractions), other |
| lt | one (1, 21...), few (2-9, 22-29...; not the teens), other; fractions many |
| lv | zero (0, 10, 20, 11-19...), one (1, 21...), other |
| ro | 1 one; 0 and 2-19 (mod 100) few; other; fractions few |
| he | 1 one, 2 two, other; fractions below 1 one |
| hi, bn, fa, gu, kn, zu, am | 0 and 1 are one (fractions below 1 too), else other |
| cy, ga, mt | Welsh (0 zero, 1 one, 2 two, 3 few, 6 many), Irish (1, 2, 3-6 few, 7-10 many), Maltese |
| is, mk, da | is and mk: one for 1, 21, 31... (not 11); da: 1 is one, fractions 0 to 2 too |
| ja, zh, ko, vi, th, id, ms, lo, my, km, jv | always other |

`SetPluralRule(lang, fn)` still overrides any of them.

`Localization::PluralText(key, count, extraArgs)` looks the text up in the string table as `key.<form>` (`coins.one`, `coins.few`...). Order: the form in the current language, `key.other` in the current language, then the same two in the default language, then `key` itself as an inline `{0, plural, ...}` pattern. `{count}` and `{0}` are the number; extra args are `{1}`, `{2}`. Pseudo-localization applies as for Get.

### Fallback chain

`Get`, `TryGet`, `Localize` now fall back through the parents of the language -> default language -> key (`pt-BR` -> `pt` -> default; `zh-Hant-TW` -> `zh-Hant` -> `zh` -> default), each miss reported once. A language never borrows from a sibling script (`zh-Hans` does not fall back to `zh-Hant`). `LocArg` integers keep their exact digits (a 64-bit id used to be rounded through a double).

### Formatting

`FormatNumber(value, decimals, lang)`, `FormatCurrency(amount, "EUR", lang)`, `FormatPercent(fraction, decimals, lang)`, `FormatDate(y, m, d, Short|Long, lang)`, `FormatTime(h, m, lang)`. An empty `lang` means the current language. Separators and symbol placement are built in for en, de, fr, es, it, pt (BR / PT), ru, pl, cs and similar, ja, zh, ko; unknown languages format like English. Currency decimals follow the currency (JPY, KRW none; KWD three). A value that rounds to zero never prints `-0`. These are formatting helpers, not a full CLDR: month names are built in for en, de, fr, es, pt, ru, pl.

### Typed Prefs

`SetBool/GetBool`, `SetVec3/GetVec3`, `SetColor/GetColor` join the int / float / string accessors; every getter takes a default for a missing or unparsable key. `HasKey`, `DeleteKey`, `DeleteAll` are aliases of `Has`, `Delete`, `Clear`. Values stay text in the file (`1`/`0`, `x,y,z`, `r,g,b,a`) so the file format is unchanged and older builds read it.

Prefs files are written whole or not at all (temp file, flush, rename), keys in order, and the previous good file is kept as `<file>.bak`. `Load` reads CR LF files, refuses a cut-off or zero-filled file and then reads `<file>.bak` (`Prefs::LastLoadUsedBackup()`; `Prefs::LastError()` says why a load or save failed or fell back). Every `Prefs` call may be made from any thread; a listener runs on the thread that made the change, outside the lock. See [save_games.md](save_games.md#what-changed-hardening-pass).

`Prefs::AddListener(fn)` / `RemoveListener(id)`: `fn(key)` runs after a Set changes a value (setting the same value is silent), after a delete, once per key on `DeleteAll`, and with `""` after `Load`.

### Fixed timestep

`Time::FixedDeltaTime()` (default 0.02) and `SetFixedDeltaTime`. `FixedTimestep` is the accumulator for gameplay code:

```cpp
FixedTimestep fixed;   // step from Time::FixedDeltaTime(), 5 steps at most per frame
fixed.Run(Time::DeltaTime(), [&](float dt) { Simulate(dt); });
Draw(Lerp(prev, cur, fixed.Alpha()));
```

Feed it `Time::DeltaTime()` to follow the time scale and pause, or `UnscaledDeltaTime()` to ignore them. Time beyond `MaxSteps` is dropped (`DroppedTime()`), not owed. It uses the same scheme as `Scene::StepPhysics` (accumulate, cap, step, alpha); scene physics is untouched and a test checks both give the same step counts.

### Core audit fixes

- `Mathf::SmoothDamp` with `deltaTime == 0` (paused) made the velocity NaN; it now returns `current`.
- `Mathf::Repeat` / `PingPong` with length 0 gave NaN.
- `Quat::LookRotation` with forward parallel to up (looking straight up/down) was degenerate; zero forward gives Identity.
- `Quat::Normalized` of a NaN quaternion passed the NaN on; now Identity.
- UI markup colors (`UIDocument`) wrapped byte values (`300` became 44, `-1` 255); they clamp.
- New `Color::Parse` ("#RGB", "#RRGGBB", "#RRGGBBAA", "r,g,b[,a]").
