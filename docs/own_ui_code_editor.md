# OkayUI::CodeEdit: our own code editor widget (replaces ImGuiColorTextEdit)

Own code. Part of the "replace every third-party library" work (`docs/own_code_roadmap.md`, rows ImGuiColorTextEdit and Dear ImGui; toolkit background in `docs/own_ui_toolkit.md`, section 4.29).

**Status: built and tested headless, not used by the editor.** OkayUI is off by default and the editor still draws its script editor with the vendored `editor/vendor/TextEditor.*` on Dear ImGui. Nothing was wired in. The widget is what the script editor's port will use once the editor itself runs on OkayUI.

## Where it is

| File | What |
|---|---|
| `okayui/include/okay/UI/OkayUICodeEdit.hpp` | the API (`OkayUI::CodeEdit`, `CodeLanguage`, `CodePalette`, `CodeMarker`, `CodeToken`) |
| `okayui/src/OkayUICodeEdit.cpp` | the editor, the highlighter, the drawing (one file added to the `okay_ui` library) |
| `okayui/tests/test_okayui_codeedit.cpp` | behaviour, widget (synthetic mouse and keys), highlighting, golden image, 100k-line performance |
| `okayui/tests/test_okayui_codeedit_fuzz.cpp` | model fuzz and widget fuzz |
| `okayui/tests/golden/codeedit_highlight.png` | the golden image |

