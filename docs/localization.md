# Localization

A project keeps every piece of text players read in one string table,
`strings.okayloc` at the project root. Each entry has a key (`menu.start`), an
optional note for translators, and a text per language. Texts in the scene, the
pause menu, the hold prompt, the loading screen, dialogue and quests show the
text of their key in the current language, and change the moment the language
does. Scripts and visual scripts read keys too.

The language a game starts in is the one the player picked last time, else the
operating system's language when the table has it, else the table's default.

## The string table

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
text en shop.coins "{count, plural, one {# coin} other {# coins}}"
text fr shop.coins "{count, plural, one {# pièce} other {# pièces}}"
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

### What players see when a text is missing

`Localization::Get("key")` returns the key's text in the current language. When
that language has no text, it shows the default language's text; when that is
missing too, it shows the key itself. Each miss is logged once (per language and
key), as a warning: `Localization: "shop.coins" has no fr text; showing the en one`.

## Arguments and plurals

Placeholders are filled from the arguments a text is asked for with:

- `{0}`, `{1}`... are the arguments in order: `loc("score", 120)` with
  `"Score: {0}"` gives `Score: 120`.
- `{name}` is a named argument: in OkayScript pass a map,
  `loc("greet", map_set(map(), "player", "Ana"))` with `"Hello, {player}!"`.
  In C++, `LocArg::Named("player", "Ana")`.
- `{n, plural, one {...} other {...}}` picks a form by the number: `#` inside
  it is the number. `=0 {...}`, `=1 {...}` match exact numbers first. The forms
  are `zero`, `one`, `two`, `few`, `many` and `other`.
- `{{` and `}}` are literal braces. A placeholder without an argument stays as
  written, so a missing argument is easy to spot.

Built-in plural rules: English and most languages use `one` for exactly 1;
French uses `one` for 0 and 1; Japanese, Chinese and Korean always use `other`.
A game sets the rule of any other language in C++:

```cpp
okay::Localization::SetPluralRule("ru", [](double n) {
    using F = okay::Localization::PluralForm;
    const long i = (long)n;
    if (i % 10 == 1 && i % 100 != 11) return F::One;
    if (i % 10 >= 2 && i % 10 <= 4 && (i % 100 < 12 || i % 100 > 14)) return F::Few;
    return F::Many;
});
```

## The Localization window

**View > Editors & Tools > Localization** shows the table as a grid: a row per
key, a column for its note and one per language.

- Click a cell and type; the scene's texts follow as you type. Leaving the cell
  saves the table and makes one undo step: Ctrl+Z and Ctrl+Y undo and redo table
  edits while the window has focus. A line break shows as `\n` in a cell.
- Cells a language has no text for are tinted. The bars above the grid show how
  much of each language is translated; hover one for the count, click it for the
  language's name, font and default, double-click it to preview it.
- **+ Key** adds a key with its text in the default language. Double-click a key
  (or right-click it) to rename it; texts in the scene that use the key follow
  the rename. Right-click > Delete removes it.
- **+ Language** adds a language by its code (`fr`, `de`, `ja`, `pt-BR`) and name.
- The search box matches keys, notes and texts. The filter shows every key, the
  keys **missing in** one language, or the **unused** keys: keys that no text in
  the open scene, no script or visual script, scene, prefab, dialogue graph or
  quest file mentions (**Rescan** after editing scripts).
- Select a key to see **Find in Scene**: a button per object that uses it, which
  selects the object.
- **Add Dialogue & Quest Text** adds a key for every dialogue line, choice and
  speaker and every quest title, description and objective in the project, with
  the text typed in the graph or quest (see [Dialogue and quests](#dialogue-and-quests)).
- **Pseudo-localize** previews every localized text accented and about 30%
  longer, in brackets: `[Šţåŕţ~~]`. Text that gets cut off, and text that never
  went through the table (it stays plain), stand out.

The toolbar's language menu (beside **Build**) switches the preview language of
the Scene and Game views, and of Play.

### CSV for translators

**Export CSV...** writes the table as a spreadsheet: a header row
`key,note,en,fr,ja` and a row per key, UTF-8 with a byte order mark so
spreadsheet programs keep accents. **Import CSV...** reads one back: the header
row names the columns (`key`, `note` and language codes, in any order),
separated by commas, semicolons or tabs. Fields with separators, quotes or line
breaks are quoted, with `""` for a quote. Importing adds new keys and languages
and replaces the texts it has; an empty cell keeps the text the table has.

## Localized texts in the scene

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

## Dialogue and quests

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

## Scripting

OkayScript:

```
start() {
    title = loc("menu.start")
    coins = loc("shop.coins", map_set(map(), "count", 3))   // "3 coins"
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
        string coins = Localization.Get("shop.coins", 3);   // up to three arguments
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

## The player's language

Build Game copies `strings.okayloc` (and the fonts its languages name) into the
game's data folder. At start the player loads it and picks the language: the
one saved in the player's preferences (key `loc.language`, written by
`set_language` / `Localization.SetLanguage`), else the first of the operating
system's preferred languages the table has (`fr-CA` matches `fr-CA`, then `fr`),
else the default language.

## Fonts

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
- Right-to-left scripts (Arabic, Hebrew) draw left to right, unshaped.