Hooks added to the existing OkayUI files (three tiny functions, nothing else in `OkayUI.cpp` changed): `OkayUI::GetFrameInput()` (this frame's raw `Input`), `OkayUI::GetKeyboardFocusId()` and `OkayUI::SetKeyboardFocusId(id)` (takes the keyboard and marks the click as taken so the click does not drop focus again). A widget in another file needs them to read typed text and key edges and to own the keyboard.

## How it is used

```cpp
OkayUI::CodeEdit ed;                       // one per open script; owns its text, caret, undo history
ed.SetLanguage(OkayUI::OkayScriptLanguage());
ed.SetText(source);
ed.SetBreakpointCallback([](int line, bool set) { /* store it */ });
// every frame, inside an OkayUI window:
bool changed = ed.Draw("script", 0, 0);    // 0, 0 = fill what is left of the window
ed.SetMarkers({{line, colStart, colEnd, "unknown name 'x'"}});   // compile errors
```

`Draw` reads the frame's input itself (mouse, wheel, typed text, key edges, `ctrl` and `shift`), edits, draws only the visible lines, and returns true when the text changed. Every editing operation is also a public method (`InsertText`, `Backspace`, `NewLine`, `Tab`, `Undo`, `Move`, `FindNext`, ...), so the editor can be driven and tested with no input at all.

Keys the widget acts on while it has the keyboard: typed text; Enter; Backspace and Delete (with Ctrl: by word); Left, Right, Up, Down (Ctrl: by word on Left and Right); Home (first non-blank, then column 0) and End (Ctrl: start and end of text); PageUp and PageDown; Tab and Shift+Tab; Ctrl+A; Ctrl+C, Ctrl+X, Ctrl+V (as the host's `Input::copy`, `cut`, `paste` edges); Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y; Escape drops the selection. Shift extends the selection on every movement key. Find, replace and go-to-line are methods; the dialog (Ctrl+F, Ctrl+G) is the caller's.

Mouse: click places the caret, Shift+click extends, drag selects (and scrolls when the pointer leaves the view), double click selects a word, triple click a line, a click on the line numbers selects the line, a click in the narrow column left of the numbers toggles a breakpoint, the wheel scrolls three lines (Shift+wheel sideways), both scroll bars can be dragged or clicked.

## HAVE and GAP against what the script editor uses of ImGuiColorTextEdit

The list is from `grep "te\." editor/main.cpp` (the script editor's calls) plus the features of the library it shows to the user.

| ImGuiColorTextEdit feature the script editor uses | Status | Here |
|---|---|---|
| Multi-line editing: typing, Backspace, Delete, Enter | HAVE | `InsertText`, `Backspace`, `DeleteForward`, `NewLine` |
| Auto-indent (keeps indent, deeper after `{`) | HAVE | `NewLine`; also after `(` and `[`, and `{\|}` opens a blank line between |
| Tab and Shift+Tab, tab size | HAVE | `Tab`, `SetTabSize`, `SetInsertSpaces`; multi-line selection indents and outdents every line |
| Line numbers | HAVE | gutter, `SetShowLineNumbers` |
| Caret, selection by mouse and keys, word and line selection | HAVE | see above; `SetSelection`, `SelectAll`, `SelectWordAt` (the library's `SelectWordUnderCursor`), `SelectedText`, `HasSelection` |
| `SetCursorPosition` / `GetCursorPosition` | HAVE | `SetCaret` / `Caret()` (line and byte offset; the library counted columns) |
| `LineLength`, `GetTotalLines`, `GetCurrentLineText`, `GetTextLines`, `GetText` / `SetText` | HAVE | `LineLength`, `LineCount`, `Line(Caret().line)`, `Line(i)`, `GetText`, `SetText` |
| `InsertTextUndoable`, `Delete` | HAVE | `InsertText`, `DeleteForward`; both undoable |
| Undo and redo | HAVE | grouped: a word of typing is one step, a run of Backspaces one, Enter, paste, cut, indent, replace-all are one each; the caret and selection come back; 2000 steps |
| Copy, cut, paste | HAVE | through `OkayUI::SetClipboardText` / `GetClipboardText` and `Input::copy` / `cut` / `paste` (see "limits" below) |
| `IsTextChanged` | HAVE | `Draw` returns it; also `Version()` |
| Syntax highlighting with a language definition | HAVE | `CodeLanguage` (keywords, types, known words, line and block comments), `OkayScriptLanguage()`, `CLikeLanguage()`; or any callback `CodeHighlighter` (token ranges with a colour id, one line at a time with a state that carries block comments across lines). Not regex based |
| `SetUserTypes` (classes and enums declared in the script) | HAVE | `SetUserTypes` |
| Known identifiers (VM built-ins in a different colour) | HAVE | `CodeLanguage::known`, colour `CodeColor_Known`. The hover tooltip with the signature (the library had it) is the caller's: `LineTokens` and `ScreenToPos` give what is under the pointer |
| `SetColorizerEnable` | HAVE | `SetHighlighter(nullptr)` turns colouring off |
| Palette | HAVE | `CodePalette`, `DefaultCodePalette()` is the script editor's Darcula-flavoured one |
| `SetErrorMarkers` | HAVE | `SetMarkers`: a gutter mark, a wash over the line, a wavy underline under a column range, the message as a tooltip |
| `SetBreakpoints` and the gutter click | HAVE | `SetBreakpoints`, `ToggleBreakpoint`, `SetBreakpointCallback`; breakpoints move with the text |
| Current line highlight | HAVE | while focused with no selection |
| Scrolling: wheel, scroll bars, caret kept visible, `FirstVisibleLine` | HAVE | `ScrollX`, `ScrollY`, `SetScroll`, `GoToLine`, `FirstVisibleLine`, `VisibleLineCount` |
| `RowHeight`, `CharWidth`, `TextStartX`, `ContentOrigin`, `CaretScreenPosBelow`, `ScreenToCoords` | HAVE | `LineHeight`, `CharWidth`, `GutterWidth` / `ViewRect`, `PosToScreen`, `CaretScreenPosBelow`, `ScreenToPos` |
| `SetHandleKeyboardInputs` (the autocomplete popup owns the keys) | HAVE | `SetHandleKeyboardInput` |
| Read-only mode | HAVE | `SetReadOnly`: moving, selecting and copying work, nothing else |
| Find next and previous, highlight all | HAVE (new) | `SetFind`, `FindNext`, `FindPrev`, `FindCount`, every match in view is tinted |
| Replace, replace all | HAVE (new) | `ReplaceCurrent`, `ReplaceAll` (one undo step) |
| Bracket matching | HAVE (new) | `MatchBracket`; the pair is drawn when the caret is next to one; brackets in strings and comments are skipped |
| 100,000 lines | HAVE | only the visible lines are drawn and tokenised; see numbers below |
| `Render(...)` into an ImGui child | n/a | `Draw` into an OkayUI window |
| `te.tip`, `te.label`, `te.icon`, `te.group`, `te.textBtn`, `te.active` | not the library | those are the editor's own structs |

## Numbers (Release, this machine, 100,000 lines, 6.3 MB of OkayScript with block comments)

From `test_okayui_codeedit` (it prints them):

| What | Time |
|---|---|
| `SetText` of the whole file | about 20 ms |
| first frame at the top | about 1 ms |
| a steady frame at the top, 15 lines drawn | about 0.3 ms (the same frame on a 4 KB text takes the same) |
| jump to the last line (highlighter runs over all 100,000 lines once to learn the block comment state) | about 35 ms |
| a steady frame at the end | about 0.3 ms |
| type a character and a newline in the middle, plus the frame | about 0.5 ms |
| count 100,000 find matches | about 9 ms |
| select all, delete, undo | about 12 ms |
| vertices drawn at the top / at the end | about 27,000 / 36,000 (batch limit 262,144; nothing dropped) |

The test asserts generous limits (a frame under 20 ms, a jump under 2 s, ...) and multiplies them by 20 under ASan or UBSan.

## How it was tested

- `test_okayui_codeedit` (407 checks): every editing operation on its own (insert, replace a selection, Backspace, Delete, word delete, Enter with auto-indent in each case, Tab and Shift+Tab on one and many lines, tabs and spaces), movement (arrows across line ends, vertical movement keeping the column through short lines, tabs and wide characters, smart Home, End, document ends, words over punctuation and blanks, page moves, Shift extending, collapsing a selection), selection edge cases with UTF-8 (two, three and four byte characters never split, positions inside a character snap to its start, backwards selections), clipboard (round trip, multi-line, CRLF, control characters, read-only, a selection too big for the clipboard is not cut), undo and redo (grouping, caret and selection restored, redo dropped by a new edit, one step for compound edits, the 2000 step limit), find (case, wrap both ways, UTF-8 needles, replace, replace all as one step), highlighting (each token kind, escapes, `#` comments in OkayScript, `"#fff"` stays a string, block comments across lines, an unterminated string, edits re-highlighting the lines below, the highlighter called once per line and cached), bracket matching (nesting, strings and comments, unbalanced, follows edits), breakpoints and markers moving with the text.
- The same widget driven with synthetic input through `Sim`: focus on click and loss on a click elsewhere, typing without focus does nothing, click, Shift+click, drag, double and triple click, clicks past the ends, every key above, read-only, PageUp and PageDown, the caret staying in view, the wheel (clamped at both ends, ignored when the pointer is elsewhere), Shift+wheel, both scroll bars dragged, the track clicked, the breakpoint gutter and its callback.
- Pixels: breakpoint dot, error mark and wash, line numbers, selection over the newline, comment and keyword colours, the caret, no caret when unfocused, plus the golden image `codeedit_highlight.png` (`OKAYUI_UPDATE_GOLDEN=1` regenerates it).
- `test_okayui_codeedit_fuzz`: (1) editing against a flat-string reference (random selections, inserts, Backspace, Delete, cut, paste, undo, redo, tab, find), text compared after every step, invariants after every step (no `\n` or `\r` inside a line, valid UTF-8, caret and anchor inside the text and on character boundaries, scroll finite and not negative, breakpoints sorted, `GetText`/`SetText` round trip), an undo followed by a redo changes nothing, undo of everything gives the first text and redo of everything the last; (2) the widget under random mouse moves (including NaN and infinity), clicks, wheel, junk text with invalid UTF-8, key edges, held keys, Ctrl and Shift, blocked input, random sizes, random calls of the API, rasterised now and then. 6 seeds each, `OKAYUI_FUZZ_ITERS` (default 1500) steps, `OKAYUI_FUZZ_SEED`. Run under `-fsanitize=address,undefined` with 4000 steps on three more seeds: clean.

## Limits and decisions, said bluntly

- **A monospaced font is assumed.** The cell width is the width of `0`. The 8x8 classic font is monospaced; the launcher look's Roboto is not, and with it the caret and selection would drift. OkayUI has no way yet to choose a code font (family 23 in `own_ui_toolkit.md`); JetBrains Mono is in `shared/` but not hooked up.
- **One character is one cell.** Wide (CJK, emoji) characters are drawn one cell wide and may overlap their neighbour; combining marks take a cell. The 8x8 font has only ASCII glyphs, so other characters show as the font's fallback.
- **The clipboard holds 4095 bytes** (`kClipboardMax`). Copy of a bigger selection is cut; Cut of a bigger selection does nothing (it never deletes text it could not copy). This is the OkayUI clipboard's limit, not the editor's.
- **Columns are byte offsets** (on character boundaries). The library counted columns; a port has to convert (`ByteToColumn`, `ColumnToByte`).
- Tab size 1 to 16; tab characters are kept as they are, Tab inserts spaces by default.
- Breakpoints and markers move with inserted and removed lines. A marker on a removed line is dropped. A breakpoint at the line where a cut begins stays there. Undo does not bring a dropped marker or breakpoint back.
- Line endings are normalised to `\n` on input; the caller converts back on save if it wants `\r\n`.
- Typed text is read from `Input::text` only while the widget has focus. The host (not on this branch) still has to send key edges and Ctrl+C / X / V for OkayUI at all.
- Very long lines (hundreds of thousands of characters on one line) are walked from their start each frame; only the visible columns are drawn but the walk is linear in the line length.
- The highlighter state is cached per line from the top; an edit near the top of a 100,000 line file re-tokenises downwards only as far as the view needs.

## NOT DONE

- **IME** (composition text, candidate window): the widget shows the preedit, takes the commit and reports the caret rectangle (`docs/own_ui_toolkit.md` section 4.31); no host sends the events yet, so in a real window typed text still arrives as finished characters only.
- **Ligatures**, **bidirectional text**, **wide-character cell widths**, **combining-mark clusters**.
- **Multiple cursors** and **column (block) selection**.
- **Code folding.**
- **Word wrap.** Long lines scroll sideways.
- **Auto-close brackets and quotes**, **auto-dedent when typing `}`**, **toggle comment (Ctrl+/)**, **move line up and down**, **duplicate line**: not in the library the script editor uses either, or done by the editor itself on top of the API (`FormatOkayScript`, comment toggle in `editor/main.cpp` work on the text through `SetSelection`, `InsertText`).
- **Autocomplete popup and the hover tooltip**: the editor draws them itself (they use `CaretScreenPosBelow`, `ScreenToPos`, `LineTokens`); the widget only offers the geometry and `SetHandleKeyboardInput`.
- **Drag and drop of selected text**, **drop of files onto the editor**.
- **Show whitespace**, **minimap**, **indent guides**, **matching word highlight**.
- **Replace across several lines of the needle** (a find text with a newline matches nothing).
- **Mouse cursor shape** is set (`MouseCursor_TextInput` over the text) but, like the rest of OkayUI, only the host shows it.
- **Not verified**: a real window on screen with the SDL_Renderer or Direct3D 11 backend (only the headless draw-list rasteriser was used; it skips textured quads, so the launcher look's Roboto glyphs are not in the golden), Windows and macOS builds, a real keyboard (host key repeat, dead keys, layouts), the real system clipboard, the editor's script editor actually running on it.
